/*****************************************************************************
* File Name        : ppc_config.c
*
* Description      : Peripheral Protection Controller (PPC) configuration for
*                    the secure CM33 application.
*
*                    Moved out of main.c so the start-up partitioning and the
*                    flash-window enable/disable helpers used by the PAS live
*                    in one place.
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

#include "cybsp.h"
#include "cy_pdl.h"
#include "cy_ppc.h"

#include "ppc_config.h"

/******************************************************************************
 * Function Definitions
 *****************************************************************************/

#if defined(DEVICE_PSC3M5)

cy_en_ppc_status_t config_ppc(void)
{
    PPC_Type* ppcPtr = NULL;
    cy_en_ppc_status_t ppcStatus = CY_PPC_FAILURE;

    cy_stc_ppc_init_t ppcInit;
    cy_stc_ppc_attribute_t ppcAttribute;
    cy_stc_ppc_pc_mask_t pcMaskConfig;

    /* Configure PPC0 for CM33 access */
    ppcPtr = PPC;

    /* Initialize PPC */
    ppcInit.respConfig = CY_PPC_BUS_ERR;
    ppcStatus = Cy_Ppc_InitPpc(ppcPtr, &ppcInit);
    if (ppcStatus != CY_PPC_SUCCESS)
        return ppcStatus;

    ppcAttribute.secAttribute = CY_PPC_NON_SECURE;
    ppcAttribute.secPrivAttribute = CY_PPC_SEC_NONPRIV;
    ppcAttribute.nsPrivAttribute = CY_PPC_NON_SEC_NONPRIV;

    /* Address: 0x42000000 - 0x42004157 */
    ppcAttribute.startRegion = PROT_PERI0_MAIN;
    ppcAttribute.endRegion = PROT_PERI0_GR5_GROUP;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    /* Address: 0x42040000 - 0x4204FFFF */
    ppcAttribute.startRegion = PROT_PERI_PCLK0_MAIN;
    ppcAttribute.endRegion = PROT_PERI_PCLK0_MAIN;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    /* Address: 0x42100000 - 0x421102FF */
    ppcAttribute.startRegion = PROT_CPUSS;
    ppcAttribute.endRegion = PROT_RAMC0_RAM_PWR;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    /* Address: 0x42160000 - 0x421600FF */
    ppcAttribute.startRegion = PROT_MXCM33_CM33;
    ppcAttribute.endRegion = PROT_MXCM33_CM33;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    /* Address: 0x42168000 - 0x421c2007 */
    ppcAttribute.startRegion = PROT_MXCM33_CM33_INT;
    ppcAttribute.endRegion = PROT_CPUSS_BOOT;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    /* Address: 0x421c5004 - 0x421c51f4 */
    ppcAttribute.startRegion = PROT_MS_PC0_PRIV_MIR;
    ppcAttribute.endRegion = PROT_MS_PC31_PRIV_MIR;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    /* Address: 0x42230200 - 0x4222ff00 */
    ppcAttribute.startRegion = PROT_CPUSS_SL_CTL_GROUP;
    ppcAttribute.endRegion = PROT_BACKUP_BACKUP_SECURE;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    /* Address: 0x42240000 - 0x42BFFFFF */
    ppcAttribute.startRegion = PROT_MXCORDIC_1_0;
    ppcAttribute.endRegion = PROT_MCPASS;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    /* Address: 0x42008000 - 0x4200FFFF */
    ppcAttribute.startRegion = PROT_PERI0_TR;
    ppcAttribute.endRegion = PROT_PERI0_TR;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    /* Set PC Mask */
    pcMaskConfig.startRegion = PROT_PERI0_MAIN;
    pcMaskConfig.endRegion = PROT_MCPASS;
    pcMaskConfig.pcMask = 0xFFFFFFFF;
    ppcStatus = Cy_Ppc_SetPcMask(ppcPtr, &pcMaskConfig);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    return CY_PPC_SUCCESS;
}

