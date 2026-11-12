"""PQC demo app - backend.

Serves the full-screen demo UI (static/index.html) and drives the real board:
  * J-Link (KIT_PSC3M6_EVAL on-board probe): load signed images, reset, read flash, drive LED4 / mirror LED3
  * COM4: EPBL + responder console, streamed live to the UI
  * spdm-requester.exe over the KitProg3 I2C bridge: live SPDM 1.4 (ML-DSA-87 / ML-KEM-1024)
Every SPDM transfer the requester performs is decoded from its work dir and streamed to the UI.

Run:  py -3.13 server.py        (START DEMO.cmd does this and opens Chrome in kiosk mode)
"""
import glob
import hashlib
import json
import os
import queue
import re
import struct
import subprocess
import sys
import threading
import time
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pylink  # pylink-square (J-Link DLL)
import serial  # pyserial

HACK = r'C:\Users\kpato\KP\Hackathon'
IMG = os.path.join(HACK, 'results', 'task4_boot')
JLINK = r'C:\Program Files\SEGGER\JLink_V976a\JLink.exe'
JLINK_DLL = r'C:\Program Files\SEGGER\JLink_V976a\JLink_x64.dll'
DFUH = r'C:\Users\kpato\ModusToolbox\tools_3.9\dfuh-tool\dfuh-cli.exe'
REQ = os.path.join(HACK, r'PSOC_C3_SPDM_v2\PSOC_C3_SPDM\SPDM\psoc-spdm-requester\spdm-requester.exe')
ROOT_DER = os.path.join(HACK, r'results\task3_neg_roots\good_root.der')
PINNED_KEY = os.path.join(HACK, r'mtb-example-ce243041-secure-boot-and-update-with-epb-master\keys\ml_dsa_87_img_sign_key_pub.der')
ROGUE_KEY = os.path.join(HACK, r'mtb-example-ce243041-secure-boot-and-update-with-epb-master\keys\rogue_ml_dsa_87_pub.der')
DEMO_OUT = os.path.join(HACK, 'results', 'task5_demo')
TASK4_RUN = os.path.join(HACK, r'results\task4_EPBL_SPDM_20261111_2059')
TASK3_NEG = os.path.join(HACK, r'results\task3_NEG_P4_20261111_1533\summary.txt')
COM = 'COM4'
DEVICE = 'PSC3M6GES3AH'
PORT = 8765
STATIC = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'static')

GPIO8 = 0x42410400           # GPIO_PRT8 (non-secure alias); LED3 = P8.4 (firmware heartbeat), LED4 = P8.5 (ours)
PRIMARY_HDR = 0x3201B000     # MCUboot header of the primary slot
LED4 = 0x20
LED3 = 0x10

sys.path.insert(0, os.path.join(HACK, 'tools'))
from resp_epbl_sign import read_ihex  # noqa: E402

# --------------------------------------------------------------------------------------------- events
_clients = []
_clients_lock = threading.Lock()


def emit(_etype, **data):
    data['type'] = _etype
    data['t'] = time.time()
    msg = json.dumps(data, default=str)
    with _clients_lock:
        for q in list(_clients):
            q.put(msg)


def log(msg):
    print(time.strftime('%H:%M:%S'), msg, flush=True)
    emit('syslog', text=msg)


# --------------------------------------------------------------------------------------------- images
IMAGES = {
    'v1_0_boot':   ('resp_v1_0_0_boot.hex',           'genuine v1.0.0 (primary slot)'),
    'v1_1_boot':   ('resp_v1_1_0_boot.hex',           'genuine v1.1.0 (primary slot)'),
    'v1_0_update': ('resp_v1_0_0_update.hex',         'genuine but OLDER v1.0.0 (update candidate)'),
    'v1_1_update': ('resp_v1_1_0_update.hex',         'genuine v1.1.0 (update candidate)'),
    'v1_2_update': ('resp_v1_2_0_update.hex',         'genuine v1.2.0 (reference, not shown)'),
    'v2_0_update': ('resp_v2_0_0_update.hex',         'genuine v2.0.0 (update candidate)'),
    'rogue':       ('resp_v1_2_0_rogue_update.hex',   'v1.2.0 signed with a ROGUE ML-DSA-87 key'),
    'corrupt':     ('resp_v1_2_0_corrupt_update.hex', 'genuine v1.2.0 with ONE code byte changed'),
    'header':      ('resp_v1_2_0_header_update.hex',  'genuine v1.2.0 with the header version edited'),
}
ANALYSIS = {}
EPBL = {}
_mem_cache = {}


def mem_of(name):
    if name not in _mem_cache:
        _mem_cache[name] = read_ihex(os.path.join(IMG, name))
    return _mem_cache[name]


def hexs(b, n=None):
    b = b if n is None else b[:n]
    return ' '.join(f'{x:02X}' for x in b)


