/*****************************************************************************
 * File Name        : prov_csr_mldsa.c
 *
 * Description      : Hand-built PKCS#10 CSR encoder for ML-DSA-87 keys.
 *
 *                    wolfCrypt is compiled with WOLFSSL_DILITHIUM_NO_ASN1, so
 *                    neither wolfSSL nor mbedTLS can serialise an ML-DSA
 *                    certification request. This module emits the DER by hand:
 *                    the CertificationRequestInfo (TBS) is written back-to-front
 *                    into the caller's buffer, the signatureAlgorithm and an
 *                    (empty header) BIT STRING are appended, the TBS is signed
 *                    in place, and the outer SEQUENCE header is prepended.
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

#if defined(PROV_ENABLE_MLDSA87)

/******************************************************************************
 * Macros
 *****************************************************************************/

/* Maximum number of RDNs accepted from a subject template. */
#define PROV_CSR_MAX_RDN        (8U)

/* Fixed 4-byte outer SEQUENCE header (30 82 HH LL): the ML-DSA CSR always
 * exceeds 255 bytes, so the long-form 2-length-byte encoding is always used. */
#define PROV_CSR_OUTER_HDR_LEN  (4U)

/* AlgorithmIdentifier for id-ml-dsa-87 (2.16.840.1.101.3.4.3.19), no params:
 * SEQUENCE { OID }. Reused for both the SPKI and the signatureAlgorithm. */
static const uint8_t k_mldsa_algid[] = {
    0x30, 0x0B, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
    0x65, 0x03, 0x04, 0x03, 0x13
};

/* version INTEGER 0 and an empty attributes [0] IMPLICIT SET. */
static const uint8_t k_version0[]   = { 0x02, 0x01, 0x00 };
static const uint8_t k_attrs_empty[] = { 0xA0, 0x00 };

/******************************************************************************
 * Reverse (back-to-front) DER writer
 *****************************************************************************/

typedef struct
{
    uint8_t *buf;   /* start of the destination buffer                     */
    size_t   pos;   /* next byte is written at buf[pos-1]; [pos,cap) filled */
} der_w;

static int der_put(der_w *w, const uint8_t *data, size_t len)
{
    if (w->pos < len)
    {
        return -1;
    }
    w->pos -= len;
    (void)memcpy(&w->buf[w->pos], data, len);
    return 0;
}

static int der_put_byte(der_w *w, uint8_t b)
{
    if (w->pos < 1U)
    {
        return -1;
    }
    w->pos -= 1U;
    w->buf[w->pos] = b;
    return 0;
}

/* Minimal-length DER length encoding (definite form). */
static int der_put_len(der_w *w, size_t len)
{
    if (len < 0x80U)
    {
        return der_put_byte(w, (uint8_t)len);
    }
    if (len < 0x100U)
    {
        if (der_put_byte(w, (uint8_t)len) != 0)
        {
            return -1;
        }
        return der_put_byte(w, 0x81U);
    }
    if (len < 0x10000U)
    {
        if (der_put_byte(w, (uint8_t)(len & 0xFFU)) != 0)
        {
            return -1;
        }
        if (der_put_byte(w, (uint8_t)((len >> 8) & 0xFFU)) != 0)
        {
            return -1;
        }
        return der_put_byte(w, 0x82U);
    }
    return -1;   /* CSR contents never reach 64 KiB */
}

static int der_put_hdr(der_w *w, uint8_t tag, size_t content_len)
{
    if (der_put_len(w, content_len) != 0)
    {
        return -1;
    }
    return der_put_byte(w, tag);
}

/******************************************************************************
 * Subject (Name) encoding
 *****************************************************************************/

/**
 * @brief Resolve a template field id to its DER attribute OID and value tag.
 *
 * @param field    Field id (PROV_CSR_FIELD_*).
 * @param oid      Receives the full OID TLV.
 * @param oid_len  Receives the OID TLV length.
 * @param vtag     Receives the DER string tag for the value.
 * @return 0 on success, -1 if the field is unknown.
 */
static int rdn_meta(uint8_t field, const uint8_t **oid, size_t *oid_len,
                    uint8_t *vtag)
{
    /* AttributeType OIDs under joint-iso-itu-t(2) ds(5) 4 (id-at). */
    static const uint8_t oid_cn[]     = { 0x06, 0x03, 0x55, 0x04, 0x03 };
    static const uint8_t oid_o[]      = { 0x06, 0x03, 0x55, 0x04, 0x0A };
    static const uint8_t oid_ou[]     = { 0x06, 0x03, 0x55, 0x04, 0x0B };
    static const uint8_t oid_c[]      = { 0x06, 0x03, 0x55, 0x04, 0x06 };
    static const uint8_t oid_serial[] = { 0x06, 0x03, 0x55, 0x04, 0x05 };

    switch (field)
    {
        case PROV_CSR_FIELD_CN:
            *oid = oid_cn;     *oid_len = sizeof(oid_cn);     *vtag = 0x0CU; break;
        case PROV_CSR_FIELD_O:
            *oid = oid_o;      *oid_len = sizeof(oid_o);      *vtag = 0x0CU; break;
        case PROV_CSR_FIELD_OU:
            *oid = oid_ou;     *oid_len = sizeof(oid_ou);     *vtag = 0x0CU; break;
        case PROV_CSR_FIELD_C:
            *oid = oid_c;      *oid_len = sizeof(oid_c);      *vtag = 0x13U; break;
        case PROV_CSR_FIELD_SERIAL:
            *oid = oid_serial; *oid_len = sizeof(oid_serial); *vtag = 0x13U; break;
        default:
            return -1;
    }
    return 0;
}

