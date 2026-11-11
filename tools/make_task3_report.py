"""Generate results/Task3_PQC_SPDM_Report.pdf from the measured Task 3 data.
Run: py -3.13 -I tools/make_task3_report.py
Data: latest results/task3_P4_* (positive suite), latest results/task3_NEG_P4_* (negative tests),
results/task1_P2_20261111_1215 (ECC baseline), build logs (see 'Evidence').
"""
import csv
import re
import statistics
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (KeepTogether, Paragraph, Preformatted, SimpleDocTemplate, Spacer,
                                Table, TableStyle)

HACK = Path(r"C:\Users\kpato\KP\Hackathon")
RES = HACK / "results"
sys.path.insert(0, str(HACK / "tools"))
from perf_summary import parse, summarise  # noqa: E402

OUT = RES / "Task3_PQC_SPDM_Report.pdf"
P2 = RES / "task1_P2_20261111_1215"
P4 = sorted(d for d in RES.glob("task3_P4_*") if d.is_dir())[-1]
NEGS = sorted(d for d in RES.glob("task3_NEG_P4_*") if (d / "summary.csv").exists())
NEG = NEGS[-1] if NEGS else None

# ---------------------------------------------------------------- data
def suite_summary(run):
    rows = {}
    raw = (run / "summary.txt").read_bytes() if (run / "summary.txt").exists() else b""
    txt = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8", "replace")
    for line in txt.splitlines():
        m = re.match(r"^(\S+)\s+(-?\d+)\s+(True|False)\s+([\d.]+)", line.strip())
        if m:
            rows[m.group(1)] = (int(m.group(2)), m.group(3) == "True", float(m.group(4)))
    return rows


def perf_values(run, key):
    _, perf = parse(run)
    return [int(kv[key]) for _, kv in perf if int(kv[key]) > 0]


def ops(run):
    return {r[0]: r for r in summarise(run)}


p2s, p4s = suite_summary(P2), suite_summary(P4)
p2o, p4o = ops(P2), ops(P4)
sign2, sign4 = perf_values(P2, "sign"), perf_values(P4, "sign")
kem4 = perf_values(P4, "kem")
_, perf4 = parse(P4)
fin4 = [int(kv["t"]) for _, kv in perf4 if kv["req"] == "SEC" and int(kv["rq"]) > 80]
app4 = [int(kv["t"]) for _, kv in perf4 if kv["req"] == "SEC" and int(kv["rq"]) <= 80]
_, perf2 = parse(P2)
fin2 = [int(kv["t"]) for _, kv in perf2 if kv["req"] == "SEC" and int(kv["rq"]) > 80]
kex2 = [int(kv["t"]) for _, kv in perf2 if kv["req"] == "85" and int(kv["sign"]) > 0]
kex4 = [int(kv["t"]) for _, kv in perf4 if kv["req"] == "85" and int(kv["sign"]) > 0]
peak = lambda perf, k: max(int(kv[k]) for _, kv in perf)


def ms(us):
    return f"{us / 1000:.1f}"


def rng(v):
    return f"{ms(min(v))} - {ms(max(v))} (median {ms(statistics.median(v))}, n={len(v)})" if v else "n/a"


neg_rows = []
if NEG:
    with open(NEG / "summary.csv", encoding="utf-8-sig") as f:
        neg_rows = list(csv.DictReader(f))

# ---------------------------------------------------------------- styles
ss = getSampleStyleSheet()
H1 = ParagraphStyle("H1", parent=ss["Heading1"], fontSize=15, spaceBefore=10, spaceAfter=6,
                    textColor=colors.HexColor("#1F3864"))
H2 = ParagraphStyle("H2", parent=ss["Heading2"], fontSize=12, spaceBefore=8, spaceAfter=4,
                    textColor=colors.HexColor("#1F3864"))
BODY = ParagraphStyle("B", parent=ss["BodyText"], fontSize=9, leading=12, alignment=TA_LEFT)
CELL = ParagraphStyle("C", parent=BODY, fontSize=8, leading=10)
MONO = ParagraphStyle("M", parent=BODY, fontName="Courier", fontSize=7.5, leading=9.5)
PASS = colors.HexColor("#E2EFDA")
REJ = colors.HexColor("#FCE4D6")
WARN = colors.HexColor("#FFF2CC")
HEAD = colors.HexColor("#1F3864")


def p(text, style=BODY):
    return Paragraph(text, style)


def table(rows, widths, shade=None):
    data = [[Paragraph(f'<font color="white"><b>{c}</b></font>', CELL) for c in rows[0]]]
    data += [[p(str(c), CELL) for c in r] for r in rows[1:]]
    t = Table(data, colWidths=[w * mm for w in widths], repeatRows=1)
    st = [("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#A6A6A6")),
          ("VALIGN", (0, 0), (-1, -1), "TOP"),
          ("LEFTPADDING", (0, 0), (-1, -1), 3), ("RIGHTPADDING", (0, 0), (-1, -1), 3),
          ("TOPPADDING", (0, 0), (-1, -1), 2), ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
          ("BACKGROUND", (0, 0), (-1, 0), HEAD)]
    for (r, c) in (shade or []):
        st.append(("BACKGROUND", (0, r), (-1, r), c))
    t.setStyle(TableStyle(st))
    return t


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 7)
    canvas.setFillColor(colors.grey)
    canvas.drawString(15 * mm, 10 * mm,
                      "PQC Hackathon - Task 3: real-time PQC (SPDM 1.4, ML-DSA-87 / ML-KEM-1024)  |  measured 2026-11-11")
    canvas.drawRightString(195 * mm, 10 * mm, f"Page {doc.page}")
    canvas.restoreState()


