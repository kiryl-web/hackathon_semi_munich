################################################################################
# \file common_app.mk
# \version 2.0
#
# \brief
# Common variables shared across the entire application.
#
################################################################################
# \copyright
# (c) 2021-2026, Infineon Technologies AG, or an affiliate of Infineon
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

# Default build configuration. Options include:
#
# Debug -- build with minimal optimizations, focus on debugging.
# Release -- build with full optimizations
# Custom -- build with custom configuration, set the optimization flag in CFLAGS
#           CUSTOM_OPTIMIZATION variable can be used.
# 
# If CONFIG is manually edited, ensure to update or regenerate launch configurations 
# for your IDE.
CONFIG=Debug

# Python path definition
CY_PYTHON_REQUIREMENT=true

################################################################################
# Paths
################################################################################

# Locate ModusToolbox IDE helper tools folders in default installation
# locations for Windows, Linux, and macOS.
CY_WIN_HOME=$(subst \,/,$(USERPROFILE))
CY_TOOLS_PATHS ?= $(wildcard \
    $(CY_WIN_HOME)/ModusToolbox/tools_* \
    $(HOME)/ModusToolbox/tools_* \
    /Applications/ModusToolbox/tools_*)

# If you install ModusToolbox IDE in a custom location, add the path to its
# "tools_X.Y" folder (where X and Y are the version number of the tools
# folder).
CY_TOOLS_PATHS+=

# Default to the newest installed tools folder, or the user override (if it's
# found).
CY_TOOLS_DIR=$(lastword $(sort $(wildcard $(CY_TOOLS_PATHS))))

ifeq ($(CY_TOOLS_DIR),)
$(error Unable to find any of the available CY_TOOLS_PATHS -- $(CY_TOOLS_PATHS))
endif

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
CY_GETLIBS_SHARED_PATH=../../

# Directory name of the shared repo location.
#
CY_GETLIBS_SHARED_NAME=mtb_shared

#Common variables for bootloader and user app
BOOT_HEADER_SIZE=0x400

# Path of the key file passed as a parameter to the Python module "imgtool" for
# signing the image. Name of the key file is set in bootloader_cm0p/shared_config.mk
# as a common parameter to both the Bootloader app and user app.

# Name of the key file, used in two places. 
# 1. #included in keys.c for embedding it in the Bootloader app, used for image
#    authentication. 
# 2. Passed as a parameter to the Python module "imgtool" for signing the image
#    in the user app Makefile. The path of this key file is set in the user
#    app Makefile.

IMGTOOL_PATH ?= $(SEARCH_mcuboot)/scripts/imgtool.py
CY_ELF_TO_HEX_TOOL = $(MTB_TOOLCHAIN_GCC_ARM__BASE_DIR)/bin/arm-none-eabi-objcopy
CY_ELF_TO_HEX_OPTIONS = -I ihex -O ihex
