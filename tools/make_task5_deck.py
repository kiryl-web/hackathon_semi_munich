"""Task 5: build the presentation results/Task5_Presentation.pptx (+ charts in results/task5_charts/).
All numbers are the measured values from Tasks 1-4 (see results/TASK*_NOTES.md and the reports).
Run: py -3.13 -I tools/make_task5_deck.py      (PDF export: tools/task5_export_pdf.ps1)
"""
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import matplotlib.ticker  # noqa: E402
from pptx import Presentation  # noqa: E402
from pptx.dml.color import RGBColor  # noqa: E402
from pptx.enum.shapes import MSO_SHAPE  # noqa: E402
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR  # noqa: E402
from pptx.util import Inches, Pt, Emu  # noqa: E402

HACK = Path(r"C:\Users\kpato\KP\Hackathon")
RES = HACK / "results"
CH = RES / "task5_charts"
CH.mkdir(exist_ok=True)
OUT = RES / "Task5_Presentation.pptx"

# Reference categorical palette, slots 1-2 (validated all-pairs, light mode); text never wears series colour.
ECC, PQC = "#eb6834", "#2a78d6"
INK, INK2, MUTED, GRID, SURF = "#0b0b0b", "#52514e", "#8a8984", "#e6e5e1", "#fcfcfb"
NAVY = "#1F3864"


# ---------------------------------------------------------------- charts
def style_axes(ax):
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.spines["bottom"].set_color(MUTED)
    ax.tick_params(colors=INK2, labelsize=10, length=0)
    ax.xaxis.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def paired_hbar(fname, title, labels, ecc, pqc, unit, fmt="{:,.0f}", note=None, ecc_name="ECC baseline", pqc_name="PQC (CNSA 2.0)"):
    """Horizontal paired bars, one shared axis, value labels on every bar (identity not colour-only)."""
    n = len(labels)
    fig, ax = plt.subplots(figsize=(6.4, 0.75 * n + 1.3), dpi=200)
    fig.patch.set_facecolor(SURF)
    ax.set_facecolor(SURF)
    h = 0.36
    ys = list(range(n))[::-1]
    for y, e, q in zip(ys, ecc, pqc):
        ax.barh(y + h / 2 + 0.01, e, height=h, color=ECC, edgecolor=SURF, linewidth=1)
        ax.barh(y - h / 2 - 0.01, q, height=h, color=PQC, edgecolor=SURF, linewidth=1)
    xmax = max(max(ecc), max(pqc))
    for y, e, q in zip(ys, ecc, pqc):
        ax.text(e + xmax * 0.012, y + h / 2, fmt.format(e) + f" {unit}", va="center", fontsize=9, color=INK2)
        ax.text(q + xmax * 0.012, y - h / 2, fmt.format(q) + f" {unit}", va="center", fontsize=9, color=INK)
    ax.set_yticks(ys)
    ax.set_yticklabels(labels, fontsize=10, color=INK)
    ax.set_xlim(0, xmax * 1.28)
    style_axes(ax)
    fig.suptitle(title, x=0.01, ha="left", fontsize=12, color=INK, fontweight="bold")
    ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:,.0f}"))
    hs = [plt.Rectangle((0, 0), 1, 1, color=ECC), plt.Rectangle((0, 0), 1, 1, color=PQC)]
    ax.legend(hs, [ecc_name, pqc_name], loc="lower left", bbox_to_anchor=(0, 1.0), ncol=2, frameon=False,
              fontsize=9, labelcolor=INK2, handlelength=1.2, borderaxespad=0.2)
    if note:
        import textwrap
        fig.text(0.01, 0.01, "\n".join(textwrap.wrap(note, 105)), fontsize=7.5, color=MUTED, va="bottom")
    fig.tight_layout(rect=(0, 0.07 if note else 0, 1, 0.95))
    fig.savefig(CH / fname, facecolor=SURF)
    plt.close(fig)
    return CH / fname


c_time = paired_hbar("time.png", "Device time per operation (ms) - lower is better",
                     ["Identity signature", "Key establishment", "Boot image verification", "KEY_EXCHANGE message"],
                     [84.4, 162.9, 170, 252.2], [112.0, 11.1, 61, 132.3], "ms", "{:,.1f}",
                     "ECDSA-P384 / ECDHE-P384 (SW mbedTLS) vs ML-DSA-87 / ML-KEM-1024 (wolfCrypt). ML-DSA signing is "
                     "rejection-sampled: 66-346 ms, median shown.")
c_comm = paired_hbar("comm.png", "Bytes on the wire / in flash per object",
                     ["Certificate chain", "KEY_EXCHANGE response", "Full SPDM session", "Boot image metadata (TLVs)"],
                     [1218, 302, 2569, 215], [15187, 6305, 28199, 4771], "B", "{:,.0f}",
                     "Session = VERSION ... cert fetch ... KEY_EXCHANGE / FINISH / END_SESSION over 127 B DFU transfers "
                     "(24 -> 214 transfers).")
