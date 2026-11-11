# Task 2 – Boot-time PQC protection (EPBL with ML-DSA-87) – measured 2026-11-11

Report: `results/Task2_PQC_Boot_Report.pdf` (generator `tools/make_task2_report.py`).
Evidence: `results/task2_boot/` (boot logs, tampered images, flash readbacks, `summary_ML-DSA-87.txt`).

## Configuration
| Item | ECC baseline (Task 1) | PQC (Task 2) |
|---|---|---|
| EPBL `validation_key_type` | ECDSA-384 | **ML-DSA-87** (FIPS 204, category 5) |
| Signature mode | ECDSA over SHA-384 image hash | **HashML-DSA** (FIPS 204 §5.4) over **SHA-512** image hash (`ml_dsa_sig_hash=SHA512`) |
| Image hash TLV | SHA-384 (0x11, 48 B) | SHA-512 (0x12, 64 B) |
| Key-hash TLV | 48 B | 64 B |
| Signature TLV | ECDSA_SIG 0x22, 103 B DER | ML-DSA-87 0x105, **4,627 B** raw |
| TLV area total | 215 B | 4,771 B |
| Validation public key (embedded in EPBL flash as C array) | P-384, 120 B DER | ML-DSA-87, 2,592 B raw (2,614 B DER) |
| Rollback | `sw_downgrade_prev=true` (overwrite mode) | same |
| Configs | `epbl/bl/platforms/PSC3_M6/feature_config_ECDSA-384_baseline.json` | `feature_config_ML-DSA-87.json` (active `feature_config.json`) |

Changes made:
- EPBL `feature_config.json` → ML-DSA-87, key `keys/ml_dsa_87_img_sign_key_pub.der`, `ml_dsa_sig_hash=SHA512`.
- CE243041 `configs/prebuild.py` was hard-wired to SHA-256 for ML-DSA → added `--sha` override; Makefile variable
  `IMAGE_SHA` (empty = old default). Must equal the EPBL hash, otherwise the image is rejected (test N9).
- `tools/boot_tests.ps1`: new `-Sha` and `-NoFlash`; new `tools/task2_boot_tests.ps1` (full matrix),
  `tools/epbl_ram_peak.ps1` (EPBL stack/heap painting via J-Link).
- Rogue key for N7: `keys/rogue_ml_dsa_87_{priv,pub}.der` (edgeprotecttools create-key --key-type ml-dsa-87).

Why SHA-512 and not the documented default SHA-256: CNSA 2.0 allows only SHA-384/SHA-512; a 256-bit pre-hash gives
128-bit collision resistance, below ML-DSA-87's category 5. Note the EPBL Makefile already defaults ML-DSA-87 to
SHA-512 when the field is empty while the CE243041 signer defaults to SHA-256 → an empty field gives a
non-booting image. Set both explicitly.

