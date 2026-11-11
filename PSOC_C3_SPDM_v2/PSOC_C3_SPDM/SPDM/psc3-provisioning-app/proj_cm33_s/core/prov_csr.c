/*****************************************************************************
 * File Name        : prov_csr.c
 *
 * Description      : PKCS#10 CSR builder implementation. See prov_csr.h.
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

#include "prov_csr.h"
#include "prov_crypto.h"

/* Everything below is the ECDSA CSR path (mbedTLS X.509 writer). ML-DSA-87
 * builds its CSR by hand in prov_csr_mldsa.c, so this translation unit is
 * empty in an ml-dsa-87-only build. */
#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)

#include "mbedtls/x509_csr.h"
#include "mbedtls/pk.h"
#include "mbedtls/psa_util.h"

/******************************************************************************
 * Macros
 *****************************************************************************/

/* Upper bound on the RFC 1779 subject string we hand to mbedTLS. */
#define PROV_CSR_SUBJECT_MAX    (256U)

/******************************************************************************
 * Local helpers
 *****************************************************************************/

/**
 * @brief Map a template field id to its RFC 1779 attribute key.
 *
 * @param field  Field id from the TLV template.
 * @return Attribute key string ("CN", "O", ...), or NULL if unknown.
 */
static const char *prov_csr_field_key(uint8_t field)
{
    switch (field)
    {
        case PROV_CSR_FIELD_CN:     return "CN";
        case PROV_CSR_FIELD_O:      return "O";
        case PROV_CSR_FIELD_OU:     return "OU";
        case PROV_CSR_FIELD_C:      return "C";
        case PROV_CSR_FIELD_SERIAL: return "serialNumber";
        default:                    return NULL;
    }
}

/**
 * @brief Append "key=value" (optionally preceded by ',') to a subject string.
 *
 * @param subj      Destination subject string buffer.
 * @param cap       Capacity of @p subj.
 * @param used      In/out current string length (excluding NUL).
 * @param key       Attribute key.
 * @param val       Attribute value bytes (not necessarily NUL-terminated).
 * @param val_len   Attribute value length.
 * @return 0 on success, -1 on overflow or invalid value char.
 */
static int prov_csr_append(char *subj, size_t cap, size_t *used,
                           const char *key, const uint8_t *val, size_t val_len)
{
    size_t key_len = strlen(key);
    size_t pos = *used;
    size_t i;

    /* leading comma between RDNs */
    if (pos != 0U)
    {
        if ((pos + 1U) >= cap)
        {
            return -1;
        }
        subj[pos++] = ',';
    }

    if ((pos + key_len + 1U) >= cap)
    {
        return -1;
    }
    (void)memcpy(&subj[pos], key, key_len);
    pos += key_len;
    subj[pos++] = '=';

    for (i = 0U; i < val_len; i++)
    {
        uint8_t c = val[i];

        /* Reject control chars and RFC 1779 metacharacters to keep the
         * generated string unambiguous (no escaping logic on-device). */
        if ((c < 0x20U) || (c > 0x7EU) ||
            (c == ',') || (c == '=') || (c == '+') ||
            (c == '"') || (c == '\\') || (c == '<') || (c == '>') ||
            (c == ';'))
        {
            return -1;
        }
        if ((pos + 1U) >= cap)
        {
            return -1;
        }
        subj[pos++] = (char)c;
    }

    subj[pos] = '\0';
    *used = pos;
    return 0;
}

/**
 * @brief Build the subject string from the TLV template (or a default CN).
 *
 * @param templ      Template TLV blob (may be NULL).
 * @param templ_len  Template length.
 * @param uid        Device UID (for the default CN).
 * @param uid_len    Device UID length.
 * @param subj       Destination subject string.
 * @param cap        Capacity of @p subj.
 * @return 0 on success, -1 on malformed template / overflow.
 */
