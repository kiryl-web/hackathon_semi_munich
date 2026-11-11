/*****************************************************************************
 * File Name        : prov_router.c
 *
 * Description      : Non-secure provisioning command router. Registered with
 *                    the DFU middleware as the user-defined command handler, it
 *                    decodes provisioning opcodes (0x50-0x73), marshals each
 *                    request payload, calls the secure world through the NSC
 *                    veneers (prov_nsc_*), and serializes the responses for the
 *                    host. The module also owns the DFU service loop.
 *
 *                    Wire formats follow Provisioning-Command-Reference §4-§8.
 *                    All multi-byte fields are little-endian. Every response
 *                    payload begins with the prov_cmd_status byte; on a
 *                    non-PROV_OK status only that single byte is returned
 *                    (except where the contract defines fixed leading fields).
 *
 *                    The NS-App holds no secrets. The only blob it stages in
 *                    SRAM is the inbound certificate during chunked write; the
 *                    CSR lives in secure RAM and is streamed chunk-by-chunk.
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

#include "cybsp.h"
#include "cy_pdl.h"

#include "prov_router.h"
#include "prov_nsc.h"
#include "log/log.h"

/******************************************************************************
 * Macros
 *****************************************************************************/

/* LED blink period in milliseconds (idle heartbeat). */
#define BLINKY_LED_DELAY_MSEC       (500U)

/* No command received within this window restarts the DFU session. */
#define DFU_COMMAND_TIMEOUT_MS      (5000U)

/* Non-secure staging buffer, shared by two mutually-exclusive flows:
 *   - PROV_CERT_WRITE : accumulates the inbound certificate chain here before
 *     it is committed to flash in one prov_nsc_cert_commit() call.
 *   - PROV_GET_CSR    : the secure world builds the whole CSR straight into
 *     this buffer (prov_nsc_build_csr), after which it is streamed to the host
 *     without any further secure calls.
 * The private key never enters this buffer; the CSR carries only public data.
 * When the PQC suite is enabled the inbound ML-DSA-87 SPDM cert_chain (root +
 * leaf ML-DSA-87 certs + SPDM header/RootHash ~= 15 KB) dominates the sizing,
 * exceeding even the ML-DSA-87 CSR (~7.3 KB), so the buffer grows to 16 KiB;
 * the ECDSA-only build keeps 4 KiB, which covers both its ~0.4 KB CSR and the
 * configured certificate slot. */
#if defined(PROV_ENABLE_MLDSA87)
#define PROV_STAGE_BUF_SIZE         (0x4000U)
#else
#define PROV_STAGE_BUF_SIZE         (0x1000U)
#endif

/* Response field offsets shared by the chunked-read commands
 * (status(1) | total(2) | ret(2) | bytes...). */
#define PROV_READ_RSP_HDR_LEN       (5U)

/* PROV_PROVE_CERT chunked-response header:
 * status(1) | total(2) | alg_id(2) | cert_sha256(32) | ret(2) | bytes... */
#define PROV_PROVE_RSP_HDR_LEN      (7U + PROV_SHA256_LEN)

/* PROV_CERT_WRITE first-chunk header length:
 * slot_id(1) | offset(2) | key_slot(1) | total_len(4) | sha256(32). */
#define PROV_CERT_WR_HDR_LEN        (40U)

/******************************************************************************
 * Module State
 *****************************************************************************/

/* Total length of the CSR currently held in the shared staging buffer,
 * captured from prov_nsc_build_csr() on the first (offset == 0) PROV_GET_CSR
 * chunk and echoed on every subsequent chunk's response. */
static uint16_t s_csr_total;

/* PROV_PROVE_CERT staging: the signature (up to 4627 B for ML-DSA-87) is built
 * into the shared staging buffer on the first (offset == 0) chunk; the length,
 * suite id and certificate digest are cached here and echoed on every chunk. */
static uint16_t s_prove_total;              /* full signature length          */
static uint16_t s_prove_alg;                /* signing suite id               */
static uint8_t  s_prove_sha[PROV_SHA256_LEN]; /* H = SHA256(cert)             */

