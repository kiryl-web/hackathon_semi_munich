/* user_settings.h
 *
 * Minimal WolfSSL configuration for ML-DSA verification in MCUboot bootloader
 */

#ifndef WOLFSSL_USER_SETTINGS_H
#define WOLFSSL_USER_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Enable Dilithium/ML-DSA support */
#define HAVE_DILITHIUM

/* Use WolfSSL's pure C implementation (not liboqs) */
#define WOLFSSL_WC_DILITHIUM

/* Enable only verification (reduce code size) */
#define WOLFSSL_DILITHIUM_VERIFY_ONLY
#define WOLFSSL_DILITHIUM_NO_MAKE_KEY
#define WOLFSSL_DILITHIUM_NO_SIGN

/* Enable small memory verification mode */
#define WOLFSSL_DILITHIUM_VERIFY_SMALL_MEM

/* Use smaller code path: process SHAKE-128 one block at a time instead of 5.
 * Trade-off: possibly slower NTT polynomial rejection sampling. */
#define WOLFSSL_DILITHIUM_SMALL

/* Assign key data by pointer instead of copying into struct.
 * Reduces MlDsaKey from ~20KB to ~12.5KB by replacing the embedded
 * p[2592] and k[4896] buffers with const byte* pointers (8 bytes total).
 * Safe: bootloader public key lives in flash (const) and outlives the key struct.
 * Cannot be used with make_key (already disabled via VERIFY_ONLY). */
#define WOLFSSL_DILITHIUM_ASSIGN_KEY

/* Use built-in hardcoded hash OID tables instead of wc_HashGetOID/OidFromId.
 * Required because ASN.1 OID lookup functions are not compiled in this minimal
 * WolfSSL build. This enables HashML-DSA verify (wc_dilithium_verify_ctx_hash)
 * without pulling in the full ASN.1 infrastructure. */
#define WOLFSSL_DILITHIUM_NO_ASN1

/* Enable all ML-DSA parameter sets (disable unused ones based on USE_MLDSA* from command line) */
#ifndef USE_MLDSA44
#define WOLFSSL_NO_ML_DSA_44
#endif

#ifndef USE_MLDSA65
#define WOLFSSL_NO_ML_DSA_65
#endif

#ifndef USE_MLDSA87
#define WOLFSSL_NO_ML_DSA_87
#endif

/* Enable SHA3 and SHAKE (required for Dilithium) */
#define WOLFSSL_SHA3
#define WOLFSSL_SHAKE128
#define WOLFSSL_SHAKE256

#if defined(USE_MLDSA44) ||defined(USE_MLDSA65) || defined(USE_MLDSA87)

#if defined(USE_SHA384)
#define WOLFSSL_SHA384
#endif

#if defined(USE_SHA512)
#define WOLFSSL_SHA512
#endif

#endif

/* Disable unused features to minimize code size */
#define NO_FILESYSTEM
#define NO_WRITEV
#define NO_MAIN_DRIVER
#define NO_DEV_RANDOM
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

/* Use standard C types */
#define SIZEOF_LONG_LONG 8

/* Platform specific */
#define SINGLE_THREADED
#define WOLFSSL_USER_IO

/* No dynamic memory allocation - use stack memory for verification */
#define WOLFSSL_NO_MALLOC
/* Dilithium-specific: don't use XMALLOC in verify path, pin memory to key.
 * Required for ARM Compiler (armlink L6218E: wolfSSL_Malloc/Free undefined) */
#define WOLFSSL_DILITHIUM_VERIFY_NO_MALLOC
/* NO_WOLFSSL_MEMORY: instructs types.h to bypass the wolfSSL_Malloc/Free
 * wrapper layer entirely. With WOLFSSL_NO_MALLOC also set, types.h resolves
 * XMALLOC(s,h,t) to NULL (no call, no symbol reference) at compile time.
 * This is the correct wolfssl mechanism - types.h re-defines XMALLOC after
 * user_settings.h is included, so manual #define XMALLOC overrides in
 * user_settings.h are silently overwritten. NO_WOLFSSL_MEMORY is the official
 * flag that controls the types.h XMALLOC selection logic. */
#define NO_WOLFSSL_MEMORY

#ifdef __cplusplus
}
#endif

#endif /* WOLFSSL_USER_SETTINGS_H */