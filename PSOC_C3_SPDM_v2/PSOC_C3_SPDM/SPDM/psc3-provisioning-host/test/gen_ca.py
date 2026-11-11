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
"""Generate a self-signed test root CA (classical or post-quantum ML-DSA).

Stands in for the OEM root CA when driving provisioning / SPDM cert-chain tests
by hand. Supports an ML-DSA-87 (FIPS 204) root so the whole SPDM ``cert_chain``
can be post-quantum (root ML-DSA-87 -> leaf ML-DSA-87):

    # 1. create a PQC root CA
    python test/gen_ca.py --alg ml-dsa-87 --cn "PSC3 PQC Root CA" \
        --out-key test/ca_mldsa87.key --out-crt test/ca_mldsa87.crt

    # 2. sign a device CSR with it (sign_csr.py auto-detects the ML-DSA CA key)
    python test/sign_csr.py dev.csr -o leaf.crt \
        --ca-key test/ca_mldsa87.key --ca-crt test/ca_mldsa87.crt

    # 3. build the SPDM cert_chain (root-first); RootHash alg must match the
    #    SPDM negotiated BaseHashAlgo (SHA-512 for the CNSA 2.0 / ML-DSA profile)
    python provisioner.py put-cert --slot 1 --key-slot 0 --spdm-chain \
        --hash-alg sha512 -i test/ca_mldsa87.crt -i leaf.crt

The certificate is hand-assembled through the same DER path ``sign_csr.py`` uses
for ML-DSA, so it works uniformly for EC, RSA and ML-DSA keys regardless of the
installed ``cryptography`` version.

Requires the ``cryptography`` package: python -m pip install -r requirements.txt
"""
from __future__ import annotations

import argparse
import datetime
import sys
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent
if str(TEST_DIR) not in sys.path:
    sys.path.insert(0, str(TEST_DIR))

from sign_csr import (  # noqa: E402  (sibling helper module in the same dir)
    _ca_sig_algid_and_sign,
    _der_int,
    _der_time,
    _ec_sig_hash,
    _fail,
    _mldsa_mod,
    _tlv,
)

# X.509 v3 extensions for a CA certificate, both critical: basicConstraints
# cA=TRUE and keyUsage with keyCertSign|cRLSign (libspdm requires CA:TRUE on
# non-leaf certificates when walking the cert_chain).
_EXT_BASIC_CONSTRAINTS_CA = _tlv(
    0x30,
    b"\x06\x03\x55\x1d\x13"                      # OID 2.5.29.19 basicConstraints
    + b"\x01\x01\xff"                            # critical = TRUE
    + _tlv(0x04, _tlv(0x30, b"\x01\x01\xff")))   # SEQUENCE{ cA TRUE }
_EXT_KEY_USAGE_CA = _tlv(
    0x30,
    b"\x06\x03\x55\x1d\x0f"                      # OID 2.5.29.15 keyUsage
    + b"\x01\x01\xff"                            # critical = TRUE
    + _tlv(0x04, b"\x03\x02\x01\x06"))           # BIT STRING: keyCertSign|cRLSign


def _gen_key(alg: str):
    from cryptography.hazmat.primitives.asymmetric import ec, rsa

    if alg == "ecdsa-p256":
        return ec.generate_private_key(ec.SECP256R1())
    if alg == "ecdsa-p384":
        return ec.generate_private_key(ec.SECP384R1())
    if alg == "ecdsa-p521":
        return ec.generate_private_key(ec.SECP521R1())
    if alg == "rsa-2048":
        return rsa.generate_private_key(public_exponent=65537, key_size=2048)
    if alg.startswith("ml-dsa-"):
        mldsa = _mldsa_mod()
        if mldsa is None:
            _fail("ML-DSA requires cryptography>=46 "
                  "(python -m pip install -r requirements.txt)")
        return {
            "ml-dsa-44": mldsa.MLDSA44PrivateKey,
            "ml-dsa-65": mldsa.MLDSA65PrivateKey,
            "ml-dsa-87": mldsa.MLDSA87PrivateKey,
        }[alg].generate()
    _fail(f"Unsupported --alg: {alg}")
    return None   # unreachable


def _build_name(args):
    from cryptography import x509
    from cryptography.x509.oid import NameOID

    attrs = [x509.NameAttribute(NameOID.COMMON_NAME, args.cn)]
    if args.org:
        attrs.append(x509.NameAttribute(NameOID.ORGANIZATION_NAME, args.org))
    if args.ou:
        attrs.append(x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, args.ou))
    if args.country:
        attrs.append(x509.NameAttribute(NameOID.COUNTRY_NAME, args.country))
    if args.serial:
        attrs.append(x509.NameAttribute(NameOID.SERIAL_NUMBER, args.serial))
    return x509.Name(attrs)


