################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Top-level application make file.
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


################################################################################
# Basic Configuration
################################################################################

# Type of ModusToolbox Makefile Options include:
#
# COMBINED    -- Top Level Makefile usually for single standalone application
# APPLICATION -- Top Level Makefile usually for multi project application
# PROJECT     -- Project Makefile under Application
#
MTB_TYPE=COMBINED

# Target board/hardware (BSP).
# To change the target, it is recommended to use the Library manager
# ('make modlibs' from command line), which will also update Eclipse IDE launch
# configurations. If TARGET is manually edited, ensure TARGET_<BSP>.mtb with a
# valid URL exists in the application, run 'make getlibs' to fetch BSP contents
# and update or regenerate launch configurations for your IDE.
TARGET=APP_KIT_PSC3M6_EVAL

# Name of application (used to derive name of final linked file).
#
# If APPNAME is edited, ensure to update or regenerate launch
# configurations for your IDE.
APPNAME=mtb-example-ce243041-secure-boot-and-update-with-epb

# Name of toolchain to use. Options include:
#
# GCC_ARM -- GCC provided with ModusToolbox IDE
TOOLCHAIN=GCC_ARM

# Default build configuration. Options include:
#
# Debug -- build with minimal optimizations, focus on debugging.
# Release -- build with full optimizations
# Custom -- build with custom configuration, set the optimization flag in CFLAGS
#
# If CONFIG is manually edited, ensure to update or regenerate launch configurations
# for your IDE.
CONFIG=Debug

# If set to "true" or "1", display full command-lines when building.
VERBOSE=


# Enable the CY_PYTHON_PATH requirement.
CY_PYTHON_REQUIREMENT=true

# Set Python path
ifeq ($(CY_PYTHON_PATH),)
ifeq ($(OS),Windows_NT)
CY_PYTHON_PATH?=$(shell which python)
else
CY_PYTHON_PATH?=$(shell which python3)
endif
endif # checking for Python path


# Image signing key type.
#  Allowed values:
#   Post-quantum : XMSS_SHA2_10_256, LMS_SHA256_M32_H10, ML-DSA-44 , ML-DSA-65, ML-DSA-87
#   Classic ECC  : ECDSA-256, ECDSA-384, ECDSA-521
# Hackathon Task 1 = ECDSA-384 baseline; Task 2 = ML-DSA-87.
IMAGE_SIGNING_KEY_TYPE=ECDSA-384

# Path to Image signing private key (must match IMAGE_SIGNING_KEY_TYPE).
# Example private keys provided in the keys folder:
#   XMSS_SHA2_10_256   : ../keys/xmss_img_sign_key_priv.der
#   LMS_SHA256_M32_H10 : ../keys/lms_img_sign_key_priv.der
#   ML-DSA-44          : ../keys/ml_dsa_key_private_44.der
#   ML-DSA-65          : ../keys/ml_dsa_key_private_65.der
#   ML-DSA-87          : ../keys/ml_dsa_87_img_sign_key_priv.der
#   ECDSA-256          : ../keys/ecdsa_p256_img_sign_key_priv.der
#   ECDSA-384          : ../keys/ecdsa_p384_img_sign_key_priv.der
#   ECDSA-521          : ../keys/ecdsa_p521_img_sign_key_priv.der
IMAGE_SIGNING_KEY=../keys/ecdsa_p384_img_sign_key_priv.der

# Hackathon Task 2: image hash / HashML-DSA pre-hash override (sha256|sha384|sha512).
# Empty = scheme default from configs/prebuild.py. Must match EPBL feature_config
# "ml_dsa_sig_hash" (CNSA 2.0 profile: ML-DSA-87 + SHA512).
IMAGE_SHA?=

# Image encryption settings (applies to UPDATE images only)
# Set to 1 to enable encryption, 0 to disable
IMAGE_ENCRYPTION=0

# Encryption type: EC256 or KDF-CMAC
# EC256    : ECIES-P256 + AES-128-CTR (software)
#            Compatible with: ML-DSA-44, ML-DSA-65, ML-DSA-87, LMS, XMSS, ECDSA-256
#            NOT compatible with: ECDSA-384, ECDSA-521
# KDF-CMAC : Hardware key derivation + AES-128-CTR
#            Compatible with: All signing schemes
IMAGE_ENCRYPTION_TYPE=EC256

# Path to encryption key (type depends on IMAGE_ENCRYPTION_TYPE)
# EC256    : EC P-256 public key (.pem)
# KDF-CMAC : 16-byte AES-128 master key (.bin)
# Example keys:
#   EC256    : ../keys/enc_ec256_pub.pem
#   KDF-CMAC : ../keys/aes128_enc_master_key.bin
IMAGE_ENCRYPTION_KEY=../keys/enc_ec256_pub.pem

# Set Image type as BOOT or UPDATE
#  * BOOT   : Images will generated for Primary slots (Suitable for directly Programming the image through onboard debugger))
#  * UPDATE : Images will generated for Secondary slots (To be used for DFU over serial interface)
IMG_TYPE=BOOT


