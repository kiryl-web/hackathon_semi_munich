#!/usr/bin/env python3
################################################################################
# \copyright
# (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
# Technologies AG. All rights reserved.
# This software, associated documentation and materials ("Software") is
# owned by Infineon Technologies AG or one of its affiliates ("Infineon")
# and is protected by and subject to worldwide patent protection, worldwide
# copyright laws, and international treaty provisions. Therefore, you may use
# this Software only as provided in the license agreement accompanying the
# software package from which you obtained this Software. If no license
# agreement applies, then any use, reproduction, modification, translation, or
# compilation of this Software is prohibited without the express written
# permission of Infineon.
#
# Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
# IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
# INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
# THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
# SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
# Infineon reserves the right to make changes to the Software without notice.
# You are responsible for properly designing, programming, and testing the
# functionality and safety of your intended application of the Software, as
# well as complying with any legal requirements related to its use. Infineon
# does not guarantee that the Software will be free from intrusion, data theft
# or loss, or other breaches ("Security Breaches"), and Infineon shall have
# no liability arising out of any Security Breaches. Unless otherwise
# explicitly approved by Infineon, the Software may not be used in any
# application where a failure of the Product or any consequences of the use
# thereof can reasonably be expected to result in personal injury.
################################################################################
"""Sign a device CSR with a local throwaway test CA.

Standalone helper for *manual* provisioning testing. It stands in for the OEM
CA (whose CSR -> certificate signing step is out of scope for the provisioning
tool) so you can drive the provisioning flow by hand:

    python provisioner.py gen-key  --slot 0
    python provisioner.py get-csr  --slot 0 -o dev.csr
    python test/sign_csr.py dev.csr -o dev.crt --ca-key CA.key --ca-crt CA.crt
    python provisioner.py put-cert --slot 1 -i dev.crt
    python provisioner.py get-cert --slot 1 -o readback.crt
    python provisioner.py prove-cert --key-slot 0 --cert-slot 1

The issued certificate carries the device public key taken from the CSR, so
prove-cert's key<->cert binding check will pass. The CA key/certificate are
mandatory and must be chosen explicitly -- the repo ships two ready test roots:
``test/ca_ec256.{key,crt}`` (classical ECDSA P-256) and
``test/ca_mldsa87.{key,crt}`` (post-quantum ML-DSA-87). Generate more with
``test/gen_ca.py``.

Input CSR and output certificate may be DER or PEM; the encoding is
auto-detected on input and selected with ``--out-form`` on output (default DER,
which is what ``put-cert`` expects).

Requires the ``cryptography`` package (already a host dependency for
prove-cert): ``python -m pip install -r requirements.txt``.
"""
from __future__ import annotations

import argparse
import datetime
import sys
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent


def _fail(msg: str) -> "None":
    print(f"[FAIL] {msg}", file=sys.stderr)
    raise SystemExit(1)


def _load_csr(data: bytes):
    """Load a PKCS#10 CSR, auto-detecting PEM vs DER."""
    from cryptography import x509

    if b"-----BEGIN" in data:
        return x509.load_pem_x509_csr(data)
    return x509.load_der_x509_csr(data)


def _csr_pop_valid(csr) -> bool:
    """Verify the CSR proof-of-possession signature.

    ``cryptography`` (<=48) does not implement ``is_signature_valid`` for the
    ML-DSA ``signatureAlgorithm``, so for post-quantum keys the check is done
    explicitly against the parsed CertificationRequestInfo. Classical keys keep
    using the library's built-in verification.
    """
    try:
        from cryptography.hazmat.primitives.asymmetric import mldsa
    except ImportError:
        return csr.is_signature_valid

    pub = csr.public_key()
    if isinstance(pub, (mldsa.MLDSA44PublicKey, mldsa.MLDSA65PublicKey,
                        mldsa.MLDSA87PublicKey)):
        try:
            pub.verify(csr.signature, csr.tbs_certrequest_bytes)
            return True
        except Exception:
            return False
    return csr.is_signature_valid


