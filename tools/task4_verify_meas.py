"""Task 4: host-side reference-value check for the SPDM measurements of the
responder running behind EPBL (TARGET=APP_KIT_PSC3M6_EVAL_EPBL).

Recomputes, from the release artefacts only,
  block 1 = SHA-384 over the EPBL area 0x32000000..0x3201B000 (epbl_full.hex),
  block 2 = SHA-384 over header + payload + protected TLVs of the signed image
            (exactly the bytes EPBL hashed before verifying the ML-DSA-87 signature),
  block 3 = MCUboot version as (major << 24 | minor << 16 | revision), little-endian u64,
and compares them with the signed GET_MEASUREMENTS output of spdm-requester.exe.

Usage: py -3.13 -I task4_verify_meas.py <epbl_full.hex> <signed_image.hex> <measure.txt>
Exit 0 = all three match.
"""
import hashlib
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from resp_epbl_sign import read_ihex  # noqa: E402


def image_bytes(path):
    mem = read_ihex(path)
    base = min(mem)
    hdr = bytes(mem.get(base + i, 0) for i in range(32))
    magic, _load, hdr_size, prot, img_size = struct.unpack_from('<IIHHI', hdr, 0)
    if magic != 0x96F3B83D:
        sys.exit(f'{path}: no MCUboot header at 0x{base:08X}')
    total = hdr_size + img_size + prot
    data = bytes(mem.get(base + i, 0) for i in range(total))
    maj, mnr, rev, bn = struct.unpack_from('<BBHI', hdr, 20)
    return data, (maj, mnr, rev, bn)


def main():
    epbl_hex, img_hex, meas_txt = sys.argv[1:4]
    m = read_ihex(epbl_hex)
    epbl = bytes(m.get(0x32000000 + i, 0) for i in range(0x1B000))
    img, ver = image_bytes(img_hex)
    exp = {
        1: hashlib.sha384(epbl).hexdigest(),
        2: hashlib.sha384(img).hexdigest(),
        3: struct.pack('<Q', (ver[0] << 24) | (ver[1] << 16) | ver[2]).hex(),
    }
    got = {}
    for line in open(meas_txt, encoding='utf-8', errors='replace'):
        mm = re.search(r'block (\d+):.*\)\s+([0-9a-f]+)\s*$', line)
        if mm:
            got[int(mm.group(1))] = mm.group(2)
    ok = True
    names = {1: 'EPBL boot stage digest', 2: 'verified image digest', 3: 'secure version (MCUboot version)'}
    print(f'image {os.path.basename(img_hex)} version {ver[0]}.{ver[1]}.{ver[2]}+{ver[3]}')
    for b in (1, 2, 3):
        match = got.get(b) == exp[b]
        ok &= match
        print(f'block {b} {names[b]:<34} {"MATCH" if match else "MISMATCH"}')
        if not match:
            print(f'   expected {exp[b]}\n   device   {got.get(b)}')
    if 4 in got:
        print('block 4 informational              ', bytes.fromhex(got[4]).decode(errors='replace'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