def analyse_image(key, fname, desc, pinned_hash):
    m = mem_of(fname)
    base = min(m)

    def rd(a, n):
        return bytes(m.get(a + i, 0) for i in range(n))
    magic, _load, hs, prot, isz, flags = struct.unpack('<IIHHII', rd(base, 20))
    maj, mnr, rev, bn = struct.unpack('<BBHI', rd(base + 20, 8))
    body = rd(base, hs + isz + prot)
    t = base + hs + isz + prot
    tmagic, ttot = struct.unpack('<HH', rd(t, 4))
    tl, a = {}, t + 4
    while a < t + ttot:
        ty, ln = struct.unpack('<HH', rd(a, 4))
        tl[ty] = (a + 4, rd(a + 4, ln))
        a += 4 + ln
    stored = tl.get(0x12, (0, b''))[1]
    calc = hashlib.sha512(body).digest()
    keyhash = tl.get(0x01, (0, b''))[1]
    sig = tl.get(0x105, (0, b''))[1]
    r = dict(key=key, file=fname, desc=desc, base=f'0x{base:08X}',
             slot='upgrade slot' if base == 0x3204E000 else 'primary slot',
             version=f'{maj}.{mnr}.{rev}', version_tuple=[maj, mnr, rev], build=bn,
             header_hex=hexs(rd(base, 32)), hdr_size=hs, img_size=isz, prot_tlv=prot,
             total_signed=len(body), tlv_total=ttot,
             sha512_stored=stored.hex(), sha512_calc=calc.hex(), digest_ok=(stored == calc),
             keyhash=keyhash.hex(), keyhash_ok=(keyhash.hex() == pinned_hash),
             sig_len=len(sig), sig_head=hexs(sig, 24), sig_tail=hexs(sig[-12:]),
             sig_addr=f'0x{tl.get(0x105, (0,))[0]:08X}')
    # what was changed vs the genuine build of the same version
    if key in ('corrupt', 'header'):
        g = mem_of(IMAGES['v1_2_update'][0])
        diffs = [ad for ad in sorted(set(g) | set(m)) if g.get(ad) != m.get(ad)]
        if diffs:
            ad = diffs[0]
            row = ad & ~0xF
            r['diff'] = dict(addr=f'0x{ad:08X}', offset=ad - base, count=len(diffs),
                             before=f'{g.get(ad, 0):02X}', after=f'{m.get(ad, 0):02X}',
                             row=f'0x{row:08X}',
                             orig=[f'{g.get(row + i, 0):02X}' for i in range(32)],
                             new=[f'{m.get(row + i, 0):02X}' for i in range(32)],
                             col=ad - row,
                             where='MCUboot header (version field)' if ad - base < hs else 'application code')
    return r


def analyse_all():
    pk = open(PINNED_KEY, 'rb').read()[-2592:]
    rk = open(ROGUE_KEY, 'rb').read()[-2592:]
    pinned_hash = hashlib.sha512(pk).hexdigest()
    m = mem_of('epbl_full.hex')
    epbl = bytes(m.get(0x32000000 + i, 0) for i in range(0x1B000))
    pos = epbl.find(pk)
    EPBL.update(pinned_hash=pinned_hash, rogue_hash=hashlib.sha512(rk).hexdigest(),
                key_addr=f'0x{0x32000000 + pos:08X}' if pos >= 0 else 'not found',
                key_len=len(pk), key_head=hexs(pk, 16),
                digest384=hashlib.sha384(epbl).hexdigest(), size=0x1B000)
    for k, (f, d) in IMAGES.items():
        try:
            ANALYSIS[k] = analyse_image(k, f, d, pinned_hash)
        except Exception as e:  # keep the demo running even if one file is missing
            log(f'analysis of {f} failed: {e}')
    _mem_cache.clear()
    log(f'image analysis ready ({len(ANALYSIS)} images); pinned ML-DSA-87 key found in EPBL at {EPBL["key_addr"]}')
    emit('analysis', images=ANALYSIS, epbl=EPBL)


# --------------------------------------------------------------------------------------------- LED / J-Link session
class Led:
    """J-Link DLL session (pylink) used between board operations: drives LED4 (P8.5, configured by us)
    and mirrors the real GPIO output register (LED3 heartbeat + LED4) to the UI. Never halts the core."""

    def __init__(self):
        self.j = None
        self.lock = threading.RLock()
        self.mode = 'off'
        self.alarm_until = 0
        self.want_toggle = False
        self.last = None
        self.cfg_done = False
        self.ok = False
        threading.Thread(target=self._loop, daemon=True).start()

    def start(self):
        with self.lock:
            if self.j:
                return
            try:
                j = pylink.JLink(lib=pylink.Library(JLINK_DLL))
                j.disable_dialog_boxes()
                j.open()
                j.set_tif(pylink.enums.JLinkInterfaces.SWD)
                j.connect(DEVICE, speed=4000)
                self.j, self.ok, self.cfg_done = j, True, False
            except Exception as e:
                log(f'J-Link session failed: {e}')
                try:
                    j.close()
                except Exception:
                    pass
                self.j, self.ok = None, False

    def stop(self):
        with self.lock:
            j, self.j, self.ok = self.j, None, False
            if j:
                try:
                    j.close()
                except Exception:
                    pass

    def r32(self, addr):
        with self.lock:
            return self.j.memory_read32(addr, 1)[0] if self.j else None

    def w32(self, addr, val):
        with self.lock:
            if self.j:
                self.j.memory_write32(addr, [val])

    def read_primary_version(self):
        try:
            with self.lock:
                if not self.j:
                    return None
                if self.j.memory_read32(PRIMARY_HDR, 1)[0] != 0x96F3B83D:
                    return 'none'
                b = self.j.memory_read8(PRIMARY_HDR + 0x14, 4)
            return f'{b[0]}.{b[1]}.{b[2] | (b[3] << 8)}'
        except Exception as e:
            log(f'flash read failed: {e}')
            return None

    def set(self, mode, seconds=0):
        self.mode = mode
        if mode == 'alarm':
            self.alarm_until = time.time() + seconds
        emit('led_mode', mode=mode)

    def toggle(self):
        self.want_toggle = True

    def _loop(self):
        phase, last_poll, last_mode = False, 0, None
        while True:
            time.sleep(0.08)
            if not self.j:
                last_mode = None
                continue
            try:
                with self.lock:
                    if not self.j:
                        continue
                    if not self.cfg_done:
                        cfg = self.r32(GPIO8 + 0x44)
                        self.w32(GPIO8 + 0x44, (cfg & ~(0xF << 20)) | (0x6 << 20))   # P8.5 strong drive
                        self.cfg_done = True
                        last_mode = None
                    mode = self.mode
                    if mode == 'alarm' and time.time() > self.alarm_until:
                        self.mode = mode = 'on'
                        emit('led_mode', mode='on')
                    if mode == 'alarm':
                        phase = not phase
                        self.w32(GPIO8 + (0x4 if phase else 0x8), LED4)
                    elif mode == 'activity':
                        if self.want_toggle:
                            self.want_toggle = False
                            self.w32(GPIO8 + 0xC, LED4)
                    elif mode != last_mode:
                        self.w32(GPIO8 + (0x4 if mode == 'on' else 0x8), LED4)
                    last_mode = mode
                    if time.time() - last_poll > 0.12:
                        last_poll = time.time()
                        v = self.r32(GPIO8)
                        state = dict(led3=not (v & LED3), led4=not (v & LED4))
                        if state != self.last:
                            self.last = state
                            emit('leds', **state)
            except Exception as e:
                log(f'J-Link LED session lost: {e}')
                self.stop()


