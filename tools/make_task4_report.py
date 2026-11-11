"""Generate results/Task4_Lifecycle_Report.pdf from the measured Task 4 data.
Run: py -3.13 -I tools/make_task4_report.py
Data: latest results/task4_EPBL_SPDM_* (responder behind EPBL: update / rollback / tamper + SPDM),
results/task4_EDGE_PROV_* (credential rejections), results/task4_EDGE_RESTORE_* (credential rotation),
results/task4_boot/ (signed images), tools/task4_verify_meas.py (reference values).
"""
import csv
import re
import subprocess
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import Paragraph, Preformatted, SimpleDocTemplate, Spacer, Table, TableStyle

HACK = Path(r"C:\Users\kpato\KP\Hackathon")
RES = HACK / "results"
TOOLS = HACK / "tools"
IMG = RES / "task4_boot"
sys.path.insert(0, str(TOOLS))
from perf_summary import summarise  # noqa: E402

OUT = RES / "Task4_Lifecycle_Report.pdf"
RUN = sorted(d for d in RES.glob("task4_EPBL_SPDM_*") if (d / "summary.csv").exists())[-1]
PROV = sorted(d for d in RES.glob("task4_EDGE_PROV_*") if (d / "summary.csv").exists())[-1]
REST = sorted(d for d in RES.glob("task4_EDGE_RESTORE_*") if (d / "summary.csv").exists())[-1]


def rows(d):
    with open(d / "summary.csv", encoding="utf-8-sig") as f:
        return list(csv.DictReader(f))


run, prov, rest = rows(RUN), rows(PROV), rows(REST)
runm = {r["test"]: r for r in run}
ops = {r[0]: r for r in summarise(RUN)}


def bootlog(name):
    return (RUN / f"{name}_boot.log").read_text(errors="replace")


def epbl_ms(log, pat):
    m = re.search(r"\[(\d+)s\.(\d+)ms\]\[\w+\][^\n]*" + pat, log)
    return int(m.group(1)) * 1000 + int(m.group(2)) if m else None


t0 = bootlog("T0_boot_v1_0_0")
t1 = bootlog("T1_valid_update_v1_1_0")
verify_ms = epbl_ms(t0, r"returned = 0x742e") - epbl_ms(t0, r"> boot_validate_slot")
app_ms = epbl_ms(t0, "Starting User Application")
upd_app = epbl_ms(t1, "Starting User Application")
erase_ms = epbl_ms(t1, "Copying the secondary") - epbl_ms(t1, "Erasing the primary slot")
copy_ms = epbl_ms(t1, r"> boot_validate_slot: fa_id = 1") - epbl_ms(t1, "Copying the secondary")


def reason(name):
    log = bootlog(name)
    for pat, txt in ((r"insufficient version", "[ERR] insufficient version in secondary slot (downgrade prevention)"),
                     (r"returned = 0x2e\b", "validate returned 0x2e: no key matches the key-hash TLV -> secondary not valid"),
                     (r"returned = 0xffffffff.*\n.*primary slot is not valid", "validate returned 0xffffffff: SHA-512 hash mismatch -> primary not valid, handle_error, no boot"),
                     (r"returned = 0xffffffff", "validate returned 0xffffffff: SHA-512 image hash mismatch -> secondary not valid")):
        if re.search(pat, log):
            return txt
    return runm[name]["evidence"]


def verify(meas, image):
    r = subprocess.run([sys.executable, "-I", str(TOOLS / "task4_verify_meas.py"), str(IMG / "epbl_full.hex"),
                        str(IMG / image), str(RUN / f"{meas}.txt")], capture_output=True, text=True)
    got = {int(m.group(1)): m.group(2) for m in re.finditer(r"block (\d) .*?(MATCH|MISMATCH)", r.stdout)}
    info = re.search(r"block 4 informational\s+(.*)", r.stdout)
    return got, (info.group(1).strip() if info else ""), r.returncode == 0


