# Task 3 – Real-time PQC protection with SPDM (measured 2026-11-11)

Report: `results/Task3_PQC_SPDM_Report.pdf` (generator `tools/make_task3_report.py`, reads the run dirs below).
Positive run: `results/task3_P4_20261111_1449/`; negative run: `results/task3_NEG_P4_20261111_1533/`;
ECC baseline for comparison: `results/task1_P2_20261111_1215/`.

## Configuration (P2 → P4)
| Item | P2 (Task 1 baseline) | P4 (Task 3) |
|---|---|---|
| SPDM version | 1.2/1.3 classic | **1.4** |
| Device identity / signatures | ECDSA P-384 | **ML-DSA-87** (FIPS 204, cat 5), FIPS 204 context = SPDM op string |
| Key establishment | ECDHE P-384 | **ML-KEM-1024** (FIPS 203, cat 5), responder encapsulates in the SPE |
| Hash / AEAD | SHA-384 / AES-256-GCM | SHA-384 / AES-256-GCM |
| Cert chain | P-384 root → leaf, 1,218 B | ML-DSA-87 root (7,578 B) → leaf (7,557 B), **15,187 B** |
| Classical algorithms offered | – | none (BaseAsym = none, DHE = none) → no downgrade |

CNSA 2.0: only ML-DSA-87 + ML-KEM-1024 (+ SHA-384/512, AES-256) qualify; ML-DSA-65/ML-KEM-768 do not (test A2).

## Steps / changes
- Provisioning app `common.mk`: `PROV_SIG_SUITES ?= ml-dsa-87` (built Debug, programmed, no full erase).
- `tools/task3_provision_p4.ps1`: erase slots 0/1, on-device ML-DSA-87 keygen, CSR, CA sign (ca_mldsa87), put-cert
  `--spdm-chain --hash-alg sha384`, read-back, prove-cert. 41 s. **No `--freeze`.**
- Responder `common.mk`: `SPDM_PROFILE ?= P4` (default now P4; P2 baseline via `make ... SPDM_PROFILE=P2`, clean first).
- Responder test-only fault injection `SPDM_FAULT_INJECT=1..6` (`proj_cm33_ns/spdm/spdm_fault_inject.h`, Makefile
  default 0 = compiled out; FI image prints `[FI] FAULT-INJECTION BUILD, mode N` and `[FI] ...` per tamper).
  1 cert CN, 2 signature byte, 3 transcript hash bit (signed), 4 ML-KEM ciphertext byte (before signing),
  5 AEAD tag of 1st secured rsp (FINISH_RSP), 6 AEAD tag of 2nd secured rsp (app data).
- Patch: `patches/psoc-spdm-responder_task3.patch` (cumulative, supersedes task1 patch; verified `git apply --check`).
- Tools: `task1_spdm_runs.ps1 -Tag` (output dir prefix), `task3_make_neg_roots.py`, `task3_neg_tests.ps1`,
  `perf_summary.py` (per-operation transfers/bytes/device time from [PERF]).

## Results
Positive (P4): connect, slots, get-cert, challenge, measure, session, exchange_1, exchange_3 PASS;
**exchange_2 FAIL (SPDM_REQ_ESESSION)** at the last KEY_EXCHANGE_RSP chunk (transfer 212): requester got an SPDM 1.0
ERROR (UnsupportedRequest) = responder had no negotiated state (looks like a reset); UART capture stopped at the same
second (15:20:58) and recorded nothing afterwards; no Windows USB/PnP event → root cause undetermined. Failed closed.
Negative: 12/12 REJECTED (A0 classical root in the suite + A1–A5 anchors + B1–B6 fault images), reconnect R1/R2 PASS
after the tamper images. R3 interrupted by Claude Code (PC memory pressure stopped the background job) → no result.
| # | Tampering | Requester error |
|---|---|---|
| A0/A3 | classical ECDSA P-384 root | get_certificate EAUTH |
| A1 | rogue ML-DSA-87 root, same subject | EAUTH |
| A2 | ML-DSA-65 root | EAUTH |
| A4 | 1 byte flipped in root ML-DSA public key | EAUTH |
| A5 | truncated root DER | EAUTH |
| B1 | leaf CN changed on device | get_certificate EAUTH |
| B2 | ML-DSA signature byte flipped | challenge EAUTH |
| B3 | valid signature over modified transcript | start_session ESESSION |
| B4 | ML-KEM ciphertext modified before signing (sig valid, implicit rejection → key confirmation fails) | ESESSION |
| B5 | MITM AEAD tag FINISH_RSP | ESESSION |
| B6 | MITM AEAD tag app data | send_receive ESESSION |

