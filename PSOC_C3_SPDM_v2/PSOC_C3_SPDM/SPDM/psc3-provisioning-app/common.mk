################################################################################
# \file common.mk
# \version 1.0
#
# \brief
# Settings shared across all projects.
#
################################################################################
# \copyright
# (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
# Technologies AG.  SPDX-License-Identifier: Apache-2.0
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

MTB_TYPE=PROJECT

# Target board/hardware (BSP).
# To change the target, it is recommended to use the Library manager
# ('make library-manager' from command line), which will also update Eclipse IDE launch
# configurations.
TARGET=APP_KIT_PSC3M6_EVAL

# Name of toolchain to use. Options include:
#
# GCC_ARM -- GCC provided with ModusToolbox software
# ARM     -- ARM Compiler (must be installed separately)
# IAR     -- IAR Compiler (must be installed separately)
#
# See also: CY_COMPILER_PATH below
TOOLCHAIN=GCC_ARM

# Map the selected TARGET to exactly one device-guard macro. Device-specific
# source (e.g. the secure TrustZone bring-up in proj_cm33_s/main.c) is gated on
# these macros. PSC3M5 is the odd one out: it hand-configures SAU + PPC in
# firmware, whereas PSC3M6/PSC3M8 (and later devices) have SAU/MPC/PPC generated
# by the Device Configurator (design.modus) and applied inside cybsp_init(). Add
# one line per new board; the positive DEVICE_PSC3M5 guard keeps the manual
# bring-up compiled in only where it is needed. PSC3M8 is a multicore part but
# only its main CM33 (TrustZone) core is used here, so it tracks PSC3M6.
ifeq ($(TARGET),APP_KIT_PSC3M5_EVK)
DEVICE_GUARD=DEVICE_PSC3M5
else ifeq ($(TARGET),APP_KIT_PSC3M6_EVAL)
DEVICE_GUARD=DEVICE_PSC3M6
else ifeq ($(TARGET),APP_KIT_PSC3M8_EVK)
DEVICE_GUARD=DEVICE_PSC3M8
endif
ifneq ($(DEVICE_GUARD),)
DEFINES+=$(DEVICE_GUARD)
endif

# Auto-detect PLATFORM from TARGET so each device pins its own dependency
# versions (see platforms/$(PLATFORM)/deps, selected via CY_GETLIBS_DEPS_PATH in
# the project Makefiles). Each platforms/*/detect_platform.mk sets PLATFORM for
# its own TARGET list; add a new board there rather than editing this file.
-include $(wildcard ../platforms/*/detect_platform.mk)
ifndef PLATFORM
$(error Unrecognized TARGET='$(TARGET)'. Add it to a platforms/*/detect_platform.mk to set PLATFORM)
endif

# ---------------------------------------------------------------------------
# Signature-suite selection (build-time opt-in).
#
# PROV_SIG_SUITES picks which provisioning signature suites are compiled into
# the firmware. It accepts any combination of:
#     ecdsa-p256   ECDSA secp256r1 / SHA-256  (PSA + mbedTLS; PSC3M5/M6/M8)
#     ecdsa-p384   ECDSA secp384r1 / SHA-384  (PSA + mbedTLS; PSC3M5/M6/M8)
#     ecdsa-p521   ECDSA secp521r1 / SHA-512  (PSA + mbedTLS; PSC3M5/M6/M8)
#     ml-dsa-87    ML-DSA-87 PQC / FIPS 204    (wolfCrypt; PSC3M6/PSC3M8 only)
# Examples:
#     make PROV_SIG_SUITES="ecdsa-p256"             # classical only (default)
#     make PROV_SIG_SUITES="ecdsa-p384"             # P-384 only
#     make PROV_SIG_SUITES="ecdsa-p256 ecdsa-p521"  # P-256 + P-521
#     make PROV_SIG_SUITES="ml-dsa-87"              # PQC only  (M6/M8)
#     make PROV_SIG_SUITES="ecdsa-p256 ml-dsa-87"   # both      (M6/M8)
#
# Each selected suite exports a PROV_ENABLE_* make variable AND a matching
# compile define; prov_svc.c advertises exactly the enabled suites in
# PROV_GET_INFO, and proj_cm33_s/Makefile wires the wolfCrypt sources only when
# ml-dsa-87 is enabled. Defaulting to ecdsa-p256 keeps pre-PQC builds byte-for-
# byte unchanged. Note: only P-256 is hardware accelerated (MXCRYPTOLITE);
# P-384/P-521 fall back to software mbedTLS (larger secure .text, slower).
# ---------------------------------------------------------------------------
# Task 1 (ECC baseline): ecdsa-p384. Task 3 (PQC): set back to ml-dsa-87.
# "ecdsa-p384 ml-dsa-87" together overflows the 128 KB secure flash by ~1 KB (Debug).
PROV_SIG_SUITES ?= ecdsa-p384