## Test matrix (all on hardware, `task2_boot/summary_ML-DSA-87.txt`)
| # | Case | Expected | Result | EPBL evidence |
|---|---|---|---|---|
| P1 | authorized v1.0.0 (ML-DSA-87, SHA-512) | boot | BOOTED 1.0.0+0 | validate returned 0x742e |
| N1 | payload byte flipped @0x3201C000 | reject | REJECTED | IMAGE_TLV_SHA is invalid |
| N2 | MCUboot header version byte @0x3201B014 | reject | REJECTED | IMAGE_TLV_SHA is invalid |
| N3 | protected TLV byte @0x3202546C | reject | REJECTED | IMAGE_TLV_SHA is invalid |
| N4 | key-hash TLV byte @0x32025559 | reject | REJECTED | returned 0x2e (no matching key) |
| N5 | byte inside ML-DSA-87 signature @0x32025985 | reject | REJECTED | hash OK, ML-DSA verify fails (at 120 ms) |
| N6 | malformed: signature TLV length 4627→4611 @0x3202559B | reject | REJECTED | returned 0xffffffff |
| N7 | complete image signed with rogue ML-DSA-87 key | reject | REJECTED | returned 0x2e |
| N8 | ECDSA-P384-signed image (classical downgrade) | reject | REJECTED | no fallback to ECDSA |
| N9 | ML-DSA-87 signed with SHA-256 pre-hash (non-compliant) | reject | REJECTED | returned 0xffffffff |
| P2 | restore authorized v1.0 | boot | BOOTED 1.0.0+0 | |
| N11 | image signed with ML-DSA-65 (unsupported parameter set) | reject | REJECTED | returned 0x2e |
| N10 | rollback: signed v0.9.0 in upgrade slot | reject | REJECTED (v1.0 keeps running) | `[ERR] insufficient version in secondary slot` at 47 ms (re-run, `summary_ML-DSA-87_rerun_N10.txt`, complete boot log); readback: upgrade slot 96F3B83D/ver 0.9 before → all 0x00 after, primary still ver 1 |
| P3 | valid update v2.0.0+1 in upgrade slot | install | BOOTED 2.0.0+1 | verify secondary, erase, copy 128 KB, verify primary; readback primary ver 2 |
| P4 | reboot after update | boot | BOOTED 2.0.0+1 | |
| L1 | (limitation) signed v0.9 written directly to primary slot via debug | – | **BOOTS 0.9.0** | SW downgrade prevention only guards the update path |

L1 consequence: anti-rollback against an attacker with flash/debug access needs `hw_rollback_prot` (eFuse NV counter,
irreversible – not enabled on the shared board) **and** closed debug access (AN243370 §9.3.6).

## Measurements (EPBL log timestamps, same board, Debug EPBL build, logging = debug)
| Metric | ECDSA-P384 | ML-DSA-87 (SHA-512) | Delta |
|---|---:|---:|---:|
| Signature verification | ≈170 ms (59→229 ms) | **≈61 ms** (59→120 ms) | −109 ms |
| boot_validate_slot total (hash + key hash + signature) | ≈179 ms | ≈69 ms | −110 ms |
| Reset → "Starting User Application" | ≈266 ms | **≈146 ms** | −120 ms |
| Reset → app with update install (erase + copy 128 KB + 2 verifications) | ≈3.80 s (log 3s.802) | ≈3.57 s (log 3s.573) | −0.23 s |
| EPBL flash (.text + .data) | 35,888 B | **47,244 B** | +11,356 B (of 110,592 B EPBL slot: 43 %) |
| EPBL .data + .bss (static RAM) | 6,948 B | 19,044 B | +12,096 B (static MlDsaKey ≈12.5 KB) |
| EPBL peak stack during full verification (painted) | 3,752 B | **6,120 B** | +2,368 B (of 16 KB) |
| EPBL heap touched | 120 B | 120 B | 0 |
| Peak boot RAM (static + stack) | ≈10.7 KB | **≈25.2 KB** | +14.5 KB (of 54 KB EPBL RAM region) |
| Image metadata (TLV area) | 215 B | 4,771 B | +4,556 B per image (both slots) |

Why ML-DSA is faster here: P-384 ECDSA runs in software mbedTLS (only P-256 is CryptoLite-accelerated); ML-DSA
verify is NTT arithmetic + SHAKE in wolfCrypt (VERIFY_SMALL_MEM). Correction to Task 1 report: update install took
≈3.8 s reset→app (Task 1 said "≈2.5–3 s"; the 2.5 s was the erase phase only).

## Boot chain / who verifies what
```
Reset → ROM_BOOT (immutable, Infineon) validates SFLASH / FLASH_BOOT          classical, Infineon keys
      → FLASH_BOOT: SIMPLE_APP policy (DEVELOPMENT LCS, no OEM key) → launches EPBL WITHOUT authentication
          (with SECURE_APP policy: authenticates EPBL with OEM_ROT_KEY, ECDSA P-256/384/521 only)
      → EPBL @0x32000000: authenticates primary slot @0x3201B000 with ML-DSA-87 on every boot,
          and the upgrade slot @0x32040000 before installing (plus version check) and again after copying
      → application
```
- BootROM is NOT assumed to do ML-DSA (AN243370 §1.3: BootROM secure boot = ECDSA; PQC needs EPB as first OEM app).
- ML-DSA-87 validation key: embedded in EPBL flash as C array (`generated/ml_dsa_sign_key.c`); `sflash_keys` is
  not available for ML-DSA (EPBL Makefile error). Its integrity therefore depends on EPBL integrity → needs BootROM
  authentication of EPBL (ECDSA OEM key) + closed DAP in production.

