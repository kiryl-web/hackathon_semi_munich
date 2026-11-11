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
"""PROV_READ_PUBKEY (0x61) -- read a slot's public key (Packet formats).

Request (per chunk) : slot_id(1), offset(2), len(2).
Response (per chunk): prov_cmd_status(1), alg_id(2), total(2), ret(2), bytes(ret).

Diagnostic read of the public half of a key pair (the private key never leaves
the device). The key is read from flash, so this is plain chunked streaming; the
assembled bytes are printed as hex and, with ``--out``, written verbatim.
"""
from __future__ import annotations

import os

from core.packet import (
    MAX_CHUNK,
    AlgId,
    Opcode,
    ProvStatus,
    enum_name,
    read_request,
)
from core.registry import ProvCommand, register


@register
class ReadPubkeyCommand(ProvCommand):
    name = "read-pubkey"
    opcode = Opcode.READ_PUBKEY
    help = "Read the public key stored in a key slot (PROV_READ_PUBKEY, 0x61)"
    description = (
        "Diagnostic read of the public half of a key pair. Prints the algorithm,\n"
        "length and the raw public key as hex; with --out the bytes are also saved\n"
        "to a file. The private key is never exposed. Read-only.")
    examples = (
        "provisioner read-pubkey --slot 0",
        "provisioner read-pubkey --slot 0 -o build/slot0.pub",
    )

    @classmethod
    def add_arguments(cls, parser):
        parser.add_argument("--slot", type=int, required=True,
                            help="Key slot to read")
        parser.add_argument("-o", "--out",
                            help="Optional output path for the raw public key bytes")

    def run(self, ctx, args) -> int:
        if ctx.dry_run:
            ctx.session.send_command(
                self.opcode, read_request(args.slot, 0, MAX_CHUNK), name=self.name)
            print(f"[dry-run] {self.name}: first chunk request built (slot={args.slot}).")
            return 0

        status, alg_id, data = ctx.session.read_pubkey(args.slot, name=self.name)
        print(f"prov_cmd_status : {enum_name(ProvStatus, status)}")
        if status != ProvStatus.PROV_OK:
            return int(status)

        print(f"alg             : {enum_name(AlgId, alg_id)} (0x{alg_id:04X})")
        print(f"pub_len         : {len(data)} bytes")
        print(f"public_key      : {data.hex()}")

        if args.out:
            os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
            with open(args.out, "wb") as f:
                f.write(data)
            print(f"written         : {args.out}")
        return 0