LEDS = Led()

# --------------------------------------------------------------------------------------------- board state
STATE = dict(primary=None, busy=None, spdm=None, last_verdict=None, jlink=False, i2c=None, com=None,
             measure_src=None, exchange_src=None)
BOARD_LOCK = threading.Lock()
LATEST = [0]      # id of the newest board/SPDM request; older ones stop as soon as they can
SPDM_PROC = {'p': None, 'op': None}


def push_state():
    emit('state', state=STATE)


def refresh_primary():
    v = LEDS.read_primary_version()
    STATE['primary'] = v
    STATE['jlink'] = v is not None
    push_state()
    return v


def classify(line):
    if re.search(r'not valid|insufficient version|\[ERR\]|handle_error|is invalid|returned = 0x2e', line):
        return 'bad'
    if re.search(r'validated successfully|Starting User Application|Image upgrade secondary|Copying the secondary|returned = 0x742e', line):
        return 'good'
    if re.search(r'SPDM Responder|cert chain loaded|SPDM responder initialized|waiting for host|App version', line):
        return 'app'
    if re.search(r'MCUBoot|boot_validate_slot|Swap type|bootutil|Erasing|swap_type|Processing img', line):
        return 'boot'
    return 'dim'


def _ts(line):
    mm = re.match(r'\s*\[(\d+)s\.(\d+)ms\]', line)
    return int(mm.group(1)) * 1000 + int(mm.group(2)) if mm else None


def boot_timing(lines):
    """EPBL check time per slot from the EPBL's own log timestamps:
    '> boot_validate_slot: fa_id = N' -> 'bootutil_img_validate ... returned' (hash + ML-DSA-87 verify)."""
    out, slot, start = {}, None, None
    for ln in lines:
        t = _ts(ln)
        mm = re.search(r'> boot_validate_slot: fa_id = (\d)', ln)
        if mm and t is not None:
            slot, start = ('candidate' if mm.group(1) == '2' else 'primary'), t
        elif 'bootutil_img_validate' in ln and start is not None and t is not None:
            out[slot] = t - start
            start = None
    return out


def run_board(op_id, step, hexes, max_s, expect_install=False):
    emit('op_start', op=op_id, step=step, hexes=hexes)
    LEDS.stop()
    emit('led_mode', mode='reset')
    cmds = 'connect\n' + ''.join(f'loadfile {os.path.join(IMG, h)}\n' for h in hexes) + 'r\ng\nexit\n'
    cf = os.path.join(os.environ.get('TEMP', '.'), 'pqcdemo_load.jlink')
    open(cf, 'w').write(cmds)
    lines, sp = [], None
    try:
        sp = serial.Serial(COM, 115200, timeout=0.1)
        sp.reset_input_buffer()
        STATE['com'] = True
    except Exception as e:
        STATE['com'] = False
        log(f'{COM} not available: {e}')
    if hexes:
        emit('phase', op=op_id, text=f'J-Link: programming {", ".join(hexes)} into flash ...')
    else:
        emit('phase', op=op_id, text='J-Link: reset only (nothing programmed) ...')
    com_ok = sp is not None
    tj = time.time()
    try:
        jl_out = subprocess.run([JLINK, '-device', DEVICE, '-if', 'SWD', '-speed', '4000', '-autoconnect', '1',
                                 '-NoGui', '1', '-ExitOnError', '1', '-CommandFile', cf],
                                capture_output=True, text=True, creationflags=0x08000000, timeout=60).stdout
    except subprocess.TimeoutExpired:
        jl_out = ''
        emit('phase', op=op_id, text='J-Link did not finish within 60 s')
    except Exception:
        if sp:
            sp.close()
        LEDS.start()
        raise
    if 'Cortex-M33 identified' not in jl_out:
        emit('phase', op=op_id, text='J-Link could not connect to the board (USB J13?)')
    emit('phase', op=op_id, text=f'flash done in {time.time() - tj:.1f} s - reset -> EPBL runs (board console COM4, live):')
    t0 = time.time()
    seen_app = None
    superseded = False
    epbl_done = False
    buf = b''
    while sp and time.time() - t0 < max_s:
        # a newer experiment was requested: stop as soon as EPBL has finished touching flash
        # (programming flash while EPBL erases/copies a slot can hang the probe)
        if LATEST[0] != op_id and (epbl_done or time.time() - t0 > 8):
            superseded = True
            break
        try:
            chunk = sp.read(4096)
        except Exception as e:  # e.g. USB unplugged during boot: stop reading, classify what we have
            log(f'{COM} read failed: {e}')
            com_ok = False
            break
        if chunk:
            buf += chunk
            while b'\n' in buf:
                raw, buf = buf.split(b'\n', 1)
                ln = raw.decode('utf-8', 'replace').replace('\x1b[2J', '').replace('\x1b[;H', '').replace('\r', '').rstrip()
                if not ln.strip():
                    continue
                lines.append(ln)
                if 'Starting User Application' in ln:
                    epbl_done = True
                if LATEST[0] == op_id:
                    emit('uart', op=op_id, text=ln, cls=classify(ln))
                if 'waiting for host' in ln:
                    seen_app = time.time()
        if seen_app and time.time() - seen_app > 0.4:
            break
    if sp:
        try:
            sp.close()
        except Exception:
            pass
    if superseded:
        emit('op_done', op=op_id, step=step, verdict='SKIPPED', reason='superseded by a newer experiment', timing={})
        return None
    txt = '\n'.join(lines)
    if 'Cortex-M33 identified' not in jl_out and not lines:
        verdict, reason = 'ERROR', 'J-Link could not reach the board - check the USB cable (J13) and retry (R)'
    elif not com_ok:
        verdict, reason = 'ERROR', f'{COM} could not be read (serial monitor open? cable?) - board output not visible, fix and retry'
    elif 'Starting User Application' not in txt:
        verdict, reason = 'NOBOOT', 'EPBL found no valid signed image - nothing runs'
    elif re.search(r'Image upgrade secondary slot -> primary slot|Copying the secondary', txt):
        verdict, reason = 'INSTALLED', 'update verified with ML-DSA-87 and installed'
    elif re.search(r'insufficient version', txt):
        verdict, reason = 'REJECTED', 'rollback: candidate version is lower than the installed one'
    elif re.search(r'IMAGE_TLV_SHA is invalid', txt):
        verdict, reason = 'REJECTED', 'SHA-512 of the image does not match the signed digest'
    elif re.search(r'not valid|returned = 0x2e', txt):
        verdict, reason = 'REJECTED', 'signature check failed: not signed by the provisioned ML-DSA-87 key'
    else:
        verdict, reason = 'BOOTED', 'image verified with ML-DSA-87, responder started'
    timing = boot_timing(lines)
    open(os.path.join(HACK, 'results', 'task5_demo_last_boot.log'), 'w', encoding='utf-8').write(txt)
    time.sleep(0.3)
    LEDS.start()
    ver = refresh_primary()
    if verdict in ('BOOTED', 'INSTALLED'):
        LEDS.set('on')
    elif verdict == 'REJECTED':
        LEDS.set('alarm', 4.0)
    else:
        LEDS.set('off')
    res = dict(op=op_id, step=step, verdict=verdict, reason=reason, timing=timing, primary=ver,
               seconds=round(time.time() - t0, 1), app_started='waiting for host' in txt)
    STATE['last_verdict'] = res
    emit('op_done', **res)
    log(f'{step}: {verdict} ({reason})')
    return res