/* Certificate chunked-write staging (PROV_CERT_WRITE). */
static uint8_t  s_stage_buf[PROV_STAGE_BUF_SIZE];
static uint32_t s_stage_len;        /* bytes accumulated so far              */
static uint32_t s_stage_total;      /* declared total_len (first chunk)      */
static uint8_t  s_stage_cert_slot;  /* target certificate slot               */
static uint8_t  s_stage_key_slot;   /* bound key slot                        */
static uint8_t  s_stage_sha[PROV_SHA256_LEN]; /* declared SHA-256            */

/******************************************************************************
 * Local Function Prototypes
 *****************************************************************************/

static cy_en_dfu_status_t prov_router_handle_command(uint32_t command, uint8_t *packetData,
                                                     uint32_t dataSize, uint32_t *rspSize,
                                                     struct cy_stc_dfu_params_s *params,
                                                     bool *noResponse);

static cy_en_dfu_status_t prov_handle_get_status(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize);
static cy_en_dfu_status_t prov_handle_get_info(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize);
static cy_en_dfu_status_t prov_handle_erase(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize);
static cy_en_dfu_status_t prov_handle_finalize(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize);
static cy_en_dfu_status_t prov_handle_gen_key(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize);
static cy_en_dfu_status_t prov_handle_read_pubkey(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize);
static cy_en_dfu_status_t prov_handle_get_csr(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize);
static cy_en_dfu_status_t prov_handle_cert_write(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize);
static cy_en_dfu_status_t prov_handle_read_cert(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize);
static cy_en_dfu_status_t prov_handle_prove_cert(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize);

/******************************************************************************
 * Little-endian (de)serialization helpers
 *****************************************************************************/

static inline void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static inline void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static inline uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Emit a single-byte error response carrying a prov_cmd_status. */
static cy_en_dfu_status_t prov_error_rsp(uint8_t *pd, uint32_t *rspSize, prov_status_t code)
{
    pd[0] = (uint8_t)code;
    *rspSize = 1U;
    return CY_DFU_SUCCESS;
}

/******************************************************************************
 * Public Functions
 *****************************************************************************/

cy_en_dfu_status_t prov_router_register(cy_stc_dfu_params_t *dfu_params)
{
    return Cy_DFU_RegisterUserCommand(dfu_params, prov_router_handle_command);
}

void prov_router_run(cy_stc_dfu_params_t *dfu_params, uint32_t *dfu_state)
{
    cy_en_dfu_status_t dfu_status;
    uint32_t count = 0U;
    uint32_t tick  = (dfu_params->timeout != 0U) ? dfu_params->timeout
                                                  : PROV_ROUTER_SESSION_TIMEOUT_MS;

    for (;;)
    {
        dfu_status = Cy_DFU_Continue(dfu_state, dfu_params);
        count++;

        if (*dfu_state == CY_DFU_STATE_FINISHED)
        {
            /* Provisioning rides on user-defined commands; reset the session
             * so the device stays available for the next transaction. */
            LOG_INFO("DFU session finished; restarting session");
            count = 0U;
            (void)Cy_DFU_Init(dfu_state, dfu_params);
            (void)prov_router_register(dfu_params);
            Cy_DFU_TransportReset();
        }
        else if (*dfu_state == CY_DFU_STATE_FAILED)
        {
            LOG_ERR("DFU session failed: 0x%X; restarting session", (unsigned int)dfu_status);
            count = 0U;
            (void)Cy_DFU_Init(dfu_state, dfu_params);
            (void)prov_router_register(dfu_params);
            Cy_DFU_TransportReset();
        }
        else if (*dfu_state == CY_DFU_STATE_UPDATING)
        {
            if ((dfu_status == CY_DFU_ERROR_TIMEOUT) &&
                (count >= (DFU_COMMAND_TIMEOUT_MS / tick)))
            {
                /* No command received recently; restart the DFU session. */
                LOG_WARN("Command timeout; restarting session");
                count = 0U;
                (void)Cy_DFU_Init(dfu_state, dfu_params);
                (void)prov_router_register(dfu_params);
                Cy_DFU_TransportReset();
            }
            else if (dfu_status == CY_DFU_SUCCESS)
            {
                count = 0U;
            }
            else
            {
                /* Other transient errors: keep servicing the session. */
            }
        }
        else
        {
            /* CY_DFU_STATE_NONE: idle. Toggle the heartbeat LED. */
            if (count >= (BLINKY_LED_DELAY_MSEC / tick))
            {
                Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
                count = 0U;
            }
        }
    }
}

