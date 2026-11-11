################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Makefile for configuring the Cortex-M33 core Secure project
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

include ../common.mk

################################################################################
# Basic Configuration
###############################################################################

# Name of application (used to derive name of final linked file).
#
# If APPNAME is edited, ensure to update or regenerate launch
# configurations for your IDE.
APPNAME=proj_cm33_s

# If set to "true" or "1", display full command-lines when building.
VERBOSE=


################################################################################
# Advanced Configuration
################################################################################

# Enable optional code that is ordinarily disabled by default.
#
# Available components depend on the specific targeted hardware and firmware
# in use. In general, if you have
#
#    COMPONENTS=foo bar
#
# ... then code in directories named COMPONENT_foo and COMPONENT_bar will be
# added to the build
#
COMPONENTS+=SECURE_DEVICE

# Enable the cy-mbedtls-acceleration transparent driver so the PSA P-256 /
# SHA-256 operations used by the provisioning service dispatch to the
# MXCRYPTOLITE hardware instead of the software built-ins.
COMPONENTS+=MW_CY_MBEDTLS_ACCELERATION


# Like COMPONENTS, but disable optional code that was enabled by default.
DISABLE_COMPONENTS+=

# Set this application to run at the CM0+
CORE=CM33
CORE_NAME=CM33_0

# Set this application to be secure
VCORE_ATTRS=SECURE

#Configure this application to be loaded into flash/ram. 
APPTYPE=flash

# Non-Secure-Callable (NSC) veneer object. The secure project generates this
# import library (--out-implib); the non-secure project links against it
# (--in-implib). Both projects point to the same shared path.
NSC_VENEER=../build/nsc_veneer.o

# By default the build system automatically looks in the Makefile's directory
# tree for source code and builds it. The SOURCES variable can be used to
# manually add source code to the build process from a location not searched
# by default, or otherwise not found by the build system.
SOURCES+=

# Like SOURCES, but for include directories. Value should be paths to
# directories (without a leading -I).
INCLUDES+=../shared
INCLUDES+=pas
INCLUDES+=core
INCLUDES+=nsc
INCLUDES+=config
# Per-platform CryptoLite config override folder (e.g. platforms/PSC3M5/config);
# expands to nothing for platforms that use the default config/ file.
INCLUDES+=$(wildcard ../platforms/$(PLATFORM)/config)
INCLUDES+=platform

# Add additional defines to the build process (without a leading -D).
DEFINES+=NORMAL_PROVISIONED_LCS CY_PDL_FLASH_BOOT
# Secure SRAM/flash sizes come from common.mk (single source of truth); these
# feed partition_psc3.h -> TZ_SAU_Setup(), while common.mk feeds the linker.
ifeq ($(TARGET),APP_KIT_PSC3M5_EVK)
DEFINES+=USER_SRAM_S_SIZE=$(SECURE_SRAM_SIZE) USER_FLASH_S_SIZE=$(SECURE_FLASH_SIZE)
endif

# Full-replacement mbedTLS / PSA-Crypto configuration for the provisioning
# service (see config/). mbedtls_config.h REPLACES the Infineon platform base
# config so only the provisioning crypto suite (ECDSA-P256 / SHA-256 / AES-GCM
# + PK + X.509 CSR) is compiled; it re-supplies the IFX MXCRYPTOLITE driver
# wiring and selects psa_crypto_config.h via MBEDTLS_PSA_CRYPTO_CONFIG_FILE.
DEFINES+=MBEDTLS_CONFIG_FILE='"prov_mbedtls_config.h"'

# CryptoLite PSA-driver user config, passed to the MXCryptolite driver as a
# compiler -D (NOT a #define in prov_mbedtls_config.h: the vendor driver headers
# include ifx_cryptolite_config.h before any mbedTLS config is processed). The
# default is the v3.X parts' config in config/ (PSC3M6/PSC3M8: keeps SHA-384/512
# hardware, routes P-521 ECDSA sign to software). A platform may override it in
# its detect_platform.mk -- PSC3M5 points PLATFORM_CRYPTOLITE_USER_CONFIG at its
# SHA-256-only v2.X config under platforms/PSC3M5/config (on the include path via
# the wildcard above).
PLATFORM_CRYPTOLITE_USER_CONFIG ?= prov_cryptolite_config_v3.h
DEFINES+=IFX_PSA_CRYPTOLITE_USER_CONFIG_FILE='"$(PLATFORM_CRYPTOLITE_USER_CONFIG)"'

