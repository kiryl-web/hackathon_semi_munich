/*****************************************************************************
 * File Name        : prov_crypto.h
 *
 * Description      : PSA-Crypto wrapper for the secure provisioning service.
 *
 *                    Centralises every cryptographic primitive the service
 *                    needs so the higher layers (prov_svc, prov_csr) never call
 *                    PSA directly:
 *                      - key-pair generation (ECDSA P-256);
 *                      - raw SHA-256 (one-shot and streaming/multipart);
 *                      - ECDSA signing over a message (PROV_PROVE_CERT);
 *                      - AES-GCM wrap / unwrap of the exported private scalar
 *                        before it is stored in the PAS key slot;
 *                      - importing a wrapped private key back into a volatile
 *                        PSA key (for CSR signing).
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

#ifndef PROV_CRYPTO_H
#define PROV_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include "psa/crypto.h"

#if defined(__cplusplus)
extern "C" {
#endif

/******************************************************************************
 * Sizing constants
 *****************************************************************************/

/** SHA-256 digest length. */
#define PROV_CRYPTO_HASH_LEN    (32U)

/** AES-GCM wrap overhead added to the plaintext: nonce(12) + tag(16). */
#define PROV_CRYPTO_GCM_NONCE   (12U)
#define PROV_CRYPTO_GCM_TAG     (16U)
#define PROV_CRYPTO_WRAP_OVERHEAD   (PROV_CRYPTO_GCM_NONCE + PROV_CRYPTO_GCM_TAG)

/* Raw EC scratch sizes, set to the largest curve compiled into this build so a
 * P-256-only image is not forced to carry P-521-sized buffers. The sizes are
 * monotonic in curve order (P-256 < P-384 < P-521), so the max of any enabled
 * subset is just the largest enabled curve:
 *   P-256 priv 32 / pub 64  / sig 64
 *   P-384 priv 48 / pub 96  / sig 96
 *   P-521 priv 66 / pub 132 / sig 132  (field = ceil(521/8) = 66 bytes). */
#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)
#if defined(PROV_ENABLE_EC521)
#define PROV_CRYPTO_EC_PRIV_MAX     (66U)
#define PROV_CRYPTO_EC_PUB_MAX      (132U)
#define PROV_CRYPTO_EC_SIG_MAX      (132U)
#elif defined(PROV_ENABLE_EC384)
#define PROV_CRYPTO_EC_PRIV_MAX     (48U)
#define PROV_CRYPTO_EC_PUB_MAX      (96U)
#define PROV_CRYPTO_EC_SIG_MAX      (96U)
#elif defined(PROV_ENABLE_EC256)
#define PROV_CRYPTO_EC_PRIV_MAX     (32U)
#define PROV_CRYPTO_EC_PUB_MAX      (64U)
#define PROV_CRYPTO_EC_SIG_MAX      (64U)
#endif

/** Worst-case wrapped EC private-key length (priv scalar + GCM overhead). */
#define PROV_CRYPTO_EC_WRAP_MAX \
    (PROV_CRYPTO_EC_PRIV_MAX + PROV_CRYPTO_WRAP_OVERHEAD)

/**
 * EC curve selector for the generic key / sign / import helpers. All supported
 * NIST curves (P-256 / P-384 / P-521) run through a single curve-parameterised
 * code path (prov_crypto_*_ec()); each curve is compiled in only when its
 * matching PROV_ENABLE_EC* build switch is selected.
 */
typedef enum
{
    PROV_EC_CURVE_P256 = 0,
    PROV_EC_CURVE_P384 = 1,
    PROV_EC_CURVE_P521 = 2
} prov_ec_curve_t;
#endif /* PROV_ENABLE_EC256 || PROV_ENABLE_EC384 || PROV_ENABLE_EC521 */

#if defined(PROV_ENABLE_MLDSA87)
/** Raw ML-DSA-87 (FIPS 204, category 5) public key length. */
#define PROV_CRYPTO_MLDSA87_PUB_LEN     (2592U)

/** Raw ML-DSA-87 private key length. */
#define PROV_CRYPTO_MLDSA87_PRIV_LEN    (4896U)

/** ML-DSA-87 signature length. */
#define PROV_CRYPTO_MLDSA87_SIG_LEN     (4627U)

/** Worst-case wrapped ML-DSA-87 private-key length (priv + GCM overhead). */
#define PROV_CRYPTO_MLDSA87_WRAP_MAX \
    (PROV_CRYPTO_MLDSA87_PRIV_LEN + PROV_CRYPTO_WRAP_OVERHEAD)