/******************************************************************************
 * Command Dispatch
 *****************************************************************************/

/*******************************************************************************
 * Function Name: prov_router_handle_command
 ********************************************************************************
 * Summary:
 *  DFU user-defined command handler (Cy_DFU_CustomCommandHandler). Decodes the
 *  provisioning opcode and dispatches to the matching handler. The provisioning
 *  result is returned as the first payload byte (prov_cmd_status); the DFU-level
 *  status is the function return value.
 *
 *******************************************************************************/
static cy_en_dfu_status_t prov_router_handle_command(uint32_t command, uint8_t *packetData,
                                                     uint32_t dataSize, uint32_t *rspSize,
                                                     struct cy_stc_dfu_params_s *params,
                                                     bool *noResponse)
{
    cy_en_dfu_status_t status;

    CY_UNUSED_PARAMETER(params);

    /* All provisioning commands send a response packet. */
    *noResponse = false;
    *rspSize = 0U;

    LOG_INFO("Command 0x%02X received (dataSize=%u)",
             (unsigned int)command, (unsigned int)dataSize);

    switch ((prov_opcode_t)command)
    {
        /* 0x5X — general */
        case PROV_CMD_GET_STATUS:
            status = prov_handle_get_status(packetData, dataSize, rspSize);
            break;
        case PROV_CMD_GET_INFO:
            status = prov_handle_get_info(packetData, dataSize, rspSize);
            break;
        case PROV_CMD_ERASE:
            status = prov_handle_erase(packetData, dataSize, rspSize);
            break;
        case PROV_CMD_FINALIZE:
            status = prov_handle_finalize(packetData, dataSize, rspSize);
            break;

        /* 0x6X — key */
        case PROV_CMD_GEN_KEY:
            status = prov_handle_gen_key(packetData, dataSize, rspSize);
            break;
        case PROV_CMD_READ_PUBKEY:
            status = prov_handle_read_pubkey(packetData, dataSize, rspSize);
            break;

        /* 0x7X — certificate & CSR */
        case PROV_CMD_GET_CSR:
            status = prov_handle_get_csr(packetData, dataSize, rspSize);
            break;
        case PROV_CMD_CERT_WRITE:
            status = prov_handle_cert_write(packetData, dataSize, rspSize);
            break;
        case PROV_CMD_READ_CERT:
            status = prov_handle_read_cert(packetData, dataSize, rspSize);
            break;
        case PROV_CMD_PROVE_CERT:
            status = prov_handle_prove_cert(packetData, dataSize, rspSize);
            break;

        default:
            /* Not a provisioning opcode; let the DFU core report an error. */
            LOG_WARN("Unknown opcode 0x%02X; rejecting", (unsigned int)command);
            status = CY_DFU_ERROR_CMD;
            break;
    }

    LOG_DBG("Command 0x%02X handled: status=0x%X, rspSize=%u",
            (unsigned int)command, (unsigned int)status, (unsigned int)*rspSize);

    return status;
}

/******************************************************************************
 * Command Handlers — general (0x5X)
 *****************************************************************************/

/*******************************************************************************
 * Function Name: prov_handle_get_status  (PROV_GET_STATUS / 0x50)
 ********************************************************************************
 * Response: prov_cmd_status(1) | provisioned(1) | num_slots(1) | pas_version(2)
 *           | slot_type(1) * num_slots.
 *******************************************************************************/
static cy_en_dfu_status_t prov_handle_get_status(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize)
{
    prov_status_info_t info;
    prov_status_t result;
    uint8_t i;

    CY_UNUSED_PARAMETER(dataSize);   /* PROV_GET_STATUS takes no request payload */

    result = prov_nsc_get_status(&info);
    pd[0] = (uint8_t)result;

    if (result != PROV_OK)
    {
        *rspSize = 1U;
        LOG_ERR("GET_STATUS failed: prov_status=0x%02X", (unsigned int)result);
        return CY_DFU_SUCCESS;
    }

    if (info.num_slots > PROV_NUM_SLOTS)
    {
        info.num_slots = PROV_NUM_SLOTS;
    }

    pd[1] = info.provisioned;
    pd[2] = info.num_slots;
    put_u16(&pd[3], info.pas_version);
    for (i = 0U; i < info.num_slots; i++)
    {
        pd[5U + i] = info.slot_type[i];
    }
    *rspSize = 5U + (uint32_t)info.num_slots;

    LOG_INFO("GET_STATUS: provisioned=%u num_slots=%u pas_ver=0x%04X",
             (unsigned int)info.provisioned, (unsigned int)info.num_slots,
             (unsigned int)info.pas_version);
    return CY_DFU_SUCCESS;
}