def _is_mldsa_key(pub) -> bool:
    try:
        from cryptography.hazmat.primitives.asymmetric import mldsa
    except ImportError:
        return False
    return isinstance(pub, (mldsa.MLDSA44PublicKey, mldsa.MLDSA65PublicKey,
                            mldsa.MLDSA87PublicKey))


def _mldsa_mod():
    """Return the ``mldsa`` module if the installed cryptography exposes it."""
    try:
        from cryptography.hazmat.primitives.asymmetric import mldsa
        return mldsa
    except ImportError:
        return None


def _is_mldsa_privkey(key) -> bool:
    """True if ``key`` is an ML-DSA (post-quantum) private key."""
    mldsa = _mldsa_mod()
    if mldsa is None:
        return False
    return isinstance(key, (mldsa.MLDSA44PrivateKey, mldsa.MLDSA65PrivateKey,
                            mldsa.MLDSA87PrivateKey))


# --- Minimal DER helpers for hand-assembling an ML-DSA certificate ----------
# cryptography (<=48) cannot embed an ML-DSA public key via CertificateBuilder,
# so for post-quantum device keys the throwaway test CA hand-builds the cert.

def _der_len(n: int) -> bytes:
    if n < 0x80:
        return bytes([n])
    out = []
    while n:
        out.append(n & 0xFF)
        n >>= 8
    out.reverse()
    return bytes([0x80 | len(out)]) + bytes(out)


def _tlv(tag: int, content: bytes) -> bytes:
    return bytes([tag]) + _der_len(len(content)) + content


