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
"""PROV_FINALIZE (0x53) -- freeze provisioning (Packet formats).

Request : confirm(4 LE) = CONFIRM_FINALIZE.
Response: prov_cmd_status(1) and, on success, provisioned(1).
One-way and irreversible: after this the freeze record is locked, slots become
read-only and PROV_ERASE / PROV_GEN_KEY / PROV_CERT_WRITE are rejected. Requires
an explicit ``--yes``.
"""
from __future__ import annotations

from core.packet import (
    CONFIRM_FINALIZE,
    Opcode,
    ProvStatus,
    enum_name,
    u32,
)
from core.registry import ProvCommand, register


@register
class FinalizeCommand(ProvCommand):
    name = "finalize"
    opcode = Opcode.FINALIZE
    help = "Freeze provisioning, making slots read-only (PROV_FINALIZE, 0x53)"
    description = (
        "Freeze provisioning. This is one-way and irreversible: afterwards the\n"
        "slots become read-only and gen-key / erase / put-cert are rejected.\n"
        "The explicit --yes flag is required.")
    examples = (
        "provisioner finalize --yes",
    )

    @classmethod
    def add_arguments(cls, parser):
        parser.add_argument("--yes", action="store_true",
                            help="Confirm this irreversible operation (required)")

    def build_request(self, args) -> bytes:
        return u32(CONFIRM_FINALIZE)

    def parse_response(self, payload: bytes) -> dict:
        status = payload[0] if payload else ProvStatus.PROV_ERR_INTERNAL
        result = {"prov_cmd_status": status}
        if status == ProvStatus.PROV_OK and len(payload) >= 2:
            result["provisioned"] = payload[1]
        return result

    def render(self, result: dict, args, ctx) -> None:
        print(f"prov_cmd_status : {enum_name(ProvStatus, result['prov_cmd_status'])}")
        if "provisioned" in result:
            label = "PROVISIONED" if result["provisioned"] else "NOT_PROVISIONED"
            print(f"provisioned     : {label}")

    def run(self, ctx, args) -> int:
        if not args.yes and not ctx.dry_run:
            print("refusing to finalize without --yes (irreversible).")
            return 2
        return super().run(ctx, args)
