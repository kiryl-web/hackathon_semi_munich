/*****************************************************************************
 * File Name        : prov_crypto.c
 *
 * Description      : PSA-Crypto wrapper implementation for the secure
 *                    provisioning service. See prov_crypto.h for the contract.
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

/******************************************************************************
 * Header Files
 *****************************************************************************/

#include <string.h>
#include <stdlib.h>

#include "cy_pdl.h"

#include "prov_crypto.h"

#if defined(PROV_ENABLE_MLDSA87)
/* Post-quantum suite: ML-DSA-87 keygen/sign are provided by wolfCrypt's
 * pure-C Dilithium (mbedTLS/PSA cannot do ML-DSA). Configuration lives in
 * wolfssl/user_settings.h (selected via WOLFSSL_USER_SETTINGS). */
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/dilithium.h"
#include "wolfssl/wolfcrypt/random.h"
#endif

/******************************************************************************
 * Local helpers
 *****************************************************************************/

/* AES-256-GCM for private-key wrapping. */
#define PROV_WRAP_ALG       PSA_ALG_GCM
#define PROV_WRAP_KEY_BITS  (256U)
#define PROV_WRAP_KEY_BYTES (PROV_WRAP_KEY_BITS / 8U)

/* Device Hardware Unique Key (HUK) source: 32-byte field in supervisory flash.
 * Only the secure world can read it; it is the root secret the wrapping key is
 * derived from. */
#define PROV_HUK_BYTES      (32U)

/**
 * @brief Obtain the device key-wrapping key (volatile AES-256 PSA key).
 *
 * The wrapping key is derived from the device Hardware Unique Key (HUK) via
 * HKDF-SHA256 (PSA key derivation). Because the HUK is unique per silicon and
 * only readable by the secure world, the resulting AES-256 key — and therefore
 * every private key wrapped with it — is bound to this device and cannot be
 * unwrapped off-chip. The derivation context string domain-separates this key
 * from any other future HUK-derived key.
 *
 * Isolating this in a single function keeps the key-derivation surface in one
 * place.
 *
 * @param key_id  Receives the volatile wrapping-key id (destroy when done).
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
static psa_status_t prov_device_wrap_key(psa_key_id_t *key_id)
{
    /* Domain-separation label bound into the HKDF 'info' input. */
    static const uint8_t k_wrap_info[] = "PROV_DEV_WRAP_KEY_v1";

    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_attributes_t huk_attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t huk_id = PSA_KEY_ID_NULL;
    psa_status_t status;

    *key_id = PSA_KEY_ID_NULL;

    /* The HKDF secret (IKM) must be supplied as a key object — not as raw bytes
     * via psa_key_derivation_input_bytes() — otherwise deriving a *key* output
     * (psa_key_derivation_output_key) is rejected with PSA_ERROR_NOT_PERMITTED.
     * Import the HUK as a DERIVE key with USAGE_DERIVE for HKDF-SHA256. */
    psa_set_key_usage_flags(&huk_attr, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&huk_attr, PSA_ALG_HKDF(PSA_ALG_SHA_256));
    psa_set_key_type(&huk_attr, PSA_KEY_TYPE_DERIVE);
    status = psa_import_key(&huk_attr, (const uint8_t *)SFLASH->HUK,
                            PROV_HUK_BYTES, &huk_id);
    psa_reset_key_attributes(&huk_attr);
    if (status != PSA_SUCCESS)
    {
        return status;
    }

    status = psa_key_derivation_setup(&op, PSA_ALG_HKDF(PSA_ALG_SHA_256));

    /* Feed the HUK key as the HKDF input secret (IKM). */
    if (status == PSA_SUCCESS)
    {
        status = psa_key_derivation_input_key(&op,
                                              PSA_KEY_DERIVATION_INPUT_SECRET,
                                              huk_id);
    }
    if (status == PSA_SUCCESS)
    {
        status = psa_key_derivation_input_bytes(&op,
                                                PSA_KEY_DERIVATION_INPUT_INFO,
                                                k_wrap_info,
                                                sizeof(k_wrap_info) - 1U);
    }

    if (status == PSA_SUCCESS)
    {
        psa_set_key_usage_flags(&attr,
                                PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
        psa_set_key_algorithm(&attr, PROV_WRAP_ALG);
        psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
        psa_set_key_bits(&attr, PROV_WRAP_KEY_BITS);

        status = psa_key_derivation_output_key(&attr, &op, key_id);
    }

    psa_reset_key_attributes(&attr);
    (void)psa_key_derivation_abort(&op);
    (void)psa_destroy_key(huk_id);
    return status;
}