/*******************************************************************************
 * Function Name: prov_handle_get_info  (PROV_GET_INFO / 0x51)
 ********************************************************************************
 * Response: prov_cmd_status(1) | uid(PROV_UID_LEN) | caps(2) | num_slots(1)
 *           | slot_size(2) * num_slots | n_algs(1)
 *           | { alg_id(2), pub_len(2), sig_len(2) } * n_algs | max_chunk(2).
 *******************************************************************************/
static cy_en_dfu_status_t prov_handle_get_info(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize)
{
    prov_info_report_t info;
    prov_status_t result;
    uint32_t i = 1U;
    uint8_t s;

    CY_UNUSED_PARAMETER(dataSize);   /* PROV_GET_INFO takes no request payload */

    result = prov_nsc_get_info(&info);
    pd[0] = (uint8_t)result;

    if (result != PROV_OK)
    {
        *rspSize = 1U;
        LOG_ERR("GET_INFO failed: prov_status=0x%02X", (unsigned int)result);
        return CY_DFU_SUCCESS;
    }

    if (info.num_slots > PROV_NUM_SLOTS)
    {
        info.num_slots = PROV_NUM_SLOTS;
    }
    if (info.n_algs > PROV_MAX_ALGS)
    {
        info.n_algs = PROV_MAX_ALGS;
    }

    memcpy(&pd[i], info.uid, PROV_UID_LEN);
    i += PROV_UID_LEN;
    put_u16(&pd[i], info.caps);
    i += 2U;
    pd[i++] = info.num_slots;
    for (s = 0U; s < info.num_slots; s++)
    {
        put_u16(&pd[i], info.slot_size[s]);
        i += 2U;
    }
    pd[i++] = info.n_algs;
    for (s = 0U; s < info.n_algs; s++)
    {
        put_u16(&pd[i], info.algs[s].alg_id);
        i += 2U;
        put_u16(&pd[i], info.algs[s].pub_len);
        i += 2U;
        put_u16(&pd[i], info.algs[s].sig_len);
        i += 2U;
    }
    put_u16(&pd[i], info.max_chunk);
    i += 2U;

    *rspSize = i;
    LOG_INFO("GET_INFO: num_slots=%u n_algs=%u max_chunk=%u",
             (unsigned int)info.num_slots, (unsigned int)info.n_algs,
             (unsigned int)info.max_chunk);
    return CY_DFU_SUCCESS;
}

/*******************************************************************************
 * Function Name: prov_handle_erase  (PROV_ERASE / 0x52)
 ********************************************************************************
 * Request : slot_id(1) (0xFF = all) | confirm(4).
 * Response: prov_cmd_status(1).
 *******************************************************************************/
static cy_en_dfu_status_t prov_handle_erase(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize)
{
    uint8_t  slot_id;
    uint32_t confirm;

    if (dataSize < 5U)
    {
        return prov_error_rsp(pd, rspSize, PROV_ERR_PARAM);
    }

    slot_id = pd[0];
    confirm = get_u32(&pd[1]);

    pd[0] = (uint8_t)prov_nsc_erase(slot_id, confirm);
    *rspSize = 1U;
    return CY_DFU_SUCCESS;
}

/*******************************************************************************
 * Function Name: prov_handle_finalize  (PROV_FINALIZE / 0x53)
 ********************************************************************************
 * Request : confirm(4).
 * Response: prov_cmd_status(1) | provisioned(1) (on PROV_OK).
 *******************************************************************************/
