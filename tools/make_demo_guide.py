"""Presenter guide for the live demo (PDF). Run: py -3.13 tools/make_demo_guide.py"""
import os
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (KeepTogether, PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table,
                                TableStyle)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'results', 'Demo_Presenter_Guide.pdf')

F = r'C:\Windows\Fonts'
pdfmetrics.registerFont(TTFont('Arial', os.path.join(F, 'arial.ttf')))
pdfmetrics.registerFont(TTFont('Arial-Bold', os.path.join(F, 'arialbd.ttf')))
pdfmetrics.registerFont(TTFont('Arial-Italic', os.path.join(F, 'ariali.ttf')))
pdfmetrics.registerFont(TTFont('Consolas', os.path.join(F, 'consola.ttf')))
from reportlab.pdfbase.pdfmetrics import registerFontFamily  # noqa: E402
registerFontFamily('Arial', normal='Arial', bold='Arial-Bold', italic='Arial-Italic', boldItalic='Arial-Bold')

NAVY = colors.HexColor('#12305a')
CYAN = colors.HexColor('#0e7490')
GREY = colors.HexColor('#5b6577')
LIGHT = colors.HexColor('#eef3f9')
WARN = colors.HexColor('#fff4e0')

body = ParagraphStyle('b', fontName='Arial', fontSize=10, leading=14, spaceAfter=5, alignment=TA_LEFT)
small = ParagraphStyle('s', parent=body, fontSize=8.8, leading=11.5, spaceAfter=0)
h1 = ParagraphStyle('h1', parent=body, fontName='Arial-Bold', fontSize=20, leading=24, textColor=NAVY, spaceAfter=4)
sub = ParagraphStyle('sub', parent=body, fontSize=11, textColor=GREY, spaceAfter=12)
h2 = ParagraphStyle('h2', parent=body, fontName='Arial-Bold', fontSize=13.5, leading=17, textColor=NAVY,
                    spaceBefore=12, spaceAfter=5)
h3 = ParagraphStyle('h3', parent=body, fontName='Arial-Bold', fontSize=10.5, leading=14, textColor=CYAN,
                    spaceBefore=7, spaceAfter=2)
bul = ParagraphStyle('bul', parent=body, leftIndent=12, bulletIndent=2, spaceAfter=2.5)
say = ParagraphStyle('say', parent=body, fontName='Arial-Italic', leftIndent=10, textColor=colors.HexColor('#1f3b63'),
                     spaceAfter=4)

story = []
P = lambda t, s=body: story.append(Paragraph(t, s))  # noqa: E731


def B(items):
    for t in items:
        story.append(Paragraph(t, bul, bulletText='\u2022'))


def T(rows, widths, head=True, shade=None):
    data = [[Paragraph(str(c), small) for c in r] for r in rows]
    t = Table(data, colWidths=[w * mm for w in widths], repeatRows=1 if head else 0)
    st = [('GRID', (0, 0), (-1, -1), 0.4, colors.HexColor('#c5cfdd')),
          ('VALIGN', (0, 0), (-1, -1), 'TOP'),
          ('LEFTPADDING', (0, 0), (-1, -1), 4), ('RIGHTPADDING', (0, 0), (-1, -1), 4),
          ('TOPPADDING', (0, 0), (-1, -1), 2.5), ('BOTTOMPADDING', (0, 0), (-1, -1), 2.5)]
    if head:
        st += [('BACKGROUND', (0, 0), (-1, 0), NAVY)]
        for c in data[0]:
            c.style = ParagraphStyle('th', parent=small, fontName='Arial-Bold', textColor=colors.white)
    if shade:
        st += [('BACKGROUND', (0, 0), (-1, -1), shade)]
    t.setStyle(TableStyle(st))
    story.append(t)
    story.append(Spacer(1, 6))


def box(text, bg=WARN):
    t = Table([[Paragraph(text, body)]], colWidths=[170 * mm])
    t.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, -1), bg), ('BOX', (0, 0), (-1, -1), 0.6, colors.HexColor('#e0b060')),
                           ('LEFTPADDING', (0, 0), (-1, -1), 7), ('RIGHTPADDING', (0, 0), (-1, -1), 7),
                           ('TOPPADDING', (0, 0), (-1, -1), 5), ('BOTTOMPADDING', (0, 0), (-1, -1), 3)]))
    story.append(t)
    story.append(Spacer(1, 7))