# --------------------------------------------------------------------------------------------- SPDM decoding
REQ_NAMES = {0x84: 'GET_VERSION', 0xE1: 'GET_CAPABILITIES', 0xE3: 'NEGOTIATE_ALGORITHMS', 0x81: 'GET_DIGESTS',
             0x82: 'GET_CERTIFICATE', 0x83: 'CHALLENGE', 0xE0: 'GET_MEASUREMENTS', 0xE4: 'KEY_EXCHANGE',
             0xE5: 'FINISH', 0x85: 'CHUNK_SEND', 0x86: 'CHUNK_GET', 0xEC: 'END_SESSION', 0xE8: 'HEARTBEAT'}
RSP_NAMES = {0x04: 'VERSION', 0x61: 'CAPABILITIES', 0x63: 'ALGORITHMS', 0x01: 'DIGESTS', 0x02: 'CERTIFICATE',
             0x03: 'CHALLENGE_AUTH', 0x60: 'MEASUREMENTS', 0x64: 'KEY_EXCHANGE_RSP', 0x65: 'FINISH_RSP',
             0x05: 'CHUNK_SEND_ACK', 0x06: 'CHUNK_RESPONSE', 0x7F: 'ERROR', 0x6C: 'END_SESSION_ACK'}
NOTES = {
    'GET_VERSION': 'which SPDM versions do you speak?',
    'GET_CAPABILITIES': 'capabilities: certificates, challenge, measurements, key exchange, chunking',
    'NEGOTIATE_ALGORITHMS': 'host offers ONLY CNSA 2.0 algorithms: ML-DSA-87, ML-KEM-1024, SHA-384, AES-256-GCM',
    'GET_DIGESTS': 'hash of the certificate chain in slot 0',
    'GET_CERTIFICATE': 'download the 15,187-byte ML-DSA-87 certificate chain',
    'CHALLENGE': 'prove possession of the ML-DSA-87 private key (fresh nonce)',
    'GET_MEASUREMENTS': 'signed firmware measurements of the running image',
    'KEY_EXCHANGE': 'host ephemeral ML-KEM-1024 encapsulation key (1,568 B)',
    'FINISH': 'key confirmation (encrypted + MAC)',
    'END_SESSION': 'close the secure session',
}
OID_MLDSA87 = bytes.fromhex('0609608648016503040313')


def parse_transfer(mtbdfu, binf):
    try:
        j = json.load(open(mtbdfu))
        req = bytes.fromhex(j['commands'][0]['dataBytes'])
    except Exception:
        return None
    rsp = b''
    try:
        txt = open(binf, encoding='utf-8', errors='replace').read()
        mm = re.search(r'Read packet:\s*([0-9a-fA-F ]+)', txt)
        if mm:
            raw = bytes.fromhex(mm.group(1).strip())
            ln = raw[2] | (raw[3] << 8)
            rsp = raw[4:4 + ln]
    except Exception:
        pass
    return req, rsp


