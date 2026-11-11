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
"""Command base class and registry with auto-discovery (see docs/adding_commands.md).

Every provisioning command subclasses :class:`ProvCommand`, declares its CLI
``name`` and ``opcode``, and is registered with :func:`register`.
:func:`discover` imports all modules under ``commands/`` so their decorators run
and populate :data:`COMMANDS`. Adding a future command is therefore just a new
module under ``commands/`` -- no changes to the core or CLI are required.
"""
from __future__ import annotations

import importlib
import logging
import pkgutil
from pathlib import Path

from core.packet import ProvStatus
from core.session import Context

LOG = logging.getLogger("provisioner.registry")

COMMANDS: dict = {}


def register(cls):
    """Class decorator that registers a command by its ``name``."""
    if not getattr(cls, "name", None):
        raise ValueError(f"{cls.__name__} must define a non-empty 'name'")
    COMMANDS[cls.name] = cls
    return cls


def discover():
    """Import every module under ``commands/`` to populate the registry."""
    import commands as commands_pkg

    pkg_path = Path(commands_pkg.__file__).resolve().parent
    for mod in pkgutil.iter_modules([str(pkg_path)]):
        if mod.name.startswith("_"):
            continue
        importlib.import_module(f"commands.{mod.name}")
    return COMMANDS


class ProvCommand:
    """Base class for a provisioning sub-command.

    Subclasses set ``name``/``opcode``/``help`` and override the hooks they
    need. The default :meth:`run` implements the common single-command
    transaction: build request -> send -> parse -> render -> exit code.

    For richer ``<command> -h`` output a subclass may also set:
      * ``description`` -- a fuller paragraph shown above the argument list
        (falls back to ``help`` when empty).
      * ``examples`` -- a sequence of example command lines rendered in the
        help epilog.
    """

    name: str = ""
    opcode: int = 0
    help: str = ""
    description: str = ""
    examples: tuple = ()

    @classmethod
    def add_arguments(cls, parser):
        """Override to add command-specific CLI arguments."""

    def build_request(self, args) -> bytes:
        """Override to serialize the request payload (bytes after the opcode)."""
        return b""

    def parse_response(self, payload: bytes) -> dict:
        """Override to decode the response payload (payload[0] = prov_cmd_status)."""
        raise NotImplementedError

    def render(self, result: dict, args, ctx: Context) -> None:
        """Override to print a human-readable result."""

    def run(self, ctx: Context, args) -> int:
        """Default orchestration for a single-command transaction."""
        request = self.build_request(args)
        resp = ctx.session.send_command(self.opcode, request, name=self.name)

        if ctx.dry_run:
            print(f"[dry-run] {self.name}: request built; no device response to parse.")
            return 0
        if not resp.payload:
            print(f"{self.name}: empty response from device.")
            return int(ProvStatus.PROV_ERR_INTERNAL)
        if resp.checksum_ok is False:
            LOG.warning("Response checksum mismatch for %s", self.name)

        result = self.parse_response(resp.payload)
        self.render(result, args, ctx)
        status = result.get("prov_cmd_status", resp.prov_status)
        return 0 if status == ProvStatus.PROV_OK else int(status)
