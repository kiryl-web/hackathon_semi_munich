#!/usr/bin/env python3
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
"""Provisioning Host CLI entry point (see docs/design_and_implementation.md).

Loads configuration, discovers command modules, builds the argparse
sub-command surface from the registry, and dispatches the selected command.
Global flags (e.g. --dry-run, --hwid) override config.yaml values.
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

import yaml

from core import registry
from core.dfu_backend import DfuBackend
from core.session import Context, Session

HERE = Path(__file__).resolve().parent
DEFAULT_CONFIG = HERE / "config.yaml"
LOG = logging.getLogger("provisioner")

DEFAULTS = {
    "device": {"hwid": None, "i2c_address": 0x0C, "i2c_speed": 100},
    "protocol": {"checksum_type": 0, "product_id": "01020304",
                 "file_version": "0x1", "max_packet_payload": 128,
                 "timeout_ms": 30000},
    "paths": {"work_dir": str(HERE / "_work")},
    "tools": {"dfuh_cli": "dfuh-cli"},
}


def _as_int(value) -> int:
    """Parse an int from an int or a string in any base (e.g. '0x0C')."""
    return value if isinstance(value, int) else int(str(value), 0)


def _merge(base: dict, override: dict) -> dict:
    """Deep-merge ``override`` onto ``base`` (override wins)."""
    out = dict(base)
    for key, val in (override or {}).items():
        if isinstance(val, dict) and isinstance(out.get(key), dict):
            out[key] = _merge(out[key], val)
        else:
            out[key] = val
    return out


def load_config(path) -> dict:
    cfg = {section: dict(values) for section, values in DEFAULTS.items()}
    cfg_path = Path(path)
    if cfg_path.exists():
        loaded = yaml.safe_load(cfg_path.read_text(encoding="utf-8")) or {}
        cfg = _merge(cfg, loaded)
    else:
        LOG.warning("Config %s not found; using built-in defaults", cfg_path)
    return cfg


def _add_global_args(parser: argparse.ArgumentParser) -> None:
    """Add flags shared by the top level and every sub-command.

    Defaults are SUPPRESS so a flag absent from one position never overwrites
    the value supplied in the other; real defaults are resolved in ``main``.
    The flags live in their own "global options" group so that, in a
    ``<command> -h`` listing, they stay visually separate from the
    command-specific arguments.
    """
    group = parser.add_argument_group("global options")
    group.add_argument("--config", default=argparse.SUPPRESS, help="Path to config.yaml")
    group.add_argument("--hwid", default=argparse.SUPPRESS,
                       help="KitProg3/MiniProg hardware id override")
    group.add_argument("--i2c-address", default=argparse.SUPPRESS,
                       help="I2C address override (e.g. 0x0C)")
    group.add_argument("--i2c-speed", type=int, default=argparse.SUPPRESS,
                       help="I2C speed in kHz override")
    group.add_argument("--checksum", type=int, choices=(0, 1), default=argparse.SUPPRESS,
                       help="Packet checksum type override (0=sum, 1=CRC-16)")
    group.add_argument("--product-id", default=argparse.SUPPRESS,
                       help="Enter-DFU Product Id override (hex)")
    group.add_argument("--dfuh-cli", default=argparse.SUPPRESS,
                       help="Path or command name of the DFU Host Tool CLI override")
    group.add_argument("--timeout-ms", type=int, default=argparse.SUPPRESS,
                       help="Per-command dfuh-cli response timeout in ms "
                            "(raise for slow ML-DSA-87 keygen/sign)")
    group.add_argument("--dry-run", action="store_true", default=argparse.SUPPRESS,
                       help="Build the .mtbdfu and show frames without running dfuh-cli")
    group.add_argument("--debug", action="store_true", default=argparse.SUPPRESS,
                       help="Verbose logging")


TOP_EPILOG = (
    "Run 'provisioner <command> -h' for usage and arguments of a specific command.\n"
    "\n"
    "common examples:\n"
    "  provisioner status\n"
    "  provisioner info --json\n"
    "  provisioner gen-key --slot 0 --alg ecdsa-p256\n"
    "  provisioner get-csr --slot 0 --cn 'PSC3 Device' -o build/dev.csr\n"
    "  provisioner put-cert --slot 1 --key-slot 0 -i build/dev.crt\n"
    "  provisioner prove-cert --slot 1 -i build/dev.crt\n"
)


def _command_epilog(cmd_cls) -> str | None:
    """Render a command's ``examples`` into a help epilog, or ``None``."""
    examples = getattr(cmd_cls, "examples", ()) or ()
    if not examples:
        return None
    return "\n".join(["examples:"] + [f"  {line}" for line in examples])


