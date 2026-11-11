/********************************************************************************
* (c) 2021-2026, Infineon Technologies AG, or an affiliate of Infineon
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

#ifndef CLEANUP_H
#define CLEANUP_H

#include <stdint.h>

#include "cy_mpc.h"
#include "cy_ppc.h"
#include "cy_ms_ctl.h"
#include "platform_defines.h"

#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define STACKLESS __STATIC_INLINE
#elif defined(__ICCARM__)
#define STACKLESS __stackless __STATIC_INLINE
#elif defined(__GNUC__)
#define STACKLESS __STATIC_INLINE
#endif

/* IAR compiler doesn't support __NO_RETURN in typedef, so removed it.
 * The reset handler is a function pointer that doesn't return by nature. */
typedef void (*reset_handler_t)(void);

typedef struct vect_tbl_start_s {
    uint32_t        stack_pointer;
    reset_handler_t reset_handler;
} vect_tbl_start_t;

/*******************************************************************************
 * Function Name: cleanup_helper
 ********************************************************************************
 * Summary:
 * Cleans ram region
 * This function used inside cleanup_and_boot function
 *
 * Parameters:
 *  l - region start pointer(lower address)
 *  r - region end pointer (higher address)
 *
 * Note:
 *   This function is critical to be "stackless".
 *   Two oncoming indices algorithm is used to prevent compiler optimization
 *     from calling memset function.
 *
 *******************************************************************************/
STACKLESS
void cleanup_helper(register uint8_t *l, register uint8_t *r)
{
    register uint8_t v = 0u;

    do {
        *l = v;
        ++l;

        --r;
        *r = v;
    } while (l < r);
}

/*******************************************************************************
 * Function Name: cleanup_and_boot
 ********************************************************************************
 * Summary:
 * This function cleans all ram and boots target app
 *
 * Parameters:
 * p_vect_tbl_start - target app vector table address
 *
 *
 *******************************************************************************/
STACKLESS __NO_RETURN
void cleanup_and_boot(register vect_tbl_start_t* p_vect_tbl_start)
{
    /* Init next app vector table */
    MXCM33->CM33_NS_VECTOR_TABLE_BASE = (uint32_t)(void*)p_vect_tbl_start;
    SCB->VTOR = (uint32_t)(void*)p_vect_tbl_start;

    __DSB();

    /* Disable stack limit control */
    __set_MSPLIM(0);

    /* Init next app stack pointer */
    __set_MSP(p_vect_tbl_start->stack_pointer);

    /* Cleanup ram */
    cleanup_helper((uint8_t*)RAM_ORIGIN, (uint8_t*)(RAM_ORIGIN + RAM_SIZE));

#ifdef USE_PROT_CONTEXT_SWITCH
    Cy_Ms_Ctl_SetSavedPC(IFX_BLDR_M33_CPU_BUS_MASTER_ID, 3);
    Cy_Ms_Ctl_SetActivePC(IFX_BLDR_M33_CPU_BUS_MASTER_ID, 3);
#endif

    /* Jump to next app - this function never returns */
    p_vect_tbl_start->reset_handler();

    /* Inform compiler that this point is unreachable (suppresses warnings for all compilers) */
#if defined(__GNUC__)
    __builtin_unreachable();
#elif defined(__ICCARM__)
    /* IAR doesn't need this, but add infinite loop as fallback */
    while(1);
#elif defined(__ARMCC_VERSION)
    __builtin_unreachable();
#else
    while(1);
#endif
}

#endif /* CLEANUP_H */
