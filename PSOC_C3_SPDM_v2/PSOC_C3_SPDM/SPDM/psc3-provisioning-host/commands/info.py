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
"""PROV_GET_INFO (0x51) -- device identity, slot geometry & capabilities
(Packet formats).

Response payload (after prov_cmd_status):
  uid(UID_LEN), caps(2), num_slots(1), slot_size(2) * num_slots,
  n_algs(1), { alg_id(2), pub_len(2), sig_len(2) } * n_algs, max_chunk(2).
The per-slot size array lets the host decide which slots to use for keys vs
certificates; the alg list sizes buffers without hard-coding any key/sig length.
"""
from __future__ import annotations

import json

from core.packet import UID_LEN, AlgId, Opcode, ProtocolError, ProvStatus, enum_name
from core.registry import ProvCommand, register


@register
class InfoCommand(ProvCommand):
    name = "info"
    opcode = Opcode.GET_INFO
    help = "Read device UID, slot geometry and supported algorithms (PROV_GET_INFO, 0x51)"
    description = (
        "Read static device information: the 8-byte unique id, capability flags,\n"
        "per-slot sizes, the list of supported algorithms (with public-key and\n"
        "signature lengths) and the maximum transfer chunk. Read-only.")
    examples = (
        "provisioner info",
        "provisioner info --json",
    )

    @classmethod
    def add_arguments(cls, parser):
        parser.add_argument("--json", action="store_true",
                            help="Emit the decoded info as JSON")

    def parse_response(self, payload: bytes) -> dict:
        status = payload[0] if payload else ProvStatus.PROV_ERR_INTERNAL
        if status != ProvStatus.PROV_OK:
            return {"prov_cmd_status": status}

        need = 1 + UID_LEN + 2 + 1
        if len(payload) < need:
            raise ProtocolError(
                f"PROV_GET_INFO response too short: {len(payload)} bytes (need >={need})")

        pos = 1
        uid = payload[pos:pos + UID_LEN]
        pos += UID_LEN
        caps = int.from_bytes(payload[pos:pos + 2], "little")
        pos += 2
        num_slots = payload[pos]
        pos += 1

        if len(payload) < pos + 2 * num_slots + 1:
            raise ProtocolError("PROV_GET_INFO response truncated in slot_size array")
        slot_sizes = []
        for _ in range(num_slots):
            slot_sizes.append(int.from_bytes(payload[pos:pos + 2], "little"))
            pos += 2

        n_algs = payload[pos]
        pos += 1
        if len(payload) < pos + 6 * n_algs + 2:
            raise ProtocolError("PROV_GET_INFO response truncated in alg array")
        algs = []
        for _ in range(n_algs):
            alg_id = int.from_bytes(payload[pos:pos + 2], "little")
            pub_len = int.from_bytes(payload[pos + 2:pos + 4], "little")
            sig_len = int.from_bytes(payload[pos + 4:pos + 6], "little")
            pos += 6
            algs.append({"alg_id": alg_id, "pub_len": pub_len, "sig_len": sig_len})

        max_chunk = int.from_bytes(payload[pos:pos + 2], "little")
        pos += 2

        return {
            "prov_cmd_status": status,
            "uid": uid.hex(),
            "caps": caps,
            "num_slots": num_slots,
            "slot_sizes": slot_sizes,
            "algs": algs,
            "max_chunk": max_chunk,
        }

    def render(self, result: dict, args, ctx) -> None:
        if result.get("prov_cmd_status") != ProvStatus.PROV_OK:
            print(f"prov_cmd_status : {enum_name(ProvStatus, result['prov_cmd_status'])}")
            return
        if getattr(args, "json", False):
            decoded = dict(result)
            decoded["prov_cmd_status_name"] = enum_name(ProvStatus, result["prov_cmd_status"])
            for alg in decoded["algs"]:
                alg["alg_id_name"] = enum_name(AlgId, alg["alg_id"])
            print(json.dumps(decoded, indent=2))
            return
        print(f"prov_cmd_status : {enum_name(ProvStatus, result['prov_cmd_status'])}")
        print(f"uid             : {result['uid']}")
        print(f"caps            : 0x{result['caps']:04X}")
        print(f"num_slots       : {result['num_slots']}")
        for i, size in enumerate(result["slot_sizes"]):
            print(f"slot[{i}].size    : {size} bytes")
        print(f"max_chunk       : {result['max_chunk']} bytes")
        for alg in result["algs"]:
            print(f"alg             : {enum_name(AlgId, alg['alg_id'])} "
                  f"(0x{alg['alg_id']:04X}) pub_len={alg['pub_len']} sig_len={alg['sig_len']}")
