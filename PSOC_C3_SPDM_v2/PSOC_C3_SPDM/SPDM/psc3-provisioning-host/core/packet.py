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
"""DFU / provisioning packet layer: opcodes, status codes, checksums, framing.

Centralizes every wire-level constant and the command/status catalogs from the
device-app Packet formats spec so the rest of the host never hard-codes protocol
details.

dfuh-cli performs the actual on-wire framing; the ``build_frame`` / ``DfuResponse``
helpers here are used for dry-run preview and for decoding captured response files.
All multi-byte fields are little-endian (LSB first).
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from enum import IntEnum

# Fixed DFU framing bytes (Packet formats: transport framing).
SOP = 0x01
EOP = 0x17

# Standard DFU session commands used to wrap each transaction (Packet formats: session model).
ENTER_DFU = 0x38
EXIT_DFU = 0x3B

# Default per-packet payload chunk for blob reads/writes. The device advertises
# its real value in PROV_GET_INFO (max_chunk); this is the safe fallback used by
# chunked commands when GET_INFO is not consulted (Packet formats: chunking).
MAX_CHUNK = 64

# Fixed field lengths shared by several commands (Packet formats).
UID_LEN = 8        # device unique-id reported by PROV_GET_INFO
SHA256_LEN = 32    # certificate digest carried by CERT_WRITE / PROVE_CERT
NONCE_LEN = 32     # host nonce for PROV_PROVE_CERT
# Raw r||s ECDSA signature lengths by curve (PROV_PROVE_CERT).
SIG_LEN_P256 = 64        # secp256r1: two 32-byte coordinates
SIG_LEN_P384 = 96        # secp384r1: two 48-byte coordinates
SIG_LEN_P521 = 132       # secp521r1: two 66-byte coordinates
SIG_LEN_MLDSA87 = 4627   # raw ML-DSA-87 signature length (PROV_PROVE_CERT)

# Confirm constants guarding the destructive commands (little-endian on the wire);
# must match the device build (shared/prov_nsc.h).
CONFIRM_ERASE = 0x45524153     # 'E','R','A','S'
CONFIRM_FINALIZE = 0x46524545  # 'F','R','E','E'

# slot_id sentinel meaning "all slots" for PROV_ERASE.
SLOT_ALL = 0xFF

# Subject-DN TLV field ids for the PROV_GET_CSR template
# (Packet formats: subject-DN template).
DN_CN = 0x01            # commonName
DN_O = 0x02            # organizationName
DN_OU = 0x03           # organizationalUnitName
DN_C = 0x04            # countryName
DN_SERIAL = 0x05       # serialNumber


class ChecksumType(IntEnum):
    """Packet checksum algorithm (Packet formats: checksum)."""

    SUMMATION = 0       # 2's-complement 16-bit summation (default)
    CRC16_CCITT = 1     # CRC-16/CCITT, poly 0x1021, init 0xFFFF


class Opcode(IntEnum):
    """Provisioning command opcodes (Packet formats: command reference).

    Grouped by functional bucket (high nibble):
      0x5X — general  (status, info, erase, finalize)
      0x6X — key      (gen-key, read-pubkey)
      0x7X — cert/CSR (get-csr, cert-write, read-cert, prove-cert)
    """

    # 0x5X — general
    GET_STATUS = 0x50
    GET_INFO   = 0x51
    ERASE      = 0x52
    FINALIZE   = 0x53

    # 0x6X — key
    GEN_KEY      = 0x60
    READ_PUBKEY  = 0x61

    # 0x7X — certificate & CSR
    GET_CSR    = 0x70
    CERT_WRITE = 0x71
    READ_CERT  = 0x72
    PROVE_CERT = 0x73


class ProvStatus(IntEnum):
    """Provisioning result codes; first byte of every response payload
    (Packet formats: result codes)."""

    PROV_OK                  = 0x00
    PROV_ERR_FROZEN          = 0x01  # assets frozen; mutating command rejected
    PROV_ERR_NOT_READY       = 0x02  # structural pre-freeze check failed
    PROV_ERR_NO_KEY          = 0x03  # key slot empty / invalid
    PROV_ERR_ALG_UNSUPPORTED = 0x04  # requested alg_id not supported
    PROV_ERR_NO_CERT         = 0x05  # cert slot empty / invalid
    PROV_ERR_CRYPTO          = 0x06  # key/CSR generation or signature failure
    PROV_ERR_NO_SLOT         = 0x07  # slot_id out of range (>= num_slots)
    PROV_ERR_SLOT_TYPE       = 0x08  # slot role mismatch
    PROV_ERR_HASH_MISMATCH   = 0x09  # streamed cert hash != declared hash
    PROV_ERR_LENGTH          = 0x0A  # bad length / offset / overflow
    PROV_ERR_FLASH           = 0x0B  # flash program / erase / ECC error
    PROV_ERR_CONFIRM         = 0x0C  # missing / incorrect confirm constant
    PROV_ERR_PARAM           = 0x0D  # malformed request payload
    PROV_ERR_INTERNAL        = 0xFF  # unspecified secure-side fault


class SlotType(IntEnum):
    """Per-slot role reported by PROV_GET_STATUS (Packet formats)."""

    FREE = 0x00  # slot is empty / not yet assigned
    KEY  = 0x01  # slot holds a key pair (or symmetric key)
    CERT = 0x02  # slot holds a device certificate


class AlgId(IntEnum):
    """Crypto-suite identifiers (Packet formats)."""

    ECDSA_P256 = 0x0001  # baseline (mandatory): ECC secp256r1 / SHA-256
    ECDSA_P384 = 0x0002  # ECC secp384r1 / SHA-384
    ECDSA_P521 = 0x0003  # ECC secp521r1 / SHA-512
    ML_DSA_44  = 0x0101  # future (PQC)
    ML_DSA_65  = 0x0102  # future (PQC)
    ML_DSA_87  = 0x0103  # PQC: ML-DSA-87 (FIPS 204)
    AES_256    = 0x0201  # future (symmetric; no public key / CSR / cert)


# CLI ``--alg`` spellings mapped to their alg_id (Packet formats).
ALG_NAMES = {
    "ecdsa-p256": AlgId.ECDSA_P256,
    "ecdsa-p384": AlgId.ECDSA_P384,
    "ecdsa-p521": AlgId.ECDSA_P521,
    "ml-dsa-44":  AlgId.ML_DSA_44,
    "ml-dsa-65":  AlgId.ML_DSA_65,
    "ml-dsa-87":  AlgId.ML_DSA_87,
    "aes-256":    AlgId.AES_256,
}


class ProtocolError(Exception):
    """Raised when a response cannot be parsed against the expected layout."""


def enum_name(enum_cls, value):
    """Return the enum member name for ``value`` or a readable UNKNOWN tag."""
    try:
        return enum_cls(value).name
    except ValueError:
        return f"UNKNOWN(0x{value:02X})"


def summation_2c(data) -> int:
    """2's-complement 16-bit summation checksum (Packet formats: checksum, type 0)."""
    return (~sum(data) + 1) & 0xFFFF