static cy_en_dfu_status_t prov_handle_finalize(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize)
{
    uint32_t confirm;
    uint8_t  provisioned = 0U;
    prov_status_t result;

    if (dataSize < 4U)
    {
        return prov_error_rsp(pd, rspSize, PROV_ERR_PARAM);
    }

    confirm = get_u32(&pd[0]);
    result  = prov_nsc_finalize(confirm, &provisioned);

    pd[0] = (uint8_t)result;
    if (result == PROV_OK)
    {
        pd[1] = provisioned;
        *rspSize = 2U;
        LOG_INFO("FINALIZE: provisioned=%u", (unsigned int)provisioned);
    }
    else
    {
        *rspSize = 1U;
        LOG_ERR("FINALIZE failed: prov_status=0x%02X", (unsigned int)result);
    }
    return CY_DFU_SUCCESS;
}

/******************************************************************************
 * Command Handlers — key (0x6X)
 *****************************************************************************/

/*******************************************************************************
 * Function Name: prov_handle_gen_key  (PROV_GEN_KEY / 0x60)
 ********************************************************************************
 * Request : slot_id(1) | alg_id(2).
 * Response: prov_cmd_status(1) | slot_id(1) | alg_id(2) | pub_len(2) (on PROV_OK).
 *******************************************************************************/
static cy_en_dfu_status_t prov_handle_gen_key(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize)
{
    uint8_t  slot_id;
    uint16_t alg_id;
    uint16_t pub_len = 0U;
    prov_status_t result;

    if (dataSize < 3U)
    {
        return prov_error_rsp(pd, rspSize, PROV_ERR_PARAM);
    }

    slot_id = pd[0];
    alg_id  = get_u16(&pd[1]);

    result = prov_nsc_gen_key(slot_id, alg_id, &pub_len);

    pd[0] = (uint8_t)result;
    if (result == PROV_OK)
    {
        pd[1] = slot_id;
        put_u16(&pd[2], alg_id);
        put_u16(&pd[4], pub_len);
        *rspSize = 6U;
        LOG_INFO("GEN_KEY: slot=%u alg=0x%04X pub_len=%u",
                 (unsigned int)slot_id, (unsigned int)alg_id, (unsigned int)pub_len);
    }
    else
    {
        *rspSize = 1U;
        LOG_ERR("GEN_KEY failed: prov_status=0x%02X", (unsigned int)result);
    }
    return CY_DFU_SUCCESS;
}

/*******************************************************************************
 * Function Name: prov_handle_read_pubkey  (PROV_READ_PUBKEY / 0x61)
 ********************************************************************************
 * Request : slot_id(1) | offset(2) | len(2).
 * Response: prov_cmd_status(1) | alg_id(2) | total(2) | ret(2) | bytes(ret).
 *******************************************************************************/
static cy_en_dfu_status_t prov_handle_read_pubkey(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize)
{
    prov_read_pubkey_args_t args;
    uint16_t alg_id = 0U;
    uint16_t total  = 0U;
    uint16_t ret    = 0U;
    uint16_t len;
    prov_status_t result;

    if (dataSize < 5U)
    {
        return prov_error_rsp(pd, rspSize, PROV_ERR_PARAM);
    }

    len = get_u16(&pd[3]);
    if (len > PROV_MAX_CHUNK)
    {
        len = PROV_MAX_CHUNK;
    }

    args.slot_id    = pd[0];
    args.offset     = get_u16(&pd[1]);
    args.buf        = &pd[7];          /* response bytes region */
    args.len        = len;
    args.alg_id_out = &alg_id;
    args.total_out  = &total;
    args.ret_out    = &ret;

    result = prov_nsc_read_pubkey(&args);

    pd[0] = (uint8_t)result;
    if (result == PROV_OK)
    {
        put_u16(&pd[1], alg_id);
        put_u16(&pd[3], total);
        put_u16(&pd[5], ret);
        *rspSize = 7U + (uint32_t)ret;
    }
    else
    {
        *rspSize = 1U;
        LOG_ERR("READ_PUBKEY failed: prov_status=0x%02X", (unsigned int)result);
    }
    return CY_DFU_SUCCESS;
}

/******************************************************************************
 * Command Handlers — certificate & CSR (0x7X)
 *****************************************************************************/

