# SEMICON 2026 | PQC Hackathon Challenge

## Make an ECC-Based Embedded SPDM Device Quantum-Resistant

This repository contains the challenge materials, code examples, and references for the SEMICON 2026 PQC Hackathon.

## Why This Challenge Matters

Public-key cryptography is entering a formal migration phase. Future quantum computers could break the assumptions behind widely deployed elliptic-curve cryptography. Data captured today may also be stored and decrypted later, so migration planning cannot wait until a cryptographically relevant quantum computer exists.

CNSA 2.0 is the primary driver for this challenge. For applicable U.S. national-security systems and their suppliers, it defines a transition away from quantum-vulnerable public-key algorithms such as ECDSA and ECDH. Migrating an existing product also requires preserving device identity, updateability, interoperability, performance, and operation within constrained memory.

This challenge focuses on **SPDM (Security Protocol and Data Model)**, the DMTF standard for authenticating hardware components and establishing secure sessions. In datacenters, devices such as GPUs, storage controllers, PCIe/CXL add-in cards, and SmartNICs/DPUs can act as SPDM responders. Platform and management controllers, such as BMCs, act as requesters that verify device identity and firmware measurements before trusting a component.

**SPDM 1.4 adds post-quantum mechanisms**, including ML-DSA signatures and ML-KEM key establishment, alongside classical ECDSA/ECDHE mechanisms. This challenge asks teams to migrate an ECC-based SPDM device on real MCU hardware and measure the engineering costs.

## Regulatory and Standards Context

Participants must distinguish binding requirements from guidance and standards:

- **CNSA 2.0** is the primary challenge driver and target profile. State the applicable requirements and map the existing ECC flow to the selected post-quantum algorithms.
- **DMTF DSP2072, “SPDM Post-Quantum Cryptography White Paper,”** is an informative and directly relevant migration reference.
- **U.S. Executive Order 14028 and National Security Memorandum 10** drive federal cryptographic inventory and post-quantum migration planning. They do not require every commercial product to replace ECC immediately.
- **NIST FIPS 203, 204, and 205** standardize ML-KEM, ML-DSA, and SLH-DSA, respectively. Their relevance depends on the selected implementation and applicable requirements.
- **NIST IR 8547** describes the transition from quantum-vulnerable to post-quantum standards and is a planning reference, not a standalone law.
- **The EU Cyber Resilience Act, NIS2, and DORA** establish cybersecurity, lifecycle, and risk-management obligations; they do not universally prescribe replacing ECC with PQC.
- **BSI and UK NCSC guidance** recommends cryptographic inventory, prioritization, and migration planning for systems with long confidentiality or service lifetimes.

For this hackathon, state the CNSA 2.0 profile being targeted, show how it maps to the existing ECC flow, and identify any remaining classical dependencies. CNSA 2.0 is a design constraint for this challenge, not a claim of certification.

## Starting Point

### Device and Toolchain

| Area | Requirement |
|---|---|
| MCU | PSoC™ Control C3M/P6, using the `PSC3M6GES3A HQ1` configuration |
| Evaluation kit | PSoC™ Control C3M6 Evaluation Kit (`KIT_PSC3M6_EVAL`) |
| Development environment | ModusToolbox™ 3.8 or later, the kit BSP, and supplied host-side tools |
| Tooling constraint | Use Infineon tools and supplied software, including ModusToolbox, its bundled compilers, Edge Protect tooling, the kit BSP, and the provided examples |

### Platform and Secure-Boot References

Use the supplied platform documentation as the reference for device behavior, provisioning, and secure boot:

- **AN243370, Getting Started with PSoC™ Control C3x6 MCU Security:** secure boot, boot flow, EPBL, image signing, lifecycle security, firmware upgrades, and key revocation.
- **AN243376, PSoC™ Control C3x6 MCU Provisioning Specification:** OEM root-of-trust provisioning, policy configuration, image signing, re-provisioning, lifecycle transitions, and key revocation.
- **AN243402, Getting Started with Trusted Firmware-M on PSoC™ Control C3:** TF-M and EPBL integration, image signing and programming, secure boot, and image upgrades.
- **PSoC™ Control C3M/P6 MCU Architecture Reference Manual, Section 15:** ROM_BOOT, FLASH_BOOT, image authentication, MCUboot-format images, OEM root-of-trust keys, lifecycle states, and secure-boot behavior.
- **PSoC™ Control C3M/P6 getting-started documentation:** evaluation-kit setup, BSP usage, ModusToolbox workflow, and supported platform cryptography.

