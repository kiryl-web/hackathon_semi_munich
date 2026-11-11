/*****************************************************************************
* File Name        : ppc_config.h
*
* Description      : Peripheral Protection Controller (PPC) configuration for
*                    the secure CM33 application.
*
*                    config_ppc() partitions the peripherals into secure and
*                    non-secure worlds at start-up.
*
*                    config_ppc_flash_enable() / config_ppc_flash_disable()
*                    temporarily move the flash controller (PROT_CPUSS) into
*                    the secure world for the duration of a PAS flash
*                    erase/program operation and return it to non-secure
*                    afterwards.
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

#ifndef PPC_CONFIG_H
#define PPC_CONFIG_H

#include "cy_ppc.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Configure PPC.
 *
 * This function configures the peripheral protection controller (PPC) to
 * partition the peripherals in to secure and non-secure worlds. Resources
 * unused by the secure project are configured as non-secure; peripherals the
 * secure app owns at runtime (FLASHC for the PAS, CRYPTOLITE for PSA crypto +
 * TRNG) are kept secure.
 *
 * @return The status cy_en_ppc_status_t.
 */
cy_en_ppc_status_t config_ppc(void);

/**
 * @brief Make the flash controller (PROT_CPUSS) secure-accessible.
 *
 * Call BEFORE a PAS flash erase/program operation. Pair every call with a
 * matching config_ppc_flash_disable() once the operation completes.
 *
 * @return The status cy_en_ppc_status_t.
 */
cy_en_ppc_status_t config_ppc_flash_enable(void);

/**
 * @brief Return the flash controller (PROT_CPUSS) to non-secure.
 *
 * Call AFTER a PAS flash erase/program operation, restoring the partitioning
 * established by config_ppc().
 *
 * @return The status cy_en_ppc_status_t.
 */
cy_en_ppc_status_t config_ppc_flash_disable(void);

#if defined(__cplusplus)
}
#endif

#endif /* PPC_CONFIG_H */

/* [] END OF FILE */