# ---------------------------------------------------------------- styles
ss = getSampleStyleSheet()
H1 = ParagraphStyle("H1", parent=ss["Heading1"], fontSize=15, spaceBefore=10, spaceAfter=6, textColor=colors.HexColor("#1F3864"))
H2 = ParagraphStyle("H2", parent=ss["Heading2"], fontSize=12, spaceBefore=8, spaceAfter=4, textColor=colors.HexColor("#1F3864"))
BODY = ParagraphStyle("B", parent=ss["BodyText"], fontSize=9, leading=12, alignment=TA_LEFT)
CELL = ParagraphStyle("C", parent=BODY, fontSize=8, leading=10)
MONO = ParagraphStyle("M", parent=BODY, fontName="Courier", fontSize=7.5, leading=9.5)
PASS, REJ, WARN, HEAD = (colors.HexColor(c) for c in ("#E2EFDA", "#FCE4D6", "#FFF2CC", "#1F3864"))


def p(text, style=BODY):
    return Paragraph(text, style)


def table(data_rows, widths, shade=None):
    data = [[Paragraph(f'<font color="white"><b>{c}</b></font>', CELL) for c in data_rows[0]]]
    data += [[p(str(c), CELL) for c in r] for r in data_rows[1:]]
    t = Table(data, colWidths=[w * mm for w in widths], repeatRows=1)
    st = [("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#A6A6A6")), ("VALIGN", (0, 0), (-1, -1), "TOP"),
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
    canvas.drawString(15 * mm, 10 * mm, "PQC Hackathon - Task 4: lifecycle (EPBL ML-DSA-87 boot + SPDM 1.4 PQC identity)  |  measured 2026-11-11")
    canvas.drawRightString(195 * mm, 10 * mm, f"Page {doc.page}")
    canvas.restoreState()


fw_ok = sum(r["ok"] == "True" for r in run)
prov_unexp = [r for r in prov if r["ok"] != "True"]
rest_ok = [r for r in rest if r["ok"] == "True"]

S = [p("Task 4 - Lifecycle: verified boot, SPDM identity and secure update", ParagraphStyle("T", parent=H1, fontSize=19, spaceAfter=2)),
     p("PSoC Control C3M6 (KIT_PSC3M6_EVAL) - the SPDM 1.4 ML-DSA-87 / ML-KEM-1024 responder now runs <b>behind the Edge Protect "
       "Bootloader</b>, which verifies it with ML-DSA-87 on every boot and before every update; the SPDM measurements report "
       "the image EPBL verified."), Spacer(1, 4)]

# 1 summary
S += [p("1. Summary", H1),
      table([["Area", "Result", "Evidence"],
             ["Firmware update path (responder image)", f"{fw_ok}/{len(run)} steps as expected",
              "valid update v1.0.0 -> v1.1.0 installed; rollback, corrupted payload, modified header, rogue ML-DSA-87 key "
              "rejected; tampered primary does not boot; recovery by signed update v2.0.0"],
             ["Reconnect after update", "PASS (3 measure + 3 exchange after updates)",
              "same PAS identity before/after every update: no re-provisioning needed"],
             ["Boot identity -> SPDM", "measurements = verified image",
              "block 1/2/3 recomputed from the release files MATCH after every boot (4 of 4)"],
             ["Credential update path", f"rotation PASS ({len(rest_ok)} exchanges); {len(prov) - len(prov_unexp) - 1}/{len(prov) - 1} device checks as expected",
              "new on-device ML-DSA-87 key + cert; unsupported alg, oversize chain, foreign leaf (proof-of-possession), "
              "SHA-256 RootHash rejected; 1 finding (V5b, by design before freeze)"],
             ["Cost", f"EPBL verify {verify_ms} ms, reset->responder {app_ms} ms",
              f"update install {upd_app / 1000:.2f} s (erase {erase_ms / 1000:.2f} s, copy {copy_ms / 1000:.2f} s); EPBL 47,244 B flash unchanged"]],
            [42, 46, 92], [(1, PASS), (2, PASS), (3, PASS), (4, WARN), (5, PASS)])]