def build_self_signed_ca(priv, name, *, days: int) -> bytes:
    """Hand-assemble a self-signed X.509 v3 root CA certificate (DER)."""
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization

    now = datetime.datetime.now(datetime.timezone.utc)
    not_before = now - datetime.timedelta(minutes=5)
    not_after = now + datetime.timedelta(days=days)

    name_der = name.public_bytes()                       # issuer == subject
    spki = priv.public_key().public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo)
    sig_algid, _ = _ca_sig_algid_and_sign(priv, b"")     # AlgorithmIdentifier only
    extensions = _tlv(
        0xA3, _tlv(0x30, _EXT_BASIC_CONSTRAINTS_CA + _EXT_KEY_USAGE_CA))

    tbs_body = (
        _tlv(0xA0, _der_int(2))                          # version [0] v3
        + _der_int(x509.random_serial_number())           # serialNumber
        + sig_algid                                       # signature algorithm
        + name_der                                        # issuer
        + _tlv(0x30, _der_time(not_before) + _der_time(not_after))
        + name_der                                        # subject
        + spki                                            # subjectPublicKeyInfo
        + extensions                                      # [3] extensions
    )
    tbs = _tlv(0x30, tbs_body)
    _, sig = _ca_sig_algid_and_sign(priv, tbs)
    return _tlv(0x30, tbs + sig_algid + _tlv(0x03, b"\x00" + sig))


def _verify_self_signature(cert) -> None:
    """Verify a self-signed cert against its own public key (EC/RSA/ML-DSA)."""
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa

    pub = cert.public_key()
    mldsa = _mldsa_mod()
    if mldsa is not None and isinstance(
            pub, (mldsa.MLDSA44PublicKey, mldsa.MLDSA65PublicKey,
                  mldsa.MLDSA87PublicKey)):
        pub.verify(cert.signature, cert.tbs_certificate_bytes)
        return
    if isinstance(pub, ec.EllipticCurvePublicKey):
        pub.verify(cert.signature, cert.tbs_certificate_bytes,
                   ec.ECDSA(_ec_sig_hash(pub.curve.name)))
        return
    if isinstance(pub, rsa.RSAPublicKey):
        pub.verify(cert.signature, cert.tbs_certificate_bytes,
                   padding.PKCS1v15(), hashes.SHA256())
        return
    _fail("cannot verify CA signature: unsupported key type")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Generate a self-signed test root CA (classical or ML-DSA).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument("--alg", required=True,
                    choices=("ecdsa-p256", "ecdsa-p384", "ecdsa-p521", "rsa-2048",
                             "ml-dsa-44", "ml-dsa-65", "ml-dsa-87"),
                    help="Root CA key algorithm")
    ap.add_argument("--cn", required=True, help="CA commonName")
    ap.add_argument("--org", help="CA organization (optional)")
    ap.add_argument("--ou", help="CA organizationalUnit (optional)")
    ap.add_argument("--country", help="CA country, 2-letter (optional)")
    ap.add_argument("--serial", help="CA serialNumber attribute (optional)")
    ap.add_argument("--days", type=int, default=3650,
                    help="Validity period in days (default 3650)")
    ap.add_argument("--out-key", type=Path, required=True,
                    help="Output CA private key path (PEM, PKCS#8, unencrypted)")
    ap.add_argument("--out-crt", type=Path, required=True,
                    help="Output CA certificate path")
    ap.add_argument("--out-form", choices=("pem", "der"), default="pem",
                    help="CA certificate encoding (default pem)")
    args = ap.parse_args(argv)

    try:
        from cryptography import x509
        from cryptography.hazmat.primitives import serialization
    except ImportError:
        _fail("The 'cryptography' package is required. Run: "
              "python -m pip install -r requirements.txt")

    priv = _gen_key(args.alg)
    name = _build_name(args)
    cert_der = build_self_signed_ca(priv, name, days=args.days)

    cert = x509.load_der_x509_certificate(cert_der)
    try:
        _verify_self_signature(cert)
    except Exception as exc:            # pragma: no cover - defensive
        _fail(f"generated CA failed self-signature verification: {exc}")

    args.out_key.parent.mkdir(parents=True, exist_ok=True)
    args.out_crt.parent.mkdir(parents=True, exist_ok=True)
    args.out_key.write_bytes(priv.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption()))
    if args.out_form == "pem":
        args.out_crt.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    else:
        args.out_crt.write_bytes(cert_der)

    print(f"[ok] root CA key  ({args.alg}): {args.out_key}")
    print(f"[ok] root CA cert ({args.out_form.upper()}, {len(cert_der)} B DER, "
          f"self-signature verified): {args.out_crt}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
