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
"""PROV_GET_STATUS (0x50) -- query provisioning state (Packet formats).

Response payload: prov_cmd_status(1), provisioned(1: 0=NOT_PROVISIONED/1=PROVISIONED),
num_slots(1), pas_version(2 LE), then slot_type(1) per slot (0=free/1=key/2=cert).
All values are derived live from the freeze record and per-slot validity predicates.
"""
from __future__ import annotations

import json

from core.packet import Opcode, ProtocolError, ProvStatus, SlotType, enum_name
from core.registry import ProvCommand, register


@register
class StatusCommand(ProvCommand):
    name = "status"
    opcode = Opcode.GET_STATUS
    help = "Query provisioning state (PROV_GET_STATUS, 0x50)"
    description = (
        "Query the device provisioning state: whether it is provisioned, how many\n"
        "slots it exposes, the PAS version and each slot's role (free/key/cert).\n"
        "A safe, read-only command -- a good first check that the link works.")
    examples = (
        "provisioner status",
        "provisioner status --json",
        "provisioner --hwid KitProg3-XXXX --i2c-address 0x08 status",
    )

    @classmethod
    def add_arguments(cls, parser):
        parser.add_argument("--json", action="store_true",
                            help="Emit the decoded status as JSON")

    def parse_response(self, payload: bytes) -> dict:
        # Minimum: prov_cmd_status(1) + provisioned(1) + num_slots(1) + pas_version(2)
        if len(payload) < 5:
            raise ProtocolError(
                f"PROV_GET_STATUS response too short: {len(payload)} bytes (need ≥5)")
        num_slots = payload[2]
        expected = 5 + num_slots
        if len(payload) < expected:
            raise ProtocolError(
                f"PROV_GET_STATUS response truncated: got {len(payload)} bytes, "
                f"need {expected} ({num_slots} slot(s))")
        slot_types = [payload[5 + i] for i in range(num_slots)]
        return {
            "prov_cmd_status": payload[0],
            "provisioned": payload[1],
            "num_slots": num_slots,
            "pas_version": int.from_bytes(payload[3:5], "little"),
            "slot_types": slot_types,
        }

    def render(self, result: dict, args, ctx) -> None:
        prov_label = "PROVISIONED" if result["provisioned"] else "NOT_PROVISIONED"
        if getattr(args, "json", False):
            decoded = dict(result)
            decoded["prov_cmd_status_name"] = enum_name(ProvStatus, result["prov_cmd_status"])
            decoded["provisioned_name"] = prov_label
            decoded["slot_type_names"] = [
                enum_name(SlotType, t) for t in result["slot_types"]]
            print(json.dumps(decoded, indent=2))
            return
        print(f"prov_cmd_status : {enum_name(ProvStatus, result['prov_cmd_status'])}")
        print(f"provisioned     : {prov_label}")
        print(f"num_slots       : {result['num_slots']}")
        print(f"pas_version     : 0x{result['pas_version']:04X}")
        for i, t in enumerate(result["slot_types"]):
            print(f"slot[{i}]         : {enum_name(SlotType, t)}")
