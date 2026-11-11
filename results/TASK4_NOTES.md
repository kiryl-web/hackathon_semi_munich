# Task 4 – Lifecycle connection (boot-time + real-time PQC as one device lifecycle) – 2026-11-11

Report: `results/Task4_Lifecycle_Report.pdf` (generator `tools/make_task4_report.py`).
Evidence: `results/task4_EPBL_SPDM_*` (firmware update / boot chain + SPDM), `results/task4_EDGE_PROV_20261111_1903`
(credential rejections), `results/task4_EDGE_RESTORE_20261111_1912` (credential rotation), `results/task4_boot/` (signed images).

## What changed vs. Task 2/3
Task 2 verified the CE243041 demo app with EPBL; Task 3 ran the SPDM responder standalone at 0x32000000 (no EPBL).
Task 4 puts the **real SPDM responder behind EPBL**, so the device that answers SPDM is the image EPBL verified.

Flash map (512 KB, PSC3M6):
| Area | Range | Content |
|---|---|---|
| EPBL | 0x32000000–0x3201B000 (108 KB) | Edge Protect Bootloader, ML-DSA-87 key embedded |
| Primary slot | 0x3201B000–0x32048000 (180 KB) | MCUboot header 0x400 + responder S (0x1B400) + NS (0x34000) + TLVs |
| PAS | 0x32048000–0x3204E000 (24 KB) | SPDM identity (ML-DSA-87 key slot 0, cert chain slot 1) – **untouched by updates** |
| Upgrade slot | 0x3204E000–0x3207B000 (180 KB) | candidate image (overwrite mode) |

Build: `make -C psoc-spdm-responder build TARGET=APP_KIT_PSC3M6_EVAL_EPBL SPDM_PROFILE=P4` (new BSP variant; standalone
`TARGET=APP_KIT_PSC3M6_EVAL` unchanged). EPBL: `make -C epbl/bl build MEMORY_MAP=<abs>/epbl/bl/platforms/PSC3_M6/memory_maps/spdm_overwrite_single_flash.json`.
Signing: `tools/resp_epbl_sign.py` (merges S + NS relocated to the S-bus alias, gap = 0x00, ML-DSA-87, SHA-512, raw sig,
key-hash TLV, SecureApp boot record, overwrite-only, padded). Runner: `tools/task4_epbl_spdm_tests.ps1 -Phase PREP|BOARD`.

Code/config changes (responder patch `patches/psoc-spdm-responder_task4.patch`, cumulative; BSP overlay
`patches/bsp_overlay_TARGET_APP_KIT_PSC3M6_EVAL_EPBL/` because `bsps/` is git-ignored in the nested repo):
- BSP variant: design.modus regions m33_nvm 0x1B000/0x19000, m33ns_nvm 0x34000/0x14000 (PAS unchanged), regenerated with
  `device-configurator-cli --build design.modus --library <mtb-dsl-psc3m6>/props.json,<~/.modustoolbox/global/device-db/...>/props.json`
  (MPC config follows the regions); `bsp.mk`: `MCUBOOT_HEADER_SIZE=0x400` for the secure project only.
- `common.mk`: TARGET list + `SEARCH_TARGET_*` (both targets build without re-running getlibs); `detect_platform.mk`.
- `proj_cm33_s/Makefile`, `proj_cm33_ns/Makefile`: `NSC_VENEER=../build/nsc_veneer_$(TARGET).o` (the shared veneer import
  lib pinned stale NSC addresses -> "can not size stub section" when switching targets).
- `proj_cm33_s/core/spdm_meas.c`: **real measurements** in the EPBL build (placeholders kept for standalone):
  block 1 = SHA-384 of the EPBL area, block 2 = SHA-384 of header+image+protected TLVs of the running image (exactly what
  EPBL hashed before verifying the ML-DSA-87 signature), block 3 = SVN = MCUboot version (maj<<24|min<<16|rev),
  block 4 = "PSC3M6-SPDM-P4 fw x.y.z+b EPBL". Computed once in the SPE, NS-marked flash read via the NS alias + bounce buffer.
