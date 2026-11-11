/*****************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for the
*                    non-secure application on the CM33 CPU. It hosts the DFU
*                    middleware over I2C and the provisioning command router,
*                    which forwards provisioning requests to the secure world
*                    through Non-Secure-Callable (NSC) veneers.
*
* Related Document : See README.md and docs/design_and_implementation.md
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
*******************************************************************************/


/******************************************************************************
 * Header Files
 *****************************************************************************/

#include "cybsp.h"
#include "cy_pdl.h"

#include "log/log.h"

#include "cy_dfu.h"
#include "cy_dfu_logging.h"
#include "transport_i2c.h"
#include "mtb_hal_i2c.h"
#include "cy_scb_i2c.h"
#include "cy_sysint.h"

#include "prov_router.h"

#if defined(PROV_SELFTEST)
#include "prov_selftest.h"
#endif


/******************************************************************************
 * Global Variables
 *****************************************************************************/

/* I2C transport HAL object and context for the DFU transport. */
static mtb_hal_i2c_t            dfuI2cHalObj;
static cy_stc_scb_i2c_context_t dfuI2cContext;


/******************************************************************************
 * Function Prototypes
 *****************************************************************************/

static void dfuI2cIsr(void);
static void dfuI2cTransportCallback(cy_en_dfu_transport_i2c_action_t action);
static void dfu_i2c_transport_init(void);


/******************************************************************************
 * Function Definitions
 *****************************************************************************/

/*******************************************************************************
 * Function Name: dfuI2cIsr
 ********************************************************************************
 * Summary:
 *  I2C interrupt service routine for the DFU transport.
 *
 *******************************************************************************/
static void dfuI2cIsr(void)
{
    mtb_hal_i2c_process_interrupt(&dfuI2cHalObj);
}

/*******************************************************************************
 * Function Name: dfuI2cTransportCallback
 ********************************************************************************
 * Summary:
 *  Enables/disables the DFU I2C SCB block on transport init/deinit.
 *
 *******************************************************************************/
static void dfuI2cTransportCallback(cy_en_dfu_transport_i2c_action_t action)
{
    if (action == CY_DFU_TRANSPORT_I2C_INIT)
    {
        Cy_SCB_I2C_Enable(DFU_I2C_HW);
    }
    else if (action == CY_DFU_TRANSPORT_I2C_DEINIT)
    {
        Cy_SCB_I2C_Disable(DFU_I2C_HW, &dfuI2cContext);
    }
    else
    {
        /* No action for other callback triggers. */
    }
}

/*******************************************************************************
 * Function Name: dfu_i2c_transport_init
 ********************************************************************************
 * Summary:
 *  Configures the I2C SCB and HAL objects used by the DFU transport. The
 *  DFU_I2C_* resource is provided by the BSP design.modus configuration
 *  (an SCB configured as an I2C slave and aliased as DFU_I2C).
 *
 *******************************************************************************/
static void dfu_i2c_transport_init(void)
{
    cy_en_scb_i2c_status_t pdlI2cStatus;
    cy_en_sysint_status_t  pdlSysIntStatus;
    cy_rslt_t              halStatus;

    pdlI2cStatus = Cy_SCB_I2C_Init(DFU_I2C_HW, &DFU_I2C_config, &dfuI2cContext);
    if (CY_SCB_I2C_SUCCESS != pdlI2cStatus)
    {
        LOG_ERR("I2C PDL init failed: 0x%X", (unsigned int)pdlI2cStatus);
        CY_ASSERT(0);
    }

    halStatus = mtb_hal_i2c_setup(&dfuI2cHalObj, &DFU_I2C_hal_config, &dfuI2cContext, NULL);
    if (CY_RSLT_SUCCESS != halStatus)
    {
        LOG_ERR("I2C HAL setup failed: 0x%X", (unsigned int)halStatus);
        CY_ASSERT(0);
    }

    cy_stc_sysint_t i2cIsrCfg =
    {
        .intrSrc      = DFU_I2C_IRQ,
        .intrPriority = 3U
    };
    pdlSysIntStatus = Cy_SysInt_Init(&i2cIsrCfg, dfuI2cIsr);
    if (CY_SYSINT_SUCCESS != pdlSysIntStatus)
    {
        LOG_ERR("I2C interrupt init failed: 0x%X", (unsigned int)pdlSysIntStatus);
        CY_ASSERT(0);
    }
    NVIC_EnableIRQ((IRQn_Type)i2cIsrCfg.intrSrc);

    cy_stc_dfu_transport_i2c_cfg_t i2cTransportCfg =
    {
        .i2c      = &dfuI2cHalObj,
        .callback = dfuI2cTransportCallback,
    };
    Cy_DFU_TransportI2cConfig(&i2cTransportCfg);
}

/**
 * @brief Main function of the CM33 Non-secure project.
 *
 * Initializes the board, the DFU middleware and the I2C transport, registers
 * the provisioning command router as the DFU user-command handler, and then
 * services the DFU command/response loop.
 *
 * @return Should not return.
 */
int main(void)
{
    cy_rslt_t result;
    cy_en_dfu_status_t dfu_status;
    uint32_t dfu_state = CY_DFU_STATE_NONE;

    /* Buffer to store DFU command data. */
    CY_ALIGN(4) static uint8_t dfu_buffer[CY_DFU_SIZEOF_DATA_BUFFER];
    /* Buffer for DFU data packets for the transport API. */
    CY_ALIGN(4) static uint8_t dfu_packet[CY_DFU_SIZEOF_CMD_BUFFER];

    cy_stc_dfu_params_t dfu_params =
    {
        .timeout      = PROV_ROUTER_SESSION_TIMEOUT_MS,
        .dataBuffer   = &dfu_buffer[0],
        .packetBuffer = &dfu_packet[0],
    };

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize logging (configures the debug UART and retarget-io). */
    log_init();

    LOG_INFO("=== Non-secure provisioning application ===");
    LOG_INFO("Board init complete, starting DFU/provisioning bring-up");

    /* Initialize the DFU middleware. */
    dfu_status = Cy_DFU_Init(&dfu_state, &dfu_params);
    if (CY_DFU_SUCCESS != dfu_status)
    {
        LOG_ERR("DFU MW init failed: 0x%X", (unsigned int)dfu_status);
        CY_ASSERT(0);
    }
    LOG_INFO("DFU middleware initialized");

    /* Register the provisioning command router for user-defined opcodes. */
    dfu_status = prov_router_register(&dfu_params);
    if (CY_DFU_SUCCESS != dfu_status)
    {
        LOG_ERR("Provisioning router register failed: 0x%X", (unsigned int)dfu_status);
        CY_ASSERT(0);
    }
    LOG_INFO("Provisioning command router registered");

#if defined(PROV_SELFTEST)
    /* Build-time self-test: exercise every secure provisioning NSC service
     * directly (no transport needed), then continue into the normal DFU loop. */
    (void)prov_selftest_run();
#endif

    /* Bring up the I2C transport and start listening for the host. */
    dfu_i2c_transport_init();
    Cy_DFU_TransportStart(CY_DFU_I2C);
    LOG_INFO("I2C transport started; waiting for host commands");

    /* Hand control to the provisioning router's DFU service loop. */
    prov_router_run(&dfu_params, &dfu_state);

    /* prov_router_run() never returns. */
    CY_UNUSED_PARAMETER(dfu_status);
}

/* [] END OF FILE */