The platform documentation identifies the **Edge Protect Bootloader (EPBL)** as the integration point for PQC image verification and describes ML-DSA image-validation configuration. Use or extend EPBL to verify the next application image with the selected PQC signature before execution. State which stages are authenticated by immutable BootROM and which by EPBL. Do not assume that immutable BootROM supports ML-DSA unless the selected configuration explicitly demonstrates it.

### SPDM Code Example

The supplied **SPDM Code Example (SPDM CE)** is the real-time communication baseline. It provides:

- An SPDM responder running on the PSoC device
- A host acting as the SPDM requester
- ECDSA device authentication and ECC-based certificates or trust anchors
- ECDHE session-key establishment
- Authenticated and encrypted application data after session establishment

The example is functional but uses quantum-vulnerable public-key cryptography. The migration target is the post-quantum capability defined by SPDM 1.4.

## Challenge Objective

Design, implement, and demonstrate a two-part migration:

1. **Boot-time protection:** Protect the chain of trust from reset through application startup, including verification of the next application image.
2. **Real-time protection:** Protect device-to-host communication with SPDM after the application starts.

Both parts are mandatory. A PQC-protected SPDM session is incomplete if an unauthorized application can boot. Secure boot is incomplete if the running application exposes only a classical or unauthenticated runtime channel.

The challenge target is **not a hybrid ECC-plus-PQC protocol flow**. Use the selected PQC algorithms in the migrated target. The ECC implementation is the baseline for comparison. Clearly document any classical trust anchor that remains in immutable BootROM or another earlier boot stage. Do not claim that the entire chain is quantum-resistant while such a dependency remains.

## Required Implementation Tasks

### Task 1: Establish the ECC Baseline (11.25%)

Use the supplied ECC-based boot and SPDM implementation as the reference configuration.

Document the device boot chain, including ROM_BOOT, FLASH_BOOT, EPBL or the application stage, the OEM root-of-trust key, image format, and lifecycle state. Demonstrate applicable next-stage image authentication, rejection of an altered or incorrectly signed image, and the configured development or production lifecycle behavior.

Demonstrate the runtime flow:

- PSoC device acting as the SPDM responder and host acting as the requester
- Responder authentication with ECDSA credentials and certificates
- Session establishment using ECDHE
- Authenticated and encrypted application-data exchange
- Operation in a development lifecycle state suitable for an open hackathon

Record baseline flash and RAM usage, session-establishment time, message sizes, and application throughput or latency.

### Task 2: Protect Boot-Time Operations (15%)

Use or extend EPBL so that the next application image is authenticated with a PQC signature before execution.

Document and test:

- The separation between immutable BootROM verification and EPBL verification
- The boot chain and the exact stage at which each image is authenticated
- MCUboot metadata, image versions, and rollback protection
- The PQC image-validation key and how it is provisioned or embedded
- The selected algorithm and its mapping to the stated CNSA 2.0 target
- Rejection of corrupted, incorrectly signed, malformed, and unauthorized-rollback images
- The migration path from the existing ECC root of trust, including any classical dependency that remains

Explain how EPBL itself is authenticated and protected. If its authenticity ultimately depends on a classical immutable root of trust, document that limitation rather than claiming the entire chain is PQC-protected.

### Task 3: Protect Real-Time Operations with SPDM (30%)

Extend the baseline so that the responder can be authenticated and a session established with post-quantum protection.

The implementation should use:

- **ML-DSA** for post-quantum authentication and signatures, as defined by the applicable SPDM 1.4 profile
- **ML-KEM** for post-quantum key establishment where supported by the selected SPDM or library profile
- A certificate, credential, or trust-anchor format that the host can validate

Map the selected algorithms to the stated CNSA 2.0 target. Do not assume that ML-DSA-65 or ML-KEM-1024 is automatically the correct profile; verify the required security category and algorithm selection.

Address host validation, session-secret establishment and derivation, credential and trust-anchor provisioning, and the memory impact of PQC keys, signatures, certificates, and transcripts.

### Task 4: Connect the Device Lifecycle (18.75%)

Show that boot-time and runtime protection form one maintainable device lifecycle. Implement or describe a secure update path for application images and SPDM credentials. Demonstrate acceptance of a valid update and rejection of corrupted, incorrectly signed, or unauthorized-rollback updates.

Explain the relationship between the verified boot identity and the SPDM responder identity. State whether the runtime identity is anchored in the verified boot chain, separately provisioned credentials, or both, and describe the security consequences.

