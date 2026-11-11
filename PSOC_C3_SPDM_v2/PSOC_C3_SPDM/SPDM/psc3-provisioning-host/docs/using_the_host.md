[Click here](../README.md) to view the README.

## Using the host tool

This guide covers installation, configuration, the global options, and the full command reference. For the architecture see [Design and implementation](design_and_implementation.md); to extend the CLI see [Adding commands](adding_commands.md).


## Dependencies

- **Python** 3.8 or later.
- **`PyYAML`** — configuration parsing (always required).
- **`cryptography`** — used only by `prove-cert` for X.509 parsing and signature verification (ECDSA-P256/P384/P521 and ML-DSA-87); imported lazily so other commands run without it. ML-DSA-87 verification requires `cryptography >= 46`.
- **DFU Host Tool** (`dfuh-cli`) — installed with ModusToolbox&trade;; the host invokes it to reach the device over I2C.

Install the Python dependencies:

```
python -m pip install -r requirements.txt
```


## Configuration

Defaults live in [config.yaml](../config.yaml). Any value can be overridden per-invocation by a global flag (see below). The file has four sections:

Section / key            | Meaning
:----------------------- | :------
`device.hwid`            | Debug-bridge identifier (e.g. `KitProg3-XXXXXXXX`). `null` lets `dfuh-cli` auto-detect.
`device.i2c_address`     | Device DFU I2C address (valid range `0x08`–`0x78`).
`device.i2c_speed`       | I2C clock in kHz (e.g. `100`, `400`).
`protocol.checksum_type` | `0` = 2's-complement summation (default), `1` = CRC-16/CCITT. **Must match the device's packet-checksum build setting.**
`protocol.product_id`    | Enter-DFU Product Id (hex string).
`protocol.file_version`  | `.mtbdfu` file version field.
`protocol.timeout_ms`    | Per-command `dfuh-cli` response timeout in milliseconds (default `30000`). Raise it for the slow on-device ML-DSA-87 key generation and signing, which can exceed the tool's short default. `0` omits the per-command `timeoutMS` (tool default).
`protocol.max_packet_payload` | Maximum payload bytes per packet; bounds the chunk size for blob transfers.
`paths.work_dir`         | Where generated `.mtbdfu` requests and captured responses are written (default `./_work`).
`tools.dfuh_cli`         | Path or command name of the DFU Host Tool CLI.

A different config file can be selected with `--config <path>`.


## Global options

These apply to every command and override the matching `config.yaml` value:

Flag                  | Overrides / effect
:-------------------- | :-----------------
`--config <path>`     | Use an alternate configuration file.
`--hwid <id>`         | `device.hwid`.
`--i2c-address <hex>` | `device.i2c_address`.
`--i2c-speed <kHz>`   | `device.i2c_speed`.
`--checksum {0,1}`    | `protocol.checksum_type`.
`--product-id <hex>`  | `protocol.product_id`.
`--timeout-ms <ms>`   | `protocol.timeout_ms` (per-command `dfuh-cli` response timeout; raise for slow ML-DSA-87 keygen/sign).
`--dfuh-cli <path>`   | `tools.dfuh_cli`.
`--dry-run`           | Build and print the `.mtbdfu` request and decoded frames; do not call `dfuh-cli`.
`--debug`             | Verbose logging (also passes `--debug` to `dfuh-cli`).

Usage:

```
python provisioner.py [global options] <command> [command options]
```

Run `python provisioner.py -h` for the command list, or `python provisioner.py <command> -h` for a command's arguments and examples.


## Command reference

**Table 1. CLI commands**

Command       | Opcode | Description | Key arguments
:------------ | :----- | :---------- | :------------
`status`      | `0x50` | Query provisioning state. | `--json`
`info`        | `0x51` | Read device UID, slot geometry and supported algorithms. | `--json`
`gen-key`     | `0x60` | Generate a key pair in a slot. | `--slot <n>` (required), `--alg <name>` (default `ecdsa-p256`; `ecdsa-p384` / `ecdsa-p521` / `ml-dsa-87` require a device built with that suite)
`read-pubkey` | `0x61` | Read the public key stored in a key slot. | `--slot <n>` (required), `-o/--out <file>`
`get-csr`     | `0x70` | Build and download a CSR for a key slot. | `--slot <n>` (required), `-o/--out <file>`, `--cn`, `--org`, `--ou`, `--country`, `--serial`
`put-cert`    | `0x71` | Stage and commit a certificate (or SPDM cert_chain) into a slot. | `--slot <n>` (required), `--key-slot <n>` (required), `-i/--in <file>` (required, repeat with `--spdm-chain`), `--spdm-chain`, `--hash-alg {sha256,sha384,sha512}` (SPDM RootHash, default `sha256`), `--save-blob <file>`
`get-cert`    | `0x72` | Download whatever is stored in a slot (single cert or SPDM cert_chain), verbatim. | `--slot <n>` (required), `-o/--out <file>`
`prove-cert`  | `0x73` | Challenge the device to prove key possession. | `--slot <n>` (required), `-i/--in <file>` (required), `--nonce <hex>`
`finalize`    | `0x53` | Freeze provisioning, making slots read-only. | `--yes` (required)
`erase`       | `0x52` | Erase a slot or all slots. | `--slot <n>` \| `--all` (one required), `--yes` (required)

