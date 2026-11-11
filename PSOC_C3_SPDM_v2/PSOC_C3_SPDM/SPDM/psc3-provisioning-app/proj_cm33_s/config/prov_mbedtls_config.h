/*****************************************************************************
 * File Name        : prov_mbedtls_config.h
 *
 * Description      : Project mbedTLS configuration for the secure provisioning
 *                    service — FULL REPLACEMENT of the Infineon platform base
 *                    config.
 *
 *                    This file is selected via MBEDTLS_CONFIG_FILE (see
 *                    proj_cm33_s/Makefile) and therefore REPLACES the vendor
 *                    base config (target/.../psc3/psc3_mbedtls_config.h, which
 *                    is normally pulled in through infineon_platform.h). mbedTLS
 *                    and every ifx-mbedtls source file is compiled against this
 *                    file alone, so only the modules declared here are built.
 *
 *                    Why a full replacement (vs. an additive user config):
 *                    the IFX base config enables the complete classic stack
 *                    (TLS, RSA, every ECC curve, all hash families, DES/ARIA/
 *                    Camellia/ChaCha, …). An additive user config can only turn
 *                    features ON, so it inherited all of that (~176 KB .text)
 *                    and forced the secure flash/SRAM partitions to grow. By
 *                    replacing the base config we get a clean slate and compile
 *                    only what the provisioning service actually uses:
 *                    ECDSA-P256 / SHA-256 / AES-GCM + PK + X.509 PKCS#10 CSR.
 *
 *                    Hardware acceleration: replacing the base config means we
 *                    no longer pull in psc3_mbedtls_user_config.h (which is what
 *                    defines IFX_PSA_CRYPTOLITE_PRESENT and wires the MXCRYPTOLITE
 *                    transparent driver into the PSA driver wrappers). We
 *                    therefore re-supply that master switch below, under the
 *                    same COMPONENT_MW_CY_MBEDTLS_ACCELERATION guard. The
 *                    driver's per-feature configuration is derived from the
 *                    PSA_WANT_* macros in psa_crypto_config.h (see
 *                    ifx_cryptolite_config.h).
 *
 *                    PSA capability switches (PSA_WANT_*) live in the companion
 *                    file prov_psa_crypto_config.h, selected below via
 *                    MBEDTLS_PSA_CRYPTO_CONFIG_FILE.
 *
 * Related Document : docs/design_and_implementation.md
 *
 *******************************************************************************
 * (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG. All rights reserved.
 * This software, associated documentation and materials ("Software") is
 * owned by Infineon Technologies AG or one of its affiliates ("Infineon")
 * and is protected by and subject to worldwide patent protection, worldwide
 * copyright laws, and international treaty provisions. Therefore, you may use
 * this Software only as provided in the license agreement accompanying the
 * software package from which you obtained this Software. If no license
 * agreement applies, then any use, reproduction, modification, translation, or
 * compilation of this Software is prohibited without the express written
 * permission of Infineon.
 *
 * Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
 * IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
 * THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
 * SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
 * Infineon reserves the right to make changes to the Software without notice.
 * You are responsible for properly designing, programming, and testing the
 * functionality and safety of your intended application of the Software, as
 * well as complying with any legal requirements related to its use. Infineon
 * does not guarantee that the Software will be free from intrusion, data theft
 * or loss, or other breaches ("Security Breaches"), and Infineon shall have
 * no liability arising out of any Security Breaches. Unless otherwise
 * explicitly approved by Infineon, the Software may not be used in any
 * application where a failure of the Product or any consequences of the use
 * thereof can reasonably be expected to result in personal injury.
 *****************************************************************************/

#ifndef MBEDTLS_CONFIG_PROV_H
#define MBEDTLS_CONFIG_PROV_H

/******************************************************************************
 * Infineon MXCRYPTOLITE PSA transparent driver
 *
 * Reproduce the wiring normally provided by psc3_mbedtls_user_config.h (pulled
 * in by infineon_platform.h). Replacing the IFX base config drops that include,
 * so re-supply the master switch here. IFX_PSA_CRYPTOLITE_PRESENT registers the
 * CryptoLite transparent driver in the PSA driver wrappers; the driver's
 * per-feature macros are derived from PSA_WANT_* in ifx_cryptolite_config.h.
 *****************************************************************************/