- Host verifier `tools/task4_verify_meas.py`: recomputes blocks 1–3 from `epbl_full.hex` + the signed image and compares
  with the signed GET_MEASUREMENTS output.

## Hardware results (`results/task4_EPBL_SPDM_20261111_2059`, 18/18 as expected)
| Step | Result | Evidence |
|---|---|---|
| T0 boot v1.0.0 | BOOTED | EPBL ML-DSA-87 verify 80 ms, reset->responder 157 ms; responder loads 15,187 B chain from PAS |
| T0 measure / exchange | PASS / PASS | blocks 1-3 MATCH reference values |
| T1 valid update v1.1.0 | UPDATED | verify secondary, erase 3.55 s, copy 0x2D000 1.15 s, verify primary; reset->app 4.95 s |
| T1 measure / exchange | PASS / PASS | SVN 1.1.0, new digest, MATCH; same identity (no re-provisioning) |
| T2 rollback v1.0.0 (genuine key) | REJECTED | `[ERR] insufficient version in secondary slot` |
| T3 corrupt payload byte | REJECTED | validate 0xffffffff (SHA-512 mismatch) |
| T4 modified header (revision) | REJECTED | validate 0xffffffff |
| T5 rogue ML-DSA-87 key | REJECTED | validate 0x2e (no matching key hash) |
| T6 measure | PASS | still 1.1.0, MATCH -> no rejected image ran |
| T7 tampered primary | NOBOOT | `Image in the primary slot is not valid!`, handle_error; SPDM connect fails |
| T8 recovery update v2.0.0 | UPDATED | EPBL installs the signed update over the invalid primary |
| T8 measure / exchange | PASS / PASS | SVN 2.0.0, MATCH |
SPDM cost unchanged vs Task 3 (session 215 transfers / 28.3 KB). Host wall clock per op 6.5-11 min this run (dfuh-cli
~2-3.5 s per transfer; slower while builds ran in parallel).

Board state after the run: EPBL (spdm map, ML-DSA-87) + responder v2.0.0 (EPBL target) + PAS identity from the
task4 RESTORE (artifacts: provisioning-host `test/_work/edge_p4`, same ca_mldsa87 root). Ready for the demo.

## Answers to the brief (details in the PDF)
- SPDM identity anchored in BOTH: separately provisioned key (PAS) + verified-boot state in signed measurements.
- Classical: BootROM/FLASH_BOOT -> EPBL (ECDSA OEM key, not enabled here: SIMPLE_APP). PQC: EPBL->app, all SPDM.
- Migration ECC->PQC: firmware update via ECDSA EPBL -> EPBL key change (BootROM-authenticated in production) ->
  on-device ML-DSA-87 keygen, CSR bridged by the ECC identity -> P4-only profile -> drop ECC root -> freeze/close DAP.
- Limits: debugger-delivered updates, test images differ only in version, SW downgrade prevention only, PAS not frozen.

## Credential update path (PAS, provisioning service)
- Rotation (positive): new on-device ML-DSA-87 key + CSR + CA-signed leaf (same ML-DSA-87 root, SHA-384 RootHash),
  then encrypted SPDM exchanges: R_restore_exchange PASS, R_repeat_exchange_2 PASS (3rd repeat cut by a PC power-off).
- Rejections (task4_EDGE_PROV): V3 gen-key ML-DSA-65 -> PROV_ERR_ALG_UNSUPPORTED; V4 gen-key ECDSA-P384 (not built in)
  -> PROV_ERR_ALG_UNSUPPORTED; V6 chain > 15,360 B slot -> PROV_ERR_LENGTH; V7 leaf with a foreign key: stored (device
  does not parse X.509, design D4) but **prove-cert fails** (signature verification failed) -> provisioning must gate
  on prove-cert, and SPDM CHALLENGE would fail as well; V9 chain with SHA-256 RootHash vs negotiated SHA-384 ->
  requester get_certificate EAUTH.
- Finding V5b: gen-key on an occupied key slot is ACCEPTED (key silently replaced). By design while the PAS is not
  frozen (frozen device -> PROV_ERR_FROZEN, see provisioning-app self-test). Consequence: credential immutability
  depends on `--freeze` + closed debug port + the provisioning image never being bootable in the field.
