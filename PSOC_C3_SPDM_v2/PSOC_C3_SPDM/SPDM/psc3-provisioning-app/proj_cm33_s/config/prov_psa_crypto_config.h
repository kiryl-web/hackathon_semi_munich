/*****************************************************************************
 * File Name        : prov_psa_crypto_config.h
 *
 * Description      : Project PSA-Crypto configuration for the secure
 *                    provisioning service — FULL REPLACEMENT of the Infineon
 *                    PSA capability base config.
 *
 *                    This file is selected via MBEDTLS_PSA_CRYPTO_CONFIG_FILE
 *                    (set inside the companion mbedtls_config.h) and therefore
 *                    REPLACES the vendor PSA config
 *                    (target/.../psc3/psc3_crypto_config.h). Only the PSA_WANT_*
 *                    features declared here are compiled, which is the lever
 *                    that shrinks the secure image: the IFX PSA base config
 *                    enables RSA, FFDH, ECDH/EC-JPAKE, every NIST/Brainpool/
 *                    Montgomery curve, all hash families and every cipher, none
 *                    of which the provisioning service uses.
 *
 *                    The Infineon MXCRYPTOLITE transparent driver derives its
 *                    own per-feature configuration from these same PSA_WANT_*
 *                    switches (see ifx_cryptolite_config.h).
 *
 *                    Deterministic ECDSA (RFC 6979) is deliberately NOT enabled
 *                    here; the device key uses randomised ECDSA (PSA_ALG_ECDSA).
 *                    Which primitives the accelerator covers is documented in
 *                    docs/design_and_implementation.md.
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

#ifndef PSA_CRYPTO_CONFIG_PROV_H
#define PSA_CRYPTO_CONFIG_PROV_H

/******************************************************************************
 * SHA-256 — always required regardless of signature suite: the 32-byte
 * cert_sha256 wire field and the HKDF key-wrap derivation both use it.
 *****************************************************************************/
#define PSA_WANT_ALG_SHA_256                    1

/******************************************************************************
 * EC signature suites (build-time opt-in via common.mk). Each curve is paired
 * with its message hash: P-256 / SHA-256, P-384 / SHA-384, P-521 / SHA-512.
 * Only P-256 is MXCRYPTOLITE-accelerated; P-384 / P-521 fall back to the
 * software mbedTLS ECP/ECDSA implementation (larger secure .text, slower
 * signing). The shared ECDSA algorithm and EC key-pair operations are compiled
 * in whenever any EC curve is selected.
 *****************************************************************************/
#if defined(PROV_ENABLE_EC256)
#define PSA_WANT_ECC_SECP_R1_256                1
#endif
#if defined(PROV_ENABLE_EC384)
#define PSA_WANT_ALG_SHA_384                    1
#define PSA_WANT_ECC_SECP_R1_384                1
#endif
#if defined(PROV_ENABLE_EC521)
#define PSA_WANT_ALG_SHA_512                    1
#define PSA_WANT_ECC_SECP_R1_521                1
#endif

#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)
/* Randomised ECDSA only (so the MXCRYPTOLITE driver accelerates P-256);
 * deterministic ECDSA (RFC 6979) is deliberately NOT enabled. ECC key-pair
 * operations: generate the key, export the raw public key for read-back, and
 * export the raw private scalar so it can be wrapped and stored in the PAS key
 * slot. */
#define PSA_WANT_ALG_ECDSA                      1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC    1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_EXPORT   1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT   1
#define PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY        1
#endif

/******************************************************************************
 * AES-GCM — used to wrap the exported private scalar before it is written to
 * the PAS key slot (format: nonce(12) || ciphertext || tag(16)).
 *****************************************************************************/
#define PSA_WANT_KEY_TYPE_AES                   1
#define PSA_WANT_ALG_GCM                        1

/******************************************************************************
 * HKDF-SHA256 — derives the AES-256 key-wrapping key from the device HUK
 * (see prov_device_wrap_key() in prov_crypto.c). HKDF is built on HMAC, so the
 * HMAC algorithm and HMAC key type are required as well; the underlying
 * SHA-256 is already enabled above and stays hardware accelerated.
 *****************************************************************************/
#define PSA_WANT_ALG_HKDF                       1
#define PSA_WANT_ALG_HMAC                       1
#define PSA_WANT_KEY_TYPE_HMAC                  1

/******************************************************************************
 * AES-CMAC — not used directly by the provisioning service, but REQUIRED by
 * the Infineon MXCRYPTOLITE driver: mbedTLS always auto-enables
 * PSA_WANT_KEY_TYPE_DERIVE (psa/crypto_adjust_auto_enabled.h), which makes the
 * vendor's always-compiled SP800-108 counter-mode KDF driver
 * (ifx_cryptolite_transparent_key_derivation.c) pull in the CryptoLite CMAC
 * primitives. Omitting this breaks the vendor accel library build.
 *****************************************************************************/
#define PSA_WANT_ALG_CMAC                       1

#endif /* PSA_CRYPTO_CONFIG_PROV_H */

/* [] END OF FILE */
