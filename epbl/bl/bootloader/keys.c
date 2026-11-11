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

#include <bootutil/sign_key.h>
#include <mcuboot_config/mcuboot_config.h>
#include "cyboot_flash_list.h"

#if defined(MCUBOOT_VALIDATE_PRIMARY_SLOT) || !defined(MCUBOOT_SKIP_VALIDATE_SECONDARY_SLOT)

#if !defined(SFLASH_KEYS)
const int bootutil_key_cnt = 1;

#if defined(USE_LMS)
extern const unsigned char lms_pub_key[];
extern unsigned int lms_pub_key_len;

const struct bootutil_key bootutil_keys[] = {
    [0] = {
        .key = lms_pub_key,
        .len = &lms_pub_key_len,
    }
};
#elif defined(MCUBOOT_SIGN_XMSS)
extern const unsigned char xmss_pub_key[];
extern const unsigned int xmss_pub_key_len;
const struct bootutil_key bootutil_keys[] = {
    [0] = {
        .key = xmss_pub_key,
        .len = &xmss_pub_key_len,
    }
};
#elif defined(MCUBOOT_SIGN_MLDSA87) || defined(MCUBOOT_SIGN_MLDSA65) || defined(MCUBOOT_SIGN_MLDSA44)
extern const unsigned char mldsa_pub_key[];
extern const unsigned int mldsa_pub_key_len;
const struct bootutil_key bootutil_keys[] = {
    [0] = {
        .key = mldsa_pub_key,
        .len = &mldsa_pub_key_len,
    }
};
#else
extern const unsigned char ecdsa_pub_key[];
extern unsigned int ecdsa_pub_key_len;

const struct bootutil_key bootutil_keys[] = {
    [0] = {
        .key = ecdsa_pub_key,
        .len = &ecdsa_pub_key_len,
    }
};

#endif /*USE_LMS*/
#else
#endif /*!SFLASH_KEYS*/

#else
/* No keys defined, image validation is disabled */
const struct bootutil_key bootutil_keys[] = {};
const int bootutil_key_cnt = 0;

#endif /*MCUBOOT_VALIDATE_PRIMARY_SLOT || !MCUBOOT_SKIP_VALIDATE_SECONDARY_SLOT*/

