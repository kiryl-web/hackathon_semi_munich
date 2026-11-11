################################################################################
# \file detect_platform.mk
#
# \brief
# Sets FAMILY and PLATFORM to PSC3_M6 when TARGET matches a PSC3_M6 kit.
# Included by common.mk via wildcard, so it is only necessary to add 
# a new TARGET to the appropriate detect_platform.mk to set FAMILY and PLATFORM
# for that TARGET.
################################################################################

ifeq ($(filter $(TARGET), KIT_PSC3M6_EVAL APP_KIT_PSC3M6_EVAL),$(TARGET))
    FAMILY ?= PSC3_M6
    PLATFORM ?= PSC3_M6
endif