/*******************************************************************************
 * Function Name: prov_handle_get_csr  (PROV_GET_CSR / 0x70)
 ********************************************************************************
 * Request : slot_id(1) | offset(2) | len(2) | subject template (when offset==0).
 * Response: prov_cmd_status(1) | total(2) | ret(2) | bytes(ret).
 *
 * On the first chunk (offset == 0) the CSR is (re)built by the secure world
 * straight into the shared non-secure staging buffer and its total length
 * cached; every chunk then streams bytes from that buffer with no further
 * secure calls.
 *******************************************************************************/
static cy_en_dfu_status_t prov_handle_get_csr(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize)
{
    uint8_t  slot_id;
    uint16_t offset;
    uint16_t len;
    uint16_t ret = 0U;
    prov_status_t result;

    if (dataSize < PROV_READ_RSP_HDR_LEN)
    {
        return prov_error_rsp(pd, rspSize, PROV_ERR_PARAM);
    }

    slot_id = pd[0];
    offset  = get_u16(&pd[1]);
    len     = get_u16(&pd[3]);
    if (len > PROV_MAX_CHUNK)
    {
        len = PROV_MAX_CHUNK;
    }

    if (offset == 0U)
    {
        const uint8_t *templ = NULL;
        uint16_t templ_len = 0U;
        uint16_t total = 0U;
        prov_build_csr_args_t args;

        /* Optional subject template trails the fixed request header. It is read
         * (and copied internally) by prov_nsc_build_csr() before the CSR bytes
         * are written into the shared staging buffer below. */
        if (dataSize > PROV_READ_RSP_HDR_LEN)
        {
            templ = &pd[PROV_READ_RSP_HDR_LEN];
            templ_len = (uint16_t)(dataSize - PROV_READ_RSP_HDR_LEN);
        }

        /* Building a CSR reuses the shared staging buffer; abandon any partial
         * certificate-write session so the two flows cannot interleave. */
        s_stage_len = 0U;
        s_stage_total = 0U;

        args.slot_id   = slot_id;
        args.templ     = templ;
        args.templ_len = templ_len;
        args.out       = s_stage_buf;
        args.out_cap   = (uint16_t)sizeof(s_stage_buf);
        args.total_out = &total;

        result = prov_nsc_build_csr(&args);
        if (result != PROV_OK)
        {
            LOG_ERR("GET_CSR build failed: prov_status=0x%02X", (unsigned int)result);
            return prov_error_rsp(pd, rspSize, result);
        }
        s_csr_total = total;
        LOG_INFO("GET_CSR: built CSR total=%u", (unsigned int)total);
    }

    /* Stream the requested window out of the staging buffer. The CSR is public
     * data and already validated by the secure build, so no further secure
     * call is needed for the read-back. */
    if (offset < s_csr_total)
    {
        uint16_t avail = (uint16_t)(s_csr_total - offset);
        ret = (len < avail) ? len : avail;
        (void)memcpy(&pd[PROV_READ_RSP_HDR_LEN], &s_stage_buf[offset], ret);
    }

    pd[0] = (uint8_t)PROV_OK;
    put_u16(&pd[1], s_csr_total);
    put_u16(&pd[3], ret);
    *rspSize = PROV_READ_RSP_HDR_LEN + (uint32_t)ret;
    return CY_DFU_SUCCESS;
}

/*******************************************************************************
 * Function Name: prov_handle_cert_write  (PROV_CERT_WRITE / 0x71)
 ********************************************************************************
 * Request (offset==0): slot_id(1) | offset(2) | key_slot(1) | total_len(4)
 *                      | sha256(32) | bytes.
 * Request (offset>0) : slot_id(1) | offset(2) | bytes.
 * Response           : prov_cmd_status(1) | recv_total(4)
 *                      [ | committed(1) | cert_len(2) on the final chunk].
 *
 * Bytes are staged in NS SRAM; when recv_total reaches total_len the whole blob
 * is committed to secure flash in one prov_nsc_cert_commit() call (the secure
 * world re-hashes and verifies transfer integrity).
 *******************************************************************************/