def _der_int(n: int) -> bytes:
    if n == 0:
        body = b"\x00"
    else:
        body = n.to_bytes((n.bit_length() + 7) // 8, "big")
        if body[0] & 0x80:
            body = b"\x00" + body
    return _tlv(0x02, body)


def _der_time(dt) -> bytes:
    if 1950 <= dt.year < 2050:
        return _tlv(0x17, dt.strftime("%y%m%d%H%M%SZ").encode("ascii"))
    return _tlv(0x18, dt.strftime("%Y%m%d%H%M%SZ").encode("ascii"))


# ecdsa-with-SHA{256,384,512} share the OID prefix 1.2.840.10045.4.3.{2,3,4};
# the paired digest and the final OID byte are selected by the EC curve.
_EC_CURVE_SIG = {
    "secp384r1": ("SHA384", 0x03),
    "secp521r1": ("SHA512", 0x04),
}


def _ec_sig_hash(curve_name: str):
    """Return the ``hashes`` instance paired with an EC curve (default SHA-256)."""
    from cryptography.hazmat.primitives import hashes
    name = _EC_CURVE_SIG.get(curve_name, ("SHA256", 0x02))[0]
    return getattr(hashes, name)()


def _ca_builder_hash(ca_key):
    """Digest for CertificateBuilder.sign(): curve-paired for EC, SHA-256 for RSA."""
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec

    if isinstance(ca_key, ec.EllipticCurvePrivateKey):
        return _ec_sig_hash(ca_key.curve.name)
    return hashes.SHA256()


def _ca_sig_algid_and_sign(ca_key, tbs: bytes):
    """Return (AlgorithmIdentifier DER, signature bytes) for the test CA key."""
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa

    if isinstance(ca_key, ec.EllipticCurvePrivateKey):
        hash_name, last = _EC_CURVE_SIG.get(ca_key.curve.name, ("SHA256", 0x02))
        algid = bytes([0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48,
                       0xCE, 0x3D, 0x04, 0x03, last])  # ecdsa-with-SHA{256,384,512}
        return algid, ca_key.sign(tbs, ec.ECDSA(getattr(hashes, hash_name)()))
    if isinstance(ca_key, rsa.RSAPrivateKey):
        algid = bytes([0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86,
                       0xF7, 0x0D, 0x01, 0x01, 0x0B, 0x05, 0x00])  # sha256WithRSA
        return algid, ca_key.sign(tbs, padding.PKCS1v15(), hashes.SHA256())

    mldsa = _mldsa_mod()
    if mldsa is not None and isinstance(
            ca_key, (mldsa.MLDSA44PrivateKey, mldsa.MLDSA65PrivateKey,
                     mldsa.MLDSA87PrivateKey)):
        # id-ml-dsa-{44,65,87} = 2.16.840.1.101.3.4.3.{17,18,19}, no parameters.
        if isinstance(ca_key, mldsa.MLDSA87PrivateKey):
            last = 0x13
        elif isinstance(ca_key, mldsa.MLDSA65PrivateKey):
            last = 0x12
        else:
            last = 0x11
        algid = bytes([0x30, 0x0B, 0x06, 0x09, 0x60, 0x86, 0x48,
                       0x01, 0x65, 0x03, 0x04, 0x03, last])
        return algid, ca_key.sign(tbs)   # pure ML-DSA (FIPS 204), no pre-hash

    _fail("Test CA key must be EC, RSA or ML-DSA to sign a certificate")
    return b"", b""   # unreachable


def _hand_build_mldsa_cert(csr, ca_key, ca_crt, *, days: int,
                           out_pem: bool) -> bytes:
    """Assemble an X.509 v3 leaf cert from the CSR, signed by the test CA.

    Used whenever ML-DSA is involved on either side: the leaf carries the CSR's
    public key (ML-DSA or classical) and the issuer signature is produced by
    ``_ca_sig_algid_and_sign`` (EC, RSA or ML-DSA CA key)."""
    import datetime

    from cryptography.hazmat.primitives import serialization
    from cryptography import x509

    now = datetime.datetime.now(datetime.timezone.utc)
    not_before = now - datetime.timedelta(minutes=5)
    not_after = now + datetime.timedelta(days=days)

    spki = csr.public_key().public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo)

    ca_sig_algid, _ = _ca_sig_algid_and_sign(ca_key, b"")  # algid only

    # X.509 v3 extensions mandated by SPDM 1.3+ leaf validation (libspdm rejects
    # a leaf without them): basicConstraints CA:FALSE and keyUsage with the
    # digitalSignature bit. Mirrors the ECDSA CertificateBuilder path below.
    basic_constraints = _tlv(
        0x30,
        b"\x06\x03\x55\x1d\x13"                 # OID 2.5.29.19 basicConstraints
        + b"\x01\x01\xff"                       # critical = TRUE
        + _tlv(0x04, _tlv(0x30, b"")))          # SEQUENCE{} -> cA FALSE
    key_usage = _tlv(
        0x30,
        b"\x06\x03\x55\x1d\x0f"                 # OID 2.5.29.15 keyUsage
        + b"\x01\x01\xff"                       # critical = TRUE
        + _tlv(0x04, b"\x03\x02\x07\x80"))      # BIT STRING: digitalSignature
    extensions = _tlv(0xA3, _tlv(0x30, basic_constraints + key_usage))

    tbs_body = (
        _tlv(0xA0, _der_int(2))                        # version [0] v3
        + _der_int(x509.random_serial_number())         # serialNumber
        + ca_sig_algid                                  # signature algorithm
        + ca_crt.subject.public_bytes()                 # issuer
        + _tlv(0x30, _der_time(not_before)              # validity
               + _der_time(not_after))
        + csr.subject.public_bytes()                    # subject
        + spki                                          # subjectPublicKeyInfo
        + extensions                                    # [3] extensions
    )
    tbs = _tlv(0x30, tbs_body)

    _, sig = _ca_sig_algid_and_sign(ca_key, tbs)
    cert_der = _tlv(0x30, tbs + ca_sig_algid + _tlv(0x03, b"\x00" + sig))

    if out_pem:
        cert = x509.load_der_x509_certificate(cert_der)
        return cert.public_bytes(serialization.Encoding.PEM)
    return cert_der


