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
"""PROV_PROVE_CERT (0x73) -- prove possession of the certified private key
(Packet formats).

Request (offset==0): slot_id(1), offset(2)=0, len(2), nonce(32).
Request (offset>0) : slot_id(1), offset(2), len(2).
Response (per chunk): status(1), total(2), alg_id(2), cert_sha256(32), ret(2),
                      signature_bytes(ret).

The signature is streamed in chunks (an ML-DSA-87 signature is 4627 B); the
session layer reassembles it. Host-side check (all must pass, else non-zero
exit):
  1. SHA-256 of the local ``-i`` blob equals the device ``cert_sha256`` (the
     device signs over the digest of the WHOLE stored blob -- a single cert or
     an SPDM cert_chain structure).
  2. The signature verifies against the LEAF certificate's public key over the
     message ``nonce || cert_sha256``. The leaf is extracted from the blob
     (single cert, or last cert past the SPDM header).
ECDSA (P-256/P-384/P-521) returns a raw ``r || s`` signature (64/96/132 B)
re-encoded to DER and verified with the curve's paired digest (SHA-256/384/512);
ML-DSA-87 returns the raw 4627-byte signature verified in pure mode.
``cryptography`` is imported lazily so the other commands work without it
installed (ML-DSA-87 verification requires cryptography >= 46).
"""
from __future__ import annotations

import os

from core.packet import (
    AlgId,
    MAX_CHUNK,
    NONCE_LEN,
    SHA256_LEN,
    SIG_LEN_MLDSA87,
    SIG_LEN_P256,
    SIG_LEN_P384,
    SIG_LEN_P521,
    Opcode,
    ProtocolError,
    ProvStatus,
    enum_name,
    read_request,
)
from core.registry import ProvCommand, register


