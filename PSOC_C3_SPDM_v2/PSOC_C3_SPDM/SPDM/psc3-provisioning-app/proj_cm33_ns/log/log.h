/*****************************************************************************
 * File Name        : log.h
 *
 * Description      : Lightweight compile-time-leveled logging facility.
 *                    Output is routed through the retarget-io middleware
 *                    (debug UART) via printf().
 *
 *                    Call log_init() once at startup before using any LOG_*
 *                    macro. log_init() configures the debug UART and
 *                    initializes the retarget-io middleware.
 *
 *                    Verbosity is controlled at compile time by the LOG_LEVEL
 *                    macro (set in the project Makefile DEFINES). Log
 *                    statements above the configured level compile out to
 *                    nothing, so disabled logs cost neither code nor cycles.
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

#ifndef LOG_H
#define LOG_H

#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif

/******************************************************************************
 * Log levels
 *****************************************************************************/

/** Logging disabled entirely. */
#define LOG_LEVEL_OFF    (0)
/** Unrecoverable / error conditions only. */
#define LOG_LEVEL_ERR    (1)
/** Errors and warnings (recoverable / unexpected conditions). */
#define LOG_LEVEL_WARN   (2)
/** Errors, warnings and high-level informational progress messages. */
#define LOG_LEVEL_INFO   (3)
/** Everything, including verbose debug traces. */
#define LOG_LEVEL_DEBUG  (4)

/*
 * Active compile-time log level. Override from the Makefile, e.g.:
 *     DEFINES+=LOG_LEVEL=LOG_LEVEL_DEBUG
 * Defaults to INFO when not set by the build system.
 */
#ifndef LOG_LEVEL
#define LOG_LEVEL  LOG_LEVEL_INFO
#endif

/******************************************************************************
 * Logging macros
 *****************************************************************************/

/* Internal helper: emit a tagged line terminated with CR/LF for serial
 * terminals. Uses the GNU ##__VA_ARGS__ extension (supported by GCC_ARM /
 * ARM Clang) so the format string can stand alone with zero extra arguments. */
#define LOG_PRINT(tag, fmt, ...) \
    (void)printf("[" tag "] " fmt "\r\n", ##__VA_ARGS__)

#if (LOG_LEVEL >= LOG_LEVEL_ERR)
#define LOG_ERR(fmt, ...)   LOG_PRINT("E", fmt, ##__VA_ARGS__)
#else
#define LOG_ERR(fmt, ...)   do { } while (0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_WARN)
#define LOG_WARN(fmt, ...)  LOG_PRINT("W", fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...)  do { } while (0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_INFO)
#define LOG_INFO(fmt, ...)  LOG_PRINT("I", fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)  do { } while (0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_DEBUG)
#define LOG_DBG(fmt, ...)   LOG_PRINT("D", fmt, ##__VA_ARGS__)
#else
#define LOG_DBG(fmt, ...)   do { } while (0)
#endif

/******************************************************************************
 * Public API
 *****************************************************************************/

/**
 * @brief Initialize the logging facility.
 *
 * Configures the debug UART (SCB) and initializes the retarget-io middleware
 * so that printf() output is routed to the serial terminal. Also registers
 * the UART SysPm deep-sleep callback when the project is built with
 * CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP.
 *
 * Must be called once after cybsp_init() / __enable_irq() and before the
 * first LOG_* macro use.
 *
 * Halts execution (CY_ASSERT) if any hardware initialization step fails.
 */
void log_init(void);

#if defined(__cplusplus)
}
#endif

#endif /* LOG_H */

/* [] END OF FILE */