c_mem = paired_hbar("mem.png", "Memory (KB) - static + measured peaks",
                    ["Responder flash (S+NS)", "Responder static RAM (.bss)", "Secure heap peak", "EPBL flash", "EPBL peak boot RAM"],
                    [127.3, 30.2, 2.5, 35.0, 10.7], [124.5, 75.0, 27.1, 46.1, 25.2], "KB", "{:,.1f}",
                    "PQC costs RAM, not flash: 16 KB chain copy + 35 KB libspdm scratch (NS), wolfCrypt key/working memory (S). "
                    "Peak handshake RAM ~108 KB of 126 KB.")

# ---------------------------------------------------------------- deck helpers
prs = Presentation()
prs.slide_width, prs.slide_height = Inches(13.333), Inches(7.5)
BLANK = prs.slide_layouts[6]


def rgb(h):
    h = h.lstrip("#")
    return RGBColor(int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))


def text(slide, x, y, w, h, paras, size=16, color=INK, bold=False, align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.TOP):
    tb = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = tb.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = anchor
    for i, para in enumerate(paras if isinstance(paras, list) else [paras]):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        if isinstance(para, tuple):
            txt, opt = para
        else:
            txt, opt = para, {}
        lvl = opt.get("level", 0)
        p.level = lvl
        r = p.add_run()
        r.text = ("• " if opt.get("bullet", True) and lvl >= 0 and not opt.get("plain") else "") + txt if not opt.get("plain") else txt
        r.font.size = Pt(opt.get("size", size - 2 * lvl))
        r.font.bold = opt.get("bold", bold)
        r.font.color.rgb = rgb(opt.get("color", color))
        r.font.name = "Calibri"
        p.alignment = align
        p.space_after = Pt(opt.get("after", 6))
    return tb


def box(slide, x, y, w, h, fill, label, sub=None, fg="#ffffff", size=13, shape=MSO_SHAPE.ROUNDED_RECTANGLE, line=None):
    s = slide.shapes.add_shape(shape, Inches(x), Inches(y), Inches(w), Inches(h))
    s.fill.solid()
    s.fill.fore_color.rgb = rgb(fill)
    if line:
        s.line.color.rgb = rgb(line)
        s.line.width = Pt(1.5)
    else:
        s.line.fill.background()
    tf = s.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.CENTER
    r = p.add_run()
    r.text = label
    r.font.size = Pt(size)
    r.font.bold = True
    r.font.color.rgb = rgb(fg)
    if sub:
        p2 = tf.add_paragraph()
        p2.alignment = PP_ALIGN.CENTER
        r2 = p2.add_run()
        r2.text = sub
        r2.font.size = Pt(size - 3)
        r2.font.color.rgb = rgb(fg)
    return s


def arrow(slide, x1, y1, x2, y2, color=MUTED):
    c = slide.shapes.add_connector(1, Inches(x1), Inches(y1), Inches(x2), Inches(y2))
    c.line.color.rgb = rgb(color)
    c.line.width = Pt(2)
    ln = c.line._get_or_add_ln()
    from pptx.oxml.ns import qn
    tail = ln.makeelement(qn("a:tailEnd"), {"type": "triangle"})
    ln.append(tail)
    return c


def table(slide, x, y, w, rows, colw, size=12, h=0.36, header=NAVY, shade=None):
    t = slide.shapes.add_table(len(rows), len(rows[0]), Inches(x), Inches(y), Inches(w), Inches(h * len(rows))).table
    for j, cw in enumerate(colw):
        t.columns[j].width = Inches(cw)
    for i, r in enumerate(rows):
        for j, v in enumerate(r):
            cell = t.cell(i, j)
            cell.text = str(v)
            cell.margin_left = cell.margin_right = Inches(0.06)
            cell.margin_top = cell.margin_bottom = Inches(0.03)
            para = cell.text_frame.paragraphs[0]
            para.runs[0].font.size = Pt(size)
            para.runs[0].font.name = "Calibri"
            cell.fill.solid()
            if i == 0:
                cell.fill.fore_color.rgb = rgb(header)
                para.runs[0].font.bold = True
                para.runs[0].font.color.rgb = rgb("#ffffff")
            else:
                fill = (shade or {}).get(i, "#ffffff" if i % 2 else "#f3f3f1")
                cell.fill.fore_color.rgb = rgb(fill)
                para.runs[0].font.color.rgb = rgb(INK)
    return t


def slide(title, kicker=None, notes=""):
    s = prs.slides.add_slide(BLANK)
    bar = s.shapes.add_shape(MSO_SHAPE.RECTANGLE, 0, 0, prs.slide_width, Inches(0.12))
    bar.fill.solid()
    bar.fill.fore_color.rgb = rgb(PQC)
    bar.line.fill.background()
    text(s, 0.5, 0.3, 12.3, 0.8, [(title, {"plain": True, "bold": True, "size": 30, "color": NAVY})])
    if kicker:
        text(s, 0.5, 1.0, 12.3, 0.5, [(kicker, {"plain": True, "size": 16, "color": INK2})])
    foot = text(s, 0.5, 7.05, 12.3, 0.3, [("PQC Hackathon - PSoC Control C3M6 - ECC -> CNSA 2.0 (ML-DSA-87 / ML-KEM-1024)  |  "
                                            f"{len(prs.slides._sldIdLst)}", {"plain": True, "size": 10, "color": MUTED})])
    s.notes_slide.notes_text_frame.text = notes.strip()
    return s


