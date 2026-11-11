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

#include <inttypes.h>

#include "bootutil/bootutil.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/image.h"
#include "bootutil/ramload.h"
#include "cleanup.h"
#include "cy_pdl.h"
#include "cybsp.h"
#include "crypto.h"

#include "platform_init.h"
#include "memorymap.h"

#include "cycfg_peripherals.h"
#include "cy_retarget_io.h"

/* WDT time out for reset mode, in milliseconds. Max limit is given by
 * CYHAL_WDT_MAX_TIMEOUT_MS */
#define WDT_TIME_OUT_MS                     4000
#define ENABLE_BLOCKING_FUNCTION            1

/* Match count =  Desired interrupt interval in seconds x ILO Frequency in Hz */
#define WDT_MATCH_COUNT                     (WDT_TIME_OUT_MS*32000)/1000

#define BOOT_MSG_FINISH              \
    "MCUBoot Bootloader finished.\n\n" \
    "[INF] Deinitializing hardware...\n"

#include "timestamp.h"

/*Specific for the core and the silicon vendor*/
#define VECTOR_TABLE_ALIGNMENT (0x400U)

#if MCUBOOT_LOG_LEVEL != MCUBOOT_LOG_LEVEL_OFF
static cy_stc_scb_uart_context_t    DEBUG_UART_context;           /** UART context */
static mtb_hal_uart_t               DEBUG_UART_hal_obj;           /** Debug UART HAL object  */
#endif

void handle_error(void);

/**
 ******************************************************************************
 * Function Name: initialize_wdt
 ******************************************************************************
 * Summary:
 * Configure the WDT instance
 *
 *
 *****************************************************************************/
void initialize_wdt()
{
   /* Step 1- Unlock WDT */
   Cy_WDT_Unlock();

   /* Step 2- Write the ignore bits - operate with only 14 bits */
   Cy_WDT_SetIgnoreBits(16);

   /* Step 3- Write match value */
   Cy_WDT_SetMatch(WDT_MATCH_COUNT);

   /* Step 4- Clear match event interrupt, if any */
   Cy_WDT_ClearInterrupt();

   /* Step 5- Enable WDT */
   Cy_WDT_Enable();

   /* Step 6- Lock WDT configuration */
   Cy_WDT_Lock();
}

/**
 ******************************************************************************
 * Function Name: cbus_remap_addr
 ******************************************************************************
 * Summary:
 * Remap address for masters on CBUS
 *
 * Parameters:
 *   addr  address to be remap
 *
 * Return:
 * uintptr_t - remaped address
 *
 *****************************************************************************/
static inline uintptr_t cbus_remap_addr(const uintptr_t addr)
{
    uintptr_t remap_addr = addr;

    return remap_addr;
}


/******************************************************************************
 * Function Name: calc_app_addr
 ******************************************************************************
 * Summary:
 * Calculate start address of user application.
 *
 * Parameters:
 *  image_base - base address of flash;
 *
 *  rsp - provided by the boot loader code; indicates where to jump
 *          to execute the main image;
 *
 *  output - calculated address of application;
 *
 * Return:
 * fih_int
 *
 *****************************************************************************/
static inline __attribute__((always_inline)) fih_int calc_app_addr(
    uintptr_t image_base, const struct boot_rsp *rsp, fih_uint *app_address)
{
    fih_int fih_rc = FIH_FAILURE;

#if defined(MCUBOOT_RAM_LOAD)
    if (IS_RAM_BOOTABLE(rsp->br_hdr) == true) {
        if ((UINT32_MAX - rsp->br_hdr->ih_hdr_size) >= image_base) {
            *app_address =
                fih_uint_encode(image_base + rsp->br_hdr->ih_hdr_size);
            fih_rc = FIH_SUCCESS;
        }
    } else
#endif
    {
        if (((UINT32_MAX - rsp->br_image_off) >= image_base) &&
            ((UINT32_MAX - rsp->br_hdr->ih_hdr_size) >=
             (image_base + rsp->br_image_off))) {
            *app_address = fih_uint_encode(image_base + rsp->br_image_off +
                                           rsp->br_hdr->ih_hdr_size);
            fih_rc = FIH_SUCCESS;
        }
    }

    FIH_RET(fih_rc);
}

/******************************************************************************
 * Function Name: hw_init
 ******************************************************************************
 * Summary:
 * Initialize used hardware.
 *
 * Return:
 *  cy_rslt_t
 *
 *****************************************************************************/
static cy_rslt_t hw_init(void)
{
    return cybsp_init();
}

