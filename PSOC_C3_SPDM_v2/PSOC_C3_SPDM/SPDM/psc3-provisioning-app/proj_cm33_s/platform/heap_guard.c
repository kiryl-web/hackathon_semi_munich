/*****************************************************************************
* File Name        : heap_guard.c
*
* Description      : Guarded newlib `_sbrk` for the secure CM33 application.
*
*                    The GNU Arm toolchain links the stock libnosys `_sbrk`,
*                    which extends the program break without checking the
*                    heap region limit. An oversized allocation (for example
*                    ML-DSA-87 key generation, whose wolfCrypt SMALL_MEM path
*                    requests a single ~26 KB working buffer) is then handed
*                    memory past __HeapLimit — over the stack and off the end
*                    of SRAM — and the first write faults into Default_Handler
*                    instead of failing the allocation.
*
*                    This strong override bounds the heap to [__HeapBase,
*                    __HeapLimit) so an over-allocation returns cleanly and the
*                    caller sees a NULL/ENOMEM it can report, rather than
*                    corrupting memory. Pattern per the BSP note in
*                    system_cat1b.h (Heap and Stack Configuration).
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

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

/* Heap bounds provided by the GCC linker script (linker_s_flash.ld):
 * __HeapBase is the first heap byte, __HeapLimit is one past the last. */
extern uint8_t __HeapBase;
extern uint8_t __HeapLimit;

/******************************************************************************
 * Function Definitions
 *****************************************************************************/

/* Bounded program-break handler. Returns the previous break on success, or
 * (void *)-1 with errno = ENOMEM when the request would exceed __HeapLimit. */
void *_sbrk(ptrdiff_t incr)
{
    static uint8_t *heap_end = &__HeapBase;
    uint8_t *prev_end = heap_end;

    if (incr > 0)
    {
        ptrdiff_t avail = &__HeapLimit - heap_end;
        if (incr > avail)
        {
            errno = ENOMEM;
            return (void *)-1;
        }
    }

    heap_end += incr;
    return (void *)prev_end;
}

/* [] END OF FILE */
