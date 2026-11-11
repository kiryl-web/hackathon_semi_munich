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
"""Per-transaction session and shared command context.

The DFU Host Tool automatically frames each session with Enter DFU (0x38) at
the start and Exit DFU (0x3B) at the end, so the generated ``.mtbdfu`` contains
only the provisioning command(s) themselves (Packet formats: session model). The ``Context``
bundles what every command needs at run time.
"""
from __future__ import annotations

from dataclasses import dataclass

from core.dfu_backend import DfuBackend
from core.packet import (
    MAX_CHUNK,
    DfuCmd,
    DfuResponse,
    Opcode,
    ProtocolError,
    ProvStatus,
    parse_prove_response,
    parse_pubkey_response,
    parse_read_response,
    read_request,
)


class Session:
    """Dispatches provisioning transactions through a backend."""

    def __init__(self, backend: DfuBackend):
        self.backend = backend

    def send_command(self, opcode: int, data: bytes = b"", *, name=None) -> DfuResponse:
        """Send one provisioning command.

        Enter/Exit DFU framing is added by the DFU Host Tool, so only the
        provisioning command is emitted here. Returns the decoded device
        response (empty payload in dry-run mode).
        """
        if name is None:
            try:
                name = Opcode(opcode).name.lower()
            except ValueError:
                name = f"op_{opcode:02x}"
        cmds = [DfuCmd(opcode, bytes(data), capture=True)]
        captures = self.backend.run_transaction(cmds, name=name)
        raw = captures[0] if captures else b""
        return DfuResponse.from_capture(raw, self.backend.checksum_type)

    def run_commands(self, items, *, name) -> list:
        """Run several provisioning commands inside ONE DFU session.

        ``items`` is an iterable of ``(opcode, data)`` pairs emitted as
        successive ``commands[]`` entries between a single Enter/Exit DFU pair.
        Keeping a chunked transfer in one session is required where the device
        holds transient per-session state (e.g. the PROV_GET_CSR secure RAM
        buffer, discarded at Exit DFU — Packet formats: chunking).
        Returns one decoded :class:`DfuResponse` per command, in order.
        """
        cmds = [DfuCmd(int(opcode), bytes(data), capture=True) for opcode, data in items]
        captures = self.backend.run_transaction(cmds, name=name)
        return [DfuResponse.from_capture(raw, self.backend.checksum_type)
                for raw in captures]

    def read_blob(self, opcode: int, slot_id: int, *, template: bytes = b"",
                  max_chunk: int = MAX_CHUNK, name=None):
        """Read a chunked blob (CSR / certificate) and return ``(status, data)``.

        Implements the offset-based read loop described in Packet formats:
        chunking. The read is done in TWO steps because ``run_commands`` emits
        a single DFU session as a FIXED list of commands decided up front: we
        cannot read ``offset == 0``, look at the returned length, then decide
        how many more chunks to append within the same session. And the reads
        must all share one session, since PROV_GET_CSR's blob lives only in
        secure RAM that is discarded at Exit DFU. So:
          1. a standalone PROBE at ``offset == 0`` learns an initial length so
             we know how many chunk requests to put in the batch, then
          2. the whole blob is fetched within a SINGLE DFU session so the
             transient per-session device state persists across the chunks.
        ``template`` is appended to the first chunk only (the PROV_GET_CSR
        subject DN). Returns the device ``prov_status`` and the assembled bytes
        (empty on any non-OK status).
        """
        if name is None:
            try:
                name = Opcode(opcode).name.lower()
            except ValueError:
                name = f"op_{opcode:02x}"

        # Probe offset 0 to discover an initial total_len (and trigger CSR
        # build). This separate probe is only needed to SIZE the batched read
        # below (see the docstring); it is not the authoritative length.
        # For PROV_GET_CSR the device rebuilds the secure-RAM CSR on every
        # offset==0 request with a freshly randomized ECDSA signature, whose
        # DER length can vary by a byte or two. The probe runs in its own DFU
        # session, so the CSR it sizes is a DIFFERENT instance from the one
        # streamed in the read session below. We therefore treat the probe
        # total as an ESTIMATE only and take the authoritative length from the
        # offset==0 response of the read session. (For static blobs such as
        # certificates the estimate is exact, so this is a no-op there.)
        probe = self.send_command(
            opcode, read_request(slot_id, 0, max_chunk) + template, name=f"{name}_probe")
        status, est_total, _ret, _data = parse_read_response(probe.payload)
        if status != ProvStatus.PROV_OK:
            return status, b""
        if est_total == 0:
            return status, b""

        # Read the whole blob in ONE DFU session so the transient CSR persists
        # across chunks. Plan enough offsets to cover the estimate plus one
        # extra chunk of head-room, since the streamed CSR may be slightly
        # longer than the probe's. Each chunk requests the full max_chunk; the
        # device caps the returned byte count at what is actually available and
        # returns ret==0 once the offset is past the end of the blob.
        plan_total = est_total + max_chunk
        offsets = list(range(0, plan_total, max_chunk))
        items = []
        for off in offsets:
            req = read_request(slot_id, off, max_chunk)
            if off == 0:
                req += template
            items.append((opcode, req))
        responses = self.run_commands(items, name=name)

        blob = bytearray()
        total = None
        for off, resp in zip(offsets, responses):
            st, tot, ret, data = parse_read_response(resp.payload)
            if st != ProvStatus.PROV_OK:
                return st, b""
            if off == 0:
                total = tot  # authoritative length of the streamed blob
            if ret == 0:
                break  # past the end of the blob
            if off != len(blob):
                raise ProtocolError(
                    f"chunk gap at offset {off}: have {len(blob)} bytes")
            blob += data
        if total is None:
            total = len(blob)
        return ProvStatus.PROV_OK, bytes(blob[:total])

    def read_pubkey(self, slot_id: int, *, max_chunk: int = MAX_CHUNK, name="read-pubkey"):
        """Read a public key from a key slot; return ``(status, alg_id, data)``.

        PROV_READ_PUBKEY (0x61) uses a 7-byte response header carrying the
        ``alg_id``, so it has its own loop. The key lives in flash (no transient
        state), but the whole blob is still fetched in one session for symmetry
        with the other chunked reads. A first probe at ``offset == 0`` learns
        ``total_len`` (Packet formats).
        """
        opcode = Opcode.READ_PUBKEY
        probe = self.send_command(
            opcode, read_request(slot_id, 0, max_chunk), name=f"{name}_probe")
        status, alg_id, total, _ret, _data = parse_pubkey_response(probe.payload)
        if status != ProvStatus.PROV_OK:
            return status, 0, b""
        if total == 0:
            return status, alg_id, b""

        offsets = list(range(0, total, max_chunk))
        items = [(opcode, read_request(slot_id, off, min(max_chunk, total - off)))
                 for off in offsets]
        responses = self.run_commands(items, name=name)

        blob = bytearray()
        for off, resp in zip(offsets, responses):
            st, aid, tot, _ret, data = parse_pubkey_response(resp.payload)
            if st != ProvStatus.PROV_OK:
                return st, 0, b""
            if off != len(blob):
                raise ProtocolError(
                    f"chunk gap at offset {off}: have {len(blob)} bytes")
            blob += data
            alg_id = aid
            if tot != total:
                total = tot
        return ProvStatus.PROV_OK, alg_id, bytes(blob[:total])

    def prove_cert(self, slot_id: int, nonce: bytes, *,
                   max_chunk: int = MAX_CHUNK, name="prove-cert"):
        """Challenge PROV_PROVE_CERT and assemble the streamed signature.

        Returns ``(status, alg_id, cert_sha256, signature)``. The device signs
        ``nonce || SHA256(cert)`` on the ``offset == 0`` request and streams the
        signature back in chunks (Packet formats). Unlike a CSR the signature
        length is fixed per suite, so the probe total is exact. Because every
        ``offset == 0`` re-signs (ML-DSA-87 signing is randomized), all chunks
        must come from ONE session; only that first chunk carries the nonce.
        """
        opcode = Opcode.PROVE_CERT

        # Probe offset 0 to learn the signature length (and the suite). This
        # runs in its own session and re-signs, but the length is deterministic
        # per suite so it exactly sizes the read below.
        probe = self.send_command(
            opcode, read_request(slot_id, 0, max_chunk) + nonce, name=f"{name}_probe")
        status, alg_id, cert_sha256, total, _ret, _data = parse_prove_response(probe.payload)
        if status != ProvStatus.PROV_OK:
            return status, 0, b"", b""
        if total == 0:
            return status, alg_id, cert_sha256, b""

        # Stream the whole signature in ONE session so it comes from a single
        # signing. Only the offset==0 chunk carries the nonce and triggers it.
        offsets = list(range(0, total, max_chunk))
        items = []
        for off in offsets:
            req = read_request(slot_id, off, min(max_chunk, total - off))
            if off == 0:
                req += nonce
            items.append((opcode, req))
        responses = self.run_commands(items, name=name)

        sig = bytearray()
        for off, resp in zip(offsets, responses):
            st, aid, sha, tot, _ret, data = parse_prove_response(resp.payload)
            if st != ProvStatus.PROV_OK:
                return st, 0, b"", b""
            if off == 0:
                alg_id, cert_sha256, total = aid, sha, tot
            if off != len(sig):
                raise ProtocolError(
                    f"chunk gap at offset {off}: have {len(sig)} bytes")
            sig += data
        return ProvStatus.PROV_OK, alg_id, cert_sha256, bytes(sig[:total])


@dataclass
class Context:
    """Run-time context handed to each command's ``run``."""

    session: Session
    config: dict
    dry_run: bool = False
