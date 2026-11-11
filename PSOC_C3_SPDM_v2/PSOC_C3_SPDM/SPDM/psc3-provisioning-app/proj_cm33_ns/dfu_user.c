/*****************************************************************************
 * File Name        : dfu_user.c
 *
 * Description      : User-defined hook functions required by the DFU
 *                    middleware for the non-secure application: NVM read/write
 *                    operations, address validation, and the I2C transport
 *                    glue. The non-secure app hosts the DFU transport and the
 *                    provisioning command router; it holds no secrets.
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
 *****************************************************************************/

/******************************************************************************
 * Header Files
 *****************************************************************************/

#include <string.h>

#include "cy_syslib.h"
#include "cy_dfu.h"
#include "cy_dfu_logging.h"
#include "cy_flash.h"

#ifdef COMPONENT_DFU_I2C
    #include "transport_i2c.h"
#endif /* COMPONENT_DFU_I2C */

#if !defined(COMPONENT_DFU_I2C)
    #warning "Select the DFU I2C transport (COMPONENTS+=DFU_I2C)."
#endif /* !defined(COMPONENT_DFU_I2C) */

/******************************************************************************
 * Global Variables
 *****************************************************************************/

/* The transport currently selected by Cy_DFU_TransportStart(). */
static cy_en_dfu_transport_t selectedInterface = CY_DFU_I2C;

/******************************************************************************
 * Local Function Prototypes
 *****************************************************************************/

static bool IsMultipleOf(uint32_t value, uint32_t multiple);
static bool AddressValid(uint32_t address);

/*******************************************************************************
 * Function Name: IsMultipleOf
 ********************************************************************************
 * Summary:
 *  Returns true if value is an integer multiple of multiple.
 *
 *******************************************************************************/
static bool IsMultipleOf(uint32_t value, uint32_t multiple)
{
    return ((multiple != 0U) && ((value % multiple) == 0U));
}

/*******************************************************************************
 * Function Name: AddressValid
 ********************************************************************************
 * Summary:
 *  Validates that an address lies within the internal code flash. Provisioning
 *  rides on DFU user-defined commands and does not program flash from the
 *  non-secure side, so only the internal flash range is accepted here.
 *
 *******************************************************************************/
static bool AddressValid(uint32_t address)
{
    return ((CY_FLASH_BASE <= address) && (address < (CY_FLASH_BASE + CY_FLASH_SIZE)));
}

/*******************************************************************************
 * Function Name: Cy_DFU_WriteData
 ********************************************************************************
 * Summary:
 *  DFU SDK NVM write hook. See cy_dfu.h for the full API description.
 *
 *******************************************************************************/
cy_en_dfu_status_t Cy_DFU_WriteData(uint32_t address, uint32_t length, uint32_t ctl,
                                    cy_stc_dfu_params_t *params)
{
    cy_en_dfu_status_t status = CY_DFU_SUCCESS;

    if (!AddressValid(address))
    {
        status = CY_DFU_ERROR_ADDRESS;
    }
    else if ((IsMultipleOf(address, CY_NVM_SIZEOF_ROW) == false) ||
             ((length != CY_NVM_SIZEOF_ROW) && ((ctl & CY_DFU_IOCTL_ERASE) == 0U)))
    {
        status = CY_DFU_ERROR_LENGTH;
    }
    else
    {
        cy_en_flashdrv_status_t fstatus;

        if ((ctl & CY_DFU_IOCTL_ERASE) != 0U)
        {
            (void)memset(params->dataBuffer, 0, CY_NVM_SIZEOF_ROW);
        }

        uint32_t int_status = Cy_SysLib_EnterCriticalSection();
        fstatus = Cy_Flash_WriteRow(address, (const uint32_t *)params->dataBuffer);
        Cy_SysLib_ExitCriticalSection(int_status);

        if (fstatus != CY_FLASH_DRV_SUCCESS)
        {
            status = CY_DFU_ERROR_DATA;
            CY_DFU_LOG_ERR("Flash write failed at 0x%X (0x%X)",
                           (unsigned int)address, (unsigned int)fstatus);
        }
    }

    return status;
}

/*******************************************************************************
 * Function Name: Cy_DFU_ReadData
 ********************************************************************************
 * Summary:
 *  DFU SDK NVM read/compare hook. See cy_dfu.h for the full API description.
 *
 *******************************************************************************/
