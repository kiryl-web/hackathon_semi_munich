/*****************************************************************************
 * File Name        : prov_selftest.h
 *
 * Description      : Build-time self-test for the secure provisioning services.
 *                    When the application is built with -DPROV_SELFTEST, the
 *                    non-secure app calls prov_selftest_run() at start-up to
 *                    exercise every Non-Secure-Callable (NSC) provisioning
 *                    veneer directly (without the DFU/I2C transport) and logs a
 *                    PASS/FAIL summary.
 *
 *                    The whole implementation compiles to nothing when
 *                    PROV_SELFTEST is not defined, so it costs neither code nor
 *                    cycles in a production build.
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

#ifndef PROV_SELFTEST_H
#define PROV_SELFTEST_H

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Run the provisioning service self-test.
 *
 * Exercises each secure provisioning NSC service in turn (status, info, erase,
 * gen-key, read-pubkey, build/read CSR, cert commit, read cert, prove cert and
 * — only when PROV_SELFTEST_FINALIZE is also defined — finalize), logging a
 * PASS/FAIL line per check and a final summary.
 *
 * @return Number of failed checks (0 on full success). Always 0 when the test
 *         is compiled out (PROV_SELFTEST not defined).
 */
unsigned int prov_selftest_run(void);

#if defined(__cplusplus)
}
#endif

#endif /* PROV_SELFTEST_H */

/* [] END OF FILE */
