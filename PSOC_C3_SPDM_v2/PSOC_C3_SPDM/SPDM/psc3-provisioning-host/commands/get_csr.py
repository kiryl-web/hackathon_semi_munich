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
"""PROV_GET_CSR (0x70) -- build & stream a PKCS#10 CSR (Packet formats).

Request (per chunk): slot_id(1), offset(2), len(2), and on the first chunk
(offset==0) the subject-DN template (TLVs, §6.2).
Response (per chunk): prov_cmd_status(1), total(2), ret(2), bytes(ret).

The device assembles the DER CSR into a transient secure-RAM buffer on the
offset==0 request and streams it out; the buffer is discarded at Exit DFU, so
the whole CSR is fetched within a single DFU session (see Session.read_blob).
The assembled DER is written verbatim to ``--out``.
"""
from __future__ import annotations

import os

from core.packet import (
    DN_C,
    DN_CN,
    DN_O,
    DN_OU,
    DN_SERIAL,
    MAX_CHUNK,
    Opcode,
    ProvStatus,
    build_dn_template,
    enum_name,
    read_request,
)
from core.registry import ProvCommand, register


@register
class GetCsrCommand(ProvCommand):
    name = "get-csr"
    opcode = Opcode.GET_CSR
    help = "Build and download a CSR for a key slot (PROV_GET_CSR, 0x70)"
    description = (
        "Ask the device to build a PKCS#10 certificate signing request for the key\n"
        "in --slot, optionally filling in subject fields (--cn/--org/--ou/\n"
        "--country/--serial), and stream the DER back to --out (default:\n"
        "<work_dir>/csr_slot<N>.der). Hand the CSR to your CA to issue a cert.")
    examples = (
        "provisioner get-csr --slot 0",
        "provisioner get-csr --slot 0 --cn 'PSC3 Device' --org Acme --country US -o build/dev.csr",
    )

    @classmethod
    def add_arguments(cls, parser):
        parser.add_argument("--slot", type=int, required=True,
                            help="Key slot to certify")
        parser.add_argument("-o", "--out",
                            help="Output DER CSR path (default: <work_dir>/csr_slot<slot>.der)")
        parser.add_argument("--cn", help="subject commonName")
        parser.add_argument("--org", help="subject organizationName")
        parser.add_argument("--ou", help="subject organizationalUnitName")
        parser.add_argument("--country", help="subject countryName (2-letter)")
        parser.add_argument("--serial", help="subject serialNumber")

    def _template(self, args) -> bytes:
        return build_dn_template([
            (DN_CN, args.cn),
            (DN_O, args.org),
            (DN_OU, args.ou),
            (DN_C, args.country),
            (DN_SERIAL, args.serial),
        ])

    def _out_path(self, args, ctx) -> str:
        if args.out:
            return args.out
        work_dir = ctx.config["paths"]["work_dir"]
        return os.path.join(work_dir, f"csr_slot{args.slot}.der")

    def run(self, ctx, args) -> int:
        template = self._template(args)

        if ctx.dry_run:
            ctx.session.send_command(
                self.opcode,
                read_request(args.slot, 0, MAX_CHUNK) + template,
                name=self.name)
            print(f"[dry-run] {self.name}: first chunk request built "
                  f"(slot={args.slot}, template={len(template)} B).")
            return 0

        status, der = ctx.session.read_blob(
            self.opcode, args.slot, template=template, name=self.name)
        print(f"prov_cmd_status : {enum_name(ProvStatus, status)}")
        if status != ProvStatus.PROV_OK:
            return int(status)

        out_path = self._out_path(args, ctx)
        os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
        with open(out_path, "wb") as f:
            f.write(der)
        print(f"csr_len         : {len(der)} bytes")
        print(f"written         : {out_path}")
        return 0
