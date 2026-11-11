#ifndef _PARTITION_PSC3_H_
#define _PARTITION_PSC3_H_

#include "cy_device_headers.h"

/*******************************************************************************
*            Memory layout definition
*******************************************************************************/
#define SRAM0_NS_SAHB_START         (CY_SRAM_NS_SBUS_BASE)
#define SRAM0_S_SAHB_START          (CY_SRAM_S_SBUS_BASE)
#define SRAM0_NS_CBUS_START         (CY_SRAM_NS_CBUS_BASE)
#define SRAM0_S_CBUS_START          (CY_SRAM_S_CBUS_BASE)
#define SRAM0_SIZE                  ((CPUSS_CHIP_TOP_RAMC0_SIZE + (CPUSS_RAMC1_PRESENT ? CPUSS_CHIP_TOP_RAMC1_SIZE : 0U) +\
                                    (CPUSS_RAMC2_PRESENT ? CPUSS_CHIP_TOP_RAMC2_SIZE : 0U)) * 1024U)
#define MMIO_NS_START               0x42000000U
#define MMIO_S_START                0x52000000U
#define MMIO_SIZE                   0x02000000U
#define FLASH_NS_SAHB_START         (CY_FLASH_NS_SBUS_BASE)
#define FLASH_S_SAHB_START          (CY_FLASH_S_SBUS_BASE)
#define FLASH_NS_CBUS_START         (CY_FLASH_NS_CBUS_BASE)
#define FLASH_S_CBUS_START          (CY_FLASH_S_CBUS_BASE)
#define FLASH_SIZE                  (CY_FLASH_SIZE)


/*******************************************************************************
*            The memory sectors size. Need to be alighted with Linker scripts
*******************************************************************************/
/* The size of secure flash also includes the FLASH_NSC_SIZE */
#if defined(USER_FLASH_S_SIZE)
    #define FLASH_S_SIZE                (USER_FLASH_S_SIZE)
#else
    #define FLASH_S_SIZE                (FLASH_SIZE / 4U)
#endif

#if defined(USER_FLASH_NSC_SIZE)
    #define FLASH_NSC_SIZE              (USER_FLASH_NSC_SIZE)
#else
    #define FLASH_NSC_SIZE              0x00000800U
#endif

#define FLASH_NS_OFFSET             (FLASH_S_SIZE)
#define FLASH_NS_SIZE               (FLASH_SIZE - FLASH_S_SIZE)

/* For RAM application the size covers both code and data regions */
#if defined(USER_SRAM_S_SIZE)
    #define SRAM0_S_SIZE                (USER_SRAM_S_SIZE)
#else
    #define SRAM0_S_SIZE                (CY_SRAM_SIZE / 4U)
#endif

#if defined(USER_SRAM0_NSC_SIZE)
    #define SRAM0_NSC_SIZE              USER_SRAM0_NSC_SIZE
#else
    #if (CY_PDL_FLASH_BOOT)
        #define SRAM0_NSC_SIZE              0x00000000U
    #else
        #define SRAM0_NSC_SIZE              0x00000800U
    #endif
#endif

/* The shared memory is located in the end of SRAM region for both application */
#if defined(USER_SRAM0_SHM_S_SIZE)
    #define SRAM0_SHM_S_SIZE            (USER_SRAM0_SHM_S_SIZE)
#else
    #define SRAM0_SHM_S_SIZE            0x00000800U
#endif

#if defined(USER_SRAM0_SHM_NS_SIZE)
    #define SRAM0_SHM_NS_SIZE           (USER_SRAM0_SHM_NS_SIZE)
#else
    #define SRAM0_SHM_NS_SIZE           0x00000800U
#endif

#define SRAM0_SHM_SIZE              (SRAM0_SHM_S_SIZE + SRAM0_SHM_NS_SIZE)

#define SRAM0_NS_SIZE               (SRAM0_SIZE - SRAM0_S_SIZE - SRAM0_SHM_SIZE - SRAM0_NSC_SIZE)
#define SRAM0_NS_CBUS_CODE_START    (SRAM0_NS_CBUS_START + SRAM0_S_SIZE)
#define FLASH_NS_CBUS_CODE_START    (FLASH_NS_CBUS_START + FLASH_S_SIZE)

#define SRAM0_NS_OFFSET             (SRAM0_S_SIZE)
#define SRAM0_SHM_OFFSET            (SRAM0_S_SIZE + SRAM0_NS_SIZE)
#define SRAM0_NSC_OFFSET            (SRAM0_S_SIZE + SRAM0_NS_SIZE + SRAM0_SHM_SIZE)


#define SRAM0_S_SAHB_SHARED_START   (SRAM0_S_SAHB_START + SRAM0_SIZE - SRAM0_SHM_S_SIZE - SRAM0_NSC_SIZE)
#define SRAM0_S_CBUS_SHARED_START   (SRAM0_S_CBUS_START + SRAM0_SIZE - SRAM0_SHM_S_SIZE - SRAM0_NSC_SIZE)
#define SRAM0_NS_SAHB_SHARED_START  (SRAM0_NS_SAHB_START + SRAM0_SIZE - SRAM0_SHM_SIZE - SRAM0_NSC_SIZE)
#define SRAM0_NS_CBUS_SHARED_START  (SRAM0_NS_CBUS_START + SRAM0_SIZE - SRAM0_SHM_SIZE - SRAM0_NSC_SIZE)

/*******************************************************************************
*            SAU regions
*******************************************************************************/
#define SRAM0_NS_SBUS_SAU_START     (SRAM0_NS_SAHB_START + SRAM0_S_SIZE)
#define SRAM0_NS_SBUS_SAU_END       (SRAM0_NS_SAHB_START + SRAM0_SIZE - SRAM0_NSC_SIZE)
#define SRAM0_NS_CBUS_SAU_START     (SRAM0_NS_CBUS_START + SRAM0_S_SIZE)
#define SRAM0_NS_CBUS_SAU_END       (SRAM0_NS_CBUS_START + SRAM0_SIZE - SRAM0_NSC_SIZE)
#define MMIO_NS_SAU_START           (MMIO_NS_START)
#define MMIO_NS_SAU_END             (MMIO_NS_START + MMIO_SIZE)
#define FLASH_NS_SBUS_SAU_START     (FLASH_NS_SAHB_START + FLASH_S_SIZE)
#define FLASH_NS_SBUS_SAU_END       (FLASH_NS_SAHB_START + FLASH_SIZE)
#define FLASH_NS_CBUS_SAU_START     (FLASH_NS_CBUS_START + FLASH_S_SIZE)
#define FLASH_NS_CBUS_SAU_END       (FLASH_NS_CBUS_START + FLASH_SIZE)
#if (CY_PDL_FLASH_BOOT)
#define NS_BOOT_START               (FLASH_NS_CBUS_CODE_START)
#define NSC_SAU_START               (FLASH_S_CBUS_START + FLASH_S_SIZE - FLASH_NSC_SIZE)
#define NSC_SAU_END                 (FLASH_S_CBUS_START + FLASH_S_SIZE)
#else
#define NS_BOOT_START               (SRAM0_NS_CBUS_CODE_START)
#define NSC_SAU_START               (SRAM0_S_CBUS_START + SRAM0_SIZE - SRAM0_NSC_SIZE)
#define NSC_SAU_END                 (SRAM0_S_CBUS_START + SRAM0_SIZE)
#endif

#endif /* _PARTITION_PSC3_H_ */