class Decoder:
    def __init__(self):
        self.ctx = None
        self.secured = 0
        self.chunks = 0
        self.total = 0
        self.largest = 0
        self.inner = None

    def decode(self, n, req, rsp):
        self.total += len(req) + len(rsp)
        d = dict(n=n, req_len=len(req), rsp_len=len(rsp), req_hex=hexs(req, 40), rsp_hex=hexs(rsp, 40),
                 secure=False, note='', tag='')
        if req[:1] == b'\x06':
            self.secured += 1
            sid = req[1:5].hex()
            names = ['FINISH', 'APP DATA', 'END_SESSION']
            nm = names[self.secured - 1] if self.secured <= 3 else 'SECURED'
            d.update(name=f'{nm} (encrypted)', rsp_name='encrypted response', secure=True,
                     note=f'session 0x{sid}: AES-256-GCM ciphertext - nobody on the bus can read it',
                     tag='aead')
            return d
        ver, code = (req[1], req[2]) if len(req) > 2 else (0, 0)
        name = REQ_NAMES.get(code, f'0x{code:02X}')
        rcode = rsp[2] if len(rsp) > 2 else None
        rname = RSP_NAMES.get(rcode, f'0x{rcode:02X}' if rcode is not None else '-')
        d['ver'] = f'{ver >> 4}.{ver & 0xF}'
        if name in ('CHUNK_GET', 'CHUNK_SEND'):
            self.chunks += 1
            if name == 'CHUNK_SEND' and len(req) > 20 and req[4:6] == b'\x00\x00' and self.inner is None:
                ic = req[18] if len(req) > 18 else 0
                self.inner = REQ_NAMES.get(ic, f'0x{ic:02X}')
                self.ctx = self.inner
            part = self.ctx or ''
            if name == 'CHUNK_GET':
                part = {'KEY_EXCHANGE': 'KEY_EXCHANGE_RSP', 'GET_CERTIFICATE': 'CERTIFICATE', 'CHALLENGE': 'CHALLENGE_AUTH',
                        'GET_MEASUREMENTS': 'MEASUREMENTS'}.get(part, part)
            d['name'] = f'{name}'
            d['part_of'] = part
            d['note'] = {'CERTIFICATE': 'device -> host: piece of the 15,187-B ML-DSA-87 certificate chain',
                         'KEY_EXCHANGE': 'host -> device: piece of KEY_EXCHANGE (ML-KEM-1024 encapsulation key)',
                         'KEY_EXCHANGE_RSP': 'device -> host: ML-KEM ciphertext + ML-DSA-87 signature (6,305 B)',
                         'MEASUREMENTS': 'device -> host: signed measurement record',
                         'CHALLENGE_AUTH': 'device -> host: ML-DSA-87 signature over the transcript'}.get(part, f'127-byte piece of {part or "a large message"}')
        else:
            self.inner = None
            self.ctx = name
            d['name'] = name
            d['note'] = NOTES.get(name, '')
            self.largest = max(self.largest, len(req))
        if rcode == 0x7F and len(rsp) > 3 and rsp[3] == 0x0F:
            rname = 'LARGE_RESPONSE (fetch in chunks)'
        elif rcode == 0x7F and name == 'CHUNK_SEND':
            rname = 'CHUNK_SEND_ACK'
        d['rsp_name'] = rname
        if rcode == 0x7F and not rname.startswith(('LARGE', 'CHUNK')):
            d['tag'] = 'error'
        if OID_MLDSA87 in rsp:
            d['tag'] = 'oid'
            i = rsp.find(OID_MLDSA87)
            d['oid_at'] = i
            d['note'] = 'certificate bytes contain OID 2.16.840.1.101.3.4.3.19 = id-ML-DSA-87'
        if name == 'NEGOTIATE_ALGORITHMS':
            d['tag'] = 'algo'
        return d


def verify_measurements(meas_lines):
    got = {}
    for line in meas_lines:
        mm = re.search(r'block (\d+):.*\)\s+([0-9a-f]+)\s*$', line)
        if mm:
            got[int(mm.group(1))] = mm.group(2)
    if 3 not in got:
        return None
    v = bytes.fromhex(got[3])
    raw = struct.unpack('<Q', v)[0]
    ver = (raw >> 24, (raw >> 16) & 0xFF, raw & 0xFFFF)
    cand = {(1, 0, 0): 'v1_0_boot', (1, 1, 0): 'v1_1_update', (1, 2, 0): 'v1_2_update', (2, 0, 0): 'v2_0_update'}
    key = cand.get(ver)
    rows = []
    info = bytes.fromhex(got.get(4, '')).decode(errors='replace') if 4 in got else ''
    exp1 = EPBL.get('digest384')
    exp2 = exp3 = None
    fname = '?'
    if key:
        m = read_ihex(os.path.join(IMG, IMAGES[key][0]))
        base = min(m)
        hdr = bytes(m.get(base + i, 0) for i in range(32))
        _mg, _ld, hs, prot, isz = struct.unpack_from('<IIHHI', hdr, 0)
        body = bytes(m.get(base + i, 0) for i in range(hs + isz + prot))
        exp2 = hashlib.sha384(body).hexdigest()
        exp3 = struct.pack('<Q', (ver[0] << 24) | (ver[1] << 16) | ver[2]).hex()
        fname = IMAGES[key][0]
    for b, label, exp in ((1, 'EPBL boot-stage digest (SHA-384)', exp1),
                          (2, 'EPBL-verified image digest (SHA-384)', exp2),
                          (3, 'security version (MCUboot version)', exp3)):
        rows.append(dict(block=b, label=label, device=got.get(b), expected=exp,
                         match=bool(exp) and got.get(b) == exp))
    return dict(version='.'.join(map(str, ver)), file=fname, rows=rows, info=info,
                all_match=all(r['match'] for r in rows))


