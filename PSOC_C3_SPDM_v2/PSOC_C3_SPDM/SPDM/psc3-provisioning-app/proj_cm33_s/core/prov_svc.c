/*****************************************************************************
 * File Name        : prov_svc.c
 *
 * Description      : Secure provisioning service implementation. See
 *                    prov_svc.h for the contract and
 *                    docs/design_and_implementation.md for the behaviour.
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

#include "prov_svc.h"
#include "prov_crypto.h"
#include "prov_csr.h"
#include "key_slot.h"
#include "pas.h"

#include "cy_syslib.h"

/******************************************************************************
 * Macros
 *****************************************************************************/

/* Signature-suite selection. A build may enable ECDSA-P256, ML-DSA-87 (PQC,
 * M6 only), or both; M5 enables ECDSA-P256 only. The Makefile drives these
 * (Phase 2). If the build system selects no suite, fall back to the ECDSA-P256
 * baseline so pre-PQC builds behave unchanged. */
#if !defined(PROV_ENABLE_EC256) && !defined(PROV_ENABLE_MLDSA87)
#define PROV_ENABLE_EC256
#endif

/* Scratch buffer large enough to hold a whole key-slot payload
 * (header + pub + wrapped). 256 B comfortably covers the P-256 suite. */
#define PROV_KEY_PAYLOAD_MAX    (256U)

/* Chunk size used when streaming a certificate from flash to the hash. */
#define PROV_HASH_CHUNK         (256U)

/******************************************************************************
 * Module state
 *****************************************************************************/

/******************************************************************************
 * Local helpers
 *****************************************************************************/

/**
 * @brief Translate a PAS status code into a provisioning status code.
 */
static prov_status_t map_pas_status(pas_status_t st)
{
    switch (st)
    {
        case PAS_OK:          return PROV_OK;
        case PAS_ERR_FROZEN:  return PROV_ERR_FROZEN;
        case PAS_ERR_NO_SLOT: return PROV_ERR_NO_SLOT;
        case PAS_ERR_SLOT_TYPE: return PROV_ERR_SLOT_TYPE;
        case PAS_ERR_CRC:     return PROV_ERR_FLASH;
        case PAS_ERR_LENGTH:  return PROV_ERR_LENGTH;
        case PAS_ERR_FLASH:   return PROV_ERR_FLASH;
        case PAS_ERR_PARAM:   return PROV_ERR_PARAM;
        default:              return PROV_ERR_INTERNAL;
    }
}

/**
 * @brief Query a slot and report whether it is a valid slot of @p want type.
 *
 * @param slot  Slot index.
 * @param want  Required role (SLOT_TYPE_KEY or SLOT_TYPE_CERT).
 * @param info  Receives the slot descriptor (may be NULL).
 * @return true if the slot is valid and of the requested type.
 */
static bool slot_is_valid_type(uint8_t slot, slot_type_t want,
                               pas_slot_info_t *info)
{
    pas_slot_info_t local;

    if (pas_slot_query(slot, &local) != PAS_OK)
    {
        return false;
    }
    if (info != NULL)
    {
        *info = local;
    }
    return (local.valid && (local.type == want));
}

/**
 * @brief Load a slot's entire payload into a caller buffer.
 *
 * @param slot     Slot index.
 * @param buf      Destination buffer.
 * @param cap      Capacity of @p buf.
 * @param out_len  Receives the payload length read.
 * @return PROV_OK or a mapped PAS error.
 */
static prov_status_t load_payload(uint8_t slot, uint8_t *buf, size_t cap,
                                  size_t *out_len)
{
    pas_slot_info_t info;
    uint32_t got = 0U;
    pas_status_t st;

    if (pas_slot_query(slot, &info) != PAS_OK)
    {
        return PROV_ERR_NO_SLOT;
    }
    if (!info.valid)
    {
        return PROV_ERR_SLOT_TYPE;
    }
    if (info.payload_len > cap)
    {
        return PROV_ERR_LENGTH;
    }

    st = pas_slot_read(slot, 0U, buf, info.payload_len, &got);
    if (st != PAS_OK)
    {
        return map_pas_status(st);
    }
    *out_len = (size_t)got;
    return PROV_OK;
}

/******************************************************************************
 * Service API
 *****************************************************************************/

prov_status_t prov_svc_init(void)
{
    return (prov_crypto_init() == PSA_SUCCESS) ? PROV_OK : PROV_ERR_CRYPTO;
}

prov_status_t prov_svc_get_status(prov_status_info_t *info)
{
    uint32_t num_slots;
    uint8_t i;

    if (info == NULL)
    {
        return PROV_ERR_PARAM;
    }

    info->provisioned = pas_is_frozen() ? 1u : 0u;

    num_slots = pas_slot_count();
    info->num_slots = (num_slots <= PROV_NUM_SLOTS)
                    ? (uint8_t)num_slots : (uint8_t)PROV_NUM_SLOTS;
    info->pas_version = PAS_VERSION;

    for (i = 0u; i < info->num_slots; i++)
    {
        pas_slot_info_t si;
        if ((pas_slot_query(i, &si) == PAS_OK) && si.valid)
        {
            info->slot_type[i] = (uint8_t)si.type;
        }
        else
        {
            info->slot_type[i] = (uint8_t)PROV_SLOT_FREE;
        }
    }

    return PROV_OK;
}

