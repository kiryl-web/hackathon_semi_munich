"""Task 4: turn the SPDM responder build (TARGET=APP_KIT_PSC3M6_EVAL_EPBL) into an
MCUboot image that the Edge Protect Bootloader verifies with ML-DSA-87.

The responder is two TrustZone projects. The build exports the secure part at its
S-bus alias (0x3201B400.., right after the 0x400 MCUboot header) and the
non-secure part at its NS alias (0x22034000..). EPBL hashes/verifies one
contiguous image, so this script
  1. relocates the NS part to its S-bus alias (0x32034000..),
  2. fills the gap between S and NS with 0x00 (= erased value on PSC3M6),
  3. signs the merged image with edgeprotecttools (ML-DSA-87, SHA-512, raw sig,
     key-hash TLV, SecureApp boot record) for the boot or the upgrade slot.

Usage (Python 3.13, edgeprotecttools on PATH, e.g. after tools\\mtbenv.ps1):
  py -3.13 -I tools/resp_epbl_sign.py --combined <resp>/build/app_combined.hex \
      --kind boot|update --version 1.0.0 [--build 0] --key <priv.der> --out <file.hex>
"""
import argparse
import os
import subprocess
import sys
import tempfile

S_START = 0x3201B000          # primary (boot) slot = MCUboot header
SLOT_SIZE = 0x2D000           # 0x3201B000..0x32048000 (PAS starts at 0x32048000)
UPGRADE_START = 0x3204E000    # upgrade slot, after the 24 KB PAS
NS_ALIAS_DELTA = 0x32000000 - 0x22000000


def read_ihex(path):
    mem = {}
    base = 0
    for line in open(path, encoding="ascii").read().splitlines():
        line = line.strip()
        if not line.startswith(':'):
            continue
        raw = bytes.fromhex(line[1:])
        n, off, typ = raw[0], (raw[1] << 8) | raw[2], raw[3]
        if typ == 0:
            for i in range(n):
                mem[base + off + i] = raw[4 + i]
        elif typ == 4:
            base = ((raw[4] << 8) | raw[5]) << 16
        elif typ == 2:
            base = ((raw[4] << 8) | raw[5]) << 4
    return mem


def write_ihex(path, start, data):
    out = []
    cur_hi = None
    for i in range(0, len(data), 16):
        addr = start + i
        hi = addr >> 16
        if hi != cur_hi:
            rec = bytes([2, 0, 0, 4, (hi >> 8) & 0xFF, hi & 0xFF])
            out.append(':' + (rec + bytes([(-sum(rec)) & 0xFF])).hex().upper())
            cur_hi = hi
        chunk = data[i:i + 16]
        rec = bytes([len(chunk), (addr >> 8) & 0xFF, addr & 0xFF, 0]) + chunk
        out.append(':' + (rec + bytes([(-sum(rec)) & 0xFF])).hex().upper())
    out.append(':00000001FF')
    open(path, 'w').write('\n'.join(out) + '\n')


def merge(combined):
    mem = read_ihex(combined)
    moved = {}
    for a, v in mem.items():
        if 0x22000000 <= a < 0x23000000:      # NS alias -> S-bus alias
            a += NS_ALIAS_DELTA
        if not (S_START + 0x400 <= a < S_START + SLOT_SIZE):
            sys.exit(f'byte at 0x{a:08X} is outside the primary slot payload area')
        moved[a] = v
    lo, hi = min(moved), max(moved) + 1
    if lo != S_START + 0x400:
        sys.exit(f'image must start right after the header (0x{S_START + 0x400:08X}), starts at 0x{lo:08X}')
    data = bytearray(hi - lo)                 # gaps = 0x00 (erased value)
    for a, v in moved.items():
        data[a - lo] = v
    return lo, bytes(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--combined', required=True)
    ap.add_argument('--kind', choices=('boot', 'update'), required=True)
    ap.add_argument('--version', required=True, help='MAJ.MIN.REV')
    ap.add_argument('--build', default='0')
    ap.add_argument('--key', required=True)
    ap.add_argument('--sha', default='sha512')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()

    lo, data = merge(a.combined)
    tmp = os.path.join(tempfile.mkdtemp(), 'resp_unsigned.hex')
    write_ihex(tmp, lo, data)
    print(f'unsigned payload 0x{lo:08X}..0x{lo + len(data):08X} ({len(data)} B)')

    hex_addr = S_START if a.kind == 'boot' else UPGRADE_START
    cmd = ['edgeprotecttools', 'sign-image', '-i', tmp, '-o', a.out, '--key', a.key,
           '--sha', a.sha, '-R', '0', '-H', '0x400', '-S', hex(SLOT_SIZE),
           '--min-erase-size', '0x200', '--image-version', f'{a.version}+{a.build}',
           '--align', '1', '--public-key-format', 'hash', '--pubkey-encoding', 'der',
           '--signature-encoding', 'raw', '--boot-record', 'SecureApp', '--pad',
           '--overwrite-only', '--hex-addr', hex(hex_addr)]
    print(' '.join(cmd))
    r = subprocess.run(cmd, shell=(os.name == 'nt'))
    if r.returncode != 0:
        sys.exit(r.returncode)
    print(f'signed {a.kind} image -> {a.out} (slot 0x{hex_addr:08X})')


if __name__ == '__main__':
    main()
