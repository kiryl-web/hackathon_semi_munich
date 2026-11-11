"""Write an Intel-HEX that covers [start, end) completely: the bytes of <in.hex>
inside that range, 0x00 (= erased value on PSC3M6) everywhere else.
Used to make flash contents deterministic (EPBL area, clearing the upgrade slot),
so the host can recompute the SPDM measurement digests exactly.

Usage: py -3.13 -I hex_pad.py <in.hex|-> <out.hex> <start-hex> <end-hex>
       ('-' as input = all zeros)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from resp_epbl_sign import read_ihex, write_ihex  # noqa: E402


def main():
    src, dst = sys.argv[1], sys.argv[2]
    start, end = int(sys.argv[3], 16), int(sys.argv[4], 16)
    data = bytearray(end - start)
    if src != '-':
        for a, v in read_ihex(src).items():
            if not (start <= a < end):
                sys.exit(f'byte at 0x{a:08X} outside 0x{start:08X}..0x{end:08X}')
            data[a - start] = v
    write_ihex(dst, start, bytes(data))
    print(f'{dst}: 0x{start:08X}..0x{end:08X} ({end - start} B)')


if __name__ == '__main__':
    main()