#if MCUBOOT_LOG_LEVEL != MCUBOOT_LOG_LEVEL_OFF
/******************************************************************************
 * Function Name: debug_log_init
 ******************************************************************************
 * Summary:
 * Initialize debug logging.
 *
 * Return:
 *  cy_rslt_t
 *
 ******************************************************************************/
static cy_rslt_t debug_log_init(void)
{
    /* Debug UART init */
    cy_rslt_t result;

    /* Debug UART init */
    result = (cy_rslt_t)Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config, &DEBUG_UART_context);

    /* UART init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    Cy_SCB_UART_Enable(DEBUG_UART_HW);

    /* Setup the HAL UART */
    result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, &DEBUG_UART_hal_config, &DEBUG_UART_context, NULL);

    /* HAL UART init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    result = cy_retarget_io_init(&DEBUG_UART_hal_obj);

    return result;
}

/******************************************************************************
 * Function Name: debug_log_deinit
 ******************************************************************************
 * Summary:
 * Deinitialize debug logging.
 *
 *****************************************************************************/
static void debug_log_deinit(void)
{
    /* Deinitialize retarget-io */
    cy_retarget_io_deinit();
}
#endif

/******************************************************************************
 * Function Name: hw_deinit
 ******************************************************************************
 * Summary:
 * Deinitialize hardware before launch user application.
 *
 *****************************************************************************/
static void hw_deinit(void)
{
#if MCUBOOT_LOG_LEVEL != MCUBOOT_LOG_LEVEL_OFF
    debug_log_deinit();
    log_timestamp_deinit();
#endif

    __disable_irq();
}

/******************************************************************************
 * Function Name: run_next_app
 ******************************************************************************
 * Summary:
 * This function runs next app.
 *
 * Parameters:
 *  rsp - provided by the boot loader code; indicates where to jump
 *       to execute the main image
 *
 * Return:
 *  true - on success
 *
 *****************************************************************************/
static bool run_next_app(struct boot_rsp *rsp)
{
    uintptr_t image_base;
    int rc = 0;

    if (rsp != NULL) {
#if defined(MCUBOOT_RAM_LOAD)
#if !defined(MULTIPLE_EXECUTABLE_RAM_REGIONS)
        image_base = IMAGE_EXECUTABLE_RAM_START;
#else
        if (IS_RAM_BOOTABLE(rsp->br_hdr)) {
            BOOT_LOG_DBG(" > %s: application IS_RAM_BOOTABLE", __func__);
            rc = boot_get_image_exec_ram_info(0, (uint32_t *)&image_base,
                                            &(uint32_t){0});

            BOOT_LOG_DBG(" > %s: image_base = %d ", __func__, (uint32_t)image_base);
        } else
#endif
#endif

#if !defined(MCUBOOT_RAM_LOAD) || defined(MCUBOOT_MULTI_MEMORY_LOAD)
        {
            BOOT_LOG_DBG(" > %s: !MCUBOOT_RAM_LOAD || MCUBOOT_MULTI_MEMORY_LOAD", __func__);
            rc = flash_device_base(rsp->br_flash_dev_id, &image_base);
            BOOT_LOG_DBG(" > %s: rc = %d, image_base = 0x%X ", __func__, rc, (uint32_t)image_base);
        }
#endif

        if (0 == rc) {
            fih_int fih_rc = FIH_FAILURE;
            fih_uint app_addr = FIH_UINT_INIT(0U);

            FIH_CALL(calc_app_addr, fih_rc, image_base, rsp, &app_addr);
            if (!fih_eq(fih_rc, FIH_SUCCESS)) {
                BOOT_LOG_ERR(" > %s: calc_app_addr returned false", __func__);
                return false;
            }

            /* Check Vector Table alignment */
            const uint32_t mask = (uint32_t)VECTOR_TABLE_ALIGNMENT - 1U;

            if (!fih_uint_eq(fih_uint_and(app_addr, fih_uint_encode(mask)), FIH_UINT_ZERO)) {
                BOOT_LOG_ERR(" > %s: Invalid Vector Table alignment", __func__);
                return false;
            }

            vect_tbl_start_t *p_vect_tbl_start =
                (vect_tbl_start_t *)fih_uint_decode(app_addr);

            if (0u != (p_vect_tbl_start->stack_pointer &
                       7U) || /* Check stack alignment */
                1u != ((uintptr_t)p_vect_tbl_start->reset_handler &
                       1U)) /* Check Thumb entry point */
            {
                BOOT_LOG_ERR(" > %s: misaligned stack or invalid entry point",
                             __func__);
                return false;
            }

            BOOT_LOG_INF("Starting User Application (wait)...");

            if (IS_ENCRYPTED(rsp->br_hdr)) {
                BOOT_LOG_DBG(" * User application is encrypted");
            }

            BOOT_LOG_INF("Start slot Address: 0x%X", (uint32_t)fih_uint_decode(app_addr));

            rc = flash_device_base(rsp->br_flash_dev_id, &image_base);

            if (rc != 0) {
                return false;
            }

            BOOT_LOG_INF(BOOT_MSG_FINISH);
            hw_deinit();

            /* Start user app */
            cleanup_and_boot(p_vect_tbl_start);
        } else {
            BOOT_LOG_ERR("Flash device ID not found");
            return false;
        }
    }

    return false;
}

