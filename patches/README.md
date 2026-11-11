# Patches for nested repositories

`PSOC_C3_SPDM_v2/PSOC_C3_SPDM/SPDM/psoc-spdm-responder` is its own git repository (Infineon internal
GitLab); this repo stores only a pointer to it, so changes made there are kept here as patches.

| Patch | Base commit | Content |
|---|---|---|
| `psoc-spdm-responder_task1_perf.patch` | `4c218834dfd1bafa47914317dcaf4d6af978e31b` | Task 1 measurement instrumentation: `[PERF]` UART lines (NS SysTick timer, per-message time, secure-side crypto timing in device_secret.c / cryptlib ec.c / mlkem.c, stack painting, heap peak via NSC veneer), router per-transfer log; `common.mk`: `SPDM_PROFILE ?= P2`, `CONFIG=Release`, `SPDM_PERF_LOG ?= 1` |
| `psoc-spdm-responder_task3.patch` (cumulative, supersedes task1) | `4c218834dfd1bafa47914317dcaf4d6af978e31b` | Task 1 instrumentation **plus** Task 3 test-only fault injection: `proj_cm33_ns/spdm/spdm_fault_inject.h`, hooks in spdm_responder.c (cert), device_secret.c (signature, transcript), cryptlib/mlkem.c (ciphertext), spdm_transport_dfu.c (AEAD tag of FINISH_RSP / app data); `proj_cm33_ns/Makefile`: `SPDM_FAULT_INJECT ?= 0` (0 = hooks compiled out) |

Apply:
```
cd PSOC_C3_SPDM_v2/PSOC_C3_SPDM/SPDM/psoc-spdm-responder
git checkout 4c218834dfd1bafa47914317dcaf4d6af978e31b
git apply ../../../../patches/psoc-spdm-responder_task3.patch
```
Regenerate after further responder changes (from the responder folder):
`git diff --binary HEAD > patch` plus `git diff --no-index /dev/null <new file>` for new files.
