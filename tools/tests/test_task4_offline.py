"""Offline verification of Task 4 (lifecycle: SPDM responder behind EPBL, credential update).

No board needed. Checks the EPBL flash map of the responder, the signed MCUboot images and
their tamper variants, the measurement reference-value tool and the recorded hardware evidence.

Run:   py -3.13 -m unittest discover -s C:\\Users\\kpato\\KP\\Hackathon\\tools\\tests -p "test_task4*.py" -v
"""
import csv
import hashlib
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HACK = Path(r"C:\Users\kpato\KP\Hackathon")
SPDM = HACK / "PSOC_C3_SPDM_v2" / "PSOC_C3_SPDM" / "SPDM"
RES = HACK / "results"
TOOLS = HACK / "tools"
IMG = RES / "task4_boot"
EPBL_MAP = HACK / "epbl" / "bl" / "platforms" / "PSC3_M6" / "memory_maps" / "spdm_overwrite_single_flash.json"
EPBL_PUB = HACK / "mtb-example-ce243041-secure-boot-and-update-with-epb-master" / "keys" / "ml_dsa_87_img_sign_key_pub.der"
sys.path.insert(0, str(TOOLS))
from resp_epbl_sign import read_ihex  # noqa: E402


def responder_dir():
    """The EPBL variant lives in the responder repo (after fold-back) or in the resp_epbl work copy."""
    for d in (SPDM / "psoc-spdm-responder", SPDM / "resp_epbl"):
        if (d / "bsps" / "TARGET_APP_KIT_PSC3M6_EVAL_EPBL").exists():
            return d
    raise unittest.SkipTest("EPBL BSP variant not found")


def cymem(bsp_dir):
    vals = {}
    for line in (bsp_dir / "config" / "GeneratedSource" / "cymem_memory_locations.mk").read_text().splitlines():
        m = re.match(r"CYMEM_CM33_0_S_(\w+):=(0x[0-9A-Fa-f]+)", line)
        if m:
            vals[m.group(1)] = int(m.group(2), 16)
    return vals


def mcuboot(path):
    mem = read_ihex(path)
    base = min(mem)
    get = lambda a, n: bytes(mem.get(a + i, 0) for i in range(n))  # noqa: E731
    magic, _ld, hdr, prot, size, _fl = struct.unpack("<IIHHII", get(base, 20))
    maj, mnr, rev, bn = struct.unpack("<BBHI", get(base + 20, 8))
    tlv = {}
    off = base + hdr + size + prot
    tmagic, ttotal = struct.unpack("<HH", get(off, 4))
    p = off + 4
    while p < off + ttotal:
        t, ln = struct.unpack("<HH", get(p, 4))
        tlv[t] = get(p + 4, ln)
        p += 4 + ln
    return dict(mem=mem, base=base, magic=magic, hdr=hdr, prot=prot, size=size,
                ver=(maj, mnr, rev, bn), tlv=tlv, tlv_magic=tmagic, end=p)