Notes:

- The subject-DN flags on `get-csr` (`--cn`, `--org`, `--ou`, `--country`, `--serial`) are all optional; when none is given the device defaults the commonName to the hex UID.
- `finalize` and `erase` are irreversible (or destructive) and require `--yes` to proceed.
- `put-cert` stores bytes verbatim. A single `-i` stores one certificate (DER or PEM, PEM converted to DER). With `--spdm-chain`, repeat `-i` in **root-first** order to build an SPDM `cert_chain` structure (`Length | Reserved | RootHash | certs[root..leaf]`); the RootHash is the `--hash-alg` digest of the root cert (default `sha256`, also `sha384`/`sha512`) and **must match the SPDM negotiated BaseHashAlgo** — the width (32/48/64 B) follows the algorithm. `--hash-alg` only applies with `--spdm-chain`; multiple `-i` require `--spdm-chain`. Use `--save-blob <file>` to write the exact stored bytes for later `prove-cert`.
- `prove-cert` takes `-i` the exact stored blob — a single certificate (DER or PEM) or an SPDM `cert_chain` `.bin` (from `put-cert --save-blob` or `get-cert`). It checks the blob's SHA-256 against the device digest, then verifies the nonce-challenge signature against the **leaf** certificate's public key using `cryptography`. The leaf is located by inferring the SPDM RootHash width (SHA-256/384/512), so chains built with any `--hash-alg` verify without extra flags. Both **ECDSA** (P-256/P-384/P-521 raw `r‖s`, re-encoded to DER) and **ML-DSA-87** (raw 4627-byte pure FIPS 204 signature; needs `cryptography >= 46`) are supported. A bare concatenated DER chain (no SPDM header) is rejected.


## Typical flow

A generic provisioning sequence using a key slot (slot 0) and a certificate slot (slot 1):

```
python provisioner.py info --json
python provisioner.py gen-key  --slot 0
python provisioner.py get-csr  --slot 0 --cn "Device-001" -o assets/dev.csr
#   ... CSR signed by your CA out-of-band, yielding dev.crt ...
python provisioner.py put-cert --slot 1 --key-slot 0 -i assets/dev.crt
python provisioner.py get-cert --slot 1 -o assets/readback.crt
python provisioner.py prove-cert --slot 1 -i assets/readback.crt
python provisioner.py finalize --yes
```

To store an **SPDM cert_chain** (root-first) instead of a single leaf cert, replace the `put-cert`/`prove-cert` steps with:

```
python provisioner.py put-cert --slot 1 --key-slot 0 --spdm-chain \
    -i assets/ca.crt -i assets/dev.crt --save-blob assets/slot1.bin
python provisioner.py prove-cert --slot 1 -i assets/slot1.bin
```

Add `--hash-alg sha384` (or `sha512`) to `put-cert` when the SPDM link negotiates a `BaseHashAlgo` other than SHA-256; `prove-cert` needs no matching flag, as it infers the RootHash width from the blob.

For a fully **post-quantum** chain, provision an `ml-dsa-87` device key and store a chain rooted at an ML-DSA-87 CA with a SHA-384 RootHash — `put-cert --spdm-chain --hash-alg sha384 -i <ml-dsa-87 root>.crt -i <ml-dsa-87 leaf>.crt` (SHA-384 is the `BaseHashAlgo` the SPDM responder negotiates today). The `test/` helpers ship a ready ML-DSA-87 root (`test/ca_mldsa87.{key,crt}`, generated by `test/gen_ca.py`); `test/sign_csr.py` signs the device CSR with it.

Use `--dry-run` on any command to inspect the generated request and on-wire frames without a board attached.