prov_status_t prov_svc_get_info(prov_info_report_t *info)
{
    uint64_t uid;
    uint32_t num_slots;
    uint8_t i;

    if (info == NULL)
    {
        return PROV_ERR_PARAM;
    }

    (void)memset(info, 0, sizeof(*info));

    /* Device unique id (little-endian into the UID byte array). */
    uid = Cy_SysLib_GetUniqueId();
    for (i = 0u; i < PROV_UID_LEN; i++)
    {
        info->uid[i] = (uint8_t)((uid >> (8u * i)) & 0xFFu);
    }

    info->caps = 0u;

    num_slots = pas_slot_count();
    info->num_slots = (num_slots <= PROV_NUM_SLOTS)
                    ? (uint8_t)num_slots : (uint8_t)PROV_NUM_SLOTS;

    for (i = 0u; i < info->num_slots; i++)
    {
        uint32_t total = pas_slot_size(i);
        uint32_t usable = (total > sizeof(slot_hdr_t))
                        ? (total - (uint32_t)sizeof(slot_hdr_t)) : 0u;
        info->slot_size[i] = (usable > 0xFFFFu) ? 0xFFFFu : (uint16_t)usable;
    }

    /* Advertise every suite compiled into this build. A build may enable any
     * mix of ECDSA-P256 / P-384 / P-521 (all devices) and ML-DSA-87 (PQC,
     * M6/M8 only). The PROV_ENABLE_* switches are set by the Makefile. */
    {
        uint8_t n = 0u;

#if defined(PROV_ENABLE_EC256)
        info->algs[n].alg_id  = PROV_ALG_ECDSA_P256;
        info->algs[n].pub_len = PROV_PUB_LEN_P256;
        info->algs[n].sig_len = PROV_SIG_LEN_P256;
        n++;
#endif

#if defined(PROV_ENABLE_EC384)
        info->algs[n].alg_id  = PROV_ALG_ECDSA_P384;
        info->algs[n].pub_len = PROV_PUB_LEN_P384;
        info->algs[n].sig_len = PROV_SIG_LEN_P384;
        n++;
#endif

#if defined(PROV_ENABLE_EC521)
        info->algs[n].alg_id  = PROV_ALG_ECDSA_P521;
        info->algs[n].pub_len = PROV_PUB_LEN_P521;
        info->algs[n].sig_len = PROV_SIG_LEN_P521;
        n++;
#endif

#if defined(PROV_ENABLE_MLDSA87)
        info->algs[n].alg_id  = PROV_ALG_MLDSA_87;
        info->algs[n].pub_len = PROV_PUB_LEN_MLDSA87;
        info->algs[n].sig_len = PROV_SIG_LEN_MLDSA87;
        n++;
#endif

        info->n_algs = n;
    }

    info->max_chunk = PROV_MAX_CHUNK;

    return PROV_OK;
}

prov_status_t prov_svc_erase(uint8_t slot_id, uint32_t confirm)
{
    if (confirm != PROV_CONFIRM_ERASE)
    {
        return PROV_ERR_CONFIRM;
    }
    if (pas_is_frozen())
    {
        return PROV_ERR_FROZEN;
    }

    if (slot_id == PROV_SLOT_ALL)
    {
        uint32_t n = pas_slot_count();
        uint8_t i;
        for (i = 0u; i < n; i++)
        {
            pas_status_t st = pas_slot_erase(i);
            if (st != PAS_OK)
            {
                return map_pas_status(st);
            }
        }
        return PROV_OK;
    }

    if ((uint32_t)slot_id >= pas_slot_count())
    {
        return PROV_ERR_NO_SLOT;
    }
    return map_pas_status(pas_slot_erase(slot_id));
}