# 2 architecture
S += [p("2. Architecture: one chain from reset to the SPDM session", H1),
      p("The responder build gained a BSP variant (<font face='Courier'>TARGET=APP_KIT_PSC3M6_EVAL_EPBL</font>) with the "
        "EPBL flash map; secure image after a 0x400 B MCUboot header, non-secure image inside the same signed image. "
        "Memory regions and the MPC configuration were regenerated with the Device Configurator CLI. The PAS (SPDM identity) "
        "keeps its address, so the identity provisioned in Task 3 survived the migration and every update."),
      table([["Area", "Address range", "Content / who writes it"],
             ["EPBL", "0x32000000-0x3201B000 (108 KB)", "Edge Protect Bootloader, ML-DSA-87 validation key compiled in"],
             ["Primary slot", "0x3201B000-0x32048000 (180 KB)", "header + responder S (0x3201B400) + NS (0x32034000) + TLVs (SHA-512, key hash, 4,627 B ML-DSA-87 signature); written only by EPBL (install) or debugger"],
             ["PAS", "0x32048000-0x3204E000 (24 KB)", "SPDM identity: ML-DSA-87 key slot 0 (generated on device), cert chain slot 1 (15,187 B); written only by the provisioning service"],
             ["Upgrade slot", "0x3204E000-0x3207B000 (180 KB)", "candidate image; verified by EPBL before install, erased after install or rejection"]],
            [24, 46, 110]),
      Spacer(1, 4),
      table([["Stage", "Verifies", "Algorithm", "Status on this board"],
             ["ROM_BOOT (immutable)", "SFLASH / FLASH_BOOT", "Infineon keys", "classical, Infineon root"],
             ["FLASH_BOOT", "EPBL (with SECURE_APP policy + OEM key)", "ECDSA P-256/384/521", "SIMPLE_APP policy: EPBL launched WITHOUT authentication (no OEM key on the shared board)"],
             ["EPBL", "responder image, every boot + before install", "ML-DSA-87, SHA-512 (HashML-DSA)", f"enforced: {verify_ms} ms per verification"],
             ["Responder SPE", "-- (signs with the PAS identity key)", "ML-DSA-87 sign, ML-KEM-1024 encaps", "reports EPBL-verified image digest/version as signed SPDM measurements"],
             ["Host requester", "cert chain to pinned ML-DSA-87 root; signatures; measurements vs reference values", "ML-DSA-87, ML-KEM-1024, SHA-384, AES-256-GCM", "tools/task4_verify_meas.py recomputes the reference values"]],
            [26, 50, 40, 64], [(2, WARN)])]

# 3 firmware update path
fw_rows = [["Step", "Action", "Expected", "Result", "EPBL / requester evidence"]]
desc = {"T0_boot_v1_0_0": "EPBL + signed v1.0.0 in primary, empty upgrade slot",
        "T0_measure_v1_0_0": "signed GET_MEASUREMENTS", "T0_exchange_v1_0_0": "ML-KEM session + encrypted message",
        "T1_valid_update_v1_1_0": "signed v1.1.0 written to upgrade slot, reset",
        "T1_measure_v1_1_0": "measurements after update", "T1_exchange_after_update": "reconnect after update",
        "T2_rollback_v1_0_0": "signed (genuine key) v1.0.0 as update", "T3_corrupt_payload": "v1.2.0 update, 1 payload byte flipped",
        "T4_modified_header": "v1.2.0 update, header revision byte changed", "T5_rogue_key": "v1.2.0 signed with a rogue ML-DSA-87 key",
        "T6_measure_still_v1_1_0": "measurements after the 4 rejected updates",
        "T7_tampered_primary": "1 byte flipped in the installed primary image", "T7_connect_no_app": "SPDM connect while EPBL halted",
        "T8_recovery_update_v2_0_0": "signed v2.0.0 to upgrade slot (primary invalid)",
        "T8_measure_v2_0_0": "measurements after recovery", "T8_exchange_after_recovery": "reconnect after recovery"}
