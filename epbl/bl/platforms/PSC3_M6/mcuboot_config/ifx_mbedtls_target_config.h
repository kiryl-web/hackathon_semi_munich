/**
 * \file ifx-mbedtls-target-config.h
 *
 * \brief Configuration options (set of defines)
 *
 *  This set of compile-time options may be used to enable
 *  or disable platform specific features.
 *
 *******************************************************************************
 * \copyright
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
 ******************************************************************************/

#ifndef IFX_MBEDTLS_TARGET_CONFIG_H
#define IFX_MBEDTLS_TARGET_CONFIG_H

/* *** DO NOT CHANGE ANY SETTINGS IN THIS SECTION *** */

/* Enable SE RT Services crypto driver */
//#define IFX_PSA_SE_DPA_PRESENT

/* Enable MXCRYPTO transparent driver */
//#define IFX_PSA_MXCRYPTO_PRESENT

/* Enable CRYPTOLITE transparent driver */
#if defined(ENABLE_PSA_CRYPTOLITE)
#define IFX_PSA_CRYPTOLITE_PRESENT
#endif

/* Use SE RT Services to calculate SHA256 digest */
//#define IFX_PSA_SHA256_BY_SE_DPA

/* Use SE RT Services to generate random values */
//#define IFX_PSA_RANDOM_BY_SE_DPA

/* Use SE RT Services builtin keys */
//#define IFX_PSA_CRYPTO_BUILTIN_KEYS

/* Enable support for platform built-in keys.
   Built-in keys are stored in SE RT Services */
//#define MBEDTLS_PSA_CRYPTO_BUILTIN_KEYS

//#define MBEDTLS_PLATFORM_SETUP_TEARDOWN_ALT

#endif /* IFX_MBEDTLS_TARGET_CONFIG_H */