prov_status_t prov_svc_finalize(uint32_t confirm, uint8_t *provisioned_out)
{
    uint32_t n;
    uint8_t i;
    bool has_key = false;
    pas_status_t st;

    if (confirm != PROV_CONFIRM_FINALIZE)
    {
        return PROV_ERR_CONFIRM;
    }
    if (pas_is_frozen())
    {
        return PROV_ERR_FROZEN;
    }

    /* Structural pre-freeze check (§6.10): at least one valid key slot, and
     * every valid cert slot must bind to a valid key slot. */
    n = pas_slot_count();
    for (i = 0u; i < n; i++)
    {
        pas_slot_info_t si;
        if ((pas_slot_query(i, &si) != PAS_OK) || !si.valid)
        {
            continue;
        }
        if (si.type == SLOT_TYPE_KEY)
        {
            has_key = true;
        }
        else if (si.type == SLOT_TYPE_CERT)
        {
            if ((si.assoc_slot == SLOT_ASSOC_NONE) ||
                !slot_is_valid_type(si.assoc_slot, SLOT_TYPE_KEY, NULL))
            {
                return PROV_ERR_NOT_READY;
            }
        }
        else
        {
            /* free slot — ignore */
        }
    }
    if (!has_key)
    {
        return PROV_ERR_NOT_READY;
    }

    st = pas_freeze();
    if (st != PAS_OK)
    {
        return map_pas_status(st);
    }

    if (provisioned_out != NULL)
    {
        *provisioned_out = 1u;
    }
    return PROV_OK;
}

#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)
/* Curve-parameterised key generation for P-256 / P-384 / P-521. The suite id
 * selects the NIST curve and its raw public-key length (used both to validate
 * the export and to report back to the host); scratch is sized to the
 * worst-case EC key material. */
static prov_status_t gen_key_ec(uint8_t slot_id, uint16_t alg_id,
                                uint16_t *pub_len_out)
{
    prov_ec_curve_t curve;
    uint16_t exp_pub_len;
    uint8_t pub[PROV_CRYPTO_EC_PUB_MAX];
    uint8_t wrapped[PROV_CRYPTO_EC_WRAP_MAX];
    uint8_t payload[PROV_KEY_PAYLOAD_MAX];
    size_t pub_len = 0U;
    size_t wrapped_len = 0U;
    size_t payload_len = 0U;
    pas_status_t st;
    prov_status_t rc = PROV_ERR_INTERNAL;

    switch (alg_id)
    {
#if defined(PROV_ENABLE_EC256)
    case PROV_ALG_ECDSA_P256:
        curve = PROV_EC_CURVE_P256;
        exp_pub_len = PROV_PUB_LEN_P256;
        break;
#endif
#if defined(PROV_ENABLE_EC384)
    case PROV_ALG_ECDSA_P384:
        curve = PROV_EC_CURVE_P384;
        exp_pub_len = PROV_PUB_LEN_P384;
        break;
#endif
#if defined(PROV_ENABLE_EC521)
    case PROV_ALG_ECDSA_P521:
        curve = PROV_EC_CURVE_P521;
        exp_pub_len = PROV_PUB_LEN_P521;
        break;
#endif
    default:
        return PROV_ERR_ALG_UNSUPPORTED;
    }

    if (prov_crypto_keygen_ec(curve, pub, sizeof(pub), &pub_len,
                              wrapped, sizeof(wrapped),
                              &wrapped_len) != PSA_SUCCESS)
    {
        rc = PROV_ERR_CRYPTO;
        goto done;
    }
    if (pub_len != (size_t)exp_pub_len)
    {
        rc = PROV_ERR_CRYPTO;
        goto done;
    }

    if (key_slot_pack(pub, (uint16_t)pub_len,
                      wrapped, (uint16_t)wrapped_len,
                      payload, sizeof(payload), &payload_len) != 0)
    {
        rc = PROV_ERR_INTERNAL;
        goto done;
    }

    st = pas_slot_write(slot_id, SLOT_MAGIC_KEY, alg_id,
                        SLOT_ASSOC_NONE, payload, (uint32_t)payload_len);
    if (st != PAS_OK)
    {
        rc = map_pas_status(st);
        goto done;
    }

    if (pub_len_out != NULL)
    {
        *pub_len_out = exp_pub_len;
    }
    rc = PROV_OK;

done:
    (void)memset(wrapped, 0, sizeof(wrapped));
    (void)memset(payload, 0, sizeof(payload));
    return rc;
}
#endif /* PROV_ENABLE_EC256 || PROV_ENABLE_EC384 || PROV_ENABLE_EC521 */

#if defined(PROV_ENABLE_MLDSA87)
/* ML-DSA-87 key material is far too large for the secure stack. A single
 * static payload buffer (header || public key || wrapped private key) is
 * built in place: keygen writes the public and wrapped keys directly at their
 * final slot offsets, so neither is ever duplicated. The provisioning service
 * serialises requests, so one shared buffer is safe; it is zeroised after use. */
static uint8_t s_mldsa_payload[KEY_SLOT_HDR_LEN +
                               PROV_CRYPTO_MLDSA87_PUB_LEN +
                               PROV_CRYPTO_MLDSA87_WRAP_MAX];

