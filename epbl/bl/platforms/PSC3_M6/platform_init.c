#include <bootutil/sign_key.h>
#include "bootutil/fault_injection_hardening.h"
#include "platform_init.h"
#include <stdlib.h>
#include <string.h>
#include "cy_cryptolite_trng.h"
#include "crypto.h"
#if defined(MCUBOOT_FIH_PROFILE_HIGH)
#include "boot_rng.h"
#endif /* MCUBOOT_FIH_PROFILE_HIGH */

#define SFLASH_USER_ROW                 (0x034076F0)

#if defined(SFLASH_KEYS)
/* Primary OEM_ROT_KEY */
#define SFLASH_OEM_ROT_KEY_0            (0x03400A70)
/* Secondary OEM_ROT_KEY */
#define SFLASH_OEM_ROT_KEY_1            (0x03400AF8)
/* Development OEM_ROT_KEY*/
#define SFLASH_OEM_ROT_KEY_DEV          (0x03400E8C)

#define SFLASH_OEM_ROT_KEY_REVOCATION   (0x0340080B)
#define REVOCATION_OEM_ROT_KEY_0        (0x3C)
#define REVOCATION_OEM_ROT_KEY_1        (0xA5)
/* CRC of the BOOTROW, contains DATA about life cycle */
#define SFLASH_BOOTROW_CRC              (0x03400804)

#define LIFE_CYCLE_DEVELOPMENT          (0x3C56A2F7)
#define LIFE_CYCLE_PRODUCTION           (0x7C68E2FC)

/* Set device-specific key lengths */
#if defined(MCUBOOT_SIGN_EC256)
const unsigned int ecdsa_pub_key_len = 65;  /* RAW EC256: 0x04 + 32 + 32 */
#elif defined(MCUBOOT_SIGN_EC384)
const unsigned int ecdsa_pub_key_len = 97;  /* RAW EC384: 0x04 + 48 + 48 */
#elif defined(MCUBOOT_SIGN_EC521)
const unsigned int ecdsa_pub_key_len = 133; /* RAW EC521: 0x04 + 66 + 66 */
#endif /* MCUBOOT_SIGN_EC521 */

const int bootutil_key_cnt = 1;
/* Define bootutil_keys array as NON-CONST for runtime initialization */
struct bootutil_key bootutil_keys[] = {
    [0] = {
        .key = NULL,  /* Will be set in platform_init() based on lifecycle */
        .len = (unsigned int *)&ecdsa_pub_key_len,
    }
};

static fih_int platform_ecdsa_key_init(void)
{
    fih_int ret = FIH_FAILURE;
    /* Handle Development LCS */
    if ((LIFE_CYCLE_DEVELOPMENT == *((volatile uint32_t *)SFLASH_BOOTROW_CRC)) &&
#if defined(MCUBOOT_FIH_PROFILE_ON)
        fih_delay() &&
#endif /* MCUBOOT_FIH_PROFILE_ON */
        (LIFE_CYCLE_DEVELOPMENT == *((volatile uint32_t *)SFLASH_BOOTROW_CRC))) { 
        /* For Development LCS used OEM_ROT_KEY_DEV */
        bootutil_keys[0].key = (uint8_t *)(SFLASH_OEM_ROT_KEY_DEV);
        if ((uint8_t *)SFLASH_OEM_ROT_KEY_DEV == bootutil_keys[0].key) {
            ret = FIH_SUCCESS;
        }
    }
    /* Handle other life cycles */
    else {
        if ((*((volatile uint8_t *)SFLASH_OEM_ROT_KEY_REVOCATION) == REVOCATION_OEM_ROT_KEY_0) &&
#if defined(MCUBOOT_FIH_PROFILE_ON)
            fih_delay() &&
#endif /* MCUBOOT_FIH_PROFILE_ON */
            (*((volatile uint8_t *)SFLASH_OEM_ROT_KEY_REVOCATION) == REVOCATION_OEM_ROT_KEY_0)) {

            bootutil_keys[0].key = (uint8_t *)(SFLASH_OEM_ROT_KEY_0);
            if ((uint8_t *)SFLASH_OEM_ROT_KEY_0 == bootutil_keys[0].key) {
                ret = FIH_SUCCESS;
            }
        }
        else if ((*((volatile uint8_t *)SFLASH_OEM_ROT_KEY_REVOCATION) == REVOCATION_OEM_ROT_KEY_1) &&
#if defined(MCUBOOT_FIH_PROFILE_ON)
            fih_delay() &&
#endif /* MCUBOOT_FIH_PROFILE_ON */
            (*((volatile uint8_t *)SFLASH_OEM_ROT_KEY_REVOCATION) == REVOCATION_OEM_ROT_KEY_1)) {

            bootutil_keys[0].key = (uint8_t *)(SFLASH_OEM_ROT_KEY_1);
            if ((uint8_t *)SFLASH_OEM_ROT_KEY_1 == bootutil_keys[0].key) {
                ret = FIH_SUCCESS;
            }
        }
        else {
            /* Other - an invalid value */
            ret = FIH_FAILURE;
        }
    }

    return ret;
}
#endif /* SFLASH_KEYS */

