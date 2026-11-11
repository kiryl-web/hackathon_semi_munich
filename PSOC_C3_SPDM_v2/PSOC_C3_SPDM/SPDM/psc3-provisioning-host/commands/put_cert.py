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
"""PROV_CERT_WRITE (0x71) -- stage & commit a device certificate
(Packet formats).

First chunk (offset==0): slot_id(1), offset(2)=0, key_slot(1), total_len(4),
sha256(32), bytes. Later chunks: slot_id(1), offset(2), bytes.
Response (per chunk): prov_cmd_status(1), recv_total(4) and, on the final chunk
(recv_total == total_len), committed(1), cert_len(2).

The whole certificate length is known up front (the file size), so every chunk
is emitted in ONE DFU session; the device stages the bytes, verifies the SHA-256
the host supplies, then atomically commits on the last chunk.
"""
from __future__ import annotations

import hashlib
import os

from core.packet import (
    MAX_CHUNK,
    Opcode,
    ProtocolError,
    ProvStatus,
    enum_name,
    u16,
    u32,
)
from core.registry import ProvCommand, register


@register
class PutCertCommand(ProvCommand):
    name = "put-cert"
    opcode = Opcode.CERT_WRITE
    help = "Stage and commit a certificate into a slot (PROV_CERT_WRITE, 0x71)"
    description = (
        "Upload a certificate into --slot, bound to key --key-slot. By default a\n"
        "single -i file is stored verbatim (DER pass-through, PEM converted to DER).\n"
        "\n"
        "With --spdm-chain, repeat -i once per certificate in ROOT-first order to\n"
        "build an SPDM cert_chain structure (Length | Reserved | RootHash |\n"
        "certs[root..leaf]). The host computes RootHash over the first (root)\n"
        "certificate with --hash-alg (SHA-256 by default; must match the SPDM\n"
        "negotiated BaseHashAlgo) and prepends the 4-byte header, so the device\n"
        "serves the chain with no runtime formatting. Multiple -i files require\n"
        "--spdm-chain.\n"
        "\n"
        "The host streams the blob in chunks with an integrity SHA-256 the device\n"
        "verifies before atomically committing. Rejected after 'finalize'.")
    examples = (
        "provisioner put-cert --slot 1 --key-slot 0 -i build/dev.crt",
        "provisioner put-cert --slot 1 --key-slot 0 --spdm-chain -i ca.crt -i dev.crt",
    )

    @classmethod
    def add_arguments(cls, parser):
        parser.add_argument("--slot", type=int, required=True,
                            help="Certificate slot to write")
        parser.add_argument("--key-slot", type=int, required=True,
                            help="Key slot the certificate is bound to")
        parser.add_argument("-i", "--in", dest="infile", required=True,
                            action="append", metavar="FILE",
                            help="Certificate file to upload (DER or PEM). With "
                                 "--spdm-chain, repeat -i in ROOT-first order to store a "
                                 "chain; otherwise exactly one -i is allowed.")
        parser.add_argument("--spdm-chain", action="store_true",
                            help="Wrap the certificate(s) in an SPDM cert_chain "
                                 "structure (Length | Reserved | RootHash | "
                                 "certs[root..leaf]); RootHash is the --hash-alg "
                                 "digest of the first (root) cert. Required to pass "
                                 "more than one -i.")
        parser.add_argument("--hash-alg", choices=sorted(cls._HASH_ALGS),
                            default="sha256",
                            help="Hash algorithm for the SPDM cert_chain RootHash "
                                 "(default: sha256). Only used with --spdm-chain and "
                                 "must match the SPDM negotiated BaseHashAlgo.")
        parser.add_argument("--save-blob", metavar="FILE",
                            help="Also write the exact bytes being stored (single cert "
                                 "or SPDM cert_chain) to FILE, e.g. for later "
                                 "'prove-cert -i FILE'.")

    @staticmethod
    def _load_der(path: str) -> bytes:
        """Return the DER bytes of a certificate file (pass-through for DER,
        converted for PEM). Rejects input that is neither DER nor PEM."""
        with open(path, "rb") as f:
            raw = f.read()
        # A DER certificate starts with an ASN.1 SEQUENCE tag (0x30).
        if raw[:1] == b"\x30":
            return raw
        if b"-----BEGIN" in raw:
            try:
                from cryptography import x509
                from cryptography.hazmat.primitives import serialization
            except ImportError as exc:
                raise ProtocolError(
                    "PEM input requires the 'cryptography' package "
                    "(python -m pip install -r requirements.txt), or pass DER."
                ) from exc
            der = b""
            try:
                certs = x509.load_pem_x509_certificates(raw)  # cryptography >= 39
            except AttributeError:
                certs = [x509.load_pem_x509_certificate(raw)]
            for cert in certs:
                der += cert.public_bytes(serialization.Encoding.DER)
            return der
        raise ProtocolError(
            f"{path}: not a recognizable certificate (expected DER starting with "
            "0x30, or PEM containing '-----BEGIN').")

    # SPDM BaseHashAlgo names the host can compute a RootHash for.
    _HASH_ALGS = ("sha256", "sha384", "sha512")

    @classmethod
    def _build_spdm_cert_chain(cls, der_certs: list, hash_alg: str = "sha256") -> bytes:
        """Wrap DER certs (root-first) in an SPDM cert_chain structure:
        Length(2) | Reserved(2) | RootHash(H) | Certificates(root..leaf).
        RootHash is the ``hash_alg`` digest of the root (first) certificate;
        H must match the SPDM negotiated BaseHashAlgo."""
        root_hash = hashlib.new(hash_alg, der_certs[0]).digest()
        certs = b"".join(der_certs)
        length = 4 + len(root_hash) + len(certs)
        if length > 0xFFFF:
            raise ProtocolError(
                f"SPDM cert_chain too large: {length} bytes (max 65535).")
        return u16(length) + u16(0) + root_hash + certs

    def _build_chunks(self, args, data: bytes):
        """Yield ``(opcode, request)`` chunks covering the whole certificate."""
        total = len(data)
        digest = hashlib.sha256(data).digest()
        chunks = []
        off = 0
        first = True
        while off < total or first:
            n = min(MAX_CHUNK, total - off)
            piece = data[off:off + n]
            if off == 0:
                req = (bytes([args.slot & 0xFF]) + u16(0) + bytes([args.key_slot & 0xFF])
                       + u32(total) + digest + piece)
            else:
                req = bytes([args.slot & 0xFF]) + u16(off) + piece
            chunks.append((self.opcode, req))
            off += n
            first = False
            if total == 0:
                break
        return chunks, total, digest

    def run(self, ctx, args) -> int:
        if len(args.infile) > 1 and not args.spdm_chain:
            raise ProtocolError(
                "multiple -i certificates require --spdm-chain; pass a single -i for "
                "a raw certificate.")
        if args.hash_alg != "sha256" and not args.spdm_chain:
            raise ProtocolError(
                "--hash-alg only applies to the SPDM cert_chain RootHash; pass "
                "--spdm-chain or omit --hash-alg.")
        der_certs = [self._load_der(path) for path in args.infile]
        if args.spdm_chain:
            if len(der_certs) == 1:
                print("warning: --spdm-chain with a single certificate sets "
                      "RootHash = hash(this cert); it is only a valid trust anchor "
                      "if the cert is self-signed.")
            data = self._build_spdm_cert_chain(der_certs, args.hash_alg)
        else:
            data = der_certs[0]
        chunks, total, digest = self._build_chunks(args, data)

        if args.save_blob:
            os.makedirs(os.path.dirname(os.path.abspath(args.save_blob)), exist_ok=True)
            with open(args.save_blob, "wb") as f:
                f.write(data)
            print(f"saved blob      : {args.save_blob} ({total} B, "
                  f"sha256={digest.hex()[:16]}...)")

        if ctx.dry_run:
            ctx.session.send_command(self.opcode, chunks[0][1], name=self.name)
            print(f"[dry-run] {self.name}: {len(chunks)} chunk(s) for {total} B cert "
                  f"(sha256={digest.hex()[:16]}...).")
            return 0

        responses = ctx.session.run_commands(chunks, name=self.name)
        final = responses[-1] if responses else None
        for resp in responses:
            status = resp.payload[0] if resp.payload else ProvStatus.PROV_ERR_INTERNAL
            if status != ProvStatus.PROV_OK:
                print(f"prov_cmd_status : {enum_name(ProvStatus, status)}")
                return int(status)

        payload = final.payload
        if len(payload) < 5:
            raise ProtocolError(
                f"PROV_CERT_WRITE final response too short: {len(payload)} bytes")
        recv_total = int.from_bytes(payload[1:5], "little")
        committed = payload[5] if len(payload) >= 6 else 0
        cert_len = int.from_bytes(payload[6:8], "little") if len(payload) >= 8 else 0

        print(f"prov_cmd_status : {enum_name(ProvStatus, ProvStatus.PROV_OK)}")
        print(f"recv_total      : {recv_total} bytes")
        print(f"committed       : {'yes' if committed else 'no'}")
        if cert_len:
            print(f"cert_len        : {cert_len} bytes")

        if recv_total != total or not committed:
            print("error: certificate not committed (length/commit mismatch).")
            return int(ProvStatus.PROV_ERR_INTERNAL)
        return 0