static cy_en_dfu_status_t prov_handle_cert_write(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize)
{
    uint8_t  slot_id;
    uint16_t offset;

    if (dataSize < 3U)
    {
        return prov_error_rsp(pd, rspSize, PROV_ERR_PARAM);
    }

    slot_id = pd[0];
    offset  = get_u16(&pd[1]);

    if (offset == 0U)
    {
        uint8_t  key_slot;
        uint32_t total_len;
        uint32_t chunk_len;

        if (dataSize < PROV_CERT_WR_HDR_LEN)
        {
            return prov_error_rsp(pd, rspSize, PROV_ERR_PARAM);
        }

        key_slot  = pd[3];
        total_len = get_u32(&pd[4]);
        chunk_len = dataSize - PROV_CERT_WR_HDR_LEN;

        if ((total_len == 0U) || (total_len > PROV_STAGE_BUF_SIZE) ||
            (chunk_len > total_len))
        {
            return prov_error_rsp(pd, rspSize, PROV_ERR_LENGTH);
        }

        s_stage_total     = total_len;
        s_stage_cert_slot = slot_id;
        s_stage_key_slot  = key_slot;
        memcpy(s_stage_sha, &pd[8], PROV_SHA256_LEN);
        if (chunk_len > 0U)
        {
            memcpy(&s_stage_buf[0], &pd[PROV_CERT_WR_HDR_LEN], chunk_len);
        }
        s_stage_len = chunk_len;
    }
    else
    {
        uint32_t chunk_len = dataSize - 3U;

        /* Offset-ordered append against the in-progress staging session. */
        if ((slot_id != s_stage_cert_slot) ||
            ((uint32_t)offset != s_stage_len) ||
            (((uint32_t)offset + chunk_len) > s_stage_total))
        {
            return prov_error_rsp(pd, rspSize, PROV_ERR_LENGTH);
        }

        if (chunk_len > 0U)
        {
            memcpy(&s_stage_buf[offset], &pd[3], chunk_len);
            s_stage_len += chunk_len;
        }
    }

    pd[0] = (uint8_t)PROV_OK;
    put_u32(&pd[1], s_stage_len);
    *rspSize = 5U;

    if (s_stage_len == s_stage_total)
    {
        prov_cert_commit_args_t args;
        uint16_t cert_len = 0U;
        prov_status_t result;

        args.slot_id      = s_stage_cert_slot;
        args.key_slot     = s_stage_key_slot;
        args.cert         = s_stage_buf;
        args.total_len    = s_stage_total;
        args.sha256       = s_stage_sha;
        args.cert_len_out = &cert_len;

        result = prov_nsc_cert_commit(&args);

        pd[0] = (uint8_t)result;
        if (result == PROV_OK)
        {
            pd[5] = 1U;                 /* committed */
            put_u16(&pd[6], cert_len);
            *rspSize = 8U;
            LOG_INFO("CERT_WRITE: committed slot=%u cert_len=%u",
                     (unsigned int)s_stage_cert_slot, (unsigned int)cert_len);
        }
        else
        {
            LOG_ERR("CERT_WRITE commit failed: prov_status=0x%02X", (unsigned int)result);
        }

        /* Transfer complete (committed or rejected): drop the staging session. */
        s_stage_len = 0U;
        s_stage_total = 0U;
    }

    return CY_DFU_SUCCESS;
}

/*******************************************************************************
 * Function Name: prov_handle_read_cert  (PROV_READ_CERT / 0x72)
 ********************************************************************************
 * Request : slot_id(1) | offset(2) | len(2).
 * Response: prov_cmd_status(1) | total(2) | ret(2) | bytes(ret).
 *******************************************************************************/
static cy_en_dfu_status_t prov_handle_read_cert(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize)
{
    prov_read_cert_args_t args;
    uint16_t total = 0U;
    uint16_t ret   = 0U;
    uint16_t len;
    prov_status_t result;

    if (dataSize < PROV_READ_RSP_HDR_LEN)
    {
        return prov_error_rsp(pd, rspSize, PROV_ERR_PARAM);
    }

    len = get_u16(&pd[3]);
    if (len > PROV_MAX_CHUNK)
    {
        len = PROV_MAX_CHUNK;
    }

    args.slot_id   = pd[0];
    args.offset    = get_u16(&pd[1]);
    args.buf       = &pd[PROV_READ_RSP_HDR_LEN];
    args.len       = len;
    args.total_out = &total;
    args.ret_out   = &ret;

    result = prov_nsc_read_cert(&args);

    pd[0] = (uint8_t)result;
    if (result == PROV_OK)
    {
        put_u16(&pd[1], total);
        put_u16(&pd[3], ret);
        *rspSize = PROV_READ_RSP_HDR_LEN + (uint32_t)ret;
    }
    else
    {
        *rspSize = 1U;
        LOG_ERR("READ_CERT failed: prov_status=0x%02X", (unsigned int)result);
    }
    return CY_DFU_SUCCESS;
}