#endif /* PROV_ENABLE_MLDSA87 */

/******************************************************************************
 * API
 *****************************************************************************/

/**
 * @brief Initialise the PSA crypto subsystem.
 *
 * Idempotent — safe to call once from secure start-up (prov_svc_init()).
 *
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_init(void);

/**
 * @brief One-shot SHA-256.
 *
 * @param msg  Input message (may be NULL iff @p len == 0).
 * @param len  Message length in bytes.
 * @param out  Receives the digest (PROV_CRYPTO_HASH_LEN bytes).
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_sha256(const uint8_t *msg, size_t len,
                                uint8_t out[PROV_CRYPTO_HASH_LEN]);

/* ---- multipart SHA-256 (used to hash a certificate streamed from flash) ---- */

/**
 * @brief Begin a multipart SHA-256 computation.
 * @param op  Caller-owned operation object to set up.
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_sha256_start(psa_hash_operation_t *op);

/**
 * @brief Feed a chunk into a multipart SHA-256 computation.
 * @param op   Operation started with prov_crypto_sha256_start().
 * @param data Chunk bytes.
 * @param len  Chunk length.
 * @return PSA_SUCCESS on success, or a PSA error code (op is aborted on error).
 */
psa_status_t prov_crypto_sha256_update(psa_hash_operation_t *op,
                                       const uint8_t *data, size_t len);

/**
 * @brief Finish a multipart SHA-256 computation.
 * @param op   Operation started with prov_crypto_sha256_start().
 * @param out  Receives the digest (PROV_CRYPTO_HASH_LEN bytes).
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_sha256_finish(psa_hash_operation_t *op,
                                       uint8_t out[PROV_CRYPTO_HASH_LEN]);

#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)
/**
 * @brief Generate a fresh ECDSA device key pair on a selectable NIST curve.
 *
 * Single curve-parameterised key generator for P-256 (SHA-256), P-384
 * (SHA-384) and P-521 (SHA-512). Generates a volatile PSA key, exports the raw
 * public key (X||Y, no 0x04 prefix), exports and AES-GCM-wraps the raw private
 * scalar, then destroys the volatile key.
 *
 * @param curve        Curve selector (PROV_EC_CURVE_P256 / _P384 / _P521).
 * @param pub          Receives the raw public key (curve pub_len bytes).
 * @param pub_cap      Capacity of @p pub (>= curve pub_len).
 * @param pub_len      Receives the raw public-key length.
 * @param wrapped      Receives the wrapped private key.
 * @param wrapped_cap  Capacity of @p wrapped (>= PROV_CRYPTO_EC_WRAP_MAX).
 * @param wrapped_len  Receives the wrapped private-key length.
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_keygen_ec(prov_ec_curve_t curve,
                                   uint8_t *pub, size_t pub_cap, size_t *pub_len,
                                   uint8_t *wrapped, size_t wrapped_cap,
                                   size_t *wrapped_len);

/**
 * @brief Sign a message with a wrapped EC private key (randomised ECDSA).
 *
 * PSA hashes the message with the curve-paired digest (P-256 -> SHA-256,
 * P-384 -> SHA-384, P-521 -> SHA-512) and produces a raw r||s signature.
 *
 * @param curve        Curve selector (PROV_EC_CURVE_P256 / _P384 / _P521).
 * @param wrapped      Wrapped private key from prov_crypto_keygen_ec().
 * @param wrapped_len  Wrapped private-key length.
 * @param msg          Message to sign.
 * @param msg_len      Message length.
 * @param sig          Receives the signature (curve sig_len bytes).
 * @param sig_cap      Capacity of @p sig (>= curve sig_len).
 * @param sig_len      Receives the produced signature length.
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_sign_ec(prov_ec_curve_t curve,
                                 const uint8_t *wrapped, size_t wrapped_len,
                                 const uint8_t *msg, size_t msg_len,
                                 uint8_t *sig, size_t sig_cap, size_t *sig_len);

/**
 * @brief Import a wrapped EC private key into a volatile PSA key.
 *
 * Used by the CSR builder for all curves. The caller MUST destroy the returned
 * key with prov_crypto_destroy_key() when done. The imported key permits both
 * SIGN_HASH and SIGN_MESSAGE with the curve-paired ECDSA algorithm.
 *
 * @param curve        Curve selector (PROV_EC_CURVE_P256 / _P384 / _P521).
 * @param wrapped      Wrapped private key.
 * @param wrapped_len  Wrapped private-key length.
 * @param key_id       Receives the volatile PSA key id.
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_import_priv_ec(prov_ec_curve_t curve,
                                        const uint8_t *wrapped,
                                        size_t wrapped_len,
                                        psa_key_id_t *key_id);
#endif /* PROV_ENABLE_EC256 || PROV_ENABLE_EC384 || PROV_ENABLE_EC521 */

