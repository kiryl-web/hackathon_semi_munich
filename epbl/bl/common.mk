################################################################################
# \file common.mk
# \version 2.1
#
# \brief
# Settings shared across all projects.
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

MTB_TYPE=PROJECT

# Name of toolchain to use. Options include:
#
# GCC_ARM -- GCC provided with ModusToolbox IDE
# ARM     -- ARM Compiler (must be installed separately)
# IAR     -- IAR Compiler (must be installed separately)
#
# See also: CY_COMPILER_PATH below

TOOLCHAIN=GCC_ARM

# Target board/hardware (BSP).
# To change the target, it is recommended to use the Library manager
# ('make modlibs' from command line), which will also update Eclipse IDE launch
# configurations. If TARGET is manually edited, ensure TARGET_<BSP>.mtb with a
# valid URL exists in the application, run 'make getlibs' to fetch BSP contents
# and update or regenerate launch configurations for your IDE.
TARGET=APP_KIT_PSC3M6_EVAL

################################################################################
# Family and platform
################################################################################
include ../common_app.mk

# Auto-detect FAMILY and PLATFORM from TARGET.
# Each platforms/*/detect_platform.mk sets FAMILY and PLATFORM for its own TARGET list.
-include $(wildcard ../platforms/*/detect_platform.mk)
# If no detect_platform.mk matched TARGET add the actual TARGET to the appropriate
# platforms/*/detect_platform.mk to resolve this error.
ifndef FAMILY
$(error Unrecognized TARGET='$(TARGET)'. No platforms/*/detect_platform.mk set FAMILY. \
        Add $(TARGET) to the correct platforms/<PLATFORM>/detect_platform.mk)
endif
ifndef PLATFORM
$(error Unrecognized TARGET='$(TARGET)'. No platforms/*/detect_platform.mk set PLATFORM. \
        Add $(TARGET) to the correct platforms/<PLATFORM>/detect_platform.mk)
endif

# Hackathon: use the memory map shipped with the secure-boot app example (CE243041).
MEMORY_MAP ?= C:/Users/kpato/KP/Hackathon/mtb-example-ce243041-secure-boot-and-update-with-epb-master/memory_map/overwrite_single_flash.json