/**
 * @brief Write one RelativeDistinguishedName (SET { SEQ { OID, value } }).
 */
static int der_put_rdn(der_w *w, uint8_t field,
                       const uint8_t *val, size_t val_len)
{
    const uint8_t *oid = NULL;
    size_t oid_len = 0U;
    uint8_t vtag = 0U;
    size_t set_end;
    size_t i;

    if (rdn_meta(field, &oid, &oid_len, &vtag) != 0)
    {
        return -1;
    }

    /* Restrict values to printable ASCII (valid UTF8String/PrintableString). */
    for (i = 0U; i < val_len; i++)
    {
        if ((val[i] < 0x20U) || (val[i] > 0x7EU))
        {
            return -1;
        }
    }

    set_end = w->pos;

    /* AttributeValue (written first => appears last in the SEQUENCE). */
    if (der_put(w, val, val_len) != 0)
    {
        return -1;
    }
    if (der_put_hdr(w, vtag, val_len) != 0)
    {
        return -1;
    }
    /* AttributeType (OID). */
    if (der_put(w, oid, oid_len) != 0)
    {
        return -1;
    }
    /* AttributeTypeAndValue SEQUENCE. */
    if (der_put_hdr(w, 0x30U, set_end - w->pos) != 0)
    {
        return -1;
    }
    /* RelativeDistinguishedName SET. */
    if (der_put_hdr(w, 0x31U, set_end - w->pos) != 0)
    {
        return -1;
    }
    return 0;
}

/******************************************************************************
 * Public API
 *****************************************************************************/