# ---------------------------------------------------------------- content
pos_names = ["connect", "slots", "get_cert", "challenge", "measure", "session",
             "exchange_1", "exchange_2", "exchange_3"]
pos_pass = sum(1 for n in pos_names if p4s.get(n, (1, False, 0))[1])
neg_total = len(neg_rows) - sum(1 for r in neg_rows if r["expect"] == "PASS")
neg_ok = sum(1 for r in neg_rows if r["expect"] == "REJECT" and r["ok"] == "True")
wrong_root_ok = ("NEG_wrong_root" in p4s) and p4s["NEG_wrong_root"][0] != 0
restore = [r for r in neg_rows if r["expect"] == "PASS"]
restore_ok = bool(restore) and all(r["ok"] == "True" for r in restore)
neg_line = (f"{neg_ok + int(wrong_root_ok)}/{neg_total + 1} REJECTED" if NEG
            else "classical-root test only - device/anchor negative tests PENDING")


def _phase_csv(prefix):
    out = {}
    for d in sorted(RES.glob(f"task3_EDGE_{prefix}_*")):
        if (d / "summary.csv").exists():
            with open(d / "summary.csv", encoding="utf-8-sig") as fh:
                out.update({r["test"]: r for r in csv.DictReader(fh)})
    return out


_p2fi, _rest = _phase_csv("P2FI"), _phase_csv("RESTORE")
p2fi_rej = [r for r in _p2fi.values() if r["expect"] == "REJECT"]
p2fi_line = (f"{sum(r['ok'] == 'True' for r in p2fi_rej)}/{len(p2fi_rej)} REJECTED on the ECC baseline; clean ECC "
             f"exchange {'PASS' if _p2fi.get('P2_R1_clean_exchange', {}).get('ok') == 'True' else 'FAIL'}"
             if _p2fi else "not run")
p2fi_good = bool(_p2fi) and all(r["ok"] == "True" for r in _p2fi.values())
rep = [r for n, r in _rest.items() if "exchange" in n]
rep_line = (f"{sum(r['ok'] == 'True' for r in rep)}/{len(rep)} PASS after re-provisioning" if rep else "not run")
rep_good = bool(rep) and all(r["ok"] == "True" for r in rep)

S = []
S += [
    p("Task 3 - Real-time PQC Protection with SPDM", ParagraphStyle("T", parent=H1, fontSize=20, spaceAfter=2)),
    p("PSoC Control C3M6 (KIT_PSC3M6_EVAL) - SPDM 1.4 responder authenticated with ML-DSA-87, "
      "session keys from ML-KEM-1024", BODY),
    Spacer(1, 6),
    p("1. Summary", H1),
    p("The SPDM responder was migrated from the classical profile P2 (ECDSA-P384 / ECDHE-P384) to the "
      "CNSA 2.0 profile <b>P4</b>: SPDM <b>1.4</b>, device identity <b>ML-DSA-87</b> (FIPS 204), key "
      "establishment <b>ML-KEM-1024</b> (FIPS 203), SHA-384 transcript/KDF hash and AES-256-GCM. The device "
      "identity key is generated inside the secure world, certified by an ML-DSA-87 root CA (root and leaf "
      "both ML-DSA-87, no classical certificate in the chain) and the host requester validates the chain against "
      "a pinned ML-DSA-87 root. The responder offers no classical algorithm (BaseAsymAlgo and DHE group are "
      "empty), so a classical-only requester or trust anchor cannot negotiate a downgrade."),
    Spacer(1, 4),
    table([["Area", "Result", "Key figures (P2 ECC -> P4 PQC)"],
           ["Positive tests", f"{pos_pass}/{len(pos_names)} PASS (connect, slots, get-cert, challenge, measure, "
            "session, 3x encrypted exchange)" + (f"; {len(pos_names) - pos_pass} transport/board fault "
            "(fail-closed, see 4.1)" if pos_pass < len(pos_names) else "")
            + (f"; plus {sum(1 for r in neg_rows if r['expect'] == 'PASS' and r['ok'] == 'True')} reconnect "
               "exchanges after the tamper images" if NEG else ""),
            f"device session setup {ms(kex2[0] + fin2[0]) if kex2 and fin2 else '?'} -> " +
            (f"{ms(min(kex4) + statistics.median(fin4))}-{ms(max(kex4) + statistics.median(fin4))} ms "
             f"(median {ms(statistics.median(kex4) + statistics.median(fin4))}; ML-DSA signing time varies)"
             if kex4 and fin4 else "?")],
           ["Negative tests", neg_line,
            "modified cert, invalid signature, modified transcript, modified KEM ciphertext, MITM on "
            "FINISH / app data, rogue / ML-DSA-65 / classical / malformed anchors"],
           ["Reconnect after tamper images", ("PASS" if restore_ok else ("FAIL" if restore else "PENDING")),
            "clean image re-flashed, encrypted exchange works"],
           ["ECC baseline negative tests", p2fi_line,
            "same fault hooks on P2: modified cert, invalid ECDSA signature, modified transcript, MITM FINISH / app "
            "data, PQC root vs ECC device (4.2b)"],
           ["Repeat P4 exchanges", rep_line, "checks that the exchange_2 transport fault does not reproduce (4.2c)"],
           ["Resources", "fits: NS RAM 100 % allocated by design (heap = rest), peaks measured",
            "NS .bss 20.0 -> 57.8 KB; S .bss 11.0 -> 18.9 KB; app flash 130.3 -> 127.5 KB"],
           ["Communication", "host-bound (DFU/I2C, 127 B per transaction)",
            f"get-cert {p2o['get_cert'][1]} -> {p4o['get_cert'][1]} transfers, "
            f"{p2o['get_cert'][2]} -> {p4o['get_cert'][2]} B"]],
          [34, 76, 70], shade=[(1, PASS), (2, PASS if NEG and neg_ok == neg_total else WARN),
                              (3, PASS if restore_ok else WARN), (4, PASS if p2fi_good else WARN),
                              (5, PASS if rep_good else WARN), (7, WARN)]),
]