#ifdef COMPONENT_MW_CY_MBEDTLS_ACCELERATION
#define IFX_PSA_CRYPTOLITE_PRESENT

/* The CryptoLite curve coverage (and the P-521-to-software fallback) is tailored
 * by the CryptoLite user config selected via IFX_PSA_CRYPTOLITE_USER_CONFIG_FILE
 * -- see that file (config/prov_cryptolite_config_v3.h and the PSC3M5 override)
 * and docs/design_and_implementation.md. That selection is NOT made here: it is
 * a command-line -D from proj_cm33_s/Makefile, because the MXCryptolite driver
 * headers include ifx_cryptolite_config.h before any mbedTLS config is processed,
 * so defining it here would always be too late. */
#endif

/******************************************************************************
 * Platform abstraction (mirrors the IFX base config so the platform behaviour
 * is identical to the proven build: no libc stdio assumptions, custom snprintf,
 * calloc/free-backed allocator).
 *****************************************************************************/
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_SNPRINTF_ALT
#define MBEDTLS_PLATFORM_STD_MEM_HDR   <stdlib.h>
#define MBEDTLS_PLATFORM_STD_CALLOC        calloc
#define MBEDTLS_PLATFORM_STD_FREE            free

/******************************************************************************
 * Entropy / RNG
 *
 * The TRNG is provided by the IFX acceleration library through the hardware
 * entropy hook (mbedtls_hardware_poll); no platform/OS entropy source exists.
 * PSA's internal RNG is seeded from the entropy module through CTR_DRBG, which
 * in turn relies on the software AES primitive (hence MBEDTLS_AES_C).
 *****************************************************************************/
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_AES_C

/* Cipher layer: required because mbedTLS always auto-enables
 * PSA_WANT_KEY_TYPE_DERIVE, which makes the Infineon MXCRYPTOLITE KDF driver
 * pull in CMAC (PSA_WANT_ALG_CMAC -> MBEDTLS_CMAC_C). CMAC needs the cipher
 * abstraction layer (MBEDTLS_CIPHER_C + MBEDTLS_AES_C). MBEDTLS_CIPHER_C also
 * satisfies the PSA soft-block-mode prerequisite (check_config.h). */
#define MBEDTLS_CIPHER_C

/******************************************************************************
 * PSA Crypto core (driver-based configuration)
 *
 * MBEDTLS_PSA_CRYPTO_CONFIG makes the enabled crypto primitives follow the
 * PSA_WANT_* switches in psa_crypto_config.h; MBEDTLS_PSA_CRYPTO_DRIVERS enables
 * the transparent-driver dispatch used by the CryptoLite accelerator.
 *****************************************************************************/
#define MBEDTLS_PSA_CRYPTO_C
#define MBEDTLS_PSA_CRYPTO_CONFIG
#define MBEDTLS_PSA_CRYPTO_DRIVERS
#define MBEDTLS_PSA_CRYPTO_CONFIG_FILE "prov_psa_crypto_config.h"

/* Route PK / X.509 signing through PSA so the CSR is signed by the CryptoLite
 * hardware and the private key never leaves the secure world. */
#define MBEDTLS_USE_PSA_CRYPTO

/******************************************************************************
 * Public-key (PK) layer + X.509 PKCS#10 CSR writing (prov_csr.c) — used only by
 * the ECDSA suites. ML-DSA-87 hand-builds its CSR in DER with wolfCrypt
 * (prov_csr_mldsa.c), so an ml-dsa-87-only build needs none of this; leaving
 * MBEDTLS_PK_C on with no EC/RSA compiled in trips its check_config prereq.
 * The CSR is emitted in DER (no PEM), so no base64 / PEM modules are needed.
 *****************************************************************************/
#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CREATE_C
#define MBEDTLS_X509_CSR_WRITE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_MD_C

/* The device never parses or prints certificates, so drop the human-readable
 * X.509 strings (see Provisioning-Device-Design.md §6.9). */
#define MBEDTLS_X509_REMOVE_INFO
#endif

/******************************************************************************
 * Secure-RAM footprint: the provisioning service holds at most a couple of PSA
 * keys live at once. Cap the key store (default 32) to fit the secure SRAM
 * budget (see Provisioning-Device-Design.md §4).
 *****************************************************************************/
#define MBEDTLS_PSA_KEY_SLOT_COUNT 8

#endif /* MBEDTLS_CONFIG_PROV_H */

/* [] END OF FILE */
