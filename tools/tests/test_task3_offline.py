"""Offline verification of Task 3 (SPDM P4: ML-DSA-87 / ML-KEM-1024).

No board needed. Checks credentials, negative trust anchors, the fault-injection switch,
the responder patch, the recorded hardware evidence and the measurement tooling.

Run:   py -3.13 -m unittest discover -s C:\\Users\\kpato\\KP\\Hackathon\\tools\\tests -v
Slow build matrix (rebuilds the responder for SPDM_FAULT_INJECT=0..6, ~6 min, leaves FI=0 built):
       set RUN_BUILD_TESTS=1 before running.
"""
import csv
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import ec, mldsa
from cryptography.hazmat.primitives.serialization import Encoding

HACK = Path(r"C:\Users\kpato\KP\Hackathon")
SPDM = HACK / "PSOC_C3_SPDM_v2" / "PSOC_C3_SPDM" / "SPDM"
RESP = SPDM / "psoc-spdm-responder"
HOST = SPDM / "psc3-provisioning-host"
RES = HACK / "results"
TOOLS = HACK / "tools"
sys.path.insert(0, str(TOOLS))
import perf_summary  # noqa: E402

POS = RES / "task3_P4_20261111_1449"
NEG = RES / "task3_NEG_P4_20261111_1533"
ROOTS = RES / "task3_neg_roots"
# Task 3 evidence identity (archived before the edge tests re-provision the board)
BLOB = (RES / "task3_cred_p4_original" / "spdm_cert_chain.bin")
if not BLOB.exists():
    BLOB = HOST / "test" / "_work" / "spdm_p4" / "spdm_cert_chain.bin"
BASE_COMMIT = "4c218834dfd1bafa47914317dcaf4d6af978e31b"
PATCH = HACK / "patches" / "psoc-spdm-responder_task3.patch"
NM = Path(r"C:\Users\kpato\Infineon\Tools\mtb-gcc-arm-eabi\14.2.1\gcc\bin\arm-none-eabi-nm.exe")
NS_ELF = RESP / "proj_cm33_ns" / "build" / "APP_KIT_PSC3M6_EVAL" / "Release" / "proj_cm33_ns.elf"

LIBSPDM_MAX_CERT_CHAIN_SIZE = 0x4000          # spdm_lib_config_psc3.h (ML-DSA-87 profile)
PAS_CERT_SLOT_BYTES = 30 * 512                # pas_config.h PAS_SLOT_ROWS_INIT {16, 30} (M6)
MLDSA87_PK, MLDSA87_SIG, MLKEM1024_CT = 2592, 4627, 1568
DEVICE_CN = "PSC3 DEV 01"


def der_len(buf, off):
    """Total length (header + content) of the DER TLV starting at off."""
    l0 = buf[off + 1]
    if l0 < 0x80:
        return 2 + l0
    n = l0 & 0x7F
    return 2 + n + int.from_bytes(buf[off + 2: off + 2 + n], "big")


def split_spdm_chain(blob):
    """SPDM cert_chain = Length(2) Reserved(2) RootHash(48) certs[root..leaf]."""
    certs, off = [], 4 + 48
    while off < len(blob):
        n = der_len(blob, off)
        certs.append(blob[off: off + n])
        off += n
    return certs


def verify(issuer_cert, child_cert):
    issuer_cert.public_key().verify(child_cert.signature, child_cert.tbs_certificate_bytes)