def spdm_summary(lines):
    out = dict(lines=lines)
    joined = '\n'.join(lines)
    mm = re.search(r"reply: b'(.*)'", joined)
    if mm:
        out['reply'] = mm.group(1)
    mm = re.search(r'sent (\d+) B, received (\d+) B', joined)
    if mm:
        out['sent'], out['received'] = int(mm.group(1)), int(mm.group(2))
    out['pass'] = 'RESULT: PASS' in joined
    if 'block 1' in joined:
        try:
            out['meas'] = verify_measurements(lines)
        except Exception as e:
            log(f'measurement check failed: {e}')
    return out


# --------------------------------------------------------------------------------------------- SPDM live / replay
def find_hwid():
    try:
        r = subprocess.run([DFUH, '--display-hw'], capture_output=True, text=True, timeout=30,
                           creationflags=0x08000000)
        mm = re.search(r'I2C:\s*(\S+)', r.stdout + r.stderr)
        return mm.group(1) if mm else None
    except Exception:
        return None


def run_spdm_live(op_id, op, message):
    hwid = find_hwid()
    STATE['i2c'] = bool(hwid)
    if not hwid:
        emit('spdm_done', op=op_id, chan='live', kind=op, ok=False,
             summary=dict(lines=['No I2C bridge (KitProg3) found - replug its USB cable and retry.'], **{'pass': False}))
        STATE['spdm'] = None
        push_state()
        return
    stamp = time.strftime('%H%M%S')
    work = os.path.join(DEMO_OUT, f'spdm_{op}_{stamp}')
    os.makedirs(os.path.join(work, 'work'), exist_ok=True)
    args = [REQ, op, '--root', ROOT_DER, '--hwid', hwid, '--dfuh-cli', DFUH, '--work-dir', os.path.join(work, 'work')]
    if op == 'exchange':
        args += ['--message', message]
    emit('spdm_start', op=op_id, chan='live', kind=op, message=message, expected=190 if op == 'measure' else 218,
         src=os.path.basename(work), started=time.strftime('%H:%M:%S'))
    LEDS.set('activity')
    try:
        p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                             creationflags=0x08000000)
    except Exception as e:  # e.g. requester exe missing: report it and leave the board usable
        log(f'SPDM {op}: requester could not be started: {e}')
        LEDS.set('on')
        STATE['spdm'] = None
        emit('spdm_done', op=op_id, chan='live', kind=op, ok=False,
             summary=dict(lines=[f'requester could not be started: {e}'], **{'pass': False}))
        push_state()
        return
    SPDM_PROC.update(p=p, op=op_id)
    out = []

    def reader():
        for ln in p.stdout:
            ln = ln.rstrip()
            if ln and ' INFO ' not in ln:
                out.append(ln)
                emit('spdm_line', op=op_id, chan='live', text=ln)
    th = threading.Thread(target=reader, daemon=True)
    th.start()
    dec, seen, t0 = Decoder(), 0, time.time()
    wd = os.path.join(work, 'work')
    while True:
        done = p.poll() is not None
        n = seen + 1
        mf, bf = os.path.join(wd, f'spdm_{n:04d}.mtbdfu'), os.path.join(wd, f'spdm_{n:04d}.bin')
        while os.path.exists(bf) and os.path.exists(mf):
            time.sleep(0.05)
            r = parse_transfer(mf, bf)
            if not r:
                break
            d = dec.decode(n, *r)
            d.update(chan='live', op=op_id, elapsed=round(time.time() - t0, 1), total=dec.total)
            emit('spdm_msg', **d)
            LEDS.toggle()
            seen = n
            n += 1
            mf, bf = os.path.join(wd, f'spdm_{n:04d}.mtbdfu'), os.path.join(wd, f'spdm_{n:04d}.bin')
        if done:
            break
        time.sleep(0.25)
    th.join(2)
    open(os.path.join(work, f'{op}.txt'), 'w', encoding='utf-8').write('\n'.join(out))
    s = spdm_summary(out)
    s.update(transfers=seen, bytes=dec.total, seconds=round(time.time() - t0), src=os.path.basename(work))
    ok = p.returncode == 0 and s.get('pass')
    if ok:
        STATE['measure_src' if op == 'measure' else 'exchange_src'] = dict(txt=os.path.join(work, f'{op}.txt'), work=wd,
                                                                          label=f'live run {stamp[:2]}:{stamp[2:4]}')
    LEDS.set('on' if ok else 'alarm', 6)
    SPDM_PROC.update(p=None, op=None)
    STATE['spdm'] = None
    emit('spdm_done', op=op_id, chan='live', kind=op, ok=bool(ok), summary=s, aborted=p.returncode not in (0, 1))
    push_state()
    log(f'SPDM {op}: {"PASS" if ok else "FAILED"} after {s["seconds"]} s, {seen} transfers')


def default_source(kind):
    if STATE.get(f'{kind}_src'):
        return STATE[f'{kind}_src']
    runs = sorted(glob.glob(os.path.join(DEMO_OUT, f'spdm_{kind}_*')), key=os.path.getmtime, reverse=True)
    for r in runs:
        txt = os.path.join(r, f'{kind}.txt')
        if os.path.exists(txt) and 'RESULT: PASS' in open(txt, encoding='utf-8', errors='replace').read():
            ts = time.strftime('%d %b %H:%M', time.localtime(os.path.getmtime(txt)))
            return dict(txt=txt, work=os.path.join(r, 'work'), label=f'recorded on this board, {ts}')
    if kind == 'measure':
        return dict(txt=os.path.join(TASK4_RUN, 'T1_measure_v1_1_0.txt'),
                    work=os.path.join(TASK4_RUN, 'work_T1_measure_v1_1_0'), label='recorded Task 4 run, 11 Nov 21:1x')
    return dict(txt=os.path.join(TASK4_RUN, 'T1_exchange_after_update.txt'),
                work=os.path.join(TASK4_RUN, 'work_T1_exchange_after_update'), label='recorded Task 4 run, 11 Nov 21:2x')


