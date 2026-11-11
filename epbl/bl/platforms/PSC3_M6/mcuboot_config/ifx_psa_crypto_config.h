/**
 * \file psa/crypto_config.h
 * \brief PSA crypto configuration options (set of defines)
 *
 */

#ifndef PSA_CRYPTO_CONFIG_H
#define PSA_CRYPTO_CONFIG_H

/*
 * CBC-MAC is not yet supported via the PSA API in Mbed TLS.
 */
//#define PSA_WANT_ALG_CBC_MAC                    1
//#define PSA_WANT_ALG_CBC_NO_PADDING             1
//#define PSA_WANT_ALG_CBC_PKCS7                  1
//#define PSA_WANT_ALG_CCM                        1
//#define PSA_WANT_ALG_CCM_STAR_NO_TAG            1

/*
 * CMAC must be always enabled because mbedTLS unconditionally auto-defines
 * PSA_WANT_KEY_TYPE_DERIVE (in crypto_adjust_auto_enabled.h), which triggers
 * IFX_PSA_CRYPTOLITE_KEY_DERIVATION in the Cryptolite driver. That key
 * derivation code internally uses CMAC MAC functions/types (guarded by
 * IFX_PSA_CRYPTOLITE_MAC), so PSA_WANT_ALG_CMAC must be set to avoid
 * "unknown type name" build errors.
 */
#define PSA_WANT_ALG_CMAC                       1

//#define PSA_WANT_ALG_CFB                        1
//#define PSA_WANT_ALG_CHACHA20_POLY1305          1

/*
 * AES-CTR is needed for EC256 image encryption (decrypting image payload).
 */
#if defined(ENC_IMG) && !defined(USE_ENCRYPTION_KDF_CTR)
#define PSA_WANT_ALG_CTR                        1
#endif

//#define PSA_WANT_ALG_DETERMINISTIC_ECDSA        1
//#define PSA_WANT_ALG_ECB_NO_PADDING             1

/*
 * ECDH is needed for EC256 image encryption (ENC_IMG without KDF_CTR).
 * Enabled when ENC_IMG is set and USE_ENCRYPTION_KDF_CTR is not set.
 */
#if defined(ENC_IMG) && !defined(USE_ENCRYPTION_KDF_CTR)
#define PSA_WANT_ALG_ECDH                       1
#endif

//#define PSA_WANT_ALG_FFDH                       1
//#define PSA_WANT_ALG_ECDSA                      1
//#define PSA_WANT_ALG_JPAKE                      1
//#define PSA_WANT_ALG_GCM                        1

/*
 * HKDF and HMAC are needed for EC256 image encryption key derivation.
 */
#if defined(ENC_IMG) && !defined(USE_ENCRYPTION_KDF_CTR)
#define PSA_WANT_ALG_HKDF                       1
#define PSA_WANT_ALG_HMAC                       1
#endif

//#define PSA_WANT_ALG_HKDF_EXTRACT               1
//#define PSA_WANT_ALG_HKDF_EXPAND                1
//#define PSA_WANT_ALG_MD5                        1
//#define PSA_WANT_ALG_OFB                        1
//#define PSA_WANT_ALG_PBKDF2_HMAC                1
//#define PSA_WANT_ALG_PBKDF2_AES_CMAC_PRF_128    1
//#define PSA_WANT_ALG_RIPEMD160                  1
//#define PSA_WANT_ALG_RSA_OAEP                   1
//#define PSA_WANT_ALG_RSA_PKCS1V15_CRYPT         1
//#define PSA_WANT_ALG_RSA_PKCS1V15_SIGN          1
//#define PSA_WANT_ALG_RSA_PSS                    1
//#define PSA_WANT_ALG_SHA_1                      1
//#define PSA_WANT_ALG_SHA_224                    1

/*
 * SHA algorithm selection.
 *
 * Image hash verification uses Cryptolite SHA directly, not PSA.
 * PSA SHA is only needed for HKDF-SHA256 in EC256 encryption.
 *
 * We keep USE_SHA* gates as well for completeness - they add minimal overhead
 * and ensure the PSA subsystem has correct SHA support if ever used.
 */
#if (defined(ENC_IMG) && !defined(USE_ENCRYPTION_KDF_CTR)) || defined(USE_LMS)
#define PSA_WANT_ALG_SHA_256                    1
#endif

//#define PSA_WANT_ALG_SHA_384                    1
//#define PSA_WANT_ALG_SHA_512                    1
//#define PSA_WANT_ALG_SHA3_224                   1
//#define PSA_WANT_ALG_SHA3_256                   1
//#define PSA_WANT_ALG_SHA3_384                   1
//#define PSA_WANT_ALG_SHA3_512                   1
//#define PSA_WANT_ALG_STREAM_CIPHER              1
//#define PSA_WANT_ALG_TLS12_PRF                  1
//#define PSA_WANT_ALG_TLS12_PSK_TO_MS            1
//#define PSA_WANT_ALG_TLS12_ECJPAKE_TO_PMS       1

/* XTS is not yet supported via the PSA API in Mbed TLS.
 * Note: when adding support, also adjust include/mbedtls/config_psa.h */
//#define PSA_WANT_ALG_XTS                        1

//#define PSA_WANT_ECC_BRAINPOOL_P_R1_256         1
//#define PSA_WANT_ECC_BRAINPOOL_P_R1_384         1
//#define PSA_WANT_ECC_BRAINPOOL_P_R1_512         1
//#define PSA_WANT_ECC_MONTGOMERY_255             1
//#define PSA_WANT_ECC_MONTGOMERY_448             1
//#define PSA_WANT_ECC_SECP_K1_192                1
/*
 * SECP224K1 is buggy via the PSA API in Mbed TLS
 * (https://github.com/Mbed-TLS/mbedtls/issues/3541). Thus, do not enable it by
 * default.
 */