@register
class ProveCertCommand(ProvCommand):
    name = "prove-cert"
    opcode = Opcode.PROVE_CERT
    help = "Challenge the device to prove key possession (PROV_PROVE_CERT, 0x73)"
    description = (
        "Prove the device holds the private key for the certificate in --slot.\n"
        "Pass -i the exact bytes stored in the slot: a single certificate (DER or\n"
        "PEM), or an SPDM cert_chain binary blob (as written by\n"
        "'put-cert --save-blob' or 'get-cert'). The host verifies that (1) SHA-256\n"
        "of the -i blob matches the device digest (taken over the whole stored\n"
        "blob) and (2) the returned signature checks out over nonce||digest, using\n"
        "the LEAF certificate's public key. Exits non-zero on any mismatch.\n"
        "Requires the 'cryptography' package.")
    examples = (
        "provisioner prove-cert --slot 1 -i build/dev.crt",
        "provisioner prove-cert --slot 1 -i build/dev.crt --nonce 00112233...",
    )

    # alg_id -> (paired digest name, raw r||s signature length) for ECDSA curves.
    _ECDSA_PARAMS = {
        AlgId.ECDSA_P256: ("SHA256", SIG_LEN_P256),
        AlgId.ECDSA_P384: ("SHA384", SIG_LEN_P384),
        AlgId.ECDSA_P521: ("SHA512", SIG_LEN_P521),
    }

    @classmethod
    def add_arguments(cls, parser):
        parser.add_argument("--slot", type=int, required=True,
                            help="Certificate slot to challenge")
        parser.add_argument("-i", "--in", dest="infile", required=True,
                            help="Local copy of the exact stored blob: a single cert "
                                 "(DER or PEM), or an SPDM cert_chain .bin from "
                                 "put-cert --save-blob / get-cert")
        parser.add_argument("--nonce",
                            help="Optional 32-byte nonce as hex (default: random)")

    def _nonce(self, args) -> bytes:
        if args.nonce:
            nonce = bytes.fromhex(args.nonce)
            if len(nonce) != NONCE_LEN:
                raise ValueError(f"--nonce must be {NONCE_LEN} bytes ({2 * NONCE_LEN} hex chars)")
            return nonce
        return os.urandom(NONCE_LEN)

    def run(self, ctx, args) -> int:
        nonce = self._nonce(args)

        if ctx.dry_run:
            ctx.session.send_command(
                self.opcode, read_request(args.slot, 0, MAX_CHUNK) + nonce,
                name=self.name)
            print(f"[dry-run] {self.name}: challenge built (slot={args.slot}, "
                  f"nonce={nonce.hex()[:16]}...).")
            return 0

        status, alg_id, cert_sha256, signature = ctx.session.prove_cert(args.slot, nonce)
        print(f"prov_cmd_status : {enum_name(ProvStatus, status)}")
        if status != ProvStatus.PROV_OK:
            return int(status)
        cert_sha256 = bytes(cert_sha256)

        print(f"alg             : {enum_name(AlgId, alg_id)} (0x{alg_id:04X})")
        print(f"cert_sha256     : {cert_sha256.hex()}")
        print(f"nonce           : {nonce.hex()}")
        print(f"sig_len         : {len(signature)}")

        # 1) Integrity: the -i blob must match the digest the device signed
        #    (the device hashes the WHOLE stored blob, chain or not).
        import hashlib
        with open(args.infile, "rb") as f:
            blob = f.read()
        local_sha = hashlib.sha256(blob).digest()
        if local_sha != cert_sha256:
            print("FAIL: local blob SHA-256 does not match device cert_sha256.")
            print(f"  local : {local_sha.hex()}")
            return 1

        # 2) Signature: verify over nonce || cert_sha256 using the LEAF cert's
        #    public key (extracted from a single cert or an SPDM cert_chain).
        message = nonce + cert_sha256
        leaf_der = self._extract_leaf_cert(blob)
        if alg_id in self._ECDSA_PARAMS:
            hash_name, sig_len = self._ECDSA_PARAMS[alg_id]
            ok = self._verify_ecdsa(leaf_der, message, signature, hash_name, sig_len)
        elif alg_id == AlgId.ML_DSA_87:
            ok = self._verify_mldsa87(leaf_der, message, signature)
        else:
            print(f"FAIL: unsupported alg 0x{alg_id:04X}; host verifies "
                  "ECDSA-P256/P384/P521 and ML-DSA-87 only.")
            return 1
        if not ok:
            print("FAIL: signature verification failed.")
            return 1

        print("OK: device proved possession of the certified private key.")
        return 0

    @staticmethod
    def _split_der_certs(data: bytes) -> list:
        """Split a buffer of back-to-back DER certificates into a list of DER
        byte-strings by walking each ASN.1 SEQUENCE (tag 0x30) TLV header."""
        certs = []
        off = 0
        n = len(data)
        while off < n:
            if data[off] != 0x30 or off + 1 >= n:
                break
            first = data[off + 1]
            if first < 0x80:
                hdr, content = 2, first
            else:
                num = first & 0x7F
                if num == 0 or off + 2 + num > n:
                    break
                content = int.from_bytes(data[off + 2:off + 2 + num], "big")
                hdr = 2 + num
            total = hdr + content
            if off + total > n:
                break
            certs.append(data[off:off + total])
            off += total
        return certs

    # SPDM RootHash widths for the supported BaseHashAlgo values (SHA-256/384/512).
    _ROOT_HASH_LENS = (SHA256_LEN, 48, 64)

    @classmethod
    def _extract_leaf_cert(cls, blob: bytes) -> bytes:
        """Return the leaf certificate's DER bytes from a stored blob that is either
        a single certificate or an SPDM cert_chain structure
        (Length(2) | Reserved(2) | RootHash(H) | certs[root..leaf]).
        The RootHash width H is inferred from the standard SPDM hash sizes, so
        SHA-256/384/512 chains all parse. A bare concatenated DER chain (no SPDM
        header) is rejected."""
        if b"-----BEGIN" in blob:
            from cryptography import x509
            from cryptography.hazmat.primitives import serialization
            try:
                certs = x509.load_pem_x509_certificates(blob)
            except AttributeError:
                certs = [x509.load_pem_x509_certificate(blob)]
            if not certs:
                raise ProtocolError("no certificate found in PEM blob")
            return certs[-1].public_bytes(serialization.Encoding.DER)

        # An SPDM cert_chain begins with a little-endian Length, not a DER tag.
        is_spdm = blob[:1] != b"\x30"
        if is_spdm:
            length = int.from_bytes(blob[0:2], "little")
            if length != len(blob):
                raise ProtocolError(
                    f"SPDM cert_chain length field ({length}) does not match blob "
                    f"size ({len(blob)})")
            # The RootHash width is not encoded in the header; infer it by finding
            # the hash size whose trailing certs tile the body exactly (root..leaf).
            certs = None
            for hlen in cls._ROOT_HASH_LENS:
                hdr = 4 + hlen
                if len(blob) <= hdr or blob[hdr] != 0x30:
                    continue
                candidate = cls._split_der_certs(blob[hdr:])
                if candidate and sum(len(c) for c in candidate) == len(blob) - hdr:
                    certs = candidate
                    break
            if not certs:
                raise ProtocolError(
                    "no certificate found in stored blob (unrecognized SPDM "
                    "cert_chain RootHash size or malformed certificates)")
            return certs[-1]  # leaf is last in root..leaf order

        body = blob
        certs = cls._split_der_certs(body)
        if not certs:
            raise ProtocolError("no certificate found in stored blob")
        # A single-cert slot must hold exactly one cert; multi-cert chains are only
        # supported inside an SPDM cert_chain structure (Option 1: two canonical
        # forms -- single leaf cert or SPDM cert_chain).
        if len(certs) > 1:
            raise ProtocolError(
                "blob contains a bare concatenated DER chain; only a single "
                "certificate or an SPDM cert_chain is supported.")
        return certs[-1]  # leaf is last in root..leaf order

    @staticmethod
    def _verify_ecdsa(cert_bytes: bytes, message: bytes, signature: bytes,
                      hash_name: str, sig_len: int) -> bool:
        """Verify a raw ``r||s`` ECDSA signature against the cert's public key.

        ``hash_name`` is the paired digest (SHA256/SHA384/SHA512) and ``sig_len``
        the expected raw signature length (64/96/132) for the curve."""
        try:
            from cryptography import x509
            from cryptography.exceptions import InvalidSignature
            from cryptography.hazmat.primitives import hashes
            from cryptography.hazmat.primitives.asymmetric import ec
            from cryptography.hazmat.primitives.asymmetric.utils import (
                encode_dss_signature,
            )
        except ImportError as exc:  # pragma: no cover - depends on environment
            raise RuntimeError(
                "prove-cert requires the 'cryptography' package "
                "(pip install -r requirements.txt)") from exc

        try:
            cert = x509.load_der_x509_certificate(cert_bytes)
        except ValueError:
            cert = x509.load_pem_x509_certificate(cert_bytes)

        public_key = cert.public_key()
        if not isinstance(public_key, ec.EllipticCurvePublicKey):
            raise ValueError("certificate public key is not an EC key")

        if len(signature) != sig_len:
            raise ProtocolError(
                f"ECDSA signature must be {sig_len} bytes (r||s), got {len(signature)}")
        half = sig_len // 2
        r = int.from_bytes(signature[:half], "big")
        s = int.from_bytes(signature[half:], "big")
        der_sig = encode_dss_signature(r, s)

        try:
            public_key.verify(der_sig, message, ec.ECDSA(getattr(hashes, hash_name)()))
            return True
        except InvalidSignature:
            return False

    @staticmethod
    def _verify_mldsa87(cert_bytes: bytes, message: bytes, signature: bytes) -> bool:
        """Verify a raw ML-DSA-87 signature against the cert's public key.

        Pure ML-DSA (empty context), matching the device's wc_MlDsaKey_Sign.
        Requires cryptography >= 46, which parses the leaf certificate's public
        key as an MLDSA87PublicKey.
        """
        try:
            from cryptography import x509
            from cryptography.exceptions import InvalidSignature
            from cryptography.hazmat.primitives.asymmetric import mldsa
        except ImportError as exc:  # pragma: no cover - depends on environment
            raise RuntimeError(
                "ML-DSA-87 prove-cert requires cryptography >= 46 "
                "(pip install -r requirements.txt)") from exc

        try:
            cert = x509.load_der_x509_certificate(cert_bytes)
        except ValueError:
            cert = x509.load_pem_x509_certificate(cert_bytes)

        public_key = cert.public_key()
        if not isinstance(public_key, mldsa.MLDSA87PublicKey):
            raise ValueError("certificate public key is not an ML-DSA-87 key")

        if len(signature) != SIG_LEN_MLDSA87:
            raise ProtocolError(
                f"ML-DSA-87 signature must be {SIG_LEN_MLDSA87} bytes, "
                f"got {len(signature)}")
        try:
            public_key.verify(signature, message)
            return True
        except InvalidSignature:
            return False