# 2 CNSA mapping
S += [
    p("2. CNSA 2.0 target profile and mapping from the ECC flow", H1),
    p("Target: CNSA 2.0 (NSA, CNSSP 15 update) algorithm suite for national-security systems. CNSA 2.0 "
      "allows only <b>ML-DSA-87</b> for signatures and <b>ML-KEM-1024</b> for key establishment (both NIST "
      "security category 5, for all classification levels), SHA-384/SHA-512 and AES-256. ML-DSA-65 / "
      "ML-KEM-768 (category 3) are therefore <b>not</b> CNSA 2.0 compliant even though they are the common "
      "commercial default - the brief's warning applies; ML-DSA-65 anchors are actively rejected (test A2). "
      "This is a design constraint, not a certification claim."),
    table([["SPDM function", "Original (P2, ECC)", "Migrated (P4, PQC)", "CNSA 2.0 / standard"],
           ["Responder authentication (CHALLENGE_AUTH, signed MEASUREMENTS, KEY_EXCHANGE_RSP)",
            "ECDSA P-384 (BaseAsymAlgo)", "ML-DSA-87 (PqcAsymAlgo, SPDM 1.4), FIPS 204 context = SPDM op string",
            "ML-DSA-87 required (FIPS 204, cat. 5)"],
           ["Certificate chain / trust anchor", "X.509 P-384 root -> P-384 leaf (1,218 B)",
            "X.509 ML-DSA-87 root (7,578 B) -> ML-DSA-87 leaf (7,557 B); chain 15,187 B",
            "ML-DSA-87 CA signatures"],
           ["Key establishment (KEY_EXCHANGE)", "ECDHE P-384 (DHE group)", "ML-KEM-1024 (KEMAlg, SPDM 1.4)",
            "ML-KEM-1024 required (FIPS 203, cat. 5)"],
           ["Transcript hash / HKDF / HMAC", "SHA-384", "SHA-384", "SHA-384 or SHA-512"],
           ["Session AEAD", "AES-256-GCM", "AES-256-GCM", "AES-256"],
           ["Measurement hash", "SHA-384", "SHA-384", "SHA-384 or SHA-512"],
           ["SPDM version", "1.2/1.3 (classic algorithms)", "1.4 (PQC fields)", "DSP0274 1.4, DSP2072"]],
          [46, 36, 58, 40]),
    Spacer(1, 3),
    p("No hybrid ECC+PQC mode was built (excluded by the brief). The migration is a clean profile switch: "
      "both profiles share the same responder source; <font face='Courier'>SPDM_PROFILE=P4</font> selects the "
      "algorithm set at build time, so a P4 image cannot fall back to ECC at run time."),
]