Include a migration strategy for devices that initially contain ECC credentials. Identify which roots of trust remain classical, which components are PQC-protected, and what must change before the product can claim full quantum-resistant protection.

### Task 5: Present and Demonstrate the Migration (25%)

Prepare a concise technical presentation covering:

- The original ECC architecture and trust model
- The selected post-quantum algorithms and their mapping to the CNSA 2.0 target
- The roles of BootROM, FLASH_BOOT, and EPBL
- The SPDM authentication and key-establishment migration
- Provisioning, credential management, updates, revocation, and rollback protection
- Remaining classical dependencies and limitations
- Resource, performance, and communication costs versus the ECC baseline
- Engineering trade-offs and recommendations for future migration phases

The live demonstration must show an authorized image booting, rejection of an invalid or rollback image, successful SPDM responder authentication, a protected session, authenticated and encrypted application communication, a tampering attempt and its rejection, and successful operation after a valid update.

## Validation Scenarios

Run positive and negative tests for both the ECC baseline and migrated design.

**Positive tests:**

- ECC baseline image verification and boot
- PQC next-image verification and boot through EPBL
- Valid firmware or credential update followed by successful reboot
- ECC baseline SPDM session establishment
- Migrated SPDM session establishment and responder authentication
- Successful application-data exchange and reconnect after update

**Negative tests:**

- Modified responder certificate or credential
- Invalid signature
- Modified MCUboot image metadata or manifest
- Unauthorized rollback image
- Modified SPDM transcript or handshake message
- Unsupported or malformed PQC key material
- Downgrade attempt or fallback to classical-only operation

## Measurements and Comparison

Compare the ECC baseline and PQC implementation, separating boot-time and runtime costs.

| Category | Required measurements |
|---|---|
| Security | Authentication at each boot stage; image-signature, SPDM-authentication, and key-establishment algorithms |
| Memory | BootROM, EPBL, and application flash; boot-time and runtime RAM; credential storage; working buffers; peak handshake memory |
| Performance | Boot verification and boot-to-application times; session setup; signature verification; KEM encapsulation and decapsulation |
| Communication | Handshake message count and total size; largest message; fragmentation behavior |
| Migration | Remaining classical dependencies in the boot chain and SPDM; components changed to PQC; upgrade path for deployed ECC devices |

Report total memory impact, not just individual key sizes. Include certificates, signatures, transcript and temporary computation buffers, metadata, and persistent trust anchors.

## Expected Deliverables

1. Source code for the EPBL integration, PSoC responder, and host requester/test application
2. Reproducible ModusToolbox build and run instructions
3. ECC-baseline and PQC-implementation configurations
4. Credential-generation and provisioning scripts
5. A test report covering positive and negative boot-time and SPDM scenarios
6. A boot-time and runtime resource and performance comparison
7. A migration architecture describing remaining classical trust anchors and the path toward fully PQC-protected operation
8. A five-minute live demo showing verified boot, protected traffic, tamper rejection, a valid update, and successful operation afterward

## Evaluation Criteria

### Security Correctness

- Only authorized images boot; corruption, invalid signatures, and unauthorized rollback are rejected.
- The requester authenticates the responder, and the session provides authenticated encryption for application data.
- Invalid credentials, signatures, transcripts, and downgrade attempts are rejected.
- The implementation does not silently fall back to classical-only operation.

### Migration Quality

- The design addresses a real ECC-to-PQC transition.
- The stated compliance driver is mapped to concrete technical changes.
- Remaining classical dependencies and transition limitations are clearly identified.
- Provisioning, rotation, revocation, firmware updates, lifecycle transitions, and rollback protection are considered.

### Embedded Engineering

- The implementation fits PSoC resource constraints or clearly documents trade-offs.
- Large PQC objects are handled safely, with bounded memory use.
- The implementation is reproducible, understandable, and testable.


## Verified Windows Bring-Up: PSC3M6 and SPDM Profile P4

This section records the setup completed on **2026-11-10/11**. It documents
successful firmware build/programming, device provisioning, SPDM negotiation
and an Eclipse import. **A complete `CHALLENGE_AUTH` test was deliberately
cancelled before completion and is not claimed as passed.** These observations
do not establish that all challenge tasks or security properties above have
been demonstrated.

The supplied `psoc-spdm-responder` application is a **separate Git repository**.
Its detailed application README is not tracked as a file in this parent
repository, so the essential setup notes are included here directly. Obtain
the responder and the separate provisioning/host applications from the
supplied SPDM materials before following the application commands below.