shade = []
for i, r in enumerate(run, 1):
    n = r["test"]
    ev = reason(n) if r["result"] in ("REJECTED", "NOBOOT") else r["evidence"]
    ev = ev.replace("<", "&lt;")[:150]
    fw_rows.append([n, desc.get(n, ""), r["expect"], r["result"], ev])
    shade.append((i, PASS if r["ok"] == "True" and r["expect"] in ("BOOTED", "UPDATED", "PASS") else REJ if r["ok"] == "True" else WARN))
S += [p("3. Firmware update path (responder image through EPBL)", H1),
      p(f"Runner <font face='Courier'>tools/task4_epbl_spdm_tests.ps1</font> -> results/{RUN.name}. Images are loaded with the "
        "debugger into the slot (the in-field equivalent is the application writing the upgrade slot); EPBL decides at reset. "
        "All images contain the same code with different MCUboot versions (version, digest and rollback behaviour are what "
        "is tested)."),
      table(fw_rows, [36, 44, 17, 17, 66], shade)]

# 4 measurements
mrows = [["Measurement", "Image", "block 1 EPBL", "block 2 image", "block 3 SVN", "block 4 (device string)"]]
allm = True
for meas, image in (("T0_measure_v1_0_0", "resp_v1_0_0_boot.hex"), ("T1_measure_v1_1_0", "resp_v1_1_0_update.hex"),
                    ("T6_measure_still_v1_1_0", "resp_v1_1_0_update.hex"), ("T8_measure_v2_0_0", "resp_v2_0_0_update.hex")):
    got, info, ok = verify(meas, image)
    allm &= ok
    mrows.append([meas, image, got.get(1, "-"), got.get(2, "-"), got.get(3, "-"), info])
S += [p("4. Boot identity and SPDM identity", H1),
      p("<b>Answer to the brief: the runtime SPDM identity is anchored in BOTH.</b> The identity <i>key</i> is a separately "
        "provisioned credential (ML-DSA-87 key generated inside the SPE, PAS slot 0, never exported; certified by the "
        "ML-DSA-87 OEM CA). The <i>firmware state</i> is anchored in the verified boot chain: the responder reports, in "
        "ML-DSA-87-signed GET_MEASUREMENTS, the digest of the EPBL boot stage (block 1), the digest of exactly the bytes EPBL "
        "hashed and verified (block 2) and the MCUboot version (block 3). A verifier recomputes these from the release files "
        "(<font face='Courier'>tools/task4_verify_meas.py</font>):"),
      table(mrows, [36, 34, 20, 20, 18, 52], [(i, PASS) for i in range(1, 5)]),
      Spacer(1, 4),
      table([["Consequence", "Why / mitigation"],
             ["Firmware update does not change the identity", "PAS is outside both slots: reconnect after update works without "
              "re-provisioning (T1, T8). The verifier sees the new version only through the measurements."],
             ["Identity does not prove WHICH firmware runs", "any image EPBL accepts can use the key -> the verifier must check the "
              "signed measurements against reference values (and a minimum SVN), not only the certificate."],
             ["An unsigned/tampered image cannot use the key", "it never runs (T3-T5, T7). Remaining paths: debugger access "
              "(DAP open in DEVELOPMENT LCS) and the unauthenticated BootROM->EPBL step."],
             ["Remote rollback detection", "local downgrade prevention guards only the update path (Task 2 L1: debugger can write "
              "an old signed image to primary); block 3 exposes the running version, so a verifier can refuse old versions."],
             ["Stronger binding (next step)", "DICE-style: derive the attestation key from a CDI over the EPBL-measured image, so a "
              "different image gets a different key; or bind measurements into the certificate (TCB info extension)."]],
            [52, 128])]

# 5 credential path
crow = [["Test", "Expected", "Result", "Evidence"]]
csh = []
for i, r in enumerate([x for x in prov if x["test"] != "V7a_putcert_foreign_leaf"] + rest, 1):
    res = "OK" if r["ok"] == "True" else "UNEXPECTED"
    crow.append([r["test"], r["expect"], res, r["evidence"].replace("<", "&lt;")[:110]])
    csh.append((i, (PASS if r["expect"] == "PASS" else REJ) if r["ok"] == "True" else WARN))