# ------------------------------------------------------------------------------------------------ content
P('The live demonstration: what you need to know', h1)
P('Presenter guide for the PQC hackathon jury session (Phase 1: 3-min pitch, 5\u20137 min deep dive, 10 min Q&amp;A). '
  'All numbers come from our own runs on this board.', sub)

P('1. What the demo proves', h2)
P('The jury wants to see one thing: an ECC device has been migrated to post-quantum protection on <b>both planes</b>, '
  'and the protection actually works. The demo shows it live on the real board, step by step:')
B(['<b>Boot plane.</b> On every reset the Edge Protect Bootloader (EPBL) hashes the application with SHA-512 and '
   'verifies its <b>ML-DSA-87</b> signature against a key compiled into EPBL. Only then does the application run.',
   '<b>Attacks are rejected.</b> An update signed with an attacker\'s key, an update with one changed byte, and a '
   'genuine but older version (rollback) are all refused, and the verified image keeps running.',
   '<b>Secure update.</b> A genuine newer version (v1.1.0) is verified, installed and started.',
   '<b>Runtime plane.</b> The PC (the SPDM requester) authenticates the running chip with SPDM 1.4: an ML-DSA-87 '
   'certificate chain plus <b>signed measurements</b> of the firmware. It then opens an encrypted session using '
   '<b>ML-KEM-1024</b> and AES-256-GCM, after the update. That covers the brief\'s "reconnect after update".'])
P('This covers every bullet of the brief\'s live-demonstration list: authorized boot, rejected tampering, '
  'a valid update, SPDM authentication, a protected session, encrypted application data, and reconnect after update.')

P('2. Before you walk in (about 10 minutes before the slot)', h2)
B(['<b>Cables:</b> the board\'s USB J13 (J-Link + COM4 console) and the KitProg3 I2C bridge (wires SDA J16.6, '
   'SCL J16.5, VTARG J15.7, GND J15.8). The I2C bridge is only needed for SPDM.',
   '<b>Close every serial monitor</b> (ModusToolbox terminal, PuTTY, Tera Term). If COM4 is taken, the app now says '
   '"COM4 could not be read" instead of showing the board output.',
   'Double-click <b>tools\\demo_app\\START DEMO.cmd</b>. A console window starts the server and Chrome opens full '
   'screen. Keep the console window open: closing it stops the demo.',
   'Check the three status chips at the top right: <b>J-Link, COM4, I2C</b>. The "installed" field shows the version '
   'read from flash.',
   'Press <b>P</b> (pre-flight) about <b>8 minutes</b> before the jury. It programs EPBL + responder v1.0.0 '
   '(about 20 s; the chip\'s identity key is not touched), then runs one <b>live SPDM attestation</b> (about 7 min). '
   'When the screen says <b>READY</b>, press <b>Home</b>. Then wait. Press <b>Space</b> only when the deep dive starts.'])
box('<b>The one rule:</b> a board reset kills a running SPDM operation. While a live SPDM run is going, the app '
    'refuses board steps (it shows a message). Press <b>A</b> to abort the live run first if you really must. '
    'Never use "Erase device" or the provisioning <b>--freeze</b> option: this is a shared board.')

P('3. Controls', h2)
T([['Key', 'Action', 'Key', 'Action'],
   ['Space', 'start / pause the automatic run', 'R', 'redo the current step (always safe)'],
   ['\u2192 / \u2190', 'next / previous step', '1 \u2013 8', 'jump to a step'],
   ['Home or 0', 'back to the start screen', 'P', 'pre-flight (known state + live attestation)'],
   ['A', 'abort a live SPDM run', 'L', 'LED test (LED4 blinks)'],
   ['F', 'toggle full screen', 'Alt+F4', 'close Chrome (server keeps running)']],
  [22, 63, 22, 63])
P('If Chrome is closed by accident: <b>OPEN UI ONLY.cmd</b>. There is also a <b>simple</b> version '
  '(<b>START SIMPLE DEMO.cmd</b>, or http://127.0.0.1:8765/simple): one button per experiment, which starts '
  'immediately. It also has a "header edited" attack and 20-second SPDM replays. Use it in Q&amp;A when a juror asks '
  '"can you show X again?".')

