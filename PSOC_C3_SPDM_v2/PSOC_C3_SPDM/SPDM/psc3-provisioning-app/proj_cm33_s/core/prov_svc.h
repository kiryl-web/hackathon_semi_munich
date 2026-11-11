/*****************************************************************************
 * File Name        : prov_svc.h
 *
 * Description      : Secure provisioning service — the orchestration layer that
 *                    sits behind the NSC veneers. Each prov_svc_* function
 *                    implements one provisioning command by combining the PAS
 *                    (storage), the crypto wrapper and the CSR builder.
 *
 *                    Pointer arguments passed to these functions are assumed to
 *                    have ALREADY been validated by the NSC layer
 *                    (cmse_check_address_range). This layer contains no CMSE
 *                    code and could equally be unit-tested off-target.
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

#ifndef PROV_SVC_H
#define PROV_SVC_H

#include <stdint.h>

#include "prov_nsc.h"   /* shared types: prov_status_t, report structs, macros */

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Initialise the provisioning service (PSA crypto + internal state).
 *
 * Call once from secure start-up, after pas_init() and before launching the
 * non-secure world.
 *
 * @return PROV_OK on success, PROV_ERR_CRYPTO if PSA init fails.
 */
prov_status_t prov_svc_init(void);

/* The following mirror the NSC API (prov_nsc.h) but take pre-validated
 * pointers. See prov_nsc.h for parameter and return-code documentation. */

prov_status_t prov_svc_get_status(prov_status_info_t *info);

prov_status_t prov_svc_get_info(prov_info_report_t *info);

prov_status_t prov_svc_erase(uint8_t slot_id, uint32_t confirm);

prov_status_t prov_svc_finalize(uint32_t confirm, uint8_t *provisioned_out);

prov_status_t prov_svc_gen_key(uint8_t slot_id, uint16_t alg_id,
                               uint16_t *pub_len_out);

prov_status_t prov_svc_read_pubkey(uint8_t slot_id, uint16_t offset,
                                   uint8_t *buf, uint16_t len,
                                   uint16_t *alg_id_out, uint16_t *total_out,
                                   uint16_t *ret_out);

prov_status_t prov_svc_build_csr(uint8_t slot_id,
                                 const uint8_t *templ, uint16_t templ_len,
                                 uint8_t *out, uint16_t out_cap,
                                 uint16_t *total_out);

prov_status_t prov_svc_cert_commit(uint8_t slot_id, uint8_t key_slot,
                                   const uint8_t *cert, uint32_t total_len,
                                   const uint8_t sha256[PROV_SHA256_LEN],
                                   uint16_t *cert_len_out);

prov_status_t prov_svc_read_cert(uint8_t slot_id, uint16_t offset,
                                 uint8_t *buf, uint16_t len,
                                 uint16_t *total_out, uint16_t *ret_out);

prov_status_t prov_svc_prove_cert(uint8_t slot_id,
                                  const uint8_t nonce[PROV_NONCE_LEN],
                                  uint8_t cert_sha256[PROV_SHA256_LEN],
                                  uint16_t *alg_id_out,
                                  uint8_t *out, uint16_t out_cap,
                                  uint16_t *sig_len_out);

#if defined(__cplusplus)
}
#endif

#endif /* PROV_SVC_H */

/* [] END OF FILE */