int prov_csr_build_mldsa87(const uint8_t *wrapped, size_t wrapped_len,
                           const uint8_t *pub, size_t pub_len,
                           const uint8_t *templ, size_t templ_len,
                           const uint8_t *uid, size_t uid_len,
                           uint8_t *out, size_t out_cap, size_t *out_len)
{
    static const char k_hex[] = "0123456789ABCDEF";
    struct
    {
        uint8_t        field;
        const uint8_t *val;
        size_t         len;
    } fields[PROV_CSR_MAX_RDN];
    uint8_t cn_hex[2U * 16U];   /* up to 16-byte UID -> 32 hex chars */
    size_t count = 0U;
    der_w w;
    size_t tbs_end;
    size_t name_end;
    size_t spki_end;
    size_t bit_end;
    size_t tbs_start;
    size_t tbs_len;
    size_t sig_off;
    size_t siglen = 0U;
    size_t bit_content;
    size_t content;
    uint8_t *bh;
    size_t i;

    if ((out == NULL) || (out_len == NULL) || (pub == NULL) ||
        (wrapped == NULL) || (pub_len == 0U) || (wrapped_len == 0U))
    {
        return -1;
    }

    /* ---- Parse the subject template into an ordered RDN list ------------- */
    if ((templ != NULL) && (templ_len > 0U))
    {
        size_t p = 0U;

        while (p < templ_len)
        {
            uint8_t field;
            uint8_t flen;
            const uint8_t *dummy_oid = NULL;
            size_t dummy_len = 0U;
            uint8_t dummy_tag = 0U;

            if ((p + 2U) > templ_len)
            {
                return -1;
            }
            field = templ[p];
            flen = templ[p + 1U];
            p += 2U;
            if (((size_t)(p) + flen) > templ_len)
            {
                return -1;
            }
            if ((flen == 0U) ||
                (rdn_meta(field, &dummy_oid, &dummy_len, &dummy_tag) != 0))
            {
                return -1;
            }
            if (count >= PROV_CSR_MAX_RDN)
            {
                return -1;
            }
            fields[count].field = field;
            fields[count].val = &templ[p];
            fields[count].len = flen;
            count++;
            p += flen;
        }
    }

    /* Default subject: CN = hex(UID). */
    if (count == 0U)
    {
        size_t n = (uid_len > 16U) ? 16U : uid_len;

        if ((uid == NULL) || (uid_len == 0U))
        {
            return -1;
        }
        for (i = 0U; i < n; i++)
        {
            cn_hex[(2U * i)]      = (uint8_t)k_hex[(uid[i] >> 4) & 0x0FU];
            cn_hex[(2U * i) + 1U] = (uint8_t)k_hex[uid[i] & 0x0FU];
        }
        fields[0].field = PROV_CSR_FIELD_CN;
        fields[0].val = cn_hex;
        fields[0].len = 2U * n;
        count = 1U;
    }

    /* ---- Encode CertificationRequestInfo (TBS) back-to-front ------------ */
    w.buf = out;
    w.pos = out_cap;
    tbs_end = w.pos;

    /* attributes [0] IMPLICIT SET OF Attribute (empty). */
    if (der_put(&w, k_attrs_empty, sizeof(k_attrs_empty)) != 0)
    {
        return -1;
    }

    /* SubjectPublicKeyInfo ::= SEQ { AlgorithmIdentifier, BIT STRING pubkey }. */
    spki_end = w.pos;
    bit_end = w.pos;
    if (der_put(&w, pub, pub_len) != 0)
    {
        return -1;
    }
    if (der_put_byte(&w, 0x00U) != 0)   /* unused-bits octet */
    {
        return -1;
    }
    if (der_put_hdr(&w, 0x03U, bit_end - w.pos) != 0)
    {
        return -1;
    }
    if (der_put(&w, k_mldsa_algid, sizeof(k_mldsa_algid)) != 0)
    {
        return -1;
    }
    if (der_put_hdr(&w, 0x30U, spki_end - w.pos) != 0)
    {
        return -1;
    }

    /* subject Name ::= SEQUENCE OF RelativeDistinguishedName.
     * Emit RDNs in reverse so their final order matches the template order. */
    name_end = w.pos;
    for (i = count; i-- > 0U; )
    {
        if (der_put_rdn(&w, fields[i].field, fields[i].val, fields[i].len) != 0)
        {
            return -1;
        }
    }
    if (der_put_hdr(&w, 0x30U, name_end - w.pos) != 0)
    {
        return -1;
    }

    /* version INTEGER 0. */
    if (der_put(&w, k_version0, sizeof(k_version0)) != 0)
    {
        return -1;
    }

    /* CertificationRequestInfo SEQUENCE header. */
    if (der_put_hdr(&w, 0x30U, tbs_end - w.pos) != 0)
    {
        return -1;
    }

    tbs_start = w.pos;
    tbs_len = out_cap - tbs_start;

    /* ---- Assemble the outer CertificationRequest at the buffer front ----- */
    /* Layout: [outer hdr 4][TBS][sigAlg 13][BIT STRING hdr 5][signature].    */
    sig_off = PROV_CSR_OUTER_HDR_LEN + tbs_len + sizeof(k_mldsa_algid) + 5U;
    if (sig_off >= out_cap)
    {
        return -1;
    }

    /* Move the TBS to sit right after the reserved outer header. Source and
     * destination cannot overlap here (buffer is far larger than 2*TBS). */
    (void)memmove(&out[PROV_CSR_OUTER_HDR_LEN], &out[tbs_start], tbs_len);

    /* signatureAlgorithm (same AlgorithmIdentifier as the SPKI). */
    (void)memcpy(&out[PROV_CSR_OUTER_HDR_LEN + tbs_len],
                 k_mldsa_algid, sizeof(k_mldsa_algid));

    /* Sign the TBS in place; the signature lands after the BIT STRING header,
     * which does not overlap the message range [4, 4+tbs_len). */
    if (prov_crypto_sign_mldsa87(wrapped, wrapped_len,
                                 &out[PROV_CSR_OUTER_HDR_LEN], tbs_len,
                                 &out[sig_off], out_cap - sig_off,
                                 &siglen) != PSA_SUCCESS)
    {
        return -1;
    }

    /* signature BIT STRING header (03 82 HH LL 00), content = 00 || signature. */
    bit_content = 1U + siglen;
    if (bit_content > 0xFFFFU)
    {
        return -1;
    }
    bh = &out[PROV_CSR_OUTER_HDR_LEN + tbs_len + sizeof(k_mldsa_algid)];
    bh[0] = 0x03U;
    bh[1] = 0x82U;
    bh[2] = (uint8_t)((bit_content >> 8) & 0xFFU);
    bh[3] = (uint8_t)(bit_content & 0xFFU);
    bh[4] = 0x00U;

    /* Outer CertificationRequest SEQUENCE header (always long-form, 2 bytes). */
    content = tbs_len + sizeof(k_mldsa_algid) + 5U + siglen;
    if ((content < 0x100U) || (content > 0xFFFFU))
    {
        return -1;
    }
    if ((PROV_CSR_OUTER_HDR_LEN + content) > out_cap)
    {
        return -1;
    }
    out[0] = 0x30U;
    out[1] = 0x82U;
    out[2] = (uint8_t)((content >> 8) & 0xFFU);
    out[3] = (uint8_t)(content & 0xFFU);

    *out_len = PROV_CSR_OUTER_HDR_LEN + content;
    return 0;
}

#endif /* PROV_ENABLE_MLDSA87 */

/* [] END OF FILE */