def build_parser() -> argparse.ArgumentParser:
    common = argparse.ArgumentParser(add_help=False)
    _add_global_args(common)

    parser = argparse.ArgumentParser(
        prog="provisioner",
        description="Secure provisioning host tool for PSOC Control C3 MCU.",
        parents=[common],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=TOP_EPILOG)

    subparsers = parser.add_subparsers(dest="command", metavar="<command>")
    for name in sorted(registry.COMMANDS):
        cmd_cls = registry.COMMANDS[name]
        description = getattr(cmd_cls, "description", "") or getattr(cmd_cls, "help", name)
        sub = subparsers.add_parser(
            name, parents=[common],
            help=getattr(cmd_cls, "help", name),
            description=description,
            epilog=_command_epilog(cmd_cls),
            formatter_class=argparse.RawDescriptionHelpFormatter)
        cmd_cls.add_arguments(sub)
        sub.set_defaults(_command_cls=cmd_cls)
    return parser


def main(argv=None) -> int:
    registry.discover()
    parser = build_parser()
    args = parser.parse_args(argv)

    debug = getattr(args, "debug", False)
    dry_run = getattr(args, "dry_run", False)
    logging.basicConfig(
        level=logging.DEBUG if debug else logging.INFO,
        format="%(levelname)s %(name)s: %(message)s")

    cmd_cls = getattr(args, "_command_cls", None)
    if cmd_cls is None:
        parser.print_help()
        return 2

    cfg = load_config(getattr(args, "config", DEFAULT_CONFIG))
    dev, proto, paths, tools = cfg["device"], cfg["protocol"], cfg["paths"], cfg["tools"]

    hwid_arg = getattr(args, "hwid", None)
    hwid = hwid_arg if hwid_arg is not None else dev.get("hwid")
    i2c_address = _as_int(getattr(args, "i2c_address", None) or dev["i2c_address"])
    i2c_speed = getattr(args, "i2c_speed", None) or int(dev["i2c_speed"])
    checksum_type = getattr(args, "checksum", None)
    if checksum_type is None:
        checksum_type = int(proto["checksum_type"])
    product_id = getattr(args, "product_id", None) or str(proto["product_id"])
    dfuh_cli = getattr(args, "dfuh_cli", None) or tools["dfuh_cli"]
    timeout_ms = getattr(args, "timeout_ms", None) or int(proto.get("timeout_ms", 0))

    backend = DfuBackend(
        dfuh_cli=dfuh_cli,
        hwid=hwid,
        i2c_address=i2c_address,
        i2c_speed=i2c_speed,
        checksum_type=checksum_type,
        product_id_hex=product_id,
        file_version=str(proto["file_version"]),
        work_dir=paths["work_dir"],
        timeout_ms=timeout_ms,
        dry_run=dry_run,
        debug=debug,
    )
    ctx = Context(session=Session(backend), config=cfg, dry_run=dry_run)

    command = cmd_cls()
    try:
        return command.run(ctx, args)
    except Exception as exc:  # surfaced to the operator; full trace under --debug
        LOG.error("%s failed: %s", command.name, exc)
        if debug:
            raise
        return 1


if __name__ == "__main__":
    sys.exit(main())
