"""Print MCUboot header/TLV layout of a signed Intel-HEX image (absolute addresses).
Usage: py -3.13 -I mcuboot_info.py <image.hex>
"""
import struct
import sys

mem = {}
base = 0
for line in open(sys.argv[1]):
    line = line.strip()
    if not line.startswith(':'):
        continue
    r = bytes.fromhex(line[1:])
    n, off, typ = r[0], (r[1] << 8) | r[2], r[3]
    if typ == 4:
        base = ((r[4] << 8) | r[5]) << 16
    elif typ == 0:
        for i in range(n):
            mem[base + off + i] = r[4 + i]

start = min(mem)
def rd(a, n):
    return bytes(mem.get(a + i, 0xFF) for i in range(n))

magic, load, hdr_size, prot_tlv, img_size, flags = struct.unpack('<IIHHII', rd(start, 20))
ver = struct.unpack('<BBHI', rd(start + 20, 8))
print(f"image start      0x{start:08X}")
print(f"magic            0x{magic:08X}")
print(f"hdr_size         0x{hdr_size:X}  img_size 0x{img_size:X}  prot_tlv 0x{prot_tlv:X}  flags 0x{flags:X}")
print(f"version          {ver[0]}.{ver[1]}.{ver[2]}+{ver[3]}  (header bytes @0x{start + 20:08X})")
tlv = start + hdr_size + img_size
if prot_tlv:
    m, tot = struct.unpack('<HH', rd(tlv, 4))
    print(f"protected TLV    @0x{tlv:08X} magic 0x{m:04X} len {tot}")
    tlv += tot
m, tot = struct.unpack('<HH', rd(tlv, 4))
print(f"TLV info         @0x{tlv:08X} magic 0x{m:04X} total {tot}")
a, end = tlv + 4, tlv + tot
names = {0x01: 'KEYHASH', 0x10: 'SHA256', 0x11: 'SHA384', 0x12: 'SHA512', 0x22: 'ECDSA_SIG',
         0x24: 'ED25519', 0x30: 'ENC', 0x50: 'DEPENDENCY'}
while a < end:
    t, l = struct.unpack('<HH', rd(a, 4))
    print(f"  TLV type 0x{t:02X} {names.get(t, '?'):10s} len {l:5d}  data @0x{a + 4:08X}")
    a += 4 + l