static prov_status_t gen_key_mldsa87(uint8_t slot_id, uint16_t *pub_len_out)
{
    uint8_t *const pub = &s_mldsa_payload[KEY_SLOT_HDR_LEN];
    uint8_t *const wrapped =
        &s_mldsa_payload[KEY_SLOT_HDR_LEN + PROV_CRYPTO_MLDSA87_PUB_LEN];
    const size_t wrapped_cap = sizeof(s_mldsa_payload) -
                               KEY_SLOT_HDR_LEN - PROV_CRYPTO_MLDSA87_PUB_LEN;
    size_t wrapped_len = 0U;
    size_t payload_len = 0U;
    pas_status_t st;
    prov_status_t rc = PROV_ERR_INTERNAL;

    if (prov_crypto_keygen_mldsa87(pub, wrapped, wrapped_cap,
                                   &wrapped_len) != PSA_SUCCESS)
    {
        rc = PROV_ERR_CRYPTO;
        goto done;
    }

    /* In-place header, matching key_slot_pack(): pub_len || wrapped_len (LE16). */
    s_mldsa_payload[0] = (uint8_t)(PROV_PUB_LEN_MLDSA87 & 0xFFU);
    s_mldsa_payload[1] = (uint8_t)((PROV_PUB_LEN_MLDSA87 >> 8) & 0xFFU);
    s_mldsa_payload[2] = (uint8_t)(wrapped_len & 0xFFU);
    s_mldsa_payload[3] = (uint8_t)((wrapped_len >> 8) & 0xFFU);
    payload_len = (size_t)KEY_SLOT_HDR_LEN + PROV_CRYPTO_MLDSA87_PUB_LEN +
                  wrapped_len;

    st = pas_slot_write(slot_id, SLOT_MAGIC_KEY, PROV_ALG_MLDSA_87,
                        SLOT_ASSOC_NONE, s_mldsa_payload,
                        (uint32_t)payload_len);
    if (st != PAS_OK)
    {
        rc = map_pas_status(st);
        goto done;
    }

    if (pub_len_out != NULL)
    {
        *pub_len_out = PROV_PUB_LEN_MLDSA87;
    }
    rc = PROV_OK;

done:
    (void)memset(s_mldsa_payload, 0, sizeof(s_mldsa_payload));
    return rc;
}
#endif /* PROV_ENABLE_MLDSA87 */

prov_status_t prov_svc_gen_key(uint8_t slot_id, uint16_t alg_id,
                               uint16_t *pub_len_out)
{
    pas_slot_info_t si;

    if (pas_is_frozen())
    {
        return PROV_ERR_FROZEN;
    }
    if ((uint32_t)slot_id >= pas_slot_count())
    {
        return PROV_ERR_NO_SLOT;
    }
    /* Refuse to clobber a certificate slot (§6.6 step 2). */
    if ((pas_slot_query(slot_id, &si) == PAS_OK) && si.valid &&
        (si.type == SLOT_TYPE_CERT))
    {
        return PROV_ERR_SLOT_TYPE;
    }

    switch (alg_id)
    {
#if defined(PROV_ENABLE_EC256)
    case PROV_ALG_ECDSA_P256:
#endif
#if defined(PROV_ENABLE_EC384)
    case PROV_ALG_ECDSA_P384:
#endif
#if defined(PROV_ENABLE_EC521)
    case PROV_ALG_ECDSA_P521:
#endif
#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)
        /* All EC suites share one curve-parameterised path; gen_key_ec()
         * maps the alg_id to its NIST curve. */
        return gen_key_ec(slot_id, alg_id, pub_len_out);
#endif
#if defined(PROV_ENABLE_MLDSA87)
    case PROV_ALG_MLDSA_87:
        return gen_key_mldsa87(slot_id, pub_len_out);
#endif
    default:
        return PROV_ERR_ALG_UNSUPPORTED;
    }
}

prov_status_t prov_svc_read_pubkey(uint8_t slot_id, uint16_t offset,
                                   uint8_t *buf, uint16_t len,
                                   uint16_t *alg_id_out, uint16_t *total_out,
                                   uint16_t *ret_out)
{
    uint8_t hdr[KEY_SLOT_HDR_LEN];
    uint16_t pub_len;
    uint32_t got = 0U;
    pas_slot_info_t si;
    uint16_t avail;
    uint16_t copy;
    pas_status_t st;

    if ((buf == NULL) || (ret_out == NULL))
    {
        return PROV_ERR_PARAM;
    }
    if ((uint32_t)slot_id >= pas_slot_count())
    {
        return PROV_ERR_NO_SLOT;
    }
    if (!slot_is_valid_type(slot_id, SLOT_TYPE_KEY, &si))
    {
        return si.valid ? PROV_ERR_SLOT_TYPE : PROV_ERR_NO_KEY;
    }

    /* Read only the 4-byte slot header for the public-key length, then stream
     * the public key straight from flash. Avoids buffering the whole payload
     * (an ML-DSA-87 key slot is ~7.5 KB) and never touches the wrapped key. */
    st = pas_slot_read(slot_id, 0U, hdr, sizeof(hdr), &got);
    if (st != PAS_OK)
    {
        return map_pas_status(st);
    }
    if (got < KEY_SLOT_HDR_LEN)
    {
        return PROV_ERR_INTERNAL;
    }
    pub_len = (uint16_t)((uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8));

    if (alg_id_out != NULL)
    {
        *alg_id_out = si.alg_id;
    }
    if (total_out != NULL)
    {
        *total_out = pub_len;
    }

    if (offset >= pub_len)
    {
        *ret_out = 0u;
        return PROV_OK;
    }
    avail = (uint16_t)(pub_len - offset);
    copy = (len < avail) ? len : avail;

    st = pas_slot_read(slot_id, (uint32_t)KEY_SLOT_HDR_LEN + offset,
                       buf, copy, &got);
    if (st != PAS_OK)
    {
        return map_pas_status(st);
    }
    *ret_out = (uint16_t)got;
    return PROV_OK;
}