P('4. The run, step by step', h2)
P('After Space the run advances by itself (about 3.5 minutes for steps 0\u20136). You can pause at any point to talk. '
  'For each step: what happens, what to say, and what the screen should show.')

steps = [
    ('Step 0 \u00b7 Overview (14 s)',
     'No board action. The screen shows the chain of trust (ROM_BOOT \u2192 FLASH_BOOT \u2192 EPBL \u2192 responder \u2192 host) '
     'and the CNSA 2.0 mapping table. Note the two grey "classical" boxes: you will come back to them.',
     '"An ECC-based SPDM device, migrated to the CNSA 2.0 profile. Everything you see next runs live on this board."'),
    ('Step 1 \u00b7 Verified boot (expect: VERIFIED \u00b7 RUNNING, ~4 s)',
     'The app resets the board. The console shows the real EPBL log from COM4, ending with "validated successfully" and the '
     'responder banner. The INPUT panel shows what EPBL checks: the SHA-512 stored in the image versus recomputed, the '
     'signer key hash versus the key built into EPBL, and the 4,627-byte ML-DSA-87 signature. The check takes about '
     '<b>80 ms</b> (from the EPBL log timestamps).',
     '"EPBL hashes the whole 165 KB image with SHA-512 and verifies the ML-DSA-87 signature before it jumps to the application."'),
    ('Step 2 \u00b7 Attack: rogue key (expect: ATTACK BLOCKED, ~12 s)',
     'A perfectly formatted update, signed with a real ML-DSA-87 key, but the attacker\'s key. The key-hash row turns red. '
     'EPBL rejects it in about 20 ms, LED4 blinks fast, and "installed" stays v1.0.0.',
     '"Being well-formed is not enough. Only our provisioned key may sign firmware."'),
    ('Step 3 \u00b7 Attack: one byte (expect: ATTACK BLOCKED, ~13 s)',
     'A genuine signed update with one code byte changed after signing. The right panel shows the exact byte, old '
     'versus new value. The recomputed SHA-512 no longer matches the signed digest.',
     '"One bit is enough. The fingerprint changes completely, so the signature no longer fits."'),
    ('Step 4 \u00b7 Valid update v1.1 (expect: UPDATE INSTALLED, ~14 s)',
     'A genuine newer image. EPBL verifies the candidate, copies it into the primary slot, verifies it again and starts '
     'it. "installed" changes to v1.1.0. The SPDM identity in protected storage is not touched.',
     '"This is the normal field update: verified twice, and the device keeps its identity."'),
    ('Step 5 \u00b7 Attack: rollback (expect: ATTACK BLOCKED, ~8 s)',
     'The genuine but older v1.0.0. Signature and digest are fine; the only defence is the version check. EPBL logs '
     '"insufficient version".',
     '"The classic downgrade attack: an old version with a known bug, correctly signed by us. Refused because it is older."'),
    ('Step 6 \u00b7 SPDM attestation (replay, ~20 s)',
     'This is a fast replay of the live run from your pre-flight: the real bytes from this board, 187 transfers. You see '
     'every SPDM message decoded, the ML-DSA-87 OID in the certificate bytes, and at the end three green <b>MATCH</b> '
     'rows. The device\'s signed measurements (EPBL digest, verified-image digest, version) equal what the PC recomputes '
     'from the release files.',
     '"The host pins an ML-DSA-87 root, checks the 15 KB chain, and the chip attests exactly the image EPBL verified. '
     'This was recorded live 10 minutes ago. Over this debug bridge one run takes 7 minutes, so we replay it fast."'),
    ('Step 7 \u00b7 Encrypted session (live, ~7 min, runs in the background)',
     'Now a <b>live</b> SPDM session starts against the updated v1.1.0: ML-KEM-1024 key exchange, transcript signed with '
     'ML-DSA-87, then a time-stamped message sent AES-256-GCM-encrypted and echoed back. The screen shows the ciphertext '
     'on the wire. Move on; a pop-up announces the result during Q&amp;A (press 7 to view it).',
     '"This is the reconnect after the update. The device needs under a second; the 7 minutes are the 127-byte I2C test tool."'),
    ('Step 8 \u00b7 Evidence &amp; cost',
     'Recorded results: the Task 4 lifecycle run (18/18) and the Task 3 SPDM tamper campaign (12/12 rejected), the key '
     'cost numbers, and the "Honest limits" box. End here and go to Q&amp;A.',
     '"PQC compute is not the bottleneck on this Cortex-M33. Bytes, RAM and transport are. And this is what is still classical."'),
]
for title, what, line in steps:
    story.append(KeepTogether([Paragraph(title, h3), Paragraph(what, body), Paragraph(line, say)]))