def legend_chip(s, x, y, color, label):
    box(s, x, y, 0.28, 0.22, color, "", shape=MSO_SHAPE.RECTANGLE)
    text(s, x + 0.35, y - 0.07, 3.5, 0.35, [(label, {"plain": True, "size": 12, "color": INK2})])


# ---------------------------------------------------------------- 1 title
s = prs.slides.add_slide(BLANK)
bg = s.shapes.add_shape(MSO_SHAPE.RECTANGLE, 0, 0, prs.slide_width, prs.slide_height)
bg.fill.solid(); bg.fill.fore_color.rgb = rgb(NAVY); bg.line.fill.background()
text(s, 0.8, 2.0, 11.7, 1.4, [("Quantum-resistant SPDM device on PSoC Control C3M6", {"plain": True, "bold": True, "size": 40, "color": "#ffffff"})])
text(s, 0.8, 3.5, 11.7, 1.2, [("From ECC to CNSA 2.0: ML-DSA-87 verified boot + SPDM 1.4 with ML-DSA-87 / ML-KEM-1024 - on real hardware, measured",
                               {"plain": True, "size": 20, "color": "#d9e2f3"})])
text(s, 0.8, 5.4, 11.7, 0.9, [("SEMICON 2026 PQC Hackathon  |  KIT_PSC3M6_EVAL  |  ModusToolbox 3.9, Edge Protect Bootloader, libspdm 4.0",
                               {"plain": True, "size": 14, "color": "#b4c7e7"})])
s.notes_slide.notes_text_frame.text = (
    "Opening (20 s): We took Infineon's ECC-based SPDM code example on the PSoC Control C3M6 and migrated both planes - "
    "the boot chain and the runtime SPDM channel - to the CNSA 2.0 post-quantum algorithms, on the real evaluation kit, "
    "and measured what it costs. Everything you will see is from hardware runs, with positive and negative tests.")

# ---------------------------------------------------------------- 2 problem / CNSA mapping
s = slide("The target: CNSA 2.0, not 'some PQC'",
          "ECDSA and ECDH are quantum-vulnerable; datacenter attestation identities live for 10+ years (harvest-now, forge-later)",
          """Why (30 s): SPDM is how GPUs, NICs and storage controllers prove identity to the BMC. An ECC identity provisioned today
must stay trustworthy for the life of the hardware. CNSA 2.0 is our design constraint. Key point: the brief warns not to
assume ML-DSA-65 - CNSA 2.0 allows ONLY category-5 parameter sets: ML-DSA-87 and ML-KEM-1024, with SHA-384/512 and AES-256.
We enforce that: ML-DSA-65 images and roots are actively rejected in our tests. No hybrid mode, per the brief.""")
table(s, 0.5, 1.7, 12.3, [
    ["Function", "Original (ECC baseline)", "Migrated", "CNSA 2.0 requirement", "Enforced / tested"],
    ["Firmware signature (EPBL -> app)", "ECDSA-P384, SHA-384", "ML-DSA-87, SHA-512 pre-hash", "ML-DSA-87 (or LMS/XMSS)", "ECDSA, ML-DSA-65, SHA-256 images rejected"],
    ["SPDM device authentication", "ECDSA-P384", "ML-DSA-87 (SPDM 1.4 PqcAsymAlgo)", "ML-DSA-87", "ML-DSA-65 / P-384 roots rejected"],
    ["SPDM key establishment", "ECDHE-P384", "ML-KEM-1024 (SPDM 1.4 KEMAlg)", "ML-KEM-1024", "no classical algorithm offered"],
    ["Certificates / trust anchor", "P-384 root -> leaf (1.2 KB)", "ML-DSA-87 root -> leaf (15.2 KB)", "ML-DSA-87 CA", "pinned PQC root"],
    ["Hash / KDF / AEAD", "SHA-384, AES-256-GCM", "SHA-384/512, AES-256-GCM", "SHA-384/512, AES-256", "-"],
    ["BootROM -> first stage", "ECDSA (immutable ROM)", "unchanged (classical)", "-", "documented dependency"]],
    [2.6, 2.3, 2.6, 2.2, 2.6], size=13, h=0.55, shade={6: "#fff2cc"})
text(s, 0.5, 5.9, 12.3, 1.0, [("ML-DSA-65 / ML-KEM-768 are the common commercial default - but category 3, so not CNSA 2.0. "
                               "CNSA 2.0 is a design constraint here, not a certification claim.", {"plain": True, "size": 14, "color": INK2})])