#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)
/* Curve-parameterised CSR builder for P-256 / P-384 / P-521. Routes through
 * prov_csr_build_ec(), which selects the curve-paired message digest
 * (SHA-256 / SHA-384 / SHA-512). */
static prov_status_t build_csr_ec(uint8_t slot_id, prov_ec_curve_t curve,
                                  const uint8_t *templ, uint16_t templ_len,
                                  uint8_t *out, uint16_t out_cap,
                                  uint16_t *total_out)
{
    uint8_t payload[PROV_KEY_PAYLOAD_MAX];
    size_t payload_len = 0U;
    const uint8_t *wrapped = NULL;
    uint16_t wrapped_len = 0U;
    uint8_t uid_bytes[PROV_UID_LEN];
    uint64_t uid;
    uint8_t i;
    prov_status_t rc;

    rc = load_payload(slot_id, payload, sizeof(payload), &payload_len);
    if (rc != PROV_OK)
    {
        return rc;
    }
    if (key_slot_get_wrapped(payload, payload_len, &wrapped, &wrapped_len) != 0)
    {
        return PROV_ERR_INTERNAL;
    }

    uid = Cy_SysLib_GetUniqueId();
    for (i = 0u; i < PROV_UID_LEN; i++)
    {
        uid_bytes[i] = (uint8_t)((uid >> (8u * i)) & 0xFFu);
    }

    {
        size_t csr_len = 0U;
        int ret = prov_csr_build_ec(curve, wrapped, wrapped_len,
                                    templ, templ_len,
                                    uid_bytes, sizeof(uid_bytes),
                                    out, out_cap, &csr_len);
        (void)memset(payload, 0, sizeof(payload));
        if (ret != 0)
        {
            return PROV_ERR_CRYPTO;
        }
        if (csr_len > 0xFFFFu)
        {
            return PROV_ERR_LENGTH;
        }
        if (total_out != NULL)
        {
            *total_out = (uint16_t)csr_len;
        }
    }
    return PROV_OK;
}
#endif /* PROV_ENABLE_EC256 || PROV_ENABLE_EC384 || PROV_ENABLE_EC521 */

#if defined(PROV_ENABLE_MLDSA87)
/* ML-DSA-87 key material (2592 B public + ~4924 B wrapped private) far exceeds
 * the secure stack, so the whole slot payload is streamed into the shared
 * static s_mldsa_payload (also used by gen_key; the service serialises
 * requests). The public key and wrapped private key are then referenced in
 * place while the CSR is assembled directly into the caller-supplied
 * non-secure buffer. */
static prov_status_t build_csr_mldsa87(uint8_t slot_id,
                                       const uint8_t *templ, uint16_t templ_len,
                                       uint8_t *out, uint16_t out_cap,
                                       uint16_t *total_out)
{
    const uint8_t *pub = NULL;
    const uint8_t *wrapped = NULL;
    uint16_t pub_len = 0U;
    uint16_t wrapped_len = 0U;
    uint8_t uid_bytes[PROV_UID_LEN];
    uint64_t uid;
    size_t payload_len = 0U;
    size_t csr_len = 0U;
    prov_status_t rc;
    int ret;
    uint8_t i;

    rc = load_payload(slot_id, s_mldsa_payload, sizeof(s_mldsa_payload),
                      &payload_len);
    if (rc != PROV_OK)
    {
        return rc;
    }
    if ((key_slot_get_pub(s_mldsa_payload, payload_len, &pub, &pub_len) != 0) ||
        (key_slot_get_wrapped(s_mldsa_payload, payload_len,
                              &wrapped, &wrapped_len) != 0))
    {
        (void)memset(s_mldsa_payload, 0, sizeof(s_mldsa_payload));
        return PROV_ERR_INTERNAL;
    }

    uid = Cy_SysLib_GetUniqueId();
    for (i = 0u; i < PROV_UID_LEN; i++)
    {
        uid_bytes[i] = (uint8_t)((uid >> (8u * i)) & 0xFFu);
    }

    ret = prov_csr_build_mldsa87(wrapped, wrapped_len, pub, pub_len,
                                 templ, templ_len,
                                 uid_bytes, sizeof(uid_bytes),
                                 out, out_cap, &csr_len);
    (void)memset(s_mldsa_payload, 0, sizeof(s_mldsa_payload));
    if (ret != 0)
    {
        return PROV_ERR_CRYPTO;
    }
    if (csr_len > 0xFFFFu)
    {
        return PROV_ERR_LENGTH;
    }
    if (total_out != NULL)
    {
        *total_out = (uint16_t)csr_len;
    }
    return PROV_OK;
}
#endif /* PROV_ENABLE_MLDSA87 */

