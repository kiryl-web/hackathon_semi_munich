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
"""Quick end-to-end provisioning self-test.

Runs the full happy-path provisioning flow against a connected device so you
don't have to type each command by hand:

    status -> info -> gen-key -> get-csr -> (local CA sign) -> put-cert
           -> get-cert (read-back) -> prove-cert

The CSR -> certificate signing step is normally performed by the OEM CA and is
out of scope for the tool. For a self-contained test this script signs the CSR
locally with the throwaway test CA in ``test/ca_ec256.key`` + ``test/ca_ec256.crt``.

With ``--freeze`` it additionally finalizes the device (IRREVERSIBLE) and then
runs a negative test: it re-attempts ``gen-key`` and asserts the device rejects
it with ``PROV_ERR_FROZEN``.

Usage
-----
    # Repeatable happy path (does NOT lock the device):
    python test/e2e_test.py --hwid KitProg3-XXXX --i2c-address 0x08

    # Full flow including the irreversible freeze + negative reprovision test:
    python test/e2e_test.py --hwid KitProg3-XXXX --i2c-address 0x08 --freeze

Any flag this script does not recognize (``--hwid``, ``--i2c-address``,
``--i2c-speed``, ``--checksum``, ``--config``, ``--dfuh-cli``, ``--debug`` ...)
is forwarded verbatim as a global option to ``provisioner.py``.

Requires the ``cryptography`` package (already a host dependency for prove-cert).
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HOST_DIR = Path(__file__).resolve().parent.parent
TEST_DIR = Path(__file__).resolve().parent
if str(TEST_DIR) not in sys.path:
    sys.path.insert(0, str(TEST_DIR))
PROVISIONER = HOST_DIR / "provisioner.py"
OUT_DIR = TEST_DIR / "_work" / "e2e"

CA_KEY = TEST_DIR / "ca_ec256.key"
CA_CRT = TEST_DIR / "ca_ec256.crt"

# Expected device status name for the post-freeze negative test.
FROZEN_STATUS = "PROV_ERR_FROZEN"


def _fail(msg: str) -> "None":
    print(f"\n[FAIL] {msg}")
    raise SystemExit(1)


def run(globals_: list[str], sub_args: list[str], *, capture: bool = False):
    """Invoke provisioner.py: <globals> <subcommand-args>. Returns CompletedProcess."""
    cmd = [sys.executable, str(PROVISIONER), *globals_, *sub_args]
    print(f"\n>> {' '.join(cmd[2:])}")
    res = subprocess.run(cmd, cwd=str(HOST_DIR), text=True,
                         capture_output=capture)
    if capture:
        if res.stdout:
            sys.stdout.write(res.stdout)
        if res.stderr:
            sys.stderr.write(res.stderr)
    return res


def ca_sign_csr(csr_der: Path, out_crt: Path) -> None:
    """Sign a DER PKCS#10 CSR with the local test CA, writing a DER X.509 cert.

    Delegates to ``sign_csr.ca_sign_csr`` (the shared CA stand-in) so both EC and
    ML-DSA device keys are handled -- sign_csr hand-builds the ML-DSA leaf cert.
    The issued certificate carries the device public key from the CSR, so
    prove-cert's key<->cert binding check will pass.
    """
    try:
        import cryptography  # noqa: F401
        from sign_csr import ca_sign_csr as _sign
    except ImportError:
        _fail("The 'cryptography' package is required. Run: "
              "python -m pip install -r requirements.txt")

    for path in (CA_KEY, CA_CRT):
        if not path.exists():
            _fail(f"Test CA file not found: {path}")

    cert_der = _sign(csr_der.read_bytes(), CA_KEY.read_bytes(),
                     CA_CRT.read_bytes(), out_pem=False)
    out_crt.write_bytes(cert_der)
    print(f"   CA-signed certificate written: {out_crt}")


def expect_ok(res, step: str) -> None:
    if res.returncode != 0:
        _fail(f"{step} returned exit code {res.returncode}")
    print(f"   [ok] {step}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="End-to-end provisioning self-test.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument("--key-slot", type=int, default=0, help="Key slot (default 0)")
    ap.add_argument("--cert-slot", type=int, default=1, help="Certificate slot (default 1)")
    ap.add_argument("--alg", default="ecdsa-p256", help="Key algorithm (default ecdsa-p256)")
    ap.add_argument("--cn", default="E2E-Test-Device", help="CSR commonName")
    ap.add_argument("--freeze", action="store_true",
                    help="Finalize (IRREVERSIBLE) and run the post-freeze negative test")
    # Everything else (e.g. --hwid, --i2c-address) is forwarded to provisioner.py.
    known, globals_ = ap.parse_known_args(argv)

    dry_run = "--dry-run" in globals_

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    csr_path = OUT_DIR / "dev.csr"
    crt_path = OUT_DIR / "dev.crt"
    rb_path = OUT_DIR / "readback.crt"

    ks, cs = str(known.key_slot), str(known.cert_slot)

    print("=" * 70)
    print(" Provisioning end-to-end self-test")
    print(f"   key slot={ks}  cert slot={cs}  alg={known.alg}  freeze={known.freeze}")
    print(f"   globals forwarded to provisioner.py: {globals_ or '(none)'}")
    print("=" * 70)

    # --- Link / state probes -------------------------------------------------
    expect_ok(run(globals_, ["status"]), "status")
    expect_ok(run(globals_, ["info", "--json"]), "info")

    # --- Stage 1: key + CSR --------------------------------------------------
    expect_ok(run(globals_, ["gen-key", "--slot", ks, "--alg", known.alg]),
              "gen-key")
    expect_ok(run(globals_, ["get-csr", "--slot", ks, "--cn", known.cn,
                             "-o", str(csr_path)]),
              "get-csr")

    if dry_run:
        print("\n" + "=" * 70)
        print(" DRY-RUN: command wiring validated up to get-csr.")
        print(" The CA-sign, put-cert, get-cert, prove-cert and freeze steps")
        print(" need real device responses, so they are skipped in dry-run.")
        print("=" * 70)
        return 0

    # --- Out-of-band CA step (local test CA) ---------------------------------
    print("\n>> (local test CA) sign CSR -> certificate")
    ca_sign_csr(csr_path, crt_path)

    # --- Stage 2: cert + proof ----------------------------------------------
    expect_ok(run(globals_, ["put-cert", "--slot", cs, "--key-slot", ks,
                             "-i", str(crt_path)]),
              "put-cert")
    expect_ok(run(globals_, ["get-cert", "--slot", cs, "-o", str(rb_path)]),
              "get-cert (read-back)")
    expect_ok(run(globals_, ["prove-cert", "--slot", cs, "-i", str(rb_path)]),
              "prove-cert")

    if not known.freeze:
        print("\n" + "=" * 70)
        print(" PASS: happy-path flow completed (device NOT frozen).")
        print(" Re-run with --freeze to lock the device and test reprovisioning.")
        print("=" * 70)
        return 0

    # --- Stage 2b: freeze ----------------------------------------------------
    expect_ok(run(globals_, ["finalize", "--yes"]), "finalize (freeze)")

    # --- Stage 3: negative test — reprovision must be rejected ---------------
    print("\n>> negative test: gen-key after freeze must be rejected")
    neg = run(globals_, ["gen-key", "--slot", ks, "--alg", known.alg], capture=True)
    out = (neg.stdout or "") + (neg.stderr or "")
    if neg.returncode == 0:
        _fail("gen-key SUCCEEDED after freeze — device is not enforcing the lock!")
    if FROZEN_STATUS not in out:
        _fail(f"gen-key was rejected (exit {neg.returncode}) but not with "
              f"{FROZEN_STATUS}; output:\n{out}")
    print(f"   [ok] reprovision correctly rejected with {FROZEN_STATUS}")

    print("\n" + "=" * 70)
    print(" PASS: full flow + freeze + negative reprovision test succeeded.")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
