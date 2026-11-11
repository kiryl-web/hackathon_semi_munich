# Task 1 – ECC baseline: measured so far (build-time, no hardware needed)

Device: PSC3M6 (KIT_PSC3M6_EVAL), 512 KB flash, 128 KB SRAM (per build memory report).
Toolchain: ModusToolbox tools 3.9, GCC 14.2.1, CONFIG=Release, logging off (production size).
Logs: `resp_p2_release.log`, `resp_p4_release.log`.

## Responder static memory (bytes), uninstrumented Release

| Region | P2 ECDSA/ECDHE P-384 (baseline) | P4 ML-DSA-87 / ML-KEM-1024 | Delta |
|---|---:|---:|---:|
| Secure flash (m33_nvm, 128 KB) | 82,300 | 79,532 | −2,768 |
| Non-secure flash (m33ns_nvm, 160 KB) | 48,036 | 47,972 | −64 |
| **Total app flash** | **130,336** | **127,504** | −2,832 |
| Secure .bss | 11,016 | 18,940 | +7,924 |
| Non-secure .bss (libspdm buffers) | 19,952 | 57,816 | **+37,864** |
| Secure heap region (rest of 58 KB S-SRAM) | 39,040 | 31,116 | |
| Non-secure heap region (rest of 68 KB NS-SRAM) | 44,600 | 6,736 | |
| Stacks (S / NS) | 8,192 / 4,096 | 8,192 / 4,096 | |
| Credential store (PAS) | 24,576 (fixed) | 24,576 (fixed) | |

Notes
- P4 is *smaller* in flash: P-384 runs in software mbedTLS on this part (only P-256
  is CryptoLite-accelerated), while ML-DSA/ML-KEM come from wolfCrypt.
- The PQC cost is RAM: NS buffers almost triple (cert chain up to 16 KB, max SPDM
  message 7 KB vs 2 KB, chunk reassembly), see `spdm_lib_config_psc3.h`.
- SRAM shows 100 % in the build report because the heap is sized to fill the
  remainder. Real heap/stack peaks come from the `[PERF]` UART lines at runtime.
- Provisioning app: `PROV_SIG_SUITES="ecdsa-p384 ml-dsa-87"` overflows the 128 KB
  secure flash by 1,016 B (Debug) → one suite per build.
- PAS on M6: key slot 8 KB, cert slot 15 KB; ML-DSA-87 chain = 15,168 B (~99 % full).

## Runtime results – P2 ECC baseline (measured on hardware 2026-11-11)