prov_status_t prov_svc_build_csr(uint8_t slot_id,
                                 const uint8_t *templ, uint16_t templ_len,
                                 uint8_t *out, uint16_t out_cap,
                                 uint16_t *total_out)
{
    pas_slot_info_t si;

    if ((out == NULL) || (out_cap == 0u) || (total_out == NULL))
    {
        return PROV_ERR_PARAM;
    }
    *total_out = 0u;

    if ((uint32_t)slot_id >= pas_slot_count())
    {
        return PROV_ERR_NO_SLOT;
    }
    if (!slot_is_valid_type(slot_id, SLOT_TYPE_KEY, &si))
    {
        return si.valid ? PROV_ERR_SLOT_TYPE : PROV_ERR_NO_KEY;
    }

    switch (si.alg_id)
    {
#if defined(PROV_ENABLE_EC256)
    case PROV_ALG_ECDSA_P256:
        return build_csr_ec(slot_id, PROV_EC_CURVE_P256, templ, templ_len,
                            out, out_cap, total_out);
#endif
#if defined(PROV_ENABLE_EC384)
    case PROV_ALG_ECDSA_P384:
        return build_csr_ec(slot_id, PROV_EC_CURVE_P384, templ, templ_len,
                            out, out_cap, total_out);
#endif
#if defined(PROV_ENABLE_EC521)
    case PROV_ALG_ECDSA_P521:
        return build_csr_ec(slot_id, PROV_EC_CURVE_P521, templ, templ_len,
                            out, out_cap, total_out);
#endif
#if defined(PROV_ENABLE_MLDSA87)
    case PROV_ALG_MLDSA_87:
        return build_csr_mldsa87(slot_id, templ, templ_len, out, out_cap, total_out);
#endif
    default:
        return PROV_ERR_ALG_UNSUPPORTED;
    }
}

prov_status_t prov_svc_cert_commit(uint8_t slot_id, uint8_t key_slot,
                                   const uint8_t *cert, uint32_t total_len,
                                   const uint8_t sha256[PROV_SHA256_LEN],
                                   uint16_t *cert_len_out)
{
    uint8_t digest[PROV_SHA256_LEN];
    pas_slot_info_t si;
    pas_status_t st;

    if ((cert == NULL) || (sha256 == NULL) || (total_len == 0u))
    {
        return PROV_ERR_PARAM;
    }
    if (pas_is_frozen())
    {
        return PROV_ERR_FROZEN;
    }
    if ((uint32_t)slot_id >= pas_slot_count())
    {
        return PROV_ERR_NO_SLOT;
    }
    /* Refuse to overwrite a key slot with a certificate (§6.8 step 1). */
    if ((pas_slot_query(slot_id, &si) == PAS_OK) && si.valid &&
        (si.type == SLOT_TYPE_KEY))
    {
        return PROV_ERR_SLOT_TYPE;
    }
    /* The bound key slot must already hold a valid key (§6.8 step 2). */
    if ((uint32_t)key_slot >= pas_slot_count())
    {
        return PROV_ERR_NO_KEY;
    }
    if (!slot_is_valid_type(key_slot, SLOT_TYPE_KEY, NULL))
    {
        return PROV_ERR_NO_KEY;
    }

    /* Transfer-integrity check only — no X.509 parsing (§6.8 step 3). */
    if (prov_crypto_sha256(cert, (size_t)total_len, digest) != PSA_SUCCESS)
    {
        return PROV_ERR_CRYPTO;
    }
    if (memcmp(digest, sha256, PROV_SHA256_LEN) != 0)
    {
        return PROV_ERR_HASH_MISMATCH;
    }

    st = pas_slot_write(slot_id, SLOT_MAGIC_CERT, 0u, key_slot,
                        cert, total_len);
    if (st != PAS_OK)
    {
        return map_pas_status(st);
    }

    if (cert_len_out != NULL)
    {
        *cert_len_out = (total_len > 0xFFFFu) ? 0xFFFFu : (uint16_t)total_len;
    }
    return PROV_OK;
}

