/* user_settings.h
 *
 * wolfCrypt configuration for the PSC3M6 provisioning service.
 *
 * Scope: ML-DSA-87 (FIPS 204, category 5) KEY GENERATION + SIGNING on-device,
 * using wolfCrypt's pure-C Dilithium. This is deliberately NOT the verify-only
 * profile used by bootloaders: the provisioning flow creates a device key pair
 * and signs a proof/CSR, so the make-key and sign paths must stay compiled in.
 *
 * The classical ECDSA-P256 provisioning suite is served by PSA / mbedTLS, not
 * wolfCrypt; this file only needs to enable the Dilithium primitive and its
 * SHA-3/SHAKE + RNG dependencies, and disable everything else to keep the
 * auto-discovered wolfcrypt/src C sources compiling to (near) nothing.
 *
 * Selected from proj_cm33_s/Makefile via WOLFSSL_USER_SETTINGS + USE_MLDSA87,
 * and only when PROV_SIG_SUITES includes ml-dsa-87 (M6 only).
 */

#ifndef WOLFSSL_USER_SETTINGS_H
#define WOLFSSL_USER_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- ML-DSA / Dilithium ------------------------------------------------- */

/* Enable Dilithium/ML-DSA, pure-C wolfCrypt implementation (not liboqs). */
#define HAVE_DILITHIUM
#define WOLFSSL_WC_DILITHIUM

/* Provisioning needs make-key AND sign, so do NOT define any of:
 *   WOLFSSL_DILITHIUM_VERIFY_ONLY / _NO_MAKE_KEY / _NO_SIGN / _ASSIGN_KEY /
 *   _VERIFY_SMALL_MEM / _VERIFY_NO_MALLOC
 * (those are verify-only optimisations and are incompatible with keygen/sign).
 */

/* Use the built-in hardcoded hash OID tables instead of the ASN.1 OID lookup
 * infrastructure. Key material is exchanged raw over the provisioning wire and
 * the CSR DER is assembled by the provisioning service, so the full ASN.1 code
 * is not needed and this keeps asn.c compiling to almost nothing. */
#define WOLFSSL_DILITHIUM_NO_ASN1

/* Compile only the category-5 parameter set (USE_MLDSA87 comes from the
 * Makefile); disable the unused 44/65 sets to save code and RAM. */
#ifndef USE_MLDSA44
#define WOLFSSL_NO_ML_DSA_44
#endif
#ifndef USE_MLDSA65
#define WOLFSSL_NO_ML_DSA_65
#endif
#ifndef USE_MLDSA87
#define WOLFSSL_NO_ML_DSA_87
#endif

/* SHA-3 / SHAKE are the Dilithium hashing primitives. */
#define WOLFSSL_SHA3
#define WOLFSSL_SHAKE128
#define WOLFSSL_SHAKE256

/* --- Memory footprint (category-5 is the largest parameter set) --------- *
 * ML-DSA-87 (k=8, l=7) expands a full A matrix of 56 polynomials
 * (56 * 256 * 4 = 57,344 bytes) during key generation and signing. On this
 * part the whole device has only 64 KB of SRAM, so materialising the full
 * matrix is not an option. The SMALL_MEM variants stream the matrix a row at
 * a time, trading speed for a working set of a few KB, which is mandatory
 * here. They only affect the make-key and sign code paths. */
#define WOLFSSL_DILITHIUM_MAKE_KEY_SMALL_MEM
#define WOLFSSL_DILITHIUM_SIGN_SMALL_MEM

/* --- RNG ---------------------------------------------------------------- */

/* Key generation and signing require a WC_RNG. Use wolfCrypt's Hash-DRBG,
 * seeded from the PSC3 hardware TRNG. On M6 (HAL v3) the asset's built-in
 * CY_USING_HAL TRNG branch in random.c cannot be used (defining CY_USING_HAL
 * makes the generated BSP include the HAL-v2 cyhal_hwmgr.h, absent here), so
 * the seed is provided by our own CUSTOM_RAND_GENERATE_SEED hook
 * (wolfssl/prov_wc_seed.c, prov_wc_generate_seed) which drives the MTB HAL TRNG
 * directly. CUSTOM_RAND_GENERATE_SEED itself is defined on the Makefile command
 * line so the .S asm sources (which do not include this header) never see it.
 * NO_DEV_RANDOM keeps the POSIX /dev/random fallback out of the build. */
#define HAVE_HASHDRBG
#define NO_DEV_RANDOM
extern int prov_wc_generate_seed(unsigned char* output, unsigned int sz);

/* --- Trim everything unused -------------------------------------------- */

/* No TLS/PKI layer, filesystem, sockets or threads on this bare-metal secure
 * core; the EC path lives in mbedTLS, so all classic wolfCrypt algorithms are
 * turned off. */
#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define WOLFSSL_USER_IO
#define NO_FILESYSTEM
#define NO_WRITEV
#define NO_MAIN_DRIVER
#define NO_DSA
#define NO_DH
#define NO_RSA
#define NO_PSK
#define NO_MD4
#define NO_MD5
#define NO_RC4
#define NO_DES3
#define NO_OLD_TLS
#define NO_SESSION_CACHE
#define NO_ERROR_STRINGS

/* Use standard C fixed-width types. */
#define SIZEOF_LONG_LONG 8

/* NOTE: dynamic-memory posture (heap vs WOLFSSL_STATIC_MEMORY pool) is decided
 * in Phase 3 once the make-key/sign working-set is measured; keygen/sign cannot
 * use WOLFSSL_NO_MALLOC, so it is intentionally NOT defined here. */

#ifdef __cplusplus
}
#endif

#endif /* WOLFSSL_USER_SETTINGS_H */
