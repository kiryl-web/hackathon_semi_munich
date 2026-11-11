################################################################################
# \file detect_platform.mk
#
# \brief
# Sets PLATFORM to PSC3M8 when TARGET matches a PSC3M8 kit. Included by
# common.mk via wildcard so adding a new PSC3M8 board only requires extending
# the TARGET list below. PLATFORM selects the per-device deps folder
# (platforms/$(PLATFORM)/deps) used by 'make getlibs'.
################################################################################

ifeq ($(filter $(TARGET), KIT_PSC3M8_EVK APP_KIT_PSC3M8_EVK),$(TARGET))
    PLATFORM ?= PSC3M8
endif