prov_status_t prov_svc_read_cert(uint8_t slot_id, uint16_t offset,
                                 uint8_t *buf, uint16_t len,
                                 uint16_t *total_out, uint16_t *ret_out)
{
    pas_slot_info_t si;
    uint32_t total;
    uint32_t got = 0U;
    uint16_t copy;
    pas_status_t st;

    if ((buf == NULL) || (ret_out == NULL))
    {
        return PROV_ERR_PARAM;
    }
    if ((uint32_t)slot_id >= pas_slot_count())
    {
        return PROV_ERR_NO_SLOT;
    }
    if (!slot_is_valid_type(slot_id, SLOT_TYPE_CERT, &si))
    {
        return si.valid ? PROV_ERR_SLOT_TYPE : PROV_ERR_NO_CERT;
    }

    total = si.payload_len;
    if (total_out != NULL)
    {
        *total_out = (total > 0xFFFFu) ? 0xFFFFu : (uint16_t)total;
    }

    if ((uint32_t)offset >= total)
    {
        *ret_out = 0u;
        return PROV_OK;
    }

    copy = len;
    st = pas_slot_read(slot_id, offset, buf, copy, &got);
    if (st != PAS_OK)
    {
        return map_pas_status(st);
    }
    *ret_out = (got > 0xFFFFu) ? 0xFFFFu : (uint16_t)got;
    return PROV_OK;
}

#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)
/* Shared P-256 / P-384 / P-521 signing arm for prove_cert: loads the bound key
 * slot, references the wrapped private key in place, and writes a raw r||s
 * signature of exactly exp_sig_len bytes into the caller's non-secure buffer. */
static prov_status_t prove_sign_ec(prov_ec_curve_t curve, uint8_t key_slot,
                                   uint16_t exp_sig_len,
                                   const uint8_t *message, size_t message_len,
                                   uint8_t *out, uint16_t out_cap,
                                   uint16_t *sig_len_out)
{
    uint8_t payload[PROV_KEY_PAYLOAD_MAX];
    const uint8_t *wrapped = NULL;
    uint16_t wrapped_len = 0U;
    size_t payload_len = 0U;
    size_t sig_len = 0U;
    prov_status_t rc;

    if (out_cap < exp_sig_len)
    {
        return PROV_ERR_LENGTH;
    }
    rc = load_payload(key_slot, payload, sizeof(payload), &payload_len);
    if (rc != PROV_OK)
    {
        return rc;
    }
    if (key_slot_get_wrapped(payload, payload_len, &wrapped, &wrapped_len) != 0)
    {
        return PROV_ERR_INTERNAL;
    }
    if (prov_crypto_sign_ec(curve, wrapped, wrapped_len,
                            message, message_len,
                            out, out_cap, &sig_len) != PSA_SUCCESS)
    {
        (void)memset(payload, 0, sizeof(payload));
        return PROV_ERR_CRYPTO;
    }
    (void)memset(payload, 0, sizeof(payload));
    if (sig_len != (size_t)exp_sig_len)
    {
        return PROV_ERR_CRYPTO;
    }
    *sig_len_out = exp_sig_len;
    return PROV_OK;
}
#endif /* PROV_ENABLE_EC256 || PROV_ENABLE_EC384 || PROV_ENABLE_EC521 */