#if defined(PROV_ENABLE_MLDSA87)
/**
 * @brief Generate a fresh ML-DSA-87 device key pair (wolfCrypt Dilithium).
 *
 * Mirrors prov_crypto_keygen_p256() for the post-quantum suite: initialises a
 * wolfCrypt MlDsaKey at category 5, generates the pair from the hardware-seeded
 * Hash-DRBG, exports the raw public key (2592 B), exports and AES-GCM-wraps the
 * raw private key, then frees the wolfCrypt key so no plaintext private material
 * survives the call. The wrap uses the same device wrapping key as the EC path.
 *
 * @param pub          Receives the raw public key
 *                     (PROV_CRYPTO_MLDSA87_PUB_LEN bytes).
 * @param wrapped      Receives the wrapped private key.
 * @param wrapped_cap  Capacity of @p wrapped (>= PROV_CRYPTO_MLDSA87_WRAP_MAX).
 * @param wrapped_len  Receives the wrapped private-key length.
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_keygen_mldsa87(
    uint8_t pub[PROV_CRYPTO_MLDSA87_PUB_LEN],
    uint8_t *wrapped, size_t wrapped_cap, size_t *wrapped_len);

/**
 * @brief Sign a message with a wrapped ML-DSA-87 private key.
 *
 * Unwraps the private key, imports it into a wolfCrypt MlDsaKey, signs @p msg
 * (wolfCrypt hashes/encodes internally per FIPS 204), then frees the key.
 *
 * @param wrapped      Wrapped private key from prov_crypto_keygen_mldsa87().
 * @param wrapped_len  Wrapped private-key length.
 * @param msg          Message to sign.
 * @param msg_len      Message length.
 * @param sig          Receives the signature.
 * @param sig_cap      Capacity of @p sig (>= PROV_CRYPTO_MLDSA87_SIG_LEN).
 * @param sig_len      Receives the produced signature length.
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_sign_mldsa87(const uint8_t *wrapped, size_t wrapped_len,
                                      const uint8_t *msg, size_t msg_len,
                                      uint8_t *sig, size_t sig_cap,
                                      size_t *sig_len);
#endif /* PROV_ENABLE_MLDSA87 */

/**
 * @brief Destroy a PSA key obtained from prov_crypto_import_priv_ec().
 * @param key_id  Key id to destroy (PSA_KEY_ID_NULL is ignored).
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_destroy_key(psa_key_id_t key_id);

/**
 * @brief AES-GCM wrap a plaintext under the device wrapping key.
 *
 * Output format: nonce(12) || ciphertext(plain_len) || tag(16).
 *
 * @param plain     Plaintext bytes.
 * @param plain_len Plaintext length.
 * @param out       Receives the wrapped blob.
 * @param out_cap   Capacity of @p out (>= plain_len + PROV_CRYPTO_WRAP_OVERHEAD).
 * @param out_len   Receives the wrapped length.
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t prov_crypto_wrap(const uint8_t *plain, size_t plain_len,
                              uint8_t *out, size_t out_cap, size_t *out_len);

/**
 * @brief AES-GCM unwrap a blob produced by prov_crypto_wrap().
 *
 * @param wrapped     Wrapped blob (nonce || ciphertext || tag).
 * @param wrapped_len Wrapped length (>= PROV_CRYPTO_WRAP_OVERHEAD).
 * @param out         Receives the recovered plaintext.
 * @param out_cap     Capacity of @p out.
 * @param out_len     Receives the plaintext length.
 * @return PSA_SUCCESS on success, PSA_ERROR_INVALID_SIGNATURE on tag failure,
 *         or another PSA error code.
 */
psa_status_t prov_crypto_unwrap(const uint8_t *wrapped, size_t wrapped_len,
                                uint8_t *out, size_t out_cap, size_t *out_len);

#if defined(__cplusplus)
}
#endif

#endif /* PROV_CRYPTO_H */

/* [] END OF FILE */