## CNSA 2.0 mapping (design constraint, no certification claim)
| Function | ECC baseline | CNSA 2.0 requirement | Selected |
|---|---|---|---|
| Firmware/software signature (EPBL→app) | ECDSA-P384 (quantum-vulnerable) | ML-DSA-87 (all uses) or LMS/XMSS (SP 800-208, firmware signing) | **ML-DSA-87** |
| Hash | SHA-384 | SHA-384 or SHA-512 | **SHA-512** (image hash + HashML-DSA pre-hash) |
| BootROM→EPBL | ECDSA (not enabled) | – | remains classical (ROM, immutable) |

ML-DSA-87 vs LMS: both are CNSA 2.0 for firmware signing and both are in EPBL. ML-DSA-87 chosen because it is
stateless (no signing-state management, AN243370 §9.3.6) and is the same family as the SPDM identity in Task 3
(one PQC algorithm on both planes). LMS would give a smaller verifier and is a drop-in alternative.

## Remaining classical dependencies / limits
1. ROM_BOOT/FLASH_BOOT → EPBL: unauthenticated now; when enabled it is ECDSA (immutable ROM) → classical root.
2. EPBL is not field-updatable in this memory map; ML-DSA key rotation = reflash EPBL (or a new EPBL via an
   ECDSA-authenticated BootROM path).
3. SW downgrade prevention only (L1); HW counter needs eFuse.
4. Debug port open (DEVELOPMENT LCS) → flash patching possible; production must close DAP.
5. SPDM responder (288 KB, own map, PAS @0x32048000 overlapping the upgrade slot) is not yet behind EPBL – see
   Task 4 notes. Boot tests use the CE243041 application.

## Reproduce
```
. tools\mtbenv.ps1
copy epbl\bl\platforms\PSC3_M6\feature_config_ML-DSA-87.json epbl\bl\platforms\PSC3_M6\feature_config.json
& $make -C epbl/bl build ; & $make -C epbl/bl qprogram          # qprogram does NOT rebuild
tools\boot_tests.ps1 -Kind boot   -Major 1 -Minor 0 -Tag v1_0              -KeyType ML-DSA-87 -Key ../keys/ml_dsa_87_img_sign_key_priv.der -Sha sha512 -NoFlash
tools\boot_tests.ps1 -Kind boot   -Major 1 -Minor 0 -Tag rogue_key         -KeyType ML-DSA-87 -Key ../keys/rogue_ml_dsa_87_priv.der      -Sha sha512 -NoFlash
tools\boot_tests.ps1 -Kind update -Major 0 -Minor 9 -Tag v0_9_rollback     -KeyType ML-DSA-87 -Key ../keys/ml_dsa_87_img_sign_key_priv.der -Sha sha512 -NoFlash
tools\boot_tests.ps1 -Kind update -Major 2 -Minor 0 -Build 1 -Tag v2_0_valid_update -KeyType ML-DSA-87 -Key ../keys/ml_dsa_87_img_sign_key_priv.der -Sha sha512 -NoFlash
tools\boot_tests.ps1 -Kind boot   -Major 1 -Minor 0 -Tag v1_0_sha256       -KeyType ML-DSA-87 -Key ../keys/ml_dsa_87_img_sign_key_priv.der -Sha sha256 -NoFlash
tools\task2_boot_tests.ps1
tools\epbl_ram_peak.ps1 -Image results\task2_boot\neg_ML-DSA-87_N5_signature_byte.hex -Tag mldsa87
```