# ----------------------------------------------------------------------------- credentials
class TestProvisionedChain(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob = BLOB.read_bytes()
        cls.certs = split_spdm_chain(cls.blob)
        cls.root = x509.load_der_x509_certificate(cls.certs[0])
        cls.leaf = x509.load_der_x509_certificate(cls.certs[-1])

    def test_header_length_and_reserved(self):
        self.assertEqual(int.from_bytes(self.blob[0:2], "little"), len(self.blob))
        self.assertEqual(self.blob[2:4], b"\x00\x00")

    def test_root_hash_is_sha384_of_root(self):
        self.assertEqual(self.blob[4:52], hashlib.sha384(self.certs[0]).digest())

    def test_exactly_root_and_leaf(self):
        self.assertEqual(len(self.certs), 2)
        self.assertEqual(4 + 48 + sum(map(len, self.certs)), len(self.blob))

    def test_all_keys_are_mldsa87(self):
        self.assertIsInstance(self.root.public_key(), mldsa.MLDSA87PublicKey)
        self.assertIsInstance(self.leaf.public_key(), mldsa.MLDSA87PublicKey)

    def test_root_self_signature_and_leaf_signature(self):
        verify(self.root, self.root)
        verify(self.root, self.leaf)

    def test_signature_sizes(self):
        self.assertEqual(len(self.root.signature), MLDSA87_SIG)
        self.assertEqual(len(self.leaf.signature), MLDSA87_SIG)

    def test_issuer_subject_linkage(self):
        self.assertEqual(self.leaf.issuer, self.root.subject)
        self.assertNotEqual(self.leaf.subject, self.root.subject)

    def test_validity_now(self):
        now = datetime.now(timezone.utc)
        for c in (self.root, self.leaf):
            self.assertLessEqual(c.not_valid_before_utc, now)
            self.assertGreater(c.not_valid_after_utc, now)
        self.assertLessEqual(self.leaf.not_valid_after_utc, self.root.not_valid_after_utc)

    def test_fits_buffers(self):
        self.assertLessEqual(len(self.blob), LIBSPDM_MAX_CERT_CHAIN_SIZE)
        self.assertLessEqual(len(self.blob), PAS_CERT_SLOT_BYTES)

    def test_pinned_root_matches_device_root(self):
        self.assertEqual((POS / "root.der").read_bytes(), self.certs[0])
        self.assertEqual((ROOTS / "good_root.der").read_bytes(), self.certs[0])

    def test_device_served_chain_equals_provisioned_blob(self):
        self.assertEqual((POS / "chain.der").read_bytes(), self.blob)

    def test_fi_cert_assumption_cn_unique_and_in_leaf(self):
        hits = [m.start() for m in re.finditer(DEVICE_CN.encode(), self.blob)]
        self.assertEqual(len(hits), 1, "FI CERT hook edits the LAST occurrence; must be unique")
        self.assertGreater(hits[0], 52 + len(self.certs[0]), "occurrence must be inside the leaf")
        cn = self.leaf.subject.get_attributes_for_oid(x509.NameOID.COMMON_NAME)[0].value
        self.assertEqual(cn, DEVICE_CN)

    def test_fi_cert_modification_breaks_leaf_signature(self):
        """What B1 serves: the same chain with the CN's last char -> 'X'; must not verify."""
        b = bytearray(self.blob)
        i = self.blob.rfind(DEVICE_CN.encode()) + len(DEVICE_CN) - 1
        b[i] = ord("X")
        leaf = x509.load_der_x509_certificate(split_spdm_chain(bytes(b))[-1])
        with self.assertRaises(Exception):
            verify(self.root, leaf)


# ----------------------------------------------------------------------------- negative anchors
class TestNegativeRoots(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.good_der = (ROOTS / "good_root.der").read_bytes()
        cls.good = x509.load_der_x509_certificate(cls.good_der)
        cls.leaf = x509.load_der_x509_certificate(split_spdm_chain(BLOB.read_bytes())[-1])

    def test_all_files_present(self):
        for n in ("good_root", "rogue_mldsa87_root", "mldsa65_root", "ec384_root",
                  "malformed_key_root", "truncated_root"):
            self.assertTrue((ROOTS / f"{n}.der").stat().st_size > 0, n)

    def test_rogue_same_name_other_key_selfsigned(self):
        r = x509.load_der_x509_certificate((ROOTS / "rogue_mldsa87_root.der").read_bytes())
        self.assertEqual(r.subject, self.good.subject)
        self.assertIsInstance(r.public_key(), mldsa.MLDSA87PublicKey)
        self.assertNotEqual(r.public_key().public_bytes_raw(), self.good.public_key().public_bytes_raw())
        verify(r, r)
        with self.assertRaises(Exception):
            verify(r, self.leaf)          # the device leaf must not chain to the rogue root

    def test_mldsa65_is_other_parameter_set(self):
        r = x509.load_der_x509_certificate((ROOTS / "mldsa65_root.der").read_bytes())
        self.assertIsInstance(r.public_key(), mldsa.MLDSA65PublicKey)
        self.assertEqual(r.subject, self.good.subject)

    def test_ec384_is_classical(self):
        r = x509.load_der_x509_certificate((ROOTS / "ec384_root.der").read_bytes())
        self.assertIsInstance(r.public_key(), ec.EllipticCurvePublicKey)
        self.assertEqual(r.public_key().curve.name, "secp384r1")

    def test_malformed_one_byte_inside_public_key_only(self):
        bad = (ROOTS / "malformed_key_root.der").read_bytes()
        self.assertEqual(len(bad), len(self.good_der))
        diff = [i for i, (a, b) in enumerate(zip(bad, self.good_der)) if a != b]
        self.assertEqual(len(diff), 1)
        pk = self.good.public_key().public_bytes_raw()
        pk_off = self.good_der.find(pk)
        self.assertTrue(pk_off <= diff[0] < pk_off + MLDSA87_PK)
        m = x509.load_der_x509_certificate(bad)            # still well-formed X.509
        self.assertIsInstance(m.public_key(), mldsa.MLDSA87PublicKey)
        with self.assertRaises(Exception):
            verify(m, self.leaf)

    def test_truncated_is_unparseable(self):
        with self.assertRaises(Exception):
            x509.load_der_x509_certificate((ROOTS / "truncated_root.der").read_bytes())


# ----------------------------------------------------------------------------- FI source switch
FI_FILES = ["proj_cm33_ns/spdm/spdm_responder.c", "proj_cm33_ns/spdm/device_secret.c",
            "proj_cm33_ns/spdm/cryptlib/mlkem.c", "proj_cm33_ns/spdm/spdm_transport_dfu.c"]


class TestFaultInjectionSource(unittest.TestCase):
    def test_makefile_default_off(self):
        mk = (RESP / "proj_cm33_ns" / "Makefile").read_text()
        self.assertRegex(mk, r"SPDM_FAULT_INJECT \?= 0")
        self.assertRegex(mk, r"ifneq \(\$\(SPDM_FAULT_INJECT\),0\)\s*\r?\nDEFINES\+=SPDM_FAULT_INJECT=")

    def test_header_default_off_and_modes(self):
        h = (RESP / "proj_cm33_ns" / "spdm" / "spdm_fault_inject.h").read_text()
        self.assertRegex(h, r"#ifndef SPDM_FAULT_INJECT\s*\r?\n#define SPDM_FAULT_INJECT 0")
        modes = dict(re.findall(r"#define (SPDM_FI_\w+)\s+(\d+)", h))
        self.assertEqual(sorted(map(int, modes.values())), [1, 2, 3, 4, 5, 6])

    def test_every_fi_statement_is_guarded(self):
        """Every line that mentions [FI] or modifies data for FI sits inside an #if on SPDM_FI_ACTIVE / SPDM_FAULT_INJECT."""
        for rel in FI_FILES:
            depth_guard = []
            for no, line in enumerate((RESP / rel).read_text().splitlines(), 1):
                s = line.strip()
                if s.startswith("#if"):
                    depth_guard.append("SPDM_FI_ACTIVE" in s or "SPDM_FAULT_INJECT" in s)
                elif s.startswith("#endif"):
                    depth_guard.pop()
                elif "[FI]" in s:
                    self.assertTrue(any(depth_guard), f"{rel}:{no} unguarded FI code")

    def test_each_mode_has_a_hook(self):
        src = "".join((RESP / f).read_text() for f in FI_FILES)
        for m in ("SPDM_FI_CERT", "SPDM_FI_SIG", "SPDM_FI_TRANSCRIPT", "SPDM_FI_KEM_CT",
                  "SPDM_FI_WIRE_FINISH", "SPDM_FI_WIRE_APP"):
            self.assertIn(f"SPDM_FI_ACTIVE({m})", src, m)

    def test_deployed_build_has_no_fi_code(self):
        """The image currently built (and flashed) must be the clean FI=0 one."""
        data = NS_ELF.read_bytes()
        self.assertNotIn(b"[FI]", data)
        self.assertIn(b"SPDM responder context ready", data)


@unittest.skipUnless(os.environ.get("RUN_BUILD_TESTS") == "1", "set RUN_BUILD_TESTS=1 (rebuilds responder ~6 min)")
class TestBuildMatrix(unittest.TestCase):
    MAKE = r"C:/Users/kpato/ModusToolbox/tools_3.9/modus-shell/bin/make.exe"
    EXPECT = {1: b"[FI] CERT", 2: b"[FI] SIG", 3: b"[FI] TRANSCRIPT", 4: b"[FI] KEM_CT",
              5: b"[FI] WIRE", 6: b"[FI] WIRE"}

    def build(self, fi):
        env = dict(os.environ, CY_TOOLS_PATHS="C:/Users/kpato/ModusToolbox/tools_3.9",
                   HOME=os.environ["USERPROFILE"].replace("\\", "/"),
                   PATH="C:/Program Files/CMake/bin;C:/Users/kpato/ModusToolbox/tools_3.9/modus-shell/bin;"
                        + os.environ["PATH"])
        r = subprocess.run([self.MAKE, "-C", str(RESP).replace("\\", "/"), "build", "-j8",
                            "SPDM_PROFILE=P4", f"SPDM_FAULT_INJECT={fi}"],
                           env=env, capture_output=True, text=True, errors="replace")
        self.assertEqual(r.returncode, 0, r.stdout[-2000:] + r.stderr[-2000:])
        return NS_ELF.read_bytes()

    def test_matrix(self):
        try:
            for fi in range(1, 7):
                with self.subTest(fi=fi):
                    elf = self.build(fi)
                    self.assertIn(self.EXPECT[fi], elf)
                    self.assertIn(f"mode %d".encode(), elf)       # banner format string
                    others = {v for k, v in self.EXPECT.items() if v != self.EXPECT[fi]}
                    for o in others:
                        self.assertNotIn(o, elf, f"FI={fi} contains hook {o}")
        finally:
            elf = self.build(0)
            self.assertNotIn(b"[FI]", elf)


# ----------------------------------------------------------------------------- patch
class TestPatch(unittest.TestCase):
    def test_patch_applies_and_reproduces_working_tree(self):
        tmp = Path(tempfile.mkdtemp())
        wt = tmp / "wt"
        git = lambda *a, cwd=RESP: subprocess.run(["git", *a], cwd=cwd, capture_output=True, text=True)
        r = git("worktree", "add", "--detach", str(wt), BASE_COMMIT)
        self.assertEqual(r.returncode, 0, r.stderr)
        try:
            r = git("apply", str(PATCH), cwd=wt)
            self.assertEqual(r.returncode, 0, r.stderr)
            files = re.findall(r"^diff --git a/(\S+) b/", PATCH.read_text(errors="replace"), re.M)
            self.assertGreaterEqual(len(files), 17)
            for f in files:
                a = (wt / f).read_bytes().replace(b"\r\n", b"\n")
                b = (RESP / f).read_bytes().replace(b"\r\n", b"\n")
                self.assertEqual(a, b, f"patched {f} differs from working copy")
        finally:
            git("worktree", "remove", "--force", str(wt))
            git("worktree", "prune")


# ----------------------------------------------------------------------------- recorded evidence
def windows(run):
    """{test name: (start, end, [perf kv], [all uart lines])} from timeline + UART log."""
    marks = []
    for line in (run / "timeline.txt").read_text().splitlines():
        t, kind, rest = line.split(" ", 2)
        marks.append((datetime.strptime(t, "%H:%M:%S.%f"), kind, rest))
    uart = []
    for line in (run / "device_uart.log").read_text(errors="replace").splitlines():
        try:
            uart.append((datetime.strptime(line[:12], "%H:%M:%S.%f"), line[13:]))
        except ValueError:
            pass
    out = {}
    for i, (t, kind, name) in enumerate(marks):
        if kind != "START":
            continue
        end = marks[i + 1][0] if i + 1 < len(marks) else datetime.max
        lines = [l for (u, l) in uart if t <= u < end]
        perf = []
        for l in lines:
            if "[PERF]" in l:
                toks = l.split("[PERF] ", 1)[1].split()
                if len(toks) == len(perf_summary.FIELDS) and all("=" in x for x in toks):
                    perf.append(dict(x.split("=", 1) for x in toks))
        out[name] = (t, end, perf, lines)
    return out


class TestNegativeEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(NEG / "summary.csv", encoding="utf-8-sig") as f:
            cls.rows = {r["test"]: r for r in csv.DictReader(f)}
        cls.win = windows(NEG)
        cls.uart = (NEG / "device_uart.log").read_text(errors="replace")

    def test_all_negative_rejected(self):
        names = [n for n in self.rows if n[0] in "AB"]
        self.assertEqual(len(names), 11)
        for n in names:
            self.assertEqual(self.rows[n]["expect"], "REJECT")
            self.assertEqual(self.rows[n]["ok"], "True", n)
            self.assertNotEqual(self.rows[n]["exit"], "0", n)

    def test_reconnect_after_restore(self):
        for n in ("R1_restore_exchange", "R2_restore_exchange"):
            self.assertEqual(self.rows[n]["ok"], "True")
            self.assertIn("RESULT: PASS", (NEG / f"{n}.txt").read_text(errors="replace"))
            self.assertIn("exchange: OK (encrypted app message", (NEG / f"{n}.txt").read_text(errors="replace"))

    def test_rejections_are_crypto_not_transport(self):
        for n, r in self.rows.items():
            if r["expect"] != "REJECT":
                continue
            self.assertRegex(r["evidence"], r"SPDM_REQ_E(AUTH|SESSION)", n)
            txt = (NEG / f"{n}.txt").read_text(errors="replace").lower()
            self.assertNotIn("dfuh-cli failed", txt, n)
            self.assertNotIn("timeout", txt, n)

    def test_anchor_tests_received_full_chain_before_reject(self):
        """A1-A5/B1: the whole 15 KB chain was delivered, so the rejection is the chain verification."""
        for n in [k for k in self.rows if k[:2] in ("A1", "A2", "A3", "A4", "A5", "B1")]:
            perf = self.win[n][2]
            self.assertTrue(perf, n)
            self.assertGreaterEqual(max(int(p["cum_b"]) for p in perf), 18490, n)

    def test_each_fault_image_logged_its_banner_and_tamper(self):
        expect = {"B1_modified_cert": (1, "[FI] CERT: leaf subject CN modified"),
                  "B2_invalid_signature": (2, "[FI] SIG: flipped byte 2313 of 4627-byte signature"),
                  "B3_modified_transcript": (3, "[FI] TRANSCRIPT: signing modified transcript hash (op 0x64)"),
                  "B4_modified_kem_ct": (4, "[FI] KEM_CT: flipped byte 784 of 1568-byte ML-KEM ciphertext"),
                  "B5_mitm_finish_rsp": (5, "[FI] WIRE: flipped AEAD tag byte of secured response #1"),
                  "B6_mitm_app_data": (6, "[FI] WIRE: flipped AEAD tag byte of secured response #2")}
        tl = (NEG / "timeline.txt").read_text()
        for n, (mode, line) in expect.items():
            self.assertIn(f"[FI] FAULT-INJECTION BUILD, mode {mode}", self.uart, n)
            # window = from programming this FI image until the end of its test (CERT is applied at boot)
            t0 = re.search(rf"(\d\d:\d\d:\d\d\.\d+) PROGRAM FI={mode}", tl).group(1)
            t1 = self.win[n][1].strftime("%H:%M:%S.%f")[:12]
            hits = [l for l in self.uart.splitlines() if t0 <= l[:12] < t1 and line in l]
            self.assertTrue(hits, f"{n}: tamper line not logged while FI={mode} image ran")

    def test_handshake_faults_saw_complete_kex_response(self):
        """B3-B6: last KEY_EXCHANGE_RSP chunk (rs=112) was delivered before the requester aborted."""
        for n in ("B3_modified_transcript", "B4_modified_kem_ct", "B5_mitm_finish_rsp", "B6_mitm_app_data"):
            perf = self.win[n][2]
            self.assertTrue(any(p["large"] == "6305" for p in perf), n)
            self.assertTrue(any(p["rsp"] == "06" and p["rs"] == "112" for p in perf), n)

    def test_b4_signature_was_produced_over_modified_ct(self):
        """B4: device signed KEY_EXCHANGE_RSP after the ciphertext was modified (sign>0 in same message)."""
        perf = self.win["B4_modified_kem_ct"][2]
        kx = [p for p in perf if p["req"] == "85" and int(p["sign"]) > 0]
        self.assertEqual(len(kx), 1)
        self.assertGreater(int(kx[0]["kem"]), 0)

    def test_b6_session_reached_app_data(self):
        """B6: FINISH succeeded (1st secured rsp untouched), only the app-data response was tampered."""
        perf = self.win["B6_mitm_app_data"][2]
        self.assertGreaterEqual(sum(1 for p in perf if p["req"] == "SEC"), 2)

    def test_clean_image_restored(self):
        tl = (NEG / "timeline.txt").read_text()
        self.assertIn("READY FI=0 (banner=False)", tl)
        restore_t = re.search(r"(\d\d:\d\d:\d\d\.\d+) READY FI=0", tl).group(1)
        late = [l for l in self.uart.splitlines() if l[:12] > restore_t]
        self.assertFalse([l for l in late if "[FI]" in l], "FI output after restoring the clean image")


class TestPositiveEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.win = windows(POS)

    def txt(self, n):
        return (POS / f"{n}.txt").read_text(errors="replace")

    def test_negotiated_cnsa_profile_no_classical(self):
        t = self.txt("connect")
        self.assertRegex(t, r"SPDM version\s+: 1\.4")
        self.assertRegex(t, r"Base asym\s+: \(none\)")
        self.assertRegex(t, r"PQC asym\s+: ML_DSA_87")
        self.assertRegex(t, r"DHE group\s+: \(none\)")
        self.assertRegex(t, r"KEM alg\s+: ML_KEM_1024")
        self.assertRegex(t, r"Base hash\s+: SHA_384")
        self.assertRegex(t, r"AEAD suite\s+: AES_256_GCM")

    def test_positive_ops(self):
        for n in ("connect", "slots", "get_cert", "challenge", "measure", "session", "exchange_1", "exchange_3"):
            self.assertIn("RESULT: PASS", self.txt(n), n)
        self.assertIn("15187 bytes, verified to pinned root", self.txt("get_cert"))
        for n in ("exchange_1", "exchange_3"):
            m = re.search(r"exchange: OK \(encrypted app message, sent (\d+) B, received (\d+) B\)", self.txt(n))
            self.assertTrue(m and m.group(1) == m.group(2), n)

    def test_known_exchange2_failure_is_failclosed(self):
        t = self.txt("exchange_2")
        self.assertIn("SPDM_REQ_ESESSION", t)
        self.assertNotIn("RESULT: PASS", t)
        self.assertNotIn("exchange: OK", t)

    def test_classical_root_rejected(self):
        self.assertIn("SPDM_REQ_EAUTH", self.txt("NEG_wrong_root"))

    def test_message_sizes_consistent_with_mlkem_mldsa(self):
        perf = [p for n in ("session", "exchange_1") for p in self.win[n][2]]
        self.assertIn("1638", {p["large"] for p in perf})   # KEY_EXCHANGE (requester encaps key 1568 B)
        self.assertIn("6305", {p["large"] for p in perf})   # KEY_EXCHANGE_RSP
        # KEY_EXCHANGE_RSP fixed part: hdr4 + RspSessionID2 + MutAuth1 + SlotIDParam1 + Random32 + CT + OpaqueLen2
        # + sig + ResponderVerifyData48 -> leaves room only for opaque data (no measurement summary hash)
        fixed = 4 + 2 + 1 + 1 + 32 + MLKEM1024_CT + 2 + MLDSA87_SIG + 48
        self.assertTrue(0 <= 6305 - fixed <= 64, 6305 - fixed)
        fixed_req = 4 + 2 + 1 + 1 + 32 + MLKEM1024_CT + 2   # + opaque
        self.assertTrue(0 <= 1638 - fixed_req <= 64, 1638 - fixed_req)


# ----------------------------------------------------------------------------- tooling
class TestPerfSummary(unittest.TestCase):
    def test_counter_reset_between_requester_runs(self):
        d = Path(tempfile.mkdtemp())
        (d / "timeline.txt").write_text("10:00:00.000 START op1\n10:01:00.000 START op2\n")
        line = ("{t} [I] [PERF] n={n} req={rq} rq=9 rsp={rs} rs=128 large={lg} t={dt} sign={s} kexg=0 kexs=0 "
                "kem={k} cum_t={ct} cum_b={cb} nsstk=10 nsheap=20 sstk=30 sheap=40\n")
        log = "".join([
            line.format(t="10:00:01.000", n=1, rq="E1", rs="61", lg=0, dt=100, s=0, k=0, ct=100, cb=10),
            line.format(t="10:00:02.000", n=2, rq="85", rs="7F", lg=6305, dt=900, s=500, k=50, ct=1000, cb=30),
            line.format(t="10:00:03.000", n=1, rq="E1", rs="61", lg=0, dt=100, s=0, k=0, ct=100, cb=12),
            line.format(t="10:01:01.000", n=1, rq="E1", rs="61", lg=0, dt=7, s=0, k=0, ct=7, cb=5)])
        (d / "device_uart.log").write_text(log)
        rows = {r[0]: r for r in perf_summary.summarise(d)}
        self.assertEqual(rows["op1"][1:4], (3, 42, 1100))   # 2+1 transfers, 30+12 B, 1000+100 us
        self.assertEqual(rows["op1"][4], 500)
        self.assertEqual(rows["op1"][6], 50)
        self.assertEqual(rows["op1"][7], 6305)
        self.assertEqual(rows["op2"][1:4], (1, 5, 7))

    def test_partial_uart_line_is_skipped(self):
        d = Path(tempfile.mkdtemp())
        (d / "timeline.txt").write_text("10:00:00.000 START op1\n")
        (d / "device_uart.log").write_text(
            "10:00:01.000 [I] [PERF] n=1 req=86 rq=9 rsp=7F rs=5 large=0 t=758 cum_t=75[I] Command 0x80\n"
            "10:00:02.000 [I] [PERF] n=1 req=E1 rq=21 rsp=61 rs=21 large=0 t=5 sign=0 kexg=0 kexs=0 kem=0 "
            "cum_t=5 cum_b=42 nsstk=1 nsheap=1 sstk=1 sheap=1\n")
        rows = {r[0]: r for r in perf_summary.summarise(d)}
        self.assertEqual(rows["op1"][1:4], (1, 42, 5))

    def test_real_run_totals(self):
        rows = {r[0]: r for r in perf_summary.summarise(POS)}
        self.assertEqual(rows["get_cert"][1:3], (142, 18490))
        self.assertEqual(rows["session"][1:3], (214, 28199))


class TestReport(unittest.TestCase):
    def test_report_text(self):
        pdf = RES / "Task3_PQC_SPDM_Report.pdf"
        out = subprocess.run(["pdftotext", "-layout", str(pdf), "-"], capture_output=True, text=True,
                             errors="replace").stdout
        if not out:
            self.skipTest("pdftotext not available")
        for s in ("12/12 REJECTED", "8/9 PASS", "ML-KEM-1024", "ML-DSA-87", "1,638 B / 6,305 B",
                  "15,187 B", "fail-closed", task3_neg_name()):
            self.assertIn(s, out, s)


def task3_neg_name():
    return NEG.name




# ----------------------------------------------------------------------------- edge-case runs (hardware)
def latest(phase):
    runs = sorted(d for d in RES.glob(f"task3_EDGE_{phase}_*") if (d / "summary.csv").exists())
    runs = [d for d in runs if (d / "summary.csv").read_text(encoding="utf-8-sig").strip()]
    return runs[-1] if runs else None


def edge_rows(run):
    with open(run / "summary.csv", encoding="utf-8-sig") as f:
        return {r["test"]: r for r in csv.DictReader(f)}


class EdgeBase(unittest.TestCase):
    PHASE = None
    REQUIRED = ()

    @classmethod
    def setUpClass(cls):
        cls.rundir = latest(cls.PHASE) if cls.PHASE else None
        if cls.rundir is None:
            raise unittest.SkipTest(f"no task3_EDGE_{cls.PHASE}_* run yet")
        # merge all runs of this phase (a phase may be re-run partially with -Only); later runs win
        cls.rows, cls.uart = {}, ""
        for d in sorted(RES.glob(f"task3_EDGE_{cls.PHASE}_*")):
            if (d / "summary.csv").exists():
                cls.rows.update(edge_rows(d))
            if (d / "device_uart.log").exists():
                cls.uart += (d / "device_uart.log").read_text(errors="replace")

    def test_required_tests_present_and_as_expected(self):
        for n in self.REQUIRED:
            self.assertIn(n, self.rows, f"{n} missing (run aborted?)")
            self.assertEqual(self.rows[n]["ok"], "True", f"{n}: {self.rows[n]['evidence']}")

    def test_rejections_are_protocol_errors(self):
        for n, r in self.rows.items():
            if r["expect"] == "REJECT" and n.startswith(("P2_B", "V9", "E3", "P2_R2")):
                self.assertRegex(r["evidence"], r"SPDM_REQ_E|error:", n)

    def test_no_fault_banner_in_clean_phases(self):
        if self.PHASE in ("P4", "RESTORE"):
            self.assertNotIn("FAULT-INJECTION BUILD", self.uart)


class TestEdgeP4(EdgeBase):
    PHASE = "P4"
    # E4 (application-message sizes) was dropped to save board time -> reported as "not tested"
    REQUIRED = ("E1a_session_stability", "E1b_session_stability", "E2b_reconnect_after_abort",
                "E3_empty_cert_slot", "E5_challenge_after_reset")

    def test_e2_kill_happened_during_handshake(self):
        self.assertEqual(self.rows["E2_killed_mid_handshake"]["ok"], "True", "kill point not reached")

    def test_e5_device_rebooted_clean(self):
        self.assertIn("SPDM Responder", self.uart)        # boot banner after the J-Link reset

    def test_e3_rejected_for_the_right_reason(self):
        self.assertIn("slot 1 is empty", self.rows["E3_empty_cert_slot"]["evidence"])


class TestEdgeProv(EdgeBase):
    PHASE = "PROV"
    REQUIRED = ("V1_info", "V2a_erase_cert", "V2b_erase_key", "V3_genkey_mldsa65_unsupported",
                "V4_genkey_ecdsa384_not_built", "V5_genkey_mldsa87", "V5b_genkey_occupied_slot",
                "V6_oversize_chain", "V7b_prove_foreign_leaf", "V9_roothash_sha256_vs_sha384")


class TestEdgeP2FI(EdgeBase):
    PHASE = "P2FI"
    REQUIRED = ("P2_B1_modified_cert", "P2_B2_invalid_signature", "P2_B3_modified_transcript",
                "P2_B5_mitm_finish_rsp", "P2_B6_mitm_app_data", "P2_R1_clean_exchange", "P2_R2_pqc_root_on_ecc_device")

    def test_fault_lines_logged(self):
        for s in ("[FI] CERT", "[FI] SIG", "[FI] TRANSCRIPT", "[FI] WIRE: flipped AEAD tag byte of secured response #1",
                  "[FI] WIRE: flipped AEAD tag byte of secured response #2"):
            self.assertIn(s, self.uart, s)


class TestEdgeRestore(EdgeBase):
    PHASE = "RESTORE"
    REQUIRED = ("R_restore_exchange", "R_restore_get_cert")

    def test_restored_chain_is_mldsa87_sha384(self):
        blob = (self.rundir / "chain.der").read_bytes()
        certs = split_spdm_chain(blob)
        self.assertEqual(blob[4:52], hashlib.sha384(certs[0]).digest())
        self.assertEqual(certs[0], (ROOTS / "good_root.der").read_bytes())
        leaf = x509.load_der_x509_certificate(certs[-1])
        self.assertIsInstance(leaf.public_key(), mldsa.MLDSA87PublicKey)


if __name__ == "__main__":
    unittest.main(verbosity=2)