### Changes Needed for This Setup

| Area | Required change or recovery | Reason |
|---|---|---|
| Board and profile defaults | Set `TARGET=APP_KIT_PSC3M6_EVAL` and `SPDM_PROFILE=P4` in the responder's `common.mk` | Match the actual M6 board and its ML-DSA-87 identity |
| Build tools | Use ModusToolbox tools 3.9, GNU Arm 14.2.1, native CMake 3.31.6 and Edge Protect Security Suite 2.4.0 | CMake was missing and is required by the libspdm prebuild |
| Build environment | Set `CY_TOOLS_PATHS`, `HOME` and the tool PATH for both the shell and Eclipse | The installation was outside the default tool search paths |
| Project location | Use a short path or a `subst P:` alias to the directory containing the SPDM applications | The original nested path with spaces and parentheses broke the Windows/Cygwin build |
| Generated BSP files | Run `make getlibs`; back up and regenerate stale `GeneratedSource` from the existing `design.modus` when necessary | Device-library metadata and generated symbols were stale or missing |
| USB-I2C bridge | Select the correct bridge firmware/mode and wire SDA/SCL to I2C pins, not SWD pins | USB enumeration alone did not establish a working I2C connection |
| Eclipse | Export and import the application plus both Secure and Nonsecure projects; include CMake in the IDE PATH | The IDE needs the same tools as the command-line build |

No manual change to the cryptographic algorithms was needed for this bring-up.
The source `design.modus`, device lifecycle and PSC3M6 security policy were not
changed. M6 partitioning comes from the Device Configurator; do not apply the
M5-specific OEM-policy procedure to M6 without checking the platform guidance.

### Correct PSC3M6 I2C Wiring

**The original SPDM example table reversed the reference and ground pins:
PSC3M6 J15.7 is 3.3 V, and J15.8 is GND.**

For MiniProg4, use its **12-pin, 2x6 communication header**, not a SWD header:

| MiniProg4 pin | Signal | PSC3M6 connection |
|---|---|---|
| 1 | VTARG reference | J15.7 / 3.3 V |
| 2 | SDA | J16.6 / P9.2 |
| 4 | SCL | J16.5 / P9.0 |
| 12 | GND | J15.8 / GND |

A physically detached KitProg3 section from **CY8CPROTO-062-4343W** was also
used. Its pin labels and connector positions are different:

| Detached KitProg3 pin | Signal | PSC3M6 connection |
|---|---|---|
| `12.1` / `P12.1`, J7 pin 5 | SDA | J16.6 / P9.2 |
| `12.0` / `P12.0`, J7 pin 4 | SCL | J16.5 / P9.0 |
| J4 pin 1 | VTARG reference input | J15.7 / 3.3 V |
| J4 pin 2 | GND | J15.8 / GND |

`J7.5` means pin 5 of connector J7, not necessarily a printed board label.
J4 is the five-pin SWD connector. **Only VTARG and GND belong on J4 for this
I2C setup; SWDIO/SWDCLK are not SDA/SCL.**

Power the PSC3 through its on-board J-Link USB connector **J13** and the bridge
through its own USB connector. VTARG is an input for voltage-level translation,
not a power output for the target. Disconnect both USB supplies before
rewiring. Do not connect VBUS/5 V to the PSC3's 3.3 V reference or I2C pins,
and do not join independently driven supply rails if KitProg3 is still attached
to another powered board.

### Bridge Firmware and Mode

- The original MiniProg4 was switched from DAPLink to **`kp3-hid`** using
	`fw-loader --mode`; its firmware was not updated during that test.
- The detached KitProg3 originally ran firmware **1.01.158**. Updating that
	selected probe with `fw-loader --update-kp3` to **2.90.1804**, then using
	**`kp3-bulk`**, made its I2C interface visible to the Windows DFU host.
	This updates the programmer, not the PSC3 firmware.
- HID did not expose I2C for that updated KitProg3 setup. Check the actual
	result with `dfuh-cli --display-hw`; do not assume one mode works for every
	firmware/hardware combination. Select the corresponding `--hwid`.
- Standard KitProg3 firmware supplies the USB-I2C bridge. No separate SPDM
	firmware or MODE-button press is needed to start a host test.

The tested address was **0x08**. MiniProg4 worked at **400 kHz**; the final
KitProg3 transfer tests used **100 kHz**. Wrong connectors, reversed ground/
reference wiring, loose jumpers, USB unplugging and laptop battery loss were
encountered during setup. Keep the wires secure and the laptop on external
power throughout a long test. An initial Enter-DFU write failure (DFU error 3,
exit code 8195) occurs before SPDM and does not prove a signature rejection.

