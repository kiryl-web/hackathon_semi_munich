################################################################################
# \file detect_platform.mk
#
# \brief
# Sets PLATFORM to PSC3M6 when TARGET matches a PSC3M6 kit. Included by
# common.mk via wildcard so adding a new PSC3M6 board only requires extending
# the TARGET list below. PLATFORM selects the per-device deps folder
# (platforms/$(PLATFORM)/deps) used by 'make getlibs'.
################################################################################

ifeq ($(filter $(TARGET), KIT_PSC3M6_EVAL APP_KIT_PSC3M6_EVAL),$(TARGET))
    PLATFORM ?= PSC3M6
endif