//#define PSA_WANT_ECC_SECP_K1_224                1
//#define PSA_WANT_ECC_SECP_K1_256                1
//#define PSA_WANT_ECC_SECP_R1_192                1
//#define PSA_WANT_ECC_SECP_R1_224                1

/*
 * ECC curve selection.
 *
 * ECDSA signature verification uses ROM crypto hardware directly (not PSA),
 * so PSA curve defines are NOT needed for ECDSA P-256/P-384/P-521 verification.
 *
 * However, MBEDTLS_ECDSA_C (enabled when USE_ROM_CRYPTO_ECDSA is defined in
 * ifx_mbedtls_crypto_config.h) requires MBEDTLS_ECP_C + at least one short
 * Weierstrass curve as a prerequisite. These are auto-derived from PSA ECC
 * defines via config_adjust_legacy_from_psa.h.
 *
 * P-256 is enabled when:
 *   - USE_ROM_CRYPTO_ECDSA is set (satisfies MBEDTLS_ECDSA_C prerequisite)
 *   - EC256 encryption is used (ECDH key agreement on P-256)
 * P-384 and P-521 are never needed through PSA.
 */
#if defined(USE_ROM_CRYPTO_ECDSA) || \
	(defined(ENC_IMG) && !defined(USE_ENCRYPTION_KDF_CTR))
#define PSA_WANT_ECC_SECP_R1_256                1
#endif

/* P-384: not needed via PSA (ECDSA-384 uses Cryptolite directly) */
// #define PSA_WANT_ECC_SECP_R1_384

/* P-521: not needed via PSA (ECDSA-521 uses Cryptolite directly) */
// #define PSA_WANT_ECC_SECP_R1_521

//#define PSA_WANT_KEY_TYPE_DERIVE                1
//#define PSA_WANT_KEY_TYPE_PASSWORD              1
//#define PSA_WANT_KEY_TYPE_PASSWORD_HASH         1

/*
 * HMAC key type needed for EC256 encryption (HKDF key derivation).
 */
#if defined(ENC_IMG) && !defined(USE_ENCRYPTION_KDF_CTR)
#define PSA_WANT_KEY_TYPE_HMAC                  1
#endif

/*
 * AES key type: always enabled as a prerequisite for PSA_WANT_ALG_CMAC (see
 * above). PSA_WANT_ALG_CMAC -> MBEDTLS_CMAC_C -> requires MBEDTLS_AES_C,
 * which is derived from PSA_WANT_KEY_TYPE_AES.
 * Also needed for EC256 encryption (AES-CTR payload decryption).
 */
#define PSA_WANT_KEY_TYPE_AES                   1

//#define PSA_WANT_KEY_TYPE_ARIA                  1
//#define PSA_WANT_KEY_TYPE_CAMELLIA              1
//#define PSA_WANT_KEY_TYPE_CHACHA20              1
//#define PSA_WANT_KEY_TYPE_DES                   1
//#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR          1 /* Deprecated */

/*
 * ECC public key type: needed for MBEDTLS_ECDSA_C prerequisite (MBEDTLS_ECP_C)
 * and for EC256 encryption (ECDH key exchange via PSA).
 * ECDSA verification itself uses ROM crypto directly, but the mbedTLS module
 * check requires ECC support to be registered at the PSA level.
 */
#if defined(USE_ROM_CRYPTO_ECDSA) || \
	(defined(ENC_IMG) && !defined(USE_ENCRYPTION_KDF_CTR))
#define PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY        1
#endif

//#define PSA_WANT_KEY_TYPE_DH_PUBLIC_KEY         1
//#define PSA_WANT_KEY_TYPE_RAW_DATA              1
//#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR          1 /* Deprecated */
//#define PSA_WANT_KEY_TYPE_RSA_PUBLIC_KEY        1

/*
 * The following symbols extend and deprecate the legacy
 * PSA_WANT_KEY_TYPE_xxx_KEY_PAIR ones. They include the usage of that key in
 * the name's suffix. "_USE" is the most generic and it can be used to describe
 * a generic suport, whereas other ones add more features on top of that and
 * they are more specific.
 */

/*
 * ECC key-pair operations are needed for EC256 encryption (ECDH key agreement).
 * Enabling these activates the Cryptolite hardware ECDH path (IFX_PSA_CRYPTOLITE_ECDH)
 * for significantly faster key exchange.
 */
#if defined(ENC_IMG) && !defined(USE_ENCRYPTION_KDF_CTR)
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC    1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT   1
#endif

//#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_EXPORT   1
//#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE 1
//#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_DERIVE   1

//#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_BASIC      1
//#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_IMPORT   1
//#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_EXPORT   1
//#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_GENERATE 1
//#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_DERIVE   1 /* Not supported */

//#define PSA_WANT_KEY_TYPE_DH_KEY_PAIR_BASIC       1
//#define PSA_WANT_KEY_TYPE_DH_KEY_PAIR_IMPORT    1
//#define PSA_WANT_KEY_TYPE_DH_KEY_PAIR_EXPORT    1
//#define PSA_WANT_KEY_TYPE_DH_KEY_PAIR_GENERATE  1
//#define PSA_WANT_KEY_TYPE_DH_KEY_PAIR_DERIVE    1 /* Not supported */

#endif /* PSA_CRYPTO_CONFIG_H */
