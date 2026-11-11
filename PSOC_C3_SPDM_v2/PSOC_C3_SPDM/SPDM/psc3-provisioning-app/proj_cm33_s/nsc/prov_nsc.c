/*****************************************************************************
 * File Name        : prov_nsc.c
 *
 * Description      : Non-Secure-Callable (NSC) veneer functions for the
 *                    provisioning secure service. These cmse_nonsecure_entry
 *                    functions form the single gateway through which the
 *                    non-secure application requests provisioning operations.
 *
 *                    Each veneer:
 *                      1. validates every caller-supplied pointer with
 *                         cmse_check_address_range() — output buffers as
 *                         readable+writable, input buffers as readable — so a
 *                         malicious non-secure caller can never coerce the
 *                         secure world into touching secure memory;
 *                      2. delegates the actual work to the matching
 *                         prov_svc_* function (see core/prov_svc.c).
 *
 *                    The secure world owns all assets; the non-secure world
 *                    never touches them directly and never receives the
 *                    private key.
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

#include <arm_cmse.h>
#include <stddef.h>

#include "prov_nsc.h"
#include "prov_svc.h"

/******************************************************************************
 * Local Functions
 *****************************************************************************/

/**
 * @brief Validate a non-secure OUTPUT buffer (must be readable + writable by
 *        the non-secure caller).
 *
 * @param ptr  Pointer supplied by the non-secure caller.
 * @param len  Region size in bytes.
 * @return Validated pointer on success, NULL on failure.
 */
static void *check_ns_rw(void *ptr, size_t len)
{
    return cmse_check_address_range(ptr, len,
                                    CMSE_NONSECURE | CMSE_MPU_READWRITE);
}

/**
 * @brief Validate a non-secure INPUT buffer (must be readable by the
 *        non-secure caller).
 *
 * @param ptr  Pointer supplied by the non-secure caller.
 * @param len  Region size in bytes.
 * @return Validated const pointer on success, NULL on failure.
 */
static const void *check_ns_ro(const void *ptr, size_t len)
{
    /* cmse_check_address_range takes a non-const pointer; the cast is safe
     * because we only read through the returned pointer. */
    return cmse_check_address_range((void *)ptr, len,
                                    CMSE_NONSECURE | CMSE_MPU_READ);
}

/******************************************************************************
 * NSC Veneer Functions — general (0x5X)
 *****************************************************************************/

prov_status_t __attribute__((cmse_nonsecure_entry))
prov_nsc_get_status(prov_status_info_t *info)
{
    prov_status_info_t *safe = (prov_status_info_t *)
                               check_ns_rw(info, sizeof(*info));
    if (safe == NULL)
    {
        return PROV_ERR_PARAM;
    }
    return prov_svc_get_status(safe);
}

prov_status_t __attribute__((cmse_nonsecure_entry))
prov_nsc_get_info(prov_info_report_t *info)
{
    prov_info_report_t *safe = (prov_info_report_t *)
                               check_ns_rw(info, sizeof(*info));
    if (safe == NULL)
    {
        return PROV_ERR_PARAM;
    }
    return prov_svc_get_info(safe);
}

prov_status_t __attribute__((cmse_nonsecure_entry))
prov_nsc_erase(uint8_t slot_id, uint32_t confirm)
{
    /* No caller pointers — scalar arguments are copied by value. */
    return prov_svc_erase(slot_id, confirm);
}

prov_status_t __attribute__((cmse_nonsecure_entry))
prov_nsc_finalize(uint32_t confirm, uint8_t *provisioned_out)
{
    uint8_t *safe = (uint8_t *)check_ns_rw(provisioned_out,
                                           sizeof(*provisioned_out));
    if (safe == NULL)
    {
        return PROV_ERR_PARAM;
    }
    return prov_svc_finalize(confirm, safe);
}

/******************************************************************************
 * NSC Veneer Functions — key (0x6X)
 *****************************************************************************/

prov_status_t __attribute__((cmse_nonsecure_entry))
prov_nsc_gen_key(uint8_t slot_id, uint16_t alg_id, uint16_t *pub_len_out)
{
    uint16_t *safe = (uint16_t *)check_ns_rw(pub_len_out,
                                             sizeof(*pub_len_out));
    if (safe == NULL)
    {
        return PROV_ERR_PARAM;
    }
    return prov_svc_gen_key(slot_id, alg_id, safe);
}

prov_status_t __attribute__((cmse_nonsecure_entry))
prov_nsc_read_pubkey(prov_read_pubkey_args_t *args)
{
    prov_read_pubkey_args_t *a;
    uint8_t  *safe_buf;
    uint16_t *safe_alg;
    uint16_t *safe_total;
    uint16_t *safe_ret;

    a = (prov_read_pubkey_args_t *)check_ns_rw(args, sizeof(*args));
    if (a == NULL)
    {
        return PROV_ERR_PARAM;
    }

    safe_buf   = (uint8_t  *)check_ns_rw(a->buf, a->len);
    safe_alg   = (uint16_t *)check_ns_rw(a->alg_id_out, sizeof(uint16_t));
    safe_total = (uint16_t *)check_ns_rw(a->total_out, sizeof(uint16_t));
    safe_ret   = (uint16_t *)check_ns_rw(a->ret_out, sizeof(uint16_t));

    if ((a->len == 0u) || (safe_buf == NULL) || (safe_alg == NULL) ||
        (safe_total == NULL) || (safe_ret == NULL))
    {
        return PROV_ERR_PARAM;
    }
    return prov_svc_read_pubkey(a->slot_id, a->offset, safe_buf, a->len,
                                safe_alg, safe_total, safe_ret);
}