### What Is Flashed Where?

The USB bridge and the PSC3M6 target run different firmware. The target board
uses a Cortex-M33 MCU; the detached KitProg3 is only the programmer/bridge.

| Device | Firmware/application | How it is installed or used |
|---|---|---|
| MiniProg4 or detached KitProg3 | Compatible stock Infineon MiniProg4/KitProg3 firmware, not the SPDM application | Use `fw-loader` for a necessary bridge update and select the verified I2C mode described above |
| PSC3M6 target, provisioning stage | `psc3-provisioning-app`, including its Secure and Nonsecure projects | Program the combined application through the target board's on-board J-Link |
| PSC3M6 target, runtime stage | `psoc-spdm-responder`, including its Secure and Nonsecure projects | Replace the provisioning application through J-Link while preserving PAS keys and certificates |
| Windows PC | Provisioning host utilities and the SPDM Requester | Run these on the PC; they are not flashed onto either device |

For a target without SPDM credentials, use this order:

1. Prepare the MiniProg4/KitProg3 bridge and confirm that `dfuh-cli --display-hw`
	exposes its I2C interface. Do not flash a PSC3 application image onto it.
2. Build and program `psc3-provisioning-app` onto the **PSC3M6 target** via
	its on-board J-Link, not via the I2C-only bridge wiring.
3. Run the provisioning host on Windows through the USB-I2C bridge to generate
	the device key, obtain/sign its CSR, and transfer and verify the certificate
	chain. The private device key stays on the target.
4. Build and program the combined `psoc-spdm-responder` application onto the
	**same PSC3M6 target**, preserving the provisioned PAS contents. Use the
	application-level Program launch, not just the Nonsecure image by itself.
5. Run the SPDM Requester on Windows to communicate with the responder over
	the USB-I2C bridge.

Once the responder and credentials are installed, normal SPDM tests do **not**
require reflashing either device. A further bridge firmware update is not a
prerequisite for each test. No full-device erase, freeze/finalize operation or
lifecycle change was needed in the verified setup.

### Build and Eclipse Setup

For a fresh setup, obtain the supplied application and its libspdm submodule
and dependencies first. Prefer a short checkout path. The example below assumes
`P:` maps to the parent containing `psoc-spdm-responder` and native CMake is
installed under `C:/Tools/cmake`; adapt these paths to your installation:

```powershell
$env:CY_TOOLS_PATHS = 'C:/Infineon/Tools/ModusToolbox/tools_3.9'
$env:HOME = $env:USERPROFILE.Replace('\', '/')
$env:PATH = 'C:/Tools/cmake/bin;C:/Infineon/Tools/ModusToolbox/tools_3.9/modus-shell/bin;' + $env:PATH
$make = Join-Path $env:CY_TOOLS_PATHS 'modus-shell/bin/make.exe'
& $make -C P:/psoc-spdm-responder getlibs
& $make -C P:/psoc-spdm-responder build TARGET=APP_KIT_PSC3M6_EVAL SPDM_PROFILE=P4
& $make -C P:/psoc-spdm-responder eclipse TARGET=APP_KIT_PSC3M6_EVAL SPDM_PROFILE=P4 BSP_PROGRAM_INTERFACE=JLink
```

If generated BSP symbols are stale, back up `GeneratedSource` before the
recovery command below. This is not a mandatory action for every build:

```powershell
& (Join-Path $env:CY_TOOLS_PATHS 'device-configurator/device-configurator-cli.exe') --build 'P:/psoc-spdm-responder/bsps/TARGET_APP_KIT_PSC3M6_EVAL/config/design.modus' --readonly --force
```

Start Eclipse with the same tool environment, including the CMake, GCC and
Edge Protect paths. Updating a shell's PATH does not update an already running
IDE. The local Eclipse starter was extended with the existing CMake path;
that machine-specific starter is not part of this repository. A `P:` alias is
also local and must be recreated after a Windows restart if project paths
still refer to it.

`make eclipse` exported these three projects:

- `psoc-spdm-responder`
- `psoc-spdm-responder.proj_cm33_s`
- `psoc-spdm-responder.proj_cm33_ns`

Import them **in place**, without copying into the workspace. If the generic
Eclipse importer finds only the application, import `proj_cm33_s` and
`proj_cm33_ns` separately. Existing workspace projects were retained. Import
preference-lock and interrupted local-history messages were resolved by
regular exits/restarts after the import completed, without deleting the
workspace. The final loaded Eclipse session showed no new errors.