def run_replay(op_id, kind, seconds):
    src = default_source(kind)
    files = sorted(glob.glob(os.path.join(src['work'], 'spdm_*.mtbdfu')))
    emit('spdm_start', op=op_id, chan='replay', kind=kind, expected=len(files), src=src['label'], replay=True)
    dec = Decoder()
    big_n = sum(1 for f in files if '"05 14 86' not in open(f).read() and '"05 14 85' not in open(f).read())
    delay = max(0.01, (seconds - 2) / max(1, len(files) + 5 * big_n))
    for i, mf in enumerate(files, 1):
        r = parse_transfer(mf, mf[:-7] + '.bin')
        if not r:
            continue
        d = dec.decode(i, *r)
        big = d['name'] not in ('CHUNK_GET', 'CHUNK_SEND')
        d.update(chan='replay', op=op_id, elapsed=None, total=dec.total)
        emit('spdm_msg', **d)
        time.sleep(delay * (6 if big else 1))
    lines = [ln.rstrip() for ln in open(src['txt'], encoding='utf-8', errors='replace') if ln.strip()]
    for ln in lines:
        emit('spdm_line', op=op_id, chan='replay', text=ln)
        time.sleep(0.1)
    s = spdm_summary(lines)
    s.update(transfers=len(files), bytes=dec.total, src=src['label'])
    emit('spdm_done', op=op_id, chan='replay', kind=kind, ok=bool(s.get('pass')), summary=s)


def recorded_evidence():
    rows = []
    try:
        import csv
        with open(os.path.join(TASK4_RUN, 'summary.csv'), encoding='utf-8-sig') as f:
            for r in csv.DictReader(f):
                ev = re.sub(r'^\d\d:\d\d:\d\d\.\d+ ', '', r['evidence'])
                rows.append(dict(test=r['test'], expect=r['expect'], result=r['result'], ok=r['ok'] == 'True',
                                 s=r['wall_s'], evidence=ev[:90]))
    except Exception as e:
        log(f'task4 summary: {e}')
    neg = []
    try:
        for ln in open(TASK3_NEG, encoding='utf-8', errors='replace'):
            if 'expect=' in ln:
                neg.append(ln.strip())
    except Exception as e:
        log(f'task3 summary: {e}')
    return dict(task4=rows, task3=neg)


# --------------------------------------------------------------------------------------------- operations
_op_counter = [0]


def start_op(step, body, op_id=None):
    if op_id is None:
        _op_counter[0] += 1
        op_id = _op_counter[0]
    board_steps = {'prepare', 'boot', 'rogue', 'corrupt', 'header', 'valid', 'rollback', 'v2'}
    if (step in board_steps or step.startswith('spdm_')) and body.get('force'):
        return start_forced(op_id, step, body)
    if step in board_steps or step.startswith('spdm_'):
        if STATE['spdm']:
            return 409, dict(error='spdm_running', msg='A live SPDM run is in progress - a reset would kill it. '
                                                       'Press A to abort it first.')
        if not BOARD_LOCK.acquire(blocking=False):
            return 409, dict(error='busy', msg='The board is busy with the previous step.')

        LATEST[0] = op_id

        def worker():
            try:
                STATE['busy'] = step
                push_state()
                pv = STATE.get('primary')
                tup = tuple(int(x) for x in pv.split('.')) if pv and pv not in ('none',) else None
                if step == 'prepare':
                    run_board(op_id, step, ['epbl_full.hex', 'resp_v1_0_0_boot.hex', 'zero_upgrade.hex'], 12)
                elif step == 'boot':
                    run_board(op_id, step, ['zero_upgrade.hex'], 10)
                elif step in ('rogue', 'corrupt', 'header'):
                    run_board(op_id, step, [IMAGES[step][0]], 12)
                elif step == 'valid':
                    pre = [] if (tup and tup < (1, 1, 0)) else ['resp_v1_0_0_boot.hex']
                    run_board(op_id, step, pre + ['resp_v1_1_0_update.hex'], 20, True)
                elif step == 'rollback':
                    pre = [] if (tup and tup > (1, 0, 0)) else ['resp_v1_1_0_boot.hex']
                    run_board(op_id, step, pre + ['resp_v1_0_0_update.hex'], 12)
                elif step == 'v2':
                    run_board(op_id, step, ['resp_v2_0_0_update.hex'], 20, True)
                elif step.startswith('spdm_'):
                    STATE['spdm'] = step
                    STATE['busy'] = None
                    push_state()
                    BOARD_LOCK.release()
                    run_spdm_live(op_id, step[5:], body.get('message') or 'hello from the PQC demo')
                    return
            except Exception as e:
                traceback.print_exc()
                emit('op_done', op=op_id, step=step, verdict='ERROR', reason=str(e), timing={})
                if step.startswith('spdm_'):
                    # never leave the board locked as "SPDM running" after a crash in the SPDM path
                    STATE['spdm'] = None
                    SPDM_PROC.update(p=None, op=None)
                    emit('spdm_done', op=op_id, chan='live', kind=step[5:], ok=False,
                         summary=dict(lines=[f'SPDM error: {e}'], **{'pass': False}))
                elif not LEDS.ok:
                    LEDS.start()
            finally:
                STATE['busy'] = None
                push_state()
                if BOARD_LOCK.locked() and not step.startswith('spdm_'):
                    BOARD_LOCK.release()
        threading.Thread(target=worker, daemon=True).start()
        return 200, dict(op=op_id)
    if step.startswith('replay_'):
        threading.Thread(target=run_replay, args=(op_id, step[7:], float(body.get('seconds', 22))), daemon=True).start()
        return 200, dict(op=op_id)
    if step == 'abort':
        p = SPDM_PROC.get('p')
        if p:
            subprocess.run(['taskkill', '/F', '/T', '/PID', str(p.pid)], capture_output=True)
            log('live SPDM run aborted by the presenter')
        return 200, dict(op=op_id)
    if step == 'led_test':
        LEDS.set('alarm', 3)
        return 200, dict(op=op_id)
    return 400, dict(error='unknown step')