Run: `results\task1_P2_20261111_1215\` (device log `device_uart.log`, requester output `*.txt`).
Setup: KIT_PSC3M6_EVAL, lifecycle DEVELOPMENT, bridge KitProg3 (fw 2.90, kp3-bulk) I2C 400 kHz,
responder Release (-Os) + [PERF] logging, timer = NS SysTick @ SystemCoreClock.

### Tests (all as expected)
| Test | Result |
|---|---|
| connect (VERSION/CAPABILITIES/ALGORITHMS) | PASS |
| slots (GET_DIGESTS) | PASS |
| get-cert, chain verified to pinned ca_ec384 root | PASS (1,218 B chain) |
| challenge (CHALLENGE_AUTH, ECDSA-P384) | PASS |
| measure (signed GET_MEASUREMENTS) | PASS |
| session (KEY_EXCHANGE ECDHE-P384 / FINISH, AES-256-GCM, END_SESSION) | PASS |
| exchange ×3 (encrypted app message, echoed) | PASS |
| NEGATIVE: wrong trust anchor (ca_ec256) | REJECTED (`SPDM_REQ_EAUTH`) ✔ |

### Device-side timings (µs, from `[PERF]`, stable across runs ±0.2 %)
| Operation | P2 (ECDSA/ECDHE P-384, SW mbedTLS) | P4 |
|---|---:|---:|
| ECDSA-P384 sign (device identity) | **84,400** | |
| ECDHE-P384 ephemeral keygen | **80,700** | |
| ECDHE-P384 shared secret | **82,200** | |
| CHALLENGE_AUTH message total | 85,300 | |
| KEY_EXCHANGE message total (sign + keygen + ss) | **252,200** | |
| FINISH (secured) | 5,000 | |
| Session setup device time (KEY_EXCHANGE + FINISH) | **≈257,000** | |
| Plain SPDM msgs (VERSION/CAPS/ALGS/DIGESTS) | 245–730 | |
| CHUNK_GET (128 B fragment) | 310 | |
| Encrypted app message round (AEAD) | 1,150–1,230 | |

### Communication (P2)
| Metric | Value |
|---|---|
| Transport | MCTP over DFU/I2C, ≤127 B SPDM per DFU transaction |
| Largest SPDM message | CERTIFICATE 1,226 B → 10 CHUNK_GET fragments |
| Full `session` run: transfers / wire bytes | 24 DFU transfers / 2,569 B (VERSION … cert fetch … KEY_EXCHANGE, FINISH, END_SESSION) |
| KEY_EXCHANGE / FINISH / END_SESSION | req 166 B (2× CHUNK_SEND) / rsp 302 B (3× CHUNK_GET); FINISH 86/60 B; END_SESSION 34/37 B (wire size) |
| Host wall clock per transfer | ≈1.4 s (dfuh-cli process start per transaction) |
| `session` command wall clock | 35–38 s (host-bound; device busy ≈265 ms of it) |

### Peak memory at runtime (P2)
| Region | Peak used | Available |
|---|---:|---:|
| NS stack | 2,328 B | 4,096 B |
| NS heap | 1,032 B | 44,600 B |
| S stack | 4,336 B | 8,192 B |
| S heap | 2,576 B | 39,040 B |

Note: DWT->CYCCNT reads 0 from the NS world on this device; timings use NS SysTick.

## Boot-time baseline – ECDSA-P384 via Edge Protect Bootloader (measured on hardware 2026-11-11)

Setup: EPBL (CE43373, `epbl\bl`) with `validation_key_type=ECDSA-384`, `validate_boot=true`,
`validate_upgrade=true`, `sw_downgrade_prev=true`, memory map `overwrite_single_flash.json`
(EPBL 0x32000000/0x1B000, primary slot 0x3201B000/128 KB, upgrade slot 0x32040000/128 KB).
App: CE243041 signed with `keys/ecdsa_p384_img_sign_key_priv.der` (MCUboot format, header 0x400).
Device lifecycle: DEVELOPMENT. EPBL itself is NOT authenticated by BootROM in this setup
(no OEM provisioning / `boot_cfg_id=SECURE_APP`), see boot chain below.
Logs: `results\boot_ECDSA-384_*.log`, `boot_ecdsa384_*.log`; images `results\img_ECDSA-384_*.hex`, `neg_*.hex`.

| Test | Expected | Result |
|---|---|---|
| Authorized image v1.0.0 (ECDSA-P384) | boots | PASS – "User Application validated successfully", runs 1.0.0+0 |
| Corrupted image (1 byte flipped @0x3201C000) | rejected | PASS – `IMAGE_TLV_SHA is invalid`, "Image in the primary slot is not valid!" |
| Incorrectly signed (rogue ECDSA-P384 key) | rejected | PASS – key-hash TLV mismatch, "not valid", app not started |
| Rollback: signed v0.9.0 in upgrade slot | rejected | PASS – `[0s.47ms][ERR] insufficient version in secondary slot`, upgrade slot erased (readback before/after in `boot_evidence/`), 1.0.0 keeps running |
| Modified header version byte @0x3201B014 (B6) | rejected | PASS – `IMAGE_TLV_SHA is invalid` |
| Modified protected TLV @0x3202546C (B7) | rejected | PASS – `IMAGE_TLV_SHA is invalid` |
| Modified signature byte @0x32025585 (B8) | rejected | PASS – hash OK, ECDSA verify fails, not valid |
| Valid update v2.0.0+1 in upgrade slot | installed | PASS – verified, copied secondary→primary, re-verified, runs 2.0.0+1 |

| Metric | ECDSA-P384 baseline |
|---|---:|
| EPBL signature verification (P-384, 48 B hash, 96 B sig) | ≈170 ms |
| Reset → app start (no update pending) | ≈266 ms |
| Reset → app start with update install (erase + copy 128 KB + 2× verify) | ≈3.8 s (log 3s.802; erase alone ≈2.5 s) |
| EPBL flash / static RAM (.text+.data / .bss) | 35,888 B / 6,504 B (+31 KB heap region) |
| Signature + key-hash TLVs in image | 96 B sig + 48 B key hash (vs 4,627 B sig for ML-DSA-87) |

Test-tool lessons: the app build must be `clean`ed before re-signing (otherwise ninja skips the
signing step), and the boot log must be read concurrently with the J-Link reset. For the rollback test the
image load, readback and reset must run in the same J-Link session as the UART capture (`boot_capture.ps1
-JLinkCmds`), otherwise the first boot that sees the old image is missed.

### Boot chain as configured for the baseline (observed + to confirm in AN243370 / ARM §15)

```
Reset
 └─ ROM_BOOT (immutable, v1.0.0.13973 per J-Link)        – classical root, cannot do ML-DSA
     └─ FLASH_BOOT (v91.191.28…)                          – applies lifecycle + boot policy
         └─ image @0x32000000 = EPBL (MCUboot based)       – NOT signature-checked in this setup
             └─ EPBL verifies primary slot @0x3201B000     – ECDSA-P384 (baseline) / ML-DSA-87 (Task 2)
                 └─ application (SPDM responder later)
```
- Lifecycle state read from the device: **DEVELOPMENT** (J-Link "Life Cycle : DEVELOPMENT").
- Boot policy: default (`boot_cfg_id` not set to `SECURE_APP`, `boot_app_layout` zero) → FLASH_BOOT
  launches EPBL without authenticating it. Enabling BootROM/FLASH_BOOT authentication of EPBL needs
  ownership transfer + OEM policy provisioning with an **ECDSA** OEM key (CE243041 docs §3,
  AN243376) – irreversible steps, not performed on the shared hackathon board.
- Consequence: the image chain is authenticated from EPBL onward; EPBL→app is ECDSA-P384 in the
  baseline. The trust anchor for EPBL itself remains classical (ECDSA in ROM/FLASH_BOOT) even when
  EPBL→app migrates to ML-DSA-87 – a documented remaining classical dependency.
- TODO: quote the exact AN243370 / ARM §15 statements for FLASH_BOOT behaviour per lifecycle state.