/******************************************************************************
 * Public API
 *****************************************************************************/

psa_status_t prov_crypto_init(void)
{
    /* psa_crypto_init() is idempotent; re-initialisation returns PSA_SUCCESS. */
    return psa_crypto_init();
}

psa_status_t prov_crypto_sha256(const uint8_t *msg, size_t len,
                                uint8_t out[PROV_CRYPTO_HASH_LEN])
{
    size_t hash_len = 0U;

    if (out == NULL)
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    return psa_hash_compute(PSA_ALG_SHA_256, msg, len,
                            out, PROV_CRYPTO_HASH_LEN, &hash_len);
}

psa_status_t prov_crypto_sha256_start(psa_hash_operation_t *op)
{
    if (op == NULL)
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    *op = psa_hash_operation_init();
    return psa_hash_setup(op, PSA_ALG_SHA_256);
}

psa_status_t prov_crypto_sha256_update(psa_hash_operation_t *op,
                                       const uint8_t *data, size_t len)
{
    psa_status_t status;

    if (op == NULL)
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    status = psa_hash_update(op, data, len);
    if (status != PSA_SUCCESS)
    {
        (void)psa_hash_abort(op);
    }
    return status;
}

psa_status_t prov_crypto_sha256_finish(psa_hash_operation_t *op,
                                       uint8_t out[PROV_CRYPTO_HASH_LEN])
{
    size_t hash_len = 0U;

    if ((op == NULL) || (out == NULL))
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    return psa_hash_finish(op, out, PROV_CRYPTO_HASH_LEN, &hash_len);
}

psa_status_t prov_crypto_destroy_key(psa_key_id_t key_id)
{
    if (key_id == PSA_KEY_ID_NULL)
    {
        return PSA_SUCCESS;
    }
    return psa_destroy_key(key_id);
}

