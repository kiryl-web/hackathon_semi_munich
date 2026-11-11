/*****************************************************************************
 * File Name        : prov_cryptolite_config_v3.h
 *
 * Description      : MXCRYPTOLITE PSA transparent-driver user config -- the
 *                    DEFAULT CryptoLite config (in config/), wired via
 *                    IFX_PSA_CRYPTOLITE_USER_CONFIG_FILE in proj_cm33_s/Makefile.
 *                    It serves the v3.X parts PSC3M6 / PSC3M8, whose MXCryptolite
 *                    accelerator (cy-mbedtls-acceleration v3.X) accepts a P-521
 *                    ECDSA sign that its CryptoLite hardware does not implement.
 *                    It is used unconditionally on those devices (not per curve):
 *                    it replaces ONLY the driver's auto-derivation (the #else
 *                    branch of the stock ifx_cryptolite_config.h); the vendor's
 *                    shared validation and feature aggregation still run after it.
 *
 *                    Why it exists: the stock v3.X auto-derivation maps
 *                    PSA_WANT_ECC_SECP_R1_521 to IFX_PSA_CRYPTOLITE_ECC_SECP_R1_521,
 *                    so the CryptoLite PSA sign driver accepts a P-521 key and
 *                    calls the PDL. The CryptoLite ECDSA engine does not sign
 *                    P-521 -- Cy_Cryptolite_ECC_SignHash() returns
 *                    CY_CRYPTOLITE_NOT_SUPPORTED for SECP521R1 -- and the vendor
 *                    status map (ifx_cryptolite_status_to_psa_status) turns that
 *                    into PSA_ERROR_GENERIC_ERROR rather than
 *                    PSA_ERROR_NOT_SUPPORTED, so the PSA core never falls back to
 *                    software and the sign fails at runtime (PROV_ERR_CRYPTO on
 *                    build_csr / prove_cert). P-521 keygen / public-key export
 *                    already fall back to software (stock curve-guards them to
 *                    P-384), which is why only signing fails. (P-521 ECDSA verify
 *                    IS supported by the v3.X hardware, but the provisioning
 *                    device only signs, so that path is unused here.)
 *
 *                    This file reproduces the stock v3.X derivation faithfully
 *                    but (a) omits the P-521 curve arm and (b) gates ECDSA
 *                    sign/verify and EC key generation on a CryptoLite-supported
 *                    curve actually being present. The net effect is identical to
 *                    the stock config for P-256 / P-384 builds (hardware
 *                    accelerated, including SHA-256/384/512), while a build that
 *                    enables P-521 defines no CryptoLite P-521 macro: the
 *                    driver's sign/verify switch has no `case 521`, returns
 *                    PSA_ERROR_NOT_SUPPORTED, and the PSA core routes P-521 ECDSA
 *                    to the mbedTLS software path. SHA / HMAC / CMAC / AES
 *                    acceleration is retained on every build.
 *
 *                    It is the default config (config/) for the v3.X parts
 *                    PSC3M6 / PSC3M8. PSC3M5 ships the older v2.X accelerator
 *                    (SHA-256 only) and overrides this default with its own
 *                    prov_cryptolite_config.h under platforms/PSC3M5/config.
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

#ifndef PROV_CRYPTOLITE_CONFIG_V3_PROV_H
#define PROV_CRYPTOLITE_CONFIG_V3_PROV_H

#include "mbedtls/build_info.h"
#include "psa/crypto_driver_common.h"

/* ---- Non-EC primitives: mirror the vendor auto-derivation ----------------
 * Each enable follows the same PSA_WANT_* capability the stock v3.X
 * ifx_cryptolite_config.h keys off, so key-derivation / MAC stay hardware
 * accelerated exactly as stock. */

#if defined(PSA_WANT_KEY_TYPE_DERIVE)
#define IFX_PSA_CRYPTOLITE_KEY_DERIVATION
#endif

#if defined(PSA_WANT_ALG_HMAC)
#define IFX_PSA_CRYPTOLITE_HMAC
#endif

#if defined(PSA_WANT_ALG_CMAC)
#define IFX_PSA_CRYPTOLITE_CMAC
#endif

/* SHA: the v3.X MXCryptolite accelerator covers SHA-256 / SHA-384 / SHA-512 in
 * hardware (unlike v2.X, which is SHA-256 only), so mirror all three arms. */
#if defined(PSA_WANT_ALG_SHA_256)
#define IFX_PSA_CRYPTOLITE_SHA_256
#endif

#if defined(PSA_WANT_ALG_SHA_384)
#define IFX_PSA_CRYPTOLITE_SHA_384
#endif

#if defined(PSA_WANT_ALG_SHA_512)
#define IFX_PSA_CRYPTOLITE_SHA_512
#endif

#if (defined(PSA_WANT_ALG_SHA_256) || defined(PSA_WANT_ALG_SHA_384) || \
     defined(PSA_WANT_ALG_SHA_512))
#define IFX_PSA_CRYPTOLITE_SHA
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
 * CryptoLite ECDSA engine has no P-521 support, so defining
 * IFX_PSA_CRYPTOLITE_ECC_SECP_R1_521 (as the stock v3.X header does) makes the
 * sign driver claim P-521 and fail on the hardware. Leaving it undefined omits
 * the `case 521` from the driver's sign/verify switch, so the driver returns
 * PSA_ERROR_NOT_SUPPORTED and the PSA core routes P-521 EC to software;
 * P-256 / P-384 stay hardware accelerated exactly as stock. */
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

/* ECDH: curve-guarded (unchanged from the stock derivation). */
#if defined(PSA_WANT_ALG_ECDH) \
 && (defined(PSA_WANT_KEY_TYPE_ECC_KEY_PAIR) || defined(PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC)) \
 && (defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_192) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_224) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_256) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_384))
#define IFX_PSA_CRYPTOLITE_ECDH
#endif

/* EC public-key export: curve-guarded (unchanged from the stock derivation). */
#if defined(PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY) \
 && (defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_192) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_224) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_256) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_384))
#define IFX_PSA_CRYPTOLITE_ECC_PUBLIC_KEY_EXPORT
#define IFX_PSA_CRYPTOLITE_PUBLIC_KEY_EXPORT
#endif

/* ECDSA -- the fix. Gate BOTH sign and verify on a CryptoLite-supported curve
 * (the stock v3.X header leaves ECDSA_SIGN unguarded, which is what sends a
 * P-521 key to the hardware). A P-521-only build thus leaves ECDSA entirely on
 * the mbedTLS software path, while P-256 / P-384 keep the same hardware ECDSA
 * sign/verify as stock. */
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

/* EC key generation: curve-guarded (unchanged from the stock derivation). */
#if (defined(PSA_WANT_KEY_TYPE_ECC_KEY_PAIR) || defined(PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE)) \
 && (defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_192) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_224) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_256) \
  || defined(IFX_PSA_CRYPTOLITE_ECC_SECP_R1_384))
#define IFX_PSA_CRYPTOLITE_KEY_GENERATION
#endif

#endif /* PROV_CRYPTOLITE_CONFIG_V3_PROV_H */

/* [] END OF FILE */