/******************************************************************************
 * NSC Veneer Functions — certificate & CSR (0x7X)
 *****************************************************************************/

prov_status_t __attribute__((cmse_nonsecure_entry))
prov_nsc_build_csr(prov_build_csr_args_t *args)
{
    prov_build_csr_args_t *a;
    const uint8_t *safe_templ = NULL;
    uint8_t       *safe_out;
    uint16_t      *safe_total;

    a = (prov_build_csr_args_t *)check_ns_rw(args, sizeof(*args));
    if (a == NULL)
    {
        return PROV_ERR_PARAM;
    }

    /* The subject template is optional; only validate it when supplied. */
    if ((a->templ != NULL) && (a->templ_len > 0u))
    {
        safe_templ = (const uint8_t *)check_ns_ro(a->templ, a->templ_len);
        if (safe_templ == NULL)
        {
            return PROV_ERR_PARAM;
        }
    }

    safe_out   = (uint8_t  *)check_ns_rw(a->out, a->out_cap);
    safe_total = (uint16_t *)check_ns_rw(a->total_out, sizeof(*a->total_out));

    if ((a->out_cap == 0u) || (safe_out == NULL) || (safe_total == NULL))
    {
        return PROV_ERR_PARAM;
    }
    return prov_svc_build_csr(a->slot_id, safe_templ, a->templ_len,
                              safe_out, a->out_cap, safe_total);
}

prov_status_t __attribute__((cmse_nonsecure_entry))
prov_nsc_cert_commit(prov_cert_commit_args_t *args)
{
    prov_cert_commit_args_t *a;
    const uint8_t *safe_cert;
    const uint8_t *safe_sha;
    uint16_t      *safe_len;

    a = (prov_cert_commit_args_t *)check_ns_rw(args, sizeof(*args));
    if (a == NULL)
    {
        return PROV_ERR_PARAM;
    }
    if ((a->total_len == 0u) || (a->total_len > 0xFFFFu))
    {
        return PROV_ERR_LENGTH;
    }

    safe_cert = (const uint8_t *)check_ns_ro(a->cert, a->total_len);
    safe_sha  = (const uint8_t *)check_ns_ro(a->sha256, PROV_SHA256_LEN);
    safe_len  = (uint16_t *)check_ns_rw(a->cert_len_out, sizeof(uint16_t));

    if ((safe_cert == NULL) || (safe_sha == NULL) || (safe_len == NULL))
    {
        return PROV_ERR_PARAM;
    }
    return prov_svc_cert_commit(a->slot_id, a->key_slot, safe_cert,
                                a->total_len, safe_sha, safe_len);
}

prov_status_t __attribute__((cmse_nonsecure_entry))
prov_nsc_read_cert(prov_read_cert_args_t *args)
{
    prov_read_cert_args_t *a;
    uint8_t  *safe_buf;
    uint16_t *safe_total;
    uint16_t *safe_ret;

    a = (prov_read_cert_args_t *)check_ns_rw(args, sizeof(*args));
    if (a == NULL)
    {
        return PROV_ERR_PARAM;
    }

    safe_buf   = (uint8_t  *)check_ns_rw(a->buf, a->len);
    safe_total = (uint16_t *)check_ns_rw(a->total_out, sizeof(uint16_t));
    safe_ret   = (uint16_t *)check_ns_rw(a->ret_out, sizeof(uint16_t));

    if ((a->len == 0u) || (safe_buf == NULL) || (safe_total == NULL) ||
        (safe_ret == NULL))
    {
        return PROV_ERR_PARAM;
    }
    return prov_svc_read_cert(a->slot_id, a->offset, safe_buf, a->len,
                              safe_total, safe_ret);
}

prov_status_t __attribute__((cmse_nonsecure_entry))
prov_nsc_prove_cert(prov_prove_cert_args_t *args)
{
    prov_prove_cert_args_t *a;
    const uint8_t *safe_nonce;
    uint8_t       *safe_hash;
    uint16_t      *safe_alg;
    uint8_t       *safe_out;
    uint16_t      *safe_siglen;

    a = (prov_prove_cert_args_t *)check_ns_rw(args, sizeof(*args));
    if (a == NULL)
    {
        return PROV_ERR_PARAM;
    }

    safe_nonce  = (const uint8_t *)check_ns_ro(a->nonce, PROV_NONCE_LEN);
    safe_hash   = (uint8_t  *)check_ns_rw(a->cert_sha256, PROV_SHA256_LEN);
    safe_alg    = (uint16_t *)check_ns_rw(a->alg_id_out, sizeof(uint16_t));
    safe_out    = (uint8_t  *)check_ns_rw(a->out, a->out_cap);
    safe_siglen = (uint16_t *)check_ns_rw(a->sig_len_out, sizeof(uint16_t));

    if ((a->out_cap == 0U) || (safe_nonce == NULL) || (safe_hash == NULL) ||
        (safe_alg == NULL) || (safe_out == NULL) || (safe_siglen == NULL))
    {
        return PROV_ERR_PARAM;
    }
    return prov_svc_prove_cert(a->slot_id, safe_nonce, safe_hash,
                               safe_alg, safe_out, a->out_cap, safe_siglen);
}

/* [] END OF FILE */