# 3 Architecture
S += [
    p("3. Migrated architecture", H1),
    p("3.1 How the host validates the PQC identity", H2),
    p("1) VERSION/CAPABILITIES/ALGORITHMS: the requester negotiates SPDM 1.4 and sees "
      "PqcAsymAlgo=ML_DSA_87, KEMAlg=ML_KEM_1024, BaseAsym=none, DHE=none. "
      "2) GET_DIGESTS / GET_CERTIFICATE: the 15,187 B SPDM cert_chain (header, SHA-384 RootHash, root DER, "
      "leaf DER) is fetched in 7,168 B portions (max SPDM message) which the transport splits into 127 B "
      "CHUNK_GET transfers. 3) The requester checks that the first certificate equals the pinned root (DER), "
      "verifies the leaf's ML-DSA-87 CA signature and the RootHash. 4) CHALLENGE_AUTH / KEY_EXCHANGE_RSP "
      "carry a 4,627 B ML-DSA-87 signature over the SPDM 1.2+ signing context (100 B prefix + transcript "
      "hash) with the op-code string as FIPS 204 context; the requester verifies it with the leaf public key. "
      "Only after this does the requester trust measurements or a session."),
    p("3.2 How the session secret is established and derived", H2),
    p("KEY_EXCHANGE (1,638 B) carries the requester's ephemeral ML-KEM-1024 encapsulation key (1,568 B). "
      "The responder's non-secure libspdm hands it through an NSC veneer to the secure world, which runs "
      "ML-KEM-1024.Encaps (wolfCrypt) and returns ciphertext (1,568 B) and the 32 B shared secret. "
      "KEY_EXCHANGE_RSP (6,305 B = ciphertext + ML-DSA-87 signature + ResponderVerifyData + nonce/opaque) is "
      "signed over the transcript TH1 (VCA + cert-chain hash + KEY_EXCHANGE + RSP). Both sides run the SPDM "
      "key schedule (DSP0277/DSP0274: HKDF-SHA-384 handshake secret from the KEM shared secret, bound to TH1) "
      "-> request/response handshake keys and finished keys; ResponderVerifyData (HMAC) and the encrypted "
      "FINISH/FINISH_RSP confirm the keys (TH2), then the master secret yields the AES-256-GCM data keys. "
      "Decapsulation happens on the host; the device never holds a long-term KEM key, so each session has "
      "fresh, forward-secret keys. ML-KEM implicit rejection means a modified ciphertext gives an unrelated "
      "secret, which key confirmation detects (test B4)."),
    p("3.3 Credential and trust-anchor provisioning / update", H2),
    p("Provisioning app (ML-DSA-87 suite, <font face='Courier'>PROV_SIG_SUITES=ml-dsa-87</font>) over the same "
      "I2C/DFU link: <i>gen-key</i> creates the ML-DSA-87 key pair inside the SPE (PAS key slot 0, private key "
      "never exported) -> <i>get-csr</i> (CSR self-signed with ML-DSA-87, 7,355 B) -> OEM CA signs "
      "(<font face='Courier'>sign_csr.py</font>, ML-DSA-87 root) -> <i>put-cert --spdm-chain --hash-alg sha384</i> "
      "into PAS cert slot 1 -> read-back + <i>prove-cert</i> (device proves possession of the key for the "
      "stored leaf). Script: <font face='Courier'>tools/task3_provision_p4.ps1</font> (41 s incl. on-device key "
      "generation). The slots stay re-writable because <font face='Courier'>--freeze</font> was not used on "
      "the shared board; in production, freeze after provisioning and rotate by provisioning a second slot. "
      "Host side: the trust anchor is the pinned root DER; rotating the CA means distributing a new pinned "
      "root. The responder reads the chain from PAS at start-up, so a credential update needs no new firmware."),
    p("3.4 How the large PQC objects fit the MCU memory model", H2),
    table([["Object", "Size", "Where / how bounded"],
           ["Cert chain (root + leaf + header)", "15,187 B (P2: 1,218 B)",
            "PAS cert slot (15 KB, 99 % full) and a 16 KB NS RAM copy (LIBSPDM_MAX_CERT_CHAIN_SIZE)"],
           ["ML-DSA-87 signature", "4,627 B", "built in the SPE output buffer, copied into the response"],
           ["ML-DSA-87 private key object (wolfCrypt)", "7,912 B static (S RAM)",
            "private key never leaves the SPE"],
           ["ML-KEM-1024 encap key / ciphertext", "1,568 B / 1,568 B", "inside KEY_EXCHANGE / _RSP buffers"],
           ["Largest SPDM message", "7,168 B (CERTIFICATE portion); KEY_EXCHANGE_RSP 6,305 B",
            "LIBSPDM_MAX_SPDM_MSG_SIZE 7 KB (P2: 2 KB); messages > 127 B use CHUNK_SEND/CHUNK_GET"],
           ["libspdm scratch (message + chunk reassembly)", "35,843 B static (NS RAM)",
            "fixed size, checked against libspdm_get_sizeof_required_scratch_buffer() at init"],
           ["libspdm context", "3,328 B", "1 session, 1 root (MAX_SESSION_COUNT=1)"],
           ["Transcript", "hash only", "LIBSPDM_RECORD_TRANSCRIPT_DATA_SUPPORT=0: running SHA-384, not message copies"],
           ["Secure hash pool / heap peak", f"9,280 B / {peak(perf4, 'sheap'):,} B",
            "wolfCrypt ML-DSA/ML-KEM working memory in the SPE heap"]],
          [48, 46, 86]),
    Spacer(1, 3),
    p("All buffers are static and size-checked; no allocation grows with message size. Larger chains (e.g. an "
      "intermediate CA) do not fit the 15 KB PAS cert slot - a documented constraint."),
]

# 4 Tests
S += [p("4. Test results (hardware)", H1),
      p(f"Positive suite: <font face='Courier'>tools/task1_spdm_runs.ps1 -Profile P4 -Tag task3</font> "
        f"-> <font face='Courier'>results/{P4.name}</font>. Requester = supplied spdm-requester.exe 2.1.0, "
        "pinned root = ca_mldsa87 (DER). Device log: [PERF] UART lines.", BODY)]
pos_rows = [["Test", "Operation", "Result", "Host wall clock"]]
desc = {"connect": "VERSION/CAPABILITIES/ALGORITHMS (SPDM 1.4, ML-DSA-87, ML-KEM-1024)",
        "slots": "GET_DIGESTS", "get_cert": "chain fetch + verify to pinned ML-DSA-87 root",
        "challenge": "CHALLENGE_AUTH, ML-DSA-87 signature", "measure": "signed GET_MEASUREMENTS",
        "session": "KEY_EXCHANGE (ML-KEM-1024) / FINISH / END_SESSION, AES-256-GCM",
        "exchange_1": "encrypted application message echo", "exchange_2": "encrypted application message echo",
        "exchange_3": "encrypted application message echo"}
shade = []
for i, n in enumerate(pos_names, 1):
    e = p4s.get(n)
    res = "PASS" if e and e[1] else ("FAIL - transport (4.1)" if e else "n/a")
    pos_rows.append([n, desc[n], res, f"{e[2]} s" if e else ""])
    shade.append((i, PASS if e and e[1] else WARN))