ifneq ($(filter ecdsa-p256,$(PROV_SIG_SUITES)),)
PROV_ENABLE_EC256 := 1
DEFINES += PROV_ENABLE_EC256
endif

ifneq ($(filter ecdsa-p384,$(PROV_SIG_SUITES)),)
PROV_ENABLE_EC384 := 1
DEFINES += PROV_ENABLE_EC384
endif

ifneq ($(filter ecdsa-p521,$(PROV_SIG_SUITES)),)
PROV_ENABLE_EC521 := 1
DEFINES += PROV_ENABLE_EC521
endif

ifneq ($(filter ml-dsa-87,$(PROV_SIG_SUITES)),)
ifeq ($(filter $(TARGET),APP_KIT_PSC3M6_EVAL APP_KIT_PSC3M8_EVK),)
$(error PROV_SIG_SUITES: ml-dsa-87 is supported on PSC3M6/PSC3M8 only (TARGET=$(TARGET)). Build with TARGET=APP_KIT_PSC3M6_EVAL or TARGET=APP_KIT_PSC3M8_EVK, or drop ml-dsa-87.)
endif
PROV_ENABLE_MLDSA87 := 1
DEFINES += PROV_ENABLE_MLDSA87
endif

ifeq ($(PROV_ENABLE_EC256)$(PROV_ENABLE_EC384)$(PROV_ENABLE_EC521)$(PROV_ENABLE_MLDSA87),)
$(error PROV_SIG_SUITES must contain at least one of: ecdsa-p256 ecdsa-p384 ecdsa-p521 ml-dsa-87 (got '$(PROV_SIG_SUITES)'))
endif

# Secure SRAM raised to 32 KB (0x8000) to host the on-device PSA crypto + X.509
# CSR provisioning service (secure .bss ~13 KB + 4 KB stack + mbedTLS heap).
# Secure flash set to 100 KB (0x19000) to hold the PSA/mbedTLS code (secure
# .text ~81 KB); the NS app (~37 KB) keeps the remaining 148 KB (100 KB .. 248 KB,
# with the top 8 KB reserved for the PAS). SECURE_SRAM_SIZE/SECURE_FLASH_SIZE
# below are the single source of truth: they feed the linker (_size_S_*) here and
# proj_cm33_s/Makefile reads them for USER_*_S_SIZE. Keep the MPC RAM & flash
# splits in ns_policy/policy_oem_provisioning.json in sync (re-provision after
# changing). Total on-chip SRAM is 64 KB; total flash is 256 KB.
#
# PSC3M5 only: on PSC3M6 the memory map is owned entirely by the Device
# Configurator (design.modus -> generated cymem_*.ld / partition_psc3.h), so
# these knobs are defined-but-unused there and are guarded out to avoid a second
# (conflicting) source of truth. _size_S_* feeds the linker; USER_*_S_SIZE feeds
# partition_psc3.h -> TZ_SAU_Setup(), which is itself M5-only.
ifeq ($(TARGET),APP_KIT_PSC3M5_EVK)
SECURE_SRAM_SIZE=0x8000
SECURE_FLASH_SIZE=0x19000

ifeq ($(TOOLCHAIN),IAR)
LDFLAGS+=--config_def _size_S_SRAM=$(SECURE_SRAM_SIZE) --config_def _size_S_FLASH=$(SECURE_FLASH_SIZE)
else ifeq ($(TOOLCHAIN),GCC_ARM)
LDFLAGS+=-Wl,--defsym=_size_S_SRAM=$(SECURE_SRAM_SIZE),--defsym=_size_S_FLASH=$(SECURE_FLASH_SIZE)
else ifeq ($(TOOLCHAIN),ARM)
LDFLAGS+=--predefine="-D_size_S_SRAM=$(SECURE_SRAM_SIZE)" --predefine="-D_size_S_FLASH=$(SECURE_FLASH_SIZE)"
endif
endif

# Post-build combine (PSC3M6 and later). Each project is linked at its C-BUS
# execution alias, but the flash programmer needs the S-BUS programmable alias.
# This recipe relocates each project hex C-BUS->S-BUS and merges them into a
# single build/app_combined.hex; the {{ CYMEM_* }} tokens are resolved from the
# secure ELF so the addresses track the generated M6 memory map. On PSC3M5 each
# project is already linked at its S-BUS alias and the build system merges them
# automatically, so no combine recipe is needed there.
ifneq ($(TARGET),APP_KIT_PSC3M5_EVK)
COMBINE_SIGN_JSON ?= configs/signer_combiner.json
endif

# Default build configuration. Options include:
#
# Debug -- build with minimal optimizations, focus on debugging.
# Release -- build with full optimizations
# Custom -- build with custom configuration, set the optimization flag in CFLAGS
# 
# If CONFIG is manually edited, ensure to update or regenerate launch configurations 
# for your IDE.
CONFIG=Debug

include ../common_app.mk
