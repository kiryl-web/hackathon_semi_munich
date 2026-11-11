"""Task 3: build the host-side negative trust anchors for the P4 (ML-DSA-87) requester tests.

Usage: py -3.13 task3_make_neg_roots.py <psc3-provisioning-host dir> <out dir>

Writes (all DER, used as --root for spdm-requester.exe):
  good_root.der          the real ML-DSA-87 demo root (ca_mldsa87.crt)
  rogue_mldsa87_root.der ML-DSA-87 root with the SAME subject name but a different key (impersonation)
  mldsa65_root.der       ML-DSA-65 root, same subject (unsupported / weaker parameter set)
  ec384_root.der         classical ECDSA P-384 root (classical-downgrade anchor)
  malformed_key_root.der real root with one byte flipped inside the ML-DSA-87 public key
  truncated_root.der     real root truncated to half its length (malformed encoding)
"""
import subprocess
import sys
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives.serialization import Encoding

host = Path(sys.argv[1])
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
test = host / "test"


def der_of(pem_path: Path) -> bytes:
    return x509.load_pem_x509_certificate(pem_path.read_bytes()).public_bytes(Encoding.DER)


good = der_of(test / "ca_mldsa87.crt")
(out / "good_root.der").write_bytes(good)
(out / "ec384_root.der").write_bytes(der_of(test / "ca_ec384.crt"))

# Same subject as the genuine root -> a pure name match is not enough.
subj = x509.load_der_x509_certificate(good).subject
cn = subj.get_attributes_for_oid(x509.NameOID.COMMON_NAME)[0].value
attr = lambda oid: subj.get_attributes_for_oid(oid)[0].value
for alg, name in (("ml-dsa-87", "rogue_mldsa87"), ("ml-dsa-65", "mldsa65")):
    subprocess.run([sys.executable, str(test / "gen_ca.py"), "--alg", alg, "--cn", cn,
                    "--org", attr(x509.NameOID.ORGANIZATION_NAME),
                    "--ou", attr(x509.NameOID.ORGANIZATIONAL_UNIT_NAME),
                    "--country", attr(x509.NameOID.COUNTRY_NAME),
                    "--serial", attr(x509.NameOID.SERIAL_NUMBER),
                    "--out-key", str(out / f"{name}_root.key"),
                    "--out-crt", str(out / f"{name}_root.der"), "--out-form", "der"],
                   check=True)

# Flip one byte inside the subjectPublicKey of the genuine root. The ML-DSA-87 OID
# (2.16.840.1.101.3.4.3.19) occurs in tbs.signature, SPKI and the outer
# signatureAlgorithm; the 2nd occurrence is the SPKI, followed by the BIT STRING
# 03 82 0A 21 00 <2592-byte public key>.
oid = bytes.fromhex("0609608648016503040313")
first = good.find(oid)
spki = good.find(oid, first + 1)
bitstr = good.find(bytes.fromhex("03820a2100"), spki)
assert first >= 0 and spki > first and bitstr > spki, "unexpected root encoding"
pk_off = bitstr + 5 + 1000
bad = bytearray(good)
bad[pk_off] ^= 0x01
(out / "malformed_key_root.der").write_bytes(bytes(bad))
(out / "truncated_root.der").write_bytes(good[: len(good) // 2])
print(f"public key starts at {bitstr + 5}, flipped byte {pk_off}; files in {out}")
for f in sorted(out.glob("*.der")):
    print(f"  {f.name:28s} {f.stat().st_size:6d} B")