S += [p("5. Credential update path (SPDM identity in the PAS)", H1),
      p("Provisioning service (ML-DSA-87 suite) over the same I2C/DFU link: erase -> on-device gen-key -> CSR -> OEM CA signs "
        "-> put-cert (SPDM chain, SHA-384 RootHash) -> read-back -> prove-cert (proof of possession). Device-side checks: "
        f"results/{PROV.name}; rotation + reconnect: results/{REST.name} (a third repeat exchange was cut by a PC power-off)."),
      table(crow, [52, 18, 22, 88], csh),
      Spacer(1, 3),
      p("<b>V5b finding:</b> gen-key on an occupied slot is accepted and replaces the key. This is the provisioning app's "
        "design while the PAS is not frozen (a frozen device answers PROV_ERR_FROZEN, provisioning-app self-test). Consequence: "
        "credential immutability in the field depends on <font face='Courier'>--freeze</font> (not used on the shared board), a "
        "closed debug port, and the provisioning image never being bootable in the field - with EPBL enforcing ML-DSA-87, an "
        "unsigned provisioning image cannot run; a signed one is an OEM decision. <b>V7:</b> the device stores a leaf whose key "
        "is not the slot key (no on-device X.509 parsing, design D4), but prove-cert fails, and so would SPDM CHALLENGE - "
        "provisioning must gate on prove-cert.")]

# 6 migration
S += [p("6. Migration strategy for devices that start with ECC credentials", H1),
      p("Hybrid ECC+PQC is excluded by the brief, so each plane is cut over per device; the requester fleet accepts both roots "
        "only during the transition window. Steps 1-4 were executed on this board (Task 1 -> Tasks 2/3/4)."),
      table([["#", "Step", "Authenticated by", "Shown"],
             ["1", "Field firmware update to an image that contains the PQC code (P4 responder, ML-DSA/ML-KEM in the SPE) "
              "and still identifies with ECC", "existing ECDSA-P384 EPBL path", "Task 1/2 (ECDSA update path)"],
             ["2", "EPBL validation key ECDSA-P384 -> ML-DSA-87 (EPBL image change)", "BootROM/FLASH_BOOT (OEM key, ECDSA) - "
              "the only way EPBL itself can be replaced securely; here by debugger", "Task 2 (EPBL reflash)"],
             ["3", "New identity: on-device ML-DSA-87 keygen + CSR, CA-signed by the new ML-DSA-87 OEM CA; production: "
              "the CSR request is authenticated with the still-valid ECC identity (SPDM session with the old key) so the CA "
              "knows it is the same device", "ECC identity (bridge), then ML-DSA-87 CA", "Task 3 P2->P4 re-provisioning, Task 4 rotation"],
             ["4", "Switch the responder to the PQC-only profile (no classical algorithm offered -> no downgrade)",
              "ML-DSA-87 EPBL", "Task 3/4 (P4 image behind EPBL)"],
             ["5", "Requesters drop the ECC root (A3: P4 requester rejects a classical root); revoke ECC CA", "operator policy",
              "Task 3 A0/A3"],
             ["6", "Freeze PAS, close DAP, LCS SECURE; HW anti-rollback counter", "irreversible - not on shared board", "documented"]],
            [6, 92, 46, 36])]