def ca_sign_csr(csr_der: bytes, ca_key_pem: bytes, ca_crt_pem: bytes,
                *, days: int = 3650, out_pem: bool = False) -> bytes:
    """Sign a CSR with the given CA and return the encoded X.509 certificate.

    Stands in for the out-of-scope OEM CA. The issued certificate carries the
    device public key taken from the CSR.
    """
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization

    csr = _load_csr(csr_der)
    if not _csr_pop_valid(csr):
        _fail("CSR proof-of-possession signature is invalid")

    ca_key = serialization.load_pem_private_key(ca_key_pem, password=None)
    ca_crt = x509.load_pem_x509_certificate(ca_crt_pem)

    # cryptography's CertificateBuilder cannot embed an ML-DSA public key, so
    # post-quantum device certs are hand-assembled. The same DER path is used
    # whenever the CA key is ML-DSA (its issuer signature can't go through the
    # classical CertificateBuilder.sign() path below).
    if _is_mldsa_key(csr.public_key()) or _is_mldsa_privkey(ca_key):
        return _hand_build_mldsa_cert(csr, ca_key, ca_crt,
                                      days=days, out_pem=out_pem)

    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (
        x509.CertificateBuilder()
        .subject_name(csr.subject)
        .issuer_name(ca_crt.subject)
        .public_key(csr.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=5))
        .not_valid_after(now + datetime.timedelta(days=days))
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True, content_commitment=False,
                key_encipherment=False, data_encipherment=False,
                key_agreement=False, key_cert_sign=False, crl_sign=False,
                encipher_only=False, decipher_only=False),
            critical=True)
        .add_extension(
            x509.SubjectKeyIdentifier.from_public_key(csr.public_key()),
            critical=False)
        .add_extension(
            x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_crt.public_key()),
            critical=False)
        .sign(private_key=ca_key, algorithm=_ca_builder_hash(ca_key))
    )
    encoding = serialization.Encoding.PEM if out_pem else serialization.Encoding.DER
    return cert.public_bytes(encoding)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Sign a device CSR with the local test CA.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument("csr", type=Path, help="Input CSR file (DER or PEM)")
    ap.add_argument("-o", "--out", type=Path, default=None,
                    help="Output certificate file (default: <csr>.crt)")
    ap.add_argument("--out-form", choices=("der", "pem"), default="der",
                    help="Output certificate encoding (default der; put-cert expects der)")
    ap.add_argument("--ca-key", type=Path, required=True,
                    help="CA private key PEM (e.g. test/ca_ec256.key or "
                         "test/ca_mldsa87.key)")
    ap.add_argument("--ca-crt", type=Path, required=True,
                    help="CA certificate PEM (e.g. test/ca_ec256.crt or "
                         "test/ca_mldsa87.crt)")
    ap.add_argument("--days", type=int, default=3650,
                    help="Validity period in days (default 3650)")
    args = ap.parse_args(argv)

    try:
        import cryptography  # noqa: F401
    except ImportError:
        _fail("The 'cryptography' package is required. Run: "
              "python -m pip install -r requirements.txt")

    for path in (args.csr, args.ca_key, args.ca_crt):
        if not path.exists():
            _fail(f"File not found: {path}")

    out_path = args.out or args.csr.with_suffix(".crt")
    out_path.parent.mkdir(parents=True, exist_ok=True)

    cert_bytes = ca_sign_csr(
        args.csr.read_bytes(),
        args.ca_key.read_bytes(),
        args.ca_crt.read_bytes(),
        days=args.days,
        out_pem=(args.out_form == "pem"),
    )
    out_path.write_bytes(cert_bytes)
    print(f"[ok] CA-signed certificate written ({args.out_form.upper()}): {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