# ---------------------------------------------------------------- 3 baseline
s = slide("Starting point: the ECC baseline (measured)",
          "Infineon SPDM code example + Edge Protect Bootloader, DEVELOPMENT lifecycle",
          """Baseline (40 s): ROM_BOOT and FLASH_BOOT are Infineon's immutable stages. EPBL verified the next image with ECDSA-P384.
The SPDM responder ran on the PSoC, the host requester over I2C. ECDSA-P384 identity, ECDHE-P384 session, AES-256-GCM traffic.
We measured everything first - 84 ms signature, 163 ms ECDH key agreement, 257 ms session setup on the device - and ran
negative tests on the baseline too, so the comparison is fair.""")
for i, (lab, sub, col) in enumerate([("ROM_BOOT", "immutable, Infineon keys", "#7f7f7f"), ("FLASH_BOOT", "SIMPLE_APP policy", "#7f7f7f"),
                                     ("EPBL", "ECDSA-P384", ECC), ("App / SPDM responder", "ECDSA-P384 + ECDHE-P384", ECC),
                                     ("Host requester", "pinned P-384 root", ECC)]):
    box(s, 0.5 + i * 2.5, 1.8, 2.2, 1.1, col, lab, sub, size=14)
    if i:
        arrow(s, 0.5 + i * 2.5 - 0.3, 2.35, 0.5 + i * 2.5, 2.35)
table(s, 0.5, 3.4, 6.2, [["Baseline measurement", "Value"],
                         ["ECDSA-P384 signature (device)", "84.4 ms"], ["ECDHE-P384 keygen + shared secret", "162.9 ms"],
                         ["Session setup (device time)", "~257 ms"], ["EPBL image verification", "~170 ms"],
                         ["Cert chain / largest message", "1,218 B / 1,226 B"], ["Session: transfers / bytes", "24 / 2,569 B"]],
      [4.0, 2.2], size=13, h=0.42)
table(s, 7.0, 3.4, 5.8, [["Baseline tests", "Result"],
                         ["Authorized image boots", "PASS"], ["Corrupt / wrong key / header / TLV / signature", "REJECTED"],
                         ["Rollback (v0.9 update)", "REJECTED"], ["SPDM suite (9 ops)", "PASS"],
                         ["ECC tamper builds (cert, sig, transcript, MITM)", "6/6 REJECTED"], ["Wrong trust anchor", "REJECTED"]],
      [4.0, 1.8], size=13, h=0.42)

# ---------------------------------------------------------------- 4 migrated architecture
s = slide("Migrated architecture: one PQC chain, reset to session",
          "The SPDM responder is the image EPBL verified - and its signed measurements prove it",
          """Architecture (60 s): EPBL now verifies the responder with ML-DSA-87 over a SHA-512 image hash on every boot and before
every update. We moved the responder into the EPBL slot: new flash map, regenerated TrustZone/MPC config, signed S+NS as one
MCUboot image. The SPDM identity key lives in the PAS, outside both slots, generated on-chip and never exported - so updates
do not touch the identity. The responder reports the EPBL-verified image digest and version as signed SPDM measurements.
Honest part: ROM_BOOT -> EPBL stays classical, and on this shared board FLASH_BOOT does not authenticate EPBL at all.""")
chain = [("ROM_BOOT", "immutable", "#7f7f7f"), ("FLASH_BOOT", "ECDSA OEM key\n(not enabled here)", "#7f7f7f"),
         ("EPBL", "ML-DSA-87 + SHA-512\nevery boot + update", PQC), ("SPDM responder", "ML-DSA-87 sign\nML-KEM-1024 encaps", PQC),
         ("Host requester", "PQC root, measurements\nvs reference values", PQC)]
for i, (lab, sub, col) in enumerate(chain):
    box(s, 0.5 + i * 2.5, 1.75, 2.2, 1.25, col, lab, sub, size=14)
    if i:
        arrow(s, 0.5 + i * 2.5 - 0.3, 2.37, 0.5 + i * 2.5, 2.37)
legend_chip(s, 0.5, 3.2, "#7f7f7f", "classical / Infineon")
legend_chip(s, 3.3, 3.2, PQC, "post-quantum (CNSA 2.0)")
fm = [("EPBL", 108, "#7f7f7f"), ("Primary slot: responder S+NS + ML-DSA-87 sig", 180, PQC), ("PAS: SPDM identity", 24, "#1baf7a"),
      ("Upgrade slot (candidate)", 180, "#9fc3ee"), ("free", 20, "#e6e5e1")]
x0, total = 0.5, 512
text(s, 0.5, 3.75, 12, 0.4, [("512 KB flash map", {"plain": True, "size": 14, "bold": True, "color": INK})])
xx = x0
for lab, kb, col in fm:
    w = 12.3 * kb / total
    box(s, xx, 4.15, w - 0.03, 0.75, col, f"{kb} KB" if kb < 40 else f"{lab}\n{kb} KB", fg=INK if col in ("#e6e5e1", "#9fc3ee") else "#ffffff", size=11,
        shape=MSO_SHAPE.RECTANGLE)
    xx += w
text(s, 0.5, 4.95, 12.3, 0.4, [("EPBL 108 KB  |  PAS 24 KB: ML-DSA-87 key (on-chip keygen) + 15.2 KB cert chain - outside both slots, survives every update",
                                {"plain": True, "size": 12, "color": INK2})])