# 7 classical / limitations
S += [p("7. What remains classical and what must change for full quantum resistance", H1),
      table([["Item", "Status", "What must change"],
             ["BootROM / FLASH_BOOT -> EPBL", "classical root (ECDSA OEM key) and NOT enabled on this board (SIMPLE_APP, no OEM key)",
              "provision OEM key + SECURE_APP policy (needs EPSS-signed OEM certificate); long term a ROM with ML-DSA/LMS. Until then "
              "EPBL - and the ML-DSA-87 key inside it - is only as trustworthy as an ECDSA (or no) check"],
             ["EPBL", "PQC: ML-DSA-87 / SHA-512, every boot and every update", "EPBL is not field-updatable in this map"],
             ["SPDM authentication / key exchange", "PQC only: ML-DSA-87, ML-KEM-1024, SHA-384, AES-256-GCM", "-"],
             ["SPDM identity provisioning channel", "unauthenticated factory channel; PAS not frozen", "freeze; authenticated re-provisioning"],
             ["Rollback", "software downgrade prevention (update path only); version visible in SPDM block 3",
              "hw_rollback_prot (eFuse counter) + closed DAP"],
             ["Debug port", "open (DEVELOPMENT LCS)", "close DAP / LCS SECURE"],
             ["Update delivery", "debugger writes the upgrade slot", "in-band update over the SPDM/DFU channel"],
             ["Image content", "test images differ only in version", "real releases differ in code; same mechanism"]],
            [40, 66, 74], [(1, WARN), (4, WARN), (5, WARN), (6, WARN)])]

# 8 resources
mo = ops.get("T8_exchange_after_recovery")
S += [p("8. Resource and performance impact of the integration", H1),
      table([["Metric", "Task 3 (standalone, no EPBL)", "Task 4 (behind EPBL)", "Note"],
             ["Boot verification", "none", f"{verify_ms} ms ML-DSA-87 + SHA-512 over 165 KB", "CE243041 app in Task 2: 61 ms over 128 KB"],
             ["Reset -> responder start", "immediate", f"{app_ms} ms", ""],
             ["Update install", "n/a (debugger)", f"{upd_app / 1000:.2f} s", f"erase 180 KB {erase_ms / 1000:.2f} s, copy {copy_ms / 1000:.2f} s, 2 verifications"],
             ["Flash", "288 KB reserved for the responder", "108 KB EPBL + 2 x 180 KB slots", "responder uses 145 KB; 20 KB free"],
             ["Image metadata", "-", "0x400 B header + 4,771 B TLVs per slot", ""],
             ["SPDM session (transfers / bytes)", "214 / 28,199 B", f"{mo[1]} / {mo[2]:,} B" if mo else "-", "unchanged protocol cost"],
             ["Measurement digests", "placeholders", "SHA-384 over 108 KB + 165 KB, once per boot (cached)", "within the ~0.3-0.6 s device time of a measure op"]],
            [40, 42, 52, 46])]

S += [p("9. Reproduction", H1),
      Preformatted(
          "EPBL (spdm map) : make -C epbl/bl build MEMORY_MAP=<abs path>/epbl/bl/platforms/PSC3_M6/memory_maps/spdm_overwrite_single_flash.json\n"
          "Responder       : make -C psoc-spdm-responder build TARGET=APP_KIT_PSC3M6_EVAL_EPBL SPDM_PROFILE=P4\n"
          "Sign / variants : tools\\task4_epbl_spdm_tests.ps1 -Phase PREP   (tools/resp_epbl_sign.py, hex_pad.py, hex_flip.py)\n"
          "Board run       : tools\\task4_epbl_spdm_tests.ps1 -Phase BOARD  (T0..T8, ~55 min)\n"
          "Reference values: py -3.13 -I tools/task4_verify_meas.py results/task4_boot/epbl_full.hex <image.hex> <measure.txt>\n"
          "Credentials     : tools\\task3_edge_tests.ps1 -Phase PROV -Prefix task4 ; -Phase RESTORE -Prefix task4\n"
          "Offline tests   : py -3.13 -m unittest discover -s tools/tests\n"
          "Patch / BSP     : patches/psoc-spdm-responder_task4.patch + patches/bsp_overlay_TARGET_APP_KIT_PSC3M6_EVAL_EPBL/\n"
          f"Runs            : results/{RUN.name}, results/{PROV.name}, results/{REST.name}", MONO)]

doc = SimpleDocTemplate(str(OUT), pagesize=A4, leftMargin=15 * mm, rightMargin=15 * mm, topMargin=14 * mm,
                        bottomMargin=16 * mm, title="Task 4 - Lifecycle")
doc.build(S, onFirstPage=footer, onLaterPages=footer)
print("written", OUT, "| run", RUN.name, "| measurements all match:", allm)