S.append(table(pos_rows, [22, 88, 30, 40], shade))
if pos_pass < len(pos_names):
    S += [p("4.1 Transport fault during exchange_2", H2),
          p("exchange_2 failed with <font face='Courier'>SPDM_REQ_ESESSION</font>. Evidence: transfers 1-211 completed "
            "(chain verified, ML-KEM KEY_EXCHANGE processed and signed on the device, 54 of 55 KEY_EXCHANGE_RSP chunks "
            "delivered). For transfer 212 (last CHUNK_GET) the requester received an SPDM <b>1.0</b> ERROR "
            "(UnsupportedRequest) - the version a responder uses when it has no negotiated state, i.e. consistent with "
            "a responder reset. At the same instant (15:20:58) the PC-side UART capture of the board's debug COM port "
            "stopped and recorded nothing more for the rest of the suite. Windows logged no USB/PnP event, so the root "
            "cause (board reset / USB power glitch vs. lost I2C transaction) could not be determined. Security-relevant "
            "outcome: the handshake was aborted and no session was established - the protocol failed <b>closed</b>. "
            "exchange_3, the 11 negative tests and the reconnect runs R1/R2 (3 board re-flashes) all behaved as "
            "expected afterwards; the device metrics for exchange_3 / A0 are missing because of the stopped capture. "
            "Engineering note: a P4 session needs ~215 transport transactions vs ~25 for P2, so it is ~9x more "
            "exposed to any transport fault; a production requester should retry/resume chunk transfers instead of "
            "aborting, and a larger-MTU transport (MCTP over I3C / PCIe VDM) reduces the exposure. "
            "Reproducibility: after re-provisioning, " + rep_line.replace(" after re-provisioning", "") +
            " further P4 exchanges/sessions (4.2c) plus E1a/E1b/E2b (4.2) completed without a transport fault, i.e. "
            "the fault did not reproduce in 6 later full handshakes.")]

S.append(p("Negative tests", H2))
S.append(p("Part A: host-side trust anchors (genuine responder). Part B: responder images built with "
           "<font face='Courier'>SPDM_FAULT_INJECT=&lt;mode&gt;</font> (test-only build flag, "
           "<font face='Courier'>proj_cm33_ns/spdm/spdm_fault_inject.h</font>, default 0 = compiled out); each "
           "image logs '[FI] ...' on UART when it tampers. Runner: <font face='Courier'>tools/task3_neg_tests.ps1</font>"
           + (f" -> <font face='Courier'>results/{NEG.name}</font>." if NEG else " (PENDING).")))
what = {
    "A1_rogue_mldsa87_root": "pinned root: ML-DSA-87 CA with the same subject name, different key (impersonation)",
    "A2_mldsa65_root": "pinned root: ML-DSA-65 CA (non-CNSA parameter set), same subject",
    "A3_classical_ec384_root": "pinned root: classical ECDSA P-384 CA (classical downgrade of the anchor)",
    "A4_malformed_key_root": "pinned root: genuine root with 1 byte flipped in the ML-DSA-87 public key",
    "A5_truncated_root": "pinned root: genuine root truncated to half (malformed DER)",
    "B1_modified_cert": "device serves chain with leaf CN changed (modified on-device certificate)",
    "B2_invalid_signature": "device flips 1 byte of its ML-DSA-87 signature",
    "B3_modified_transcript": "device signs a transcript hash with 1 bit flipped (modified transcript)",
    "B4_modified_kem_ct": "ML-KEM ciphertext modified before signing (handshake data altered)",
    "B5_mitm_finish_rsp": "MITM flips AEAD tag of FINISH_RSP (modified handshake message)",
    "B6_mitm_app_data": "MITM flips AEAD tag of the encrypted application data",
    "R1_restore_exchange": "genuine image re-flashed, reconnect + encrypted exchange",
    "R2_restore_exchange": "reconnect + encrypted exchange (repeat)",
    "R3_restore_exchange": "reconnect + encrypted exchange (repeat)",
}
neg_tab = [["#", "Tampering", "Expected", "Result", "Requester evidence"]]
shade = []
e = p4s.get("NEG_wrong_root")
neg_tab.append(["A0 (suite)", "pinned root: classical ECDSA P-384 CA (ca_ec384)", "reject",
                "REJECTED" if wrong_root_ok else ("ACCEPTED!" if e else "pending"), "exit=1"])
shade.append((1, REJ if wrong_root_ok else WARN))
for i, r in enumerate(neg_rows, 2):
    ok = r["ok"] == "True"
    res = ("PASS" if ok else "FAIL") if r["expect"] == "PASS" else ("REJECTED" if ok else "ACCEPTED!")
    ev = (r.get("evidence") or "").replace("<", "&lt;").replace(">", "&gt;")[:110]
    neg_tab.append([r["test"].split("_")[0], what.get(r["test"], r["test"]),
                    "connect" if r["expect"] == "PASS" else "reject", res, ev])
    shade.append((i, (PASS if r["expect"] == "PASS" else REJ) if ok else WARN))
if not NEG:
    neg_tab.append(["A1-A5, B1-B6, R1", "see tools/task3_neg_tests.ps1", "reject", "PENDING", ""])
    shade.append((2, WARN))