prov_status_t prov_svc_prove_cert(uint8_t slot_id,
                                  const uint8_t nonce[PROV_NONCE_LEN],
                                  uint8_t cert_sha256[PROV_SHA256_LEN],
                                  uint16_t *alg_id_out,
                                  uint8_t *out, uint16_t out_cap,
                                  uint16_t *sig_len_out)
{
    uint8_t chunk[PROV_HASH_CHUNK];
    uint8_t digest[PROV_SHA256_LEN];
    uint8_t message[PROV_NONCE_LEN + PROV_SHA256_LEN];
    psa_hash_operation_t hash_op;
    pas_slot_info_t cert_si;
    pas_slot_info_t key_si;
    uint32_t offset;

    if ((nonce == NULL) || (cert_sha256 == NULL) || (out == NULL) ||
        (sig_len_out == NULL))
    {
        return PROV_ERR_PARAM;
    }
    if ((uint32_t)slot_id >= pas_slot_count())
    {
        return PROV_ERR_NO_SLOT;
    }
    if (!slot_is_valid_type(slot_id, SLOT_TYPE_CERT, &cert_si))
    {
        return cert_si.valid ? PROV_ERR_SLOT_TYPE : PROV_ERR_NO_CERT;
    }
    /* Bound key slot must be a valid key (§6.9 step 1). */
    if ((cert_si.assoc_slot == SLOT_ASSOC_NONE) ||
        ((uint32_t)cert_si.assoc_slot >= pas_slot_count()) ||
        !slot_is_valid_type(cert_si.assoc_slot, SLOT_TYPE_KEY, &key_si))
    {
        return PROV_ERR_NO_KEY;
    }

    /* H = SHA256(cert payload), streamed from flash (§6.9 step 2). */
    if (prov_crypto_sha256_start(&hash_op) != PSA_SUCCESS)
    {
        return PROV_ERR_CRYPTO;
    }
    offset = 0u;
    while (offset < cert_si.payload_len)
    {
        uint32_t want = cert_si.payload_len - offset;
        uint32_t got = 0u;
        if (want > PROV_HASH_CHUNK)
        {
            want = PROV_HASH_CHUNK;
        }
        if (pas_slot_read(slot_id, offset, chunk, want, &got) != PAS_OK)
        {
            (void)psa_hash_abort(&hash_op);
            return PROV_ERR_FLASH;
        }
        if (prov_crypto_sha256_update(&hash_op, chunk, got) != PSA_SUCCESS)
        {
            return PROV_ERR_CRYPTO;
        }
        offset += got;
        if (got == 0u)
        {
            break;  /* defensive: avoid infinite loop on short read */
        }
    }
    if (prov_crypto_sha256_finish(&hash_op, digest) != PSA_SUCCESS)
    {
        return PROV_ERR_CRYPTO;
    }

    /* S = Sign(privkey, nonce || H) (§6.9 step 3), dispatched on the bound
     * key's suite; the (possibly large) signature is written into the caller's
     * non-secure buffer, which the router streams back in chunks. */
    (void)memcpy(message, nonce, PROV_NONCE_LEN);
    (void)memcpy(&message[PROV_NONCE_LEN], digest, PROV_SHA256_LEN);

    switch (key_si.alg_id)
    {
#if defined(PROV_ENABLE_EC256)
    case PROV_ALG_ECDSA_P256:
    {
        prov_status_t rc = prove_sign_ec(PROV_EC_CURVE_P256, cert_si.assoc_slot,
                                         PROV_SIG_LEN_P256,
                                         message, sizeof(message),
                                         out, out_cap, sig_len_out);
        if (rc != PROV_OK)
        {
            return rc;
        }
        break;
    }
#endif /* PROV_ENABLE_EC256 */
#if defined(PROV_ENABLE_EC384)
    case PROV_ALG_ECDSA_P384:
    {
        prov_status_t rc = prove_sign_ec(PROV_EC_CURVE_P384, cert_si.assoc_slot,
                                         PROV_SIG_LEN_P384,
                                         message, sizeof(message),
                                         out, out_cap, sig_len_out);
        if (rc != PROV_OK)
        {
            return rc;
        }
        break;
    }
#endif /* PROV_ENABLE_EC384 */
#if defined(PROV_ENABLE_EC521)
    case PROV_ALG_ECDSA_P521:
    {
        prov_status_t rc = prove_sign_ec(PROV_EC_CURVE_P521, cert_si.assoc_slot,
                                         PROV_SIG_LEN_P521,
                                         message, sizeof(message),
                                         out, out_cap, sig_len_out);
        if (rc != PROV_OK)
        {
            return rc;
        }
        break;
    }
#endif /* PROV_ENABLE_EC521 */
#if defined(PROV_ENABLE_MLDSA87)
    case PROV_ALG_MLDSA_87:
    {
        const uint8_t *wrapped = NULL;
        uint16_t wrapped_len = 0U;
        uint32_t got = 0U;
        size_t sig_len = 0U;
        pas_status_t st;

        if (out_cap < PROV_CRYPTO_MLDSA87_SIG_LEN)
        {
            return PROV_ERR_LENGTH;
        }
        /* The bound key slot payload (~7.5 KB) is far larger than the secure
         * stack, so stream it into the shared static and reference the wrapped
         * private key in place. */
        if ((size_t)key_si.payload_len > sizeof(s_mldsa_payload))
        {
            return PROV_ERR_LENGTH;
        }
        st = pas_slot_read(cert_si.assoc_slot, 0U, s_mldsa_payload,
                           key_si.payload_len, &got);
        if (st != PAS_OK)
        {
            return map_pas_status(st);
        }
        if (key_slot_get_wrapped(s_mldsa_payload, got,
                                 &wrapped, &wrapped_len) != 0)
        {
            (void)memset(s_mldsa_payload, 0, sizeof(s_mldsa_payload));
            return PROV_ERR_INTERNAL;
        }
        if (prov_crypto_sign_mldsa87(wrapped, wrapped_len,
                                     message, sizeof(message),
                                     out, out_cap, &sig_len) != PSA_SUCCESS)
        {
            (void)memset(s_mldsa_payload, 0, sizeof(s_mldsa_payload));
            return PROV_ERR_CRYPTO;
        }
        (void)memset(s_mldsa_payload, 0, sizeof(s_mldsa_payload));
        if (sig_len > 0xFFFFu)
        {
            return PROV_ERR_LENGTH;
        }
        *sig_len_out = (uint16_t)sig_len;
        break;
    }
#endif /* PROV_ENABLE_MLDSA87 */
    default:
        return PROV_ERR_ALG_UNSUPPORTED;
    }

    (void)memcpy(cert_sha256, digest, PROV_SHA256_LEN);
    if (alg_id_out != NULL)
    {
        *alg_id_out = key_si.alg_id;
    }
    return PROV_OK;
}

/* [] END OF FILE */
