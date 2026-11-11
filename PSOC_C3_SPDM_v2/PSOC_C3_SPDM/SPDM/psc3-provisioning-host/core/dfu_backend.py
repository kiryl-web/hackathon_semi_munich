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
"""Backend that turns provisioning transactions into dfuh-cli invocations.

This is the only module that knows about the external DFU Host Tool. It builds
a ``.mtbdfu`` (JSON) request describing the DFU command sequence, runs
``dfuh-cli --custom-command``, and returns the bytes captured from each
response ``outFile`` (see docs/design_and_implementation.md). A ``dry_run`` mode builds and previews the
request (including decoded on-wire frames) without invoking the tool, so the
end-to-end flow can be developed without hardware attached.
"""
from __future__ import annotations

import json
import logging
import subprocess
from pathlib import Path

from core.packet import ChecksumType, DfuCmd, build_frame

LOG = logging.getLogger("provisioner.backend")


def _hex_spaced(data: bytes) -> str:
    """Format bytes as space-separated upper-case hex (mtbdfu ``dataBytes``)."""
    return " ".join(f"{b:02X}" for b in data)


class BackendError(Exception):
    """Raised when the DFU Host Tool cannot be run or reports failure."""


class DfuBackend:
    """Builds .mtbdfu requests and drives dfuh-cli over I2C."""

    def __init__(self, *, dfuh_cli, hwid, i2c_address, i2c_speed, checksum_type,
                 product_id_hex, file_version, work_dir, timeout_ms=0,
                 dry_run=False, debug=False):
        self.dfuh_cli = dfuh_cli
        self.hwid = hwid
        self.i2c_address = int(i2c_address)
        self.i2c_speed = int(i2c_speed)
        self.checksum_type = ChecksumType(int(checksum_type))
        self.product_id_hex = str(product_id_hex).replace(" ", "")
        self.product_id = bytes.fromhex(self.product_id_hex)
        self.file_version = file_version
        self.timeout_ms = int(timeout_ms)
        self.work_dir = Path(work_dir)
        self.dry_run = dry_run
        self.debug = debug
        self.work_dir.mkdir(parents=True, exist_ok=True)

    def _build_mtbdfu(self, cmds):
        """Return (mtbdfu_doc, capture_file_paths) for a list of DfuCmd."""
        entries = []
        cap_files = []
        for i, cmd in enumerate(cmds):
            entry = {"cmdId": f"0x{cmd.cmd_id:02X}"}
            if cmd.data:
                # dfuh-cli requires dataLength (byte count) whenever dataBytes
                # is supplied, otherwise it rejects the request with
                # "'dataLength' should be defined when additional data is passed."
                entry["dataLength"] = f"0x{len(cmd.data):02X}"
                entry["dataBytes"] = _hex_spaced(cmd.data)
            # dfuh-cli has no timeout CLI flag; the per-command timeoutMS in the
            # .mtbdfu is how long it waits for a response. Slow on-device work
            # (ML-DSA-87 keygen/sign) can exceed the tool's short default.
            if self.timeout_ms > 0:
                entry["timeoutMS"] = f"0x{self.timeout_ms:X}"
            if cmd.capture:
                fname = f"resp_{i:02d}.bin"
                entry["outFile"] = fname
                entry["outCli"] = "true"
                cap_files.append(self.work_dir / fname)
            entries.append(entry)
        doc = {
            "APPInfo": {
                "File Version": self.file_version,
                "Packet Checksum Type": f"0x{int(self.checksum_type):X}",
                "Product Id": self.product_id_hex.upper(),
            },
            "commands": entries,
        }
        return doc, cap_files

    def run_transaction(self, cmds, name="txn"):
        """Run one DFU transaction; return captured payloads in command order."""
        doc, cap_files = self._build_mtbdfu(cmds)
        mtbdfu_path = self.work_dir / f"{name}.mtbdfu"
        mtbdfu_path.write_text(json.dumps(doc, indent=2), encoding="utf-8")
        LOG.debug("Wrote %s", mtbdfu_path)

        if self.dry_run:
            self._preview(doc, cmds, mtbdfu_path)
            return [b"" for _ in cap_files]

        for path in cap_files:
            if path.exists():
                path.unlink()

        argv = [self.dfuh_cli, "--custom-command", mtbdfu_path.name]
        if self.hwid:
            argv += ["--hwid", str(self.hwid)]
        argv += ["--i2c-address", f"0x{self.i2c_address:02X}",
                 "--i2c-speed", str(self.i2c_speed)]
        if self.debug:
            argv += ["--debug"]

        LOG.info("Running: %s", " ".join(argv))
        try:
            proc = subprocess.run(argv, cwd=self.work_dir,
                                  capture_output=True, text=True)
        except FileNotFoundError as exc:
            raise BackendError(
                f"dfuh-cli not found ('{self.dfuh_cli}'). Install the DFU Host "
                f"Tool, set tools.dfuh_cli in config.yaml, or use --dry-run."
            ) from exc

        if proc.stdout:
            LOG.debug("dfuh-cli stdout:\n%s", proc.stdout)
        if proc.stderr:
            LOG.debug("dfuh-cli stderr:\n%s", proc.stderr)
        if proc.returncode != 0:
            raise BackendError(
                f"dfuh-cli exited with code {proc.returncode}: "
                f"{proc.stderr.strip() or proc.stdout.strip()}"
            )
        return [p.read_bytes() if p.exists() else b"" for p in cap_files]

    def _preview(self, doc, cmds, mtbdfu_path):
        """Print the generated request and decoded frames for --dry-run."""
        print(f"[dry-run] would invoke dfuh-cli on {mtbdfu_path}")
        print(json.dumps(doc, indent=2))
        print("[dry-run] on-wire frames (SOP..EOP):")
        for cmd in cmds:
            frame = build_frame(cmd.cmd_id, cmd.data, self.checksum_type)
            print(f"  0x{cmd.cmd_id:02X}: {_hex_spaced(frame)}")