# ---------------------------------------------------------------------------
# ML-DSA-87 (PQC) via wolfCrypt (ifx-wolfcrypto), compiled in only when the
# ml-dsa-87 suite is selected (PROV_SIG_SUITES in common.mk; PSC3M6/PSC3M8 only).
# The EC-256 provisioning path stays entirely on PSA / mbedTLS and never pulls
# in wolfCrypt, so EC-only and PSC3M5 builds are unaffected.
#
# ModusToolbox auto-discovers the shared wolfcrypt/src/*.c sources;
# wolfssl/user_settings.h (added to the include path below, selected by
# WOLFSSL_USER_SETTINGS) gates each file's content down to Dilithium +
# SHA-3/SHAKE + Hash-DRBG, and USE_MLDSA87 restricts Dilithium to the
# category-5 parameter set. Entropy for the DRBG comes from the PSC3 hardware
# TRNG via a CUSTOM_RAND_GENERATE_SEED hook (wolfssl/prov_wc_seed.c) that calls
# the MTB HAL directly: on M6/M8 (HAL v3) the asset's built-in CY_USING_HAL TRNG
# branch cannot be used because defining CY_USING_HAL makes the generated BSP
# (cycfg_pins.h) pull in the HAL-v2 cyhal_hwmgr.h, which does not exist here.
ifeq ($(PROV_ENABLE_MLDSA87),1)
COMPONENTS+=WOLFSSL
INCLUDES+=wolfssl
INCLUDES+=$(SEARCH_ifx-wolfcrypto)
DEFINES+=WOLFSSL_USER_SETTINGS USE_MLDSA87
DEFINES+=CUSTOM_RAND_GENERATE_SEED=prov_wc_generate_seed
# The asset's .cyignore does not keep its architecture-specific x86 assembly out
# of MTB shared-library auto-discovery, so drop every *.S/*.asm here (only the
# pure-C wolfCrypt paths are valid on Cortex-M33). The wildcard resolves the
# pinned asset version under mtb_shared.
WOLFSSL_SRC_DIR:=$(wildcard ../../mtb_shared/ifx-wolfcrypto/*/wolfcrypt/src)
CY_IGNORE+=$(wildcard $(WOLFSSL_SRC_DIR)/*.S)
CY_IGNORE+=$(wildcard $(WOLFSSL_SRC_DIR)/*.asm)
else
# ml-dsa-87 not selected: keep the (possibly fetched) wolfCrypt asset out of
# the build so EC-only / PSC3M5 images do not compile or link any of it.
CY_IGNORE+=$(SEARCH_ifx-wolfcrypto)
endif

# Select softfloat or hardfp floating point. Default is softfloat.
VFP_SELECT+=softfloat

# Additional / custom C compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CFLAGS+=

# Additional / custom C++ compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CXXFLAGS+=

# Additional / custom assembler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
ASFLAGS+=

# Additional / custom linker flags.
LDFLAGS+=

# Additional / custom libraries to link in to the application.
LDLIBS+=

# Path to the linker script to use (if empty, use the default linker script).
LINKER_SCRIPT+=


################################################################################
# Paths
################################################################################

# Relative path to the project directory (default is the Makefile's directory).
#
# This controls where automatic source code discovery looks for code.
CY_APP_PATH=

# Relative path to the shared repo location.
#
# All .mtb files have the format, <URI>#<COMMIT>#<LOCATION>. If the <LOCATION> field 
# begins with $$ASSET_REPO$$, then the repo is deposited in the path specified by 
# the CY_GETLIBS_SHARED_PATH variable. The default location is one directory level 
# above the current app directory.
# This is used with CY_GETLIBS_SHARED_NAME variable, which specifies the directory name.
CY_GETLIBS_SHARED_PATH=../..

# Directory name of the shared repo location.
#
CY_GETLIBS_SHARED_NAME=mtb_shared

# Resolve dependencies (.mtb) from the per-device folder selected by PLATFORM
# (set in common.mk) so PSC3M5, PSC3M6 and PSC3M8 can pin different asset
# versions (e.g. cy-mbedtls-acceleration v2.X vs v3.X). Re-run 'make getlibs'
# after switching TARGET between devices.
CY_GETLIBS_DEPS_PATH+=../platforms/$(PLATFORM)/deps/proj_cm33_s

# Absolute path to the compiler's "bin" directory.
#
# The default depends on the selected TOOLCHAIN (GCC_ARM uses the ModusToolbox
# IDE provided compiler by default).
# CY_COMPILER_PATH?=

include $(CY_TOOLS_DIR)/make/start.mk