P('5. How to read the screen', h2)
B(['<b>Verdict card (top right):</b> green = verified/installed, amber "ATTACK BLOCKED" = rejected as intended, red = '
   'something unexpected. A red line "expected: ..." means the result was not what the step wanted.',
   '<b>Board console:</b> the unedited EPBL and responder output from COM4. This is your proof that it is not a '
   'simulation. Lines in green/red are the decisive ones.',
   '<b>LED3</b> (physical, on the board) is the responder\'s heartbeat: it blinks while idle. <b>LED4</b> is driven by '
   'the app: steady = verified image running, fast blink = attack rejected, off = reset, flicker = SPDM traffic. The '
   'LED icons on screen are read from the real GPIO register, not simulated. Pointing at the physical board is effective.',
   '<b>SPDM view:</b> transfer counter, bytes on the bus, every message with request (\u2192) and response (\u2190) bytes. '
   'Encrypted messages are marked; nobody on the bus can read them.'])

P('6. When something goes wrong', h2)
T([['Symptom', 'What to do'],
   ['"J-Link could not reach the board"', 'Check USB J13, then press R. If it persists: close the app, replug, START DEMO.cmd.'],
   ['"COM4 could not be read"', 'A serial monitor is open: close it, press R.'],
   ['NO BOOT / board silent', 'Press P, or use "Known state" in the simple UI (20 s). The identity is never touched.'],
   ['"No I2C bridge found"', 'Replug the KitProg3 USB, wait 5 s, retry. Step 6 still works: it is a replay.'],
   ['Board step refused: "SPDM running"', 'Intended. Wait for the live run, or press A to abort it.'],
   ['Live session fails', 'Say so calmly: it fails closed, with no insecure fallback. Show step 6 and step 8 (recorded 3/3 exchanges, 12/12 rejected).'],
   ['Everything breaks', 'Backup: tools\\demo\\*.cmd buttons, and "4 RECORDED evidence" shows the recorded results instantly.']],
  [55, 115])
P('Tested today 11:06 on the board: prepare, boot, rogue, one byte, header, valid, rollback and the SPDM replay all '
  'gave the expected verdicts. A busy COM4 and a failed SPDM start now show clear errors instead of misleading results.')

