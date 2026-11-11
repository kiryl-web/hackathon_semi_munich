/*****************************************************************************
 * File Name        : prov_router.h
 *
 * Description      : Public interface for the non-secure provisioning command
 *                    router module. The router is registered with the DFU
 *                    middleware as a user-defined command handler; it decodes
 *                    provisioning opcodes (0x50-0xFF), marshals the request
 *                    payloads, calls the secure world through NSC veneers, and
 *                    serializes the responses back to the host.
 *
 *                    The module also owns the DFU command/response service loop
 *                    (prov_router_run), so main.c only performs board / DFU /
 *                    transport bring-up and then hands control to the router.
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

#ifndef PROV_ROUTER_H
#define PROV_ROUTER_H

#include <stdint.h>
#include <stdbool.h>

#include "cy_dfu.h"

#if defined(__cplusplus)
extern "C" {
#endif

/******************************************************************************
 * Macros
 *****************************************************************************/

/* Timeout for a single Cy_DFU_Continue() iteration, in milliseconds. Used by
 * the service loop and by main.c to seed cy_stc_dfu_params_t.timeout so both
 * agree on the per-iteration tick used for the command/LED timers. */
#define PROV_ROUTER_SESSION_TIMEOUT_MS  (20U)

/******************************************************************************
 * Enumerations
 *****************************************************************************/

/**
 * Provisioning command opcodes (Provisioning-Command-Reference §4). These live
 * in the DFU user-defined command space (0x50-0xFF); the DFU command ID is the
 * provisioning opcode. Opcodes are grouped by functional bucket (high nibble):
 *   0x5X — general  (status, info, erase, finalize)
 *   0x6X — key      (gen-key, read-pubkey)
 *   0x7X — cert/CSR (get-csr, cert-write, read-cert, prove-cert)
 */
typedef enum
{
    /* 0x5X — general */
    PROV_CMD_GET_STATUS  = 0x50U,
    PROV_CMD_GET_INFO    = 0x51U,
    PROV_CMD_ERASE       = 0x52U,
    PROV_CMD_FINALIZE    = 0x53U,

    /* 0x6X — key */
    PROV_CMD_GEN_KEY     = 0x60U,
    PROV_CMD_READ_PUBKEY = 0x61U,

    /* 0x7X — certificate & CSR */
    PROV_CMD_GET_CSR     = 0x70U,
    PROV_CMD_CERT_WRITE  = 0x71U,
    PROV_CMD_READ_CERT   = 0x72U,
    PROV_CMD_PROVE_CERT  = 0x73U
} prov_opcode_t;

/******************************************************************************
 * Function Prototypes
 *****************************************************************************/

/**
 * @brief Register the provisioning command router with the DFU middleware.
 *
 * Installs the router as the DFU user-command handler so that opcodes in the
 * 0x50-0xFF range are dispatched to the secure provisioning service.
 *
 * @param dfu_params  Initialized DFU parameters structure.
 *
 * @return CY_DFU_SUCCESS on success, otherwise a DFU error code.
 */
cy_en_dfu_status_t prov_router_register(cy_stc_dfu_params_t *dfu_params);

/**
 * @brief Run the DFU command/response service loop (does not return).
 *
 * Repeatedly calls Cy_DFU_Continue() and manages the provisioning session:
 * provisioning rides on DFU user-defined commands, so a finished/failed/timed-
 * out session is transparently re-armed (re-init + re-register + transport
 * reset) and the heartbeat LED is toggled while idle.
 *
 * @param dfu_params  Initialized DFU parameters structure (its .timeout must be
 *                    PROV_ROUTER_SESSION_TIMEOUT_MS).
 * @param dfu_state   DFU state variable, seeded by Cy_DFU_Init().
 */
void prov_router_run(cy_stc_dfu_params_t *dfu_params, uint32_t *dfu_state);

#if defined(__cplusplus)
}
#endif

#endif /* PROV_ROUTER_H */

/* [] END OF FILE */
