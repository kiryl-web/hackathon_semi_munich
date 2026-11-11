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
"""PROV_READ_CERT (0x72) -- download a stored certificate (Packet formats).

Request (per chunk) : slot_id(1), offset(2), len(2).
Response (per chunk): prov_cmd_status(1), total(2), ret(2), bytes(ret).

The certificate lives in flash, so the read is plain chunked streaming (no
transient state); the assembled bytes are written verbatim to ``--out``.
"""
from __future__ import annotations

import os

from core.packet import (
    MAX_CHUNK,
    Opcode,
    ProvStatus,
    enum_name,
    read_request,
)
from core.registry import ProvCommand, register


@register
class GetCertCommand(ProvCommand):
    name = "get-cert"
    opcode = Opcode.READ_CERT
    help = "Download the certificate stored in a slot (PROV_READ_CERT, 0x72)"
    description = (
        "Download whatever is stored in --slot (a single certificate, a raw DER\n"
        "chain, or an SPDM cert_chain structure) and write the raw bytes verbatim to\n"
        "--out (default: <work_dir>/cert_slot<N>.bin). The bytes are returned exactly\n"
        "as stored; no parsing is performed. Read-only.")
    examples = (
        "provisioner get-cert --slot 1",
        "provisioner get-cert --slot 1 -o build/dev.crt",
    )

    @classmethod
    def add_arguments(cls, parser):
        parser.add_argument("--slot", type=int, required=True,
                            help="Certificate slot to read")
        parser.add_argument("-o", "--out",
                            help="Output path (default: <work_dir>/cert_slot<slot>.bin)")

    def _out_path(self, args, ctx) -> str:
        if args.out:
            return args.out
        work_dir = ctx.config["paths"]["work_dir"]
        return os.path.join(work_dir, f"cert_slot{args.slot}.bin")

    def run(self, ctx, args) -> int:
        if ctx.dry_run:
            ctx.session.send_command(
                self.opcode, read_request(args.slot, 0, MAX_CHUNK), name=self.name)
            print(f"[dry-run] {self.name}: first chunk request built (slot={args.slot}).")
            return 0

        status, data = ctx.session.read_blob(self.opcode, args.slot, name=self.name)
        print(f"prov_cmd_status : {enum_name(ProvStatus, status)}")
        if status != ProvStatus.PROV_OK:
            return int(status)

        out_path = self._out_path(args, ctx)
        os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
        with open(out_path, "wb") as f:
            f.write(data)
        print(f"cert_len        : {len(data)} bytes")
        print(f"written         : {out_path}")
        return 0