# Set Image Version and Build Number
ifeq ($(IMG_TYPE),BOOT)
IMG_VER_MAJOR=1		# 0 - 255
IMG_VER_MINOR=0		# 0 - 255
IMG_REVISION=0		# 0 - 65535
IMG_BUILD_NO=0		# 0 - 65535
else
IMG_VER_MAJOR=2		# 0 - 255
IMG_VER_MINOR=0		# 0 - 255
IMG_REVISION=0		# 0 - 65535
IMG_BUILD_NO=1		# 0 - 65535
endif #$(IMG_TYPE)


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

# Enable the transport interface(s).
COMPONENTS+= DFU_I2C 

# Like COMPONENTS, but disable optional code that was enabled by default.
DISABLE_COMPONENTS=

# PSoC Control C3 has only one core
CORE=CM33

# Name of the PSoC Control C3 core
CORE_NAME=CM33_0

# By default the build system automatically looks in the Makefile's directory
# tree for source code and builds it. The SOURCES variable can be used to
# manually add source code to the build process from a location not searched
# by default, or otherwise not found by the build system.
SOURCES=

# Like SOURCES, but for include directories. Value should be paths to
# directories (without a leading -I).
INCLUDES=

# DFU defines
DEFINES+=DFU_I2C_TX_BUFFER_SIZE=256 DFU_I2C_RX_BUFFER_SIZE=256 CY_DFU_FLOW=CY_DFU_MCUBOOT_FLOW CY_DFU_PRODUCT=0x01020304 CY_DFU_LOG_LEVEL=CY_DFU_LOG_LEVEL_ERROR

# Select softfp or hardfp floating point. Default is softfp.
VFP_SELECT=

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
LINKER_SCRIPT=

#Prebuild: generate symbols.json consumed by the signer-combiner post-build step
PREBUILD=$(CY_PYTHON_PATH) configs/prebuild.py --maj $(IMG_VER_MAJOR) --min $(IMG_VER_MINOR) --rev $(IMG_REVISION) --bn $(IMG_BUILD_NO) --key $(IMAGE_SIGNING_KEY) --key-type $(IMAGE_SIGNING_KEY_TYPE) --enc-key $(IMAGE_ENCRYPTION_KEY) $(if $(IMAGE_SHA),--sha $(IMAGE_SHA)) --t configs/symbol_template.json -o bsps/TARGET_$(TARGET)/symbols.json
# Custom post-build commands to run.
POSTBUILD=

#Configure this application to be loaded into flash/ram.  
APPTYPE=flash


MCUBOOT_HEADER_SIZE=0x400

ifeq ($(IMG_TYPE),BOOT)
    # Boot images are never encrypted (executed in place)
    COMBINE_SIGN_JSON?=configs/boot.json
else
    # Update images: select template based on encryption settings
    ifeq ($(IMAGE_ENCRYPTION),1)
        ifeq ($(IMAGE_ENCRYPTION_TYPE),EC256)
            COMBINE_SIGN_JSON?=configs/update_image_enc_ec256.json
        else ifeq ($(IMAGE_ENCRYPTION_TYPE),KDF-CMAC)
            COMBINE_SIGN_JSON?=configs/update_image_enc_kdf_cmac.json
        else
            $(error Invalid IMAGE_ENCRYPTION_TYPE: $(IMAGE_ENCRYPTION_TYPE). Must be EC256 or KDF-CMAC)
        endif
    else
        # Encryption disabled - use plain signed update
        COMBINE_SIGN_JSON?=configs/update.json
    endif

endif

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
CY_GETLIBS_SHARED_PATH=../

# Directory name of the shared repo location.
#
CY_GETLIBS_SHARED_NAME=mtb_shared

# Absolute path to the compiler's "bin" directory.
#
# The default depends on the selected TOOLCHAIN (GCC_ARM uses the ModusToolbox

# Locate ModusToolbox IDE helper tools folders in default installation
# locations for Windows, Linux, and macOS.
CY_WIN_HOME=$(subst \,/,$(USERPROFILE))
CY_TOOLS_PATHS ?= $(wildcard \
    $(CY_WIN_HOME)/ModusToolbox/tools_* \
    $(HOME)/ModusToolbox/tools_* \
    /Applications/ModusToolbox/tools_*)

# If you install ModusToolbox IDE in a custom location, add the path to its
# "tools_X.Y" folder (where X and Y are the version number of the tools
# folder). Make sure you use forward slashes.
CY_TOOLS_PATHS+=

# Default to the newest installed tools folder, or the users override (if it's
# found).
CY_TOOLS_DIR=$(lastword $(sort $(wildcard $(CY_TOOLS_PATHS))))

ifeq ($(CY_TOOLS_DIR),)
$(error Unable to find any of the available CY_TOOLS_PATHS -- $(CY_TOOLS_PATHS). On Windows, use forward slashes.)
endif

$(info Tools Directory: $(CY_TOOLS_DIR))

include $(CY_TOOLS_DIR)/make/start.mk