class TestFlashMap(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.resp = responder_dir()
        cls.epbl = cymem(cls.resp / "bsps" / "TARGET_APP_KIT_PSC3M6_EVAL_EPBL")
        cls.std = cymem(cls.resp / "bsps" / "TARGET_APP_KIT_PSC3M6_EVAL")

    def test_regions(self):
        e = self.epbl
        self.assertEqual((e["m33_nvm_OFFSET"], e["m33_nvm_SIZE"]), (0x1B000, 0x19000))
        self.assertEqual((e["m33ns_nvm_OFFSET"], e["m33ns_nvm_SIZE"]), (0x34000, 0x14000))
        self.assertEqual(e["m33_nvm_OFFSET"] + e["m33_nvm_SIZE"], e["m33ns_nvm_OFFSET"])

    def test_pas_unchanged_and_identity_survives(self):
        for k in ("pas_OFFSET", "pas_SIZE"):
            self.assertEqual(self.epbl[k], self.std[k])
        self.assertEqual(self.epbl["m33ns_nvm_OFFSET"] + self.epbl["m33ns_nvm_SIZE"], self.epbl["pas_OFFSET"])

    def test_standalone_bsp_untouched(self):
        self.assertEqual((self.std["m33_nvm_OFFSET"], self.std["m33ns_nvm_OFFSET"]), (0, 0x20000))

    def test_epbl_map_matches_bsp_and_avoids_pas(self):
        import json
        m = json.loads(EPBL_MAP.read_text())
        bl = m["bootloader"]["bootloader_area"]
        s = m["application_1"]["slots"]
        boot, upg, size = int(s["boot"], 16), int(s["upgrade"], 16), int(s["size"], 16)
        self.assertEqual(int(bl["address"], 16) + int(bl["size"], 16), boot)
        self.assertEqual(boot - 0x32000000, self.epbl["m33_nvm_OFFSET"])
        self.assertEqual(size, self.epbl["m33_nvm_SIZE"] + self.epbl["m33ns_nvm_SIZE"])
        pas_lo = 0x32000000 + self.epbl["pas_OFFSET"]
        pas_hi = pas_lo + self.epbl["pas_SIZE"]
        for lo in (boot, upg):
            self.assertTrue(lo + size <= pas_lo or lo >= pas_hi, hex(lo))
        self.assertLessEqual(upg + size, 0x32080000)       # 512 KB flash

    def test_header_only_in_secure_project(self):
        mk = (self.resp / "bsps" / "TARGET_APP_KIT_PSC3M6_EVAL_EPBL" / "bsp.mk").read_text()
        self.assertRegex(mk, r"ifeq \(\$\(filter SECURE,\$\(VCORE_ATTRS\)\),SECURE\)\s*\nMCUBOOT_HEADER_SIZE\?=0x400")

    def test_real_measurements_only_in_epbl_build(self):
        src = (self.resp / "proj_cm33_s" / "core" / "spdm_meas.c").read_text()
        self.assertIn("#if defined(MCUBOOT_HEADER_SIZE) && (MCUBOOT_HEADER_SIZE > 0)", src)
        self.assertIn("placeholder", src)           # standalone build keeps the vendor placeholders


GENUINE = {"resp_v1_0_0_boot.hex": (1, 0, 0), "resp_v1_1_0_boot.hex": (1, 1, 0),
           "resp_v1_1_0_update.hex": (1, 1, 0), "resp_v1_0_0_update.hex": (1, 0, 0),
           "resp_v1_2_0_update.hex": (1, 2, 0), "resp_v2_0_0_update.hex": (2, 0, 0)}


class TestSignedImages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (IMG / "resp_v1_0_0_boot.hex").exists():
            raise unittest.SkipTest("run task4_epbl_spdm_tests.ps1 -Phase PREP first")
        cls.img = {f: mcuboot(IMG / f) for f in list(GENUINE) + ["resp_v1_2_0_rogue_update.hex"]}

    def test_format_versions_slots(self):
        for f, ver in GENUINE.items():
            i = self.img[f]
            self.assertEqual(i["magic"], 0x96F3B83D, f)
            self.assertEqual(i["hdr"], 0x400, f)
            self.assertEqual(i["ver"][:3], ver, f)
            self.assertEqual(i["base"], 0x3201B000 if "_boot" in f else 0x3204E000, f)
            self.assertLessEqual(i["end"] - i["base"], 0x2D000, f)

    def test_pqc_tlvs(self):
        for f, i in self.img.items():
            self.assertEqual(len(i["tlv"][0x12]), 64, f)            # SHA-512 image hash
            self.assertEqual(len(i["tlv"][0x01]), 64, f)            # key hash
            self.assertEqual(len(i["tlv"][0x105]), 4627, f)         # ML-DSA-87 signature
            self.assertNotIn(0x22, i["tlv"], f)                     # no ECDSA signature

    def test_image_hash_tlv(self):
        for f, i in self.img.items():
            n = i["hdr"] + i["size"] + i["prot"]
            data = bytes(i["mem"].get(i["base"] + k, 0) for k in range(n))
            self.assertEqual(hashlib.sha512(data).digest(), i["tlv"][0x12], f)

    def test_keyhash_is_epbl_key_rogue_is_not(self):
        # key-hash TLV = SHA-512 of the raw 2,592 B ML-DSA-87 public key that is embedded in EPBL
        raw_pk = EPBL_PUB.read_bytes()[-2592:]
        for f in GENUINE:
            self.assertEqual(self.img[f]["tlv"][0x01], hashlib.sha512(raw_pk).digest(), f)
        self.assertNotEqual(self.img["resp_v1_2_0_rogue_update.hex"]["tlv"][0x01], hashlib.sha512(raw_pk).digest())

    def test_tamper_variants_differ_by_one_byte(self):
        for src, dst, addr in (("resp_v1_2_0_update.hex", "resp_v1_2_0_corrupt_update.hex", 0x3204F000),
                               ("resp_v1_2_0_update.hex", "resp_v1_2_0_header_update.hex", 0x3204E016),
                               ("resp_v1_1_0_boot.hex", "resp_v1_1_0_tampered_boot.hex", 0x3201C000)):
            a, b = read_ihex(IMG / src), read_ihex(IMG / dst)
            diff = [k for k in a if a[k] != b.get(k)]
            self.assertEqual(diff, [addr], dst)

    def test_padded_images_never_touch_pas(self):
        for f in GENUINE:
            mem = read_ihex(IMG / f)
            self.assertFalse(any(0x32048000 <= a < 0x3204E000 for a in mem), f)


class TestMeasVerifier(unittest.TestCase):
    def setUp(self):
        if not (IMG / "epbl_full.hex").exists():
            self.skipTest("PREP not run")

    def _run(self, text):
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as t:
            t.write(text)
        return subprocess.run([sys.executable, "-I", str(TOOLS / "task4_verify_meas.py"), str(IMG / "epbl_full.hex"),
                               str(IMG / "resp_v1_1_0_update.hex"), t.name], capture_output=True, text=True)

    def _expected(self):
        m = read_ihex(IMG / "epbl_full.hex")
        b1 = hashlib.sha384(bytes(m.get(0x32000000 + i, 0) for i in range(0x1B000))).hexdigest()
        i = mcuboot(IMG / "resp_v1_1_0_update.hex")
        b2 = hashlib.sha384(bytes(i["mem"].get(i["base"] + k, 0) for k in range(i["hdr"] + i["size"] + i["prot"]))).hexdigest()
        return b1, b2, struct.pack("<Q", (1 << 24) | (1 << 16)).hex()

    def test_match_and_mismatch(self):
        b1, b2, b3 = self._expected()
        good = (f"    block 1: ImmutableROM (digest)  (48 B)  {b1}\n    block 2: MutableFirmware (digest)  (48 B)  {b2}\n"
                f"    block 3: MutableFirmwareSecurityVersion (raw)  (8 B)  {b3}\n")
        self.assertEqual(self._run(good).returncode, 0)
        bad = good.replace(b2, b2[:-1] + ("0" if b2[-1] != "0" else "1"))
        self.assertEqual(self._run(bad).returncode, 1)
        old = good.replace(b3, struct.pack("<Q", 1 << 24).hex())     # device claims 1.0.0
        self.assertEqual(self._run(old).returncode, 1)


def latest_run():
    runs = sorted(d for d in RES.glob("task4_EPBL_SPDM_*") if (d / "summary.csv").exists())
    return runs


class TestHardwareEvidence(unittest.TestCase):
    REQUIRED = ("T0_boot_v1_0_0", "T0_measure_v1_0_0", "T0_exchange_v1_0_0", "T1_valid_update_v1_1_0",
                "T1_measure_v1_1_0", "T1_exchange_after_update", "T2_rollback_v1_0_0", "T3_corrupt_payload",
                "T4_modified_header", "T5_rogue_key", "T6_measure_still_v1_1_0", "T7_tampered_primary",
                "T7_connect_no_app", "T8_recovery_update_v2_0_0", "T8_measure_v2_0_0", "T8_exchange_after_recovery")
    MEAS = {"T0_measure_v1_0_0": "resp_v1_0_0_boot.hex", "T1_measure_v1_1_0": "resp_v1_1_0_update.hex",
            "T6_measure_still_v1_1_0": "resp_v1_1_0_update.hex", "T8_measure_v2_0_0": "resp_v2_0_0_update.hex"}

    @classmethod
    def setUpClass(cls):
        runs = latest_run()
        if not runs:
            raise unittest.SkipTest("no task4_EPBL_SPDM_* run yet")
        cls.rows, cls.files = {}, {}
        for d in runs:                                   # later (partial -Only) runs win
            with open(d / "summary.csv", encoding="utf-8-sig") as f:
                for r in csv.DictReader(f):
                    cls.rows[r["test"]] = r
                    cls.files[r["test"]] = d

    def test_required_present_and_as_expected(self):
        for n in self.REQUIRED:
            self.assertIn(n, self.rows, f"{n} missing")
            self.assertEqual(self.rows[n]["ok"], "True", f"{n}: {self.rows[n]['result']} {self.rows[n]['evidence']}")

    def test_measurements_match_release_artefacts(self):
        for n, image in self.MEAS.items():
            if n not in self.rows:
                continue
            r = subprocess.run([sys.executable, "-I", str(TOOLS / "task4_verify_meas.py"), str(IMG / "epbl_full.hex"),
                                str(IMG / image), str(self.files[n] / f"{n}.txt")], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, f"{n}:\n{r.stdout}")

    def test_rejected_updates_name_the_reason(self):
        reasons = {"T2_rollback_v1_0_0": "insufficient version", "T3_corrupt_payload": r"invalid|not valid|returned",
                   "T4_modified_header": r"invalid|not valid|returned", "T5_rogue_key": r"invalid|not valid|returned"}
        for n, rx in reasons.items():
            if n in self.rows:
                log = (self.files[n] / f"{n}_boot.log").read_text(errors="replace")
                self.assertRegex(log, rx, n)
                self.assertNotIn("Image upgrade secondary slot -> primary slot", log, n)


if __name__ == "__main__":
    unittest.main(verbosity=2)