Use the PSC3's **on-board J-Link** for Program/Debug, not the I2C-only KitProg3
wiring. When selecting a particular probe, the make variable is
**`MTB_PROBE_SERIAL`**, not `PROBE_SERIAL`. The host **SPDM Requester remains a
separate program**; importing into Eclipse neither starts authentication nor
automatically removes the DFU host's per-transfer overhead.

### Programming Commands: KitProg3 and PSC3M6

These are instructions for a deliberate installation, not commands to run for
every SPDM test. Programming replaces firmware; provisioning can change the
device identity. Use stable USB power and keep the laptop on external power.
Do not unplug a device while its firmware is being programmed or verified.

Use the PowerShell build environment from **Build and Eclipse Setup** above
(`$make`, `$env:CY_TOOLS_PATHS`, HOME and PATH). The examples assume the supplied
applications and their BSP/dependencies are already available under `P:`.
Replace the example tool paths and serial placeholders with your own values.

#### 1. Prepare or Update the USB-I2C Bridge

Install the Infineon ModusToolbox Programming Tools package. `fw-loader`
selects the compatible bundled vendor firmware for the connected programmer;
do not give it the PSC3 application image. First list the connected devices:

```powershell
$programmingTools = 'C:/Infineon/Tools/ModusToolboxProgtools-2.0'
$fwLoader = Join-Path $programmingTools 'fw-loader/bin/fw-loader.exe'
$dfuh = Join-Path $env:CY_TOOLS_PATHS 'dfuh-tool/dfuh-cli.exe'
& $fwLoader --device-list
```

For the detached CY8CPROTO KitProg3, run the following block **only when a
firmware update is needed**. Copy its exact serial from the device list. Do
not use `all`, and do not confuse this serial with the board's J-Link serial.

```powershell
$bridgeSerial = 'REPLACE_WITH_KITPROG_SERIAL'
if ($bridgeSerial -like 'REPLACE_*') { throw 'Set the KitProg3 serial from --device-list first.' }
& $fwLoader --update-kp3 $bridgeSerial
if ($LASTEXITCODE -ne 0) { throw 'KitProg3 firmware update failed; stop here.' }
& $fwLoader --mode kp3-bulk $bridgeSerial
if ($LASTEXITCODE -ne 0) { throw 'KitProg3 mode selection failed; stop here.' }
& $fwLoader --device-list
& $dfuh --display-hw
```

The update must report successful programming/verification, and the final
listing must expose the selected device as **I2C**. Version 2.90.1804 and BULK
were verified for the detached KitProg3 used here; another installed tools
package may bundle a different compatible firmware version.

For the **original MiniProg4 instead**, no firmware update was needed in this
setup. Select that MiniProg4's serial and switch only its mode. This is an
alternative to the KitProg3 block, not a second mode change for the same probe:

```powershell
$miniProgSerial = 'REPLACE_WITH_MINIPROG4_SERIAL'
if ($miniProgSerial -like 'REPLACE_*') { throw 'Set the MiniProg4 serial from --device-list first.' }
& $fwLoader --mode kp3-hid $miniProgSerial
if ($LASTEXITCODE -ne 0) { throw 'MiniProg4 mode selection failed; stop here.' }
& $dfuh --display-hw
```

Do not continue to host tests unless the selected bridge is listed as I2C.
Updating/switching this bridge does **not** program the PSC3 target.

#### 2. Program the Provisioning Application onto PSC3M6

Only do this stage for a target whose credentials you intend to provision.
Connect the PSC3's **J13 USB connector** for its on-board J-Link and use that
J-Link's serial number below, not the USB-I2C bridge serial. The provisioning
application is a separate supplied project, not the SPDM requester executable.

```powershell
$psc3ProbeSerial = 'REPLACE_WITH_ONBOARD_JLINK_SERIAL'
if ($psc3ProbeSerial -like 'REPLACE_*') { throw 'Set the PSC3 board J-Link serial first.' }
& $make -C P:/psc3-provisioning-app getlibs
if ($LASTEXITCODE -ne 0) { throw 'Provisioning dependencies failed; stop here.' }
& $make -C P:/psc3-provisioning-app build TARGET=APP_KIT_PSC3M6_EVAL PROV_SIG_SUITES=ml-dsa-87 PROV_SELFTEST=0 PROV_SELFTEST_FINALIZE=0
if ($LASTEXITCODE -ne 0) { throw 'Provisioning build failed; do not program a stale image.' }
& $make -C P:/psc3-provisioning-app qprogram TARGET=APP_KIT_PSC3M6_EVAL PROV_SIG_SUITES=ml-dsa-87 PROV_SELFTEST=0 PROV_SELFTEST_FINALIZE=0 BSP_PROGRAM_INTERFACE=JLink "MTB_PROBE_SERIAL=$psc3ProbeSerial"
if ($LASTEXITCODE -ne 0) { throw 'Provisioning application programming failed.' }
```