P('7. Questions the jury will likely ask', h2)
qa = [
    ('Is the whole boot chain quantum-safe?',
     'No, and we say so. ROM_BOOT is immutable silicon and only does ECDSA. On our board FLASH_BOOT does not even check '
     'EPBL (SIMPLE_APP policy): enabling that needs an Infineon-signed OEM certificate and steps we could not do on a '
     'shared board. Everything from EPBL onward is PQC. A fully PQC root needs new silicon; meanwhile the ECDSA ROM step '
     'protects a small, rarely changed EPBL.'),
    ('Why ML-DSA-87 and not ML-DSA-65?',
     'CNSA 2.0 requires NIST category 5. ML-DSA-65 is category 3. We tested that the device rejects ML-DSA-65 at boot, '
     'in SPDM and in provisioning.'),
    ('Why not LMS/XMSS for firmware?',
     'CNSA 2.0 allows them, but they are stateful: the signer must never reuse a one-time key, which is risky in a build '
     'pipeline. ML-DSA-87 is stateless and is the same algorithm as the SPDM identity, so one PQC family covers both planes.'),
    ('Why is PQC verification faster than ECDSA?',
     'On this chip P-384 runs in software (only P-256 is accelerated). ML-DSA verification is fast integer arithmetic. '
     'Caution: the 170 ms ECDSA figure was measured on the smaller demo app (ML-DSA there: 61 ms); '
     'on the 165 KB responder ML-DSA takes about 80 ms.'),
    ('Why does SPDM take 7 minutes?',
     'The host test tool starts a new process for every 127-byte I2C transfer (~1.4 s each), and a PQC session needs about '
     '200 transfers. On the device the whole handshake is a few hundred milliseconds: ML-KEM encapsulation 11 ms versus '
     '163 ms for ECDHE. A real transport (PCIe/MCTP) removes this.'),
    ('Where is the private key? What if EPBL is bypassed?',
     'The ML-DSA-87 key is generated on the chip and stays in the TrustZone secure world. Malicious firmware that bypassed '
     'EPBL could not fake the signed measurements without also compromising the secure world; in production, BootROM '
     'authentication of EPBL closes that path.'),
    ('Can I flash an old image with a debugger?',
     'Today yes: rollback protection is a software version check on the update path. Production needs the eFuse anti-'
     'rollback counter and a closed debug port. Both are irreversible, so we did not do them on a shared board.'),
    ('What does it cost?',
     'Mostly RAM and bytes, not CPU or flash: certificate chain 1.2 KB \u2192 15.2 KB, session traffic 2.6 KB \u2192 28 KB '
     '(24 \u2192 214 transfers), runtime buffers 20 KB \u2192 58 KB, boot RAM 10.7 KB \u2192 25.2 KB, '
     'image metadata 215 B \u2192 4.8 KB. Responder flash stayed the same (130 \u2192 128 KB).'),
    ('How do you migrate devices already in the field?',
     'Five steps: field-update the firmware (still verified by the ECDSA EPBL); replace EPBL with the ML-DSA-87 EPBL; '
     'generate a new ML-DSA-87 identity on the device, vouched for by the old ECC identity; switch to the PQC-only '
     'profile; requesters drop the ECC root. We executed steps 1\u20134 on this board across the tasks.'),
    ('Why no hybrid (ECC + PQC)?',
     'The brief excluded it. A PQC-only profile also avoids any downgrade path: the device offers no classical algorithm at all.'),
]
for q, a in qa:
    story.append(KeepTogether([Paragraph('<b>Q: ' + q + '</b>', body), Paragraph(a, ParagraphStyle(
        'a', parent=body, leftIndent=10, spaceAfter=6))]))

P('8. Numbers to have in your head', h2)
T([['', 'ECC baseline', 'PQC (this demo)'],
   ['Boot signature check', 'ECDSA P-384, 170 ms', 'ML-DSA-87 + SHA-512, 61 ms (demo app) / ~80 ms (responder)'],
   ['Reset to application', '266 ms', '146 ms'],
   ['Rejected attack at boot', '\u2013', 'about 20 ms, the old image keeps running'],
   ['Device signature (SPDM)', '84 ms', '66\u2013346 ms, median 112 ms (varies: rejection sampling)'],
   ['Key exchange on device', 'ECDHE 163 ms', 'ML-KEM-1024 encapsulation 11 ms'],
   ['Certificate chain', '1,218 B', '15,187 B (fills the 15 KB slot to 99 %)'],
   ['Session on the wire', '2,569 B, 24 transfers', '28,199 B, 214 transfers'],
   ['Tests on hardware', '6/6 tamper cases rejected', 'Boot 11/11 rejected, SPDM 12/12 rejected, lifecycle 18/18']],
  [42, 50, 78])
box('<b>Your closing line:</b> "Everything from the bootloader to the encrypted session is post-quantum, measured on a '
    '128 KB-RAM microcontroller. What remains classical is the immutable ROM, and we have shown exactly what it takes to close that gap."',
    bg=LIGHT)


def footer(c, d):
    c.saveState()
    c.setFont('Arial', 8)
    c.setFillColor(GREY)
    c.drawString(20 * mm, 10 * mm, 'PQC hackathon \u00b7 live demo presenter guide')
    c.drawRightString(190 * mm, 10 * mm, f'page {d.page}')
    c.restoreState()


doc = SimpleDocTemplate(OUT, pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm, topMargin=16 * mm, bottomMargin=16 * mm,
                        title='Live demo presenter guide', author='PQC hackathon team')
doc.build(story, onFirstPage=footer, onLaterPages=footer)
print('written', os.path.abspath(OUT))
