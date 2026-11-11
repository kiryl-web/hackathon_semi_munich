################################################################################
# \file detect_platform.mk
#
# \brief
# Sets PLATFORM to PSC3M5 when TARGET matches a PSC3M5 kit. Included by
# common.mk via wildcard so adding a new PSC3M5 board only requires extending
# the TARGET list below. PLATFORM selects the per-device deps folder
# (platforms/$(PLATFORM)/deps) used by 'make getlibs'.
################################################################################

ifeq ($(filter $(TARGET), KIT_PSC3M5_EVK APP_KIT_PSC3M5_EVK),$(TARGET))
    PLATFORM ?= PSC3M5
    # Crypto accelerator is MXCryptolite (cy-mbedtls-acceleration v2.X, pinned in
    # platforms/PSC3M5/deps): it does SHA-256 only and accelerates NIST SECP_R1
    # ECDSA only up to P-384, and its stock PSA driver config hard-#errors on a
    # P-521 key. Override the default CryptoLite config with this device's
    # SHA-256-only config, kept next to this file in platforms/PSC3M5/config (put
    # on the include path by the wildcard in proj_cm33_s/Makefile), so unsupported
    # curves fall back to software (P-256/P-384 stay accelerated). The default
    # (config/prov_cryptolite_config_v3.h, used by the v3.X parts PSC3M6/M8) enables
    # SHA-384/512 hardware this v2.X accelerator does not have.
    PLATFORM_CRYPTOLITE_USER_CONFIG ?= prov_cryptolite_config.h
endif