`qprogram` programs the already built **combined application**, including its
Secure and Nonsecure images. Confirm the programmer's program/verify result.
The UART startup should identify the provisioning application and report I2C
ready; the verified board used its J-Link UART at **115200 baud, 8-N-1**.

**Stop here to provision credentials.** Run the supplied provisioning host
on Windows over the USB-I2C bridge, generate the device key/CSR, sign and
transfer the certificate chain, and verify readback and `prove-cert`. Follow
the provisioning application's instructions; do not pass `--freeze` or
finalize unless an intentional lifecycle decision requires it. Flashing the
responder alone does not create its device key or certificates.

#### 3. Program the SPDM Responder onto the Same PSC3M6

After provisioning succeeds, use the same on-board J-Link and program the
combined responder without a full-device erase. If you skipped the previous
stage because credentials already exist, set `$psc3ProbeSerial` to your
board's J-Link serial before running this block.

```powershell
if ([string]::IsNullOrWhiteSpace($psc3ProbeSerial) -or $psc3ProbeSerial -like 'REPLACE_*') { throw 'Set the PSC3 board J-Link serial first.' }
& $make -C P:/psoc-spdm-responder build TARGET=APP_KIT_PSC3M6_EVAL SPDM_PROFILE=P4
if ($LASTEXITCODE -ne 0) { throw 'Responder build failed; do not program a stale image.' }
& $make -C P:/psoc-spdm-responder qprogram TARGET=APP_KIT_PSC3M6_EVAL SPDM_PROFILE=P4 BSP_PROGRAM_INTERFACE=JLink "MTB_PROBE_SERIAL=$psc3ProbeSerial"
if ($LASTEXITCODE -ne 0) { throw 'Responder programming failed.' }
```

The combined responder image is generated as `build/app_combined.hex` in its
application directory. Do not select only the Nonsecure image, perform a
full-device erase, or overwrite PAS assets. Confirm successful program/verify
and the UART messages for responder initialization, certificate-chain loading
and the I2C transport waiting for host commands.

As an **Eclipse alternative**, select the imported responder application,
run **Build Application**, then its application-level **Program Application**
launch using the on-board J-Link. Do not choose **Erase Device**. After that,
start the SPDM Requester separately on Windows; it is not firmware to flash.

### Provisioning and Observed Verification Status

The separate provisioning application/host tools were used before flashing
the responder. Their Python environment needed **PyYAML** and
**cryptography >= 46** for ML-DSA support; version 50.0.2 was used. The device
generated its private ML-DSA-87 key in **PAS slot 0**. A demo CA signed the CSR,
and the **15,168-byte** certificate chain was placed in **PAS slot 1**, exposed
as **SPDM certificate slot 0**. Certificate readback matched byte-for-byte, and
provisioning `prove-cert` verified key possession.

The responder was programmed and verified without a full-device erase,
freeze/finalize operation or lifecycle change, retaining those assets. Preserve
PAS contents when switching applications; do not publish production/private
keys or use the demo CA as a production identity.

| Check | Observed result |
|---|---|
| P4 responder build and programming | Passed, including programming verification |
| Asset provisioning, readback and `prove-cert` | Passed; assets were not frozen |
| Responder startup | Certificate loaded, context ready, I2C listening |
| SPDM negotiation | SPDM 1.4, ML-DSA-87, ML-KEM-1024, SHA-384, AES-256-GCM |
| SPDM certificate transfer | Successful fragments; complete SPDM certificate validation not finished |
| Eclipse export/import | Application and both projects imported; clean final session |
| `CHALLENGE_AUTH`, signed measurements, secure session, encrypted exchange | Not verified end-to-end |

The final authentication run was cancelled intentionally to proceed with the
Eclipse import. Earlier runs were interrupted by disconnected cables or power
loss. A cancelled run reporting `SPDM_REQ_EAUTH` does not establish an invalid
signature. Provisioning `prove-cert` is distinct from SPDM `CHALLENGE_AUTH`.

