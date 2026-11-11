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
"""PROV_ERASE (0x52) -- erase one slot or all slots (Packet formats).

Request : slot_id(1) (0xFF = all slots), confirm(4 LE) = CONFIRM_ERASE.
Response: prov_cmd_status(1).
Destructive: requires an explicit ``--yes`` so it cannot run by accident, and is
rejected by the device after PROV_FINALIZE (the freeze record is immutable).
"""
from __future__ import annotations

from core.packet import (
    CONFIRM_ERASE,
    SLOT_ALL,
    Opcode,
    ProvStatus,
    enum_name,
    u32,
)
from core.registry import ProvCommand, register


@register
class EraseCommand(ProvCommand):
    name = "erase"
    opcode = Opcode.ERASE
    help = "Erase a slot or all slots (PROV_ERASE, 0x52)"
    description = (
        "Erase one slot (--slot N) or every slot (--all). Destructive: the\n"
        "explicit --yes flag is required, and the device rejects the command once\n"
        "provisioning has been finalized.")
    examples = (
        "provisioner erase --slot 1 --yes",
        "provisioner erase --all --yes",
    )

    @classmethod
    def add_arguments(cls, parser):
        group = parser.add_mutually_exclusive_group(required=True)
        group.add_argument("--slot", type=int, help="Slot index to erase")
        group.add_argument("--all", action="store_true", help="Erase all slots")
        parser.add_argument("--yes", action="store_true",
                            help="Confirm this destructive operation (required)")

    def build_request(self, args) -> bytes:
        slot = SLOT_ALL if args.all else (args.slot & 0xFF)
        return bytes([slot]) + u32(CONFIRM_ERASE)

    def parse_response(self, payload: bytes) -> dict:
        status = payload[0] if payload else ProvStatus.PROV_ERR_INTERNAL
        return {"prov_cmd_status": status}

    def render(self, result: dict, args, ctx) -> None:
        target = "all slots" if args.all else f"slot {args.slot}"
        print(f"erase {target}")
        print(f"prov_cmd_status : {enum_name(ProvStatus, result['prov_cmd_status'])}")

    def run(self, ctx, args) -> int:
        if not args.yes and not ctx.dry_run:
            target = "all slots" if args.all else f"slot {args.slot}"
            print(f"refusing to erase {target} without --yes (destructive).")
            return 2
        return super().run(ctx, args)
