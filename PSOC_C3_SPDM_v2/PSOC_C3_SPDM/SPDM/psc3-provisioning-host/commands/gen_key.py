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
"""PROV_GEN_KEY (0x60) -- generate a key pair in a slot (Packet formats).

Request : slot_id(1), alg_id(2 LE).
Response: prov_cmd_status(1) and, on success, slot_id(1), alg_id(2), pub_len(2).
The private key never leaves the device; only the public-key length is returned
here (read the key with the diagnostic read-pubkey path or via the CSR/cert).
"""
from __future__ import annotations

from core.packet import (
    ALG_NAMES,
    AlgId,
    Opcode,
    ProtocolError,
    ProvStatus,
    enum_name,
    u16,
)
from core.registry import ProvCommand, register


@register
class GenKeyCommand(ProvCommand):
    name = "gen-key"
    opcode = Opcode.GEN_KEY
    help = "Generate a key pair in a slot (PROV_GEN_KEY, 0x60)"
    description = (
        "Generate a fresh key pair inside the given slot. The private key never\n"
        "leaves the device; only the slot, algorithm and public-key length are\n"
        "returned. Rejected after 'finalize'.")
    examples = (
        "provisioner gen-key --slot 0",
        "provisioner gen-key --slot 0 --alg ecdsa-p256",
    )

    @classmethod
    def add_arguments(cls, parser):
        parser.add_argument("--slot", type=int, required=True,
                            help="Target slot index for the new key pair")
        parser.add_argument("--alg", choices=sorted(ALG_NAMES), default="ecdsa-p256",
                            help="Crypto suite (default: ecdsa-p256)")

    def build_request(self, args) -> bytes:
        alg_id = ALG_NAMES[args.alg]
        return bytes([args.slot & 0xFF]) + u16(alg_id)

    def parse_response(self, payload: bytes) -> dict:
        status = payload[0] if payload else ProvStatus.PROV_ERR_INTERNAL
        if status != ProvStatus.PROV_OK:
            return {"prov_cmd_status": status}
        if len(payload) < 6:
            raise ProtocolError(
                f"PROV_GEN_KEY response too short: {len(payload)} bytes (need >=6)")
        return {
            "prov_cmd_status": status,
            "slot_id": payload[1],
            "alg_id": int.from_bytes(payload[2:4], "little"),
            "pub_len": int.from_bytes(payload[4:6], "little"),
        }

    def render(self, result: dict, args, ctx) -> None:
        print(f"prov_cmd_status : {enum_name(ProvStatus, result['prov_cmd_status'])}")
        if result.get("prov_cmd_status") != ProvStatus.PROV_OK:
            return
        print(f"slot_id         : {result['slot_id']}")
        print(f"alg             : {enum_name(AlgId, result['alg_id'])} "
              f"(0x{result['alg_id']:04X})")
        print(f"pub_len         : {result['pub_len']} bytes")
