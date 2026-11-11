/*****************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for secure
*                    application in the CM33 CPU
*
* Related Document : See README.md
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
#include <arm_cmse.h>
#include "cy_pdl.h"
#include "partition_ARMCM33.h"
#include "cy_mpc.h"
#include "cy_ppc.h"
#include "cy_ms_ctl.h"
#include "pas.h"
#include "prov_svc.h"
#if defined(DEVICE_PSC3M5)
#include "ppc_config.h"
#endif

/******************************************************************************
 * Macros
 *****************************************************************************/

/* CM33 NS image boot base: stack pointer at +0, reset handler at +4. PSC3M5
 * derives it from the hand-written partition (NS_BOOT_START); PSC3M6/PSC3M8 use
 * the Device-Configurator-generated non-secure NVM C-BUS alias token. */
#if defined(DEVICE_PSC3M5)
#define CM33_NS_APP_BOOT_ADDR           NS_BOOT_START
#else
#define CM33_NS_APP_BOOT_ADDR           CYMEM_CM33_0_S_m33ns_nvm_C_START
#endif

#define CM33_NS_STACK_POINTER   ((uint32_t)(*((uint32_t*)CM33_NS_APP_BOOT_ADDR)))
#define CM33_NS_RESET_HANDLER   ((funcptr_void)(*((uint32_t*)(CM33_NS_APP_BOOT_ADDR + 4))))


/* Function pointer for NS function i.e. NonSecure_ResetHandler() */
typedef void (*funcptr_void) (void) __attribute__((cmse_nonsecure_call));

/******************************************************************************
 * Global Variables
 *****************************************************************************/


/******************************************************************************
 * Function Prototypes
 *****************************************************************************/

/******************************************************************************
 * Function Definitions
 *****************************************************************************/

/**
 * @brief Main function of M33 Secure project
 *
 * This is the secure main function for CPU. It ...
 *    1. Configures protection units
 *    2. Launches non-secure application
 *
 * @return Should not return
 *
 */
int main(void)
{
    cy_rslt_t result = 0xFFFFFFFF;
#if defined(DEVICE_PSC3M5)
    cy_en_ppc_status_t ppc_status = CY_PPC_FAILURE;
#endif
    pas_status_t pas_status;

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
    {
       CY_ASSERT(0);
    }

#if defined(DEVICE_PSC3M5)
    /* PSC3M5: SAU + PPC are hand-configured here because this device has no
     * Device-Configurator-generated protection setup. CryptoLite, TRNG and the
     * flash controller are kept secure by config_ppc(). On PSC3M6 (and later
     * devices) the Device Configurator (design.modus) generates SAU + MPC + PPC
     * and applies them inside cybsp_init(), so this manual bring-up is compiled
     * out to avoid double-configuring the protection units. */

    /* Configure SAU for SRAM, Flash and Peri MMIO */
    TZ_SAU_Setup();

    /* Configure PPC */
    ppc_status = config_ppc();
    if (CY_PPC_SUCCESS != ppc_status)
    {
       CY_ASSERT(0);
    }
#else
    /* Non-secure interrupts set-up.
     * There are no secure interrupts in this example,
     * hence marking all interrupts as non-secure */
    NVIC_NS_Setup();
#endif /* DEVICE_PSC3M5 */

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize the Provisioning Asset Store.  Must run before launching the
     * non-secure world so that provisioning commands work on first contact. */
    pas_status = pas_init();
    if (PAS_OK != pas_status)
    {
        CY_ASSERT(0);
    }

    /* Initialize the provisioning service (PSA crypto + service state).  Must
     * run before the non-secure world can issue any provisioning command. */
    if (PROV_OK != prov_svc_init())
    {
        CY_ASSERT(0);
    }

    /* Set the non-secure main stack pointer */
    __TZ_set_MSP_NS(CM33_NS_STACK_POINTER);

    /* Start non-secure application */
    CM33_NS_RESET_HANDLER();

    /* Program should not reach here */
    for (;;)
    {

    }
}
/* [] END OF FILE */