/*******************************************************************************
 * Function Name: prov_handle_prove_cert  (PROV_PROVE_CERT / 0x73)
 ********************************************************************************
 * Request (offset==0): slot_id(1) | offset(2)=0 | len(2) | nonce(32).
 * Request (offset>0) : slot_id(1) | offset(2) | len(2).
 * Response: prov_cmd_status(1) | total(2) | alg_id(2) | cert_sha256(32)
 *           | ret(2) | signature_bytes(ret).
 *
 * The signature can be large (ML-DSA-87 is 4627 B), so on the first chunk the
 * secure world hashes the certificate and signs nonce||H straight into the
 * shared staging buffer; the length, suite id and digest are cached and every
 * chunk (including the first) streams a window of the signature from there.
 * ECDSA-P256 (64 B) completes in a single chunk.
 *******************************************************************************/
static cy_en_dfu_status_t prov_handle_prove_cert(uint8_t *pd, uint32_t dataSize, uint32_t *rspSize)
{
    uint8_t  slot_id;
    uint16_t offset;
    uint16_t len;
    uint16_t ret = 0U;

    if (dataSize < PROV_READ_RSP_HDR_LEN)
    {
        return prov_error_rsp(pd, rspSize, PROV_ERR_PARAM);
    }

    slot_id = pd[0];
    offset  = get_u16(&pd[1]);
    len     = get_u16(&pd[3]);
    if (len > PROV_MAX_CHUNK)
    {
        len = PROV_MAX_CHUNK;
    }

    if (offset == 0U)
    {
        uint8_t nonce[PROV_NONCE_LEN];
        prov_prove_cert_args_t args;
        prov_status_t result;

        if (dataSize < (PROV_READ_RSP_HDR_LEN + PROV_NONCE_LEN))
        {
            return prov_error_rsp(pd, rspSize, PROV_ERR_PARAM);
        }
        /* The nonce trails the request header; copy it out before the veneer
         * overwrites the packet with the response. */
        memcpy(nonce, &pd[PROV_READ_RSP_HDR_LEN], PROV_NONCE_LEN);

        /* Signing reuses the shared staging buffer; abandon any partial
         * certificate-write session so the flows cannot interleave. */
        s_stage_len = 0U;
        s_stage_total = 0U;

        args.slot_id     = slot_id;
        args.nonce       = nonce;
        args.cert_sha256 = s_prove_sha;
        args.alg_id_out  = &s_prove_alg;
        args.out         = s_stage_buf;
        args.out_cap     = (uint16_t)sizeof(s_stage_buf);
        args.sig_len_out = &s_prove_total;

        result = prov_nsc_prove_cert(&args);
        if (result != PROV_OK)
        {
            LOG_ERR("PROVE_CERT failed: prov_status=0x%02X", (unsigned int)result);
            return prov_error_rsp(pd, rspSize, result);
        }
        LOG_INFO("PROVE_CERT: alg=0x%04X sig_len=%u", (unsigned int)s_prove_alg,
                 (unsigned int)s_prove_total);
    }

    /* Stream the requested window out of the staging buffer. The signature is
     * public data already produced by the secure sign, so no further secure
     * call is needed for the read-back. */
    if (offset < s_prove_total)
    {
        uint16_t avail = (uint16_t)(s_prove_total - offset);
        ret = (len < avail) ? len : avail;
        (void)memcpy(&pd[PROV_PROVE_RSP_HDR_LEN], &s_stage_buf[offset], ret);
    }

    pd[0] = (uint8_t)PROV_OK;
    put_u16(&pd[1], s_prove_total);
    put_u16(&pd[3], s_prove_alg);
    memcpy(&pd[5], s_prove_sha, PROV_SHA256_LEN);
    put_u16(&pd[5U + PROV_SHA256_LEN], ret);
    *rspSize = PROV_PROVE_RSP_HDR_LEN + (uint32_t)ret;
    return CY_DFU_SUCCESS;
}

/* [] END OF FILE */
