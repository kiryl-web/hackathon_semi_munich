/*
 * Copyright (c) 2018 Open Source Foundries Limited
 * Copyright (c) 2019 Cypress Semiconductor Corporation
 * (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG.  SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef MCUBOOT_CONFIG_H
#define MCUBOOT_CONFIG_H

/*
 * Template configuration file for MCUboot.
 *
 * When porting MCUboot to a new target, copy it somewhere that your
 * include path can find it as mcuboot_config/mcuboot_config.h, and
 * make adjustments to suit your platform.
 *
 * For examples, see:
 *
 * boot/zephyr/include/mcuboot_config/mcuboot_config.h
 * boot/mynewt/mcuboot_config/include/mcuboot_config/mcuboot_config.h
 */
/* Default maximum number of flash sectors per image slot; change
 * as desirable. */
#ifndef MCUBOOT_MAX_IMG_SECTORS
#define MCUBOOT_MAX_IMG_SECTORS 128U
#endif

/*
 * Signature types
 *
 * You must choose exactly one signature type.
 */

/* Uncomment for RSA signature support */
//#define MCUBOOT_SIGN_RSA

/* Uncomment for ECDSA signatures using curve P-256. */
#if defined(USE_SHA256) && !defined(USE_LMS) && !defined(MCUBOOT_SIGN_XMSS) && \
    !defined(USE_MLDSA87) && !defined(USE_MLDSA65) && !defined(USE_MLDSA44)
#define MCUBOOT_SIGN_EC256
#endif

/* ECDSA signatures using curve P-384 - disabled when using MLDSA */
#if defined(USE_SHA384) && !defined(USE_MLDSA87) && !defined(USE_MLDSA65) && !defined(USE_MLDSA44)
#define MCUBOOT_SIGN_EC384
#endif

/* ECDSA signatures using curve P-521 - disabled when using MLDSA */
#if defined(USE_SHA512) && !defined(USE_MLDSA87) && !defined(USE_MLDSA65) && !defined(USE_MLDSA44)
#define MCUBOOT_SIGN_EC521
#endif

/* Uncomment for LMS signatures*/
#ifdef USE_LMS
#define MCUBOOT_SIGN_LMS
#endif

/* ML-DSA-87 (Dilithium5) signature */
#ifdef USE_MLDSA87
#define MCUBOOT_SIGN_MLDSA87
#endif

/* ML-DSA-65 (Dilithium3) signature */
#ifdef USE_MLDSA65
#define MCUBOOT_SIGN_MLDSA65
#endif

/* ML-DSA-44 (Dilithium2) signature */
#ifdef USE_MLDSA44
#define MCUBOOT_SIGN_MLDSA44
#endif

// #define MCUBOOT_SIGN_EC

/*
 * Upgrade mode
 *
 * The default is to support A/B image swapping with rollback.  A
 * simpler code path, which only supports overwriting the
 * existing image with the update image, is also available.
 */
#ifdef MCUBOOT_OVERWRITE_ONLY
/* Uncomment to only erase and overwrite those slot 0 sectors needed
 * to install the new image, rather than the entire image slot. */
/* #define MCUBOOT_OVERWRITE_ONLY_FAST */
#else
/* Using SWAP w Scratch by default.
 * Uncomment which is needed. */
#define MCUBOOT_SWAP_USING_SCRATCH  1
/* #define MCUBOOT_SWAP_USING_MOVE     1 */
#define MCUBOOT_SWAP_USING_STATUS   1
#endif

/* Save ENC IV for encryption image */
#define MCUBOOT_SAVE_ENC_IV 1

/*
 * Cryptographic settings
 *
 * You must choose between mbedTLS and Tinycrypt as source of
 * cryptographic primitives. Other cryptographic settings are also
 * available.
 */

/* Uncomment to use ARM's mbedTLS cryptographic primitives */
#define MCUBOOT_USE_MBED_TLS
#define MCUBOOT_USE_PSA_CRYPTO
/* Uncomment to use Tinycrypt's. */
/* #define MCUBOOT_USE_TINYCRYPT */

/*
 * Flash abstraction
 */

/* Uncomment if your flash map API supports flash_area_get_sectors().
 * See the flash APIs for more details. */
// TODO: FWSECURITY-755
#define MCUBOOT_USE_FLASH_AREA_GET_SECTORS


/* Use custom interface for SHA module */
#define MCUBOOT_SHA_CUSTOM_INTERFACE

/*
 * Currently there is no configuration option, for this platform,
 * that enables the system specific mcumgr commands in mcuboot
 */
#define MCUBOOT_PERUSER_MGMT_GROUP_ENABLED 0

/*
 * Logging
 */

#define MCUBOOT_HAVE_LOGGING 1
/* Define this to support native mcuboot logging system */
#define CONFIG_MCUBOOT 1

/*
 * Assertions
 */

/* Uncomment if your platform has its own mcuboot_config/mcuboot_assert.h.
 * If so, it must provide an ASSERT macro for use by bootutil. Otherwise,
 * "assert" is used. */
//#define MCUBOOT_HAVE_ASSERT_H

#define MCUBOOT_WATCHDOG_FEED()         \
    do {                                \
        /* TODO: to be implemented */   \
    } while (0)

/* Uncomment these if support of encrypted upgrade image is needed */
#ifdef ENC_IMG
#define MCUBOOT_ENC_IMAGES
/* Choose which algorithm to use for encryption. */
#ifdef USE_ENCRYPTION_KDF_CTR
#define MCUBOOT_ENCRYPT_KDF_CMAC
#else
#define MCUBOOT_ENCRYPT_EC256
#endif /* USE_ENCRYPTION_KDF_CTR */

#endif /* ENC_IMG */

/*
 * No direct idle call implemented
 */
#define MCUBOOT_CPU_IDLE() \
    do {                   \
    } while (0)

/*
 * Do not save ENCTLV by default
 */
//#define MCUBOOT_SWAP_SAVE_ENCTLV 1

#endif /* MCUBOOT_CONFIG_H */