text(s, 0.5, 5.45, 12.3, 1.5, [
    ("Boot plane: EPBL verifies ML-DSA-87 (HashML-DSA, SHA-512) - 80 ms for the 165 KB responder", {"size": 15}),
    ("Runtime plane: SPDM 1.4 - ML-DSA-87 identity, ML-KEM-1024 session keys, AES-256-GCM; no classical algorithm offered", {"size": 15}),
    ("Link: signed measurements = EPBL digest + verified-image digest + version, recomputed by the host", {"size": 15})])

# ---------------------------------------------------------------- 5 boot-time results
s = slide("Boot-time protection: EPBL with ML-DSA-87",
          "BootROM is NOT assumed to verify ML-DSA - EPBL is the PQC enforcement point",
          """Boot results (45 s): Two campaigns. Task 2 on the Infineon demo app: 4 positive, 11 negative - corrupted payload, header,
protected TLV, key hash, signature byte, malformed TLV length, rogue key, classical ECDSA image, wrong pre-hash, ML-DSA-65,
rollback - all rejected. Task 4 repeated it with the real SPDM responder in the slot. Surprise: ML-DSA-87 verification is
faster than the ECDSA baseline here - 61 ms vs 170 ms - because P-384 runs in software while ML-DSA verify is cheap NTT math.
Known limit: downgrade prevention guards the update path; writing an old image directly with a debugger still boots -
needs the eFuse anti-rollback counter and a closed debug port in production.""")
table(s, 0.5, 1.7, 7.4, [["Test (EPBL, ML-DSA-87)", "Demo app (T2)", "SPDM responder (T4)"],
                         ["Authorized image boots", "PASS", "PASS"], ["Valid signed update installs", "PASS", "PASS (v1.1, v2.0)"],
                         ["Corrupted payload", "REJECTED", "REJECTED"], ["Modified header / protected TLV", "REJECTED", "REJECTED (header)"],
                         ["Rogue ML-DSA-87 key", "REJECTED", "REJECTED"], ["Rollback to older signed version", "REJECTED", "REJECTED"],
                         ["Signature byte / malformed TLV", "REJECTED", "-"], ["Classical ECDSA image (downgrade)", "REJECTED", "-"],
                         ["ML-DSA-65 / SHA-256 pre-hash (non-CNSA)", "REJECTED", "-"], ["Tampered installed image", "no boot", "no boot"]],
      [3.6, 1.8, 2.0], size=13, h=0.42)
text(s, 8.2, 1.7, 4.7, 4.8, [
    ("61 ms", {"plain": True, "size": 44, "bold": True, "color": PQC, "after": 0}),
    ("ML-DSA-87 image verification (ECDSA-P384: 170 ms)", {"plain": True, "size": 14, "color": INK2, "after": 14}),
    ("47 KB", {"plain": True, "size": 44, "bold": True, "color": PQC, "after": 0}),
    ("EPBL flash (+11 KB vs ECDSA), peak boot RAM 25 KB", {"plain": True, "size": 14, "color": INK2, "after": 14}),
    ("4.95 s", {"plain": True, "size": 44, "bold": True, "color": PQC, "after": 0}),
    ("install a 180 KB responder update (2 verifications)", {"plain": True, "size": 14, "color": INK2})])

# ---------------------------------------------------------------- 6 SPDM
s = slide("Real-time protection: SPDM 1.4 with ML-DSA-87 and ML-KEM-1024",
          "Same responder code, profile P2 -> P4; the requester validates a 15 KB ML-DSA-87 chain against a pinned root",
          """SPDM (60 s): The host negotiates SPDM 1.4: PQC signature ML-DSA-87, KEM ML-KEM-1024; base asym and DHE are empty, so there
is nothing classical to downgrade to. The device key is generated inside the secure world; the host validates the chain to a
pinned ML-DSA-87 root, then verifies a 4,627-byte signature over the transcript. Session secret: the host sends its ML-KEM
encapsulation key, the device encapsulates in the secure world, both run the SPDM HKDF-SHA-384 key schedule bound to the
transcript, and FINISH confirms the keys. We injected faults on the device: modified certificate, broken signature,
modified transcript, modified KEM ciphertext, MITM on FINISH and app data - all twelve rejected. Note B4: with a KEM, a modified
ciphertext still has a valid signature - key confirmation is what catches it.""")
table(s, 0.5, 1.7, 6.6, [["Negative test (12/12 rejected)", "Rejected at"],
                         ["Classical P-384 / rogue / ML-DSA-65 root", "get_certificate"], ["Malformed / truncated root", "get_certificate"],
                         ["Leaf certificate modified on device", "get_certificate"], ["ML-DSA signature byte flipped", "challenge"],
                         ["Valid signature, modified transcript", "start_session"], ["ML-KEM ciphertext modified", "start_session (key confirm.)"],
                         ["MITM: AEAD tag of FINISH_RSP", "start_session"], ["MITM: AEAD tag of app data", "send_receive"]],
      [4.2, 2.4], size=13, h=0.42)