def crc16_ccitt(data) -> int:
    """CRC-16/CCITT, poly 0x1021, init 0xFFFF (Packet formats: checksum, type 1)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


def checksum(data, kind: ChecksumType) -> int:
    """Compute the configured checksum over ``data``."""
    if kind == ChecksumType.SUMMATION:
        return summation_2c(data)
    return crc16_ccitt(data)


def build_frame(cmd_id: int, data: bytes = b"", kind: ChecksumType = ChecksumType.SUMMATION) -> bytes:
    """Build a full DFU packet: SOP | Cmd | Len(LE) | data | Checksum(LE) | EOP.

    Used for dry-run preview; on real runs dfuh-cli does the framing.
    """
    body = bytes([SOP, cmd_id]) + len(data).to_bytes(2, "little") + bytes(data)
    cks = checksum(body, kind)
    return body + cks.to_bytes(2, "little") + bytes([EOP])


def u16(value: int) -> bytes:
    """Encode a 16-bit little-endian field."""
    return int(value).to_bytes(2, "little")


def u32(value: int) -> bytes:
    """Encode a 32-bit little-endian field."""
    return int(value).to_bytes(4, "little")


def read_request(slot_id: int, offset: int, length: int) -> bytes:
    """Build the common chunked-read request: slot_id(1) | offset(2) | len(2).

    Shared by PROV_GET_CSR (0x70) and PROV_READ_CERT (0x72); GET_CSR appends
    the subject template on the first (offset==0) chunk.
    """
    return bytes([slot_id & 0xFF]) + u16(offset) + u16(length)


def parse_read_response(payload: bytes):
    """Decode a chunked-read response: status(1) | total(2) | ret(2) | bytes(ret).

    Returns ``(prov_status, total_len, ret_len, data)``. On a non-OK status the
    device returns only the single status byte, so ``total``/``ret`` are 0 and
    ``data`` is empty (Packet formats: chunking).
    """
    if not payload:
        raise ProtocolError("empty read response")
    status = payload[0]
    if status != ProvStatus.PROV_OK:
        return status, 0, 0, b""
    if len(payload) < 5:
        raise ProtocolError(
            f"read response too short: {len(payload)} bytes (need >=5)")
    total = int.from_bytes(payload[1:3], "little")
    ret = int.from_bytes(payload[3:5], "little")
    data = payload[5:5 + ret]
    if len(data) < ret:
        raise ProtocolError(
            f"read response truncated: declared ret={ret}, got {len(data)} bytes")
    return status, total, ret, data


def parse_pubkey_response(payload: bytes):
    """Decode a PROV_READ_PUBKEY response.

    Layout: status(1) | alg_id(2) | total(2) | ret(2) | bytes(ret). Unlike the
    CSR/cert reads this carries a leading alg_id, so it needs its own parser.
    Returns ``(prov_status, alg_id, total_len, ret_len, data)``; on a non-OK
    status only the single status byte is present
    (Packet formats).
    """
    if not payload:
        raise ProtocolError("empty read-pubkey response")
    status = payload[0]
    if status != ProvStatus.PROV_OK:
        return status, 0, 0, 0, b""
    if len(payload) < 7:
        raise ProtocolError(
            f"read-pubkey response too short: {len(payload)} bytes (need >=7)")
    alg_id = int.from_bytes(payload[1:3], "little")
    total = int.from_bytes(payload[3:5], "little")
    ret = int.from_bytes(payload[5:7], "little")
    data = payload[7:7 + ret]
    if len(data) < ret:
        raise ProtocolError(
            f"read-pubkey response truncated: declared ret={ret}, got {len(data)} bytes")
    return status, alg_id, total, ret, data


def parse_prove_response(payload: bytes):
    """Decode a PROV_PROVE_CERT chunked response.

    Layout: status(1) | total(2) | alg_id(2) | cert_sha256(32) | ret(2) |
    bytes(ret). The signature (up to 4627 B for ML-DSA-87) is streamed in
    fixed-size windows; ``total`` is the full signature length and ``alg_id`` /
    ``cert_sha256`` are repeated on every chunk. Returns
    ``(prov_status, alg_id, cert_sha256, total_len, ret_len, data)``; on a
    non-OK status only the single status byte is present.
    """
    if not payload:
        raise ProtocolError("empty prove-cert response")
    status = payload[0]
    if status != ProvStatus.PROV_OK:
        return status, 0, b"", 0, 0, b""
    head = 7 + SHA256_LEN
    if len(payload) < head:
        raise ProtocolError(
            f"prove-cert response too short: {len(payload)} bytes (need >={head})")
    total = int.from_bytes(payload[1:3], "little")
    alg_id = int.from_bytes(payload[3:5], "little")
    cert_sha256 = payload[5:5 + SHA256_LEN]
    ret = int.from_bytes(payload[5 + SHA256_LEN:head], "little")
    data = payload[head:head + ret]
    if len(data) < ret:
        raise ProtocolError(
            f"prove-cert response truncated: declared ret={ret}, got {len(data)} bytes")
    return status, alg_id, cert_sha256, total, ret, data


def build_dn_template(fields) -> bytes:
    """Build the PROV_GET_CSR subject DN template (Packet formats: subject-DN template).

    ``fields`` is an iterable of ``(field_id, value)`` pairs; ``value`` is a str
    (UTF-8 encoded) or bytes. Each entry is emitted as ``field_id(1) | len(1) |
    value``. Empty/None values are skipped. Raises ``ValueError`` if a value
    exceeds 255 bytes (the 1-byte length field).
    """
    out = bytearray()
    for field_id, value in fields:
        if value is None or value == "":
            continue
        raw = value.encode("utf-8") if isinstance(value, str) else bytes(value)
        if len(raw) > 0xFF:
            raise ValueError(f"DN field 0x{field_id:02X} too long: {len(raw)} bytes (max 255)")
        out += bytes([field_id & 0xFF, len(raw)]) + raw
    return bytes(out)


@dataclass
class DfuCmd:
    """A single entry in a DFU transaction (one ``commands[]`` element)."""

    cmd_id: int
    data: bytes = b""
    capture: bool = False  # capture this command's response payload to an outFile


# Matches a dfuh-cli CLI log line like:
#   " Read packet: 01 00 06 00 00 00 00 00 01 00 f8 ff 17 "
_READ_PACKET_RE = re.compile(rb"Read packet:\s*([0-9A-Fa-f ]+)")


def _extract_read_packet(data: bytes):
    """Extract the response frame from a dfuh-cli ``outCli`` text log.

    Returns the decoded ``Read packet:`` bytes, or ``None`` if ``data`` is not
    such a text log (e.g. it is already a raw frame/payload).
    """
    match = _READ_PACKET_RE.search(data)
    if not match:
        return None
    hex_bytes = match.group(1).split()
    try:
        return bytes(int(b, 16) for b in hex_bytes)
    except ValueError:
        return None



@dataclass
class DfuResponse:
    """Decoded device response.

    ``payload`` is the provisioning response data whose first byte is
    ``prov_cmd_status`` (Packet formats: result codes). ``dfu_status`` is the transport-level
    DFU Status Code when the captured file is a full frame.
    """

    payload: bytes
    dfu_status: int | None = None
    framed: bool = False
    checksum_ok: bool | None = None

    @property
    def prov_status(self):
        return self.payload[0] if self.payload else None

    @classmethod
    def from_capture(cls, data, kind: ChecksumType = ChecksumType.SUMMATION) -> "DfuResponse":
        """Decode bytes captured from a dfuh-cli ``outFile``.

        With ``outCli="true"`` the DFU Host Tool writes a human-readable text
        log containing ``Write packet:`` / ``Read packet:`` hex lines rather
        than raw bytes; the device response frame is extracted from the
        ``Read packet:`` line. The capture may also be a raw DFU frame
        (SOP..EOP) or just the response data bytes, depending on tool/version.
        All three are handled here so the rest of the host only ever sees the
        provisioning payload.
        """
        data = bytes(data or b"")

        frame = _extract_read_packet(data)
        if frame is not None:
            data = frame

        if len(data) >= 7 and data[0] == SOP and data[-1] == EOP:
            dfu_status = data[1]
            length = int.from_bytes(data[2:4], "little")
            payload = data[4:4 + length]
            cks_field = int.from_bytes(data[4 + length:6 + length], "little")
            calc = checksum(data[0:4 + length], kind)
            return cls(payload, dfu_status, True, cks_field == calc)
        return cls(data, None, False, None)