def start_forced(op_id, step, body):
    """Simple UI: a click always starts the experiment. A running board step is cut short (after its flash
    programming, which cannot be interrupted safely), a running live SPDM operation is aborted."""
    LATEST[0] = op_id
    if STATE['spdm'] or BOARD_LOCK.locked():
        emit('op_queued', op=op_id, step=step)

    def go():
        p = SPDM_PROC.get('p')
        if p:
            subprocess.run(['taskkill', '/F', '/T', '/PID', str(p.pid)], capture_output=True)
            log('live SPDM run aborted: a new experiment was started')
        t = time.time()
        while (STATE['spdm'] or BOARD_LOCK.locked()) and time.time() - t < 90:
            if LATEST[0] != op_id:
                return emit('op_done', op=op_id, step=step, verdict='SKIPPED', reason='superseded', timing={})
            time.sleep(0.1)
        if LATEST[0] != op_id:
            return emit('op_done', op=op_id, step=step, verdict='SKIPPED', reason='superseded', timing={})
        b = dict(body)
        b.pop('force', None)
        code, obj = start_op(step, b, op_id)
        if code != 200:
            emit('op_done', op=op_id, step=step, verdict='ERROR', reason=obj.get('msg', obj.get('error')), timing={})
    threading.Thread(target=go, daemon=True).start()
    return 200, dict(op=op_id)


# --------------------------------------------------------------------------------------------- HTTP
class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _json(self, code, obj):
        b = json.dumps(obj, default=str).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        if self.path.startswith('/events'):
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Cache-Control', 'no-cache')
            self.end_headers()
            q = queue.Queue()
            with _clients_lock:
                _clients.append(q)
            try:
                self.wfile.write(b'retry: 1000\n\n')
                self.wfile.flush()
                while True:
                    try:
                        msg = q.get(timeout=10)
                        self.wfile.write(f'data: {msg}\n\n'.encode())
                    except queue.Empty:
                        self.wfile.write(b': ping\n\n')
                    self.wfile.flush()
            except Exception:
                pass
            finally:
                with _clients_lock:
                    _clients.remove(q)
            return
        if self.path.startswith('/api/state'):
            return self._json(200, dict(state=STATE, images=ANALYSIS, epbl=EPBL, leds=LEDS.last,
                                        sources=dict(measure=default_source('measure')['label'],
                                                     exchange=default_source('exchange')['label'])))
        if self.path.startswith('/api/recorded'):
            kind = 'measure' if 'measure' in self.path else 'exchange'
            src = default_source(kind)
            try:
                lines = [ln.rstrip() for ln in open(src['txt'], encoding='utf-8', errors='replace') if ln.strip()]
            except Exception:
                lines = []
            s = spdm_summary(lines) if kind == 'exchange' else dict(lines=lines)
            s['src'] = src['label']
            return self._json(200, s)
        if self.path.startswith('/api/evidence'):
            return self._json(200, recorded_evidence())
        path = '/index.html' if self.path in ('/', '') else self.path.split('?')[0]
        if path in ('/simple', '/simple/'):
            path = '/simple.html'
        f = os.path.normpath(os.path.join(STATIC, path.lstrip('/')))
        if not f.startswith(STATIC) or not os.path.isfile(f):
            self.send_response(404)
            self.end_headers()
            return
        b = open(f, 'rb').read()
        self.send_response(200)
        ct = 'text/html; charset=utf-8' if f.endswith('.html') else 'application/octet-stream'
        self.send_header('Content-Type', ct)
        self.send_header('Content-Length', str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0) or 0)
        body = json.loads(self.rfile.read(n) or b'{}')
        if self.path.startswith('/api/run'):
            code, obj = start_op(body.get('step', ''), body)
            return self._json(code, obj)
        self._json(404, {})


def startup():
    analyse_all()
    LEDS.start()
    for _ in range(40):
        if LEDS.ok:
            break
        time.sleep(0.1)
    v = refresh_primary()
    log(f'J-Link {"connected" if LEDS.ok else "NOT connected"}; installed image (primary slot header): {v}')
    LEDS.set('on' if v and v != 'none' else 'off')
    STATE['i2c'] = bool(find_hwid())
    push_state()


if __name__ == '__main__':
    os.makedirs(DEMO_OUT, exist_ok=True)
    ThreadingHTTPServer.allow_reuse_address = False   # Windows: a second instance must fail, not share the port
    try:
        srv = ThreadingHTTPServer(('127.0.0.1', PORT), H)
    except OSError:
        sys.exit(f'Port {PORT} is already in use - the demo server is probably running already.')
    srv.daemon_threads = True
    threading.Thread(target=startup, daemon=True).start()
    if '--kiosk' in sys.argv or '--kiosk-simple' in sys.argv:
        subprocess.Popen([r'C:\Program Files\Google\Chrome\Application\chrome.exe', '--kiosk',
                          f'--app=http://127.0.0.1:{PORT}/' + ('simple' if '--kiosk-simple' in sys.argv else ''), '--no-first-run', '--disable-features=Translate',
                          '--user-data-dir=' + os.path.join(os.environ.get('TEMP', '.'), 'pqcdemo_chrome')])
    print(f'PQC demo server on http://127.0.0.1:{PORT}/  (Ctrl+C to stop)', flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        LEDS.stop()
