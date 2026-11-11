[Click here](../README.md) to view the README.

## Packet formats

This document specifies the on-wire protocol: the transport framing, the session model, the chunking scheme, and the meaning and purpose of every field in every command. For the higher-level design (modules, capabilities, command summary), see [Design and implementation](design_and_implementation.md).

### Conventions

- **Endianness.** Every multi-byte integer field is **little-endian** (least-significant byte first), including the framing `length`/`checksum` and all payload fields.
- **Result code first.** Every response payload begins with a 1-byte result code (`prov_cmd_status`, see [Result codes](#result-codes)).
- **Error short-circuit.** **If the result code is not `PROV_OK` (`0x00`), the device returns only that single status byte.** All the success-only fields documented below are then absent. A host must check the first byte before parsing any further field.
- **Notation.** `field(n)` is an `n`-byte field. `field(n) × k` is an array of `k` such fields. Fields shown in *[brackets]* are present only on success (`PROV_OK`).
- **No fixed lengths in the host.** Buffer sizes (public-key length, signature length, per-slot capacity, maximum chunk) are all discovered from `PROV_GET_INFO` rather than hard-coded, so the same host works across crypto suites and PAS layouts.

### Transport framing

Each provisioning command is one DFU packet. The byte layout is:

```
SOP(1)=0x01 | cmd_id(1) | length(2) | payload(length) | checksum(2) | EOP(1)=0x17
```

Field      | Size | Purpose
:--------- | :--- | :------
`SOP`      | 1    | Start-of-packet marker, always `0x01`. Lets the receiver find a frame boundary.
`cmd_id`   | 1    | For a provisioning command this is the **opcode** (see the command reference below). For session control it is a standard DFU command (Enter `0x38` / Exit `0x3B`).
`length`   | 2    | Number of bytes in `payload` (little-endian). `0` for commands that carry no payload.
`payload`  | *length* | The command **request body** (host→device) or **response body** (device→host) described per command below.
`checksum` | 2    | Checksum over `SOP..payload` (little-endian). Detects transport corruption. See [Checksum](#checksum).
`EOP`      | 1    | End-of-packet marker, always `0x17`.

The **device response** uses the same frame, but its second byte is the transport-level **DFU status code** (the response provisioning payload — beginning with `prov_cmd_status` — is the framed `payload`):

```
SOP(1)=0x01 | dfu_status(1) | length(2) | payload(length) | checksum(2) | EOP(1)=0x17
```

`dfu_status` reflects transport/framing success (for example, a bad checksum or unknown command), and is independent of `prov_cmd_status`, which reflects the provisioning outcome inside a correctly transported packet. A robust host checks `dfu_status` first, then `prov_cmd_status`.

### Checksum

Two checksum algorithms are defined; both produce a 16-bit little-endian value placed in the `checksum` field and computed over the bytes `SOP | cmd_id | length | payload`:

Type | Name          | Definition
:--- | :------------ | :---------
`0`  | Summation     | 2's-complement 16-bit summation: `(~(sum of bytes) + 1) & 0xFFFF`. This is the default.
`1`  | CRC-16/CCITT  | Polynomial `0x1021`, initial value `0xFFFF`, no final XOR.

Both ends must agree on the algorithm; the firmware default is the 2's-complement summation.

### Session model

Provisioning commands are carried as DFU **custom (user-defined) commands**. A transaction is wrapped in a standard DFU session:

1. **Enter DFU** (`cmd_id = 0x38`) opens the session.
2. One or more **provisioning command** packets are exchanged.
3. **Exit DFU** (`cmd_id = 0x3B`) closes the session.

The session boundaries matter because some commands hold **transient per-session state in secure RAM** that is discarded at Exit DFU:

- `PROV_GET_CSR` builds the DER CSR into a secure-RAM buffer on the `offset == 0` request; all chunks of that CSR must be read **within the same session**, or the buffer is gone and the read restarts.
- `PROV_PROVE_CERT` signs `nonce ‖ SHA256(cert)` into the same shared staging buffer on the `offset == 0` request; the signature (up to 4627 B for ML-DSA-87) is then streamed in chunks that must be read **within the same session**.
- `PROV_CERT_WRITE` stages the incoming certificate in a non-secure buffer until the final chunk; the whole upload should likewise stay in one session.

Commands that read from flash (`PROV_READ_PUBKEY`, `PROV_READ_CERT`) hold no transient state, but are still typically streamed in one session for symmetry.

### Chunking

Any payload that can exceed one packet — public key, CSR, certificate — is transferred in fixed-size chunks. The maximum chunk size (`max_chunk`) is reported by `PROV_GET_INFO`; a host must never request or send more than `max_chunk` payload bytes in a single chunk.

- **Reads** (`PROV_GET_CSR`, `PROV_READ_CERT`, `PROV_READ_PUBKEY`, `PROV_PROVE_CERT`): the host issues a first request at `offset == 0`. The response reports the `total` length of the blob. The host then re-requests successive `offset`/`len` windows until it has assembled `total` bytes. For `PROV_GET_CSR`, the `offset == 0` request also (re)builds the CSR and carries the subject-DN template; for `PROV_PROVE_CERT`, the `offset == 0` request carries the challenge nonce and triggers the signature.
- **Writes** (`PROV_CERT_WRITE`): the first chunk (`offset == 0`) declares the full `total_len` and the SHA-256 of the complete certificate, and carries the first slice. Later chunks append at increasing `offset`. When the cumulative received length reaches `total_len`, the device re-hashes the staged bytes, checks them against the supplied SHA-256, and atomically commits — reporting `committed = 1` and the stored `cert_len`.

The `offset` field is a byte offset into the blob (not a chunk index), so a host may pick any chunk size up to `max_chunk` and is free to retry a window.

### Result codes

The first byte of every response payload:

Code   | Name                       | Meaning
:----- | :------------------------- | :------
`0x00` | `PROV_OK`                  | Success
`0x01` | `PROV_ERR_FROZEN`          | Assets frozen; a mutating command was rejected
`0x02` | `PROV_ERR_NOT_READY`       | Structural pre-freeze check failed (e.g. finalize with an incomplete slot)
`0x03` | `PROV_ERR_NO_KEY`          | Key slot empty / invalid
`0x04` | `PROV_ERR_ALG_UNSUPPORTED` | Requested `alg_id` not supported
`0x05` | `PROV_ERR_NO_CERT`         | Certificate slot empty / invalid
`0x06` | `PROV_ERR_CRYPTO`          | Key/CSR generation or signature failure
`0x07` | `PROV_ERR_NO_SLOT`         | `slot_id` out of range (≥ `num_slots`)
`0x08` | `PROV_ERR_SLOT_TYPE`       | Slot role mismatch (e.g. CSR on a non-key slot)
`0x09` | `PROV_ERR_HASH_MISMATCH`   | Streamed certificate hash ≠ declared hash
`0x0A` | `PROV_ERR_LENGTH`          | Bad length / offset / overflow
`0x0B` | `PROV_ERR_FLASH`           | Flash program / erase / ECC error
`0x0C` | `PROV_ERR_CONFIRM`         | Missing / incorrect confirm constant
`0x0D` | `PROV_ERR_PARAM`           | Malformed request payload
`0xFF` | `PROV_ERR_INTERNAL`        | Unspecified secure-side fault

---

## Command reference

Opcodes occupy the DFU user range and are grouped by high nibble: `0x5X` general, `0x6X` key, `0x7X` certificate/CSR. Each command below lists its request and response payloads with the meaning of every field.

### `PROV_GET_STATUS` (0x50)

Read the live provisioning state. Safe, read-only — a good first probe that the link works.

**Request:** *(empty)*

**Response**

Field          | Size       | Description
:------------- | :--------- | :----------
`status`       | 1          | Result code.
`provisioned`  | 1          | `0` = not provisioned (the freeze record is not yet valid); `1` = provisioned/frozen. Derived live from the freeze record, never a stored mutable flag.
`num_slots`    | 1          | Number of asset slots the PAS exposes (the length of the `slot_type` array that follows).
`pas_version`  | 2          | On-flash PAS layout/format version. A host can use this to detect an incompatible store layout.
`slot_type[]`  | 1 × `num_slots` | Per-slot role, derived live: `0` = free, `1` = key, `2` = certificate.

### `PROV_GET_INFO` (0x51)

Read static device identity, slot geometry, and capabilities. A host calls this once to size all its buffers.

**Request:** *(empty)*

**Response**

Field         | Size              | Description
:------------ | :---------------- | :----------
`status`      | 1                 | Result code.
`uid`         | 8                 | Device unique identifier — a stable per-device value. Used to address the unit and as the default CSR `commonName` when no subject template is supplied.
`caps`        | 2                 | Capability bitfield for optional features. Reserved in this build (`0x0000`); a host should ignore unknown bits.
`num_slots`   | 1                 | Number of asset slots (length of the `slot_size` array).
`slot_size[]` | 2 × `num_slots`   | Usable byte capacity of each slot. Lets the host decide which slots can hold a key vs a certificate and reject oversized assets before transfer.
`n_algs`      | 1                 | Number of supported crypto suites that follow.
`alg_id`      | 2 (× `n_algs`)    | Crypto-suite identifier of each supported suite (e.g. `0x0001` = ECDSA secp256r1 / SHA-256, `0x0002` = ECDSA secp384r1 / SHA-384, `0x0003` = ECDSA secp521r1 / SHA-512, `0x0103` = ML-DSA-87 / FIPS 204, advertised on PSC3M6/PSC3M8 when built in).
`pub_len`     | 2 (× `n_algs`)    | Public-key length for that suite, in bytes (e.g. 64 for raw P-256 `X‖Y`, 96 for P-384, 132 for P-521, 2592 for ML-DSA-87). Sizes the host's public-key / read-pubkey buffers.
`sig_len`     | 2 (× `n_algs`)    | Signature length for that suite, in bytes (e.g. 64 for raw P-256 `r‖s`, 96 for P-384, 132 for P-521, 4627 for ML-DSA-87). Tells the host the full `PROV_PROVE_CERT` signature length to expect when assembling chunks.
`max_chunk`   | 2                 | Maximum payload bytes per chunked read/write packet. A host must not exceed this in any single chunk.

> The three per-suite fields are interleaved as one record per suite: `{ alg_id(2) | pub_len(2) | sig_len(2) } × n_algs`. `max_chunk` follows the last record.

### `PROV_ERASE` (0x52)

Return a slot (or every slot) to the free state. Destructive, and rejected once the store is frozen.

**Request**

Field      | Size | Description
:--------- | :--- | :----------
`slot_id`  | 1    | Slot to erase, or `0xFF` to erase **all** slots.
`confirm`  | 4    | Guard constant `0x45524153` (ASCII `'ERAS'`, little-endian on the wire). Any other value returns `PROV_ERR_CONFIRM`. Prevents an accidental erase from a malformed packet.

**Response**

Field    | Size | Description
:------- | :--- | :----------
`status` | 1    | Result code. `PROV_ERR_FROZEN` if the store is already finalized.

### `PROV_FINALIZE` (0x53)

Write the freeze record, making the store permanently read-only. One-way and irreversible — afterwards `gen-key`, `erase`, and `put-cert` are all rejected.

**Request**

Field     | Size | Description
:-------- | :--- | :----------
`confirm` | 4    | Guard constant `0x46524545` (ASCII `'FREE'`, little-endian). Any other value returns `PROV_ERR_CONFIRM`.

**Response**

Field           | Size | Description
:-------------- | :--- | :----------
`status`        | 1    | Result code. `PROV_ERR_NOT_READY` if a structural pre-freeze check fails.
*[`provisioned`]* | 1  | New provisioning state after the freeze (`1`). Present only on success.

### `PROV_GEN_KEY` (0x60)

Generate a fresh key pair inside a slot. The private key is generated and sealed in the secure world and never appears on the wire.

**Request**

Field     | Size | Description
:-------- | :--- | :----------
`slot_id` | 1    | Target slot for the new key pair. Becomes a *key* slot on success.
`alg_id`  | 2    | Crypto suite to generate (e.g. `0x0001` = ECDSA P-256). Must be one advertised by `PROV_GET_INFO`, else `PROV_ERR_ALG_UNSUPPORTED`.

**Response**

Field        | Size | Description
:----------- | :--- | :----------
`status`     | 1    | Result code.
*[`slot_id`]* | 1   | Echo of the slot that now holds the key (confirmation).
*[`alg_id`]*  | 2   | Echo of the generated suite.
*[`pub_len`]* | 2   | Length of the generated public key, in bytes. The public key itself is read separately via `PROV_READ_PUBKEY` / CSR.

### `PROV_READ_PUBKEY` (0x61)

Diagnostic chunked read of the public half of a key pair. The private key is never exposed.

**Request** *(per chunk)*

Field     | Size | Description
:-------- | :--- | :----------
`slot_id` | 1    | Key slot to read.
`offset`  | 2    | Byte offset into the public key. Start at `0` to learn `total`.
`len`     | 2    | Number of bytes requested in this chunk (≤ `max_chunk`).

**Response** *(per chunk)*

Field      | Size      | Description
:--------- | :-------- | :----------
`status`   | 1         | Result code (`PROV_ERR_NO_KEY` if the slot has no key).
*[`alg_id`]* | 2       | Crypto suite of the stored key — tells the host how to interpret the bytes.
*[`total`]*  | 2       | Full public-key length. Loop until this many bytes are assembled.
*[`ret`]*    | 2       | Number of key bytes in **this** chunk.
*[`bytes`]*  | `ret`   | The public-key bytes for `[offset, offset+ret)`.

### `PROV_GET_CSR` (0x70)

Build a PKCS#10 CSR for a key slot and stream it back. The CSR is assembled into transient secure RAM on the `offset == 0` request and signed inside the secure world; the buffer is discarded at Exit DFU.

**Request** *(first chunk, `offset == 0`)*

Field             | Size      | Description
:---------------- | :-------- | :----------
`slot_id`         | 1         | Key slot to certify (must be a *key* slot, else `PROV_ERR_SLOT_TYPE`).
`offset`          | 2         | `0` for the first request — this triggers the (re)build of the CSR and returns its `total` length.
`len`             | 2         | Bytes requested in this chunk (≤ `max_chunk`).
`subject template` | *var*    | Optional subject-DN [TLV blob](#subject-dn-template). If empty, the subject defaults to `CN = hex(uid)`.

**Request** *(later chunks)*

Field     | Size | Description
:-------- | :--- | :----------
`slot_id` | 1    | Same key slot.
`offset`  | 2    | Byte offset into the CSR.
`len`     | 2    | Bytes requested (≤ `max_chunk`).

**Response** *(per chunk)*

Field     | Size    | Description
:-------- | :------ | :----------
`status`  | 1       | Result code.
*[`total`]* | 2     | Full DER CSR length. Loop until assembled.
*[`ret`]*   | 2     | CSR bytes in this chunk.
*[`bytes`]* | `ret` | The CSR DER bytes for `[offset, offset+ret)`.

### `PROV_CERT_WRITE` (0x71)

Stage and commit a device certificate into a slot, bound to a key slot. The host streams the bytes with a SHA-256 the device verifies before an atomic commit. Rejected once frozen.

**Request** *(first chunk, `offset == 0`)*

Field       | Size | Description
:---------- | :--- | :----------
`slot_id`   | 1    | Certificate slot to write. Becomes a *certificate* slot on commit.
`offset`    | 2    | `0` for the first chunk.
`key_slot`  | 1    | Key slot this certificate is **bound to** (recorded as the slot association, enabling `PROV_PROVE_CERT`).
`total_len` | 4    | Full certificate length in bytes. Lets the device know when the transfer is complete and validate the slot capacity up front.
`sha256`    | 32   | SHA-256 of the **complete** certificate. Checked after the last chunk; a mismatch returns `PROV_ERR_HASH_MISMATCH` and nothing is committed.
`bytes`     | *var* | First slice of the certificate (≤ `max_chunk` minus this header).

**Request** *(later chunks)*

Field     | Size  | Description
:-------- | :---- | :----------
`slot_id` | 1     | Same certificate slot.
`offset`  | 2     | Byte offset at which to append this slice.
`bytes`   | *var* | Next slice of the certificate (≤ `max_chunk`).

**Response** *(per chunk)*

Field          | Size | Description
:------------- | :--- | :----------
`status`       | 1    | Result code.
*[`recv_total`]* | 4  | Cumulative bytes received so far. A host can use this to track progress and confirm each append landed.
*[`committed`]*  | 1  | Present only on the **final** chunk (`recv_total == total_len`): `1` once the certificate is hashed, verified, and atomically written to flash.
*[`cert_len`]*   | 2  | Present with `committed`: the stored certificate length.

### `PROV_READ_CERT` (0x72)

Chunked read-back of a stored certificate. The certificate lives in flash, so this is plain streaming with no transient state.

**Request** *(per chunk)*

Field     | Size | Description
:-------- | :--- | :----------
`slot_id` | 1    | Certificate slot to read.
`offset`  | 2    | Byte offset into the certificate. Start at `0` to learn `total`.
`len`     | 2    | Bytes requested (≤ `max_chunk`).

**Response** *(per chunk)*

Field     | Size    | Description
:-------- | :------ | :----------
`status`  | 1       | Result code (`PROV_ERR_NO_CERT` if the slot has no certificate).
*[`total`]* | 2     | Full certificate length. Loop until assembled.
*[`ret`]*   | 2     | Certificate bytes in this chunk.
*[`bytes`]* | `ret` | The certificate bytes for `[offset, offset+ret)`.

### `PROV_PROVE_CERT` (0x73)

Challenge the device to prove it holds the private key bound to a certificate. The device signs the host nonce together with the digest of the certificate it stores, then streams the signature back in chunks.

Because an ML-DSA-87 signature is 4627 bytes (far larger than one packet), the response is **chunked** like `PROV_GET_CSR`: on the `offset == 0` request the secure world computes `SHA256(cert)`, signs `nonce ‖ SHA256(cert)` into the shared staging buffer, and reports the signature `total` length; each chunk (including the first) streams a window of the signature. A raw ECDSA signature is smaller (64 B for P-256, 96 B for P-384, 132 B for P-521) and completes in one or a few chunks. The `alg_id` and `cert_sha256` are echoed on **every** chunk so the host can validate without re-reading the first response.

**Request** *(first chunk, `offset == 0`)*

Field     | Size | Description
:-------- | :--- | :----------
`slot_id` | 1    | Certificate slot to challenge.
`offset`  | 2    | `0` for the first request — this triggers the signature and returns its `total` length.
`len`     | 2    | Bytes of signature requested in this chunk (≤ `max_chunk`).
`nonce`   | 32   | Host-chosen random challenge. Guarantees freshness so a captured response cannot be replayed.

**Request** *(later chunks)*

Field     | Size | Description
:-------- | :--- | :----------
`slot_id` | 1    | Same certificate slot.
`offset`  | 2    | Byte offset into the signature.
`len`     | 2    | Bytes requested (≤ `max_chunk`).

**Response** *(per chunk)*

Field          | Size      | Description
:------------- | :-------- | :----------
`status`       | 1         | Result code.
*[`total`]*    | 2         | Full signature length. Loop until assembled.
*[`alg_id`]*   | 2         | Crypto suite of the signing (bound) key. Echoed on every chunk.
*[`cert_sha256`]* | 32     | SHA-256 of the certificate the device actually stores. Echoed on every chunk; the host compares it against its local copy to confirm both sides reference the same certificate.
*[`ret`]*      | 2         | Signature bytes in this chunk.
*[`signature`]* | `ret`    | Signature bytes for `[offset, offset+ret)`. The full signature is over `nonce ‖ cert_sha256` using the bound private key. For ECDSA this is a raw `r ‖ s` pair (64 B for P-256, 96 B for P-384, 132 B for P-521) a host re-encodes to DER; for ML-DSA-87 it is the raw 4627-byte signature.

A host accepts the proof only if **both** (1) `cert_sha256` equals the SHA-256 of its local certificate copy and (2) the fully assembled signature verifies over `nonce ‖ cert_sha256` with the certificate's public key.

---

## Subject-DN template

The `PROV_GET_CSR` first chunk may carry an optional subject Distinguished Name, letting the host set the CSR subject without building DER itself. It is a sequence of TLV (tag-length-value) entries:

```
field_id(1) | length(1) | value(length)   ... repeated
```

Field      | Size       | Description
:--------- | :--------- | :----------
`field_id` | 1          | DN attribute selector (see table below).
`length`   | 1          | Length of `value` in bytes (`0`–`255`).
`value`    | `length`   | UTF-8 attribute value.

DN attribute IDs:

`field_id` | Attribute
:--------- | :--------
`0x01`     | commonName (CN)
`0x02`     | organizationName (O)
`0x03`     | organizationalUnitName (OU)
`0x04`     | countryName (C)
`0x05`     | serialNumber

Rules:

- Entries are emitted in the order the device builds the DN; supply only the attributes you need.
- An **empty template** (the field omitted, or `length == 0` everywhere) yields a default subject of `CN = hex(device UID)`.
- An unknown `field_id` is rejected (the CSR build fails). Only the IDs above are accepted; certificate extensions are not supported in the CSR currently.
