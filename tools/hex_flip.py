"""Copy an Intel-HEX file and XOR one byte at a given absolute address (tamper test).
Usage: py -3.13 -I hex_flip.py <in.hex> <out.hex> <address-hex> [xor-mask-hex]
"""
import sys

src, dst, addr = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
mask = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x01
base = 0
done = False
out = []
for line in open(src):
    line = line.strip()
    if not line.startswith(':'):
        out.append(line)
        continue
    raw = bytearray.fromhex(line[1:])
    n, off, typ = raw[0], (raw[1] << 8) | raw[2], raw[3]
    if typ == 4:
        base = ((raw[4] << 8) | raw[5]) << 16
    elif typ == 0 and not done and base + off <= addr < base + off + n:
        i = 4 + (addr - base - off)
        print(f"flip @0x{addr:08X}: 0x{raw[i]:02X} -> 0x{raw[i] ^ mask:02X}")
        raw[i] ^= mask
        raw[-1] = (-sum(raw[:-1])) & 0xFF
        done = True
        line = ':' + raw.hex().upper()
    out.append(line)
if not done:
    sys.exit(f"address 0x{addr:08X} not found in {src}")
open(dst, 'w').write('\n'.join(out) + '\n')