text(s, 7.4, 1.7, 5.5, 5.2, [
    ("Session secret", {"bold": True, "size": 16}),
    ("Host: ephemeral ML-KEM-1024 key (1,568 B) in KEY_EXCHANGE", {"level": 1, "size": 13}),
    ("Device SPE: Encaps -> ciphertext + 32 B secret (11 ms)", {"level": 1, "size": 13}),
    ("HKDF-SHA-384 key schedule bound to the transcript; FINISH confirms", {"level": 1, "size": 13}),
    ("Fresh keys per session, no long-term KEM key on the device", {"level": 1, "size": 13}),
    ("Positive", {"bold": True, "size": 16}),
    ("connect, certificate, challenge, measurements, session, encrypted exchanges: PASS", {"level": 1, "size": 13}),
    ("1 one-off transport abort (failed closed, did not recur in any later handshake)", {"level": 1, "size": 13}),
    ("Memory model", {"bold": True, "size": 16}),
    ("all buffers static & size-checked: 16 KB chain copy, 35.8 KB scratch, 7 KB max message, transcript as running hash", {"level": 1, "size": 13})])

# ---------------------------------------------------------------- 7 lifecycle
s = slide("Lifecycle: update, rollback, credentials - one chain of trust",
          "Task 4: the real responder updated through EPBL, reconnecting over SPDM after every step (18/18 as expected)",
          """Lifecycle (60 s): This is the strongest evidence. v1.0 boots, SPDM works. A signed v1.1 update installs, the host reconnects
with the same identity and the measurements now say 1.1 - with the digest matching the release file. Then four bad updates:
rollback, corrupted, modified header, rogue key - all refused, and the measurements prove 1.1 is still running. We tamper
the installed image: EPBL refuses to boot, the device goes silent. A signed v2.0 update recovers it. Credentials: on-chip key
rotation plus reconnect works; bad credentials are rejected; proof-of-possession catches a foreign certificate.
Answer to 'where is the identity anchored': both - the key is a separately provisioned credential, the firmware state is
anchored in verified boot through the measurements.""")
steps = [("v1.0 boot", "SPDM PASS", PQC), ("update v1.1", "installed, SPDM PASS", PQC), ("rollback v1.0", "REJECTED", "#7f7f7f"),
         ("corrupt / header / rogue key", "REJECTED", "#7f7f7f"), ("tamper primary", "NO BOOT", "#7f7f7f"), ("update v2.0", "recovered, SPDM PASS", PQC)]
for i, (a, b, col) in enumerate(steps):
    box(s, 0.5 + i * 2.08, 1.75, 1.9, 1.05, col, a, b, size=13)
text(s, 0.5, 2.95, 12.3, 0.4, [("measurements after every boot: EPBL digest, image digest, version = values recomputed from the release files (4/4 MATCH)",
                                {"plain": True, "size": 13, "color": INK2})])
table(s, 0.5, 3.5, 6.3, [["Credential path (PAS)", "Result"],
                         ["Rotation: new on-chip ML-DSA-87 key + cert, reconnect", "PASS"], ["ML-DSA-65 / ECDSA keygen", "REJECTED"],
                         ["Chain larger than the 15 KB slot", "REJECTED"], ["Certificate for a foreign key", "proof-of-possession FAILS"],
                         ["SHA-256 RootHash vs negotiated SHA-384", "REJECTED by host"], ["Key overwrite before freeze (V5b)", "accepted - by design, needs freeze"]],
      [4.3, 2.0], size=12, h=0.42, shade={6: "#fff2cc"})
table(s, 7.0, 3.5, 5.9, [["Identity question", "Our answer"],
                         ["Key anchored in", "separately provisioned PAS credential"], ["Firmware state anchored in", "verified boot -> signed measurements"],
                         ["Update changes identity?", "no - reconnect without re-provisioning"], ["Unsigned image uses the key?", "cannot run (EPBL)"],
                         ["Remote rollback detection", "version in measurement block 3"], ["Next step", "DICE-style key derived from image"]],
      [2.6, 3.3], size=12, h=0.42)

# ---------------------------------------------------------------- 8 cost charts
s = slide("What it costs: measured ECC baseline vs PQC",
          "Same board, same code base, Release builds; device-side timings from on-chip instrumentation",
          """Cost (60 s): Three surprises. One: PQC is not slower on the device - ML-KEM key establishment is 15x faster than
software ECDH, ML-DSA verification at boot is 3x faster than ECDSA; only signing is somewhat slower and variable.
Two: the real cost is size - the certificate chain grows 12x to 15 KB, a session moves 11x more bytes, and over a
127-byte DFU tunnel that is 214 transactions instead of 24. Three: the cost is RAM, not flash - the responder even got
slightly smaller in flash, but static RAM grew by 45 KB; peak handshake RAM is ~108 of 126 KB. It fits, with tradeoffs:
one session, one cert chain slot that is 99 percent full.""")
s.shapes.add_picture(str(c_time), Inches(0.3), Inches(1.55), width=Inches(6.3))
s.shapes.add_picture(str(c_comm), Inches(6.75), Inches(1.55), width=Inches(6.3))
text(s, 0.4, 5.75, 12.5, 1.2, [
    ("Faster where it matters: ML-KEM 15x faster than SW ECDH, ML-DSA verify 3x faster than ECDSA", {"size": 15}),
    ("Bigger everywhere: chain x12.5, session bytes x11, 24 -> 214 transfers over 127 B DFU - host time 38 s -> ~6 min per session", {"size": 15})])

