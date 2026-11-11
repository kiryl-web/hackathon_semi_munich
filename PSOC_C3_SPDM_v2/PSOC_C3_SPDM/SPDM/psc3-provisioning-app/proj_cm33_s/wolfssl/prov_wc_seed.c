/* prov_wc_seed.c
 *
 * Entropy seed hook for wolfCrypt's Hash-DRBG on PSC3M6.
 *
 * Wired via CUSTOM_RAND_GENERATE_SEED=prov_wc_generate_seed (set on the secure
 * project's compiler command line; see proj_cm33_s/Makefile). wolfCrypt's
 * wc_GenerateSeed() calls this to seed the DRBG used by ML-DSA-87 key
 * generation and signing.
 *
 * On M6 the wolfCrypt asset's own hardware-TRNG path is gated on CY_USING_HAL
 * (HAL v2), which cannot be defined here because it makes the generated BSP
 * pull in the HAL-v2 cyhal_hwmgr.h (absent on the v3 mtb-hal / DSL device). So
 * the same PSC3 TRNG is driven directly through the MTB HAL, mirroring the
 * asset's CY_USING_MTB_HAL_TRNG implementation in wolfcrypt/src/random.c.
 */

#if defined(PROV_ENABLE_MLDSA87)

#include <stddef.h>
#include <stdint.h>
#include "mtb_hal.h"

int prov_wc_generate_seed(unsigned char* output, unsigned int sz)
{
    mtb_hal_trng_t obj;
    unsigned int i = 0U;

    if (mtb_hal_trng_setup(&obj, NULL) != CY_RSLT_SUCCESS) {
        return -1;
    }

    while (i < sz) {
        uint32_t val;
        unsigned int n;
        unsigned int j;

        if (mtb_hal_trng_get_uint32(&obj, &val) != CY_RSLT_SUCCESS) {
            return -1;
        }

        n = ((sz - i) < (unsigned int)sizeof(val)) ? (sz - i)
                                                   : (unsigned int)sizeof(val);
        for (j = 0U; j < n; j++) {
            output[i++] = (unsigned char)(val >> (8U * j));
        }
    }

    return 0;
}

#endif /* PROV_ENABLE_MLDSA87 */