cy_en_dfu_status_t Cy_DFU_ReadData(uint32_t address, uint32_t length, uint32_t ctl,
                                   cy_stc_dfu_params_t *params)
{
    cy_en_dfu_status_t status = CY_DFU_SUCCESS;

    if (IsMultipleOf(length, CY_NVM_SIZEOF_ROW) == false)
    {
        status = CY_DFU_ERROR_LENGTH;
    }
    else if (!AddressValid(address))
    {
        status = CY_DFU_ERROR_ADDRESS;
    }
    else if ((ctl & CY_DFU_IOCTL_COMPARE) == 0U)
    {
        (void)memcpy(params->dataBuffer, (const void *)address, (size_t)length);
    }
    else
    {
        status = (memcmp((const void *)params->dataBuffer, (const void *)address, length) == 0)
                     ? CY_DFU_SUCCESS
                     : CY_DFU_ERROR_VERIFY;
    }

    return status;
}

/*******************************************************************************
 * Function Name: Cy_DFU_TransportStart
 ********************************************************************************
 * Summary:
 *  Starts the selected DFU transport. See cy_dfu.h for the full API.
 *
 *******************************************************************************/
void Cy_DFU_TransportStart(cy_en_dfu_transport_t transport)
{
    selectedInterface = transport;

    switch (transport)
    {
    #ifdef COMPONENT_DFU_I2C
        case CY_DFU_I2C:
            I2C_I2cCyBtldrCommStart();
            break;
    #endif /* COMPONENT_DFU_I2C */

        default:
            CY_ASSERT(false);
            break;
    }
}

/*******************************************************************************
 * Function Name: Cy_DFU_TransportStop
 ********************************************************************************
 * Summary:
 *  Stops the active DFU transport. See cy_dfu.h for the full API.
 *
 *******************************************************************************/
void Cy_DFU_TransportStop(void)
{
    switch (selectedInterface)
    {
    #ifdef COMPONENT_DFU_I2C
        case CY_DFU_I2C:
            I2C_I2cCyBtldrCommStop();
            break;
    #endif /* COMPONENT_DFU_I2C */

        default:
            CY_ASSERT(false);
            break;
    }
}

/*******************************************************************************
 * Function Name: Cy_DFU_TransportReset
 ********************************************************************************
 * Summary:
 *  Resets the active DFU transport. See cy_dfu.h for the full API.
 *
 *******************************************************************************/
void Cy_DFU_TransportReset(void)
{
    switch (selectedInterface)
    {
    #ifdef COMPONENT_DFU_I2C
        case CY_DFU_I2C:
            I2C_I2cCyBtldrCommReset();
            break;
    #endif /* COMPONENT_DFU_I2C */

        default:
            CY_ASSERT(false);
            break;
    }
}

/*******************************************************************************
 * Function Name: Cy_DFU_TransportRead
 ********************************************************************************
 * Summary:
 *  Reads a packet from the active DFU transport. See cy_dfu.h for the full API.
 *
 *******************************************************************************/
cy_en_dfu_status_t Cy_DFU_TransportRead(uint8_t buffer[], uint32_t size, uint32_t *count,
                                        uint32_t timeout)
{
    cy_en_dfu_status_t status = CY_DFU_ERROR_UNKNOWN;

    switch (selectedInterface)
    {
    #ifdef COMPONENT_DFU_I2C
        case CY_DFU_I2C:
            status = I2C_I2cCyBtldrCommRead(buffer, size, count, timeout);
            break;
    #endif /* COMPONENT_DFU_I2C */

        default:
            CY_ASSERT(false);
            break;
    }

    return status;
}

/*******************************************************************************
 * Function Name: Cy_DFU_TransportWrite
 ********************************************************************************
 * Summary:
 *  Writes a packet to the active DFU transport. See cy_dfu.h for the full API.
 *
 *******************************************************************************/
cy_en_dfu_status_t Cy_DFU_TransportWrite(uint8_t buffer[], uint32_t size, uint32_t *count,
                                         uint32_t timeout)
{
    cy_en_dfu_status_t status = CY_DFU_ERROR_UNKNOWN;

    switch (selectedInterface)
    {
    #ifdef COMPONENT_DFU_I2C
        case CY_DFU_I2C:
            status = I2C_I2cCyBtldrCommWrite(buffer, size, count, timeout);
            break;
    #endif /* COMPONENT_DFU_I2C */

        default:
            CY_ASSERT(false);
            break;
    }

    return status;
}

/* [] END OF FILE */