psa_status_t prov_crypto_wrap(const uint8_t *plain, size_t plain_len,
                              uint8_t *out, size_t out_cap, size_t *out_len)
{
    psa_key_id_t key_id = PSA_KEY_ID_NULL;
    size_t ct_len = 0U;
    psa_status_t status;
    psa_status_t dstatus;

    if ((out == NULL) || (out_len == NULL) ||
        ((plain == NULL) && (plain_len != 0U)))
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (out_cap < (plain_len + PROV_CRYPTO_WRAP_OVERHEAD))
    {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    status = prov_device_wrap_key(&key_id);
    if (status != PSA_SUCCESS)
    {
        return status;
    }

    /* Draw a fresh random nonce and place it at the front of the blob. */
    status = psa_generate_random(out, PROV_CRYPTO_GCM_NONCE);
    if (status != PSA_SUCCESS)
    {
        (void)psa_destroy_key(key_id);
        return status;
    }

    /* ciphertext||tag goes immediately after the nonce. */
    status = psa_aead_encrypt(key_id, PROV_WRAP_ALG,
                              out, PROV_CRYPTO_GCM_NONCE,
                              NULL, 0U,
                              plain, plain_len,
                              out + PROV_CRYPTO_GCM_NONCE,
                              out_cap - PROV_CRYPTO_GCM_NONCE,
                              &ct_len);
    dstatus = psa_destroy_key(key_id);
    if (status != PSA_SUCCESS)
    {
        return status;
    }
    if (dstatus != PSA_SUCCESS)
    {
        return dstatus;
    }

    *out_len = PROV_CRYPTO_GCM_NONCE + ct_len;
    return PSA_SUCCESS;
}

psa_status_t prov_crypto_unwrap(const uint8_t *wrapped, size_t wrapped_len,
                                uint8_t *out, size_t out_cap, size_t *out_len)
{
    psa_key_id_t key_id = PSA_KEY_ID_NULL;
    psa_status_t status;
    psa_status_t dstatus;

    if ((wrapped == NULL) || (out == NULL) || (out_len == NULL))
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (wrapped_len < PROV_CRYPTO_WRAP_OVERHEAD)
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    status = prov_device_wrap_key(&key_id);
    if (status != PSA_SUCCESS)
    {
        return status;
    }

    status = psa_aead_decrypt(key_id, PROV_WRAP_ALG,
                              wrapped, PROV_CRYPTO_GCM_NONCE,
                              NULL, 0U,
                              wrapped + PROV_CRYPTO_GCM_NONCE,
                              wrapped_len - PROV_CRYPTO_GCM_NONCE,
                              out, out_cap, out_len);
    dstatus = psa_destroy_key(key_id);
    if (status != PSA_SUCCESS)
    {
        return status;
    }
    return dstatus;
}

#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)
/******************************************************************************
 * Generic (curve-parameterised) ECDSA — P-256 / P-384 / P-521
 *
 * A single code path serves every supported SECP_R1 curve: an explicit curve
 * selector resolves all per-curve constants (key bits, raw lengths and the
 * paired ECDSA hash) through one descriptor table, so adding another curve is
 * a one-line change.
 *****************************************************************************/

typedef struct
{
    size_t          key_bits;   /* PSA key bit length                       */
    size_t          priv_len;   /* raw private scalar length                */
    size_t          pub_len;    /* raw public key X||Y length               */
    size_t          sig_len;    /* raw signature r||s length                */
    psa_algorithm_t sign_alg;   /* randomised ECDSA over the paired hash     */
} prov_ec_desc_t;

static bool prov_ec_descriptor(prov_ec_curve_t curve, prov_ec_desc_t *d)
{
    switch (curve)
    {
#if defined(PROV_ENABLE_EC256)
    case PROV_EC_CURVE_P256:
        d->key_bits = 256U;
        d->priv_len = 32U;
        d->pub_len  = 64U;
        d->sig_len  = 64U;
        d->sign_alg = PSA_ALG_ECDSA(PSA_ALG_SHA_256);
        return true;
#endif
#if defined(PROV_ENABLE_EC384)
    case PROV_EC_CURVE_P384:
        d->key_bits = 384U;
        d->priv_len = 48U;
        d->pub_len  = 96U;
        d->sig_len  = 96U;
        d->sign_alg = PSA_ALG_ECDSA(PSA_ALG_SHA_384);
        return true;
#endif
#if defined(PROV_ENABLE_EC521)
    case PROV_EC_CURVE_P521:
        d->key_bits = 521U;
        d->priv_len = 66U;
        d->pub_len  = 132U;
        d->sig_len  = 132U;
        d->sign_alg = PSA_ALG_ECDSA(PSA_ALG_SHA_512);
        return true;
#endif
    default:
        break;
    }
    return false;
}

psa_status_t prov_crypto_import_priv_ec(prov_ec_curve_t curve,
                                        const uint8_t *wrapped,
                                        size_t wrapped_len,
                                        psa_key_id_t *key_id)
{
    prov_ec_desc_t d;
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    uint8_t priv[PROV_CRYPTO_EC_PRIV_MAX];
    size_t priv_len = 0U;
    psa_status_t status;

    if ((wrapped == NULL) || (key_id == NULL))
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    *key_id = PSA_KEY_ID_NULL;
    if (!prov_ec_descriptor(curve, &d))
    {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    status = prov_crypto_unwrap(wrapped, wrapped_len,
                                priv, sizeof(priv), &priv_len);
    if ((status != PSA_SUCCESS) || (priv_len != d.priv_len))
    {
        (void)memset(priv, 0, sizeof(priv));
        return (status == PSA_SUCCESS) ? PSA_ERROR_INVALID_ARGUMENT : status;
    }

    psa_set_key_usage_flags(&attr,
                            PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attr, d.sign_alg);
    psa_set_key_type(&attr,
                     PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, d.key_bits);

    status = psa_import_key(&attr, priv, priv_len, key_id);
    psa_reset_key_attributes(&attr);
    (void)memset(priv, 0, sizeof(priv));
    return status;
}

psa_status_t prov_crypto_keygen_ec(prov_ec_curve_t curve,
                                   uint8_t *pub, size_t pub_cap, size_t *pub_len,
                                   uint8_t *wrapped, size_t wrapped_cap,
                                   size_t *wrapped_len)
{
    prov_ec_desc_t d;
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = PSA_KEY_ID_NULL;
    uint8_t pub_full[1U + PROV_CRYPTO_EC_PUB_MAX];   /* 0x04 || X || Y */
    uint8_t priv[PROV_CRYPTO_EC_PRIV_MAX];
    size_t out_len = 0U;
    psa_status_t status;

    if ((pub == NULL) || (pub_len == NULL) ||
        (wrapped == NULL) || (wrapped_len == NULL))
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (!prov_ec_descriptor(curve, &d))
    {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    if (pub_cap < d.pub_len)
    {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    psa_set_key_usage_flags(&attr,
                            PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attr, d.sign_alg);
    psa_set_key_type(&attr,
                     PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, d.key_bits);

    status = psa_generate_key(&attr, &key_id);
    psa_reset_key_attributes(&attr);
    if (status != PSA_SUCCESS)
    {
        return status;
    }

    /* Export the raw public key: PSA returns 0x04 || X || Y. */
    status = psa_export_public_key(key_id, pub_full, 1U + d.pub_len, &out_len);
    if ((status != PSA_SUCCESS) || (out_len != (1U + d.pub_len)) ||
        (pub_full[0] != 0x04U))
    {
        (void)psa_destroy_key(key_id);
        return (status == PSA_SUCCESS) ? PSA_ERROR_GENERIC_ERROR : status;
    }
    (void)memcpy(pub, &pub_full[1], d.pub_len);
    *pub_len = d.pub_len;

    /* Export and immediately wrap the private scalar. */
    status = psa_export_key(key_id, priv, sizeof(priv), &out_len);
    (void)psa_destroy_key(key_id);
    if ((status != PSA_SUCCESS) || (out_len != d.priv_len))
    {
        (void)memset(priv, 0, sizeof(priv));
        return (status == PSA_SUCCESS) ? PSA_ERROR_GENERIC_ERROR : status;
    }

    status = prov_crypto_wrap(priv, d.priv_len,
                              wrapped, wrapped_cap, wrapped_len);
    (void)memset(priv, 0, sizeof(priv));
    return status;
}

psa_status_t prov_crypto_sign_ec(prov_ec_curve_t curve,
                                 const uint8_t *wrapped, size_t wrapped_len,
                                 const uint8_t *msg, size_t msg_len,
                                 uint8_t *sig, size_t sig_cap, size_t *sig_len)
{
    prov_ec_desc_t d;
    psa_key_id_t key_id = PSA_KEY_ID_NULL;
    size_t out_len = 0U;
    psa_status_t status;

    if ((sig == NULL) || (sig_len == NULL))
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (!prov_ec_descriptor(curve, &d))
    {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    if (sig_cap < d.sig_len)
    {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    status = prov_crypto_import_priv_ec(curve, wrapped, wrapped_len, &key_id);
    if (status != PSA_SUCCESS)
    {
        return status;
    }

    status = psa_sign_message(key_id, d.sign_alg, msg, msg_len,
                              sig, sig_cap, &out_len);
    (void)prov_crypto_destroy_key(key_id);

    if ((status == PSA_SUCCESS) && (out_len != d.sig_len))
    {
        return PSA_ERROR_GENERIC_ERROR;
    }
    *sig_len = out_len;
    return status;
}
#endif /* PROV_ENABLE_EC256 || PROV_ENABLE_EC384 || PROV_ENABLE_EC521 */


#if defined(PROV_ENABLE_MLDSA87)
/******************************************************************************
 * ML-DSA-87 (post-quantum) — wolfCrypt Dilithium
 *
 * The wolfCrypt working objects are large (a dilithium_key embeds the 2592-byte
 * public and 4896-byte private keys, ~7.7 KB total) and are therefore held as
 * file-static state rather than on the secure stack. The secure core is
 * single-threaded and the provisioning service serialises requests, so a single
 * shared instance guarded by a busy flag is sufficient; the flag also guards
 * against re-entrancy. Every buffer is zeroised before the object is released.
 *
 * The raw private key is deliberately NOT static: it is allocated on the secure
 * heap only while it is needed (after MakeKey returns / before SignCtx runs), so
 * it never coexists with wolfCrypt's ~26 KB SMALL_MEM keygen/sign peak. Keeping
 * it off .bss also grows the linker-leftover heap by the same 4896 bytes.
 *****************************************************************************/

static dilithium_key s_mldsa_key;
static WC_RNG        s_mldsa_rng;
static bool          s_mldsa_busy;

psa_status_t prov_crypto_keygen_mldsa87(
    uint8_t pub[PROV_CRYPTO_MLDSA87_PUB_LEN],
    uint8_t *wrapped, size_t wrapped_cap, size_t *wrapped_len)
{
    word32 pub_len = PROV_CRYPTO_MLDSA87_PUB_LEN;
    word32 priv_len = PROV_CRYPTO_MLDSA87_PRIV_LEN;
    psa_status_t status = PSA_ERROR_GENERIC_ERROR;
    uint8_t *priv = NULL;
    bool key_init = false;
    bool rng_init = false;
    int ret;

    if ((pub == NULL) || (wrapped == NULL) || (wrapped_len == NULL))
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (s_mldsa_busy)
    {
        return PSA_ERROR_BAD_STATE;
    }
    s_mldsa_busy = true;

    ret = wc_MlDsaKey_Init(&s_mldsa_key, NULL, INVALID_DEVID);
    if (ret == 0)
    {
        key_init = true;
        ret = wc_MlDsaKey_SetParams(&s_mldsa_key, WC_ML_DSA_87);
    }
    if (ret == 0)
    {
        ret = wc_InitRng(&s_mldsa_rng);
        if (ret == 0)
        {
            rng_init = true;
        }
    }
    if (ret == 0)
    {
        ret = wc_MlDsaKey_MakeKey(&s_mldsa_key, &s_mldsa_rng);
    }
    if (ret == 0)
    {
        ret = wc_MlDsaKey_ExportPubRaw(&s_mldsa_key, pub, &pub_len);
    }
    if (ret == 0)
    {
        /* Allocate the raw private key only now that MakeKey has returned (and
         * freed its ~26 KB SMALL_MEM working set), so it never coexists with
         * the keygen peak. */
        priv = malloc(PROV_CRYPTO_MLDSA87_PRIV_LEN);
        if (priv == NULL)
        {
            status = PSA_ERROR_INSUFFICIENT_MEMORY;
        }
        else
        {
            ret = wc_MlDsaKey_ExportPrivRaw(&s_mldsa_key, priv, &priv_len);
        }
    }

    if ((ret == 0) && (priv != NULL) &&
        (pub_len == PROV_CRYPTO_MLDSA87_PUB_LEN) &&
        (priv_len == PROV_CRYPTO_MLDSA87_PRIV_LEN))
    {
        status = prov_crypto_wrap(priv, priv_len,
                                  wrapped, wrapped_cap, wrapped_len);
    }

    if (priv != NULL)
    {
        (void)memset(priv, 0, PROV_CRYPTO_MLDSA87_PRIV_LEN);
        free(priv);
    }
    if (rng_init)
    {
        (void)wc_FreeRng(&s_mldsa_rng);
    }
    if (key_init)
    {
        (void)wc_MlDsaKey_Free(&s_mldsa_key);
    }
    s_mldsa_busy = false;
    return status;
}

psa_status_t prov_crypto_sign_mldsa87(const uint8_t *wrapped, size_t wrapped_len,
                                      const uint8_t *msg, size_t msg_len,
                                      uint8_t *sig, size_t sig_cap,
                                      size_t *sig_len)
{
    word32 out_sig_len = 0U;
    size_t priv_len = 0U;
    psa_status_t status = PSA_ERROR_GENERIC_ERROR;
    uint8_t *priv = NULL;
    bool key_init = false;
    bool rng_init = false;
    int ret;

    if ((wrapped == NULL) || (sig == NULL) || (sig_len == NULL) ||
        ((msg == NULL) && (msg_len != 0U)))
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (sig_cap > (size_t)UINT32_MAX)
    {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (s_mldsa_busy)
    {
        return PSA_ERROR_BAD_STATE;
    }
    s_mldsa_busy = true;

    /* Raw private key on the secure heap: it is freed right after
     * wc_MlDsaKey_ImportPrivRaw() copies it into s_mldsa_key (below), i.e.
     * before wc_MlDsaKey_SignCtx()'s ~26 KB SMALL_MEM peak, so it never
     * coexists with the sign peak. */
    priv = malloc(PROV_CRYPTO_MLDSA87_PRIV_LEN);
    if (priv == NULL)
    {
        status = PSA_ERROR_INSUFFICIENT_MEMORY;
        goto cleanup;
    }

    status = prov_crypto_unwrap(wrapped, wrapped_len,
                                priv, PROV_CRYPTO_MLDSA87_PRIV_LEN, &priv_len);
    if ((status != PSA_SUCCESS) ||
        (priv_len != PROV_CRYPTO_MLDSA87_PRIV_LEN))
    {
        status = (status == PSA_SUCCESS) ? PSA_ERROR_INVALID_ARGUMENT : status;
        goto cleanup;
    }

    status = PSA_ERROR_GENERIC_ERROR;
    ret = wc_MlDsaKey_Init(&s_mldsa_key, NULL, INVALID_DEVID);
    if (ret == 0)
    {
        key_init = true;
        ret = wc_MlDsaKey_SetParams(&s_mldsa_key, WC_ML_DSA_87);
    }
    if (ret == 0)
    {
        ret = wc_MlDsaKey_ImportPrivRaw(&s_mldsa_key, priv,
                                        (word32)priv_len);
    }

    /* Key material now lives inside s_mldsa_key; release the heap copy before
     * the large SignCtx allocation so the two never coexist. */
    (void)memset(priv, 0, PROV_CRYPTO_MLDSA87_PRIV_LEN);
    free(priv);
    priv = NULL;

    if (ret == 0)
    {
        ret = wc_InitRng(&s_mldsa_rng);
        if (ret == 0)
        {
            rng_init = true;
        }
    }
    if (ret == 0)
    {
        /* Pure ML-DSA (FIPS 204 Algorithm 2) with an empty context. The plain
         * wc_MlDsaKey_Sign() hashes mu = H(tr || M) and omits the mandatory
         * 0x00 || ctxLen domain separator, so its output does not verify
         * against standards-compliant verifiers (OpenSSL/cryptography). The
         * SignCtx form with ctx=NULL,len=0 produces mu = H(tr || 0x00||0x00 ||
         * M), which interoperates with the host CA. */
        out_sig_len = (word32)sig_cap;
        ret = wc_MlDsaKey_SignCtx(&s_mldsa_key, NULL, 0,
                                  sig, &out_sig_len,
                                  msg, (word32)msg_len, &s_mldsa_rng);
    }

    if (ret == 0)
    {
        *sig_len = (size_t)out_sig_len;
        status = PSA_SUCCESS;
    }

cleanup:
    if (priv != NULL)
    {
        (void)memset(priv, 0, PROV_CRYPTO_MLDSA87_PRIV_LEN);
        free(priv);
    }
    if (rng_init)
    {
        (void)wc_FreeRng(&s_mldsa_rng);
    }
    if (key_init)
    {
        (void)wc_MlDsaKey_Free(&s_mldsa_key);
    }
    s_mldsa_busy = false;
    return status;
}
#endif /* PROV_ENABLE_MLDSA87 */

/* [] END OF FILE */