Windows host/bridge startup measured about **11 seconds per DFU transaction**;
the small GET_VERSION exchange itself took under one second after startup.
The certificate and ML-DSA-87 signature use many small fragments, so a complete
test can take many minutes. Let it finish before claiming authenticated SPDM
success. Build/IDE caches, local logs, personal machine paths and private keys
should not be included in a GitHub publication.


## Abbreviations and Terms

| Abbreviation or term | Meaning |
|---|---|
| AN | Application Note |
| BMC | Baseboard Management Controller |
| BSP | Board Support Package |
| BSI | Bundesamt für Sicherheit in der Informationstechnik, Germany's Federal Office for Information Security |
| CE | Code Example |
| CNSA 2.0 | Commercial National Security Algorithm Suite, version 2.0 |
| CXL | Compute Express Link |
| DMTF | Distributed Management Task Force |
| DORA | Digital Operational Resilience Act |
| DPU | Data Processing Unit |
| ECDH | Elliptic-Curve Diffie-Hellman |
| ECDHE | Ephemeral Elliptic-Curve Diffie-Hellman |
| ECDSA | Elliptic Curve Digital Signature Algorithm |
| ECC | Elliptic-Curve Cryptography |
| EO 14028 | Executive Order 14028, “Improving the Nation's Cybersecurity” |
| EPBL | Edge Protect Bootloader |
| EU | European Union |
| FIPS | Federal Information Processing Standards |
| FLASH_BOOT | A platform boot stage that runs from flash; see the platform documentation for its exact behavior |
| GPU | Graphics Processing Unit |
| KEM | Key-Encapsulation Mechanism |
| MCU | Microcontroller Unit |
| MCUboot | The bootloader project and image format used by the platform; the name is not expanded as an acronym |
| ML-DSA | Module-Lattice-Based Digital Signature Algorithm; standardized in NIST FIPS 204 |
| ML-KEM | Module-Lattice-Based Key-Encapsulation Mechanism; standardized in NIST FIPS 203 |
| NCSC | National Cyber Security Centre; UK NCSC refers to the United Kingdom's centre |
| NIS2 | Network and Information Security Directive 2 |
| NIST | National Institute of Standards and Technology |
| NIST IR | NIST Interagency/Internal Report, a NIST publication series |
| NSM 10 | National Security Memorandum 10 |
| OEM | Original Equipment Manufacturer |
| PCIe | Peripheral Component Interconnect Express |
| PQC | Post-Quantum Cryptography |
| PSoC | Programmable System-on-Chip |
| RAM | Random-Access Memory |
| RoT | Root of Trust |
| ROM | Read-Only Memory |
| ROM_BOOT | A platform boot stage that runs from ROM; see the platform documentation for its exact behavior |
| SLH-DSA | Stateless Hash-Based Digital Signature Algorithm; standardized in NIST FIPS 205 |
| SmartNIC | Smart Network Interface Card |
| SPDM | Security Protocol and Data Model |
| TF-M | Trusted Firmware-M |
| UK | United Kingdom |
| U.S. | United States |

**Document and product identifiers:** FIPS 203 specifies ML-KEM, FIPS 204 specifies ML-DSA, and FIPS 205 specifies SLH-DSA. Identifiers such as `AN243370`, `PSC3M6GES3A HQ1`, and `KIT_PSC3M6_EVAL` are document, device-configuration, and evaluation-kit identifiers, respectively. ModusToolbox is a product name.

## Useful Links

Some referenced materials are also available in this repository. These links point to the original sources:

- [Secure boot and update with PQC (CE243041)](https://github.com/Infineon/mtb-example-ce243041-secure-boot-and-update-with-epb/)
- [Edge Protect Bootloader with PQC capability (CE43373)](https://github.com/Infineon/mtb-example-ce43373-edgeprotect-bootloader)
- [AN243370: Getting Started with PSoC Control C3M6 Security](https://www.infineon.com/assets/row/public/documents/non-assigned/42/infineon-an243370-getting-started-psoc-control-c3pm6-security-applicationnotes-en.pdf)
- [AN243376: PSoC Control C3M6 Provisioning](https://www.infineon.com/assets/row/public/documents/non-assigned/42/infineon-an243376-psoc-control-c3pm6-provisioning-applicationnotes-en.pdf)

## Contact

Questions? Email me or open an issue in this repository.

[ericjoerg.schulze@infineon.com](mailto:ericjoerg.schulze@infineon.com)