s = slide("What it costs: memory", "PQC moves the cost from flash to RAM",
          """Memory (30 s): Flash is flat or smaller - wolfCrypt's ML-DSA/ML-KEM are smaller than software P-384 in mbedTLS. RAM is the
constraint: the 16 KB chain copy and 35.8 KB libspdm scratch in the non-secure world, the ML-DSA key object and hash pool in
the secure world. Everything is static and size-checked - no allocation grows with message size. The credential slot is the
tightest resource: an intermediate CA would not fit.""")
s.shapes.add_picture(str(c_mem), Inches(0.4), Inches(1.5), height=Inches(5.45))
text(s, 7.9, 1.6, 5.0, 5.2, [
    ("~108 KB", {"plain": True, "size": 40, "bold": True, "color": PQC, "after": 0}),
    ("peak handshake RAM of 126 KB (ECC: ~40 KB)", {"plain": True, "size": 14, "color": INK2, "after": 14}),
    ("15,187 B", {"plain": True, "size": 40, "bold": True, "color": PQC, "after": 0}),
    ("ML-DSA-87 chain in a 15,360 B PAS slot (99 % full)", {"plain": True, "size": 14, "color": INK2, "after": 14}),
    ("4,771 B", {"plain": True, "size": 40, "bold": True, "color": PQC, "after": 0}),
    ("boot metadata per image (ECDSA: 215 B)", {"plain": True, "size": 14, "color": INK2})])

# ---------------------------------------------------------------- 10 remaining classical
s = slide("What is still classical - and what must change",
          "We do not claim full quantum resistance yet; here is exactly what is missing",
          """Limitations (45 s): Be explicit. The root of the boot chain is still classical: ROM_BOOT and FLASH_BOOT can only check EPBL
with ECDSA, and on this shared development board that check is not even enabled - it needs an Infineon-signed OEM
certificate. So EPBL, and the ML-DSA key inside it, is only as trustworthy as that step. Production also needs the irreversible
steps we could not do on a shared board: freeze the credential store, close the debug port, move to the SECURE lifecycle,
enable the eFuse rollback counter. And updates are delivered by debugger in our demo; in the field they would come in-band.""")
table(s, 0.5, 1.7, 12.3, [["Item", "Status today", "Required before claiming full quantum resistance"],
                          ["ROM_BOOT / FLASH_BOOT -> EPBL", "classical ECDSA; not enabled on the shared board", "OEM key + SECURE_APP policy now; PQC-capable ROM long term"],
                          ["EPBL key rotation", "EPBL not field-updatable in this map", "BootROM-authenticated EPBL update path"],
                          ["Rollback", "software downgrade prevention + version in SPDM measurements", "eFuse anti-rollback counter + closed DAP"],
                          ["Credential store", "not frozen; provisioning channel unauthenticated", "freeze after provisioning; authenticated re-provisioning"],
                          ["Debug / lifecycle", "DEVELOPMENT, debug port open", "SECURE lifecycle, DAP closed"],
                          ["Update delivery", "debugger writes the upgrade slot", "in-band update over the SPDM transport"],
                          ["Transport", "127 B DFU transactions over I2C", "MCTP over I3C / PCIe VDM, chunk retry"],
                          ["SPDM algorithms", "PQC only (ML-DSA-87, ML-KEM-1024)", "done"]],
      [3.0, 4.4, 4.9], size=13, h=0.5, shade={1: "#fff2cc", 3: "#fff2cc", 4: "#fff2cc", 5: "#fff2cc"})

# ---------------------------------------------------------------- 11 migration
s = slide("Migration path for devices already in the field with ECC",
          "No hybrid (per brief): each plane is cut over per device; the fleet accepts both roots only during the transition",
          """Migration (40 s): A device shipped with ECC can be moved over in six steps; on this board we executed steps 1, 4 and 5 fully,
and steps 2 and 3 in their development form (EPBL reflashed, identity re-provisioned) - the production form is described.
Firmware update to a PQC-capable image through the existing ECDSA boot path; swap the EPBL validation key to ML-DSA-87 via
the BootROM-authenticated path; generate the ML-DSA-87 identity on-chip, and have the CA trust the CSR because it arrives over
a session authenticated by the still-valid ECC identity; switch the responder to the PQC-only profile; requesters drop the ECC
root. Last, the irreversible production lock-down.""")
mig = [("1", "Firmware update to a PQC-capable image", "through the existing ECDSA-verified EPBL update path", "shown (Task 1/2)", True),
       ("2", "EPBL validation key ECDSA -> ML-DSA-87", "production: BootROM-authenticated EPBL update", "shown by reflash; BootROM path documented", None),
       ("3", "On-chip ML-DSA-87 key + CSR, signed by a new ML-DSA-87 OEM CA", "production: CSR delivered over a session authenticated by the old ECC identity", "keygen + CA shown; ECC bridge documented", None),
       ("4", "Responder switches to the PQC-only SPDM profile", "no classical algorithm offered -> no downgrade", "shown (Task 3/4)", True),
       ("5", "Requesters drop and revoke the ECC root", "fleet policy", "classical root rejected - shown", True),
       ("6", "Freeze credentials, close debug, SECURE lifecycle, eFuse counter", "irreversible", "documented (shared board)", False)]