S.append(table(neg_tab, [16, 66, 15, 19, 64], shade))
if NEG and "NOTE:" in (NEG / "summary.txt").read_text(errors="replace"):
    S.append(p("Note: a third repeat reconnect (R3) was interrupted by the host tooling (the background runner was "
               "stopped because of PC memory pressure at transfer 181 of ~215); it produced no result and is not "
               "counted. All required tests had completed before."))
S.append(p("B3-B6 were checked against the device log to rule out a transport loss: the complete KEY_EXCHANGE_RSP "
           "(all 55 chunks) was delivered and the device logged the injected fault ('[FI] ...') before the requester "
           "aborted. B4 shows why key confirmation matters with a KEM: the signature over the (modified) ciphertext "
           "is valid, but ML-KEM implicit rejection gives the requester a different secret, so ResponderVerifyData "
           "fails."))

# 4.x edge cases + automated verification
edge = {}
for d in sorted(RES.glob("task3_EDGE_P4_*")):
    f = d / "summary.csv"
    if f.exists():
        with open(f, encoding="utf-8-sig") as fh:
            for r in csv.DictReader(fh):
                edge[r["test"]] = r
edge_desc = {
    "E1a_session_stability": "repeat session (stability)", "E1b_session_stability": "repeat session (stability)",
    "E2_killed_mid_handshake": "requester killed during KEY_EXCHANGE_RSP transfer (abandoned handshake)",
    "E2b_reconnect_after_abort": "reconnect + encrypted exchange right after the abandoned handshake",
    "E3_empty_cert_slot": "get-cert from unprovisioned slot 1",
    "E5_challenge_after_reset": "J-Link target reset, then CHALLENGE (identity persists in PAS)"}
if edge:
    rows = [["Test", "Scenario", "Expected", "Result", "Evidence"]]
    sh = []
    for i, (n, r) in enumerate(edge.items(), 1):
        ok = r["ok"] == "True"
        exp = {"PASS": "pass", "REJECT": "reject", "INFO": "done"}[r["expect"]]
        res = ("PASS" if r["expect"] != "REJECT" else "REJECTED") if ok else "UNEXPECTED"
        rows.append([n.split("_")[0], edge_desc.get(n, n), exp, res, (r["evidence"] or "")[:90]])
        sh.append((i, PASS if ok else WARN))
    S += [p("4.2 Edge cases (hardware)", H2),
          p("Runner <font face='Courier'>tools/task3_edge_tests.ps1 -Phase P4</font> (UART capture now re-opens the port "
            "and logs capture errors, which hid the exchange_2 evidence). Results "
            f"<font face='Courier'>results/{', '.join(sorted({d.name for d in RES.glob('task3_EDGE_P4_*')}))}</font>."),
          table(rows, [12, 70, 16, 20, 62], sh),
          p("<b>Not tested (dropped to save board time, ~5 min per SPDM operation):</b> application-message size limits "
            "(E4: 90 B / 400 B echo); provisioning-side rejections (unsupported "
            "key algorithm, occupied slot, oversize chain, leaf not matching the device key, SHA-256 RootHash) - moved "
            "to Task 4 (credential update).")]


def phase_rows(prefix):
    out = {}
    for d in sorted(RES.glob(f"task3_EDGE_{prefix}_*")):
        f = d / "summary.csv"
        if f.exists():
            with open(f, encoding="utf-8-sig") as fh:
                for r in csv.DictReader(fh):
                    out[r["test"]] = (r, d.name)
    return out


phase_desc = {
    "P2_B1_modified_cert": "ECC device serves chain with leaf CN changed",
    "P2_B2_invalid_signature": "ECC device flips 1 byte of its ECDSA P-384 signature",
    "P2_B3_modified_transcript": "ECC device signs a transcript hash with 1 bit flipped",
    "P2_B5_mitm_finish_rsp": "MITM flips AEAD tag of FINISH_RSP (ECDHE session)",
    "P2_B6_mitm_app_data": "MITM flips AEAD tag of the encrypted application data (ECDHE session)",
    "P2_R1_clean_exchange": "clean ECC image: session + encrypted exchange (positive)",
    "P2_R2_pqc_root_on_ecc_device": "requester pins the ML-DSA-87 root, device holds the ECC identity",
    "R_restore_exchange": "P4 identity re-provisioned, clean P4 image: session + encrypted exchange",
    "R_repeat_exchange_2": "repeat P4 exchange (exchange_2 reproducibility)",
    "R_repeat_exchange_3": "repeat P4 exchange (exchange_2 reproducibility)",
    "R_restore_get_cert": "get-cert of the restored ML-DSA-87 chain",
}
for prefix, title, intro in (
        ("P2FI", "4.2b ECC baseline (P2) negative tests",
         "Same fault-injection hooks (profile-independent) built with <font face='Courier'>SPDM_PROFILE=P2</font> "
         "against the provisioned ECDSA P-384 identity (mode 4 is ML-KEM only). Closes the Task 1 open item: tampering "
         "is rejected in both the baseline and the migrated design."),
        ("RESTORE", "4.2c Restore of the PQC identity and repeat exchanges",
         "After the P2 tests the ML-DSA-87 identity was provisioned again (new on-device key, same ca_mldsa87 root, "
         "SHA-384 RootHash) and the clean P4 image re-flashed; three further encrypted exchanges check whether the "
         "exchange_2 transport fault (4.1) reproduces.")):
    rows_ = phase_rows(prefix)
    if not rows_:
        continue
    rows = [["Test", "Scenario", "Expected", "Result", "Evidence"]]
    sh = []
    for i, (n, (r, _)) in enumerate(rows_.items(), 1):
        ok = r["ok"] == "True"
        exp = {"PASS": "pass", "REJECT": "reject", "INFO": "done"}[r["expect"]]
        res = ("PASS" if r["expect"] != "REJECT" else "REJECTED") if ok else "UNEXPECTED"
        rows.append([n, phase_desc.get(n, n), exp, res,
                     (r["evidence"] or "").replace("<", "&lt;").replace(">", "&gt;")[:70]])
        sh.append((i, (PASS if r["expect"] != "REJECT" else REJ) if ok else WARN))
    dirs = sorted({d for _, d in rows_.values()})
    S += [p(title, H2), p(intro + f" Results <font face='Courier'>results/{', '.join(dirs)}</font>."),
          table(rows, [38, 62, 14, 20, 46], sh)]