cy_en_ppc_status_t config_ppc_flash_enable(void)
{
    PPC_Type* ppcPtr = NULL;
    cy_en_ppc_status_t ppcStatus = CY_PPC_FAILURE;

    cy_stc_ppc_init_t ppcInit;
    cy_stc_ppc_attribute_t ppcAttribute;

    /* Configure PPC0 for CM33 access */
    ppcPtr = PPC;

    /* Initialize PPC */
    ppcInit.respConfig = CY_PPC_BUS_ERR;
    ppcStatus = Cy_Ppc_InitPpc(ppcPtr, &ppcInit);
    if (ppcStatus != CY_PPC_SUCCESS)
        return ppcStatus;

    ppcAttribute.secAttribute = CY_PPC_SECURE;
    ppcAttribute.secPrivAttribute = CY_PPC_SEC_NONPRIV;
    ppcAttribute.nsPrivAttribute = CY_PPC_NON_SEC_NONPRIV;

    /* Address: 0x42100000 - 0x421102FF */
    ppcAttribute.startRegion = PROT_CPUSS;
    ppcAttribute.endRegion = PROT_CPUSS;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    return CY_PPC_SUCCESS;
}

cy_en_ppc_status_t config_ppc_flash_disable(void)
{
    PPC_Type* ppcPtr = NULL;
    cy_en_ppc_status_t ppcStatus = CY_PPC_FAILURE;

    cy_stc_ppc_init_t ppcInit;
    cy_stc_ppc_attribute_t ppcAttribute;

    /* Configure PPC0 for CM33 access */
    ppcPtr = PPC;

    /* Initialize PPC */
    ppcInit.respConfig = CY_PPC_BUS_ERR;
    ppcStatus = Cy_Ppc_InitPpc(ppcPtr, &ppcInit);
    if (ppcStatus != CY_PPC_SUCCESS)
        return ppcStatus;

    ppcAttribute.secAttribute = CY_PPC_NON_SECURE;
    ppcAttribute.secPrivAttribute = CY_PPC_SEC_NONPRIV;
    ppcAttribute.nsPrivAttribute = CY_PPC_NON_SEC_NONPRIV;

    /* Address: 0x42100000 - 0x421102FF */
    ppcAttribute.startRegion = PROT_CPUSS;
    ppcAttribute.endRegion = PROT_CPUSS;
    ppcStatus = Cy_Ppc_ConfigAttrib(ppcPtr, &ppcAttribute);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    return CY_PPC_SUCCESS;
}

#elif defined(DEVICE_PSC3M6) || defined(DEVICE_PSC3M8)

/* On PSC3M6/PSC3M8 the PPC driver takes the region as a call argument and the
 * attribute struct carries the PC mask, so the flash-window helpers cannot
 * share the PSC3M5 bodies above. config_ppc() is not needed on these devices;
 * the Device Configurator / cybsp_init() partitions the peripherals. */

cy_en_ppc_status_t config_ppc_flash_enable(void)
{
    PPC_Type* ppcPtr = PPC;
    cy_en_ppc_status_t ppcStatus;
    cy_stc_ppc_attribute_t ppcAttribute;

    ppcStatus = Cy_Ppc_InitPpc(ppcPtr, CY_PPC_BUS_ERR);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    ppcAttribute.pcMask = 0xFFFFFFFFUL;
    ppcAttribute.secAttribute = CY_PPC_SECURE;
    ppcAttribute.privAttribute = CY_PPC_NONPRIV;

    return Cy_Ppc_ConfigAttrib(ppcPtr, PROT_CPUSS, &ppcAttribute);
}

cy_en_ppc_status_t config_ppc_flash_disable(void)
{
    PPC_Type* ppcPtr = PPC;
    cy_en_ppc_status_t ppcStatus;
    cy_stc_ppc_attribute_t ppcAttribute;

    ppcStatus = Cy_Ppc_InitPpc(ppcPtr, CY_PPC_BUS_ERR);
    if (ppcStatus != CY_PPC_SUCCESS)
    {
        return ppcStatus;
    }

    ppcAttribute.pcMask = 0xFFFFFFFFUL;
    ppcAttribute.secAttribute = CY_PPC_NON_SECURE;
    ppcAttribute.privAttribute = CY_PPC_NONPRIV;

    return Cy_Ppc_ConfigAttrib(ppcPtr, PROT_CPUSS, &ppcAttribute);
}

#endif /* DEVICE_PSC3M5 / DEVICE_PSC3M6 / DEVICE_PSC3M8 */

/* [] END OF FILE */
