/*****************************************************************************
 * File Name        : prov_cryptolite_config.h
 *
 * Description      : MXCRYPTOLITE PSA transparent-driver user config -- PSC3M5's
 *                    override of the default CryptoLite config. It lives in the
 *                    PSC3M5 platform folder and is wired via
 *                    IFX_PSA_CRYPTOLITE_USER_CONFIG_FILE (see proj_cm33_s/Makefile;
 *                    M5's detect_platform.mk sets PLATFORM_CRYPTOLITE_USER_CONFIG).
 *                    The PSC3M5 MXCryptolite accelerator (cy-mbedtls-acceleration
 *                    v2.X) does SHA-256 only and cannot handle every curve the
 *                    provisioning suite may request. It is selected unconditionally
 *                    for the device (not per curve):
 *                    it replaces ONLY the driver's auto-derivation (the #else
 *                    branch of the stock ifx_cryptolite_config.h); the vendor's
 *                    shared validation and feature aggregation still run after
 *                    it.
 *
 *                    Why it exists: the stock auto-derivation force-enables
 *                    IFX_PSA_CRYPTOLITE_ECDSA_VERIFY whenever an EC public key
 *                    is requested -- its "mbedtls 3.6 compat" block has no curve
 *                    guard -- so a P-521 key trips the stock header's hard
 *                    #error "IFX_PSA_CRYPTOLITE_ECC_SECP_R1_xxx curve not defined
 *                    for ECDSA functionality" (the accelerator covers NIST
 *                    SECP_R1 only up to P-384; see prov_psa_crypto_config.h).
 *
 *                    This file reproduces the stock derivation faithfully but
 *                    (a) omits the P-521 curve arm and (b) gates ECDSA/ECDH on a
 *                    CryptoLite-supported curve actually being present. The net
 *                    effect is identical to the stock config for P-256 / P-384
 *                    builds (hardware accelerated), while a P-521 build defines
 *                    no CryptoLite EC macro and the PSA core routes P-521 ECDSA
 *                    to the mbedTLS software path. SHA / HMAC / CMAC / AES
 *                    acceleration is retained on every build. PSC3M6/M8 ship the
 *                    newer v3.X accelerator (SHA-384/512 in hardware) and use the
 *                    default config/prov_cryptolite_config_v3.h instead -- same
 *                    P-521-ECDSA-sign-to-software behaviour, but it keeps the
 *                    SHA-384/512 hardware this v2.X part lacks.
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

#ifndef PROV_CRYPTOLITE_CONFIG_PROV_H
#define PROV_CRYPTOLITE_CONFIG_PROV_H

#include "mbedtls/build_info.h"
#include "psa/crypto_driver_common.h"

/* ---- Non-EC primitives: mirror the vendor auto-derivation ----------------
 * Each enable follows the same PSA_WANT_* capability the stock
 * ifx_cryptolite_config.h keys off, so SHA / MAC / key-derivation remain
 * hardware accelerated exactly as on the P-256 / P-384 builds. */

#if defined(PSA_WANT_KEY_TYPE_DERIVE)
#define IFX_PSA_CRYPTOLITE_KEY_DERIVATION
#endif

#if defined(PSA_WANT_ALG_HMAC)
#define IFX_PSA_CRYPTOLITE_HMAC
#endif

#if defined(PSA_WANT_ALG_CMAC)
#define IFX_PSA_CRYPTOLITE_CMAC
#endif

#if defined(PSA_WANT_ALG_SHA_256)
#define IFX_PSA_CRYPTOLITE_SHA
#define IFX_PSA_CRYPTOLITE_SHA_256
#endif

/* AES block-cipher modes: mirror the vendor derivation so the shared
 * CIPHER/AEAD aggregators behave identically. The provisioning service uses
 * AES-GCM (via the full MXCRYPTO block, not CryptoLite) for key wrapping, so
 * none of these is expected to be enabled; the guards simply track whatever
 * PSA_WANT_* the build declares. */