for i, (n, a, b, st, done) in enumerate(mig):
    y = 1.75 + i * 0.82
    col = PQC if done else ("#9fc3ee" if done is None else "#7f7f7f")
    box(s, 0.5, y, 0.6, 0.62, col, n, size=18, shape=MSO_SHAPE.OVAL, fg=INK if done is None else "#ffffff")
    text(s, 1.3, y - 0.02, 7.6, 0.7, [(a, {"plain": True, "size": 17, "bold": True}), (b, {"plain": True, "size": 13, "color": INK2})])
    text(s, 9.1, y + 0.08, 3.9, 0.6, [(st, {"plain": True, "size": 13, "color": PQC if done else INK2, "bold": True})])

# ---------------------------------------------------------------- 12 lessons / business
s = slide("Lessons learned and why it matters beyond the hackathon", None,
          """Close (45 s): Lessons: verify the CNSA parameter set, don't assume it; size the transport and buffers, not the algorithms -
PQC compute is fine on a Cortex-M33, bytes are the problem; bind attestation to verified boot, otherwise the identity says
nothing about the firmware. Market: every SPDM responder in GPUs, NICs, storage and CXL devices faces this migration, and
federal suppliers face CNSA 2.0 dates. The product is a reference migration kit on Infineon parts: the EPBL+SPDM integration,
provisioning and test tooling, and the measured budget that lets a customer decide early.""")
text(s, 0.5, 1.3, 6.1, 5.6, [
    ("Lessons", {"plain": True, "bold": True, "size": 20, "color": NAVY}),
    ("Check the parameter set: CNSA 2.0 = category 5 (ML-DSA-87, ML-KEM-1024), not the commercial default", {"size": 15}),
    ("PQC compute is not the bottleneck on Cortex-M33 - bytes, RAM and the transport are", {"size": 15}),
    ("Defaults bite: EPBL ML-DSA expects SHA-512 pre-hash, the signer defaulted to SHA-256", {"size": 15}),
    ("Attestation must be bound to verified boot - we replaced placeholder measurements with EPBL-verified values", {"size": 15}),
    ("Test the negatives on the baseline too - fair comparison, no false confidence", {"size": 15})])
text(s, 6.9, 1.3, 6.0, 5.6, [
    ("Market and route to product", {"plain": True, "bold": True, "size": 20, "color": NAVY}),
    ("Every SPDM responder (GPU, SmartNIC/DPU, SSD controller, CXL) needs this migration; long service life", {"size": 15}),
    ("CNSA 2.0 timelines drive U.S. national-security suppliers first; CRA/NIS2 push lifecycle readiness in the EU", {"size": 15}),
    ("Deliverable: migration kit on PSoC Control C3 - EPBL + SPDM integration, provisioning, test harness, measured budget", {"size": 15}),
    ("Adoption cost is low: same responder code, build-time profile, no new silicon for the PQC plane", {"size": 15}),
    ("Next: DICE-bound keys, in-band updates, faster transport, production lock-down", {"size": 15})])

# ---------------------------------------------------------------- 13 demo
s = slide("Live demo (5 min)", "Board: EPBL (ML-DSA-87) + SPDM responder; host: requester over I2C",
          """Demo (buttons in tools/demo, cheat sheet results/TASK5_DEMO.md). Before the talk: '0 PREPARE' and '3a SPDM measure v1.0' -
it runs ~8 min over the I2C tool and finishes during the slides; do not reset the board while it runs.
Live: '1 BOOT' (EPBL verifies ML-DSA-87, responder starts), '2a TAMPER rogue key' (rejected, old image runs),
'2b VALID update v1.1' (installed in 5 s), '2c ROLLBACK v1.0' (insufficient version). Then point at the finished 3a window:
signed measurements MATCH the release file. '4 RECORDED' for reconnect-after-update and the SPDM tamper campaign;
optionally start '3b SPDM exchange' - it completes during Q&A.""")
text(s, 0.5, 1.7, 12.3, 5.0, [
    ("1  Verified boot: reset -> EPBL verifies the responder with ML-DSA-87 -> SPDM responder starts", {"size": 18}),
    ("2  Tampering: update signed with a rogue ML-DSA-87 key -> rejected, old version keeps running", {"size": 18}),
    ("3  Rollback: older genuine image as update -> rejected (insufficient version)", {"size": 18}),
    ("4  Valid signed update v1.1 -> installed in ~5 s -> responder restarts with the same identity", {"size": 18}),
    ("5  SPDM after update: ML-DSA-87 authentication, ML-KEM-1024 session, encrypted message; signed measurements = release digest", {"size": 18}),
    ("SPDM over this 127 B I2C tunnel takes ~6 min per session -> started ahead of time; recorded run as backup", {"size": 14, "color": INK2})])

prs.save(OUT)
print("written", OUT, "slides", len(prs.slides))