#if defined(MCUBOOT_ENCRYPT_EC256)
/* Use C array from generated ec256_enc_key.c file */
extern const unsigned char ec256_enc_priv_key[];
extern const unsigned int ec256_enc_priv_key_len;

const struct bootutil_key bootutil_enc_key = {
    .key = (uint8_t *)ec256_enc_priv_key,
    .len = (unsigned int *)&ec256_enc_priv_key_len,
};
#endif /* MCUBOOT_ENCRYPT_EC256 */

#if defined(USE_ENCRYPTION_KDF_CTR)
#define AES_KDF_CTR_KEY_LEN (16)
/* For AES-KDF-CTR algorithm we use symmetric aes key */

#define aes_kek_key ((void*) (SFLASH_USER_ROW))
unsigned int aes_kek_key_len = AES_KDF_CTR_KEY_LEN;
const struct bootutil_key bootutil_enc_key = {
    .key = aes_kek_key,
    .len = &aes_kek_key_len,
};

#endif /* USE_ENCRYPTION_KDF_CTR */

cy_rslt_t platform_init(void)
{
    psa_status_t psa_status = psa_crypto_init();

    if (PSA_SUCCESS != psa_status) {
        return !CY_RSLT_SUCCESS;
    }

#if defined(SFLASH_KEYS)
    fih_int result = platform_ecdsa_key_init();
    if (fih_not_eq(result, FIH_SUCCESS)) {
        return !CY_RSLT_SUCCESS;
    }

#endif /* SFLASH_KEYS */

    return CY_RSLT_SUCCESS;
}

void platform_deinit(void)
{

}

psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t *context,
                                             uint8_t *output, size_t len, size_t *olen)
{
    int ret = 0;
    *olen = 0;
    /* temporary random data buffer */
    uint32_t random = 0u;

    (void)context;

    if (CY_CRYPTOLITE_SUCCESS != Cy_Cryptolite_Trng_Init(CRYPTOLITE, NULL)) {
        return PSA_ERROR_GENERIC_ERROR;
    }
    if (CY_CRYPTOLITE_SUCCESS != Cy_Cryptolite_Trng_Enable(CRYPTOLITE)) {
        return PSA_ERROR_GENERIC_ERROR;
    }
    /* Get Random byte */
    while ((*olen < len) && (ret == 0)) {
        if (Cy_Cryptolite_Trng_ReadData(CRYPTOLITE, &random) != CY_CRYPTOLITE_SUCCESS) {
            return PSA_ERROR_GENERIC_ERROR;
        } else {
            for (uint8_t i = 0; (i < 4) && (*olen < len); i++) {
                *output++ = ((uint8_t *)&random)[i];
                *olen += 1;
            }
        }
    }
    random = 0uL;

    (void)Cy_Cryptolite_Trng_Disable(CRYPTOLITE);
    (void)Cy_Cryptolite_Trng_DeInit(CRYPTOLITE);

    return (ret);
}

#if defined(MCUBOOT_FIH_PROFILE_HIGH)
/*******************************************************************************
 * Function Name: boot_rng_init
 *******************************************************************************
 * \brief Platform-specific implementation of boot_rng_init() for PSC3_M6.
 *        Initializes and enables Cryptolite TRNG, seeds the PRNG with 
 *        a TRNG value, then disables Cryptolite TRNG.
 *
 * return true   if PRNG initialization is successful
 *        false  in other cases
 *
 ******************************************************************************/
bool boot_rng_init(void)
{
    bool ret = false;
    uint32_t rnd = 0U;

    do {
        /* Prevent RND double initialization */
        if (boot_rng_is_initialized()) {
            ret = true;
            break;
        }

        if (CY_CRYPTOLITE_SUCCESS != Cy_Cryptolite_Trng_Init(CRYPTOLITE, NULL)) {
            break;
        }

        if (CY_CRYPTOLITE_SUCCESS != Cy_Cryptolite_Trng_Enable(CRYPTOLITE)) {
            (void)Cy_Cryptolite_Trng_DeInit(CRYPTOLITE);
            break;
        }

        if (CY_CRYPTOLITE_SUCCESS != Cy_Cryptolite_Trng_ReadData(CRYPTOLITE, &rnd)) {
            (void)Cy_Cryptolite_Trng_Disable(CRYPTOLITE);
            (void)Cy_Cryptolite_Trng_DeInit(CRYPTOLITE);
            break;
        }

        (void)Cy_Cryptolite_Trng_Disable(CRYPTOLITE);
        (void)Cy_Cryptolite_Trng_DeInit(CRYPTOLITE);

        boot_rng_init_seed(rnd);
        boot_rng_initialization_done();
        rnd = 0U;
        ret = true;
    } while (false);

    return ret;
}
#endif /* MCUBOOT_FIH_PROFILE_HIGH */