/******************************************************************************
 * Function Name: main
 ******************************************************************************
 * Summary:
 * This is the main function of bootloader.
 *
 *****************************************************************************/
int main(void)
{
    struct boot_rsp rsp[BOOT_IMAGE_NUMBER] = {0};
    cy_rslt_t rc = CY_RSLT_SUCCESS;
    fih_int fih_rc = FIH_FAILURE;

    /* Initialize the device and board peripherals */
    rc = hw_init();
    if (rc != CY_RSLT_SUCCESS) {
        handle_error();
    }

    /* Initialize platform specific modules */
    rc = platform_init();
    if (rc != CY_RSLT_SUCCESS) {
        handle_error();
    }

    /* enable interrupts */
    __enable_irq();

    /* Initialize debug log hardware */
#if MCUBOOT_LOG_LEVEL != MCUBOOT_LOG_LEVEL_OFF
    /* Initialize the timebase for output logs */
    log_timestamp_init();

    rc = debug_log_init();
    if (rc != CY_RSLT_SUCCESS) {
        handle_error();
    }
#endif

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen. */
    BOOT_LOG_DBG("\x1b[2J\x1b[;H");
    BOOT_LOG_INF("MCUBoot Bootloader Started");

#if !defined(MCUBOOT_RAM_LOAD) || defined(MCUBOOT_MULTI_MEMORY_LOAD)

    BOOT_LOG_INF("boot_go_for_image_id");
    /* Perform MCUboot */
    for (uint32_t id = 0U; id < (uint32_t)BOOT_IMAGE_NUMBER; id++) {
        BOOT_LOG_INF("Processing img id: %d", id);
        FIH_CALL(boot_go_for_image_id, fih_rc, &rsp[id], id);

        if (!fih_eq(fih_rc, FIH_SUCCESS) || rsp[id].br_hdr == NULL) {
            handle_error();
        }
    }
#endif

#if defined(MCUBOOT_RAM_LOAD)
    for (uint32_t id = 0U; id < (uint32_t)BOOT_IMAGE_NUMBER; id++) {
        if (IS_RAM_BOOTABLE(rsp[id].br_hdr) == true) {
            BOOT_LOG_INF("boot_go_for_image_id_ram");
            FIH_CALL(boot_go_for_image_id_ram, fih_rc, &rsp[id], id);

            if (!fih_eq(fih_rc, FIH_SUCCESS)) {
                handle_error();
            }
        }
    }
#endif

    if (fih_eq(fih_rc, FIH_SUCCESS)) {
        BOOT_LOG_INF("User Application validated successfully");

        /* initialize watchdog timer. */
#if defined(USE_WDT)
        initialize_wdt();
#endif

        BOOT_LOG_INF("Running the first app");

        if (CY_RSLT_SUCCESS == rc) {
            if (!run_next_app(&rsp[0])) {
                BOOT_LOG_ERR("Running of next app failed!");
                handle_error();
            }
        } else {
            BOOT_LOG_ERR("Failed to init WDT");
        }
    } else {
        BOOT_LOG_ERR("MCUBoot Bootloader found none of bootable images");
    }

    /* Loop forever... */
    while (true) {
        __WFI();
    }
}

/******************************************************************************
 * Function Name: handle_error
 ******************************************************************************
 * Summary:
 * User defined error handling function.
 *
 *****************************************************************************/
void handle_error(void)
{
    BOOT_LOG_ERR("handle_error!");
    /* Loop forever... */
    while (true) {
        __WFI();
    }
}

#if defined(MULTIPLE_EXECUTABLE_RAM_REGIONS)
/*******************************************************************************
 * Function Name: boot_get_image_exec_ram_info
 ********************************************************************************
 * Summary:
 * MCUBoot library port API for ram boot feature
 *
 *******************************************************************************/
int boot_get_image_exec_ram_info(uint32_t image_id, uint32_t *exec_ram_start,
                                 uint32_t *exec_ram_size)
{
    int rc = -1;

    if(image_id < BOOT_IMAGE_NUMBER) {
        *exec_ram_start = image_boot_config[image_id].address;
        *exec_ram_size = image_boot_config[image_id].size;

        return 0;
    }

    return rc;
}
#endif