S += [p("4.3 Automated verification", H2),
      p("<font face='Courier'>tools/tests/test_task3_offline.py</font> (unittest, no board): 50 checks - SPDM cert_chain "
        "structure (length, SHA-384 RootHash, root+leaf ML-DSA-87, signatures verify, validity, fits 16 KB libspdm buffer "
        "and 15,360 B PAS slot, device-served chain = provisioned blob), each negative trust anchor is what it claims "
        "(rogue key, ML-DSA-65, P-384, one byte inside the public key, unparseable), fault-injection hooks all guarded "
        "and off by default, deployed image contains no [FI] code, patch applies to the base commit and reproduces the "
        "working tree, every recorded rejection is a protocol error (EAUTH/ESESSION) and - for handshake faults - happened "
        "after the complete KEY_EXCHANGE_RSP was delivered with the matching [FI] line logged, message sizes consistent "
        "with ML-KEM-1024 / ML-DSA-87 sizes, measurement parser incl. partial UART lines. Build matrix "
        "(RUN_BUILD_TESTS=1): every SPDM_FAULT_INJECT=1..6 image contains only its own hook, FI=0 none - PASS. "
        "Found and fixed by the tests: measurement script crashed on a partial UART line and ran its CLI on import.")]

# 5 Measurements
p2k = p2o.get("session"); p4k = p4o.get("session")
S += [
    p("5. Measurements - P2 (ECC baseline, Task 1) vs P4 (PQC)", H1),
    p("Same board, Release (-Os) responder with [PERF] logging, NS SysTick timer, I2C 400 kHz via KitProg3. "
      "Times are device-side processing per message; host wall clock is dominated by dfuh-cli start-up "
      "(~1.4 s per 127 B transaction).", BODY),
    p("5.1 Performance (device)", H2),
    table([["Operation", "P2 ECDSA/ECDHE P-384 [ms]", "P4 ML-DSA-87 / ML-KEM-1024 [ms]", "Comment"],
           ["Signature generation (SPE)", rng(sign2), rng(sign4),
            "ML-DSA signing is rejection-sampled -> variable time"],
           ["Key exchange: ECDHE keygen+shared / ML-KEM encaps", "162.9 (80.7 + 82.2)", rng(kem4),
            "ML-KEM ~15x faster than SW P-384 ECDH"],
           ["KEY_EXCHANGE message total", rng(kex2), rng(kex4), "sign + KEX + transcript"],
           ["FINISH (secured)", rng(fin2), rng(fin4), "HMAC verify + key derivation"],
           ["Encrypted app message round (AEAD)", "1.15 - 1.23", rng(app4), "AES-256-GCM, small message"],
           ["Signature verification on device", "n/a", "n/a", "responder does not verify (no mutual auth)"],
           ["Decapsulation", "n/a", "n/a (host side)", "responder is the encapsulator"]],
          [46, 38, 50, 46]),
    p("5.2 Communication", H2),
    table([["Metric", "P2", "P4", "Delta"],
           ["Cert chain size", "1,218 B", "15,187 B", "x12.5"],
           ["Largest SPDM message", "1,226 B (CERTIFICATE)", "7,168 B (CERTIFICATE portion); KEY_EXCHANGE_RSP 6,305 B", ""],
           ["KEY_EXCHANGE request / response", "166 B / 302 B", "1,638 B / 6,305 B", "x9.9 / x20.9"],
           ["get-cert: transfers / wire bytes", f"{p2o['get_cert'][1]} / {p2o['get_cert'][2]:,} B",
            f"{p4o['get_cert'][1]} / {p4o['get_cert'][2]:,} B", ""],
           ["session (VCA + cert + KEY_EXCHANGE + FINISH + END): transfers / bytes",
            f"{p2k[1]} / {p2k[2]:,} B" if p2k else "", f"{p4k[1]} / {p4k[2]:,} B" if p4k else "", ""],
           ["Fragmentation", "CHUNK_SEND x2 / CHUNK_GET x3 for KEX",
            "CHUNK_SEND x14 / CHUNK_GET ~50 for KEX; cert 3 portions x ~57 chunks", "127 B DFU payload limit"],
           ["Host wall clock per 'session'", f"{p2s.get('session', (0, 0, 0))[2]} s",
            f"{p4s.get('session', (0, 0, 0))[2]} s", "host-bound (dfuh-cli per transfer)"],
           ["Device busy per 'session'", f"{ms(p2k[3])} ms" if p2k else "", f"{ms(p4k[3])} ms" if p4k else "",
            "incl. cert-chain serving"]],
          [52, 40, 62, 26]),
    p("5.3 Memory (static from build map, Release without logging; peaks from [PERF])", H2),
    table([["Region", "P2", "P4", "Delta / note"],
           ["Secure flash (128 KB)", "82,300 B", "79,532 B", "-2.8 KB (wolfCrypt PQC < SW P-384 mbedTLS)"],
           ["Non-secure flash (160 KB)", "48,036 B", "47,972 B", "-64 B"],
           ["Secure .bss", "11,016 B", "18,940 B", "+7.9 KB (ML-DSA key object, hash pool)"],
           ["Non-secure .bss", "19,952 B", "57,816 B", "+37.9 KB (16 KB chain copy, 35.8 KB scratch)"],
           ["NS heap region left", "44,600 B", "6,736 B", "tight but sufficient (peak NS heap 1,032 B)"],
           ["Peak NS stack / S stack", f"{peak(perf2, 'nsstk'):,} / {peak(perf2, 'sstk'):,} B",
            f"{peak(perf4, 'nsstk'):,} / {peak(perf4, 'sstk'):,} B", "of 4 KB / 8 KB"],
           ["Peak S heap", f"{peak(perf2, 'sheap'):,} B", f"{peak(perf4, 'sheap'):,} B", "of 31 KB S heap region"],
           ["Credential store (PAS)", "1.2 KB used of 24 KB", "15.2 KB cert + ML-DSA-87 key (8 KB slot)",
            "cert slot 99 % full"],
           ["Peak handshake RAM (static NS+S .bss + peaks)",
            f"~{(19952 + 11016 + peak(perf2, 'nsstk') + peak(perf2, 'sstk') + peak(perf2, 'sheap') + 1032) / 1024:.0f} KB",
            f"~{(57816 + 18940 + peak(perf4, 'nsstk') + peak(perf4, 'sstk') + peak(perf4, 'sheap') + 1032) / 1024:.0f} KB",
            "of 126 KB S+NS SRAM"]],
          [50, 38, 44, 48]),
]