## Measurements (device, µs from [PERF])
| | P2 | P4 |
|---|---:|---:|
| Signature (SPE) | 84.3–84.5 ms | **66–346 ms** (median 112, rejection sampling) |
| ECDHE keygen+shared / ML-KEM encaps | 162.9 ms | **11.1 ms** |
| KEY_EXCHANGE message | 252 ms | 87–315 ms (median 132) |
| FINISH | 5.0 ms | 5.9 ms |
| App message (AEAD) | 1.2 ms | 1.9 ms |
| KEY_EXCHANGE req / rsp | 166 / 302 B | 1,638 / 6,305 B |
| Largest message | 1,226 B | 7,168 B (CERTIFICATE portion, = max msg size) |
| get-cert transfers / bytes | 17 / 1,769 B | 142 / 18,490 B |
| session transfers / bytes | 24 / 2,569 B | 214 / 28,199 B |
| Host wall clock `session` | 38 s | 375 s (dfuh-cli ≈1.4 s per 127 B transfer; chain re-fetched per op) |
| NS .bss / S .bss | 19,952 / 11,016 B | 57,816 / 18,940 B |
| Peak S heap / S stack | 2,576 / 4,336 B | 27,720 / 2,608 B |
Biggest RAM objects P4: libspdm scratch 35,843 B, NS chain copy 16,384 B, context 3,328 B; S: wolfCrypt ML-DSA key
7,912 B, hash pool 9,280 B. PAS cert slot 15 KB is 99 % full (no room for an intermediate CA).

## Open for Task 4
- Credential update / migration demo: ECC → ML-DSA re-provisioning is exactly what was done here (P2 slots → P4 slots,
  no freeze); show it as the migration path. Identity is separately provisioned (PAS), not bound to the boot chain.
- Board state now: provisioning-free **P4 responder (FI=0)** at 0x32000000 (EPBL overwritten), PAS = ML-DSA-87 identity.

## Verification pass (requested after the report, 2026-11-11 17:00–17:40)
- Offline suite `tools/tests/test_task3_offline.py` (stdlib unittest, no board): 50 checks PASS
  (`py -3.13 -m unittest discover -s tools/tests -v`). Build matrix with `RUN_BUILD_TESTS=1` (FI=0..6, ~4 min) PASS;
  it leaves the clean P4 FI=0 build in the build dir.
- Bugs found by the tests and fixed: `perf_summary.py` ran its CLI on import; crashed on a partial UART line.
- Hardware edge cases (`tools/task3_edge_tests.ps1 -Phase P4 [-Only E5]`; results `task3_EDGE_P4_20261111_1705` + `_1729`):
  E1a/E1b session PASS ×2; E2 requester killed mid KEY_EXCHANGE_RSP → E2b reconnect + exchange PASS;
  E3 empty slot 1 → rejected ("slot 1 is empty"); E5 J-Link reset → clean boot banner, challenge PASS.
- UART capture in the edge runner now re-opens the port and logs `[CAPTURE]` errors (lost evidence for exchange_2).
- NOT tested (dropped to save time): E4 app-message size limits; provisioning rejections (V-tests in the PROV
  phase) → do them in Task 4 (credential update).

## Gap closing (2026-11-11 17:50–18:40)
- Task 3 credential artifacts backed up to `results/task3_cred_p4_original` (before re-provisioning).
- Runner: provisioning-app builds now `-Clean` when the suite changes; RESTORE runs 3 exchanges (repeat check).
- **P2FI** (`results/task3_EDGE_P2FI_20261111_1753`): ECC identity provisioned, P2 FI images 1,2,3,5,6:
  **6/6 REJECTED** (B1 EAUTH, B2 EAUTH, B3/B5/B6 ESESSION, ML-DSA-87 root vs ECC device EAUTH), each with its
  `[FI]` line in the UART log; clean P2 exchange PASS. Closes the Task 1 "baseline tamper" item.
- **RESTORE** (`results/task3_EDGE_RESTORE_20261111_1805`): ML-DSA-87 identity re-provisioned (new on-device key,
  same ca_mldsa87 root, SHA-384), clean P4 FI=0 responder: **3/3 exchanges PASS** (385-393 s each) + get-cert PASS.
  exchange_2 fault did not reproduce (6 later full handshakes incl. E1a/E1b/E2b). Board is back in the Task 3 end state
  (identity artifacts of the new key: provisioning-host `test/_work/edge_p4`).
- Offline suite: 58 tests OK (3 skipped = build matrix). Report regenerated (summary rows + 4.2b/4.2c).
- Remaining limitations (documented, not blocking): E4 message-size limits; provisioning-side rejections → Task 4;
  root cause of the one-off exchange_2 transport fault unknown (fail-closed).
