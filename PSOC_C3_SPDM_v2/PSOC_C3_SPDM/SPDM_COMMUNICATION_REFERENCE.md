# SPDM communication reference

This reference collects the Requester commands used to exercise the SPDM
Responder on the PSC3M6 EVAL. It will cover four commands; this first capture
documents `connect`.

Run the commands from the `psoc-spdm-requester` directory with the Responder
application running on the device and the MiniProg4 connected to the I2C
interface. Replace `<DFUH-CLI-PATH>` and `<MINIPROG4-HWID>` with the values for
the local setup.

## Device identity and authentication

For device authentication, the Requester must trust the same root CA used
during provisioning: `..\psc3-provisioning-host\test\ca_mldsa87.crt`. The
Requester checks the certificate chain sent by the Responder against this
pinned root. It then uses the SPDM challenge exchange to verify that the
Responder possesses the private key corresponding to the identity certificate.
Pass this root certificate with `--root`; the captured command below uses the
same `.crt` file supplied to the provisioning flow.

The `challenge` operation performs this certificate verification and
challenge-response authentication. `connect` below only negotiates the SPDM
version, capabilities, and algorithms; a successful `connect` by itself does
not authenticate the device.

## 1. Connect and negotiate

The `connect` operation negotiates the SPDM version, capabilities, and
algorithms. It does not authenticate the device certificate; use the
`challenge` operation for authentication.

### Command

```powershell
.\spdm-requester.exe connect --dfuh-cli "<DFUH-CLI-PATH>" --hwid "<MINIPROG4-HWID>"
```

### Expected output

The captured PSC3M6 EVAL / P4 run completed successfully:

```text
connect: OK (VERSION / CAPABILITIES / ALGORITHMS)
  SPDM version         : 1.4 (0x14)
  Responder capabilities: CERT, CHAL, ENCRYPT, MAC, KEY_EX, CHUNK, MEAS(sig)  [0x000202d6]
  Requester capabilities: CERT, CHAL, ENCRYPT, MAC, KEY_EX, CHUNK  [0x000202c6]
  Negotiated algorithms :
    Base asym  : (none)
    PQC asym   : ML_DSA_87
    Base hash  : SHA_384
    DHE group  : (none)
    KEM alg    : ML_KEM_1024
    AEAD suite : AES_256_GCM
    Key schedule: SPDM
    OtherParams : OpaqueDataFmt1 (0x02)
    Meas spec  : DMTF (0x01)
    Meas hash  : SHA_384 (0x00000004)
RESULT: PASS
```

`RESULT: PASS` indicates that the device responded and the Requester completed
SPDM negotiation. The negotiated algorithms shown are expected for the
ML-DSA-87 / P4 responder build.

## 2. Authenticate the device

The `challenge` operation fetches the Responder's certificate chain, verifies
it against the pinned provisioning root CA, then performs an SPDM
challenge-response to verify the Responder's private-key possession.

### Command

```powershell
.\spdm-requester.exe challenge --root ..\psc3-provisioning-host\test\ca_mldsa87.crt --dfuh-cli "<DFUH-CLI-PATH>" --hwid "<MINIPROG4-HWID>"
```

### Expected output

The captured run verified the 15,187-byte certificate chain against the
provisioning root and verified the challenge signature:

```text
connect: OK
set-trust: OK (root pinned)
get-cert: OK (slot 0, 15187 bytes, verified to pinned root)
challenge: OK (slot 0, signature verified)
RESULT: PASS
```

`RESULT: PASS` confirms both chain validation to the pinned root and
challenge-signature verification succeeded.

## 3. Retrieve signed measurements

The `measure` operation requests the Responder's measurements and verifies the
signature over the measurement record. It also checks the record using the
pinned root and device identity.

### Command

```powershell
.\spdm-requester.exe measure --root ..\psc3-provisioning-host\test\ca_mldsa87.crt --dfuh-cli "<DFUH-CLI-PATH>" --hwid "<MINIPROG4-HWID>"
```

### Expected output

The captured run returned four measurement blocks and verified the signature
using SHA-384:

```text
connect: OK
set-trust: OK (root pinned)
get-cert: OK (slot 0, 15187 bytes, verified to pinned root)
measure: OK (slot 0, 4 blocks, signature verified over the record; measurement hash SHA_384)
    block 1: ImmutableROM (digest)  (48 B)  970d6d5f1437273df6b456fb2240fd6abde12b1e99ddef1884077d7b886753cb89598c4d623a0718c54925ff3784eac6
    block 2: MutableFirmware (digest)  (48 B)  6ca27493e94194b577d7e2e9e02177ceff0a86885826a31519556a6212cd5e819e7cdde8175ef398b2aa114bec228d7a
    block 3: MutableFirmwareSecurityVersion (raw)  (8 B)  0100000000000000
    block 4: Informational (raw)  (17 B)  505343334d352d5350444d2d312e302e30
RESULT: PASS
```

Measurement digests and raw values are specific to the firmware image and
device state; they may differ if the firmware or measurement configuration
changes. `RESULT: PASS` indicates that the measurement record signature was
verified successfully.

## 4. Establish a secure session and exchange a message

The `exchange` operation verifies the responder certificate against the
pinned root, establishes an SPDM secure session using the negotiated
AES-256-GCM suite, sends one encrypted application message, prints the
responder's reply, and closes the session.

### Command

```powershell
.\spdm-requester.exe exchange --message "Winter is coming" --root ..\psc3-provisioning-host\test\ca_mldsa87.crt --dfuh-cli "<DFUH-CLI-PATH>" --hwid "<MINIPROG4-HWID>"
```

### Expected output

The captured run established the secure session, sent the message, received
the echoed reply, and closed the session:

```text
connect: OK
set-trust: OK (root pinned)
get-cert: OK (slot 0, 15187 bytes, verified to pinned root)
start-session: OK (KEY_EXCHANGE / FINISH, session 0xffffffff, AES_256_GCM)
exchange: OK (encrypted app message, sent 16 B, received 16 B)
  reply: b'Winter is coming'
end-session: OK (secured END_SESSION)
RESULT: PASS
```

The `reply` matching the sent message demonstrates the encrypted request and
echo response completed over the secure session. `RESULT: PASS` also confirms
the requester closed the session successfully.
