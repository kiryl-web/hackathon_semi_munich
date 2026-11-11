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
"""Complete SPDM provisioning flow: key -> CSR -> CA-sign -> SPDM cert_chain.

Drives the full provisioning of a device so its certificate slot ends up holding
an SPDM ``cert_chain`` structure (Length | Reserved | RootHash | certs[root..leaf])
that an SPDM Responder can serve verbatim:

    status -> info -> gen-key -> get-csr -> (sign_csr.py CA sign)
           -> put-cert --spdm-chain -> get-cert (read-back) -> prove-cert

The CSR -> leaf-certificate signing step is delegated to ``sign_csr.py`` (run as
a subprocess), which stands in for the OEM CA. Every input is explicit -- the
script assumes no default CA, slots, subject, paths, validity or algorithm.

The SPDM chain is assembled ROOT-first: the ``--chain-cert`` file(s) you pass
(root, then any intermediates) precede the freshly signed leaf. ``put-cert``
computes ``RootHash = --hash-alg(first chain cert)`` (SHA-256 by default) and
prepends the SPDM header.

Usage
-----
    python test/spdm_provision.py \\
        --key-slot 0 --cert-slot 1 --alg ecdsa-p256 \\
        --cn "PSC3 Device 001" \\
        --ca-key test/ca_ec256.key --ca-crt test/ca_ec256.crt \\
        --chain-cert test/ca_ec256.crt \\
        --days 3650 --out-dir test/_work/spdm \\
        --hwid KitProg3-XXXX --i2c-address 0x08

Any flag this script does not recognize (``--hwid``, ``--i2c-address``,
``--i2c-speed``, ``--checksum``, ``--config``, ``--dfuh-cli``, ``--debug``,
``--dry-run`` ...) is forwarded verbatim as a global option to ``provisioner.py``.

Requires the ``cryptography`` package (already a host dependency for prove-cert).
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HOST_DIR = Path(__file__).resolve().parent.parent
TEST_DIR = Path(__file__).resolve().parent
PROVISIONER = HOST_DIR / "provisioner.py"
SIGN_CSR = TEST_DIR / "sign_csr.py"

# Expected device status name for the optional post-freeze negative test.
FROZEN_STATUS = "PROV_ERR_FROZEN"


def _fail(msg: str) -> "None":
    print(f"\n[FAIL] {msg}")
    raise SystemExit(1)


def run(globals_: list, sub_args: list, *, capture: bool = False):
    """Invoke provisioner.py: <globals> <subcommand-args>."""
    cmd = [sys.executable, str(PROVISIONER), *globals_, *sub_args]
    print(f"\n>> {' '.join(cmd[2:])}")
    res = subprocess.run(cmd, cwd=str(HOST_DIR), text=True, capture_output=capture)
    if capture:
        if res.stdout:
            sys.stdout.write(res.stdout)
        if res.stderr:
            sys.stderr.write(res.stderr)
    return res


def run_sign_csr(csr_path: Path, leaf_path: Path, ca_key: Path, ca_crt: Path,
                 days: int) -> None:
    """Delegate CSR -> leaf certificate signing to sign_csr.py (the CA stand-in)."""
    cmd = [sys.executable, str(SIGN_CSR), str(csr_path),
           "-o", str(leaf_path), "--out-form", "der",
           "--ca-key", str(ca_key), "--ca-crt", str(ca_crt),
           "--days", str(days)]
    print(f"\n>> {' '.join(cmd[1:])}")
    res = subprocess.run(cmd, text=True)
    if res.returncode != 0:
        _fail(f"sign_csr.py returned exit code {res.returncode}")
    if not leaf_path.exists():
        _fail(f"sign_csr.py did not produce the leaf certificate: {leaf_path}")
    print(f"   [ok] CA-signed leaf certificate: {leaf_path}")


def expect_ok(res, step: str) -> None:
    if res.returncode != 0:
        _fail(f"{step} returned exit code {res.returncode}")
    print(f"   [ok] {step}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Complete SPDM provisioning flow (key -> CSR -> CA-sign -> "
                    "SPDM cert_chain).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)

    # --- Slots / key ---------------------------------------------------------
    ap.add_argument("--key-slot", type=int, required=True,
                    help="Key slot to generate the device key pair in")
    ap.add_argument("--cert-slot", type=int, required=True,
                    help="Certificate slot to store the SPDM cert_chain in")
    ap.add_argument("--alg", required=True,
                    help="Key algorithm for gen-key (e.g. ecdsa-p256)")

    # --- CSR subject ---------------------------------------------------------
    ap.add_argument("--cn", required=True, help="CSR commonName")
    ap.add_argument("--org", help="CSR organization (optional)")
    ap.add_argument("--ou", help="CSR organizationalUnit (optional)")
    ap.add_argument("--country", help="CSR country (optional)")
    ap.add_argument("--serial", help="CSR serialNumber (optional)")

    # --- CA (delegated to sign_csr.py) ---------------------------------------
    ap.add_argument("--ca-key", type=Path, required=True,
                    help="CA private key PEM used by sign_csr.py to sign the CSR")
    ap.add_argument("--ca-crt", type=Path, required=True,
                    help="CA certificate PEM (issuer of the leaf)")
    ap.add_argument("--days", type=int, required=True,
                    help="Leaf certificate validity period in days")

    # --- SPDM chain composition ---------------------------------------------
    ap.add_argument("--chain-cert", type=Path, action="append", required=True,
                    metavar="FILE",
                    help="Chain certificate ABOVE the leaf, in ROOT-first order "
                         "(repeat for intermediates). The signed leaf is appended "
                         "last. DER or PEM.")
    ap.add_argument("--hash-alg", choices=("sha256", "sha384", "sha512"),
                    default="sha256",
                    help="Hash algorithm for the SPDM cert_chain RootHash "
                         "(default: sha256); must match the SPDM negotiated "
                         "BaseHashAlgo. Forwarded to put-cert.")

    # --- Output / control ----------------------------------------------------
    ap.add_argument("--out-dir", type=Path, required=True,
                    help="Directory for generated artifacts (CSR, leaf, blob, "
                         "read-back)")
    ap.add_argument("--freeze", action="store_true",
                    help="Finalize (IRREVERSIBLE) after provisioning and assert a "
                         "post-freeze gen-key is rejected")

    # Everything else (e.g. --hwid, --i2c-address, --dry-run) -> provisioner.py.
    known, globals_ = ap.parse_known_args(argv)

    dry_run = "--dry-run" in globals_

    # Validate input files up front (assume nothing exists).
    for path in (SIGN_CSR, known.ca_key, known.ca_crt, *known.chain_cert):
        if not path.exists():
            _fail(f"Required file not found: {path}")

    out_dir = known.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    csr_path = out_dir / "dev.csr"
    leaf_path = out_dir / "leaf.crt"
    blob_path = out_dir / "spdm_cert_chain.bin"
    rb_path = out_dir / "readback.bin"

    ks, cs = str(known.key_slot), str(known.cert_slot)
    chain_inputs = [str(p) for p in known.chain_cert]

    print("=" * 70)
    print(" SPDM provisioning (cert chain)")
    print(f"   key slot={ks}  cert slot={cs}  alg={known.alg}")
    print(f"   chain (root-first, leaf appended): {chain_inputs} + <leaf>")
    print(f"   RootHash alg={known.hash_alg}")
    print(f"   CA key={known.ca_key}  CA crt={known.ca_crt}  days={known.days}")
    print(f"   out dir={out_dir}  freeze={known.freeze}")
    print(f"   globals forwarded to provisioner.py: {globals_ or '(none)'}")
    print("=" * 70)

    # --- Link / state probes -------------------------------------------------
    expect_ok(run(globals_, ["status"]), "status")
    expect_ok(run(globals_, ["info", "--json"]), "info")

    # --- Stage 1: key + CSR --------------------------------------------------
    expect_ok(run(globals_, ["gen-key", "--slot", ks, "--alg", known.alg]),
              "gen-key")

    csr_args = ["get-csr", "--slot", ks, "--cn", known.cn, "-o", str(csr_path)]
    for flag, value in (("--org", known.org), ("--ou", known.ou),
                        ("--country", known.country), ("--serial", known.serial)):
        if value:
            csr_args += [flag, value]
    expect_ok(run(globals_, csr_args), "get-csr")

    if dry_run:
        print("\n" + "=" * 70)
        print(" DRY-RUN: command wiring validated up to get-csr.")
        print(" CA-sign, put-cert, get-cert, prove-cert and freeze need real")
        print(" device responses, so they are skipped in dry-run.")
        print("=" * 70)
        return 0

    # --- Out-of-band CA step (delegated to sign_csr.py) ----------------------
    run_sign_csr(csr_path, leaf_path, known.ca_key, known.ca_crt, known.days)

    # --- Stage 2: SPDM cert_chain + proof ------------------------------------
    put_args = ["put-cert", "--slot", cs, "--key-slot", ks, "--spdm-chain",
                "--hash-alg", known.hash_alg]
    for cert in chain_inputs:
        put_args += ["-i", cert]
    put_args += ["-i", str(leaf_path), "--save-blob", str(blob_path)]
    expect_ok(run(globals_, put_args), "put-cert (--spdm-chain)")

    expect_ok(run(globals_, ["get-cert", "--slot", cs, "-o", str(rb_path)]),
              "get-cert (read-back)")

    # The device digest is over the whole stored blob, so prove-cert is fed the
    # exact SPDM cert_chain bytes we stored (--save-blob output).
    expect_ok(run(globals_, ["prove-cert", "--slot", cs, "-i", str(blob_path)]),
              "prove-cert")

    if not known.freeze:
        print("\n" + "=" * 70)
        print(" PASS: SPDM cert_chain provisioned (device NOT frozen).")
        print(f" Stored SPDM blob: {blob_path}")
        print(" Re-run with --freeze to lock the device.")
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
    print(" PASS: SPDM cert_chain provisioning + freeze + negative test succeeded.")
    print(f" Stored SPDM blob: {blob_path}")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