#if defined(PSA_WANT_ALG_ECB_NO_PADDING)
#define IFX_PSA_CRYPTOLITE_ECB_NO_PADDING
#endif

#if defined(PSA_WANT_ALG_CBC_NO_PADDING)
#define IFX_PSA_CRYPTOLITE_CBC_NO_PADDING
#endif

#if defined(PSA_WANT_ALG_CFB)
#define IFX_PSA_CRYPTOLITE_CFB
#endif

#if defined(PSA_WANT_ALG_CTR)
#define IFX_PSA_CRYPTOLITE_CTR
#endif

#if defined(PSA_WANT_ALG_CCM)
#define IFX_PSA_CRYPTOLITE_CCM
#endif

/* ---- Elliptic curve: per supported curve, up to CryptoLite's P-384 ceiling
 * Mirror the stock per-curve derivation but WITHOUT the P-521 arm: the
 * accelerator has no P-521 engine and the stock header hard-#errors if
 * IFX_PSA_CRYPTOLITE_ECC_SECP_R1_521 is ever defined. A P-521 build therefore
 * defines no CryptoLite curve macro and the PSA core routes P-521 EC to
 * software; P-256 / P-384 stay hardware accelerated exactly as stock. */
#if defined(PSA_WANT_ECC_SECP_R1_192)
#define IFX_PSA_CRYPTOLITE_ECC_SECP_R1_192
#endif
#if defined(PSA_WANT_ECC_SECP_R1_224)
#define IFX_PSA_CRYPTOLITE_ECC_SECP_R1_224
#endif
#if defined(PSA_WANT_ECC_SECP_R1_256)
#define IFX_PSA_CRYPTOLITE_ECC_SECP_R1_256
#endif
#if defined(PSA_WANT_ECC_SECP_R1_384)
#define IFX_PSA_CRYPTOLITE_ECC_SECP_R1_384
#endif

/* ECDH: unchanged from the stock derivation (already curve-guarded there). */
#if defined(PSA_WANT_ALG_ECDH) \
 && (defined(PSA_WANT_KEY_TYPE_ECC_KEY_PAIR) || defined(PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC)) \
 && (defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_192) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_224) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_256) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_384))
#define IFX_PSA_CRYPTOLITE_ECDH
#endif

/* EC public-key export: unchanged from the stock derivation (curve-guarded). */
#if defined(PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY) \
 && (defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_192) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_224) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_256) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_384))
#define IFX_PSA_CRYPTOLITE_ECC_PUBLIC_KEY_EXPORT
#define IFX_PSA_CRYPTOLITE_PUBLIC_KEY_EXPORT
#endif

/* ECDSA -- the fix. Gate BOTH sign and verify on a CryptoLite-supported curve
 * (the stock header does not, which is what trips the P-521 #error). A P-521
 * build thus leaves ECDSA entirely on the mbedTLS software path, while P-256 /
 * P-384 keep the same hardware ECDSA sign/verify as stock. */
#if defined(PSA_WANT_ALG_ECDSA) \
 && (defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_192) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_224) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_256) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_384))
#if defined(PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC)
#define IFX_PSA_CRYPTOLITE_ECDSA_SIGN
#endif
#if defined(PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY) || defined(PSA_WANT_KEY_TYPE_ECC_KEY_PAIR)
#define IFX_PSA_CRYPTOLITE_ECDSA_VERIFY
#define IFX_PSA_CRYPTOLITE_ECDSA_VERIFY_USE_PK
#endif
#endif

/* EC key generation: unchanged from the stock derivation (curve-guarded). */
#if (defined(PSA_WANT_KEY_TYPE_ECC_KEY_PAIR) || defined(PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE)) \
 && (defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_192) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_224) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_256) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_384))
#define IFX_PSA_CRYPTOLITE_KEY_GENERATION
#endif

#endif /* PROV_CRYPTOLITE_CONFIG_PROV_H */

/* [] END OF FILE */
