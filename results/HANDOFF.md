# Handoff – state after Task 4 (2026-11-11, 22:xx)

Read this first in a new session. **Task 4 (lifecycle) is DONE**: `results/Task4_Lifecycle_Report.pdf`,
details `results/TASK4_NOTES.md`. The SPDM responder now runs BEHIND EPBL (ML-DSA-87) with real SPDM
measurements. Next = Task 5 (presentation + 5-min live demo, deadline 2026-11-12 12:00).
**Task 3 (SPDM PQC) is DONE**: `results/Task3_PQC_SPDM_Report.pdf`, details `results/TASK3_NOTES.md`.
Task 2 (boot-time PQC) is DONE: `results/Task2_PQC_Boot_Report.pdf`,
details `results/TASK2_NOTES.md`. Task 1 (ECC baseline) is DONE: `results/Task1_ECC_Baseline_Report.pdf`,
details in `results/TASK1_NOTES.md`.

## Folder layout (renamed – old paths no longer exist)
| What | Path |
|---|---|
| SPDM workspace (was "PSOC_C3_SPDM (2)") | `C:\Users\kpato\KP\Hackathon\PSOC_C3_SPDM_v2\PSOC_C3_SPDM\SPDM\` |
| – provisioning app / host | `...\SPDM\psc3-provisioning-app\`, `...\SPDM\psc3-provisioning-host\` |
| – SPDM responder (+ libspdm) | `...\SPDM\psoc-spdm-responder\` |
| – SPDM requester (exe + USER_GUIDE.pdf) | `...\SPDM\psoc-spdm-requester\` |
| Edge Protect Bootloader (was ce43373 ...-master, renamed for Windows 260-char path limit) | `C:\Users\kpato\KP\Hackathon\epbl\bl\` |
| Secure-boot demo app (CE243041) | `C:\Users\kpato\KP\Hackathon\mtb-example-ce243041-secure-boot-and-update-with-epb-master\` |
| Helper scripts | `C:\Users\kpato\KP\Hackathon\tools\` |
| Results / logs / reports | `C:\Users\kpato\KP\Hackathon\results\` |

## Environment
- Dot-source `tools\mtbenv.ps1` before CLI builds (sets CY_TOOLS_PATHS, HOME, PATH incl. CMake 4.4,
  modus-shell, edgeprotecttools; defines `$make`, `$dfuh`, `$SPDM`).
- Python for host tools: `py -3.13` (default `python` is 3.8). PyYAML, cryptography 50, reportlab installed.
- VS Code: open the `*.code-workspace` files (generated with `make vscode`), use Run Task → Build / Program.
- Board: KIT_PSC3M6_EVAL, on-board J-Link (USB J13) S/N 591239652, debug UART = COM4 (115200).
  Lifecycle DEVELOPMENT. Never run "Erase Device"; never use provisioning `--freeze`.
- I2C bridge: supervisor's KitProg3, hwid `KitProg3-140A16D001060400` (updated to fw 2.90, mode kp3-bulk).
  Check with `dfuh-cli --display-hw` → must show `I2C: KitProg3-...`. Wiring: SDA J16.6, SCL J16.5,
  VTARG J15.7, GND J15.8.

## Scripts (tools\)
| Script | Purpose |
|---|---|
| `task1_provision_p2.ps1` | provision ECDSA-P384 SPDM identity (erase slots 0/1, gen key, CA-sign, install chain) |
| `task1_spdm_runs.ps1 -Profile P2|P4 -Hwid ...` | full requester suite + negative wrong-root test + UART [PERF] capture |
| `boot_tests.ps1 -Kind boot|update -Major -Minor -Tag [-KeyType -Key]` | clean-build, sign, flash, capture EPBL boot log |
| `boot_capture.ps1 -Out file` | J-Link reset + concurrent COM4 capture |
| `hex_flip.py in out addr` | tamper one byte in an Intel-HEX |
| `make_task1_report.py` | regenerates the Task 1 PDF |

## Current device state (after Task 4) – supersedes the Task 3 section below
- Flash: EPBL (spdm map, ML-DSA-87/SHA-512) @0x32000000 + SPDM responder **v2.0.0** (TARGET=APP_KIT_PSC3M6_EVAL_EPBL,
  P4, FI=0) in the primary slot @0x3201B000; upgrade slot empty; PAS @0x32048000 = ML-DSA-87 identity from the task4
  RESTORE (artifacts provisioning-host `test/_work/edge_p4`, root ca_mldsa87 = results/task3_neg_roots/good_root.der).
- Demo images: results/task4_boot/*.hex (signed v1.0.0/1.1.0/1.2.0/2.0.0 + tamper variants, epbl_full.hex).
  Runner `tools/task4_epbl_spdm_tests.ps1 -Phase BOARD [-Only T1] [-NoSpdm]`; reference values `tools/task4_verify_meas.py`.
- Do NOT flash the standalone responder or the provisioning app unless needed: both overwrite EPBL at 0x32000000
  (then reload results/task4_boot/epbl_full.hex + a boot image).
- Both responder targets build from one tree (no getlibs needed): `TARGET=APP_KIT_PSC3M6_EVAL` (standalone) or
  `TARGET=APP_KIT_PSC3M6_EVAL_EPBL`; build/app_combined.hex is shared -> the PREP phase rebuilds the EPBL target itself.
- EPBL build for this map needs `MEMORY_MAP=<abs>/epbl/bl/platforms/PSC3_M6/memory_maps/spdm_overwrite_single_flash.json`
  (the default in epbl common.mk is still the CE243041 map for the Task 2 demo app).

## Device state after Task 3 (historical)
- Flash @0x32000000: **SPDM responder P4 (ML-DSA-87 / ML-KEM-1024), SPDM_FAULT_INJECT=0**, Release + [PERF] logging.
  EPBL + CE243041 app were overwritten → for boot demos reprogram EPBL (`make -C epbl/bl build` then `qprogram`,
  feature_config_ML-DSA-87.json active) and the signed app (results/img_ML-DSA-87_v1_0_boot.hex).
- PAS: ML-DSA-87 key slot 0 + SPDM chain slot 1 (root ca_mldsa87 → leaf "PSC3 DEV 01"), NOT frozen. Re-provisioned
  18:08 (new on-device key; artifacts in provisioning-host `test/_work/edge_p4`; original Task 3 identity artifacts
  backed up in `results/task3_cred_p4_original`).
- To switch SPDM back to ECC: program provisioning app built with PROV_SIG_SUITES=ecdsa-p384, run
  task1_provision_p2.ps1, build responder `SPDM_PROFILE=P2` (clean first), program.

## Code changes made (all measurement-only or config)
- Responder: `[PERF]` instrumentation (shared/spdm_perf.h, shared/nsc_perf.h, proj_cm33_s/nsc/nsc_perf.c,
  spdm_heap.c peak, router per-transfer log, SysTick timer in NS, crypto timing in device_secret.c,
  cryptlib/ec.c, cryptlib/mlkem.c). `common.mk`: SPDM_PROFILE ?= P2, CONFIG=Release, SPDM_PERF_LOG ?= 1.
- Provisioning app `common.mk`: PROV_SIG_SUITES ?= ecdsa-p384 (both suites together overflow S flash by
  1,016 B) → set to `ml-dsa-87` for Task 3.
- EPBL: `common.mk` MEMORY_MAP → CE243041 overwrite_single_flash.json; `platforms/PSC3_M6/feature_config.json`
  = ECDSA-384, key `../../../mtb-example-ce243041-.../keys/ecdsa_p384_img_sign_key_pub.der`,
  validate_boot/upgrade = true, sw_downgrade_prev = true. (Write JSON without BOM.)
- CE243041 Makefile: IMAGE_SIGNING_KEY_TYPE=ECDSA-384, key ecdsa_p384 (BSP added via library-manager,
  TARGET=APP_KIT_PSC3M6_EVAL).

## Gotchas learned
- Build paths must have no spaces/parentheses and stay < 260 chars.
- DWT->CYCCNT reads 0 from NS → timings use NS SysTick.
- Re-signing the CE243041 app requires `make clean` first (ninja otherwise skips signing).
- PowerShell 5.1 + `$ErrorActionPreference='Stop'` turns native stderr INFO into errors → use 'Continue'.
- PSC3M6 flash erased state reads 0x00.

## Task 1 review gaps (closed 2026-11-11, see results/boot_evidence/)
- Rollback evidence now saved (flash readback before/after), header/protected-TLV/signature tamper tests added (all rejected),
  AN243370/AN243376 quoted in report, KEY_EXCHANGE/FINISH sizes added. Tool: `tools/boot_neg_metadata.ps1`
  (params -Good/-Rollback/-Prefix → reuse for ML-DSA-87 in Task 2). `tools/mcuboot_info.py` prints header/TLV addresses.
- CORRECTION: in DEVELOPMENT LCS ownership transfer / OEM policy provisioning is REVERSIBLE (AN243370 2.1.1/2.2.1);
  only LCS transition is irreversible. Real blocker for BootROM→EPBL authentication: needs an Infineon EPSS-signed OEM
  development certificate (OSTS account, CE243041 docs/ownership_transfer.md). Device has no OEM key, SIMPLE_APP policy.
- `edgeprotecttools set-ocd` is now set to native J-Link (`-n jlink`); `device-info --probe-id 591239652` works.
- Still open (do in Task 3 for P2 and P4 together): modified on-device certificate, modified handshake message
  (add a responder fault-injection build flag).

## Task 2 summary (done)
- EPBL feature_config: ML-DSA-87, ml_dsa_sig_hash=SHA512 (baseline copy: feature_config_ECDSA-384_baseline.json).
- App signer: new `IMAGE_SHA` / prebuild `--sha` (must match EPBL hash). boot_tests.ps1 `-Sha sha512 -NoFlash`.
- `tools/task2_boot_tests.ps1` = full matrix (P1-P4, N1-N10); `tools/epbl_ram_peak.ps1` = EPBL stack painting.
- Results: 4/4 PASS, 11/11 REJECTED; verify 61 ms (ECDSA 170); EPBL 47,244 B flash, peak RAM 25.2 KB.
- Limitation L1: older signed image written directly to primary slot boots (SW downgrade only) → HW counter + DAP.
- `make qprogram` does NOT rebuild – always `make build` first after changing feature_config.

## Old plan for Task 2 (kept for reference)
1. EPBL feature_config: `validation_key_type=ML-DSA-87`, `ml_dsa_sig_hash` (docs say SHA256; CNSA 2.0
   prefers SHA-384/512 – test SHA384/SHA512), key `.../keys/ml_dsa_87_img_sign_key_pub.der`; rebuild + program EPBL.
2. `boot_tests.ps1 -KeyType ML-DSA-87 -Key ../keys/ml_dsa_87_img_sign_key_priv.der` for B1–B5 (good, corrupt,
   rogue ML-DSA key, rollback, valid update) + measure verify time / EPBL size vs ECDSA baseline.
3. Document BootROM (ECDSA, classical) vs EPBL (ML-DSA-87) split; CNSA 2.0 mapping (ML-DSA-87 = CNSA 2.0 signature).
4. Stretch: put the SPDM responder behind EPBL (memory-map conflict: responder 288 KB vs 128 KB slot; PAS overlaps
   upgrade slot) – document if not feasible.
Next = Task 3: provisioning app → ml-dsa-87, re-provision (Quick Start command, no --freeze), responder SPDM_PROFILE=P4,
`task1_spdm_runs.ps1 -Profile P4`.

## Git (2026-11-11)
- Work is committed on branch `pqc-task1-task2` (not merged to main). `Instruction.pdf` (confidential) and downloaded
  libraries (`mtb_shared/`, `epbl/mtb_shared/`) are intentionally not committed.
- `psoc-spdm-responder` is a nested repo (internal GitLab); outer repo stores only a pointer. Responder changes are kept
  as `patches/psoc-spdm-responder_task1_perf.patch` – regenerate it after Task 3 responder changes (see patches/README.md).
- Rollback tests: `boot_capture.ps1 -JLinkCmds` runs load + readback + reset in the capture session (full boot log).

## Task 3 summary (done)
- Provisioning app PROV_SIG_SUITES ?= ml-dsa-87; `tools/task3_provision_p4.ps1` (no --freeze); responder common.mk
  SPDM_PROFILE ?= P4 (default now P4).
- Positive: 8/9 PASS; exchange_2 aborted at last KEY_EXCHANGE_RSP chunk (SPDM 1.0 error = responder lost state,
  UART capture died same second, cause undetermined, failed closed). Negative: 12/12 rejected, R1/R2 reconnect PASS.
- Fault injection: `make build SPDM_FAULT_INJECT=1..6` (test only; banner `[FI]`). Runner `tools/task3_neg_tests.ps1`
  (~80 min: every requester op re-fetches the 15 KB chain at ~1.4 s per 127 B transfer).
- Measurements: ML-DSA sign 66–346 ms (P-384 84 ms), ML-KEM encaps 11 ms (ECDHE 163 ms), KEX rsp 6,305 B,
  NS .bss 57.8 KB. `tools/perf_summary.py <run dir>` summarises [PERF].
- Long runs: Claude Code may kill background jobs under PC memory pressure (happened at R3) → keep the PC lightly loaded.
- Patch: `patches/psoc-spdm-responder_task3.patch` (cumulative).
- Verification pass: `tools/tests/test_task3_offline.py` (50 checks PASS), edge runs E1/E2/E3/E5 PASS
  (`tools/task3_edge_tests.ps1`). Phases PROV (provisioning rejections + SHA-256 RootHash), P2FI (ECC tamper builds),
  RESTORE are implemented but NOT run → use PROV in Task 4 (credential update); it erases the identity, so run
  RESTORE afterwards.
- Gap closing (17:50-18:40): P2FI run → ECC baseline 6/6 tamper REJECTED + clean ECC exchange PASS
  (`results/task3_EDGE_P2FI_20261111_1753`); RESTORE run → 3/3 P4 exchanges + get-cert PASS
  (`results/task3_EDGE_RESTORE_20261111_1805`), exchange_2 fault not reproduced. Offline suite 58 OK.
  Still open (documented): PROV phase (provisioning rejections → Task 4), E4 message sizes.