static int prov_csr_make_subject(const uint8_t *templ, size_t templ_len,
                                 const uint8_t *uid, size_t uid_len,
                                 char *subj, size_t cap)
{
    static const char k_hex[] = "0123456789ABCDEF";
    size_t used = 0U;

    subj[0] = '\0';

    if ((templ != NULL) && (templ_len > 0U))
    {
        size_t i = 0U;

        while (i < templ_len)
        {
            uint8_t field;
            uint8_t flen;
            const char *key;

            /* Need at least tag + length. */
            if ((i + 2U) > templ_len)
            {
                return -1;
            }
            field = templ[i];
            flen = templ[i + 1U];
            i += 2U;

            if ((size_t)(i + flen) > templ_len)
            {
                return -1;
            }

            key = prov_csr_field_key(field);
            if ((key == NULL) || (flen == 0U))
            {
                return -1;
            }
            if (prov_csr_append(subj, cap, &used, key, &templ[i], flen) != 0)
            {
                return -1;
            }
            i += flen;
        }
    }

    /* Default subject (or fallback if the template carried no CN-equivalent):
     * CN = hex(UID). */
    if (used == 0U)
    {
        char cn[2U * 16U + 1U];   /* up to 16-byte UID -> 32 hex chars */
        size_t n = (uid_len > 16U) ? 16U : uid_len;
        size_t j;

        if ((uid == NULL) || (uid_len == 0U))
        {
            return -1;
        }
        for (j = 0U; j < n; j++)
        {
            cn[(2U * j)]      = k_hex[(uid[j] >> 4) & 0x0FU];
            cn[(2U * j) + 1U] = k_hex[uid[j] & 0x0FU];
        }
        if (prov_csr_append(subj, cap, &used, "CN",
                            (const uint8_t *)cn, 2U * n) != 0)
        {
            return -1;
        }
    }

    return 0;
}

/******************************************************************************
 * Public API
 *****************************************************************************/

int prov_csr_build_ec(prov_ec_curve_t curve,
                      const uint8_t *wrapped, size_t wrapped_len,
                      const uint8_t *templ, size_t templ_len,
                      const uint8_t *uid, size_t uid_len,
                      uint8_t *out, size_t out_cap, size_t *out_len)
{
    char subject[PROV_CSR_SUBJECT_MAX];
    mbedtls_x509write_csr csr;
    mbedtls_pk_context pk;
    psa_key_id_t key_id = PSA_KEY_ID_NULL;
    psa_status_t pstatus;
    mbedtls_md_type_t md;
    int ret = -1;
    int written;

    if ((wrapped == NULL) || (out == NULL) || (out_len == NULL))
    {
        return -1;
    }

    /* Pair the CSR message digest with the curve strength (same pairing the
     * signature uses): P-256 -> SHA-256, P-384 -> SHA-384, P-521 -> SHA-512. */
    switch (curve)
    {
#if defined(PROV_ENABLE_EC256)
        case PROV_EC_CURVE_P256: md = MBEDTLS_MD_SHA256; break;
#endif
#if defined(PROV_ENABLE_EC384)
        case PROV_EC_CURVE_P384: md = MBEDTLS_MD_SHA384; break;
#endif
#if defined(PROV_ENABLE_EC521)
        case PROV_EC_CURVE_P521: md = MBEDTLS_MD_SHA512; break;
#endif
        default:                 return -1;
    }

    mbedtls_x509write_csr_init(&csr);
    mbedtls_pk_init(&pk);

    /* Import the wrapped device key into a volatile PSA key and wrap it as an
     * opaque PK so the CSR is signed inside the secure world. */
    pstatus = prov_crypto_import_priv_ec(curve, wrapped, wrapped_len, &key_id);
    if (pstatus != PSA_SUCCESS)
    {
        goto cleanup;
    }

    if (mbedtls_pk_setup_opaque(&pk, key_id) != 0)
    {
        goto cleanup;
    }

    if (prov_csr_make_subject(templ, templ_len, uid, uid_len,
                              subject, sizeof(subject)) != 0)
    {
        goto cleanup;
    }

    if (mbedtls_x509write_csr_set_subject_name(&csr, subject) != 0)
    {
        goto cleanup;
    }

    mbedtls_x509write_csr_set_key(&csr, &pk);
    mbedtls_x509write_csr_set_md_alg(&csr, md);

    /* DER is written to the END of the buffer; 'written' is its length. */
    written = mbedtls_x509write_csr_der(&csr, out, out_cap,
                                        mbedtls_psa_get_random,
                                        MBEDTLS_PSA_RANDOM_STATE);
    if (written < 0)
    {
        goto cleanup;
    }

    /* Move the DER to the front of the buffer for streaming read-back. */
    (void)memmove(out, out + (out_cap - (size_t)written), (size_t)written);
    *out_len = (size_t)written;
    ret = 0;

cleanup:
    mbedtls_x509write_csr_free(&csr);
    mbedtls_pk_free(&pk);
    (void)prov_crypto_destroy_key(key_id);
    return ret;
}
#endif /* PROV_ENABLE_EC256 || PROV_ENABLE_EC384 || PROV_ENABLE_EC521 */

/* [] END OF FILE */