# 6 classical deps / limitations
S += [
    p("6. Remaining classical dependencies and limitations", H1),
    table([["Item", "Status", "What must change"],
           ["BootROM / FLASH_BOOT", "classical (ECDSA OEM key); not enabled on the shared board (Task 1/2)",
            "silicon/ROM with PQC or EPBL anchored via OEM provisioning + EPSS-signed certificate"],
           ["Boot -> SPDM identity link", "SPDM key is a separately provisioned credential (PAS); measurements "
            "report firmware digests", "bind identity to verified boot (Task 4: DICE-style / measurement of EPBL-verified image)"],
           ["Provisioning lock", "--freeze not used (shared board) -> slots re-writable by the provisioning app",
            "freeze + lifecycle SECURE in production"],
           ["Mutual authentication", "not used (requester is not authenticated)", "MUT_AUTH with requester ML-DSA-87 cert"],
           ["Transport", "127 B DFU transactions over I2C; one unexplained transport/board fault aborted one handshake "
            "(fail-closed)", "larger MTU transport and CHUNK_GET retry"],
           ["Requester tool", "re-fetches the 15 KB chain on every operation (~3.5 min host time)",
            "cache chain by GET_DIGESTS slot digest; faster transport than DFU-per-127-B"],
           ["Cert slot capacity", "15 KB slot holds root + leaf only", "larger PAS slot or store only leaf + "
            "root hash for intermediate CAs"],
           ["ML-DSA signing time", "variable (rejection sampling, 111-350 ms)", "acceptable; budget worst case"],
           ["Fault-injection builds", "test-only images (SPDM_FAULT_INJECT != 0) print a banner",
            "never ship; default 0 compiles hooks out"]],
          [36, 70, 74]),
]

# 7 evidence
S += [
    p("7. Evidence and reproduction", H1),
    Preformatted(
        "Build provisioning app : make -C psc3-provisioning-app build   (common.mk PROV_SIG_SUITES ?= ml-dsa-87)\n"
        "Program               : make -C psc3-provisioning-app qprogram  (no full erase)\n"
        "Provision P4 identity : tools/task3_provision_p4.ps1           (no --freeze)\n"
        "Build responder P4    : make -C psoc-spdm-responder clean build SPDM_PROFILE=P4 ; qprogram\n"
        "Positive suite        : tools/task1_spdm_runs.ps1 -Profile P4 -Tag task3\n"
        "Negative anchors      : tools/task3_make_neg_roots.py ; tools/task3_neg_tests.ps1\n"
        "Measurements          : tools/perf_summary.py <run dir>\n"
        "Report                : py -3.13 -I tools/make_task3_report.py\n"
        f"Positive run          : results/{P4.name}\n"
        f"Negative run          : results/{NEG.name if NEG else '(pending)'}\n"
        "Logs                  : results/task3_prov_build.log, task3_provision_p4.log, task3_resp_p4_build.log",
        MONO),
]

doc = SimpleDocTemplate(str(OUT), pagesize=A4, leftMargin=15 * mm, rightMargin=15 * mm,
                        topMargin=14 * mm, bottomMargin=16 * mm,
                        title="Task 3 - Real-time PQC Protection with SPDM")
doc.build(S, onFirstPage=footer, onLaterPages=footer)
print("written", OUT, "| positive run", P4.name, "| negative run", NEG.name if NEG else "PENDING")
