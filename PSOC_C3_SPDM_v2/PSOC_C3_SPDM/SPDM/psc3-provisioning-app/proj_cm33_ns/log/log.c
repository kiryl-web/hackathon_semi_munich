/*****************************************************************************
 * File Name        : log.c
 *
 * Description      : Logging facility implementation. Provides log_init(),
 *                    which initializes the debug UART and the retarget-io
 *                    middleware so that printf() output reaches the serial
 *                    terminal. The LOG_* macros in log.h delegate to printf()
 *                    after this initialization.
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

#include "log.h"

#include "cybsp.h"
#include "mtb_hal.h"
#include "cy_retarget_io.h"
#include "mtb_syspm_callbacks.h"

/******************************************************************************
 * Macros (internal)
 *****************************************************************************/

/* RTS pin: not used — pass NULL/0 to the deepsleep callback helper. */
#define LOG_UART_RTS_PORT       (NULL)
#define LOG_UART_RTS_PIN        (0U)

/* SysPm callback configuration. */
#define LOG_SYSPM_SKIP_MODE     (0U)
#define LOG_SYSPM_CB_ORDER      (1U)

/******************************************************************************
 * Static Variables
 *****************************************************************************/

/* SCB UART driver context and HAL object for the debug UART. */
static cy_stc_scb_uart_context_t log_uart_context;
static mtb_hal_uart_t            log_uart_hal_obj;

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)

/* Context reference for the retarget-io deep-sleep UART callback. */
static mtb_syspm_uart_deepsleep_context_t log_syspm_ds_context =
{
    .uart_context   = &log_uart_context,
    .async_context  = NULL,
    .tx_pin =
    {
        .port    = CYBSP_DEBUG_UART_TX_PORT,
        .pinNum  = CYBSP_DEBUG_UART_TX_PIN,
        .hsiom   = CYBSP_DEBUG_UART_TX_HSIOM
    },
    .rts_pin =
    {
        .port    = LOG_UART_RTS_PORT,
        .pinNum  = LOG_UART_RTS_PIN,
        .hsiom   = HSIOM_SEL_GPIO
    }
};

/* SysPm callback parameter and descriptor for the debug UART. */
static cy_stc_syspm_callback_params_t log_syspm_cb_params =
{
    .context = &log_syspm_ds_context,
    .base    = DEBUG_UART_HW
};

static cy_stc_syspm_callback_t log_syspm_cb =
{
    .callback       = &mtb_syspm_scb_uart_deepsleep_callback,
    .skipMode       = LOG_SYSPM_SKIP_MODE,
    .type           = CY_SYSPM_DEEPSLEEP,
    .callbackParams = &log_syspm_cb_params,
    .prevItm        = NULL,
    .nextItm        = NULL,
    .order          = LOG_SYSPM_CB_ORDER
};

#endif /* (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP) */

/******************************************************************************
 * Local Functions
 *****************************************************************************/

/*******************************************************************************
 * Function Name: log_handle_error
 ********************************************************************************
 * Summary:
 *  Called when a hardware initialization step inside log_init() fails.
 *  Disables interrupts and asserts, then loops forever so that a debugger can
 *  catch the failure.
 *
 *******************************************************************************/
static void log_handle_error(void)
{
    __disable_irq();
    CY_ASSERT(0);
    while (true) { }
}

/******************************************************************************
 * Public Functions
 *****************************************************************************/

/*******************************************************************************
 * Function Name: log_init
 ********************************************************************************
 * Summary:
 *  Initializes the logging facility. Configures the debug UART SCB, sets up
 *  the HAL object, initializes the retarget-io middleware (routing printf()
 *  to the serial terminal), and — when the project is built with deep-sleep
 *  idle mode — registers a SysPm callback to keep the UART functional across
 *  deep-sleep transitions.
 *
 *  Must be called once after cybsp_init() / __enable_irq() and before the
 *  first LOG_* macro use. Halts (via log_handle_error) on any failure.
 *
 *******************************************************************************/
void log_init(void)
{
    cy_rslt_t result;

    /* Initialize the SCB UART peripheral. */
    result = (cy_rslt_t)Cy_SCB_UART_Init(DEBUG_UART_HW,
                                          &DEBUG_UART_config,
                                          &log_uart_context);
    if (CY_RSLT_SUCCESS != result)
    {
        log_handle_error();
    }

    /* Enable the SCB UART. */
    Cy_SCB_UART_Enable(DEBUG_UART_HW);

    /* Set up the HAL UART object that retarget-io requires. */
    result = mtb_hal_uart_setup(&log_uart_hal_obj,
                                &DEBUG_UART_hal_config,
                                &log_uart_context,
                                NULL);
    if (CY_RSLT_SUCCESS != result)
    {
        log_handle_error();
    }

    /* Wire printf() to the debug UART via retarget-io. */
    result = cy_retarget_io_init(&log_uart_hal_obj);
    if (CY_RSLT_SUCCESS != result)
    {
        log_handle_error();
    }

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
    /* Register the UART deep-sleep callback so the peripheral is properly
     * suspended and resumed across deep-sleep transitions. */
    Cy_SysPm_RegisterCallback(&log_syspm_cb);
#endif /* (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP) */
}

/* [] END OF FILE */
