/*****************************************************************************
 * File Name        : prov_nsc.h
 *
 * Description      : Public interface for the provisioning Non-Secure-Callable
 *                    (NSC) functions. This header is shared between the secure
 *                    application (which implements the NSC veneers) and the
 *                    non-secure application (which calls them).
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

#ifndef PROV_NSC_H
#define PROV_NSC_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/******************************************************************************
 * Macros
 *****************************************************************************/

/* PAS version reported in PROV_GET_STATUS (Provisioning-Command-Reference §5).
 * Bumped if the on-flash layout changes. Initial value = 0x0001. */
#define PROV_PAS_VERSION            (0x0001U)

/* Number of provisioning asset slots (key slot + certificate slot). */
#define PROV_NUM_SLOTS              (2U)

/* Largest number of crypto suites advertised by PROV_GET_INFO. A build may
 * select any mix of ECDSA-P256 / P-384 / P-521 (all devices) and ML-DSA-87
 * (PQC, M6/M8 only), so up to four suites can be advertised at once. See
 * PROV_ENABLE_EC256 / PROV_ENABLE_EC384 / PROV_ENABLE_EC521 /
 * PROV_ENABLE_MLDSA87. */
#define PROV_MAX_ALGS               (4U)

/* Device unique-ID length reported by PROV_GET_INFO (PSoC 64-bit unique id). */
#define PROV_UID_LEN                (8U)

/* Maximum payload chunk the device accepts/produces per packet (bytes). Kept
 * within common I2C-DFU bridge limits. Reported by PROV_GET_INFO. */
#define PROV_MAX_CHUNK              (64U)

/* Baseline crypto suite: ECDSA secp256r1 (NIST P-256) / SHA-256.
 * Wire contract: Provisioning-Command-Reference §2. */
#define PROV_ALG_ECDSA_P256         (0x0001U)

/* Raw public-key and signature lengths for the P-256 suite.
 *   pub : uncompressed X||Y (no 0x04 prefix) = 64 bytes
 *   sig : raw r||s                            = 64 bytes */
#define PROV_PUB_LEN_P256           (64U)
#define PROV_SIG_LEN_P256           (64U)

/* Optional EC suite: ECDSA secp384r1 (NIST P-384) / SHA-384. Compiled in when
 * PROV_ENABLE_EC384 is defined (all devices). Wire contract:
 * Provisioning-Command-Reference §2. */
#define PROV_ALG_ECDSA_P384         (0x0002U)

/* Raw public-key and signature lengths for the P-384 suite.
 *   pub : uncompressed X||Y (no 0x04 prefix) = 96 bytes
 *   sig : raw r||s                            = 96 bytes */
#define PROV_PUB_LEN_P384           (96U)
#define PROV_SIG_LEN_P384           (96U)

/* Optional EC suite: ECDSA secp521r1 (NIST P-521) / SHA-512. Compiled in when
 * PROV_ENABLE_EC521 is defined (all devices). The field is 521 bits, so each
 * scalar/coordinate is ceil(521/8) = 66 bytes. Wire contract:
 * Provisioning-Command-Reference §2. */
#define PROV_ALG_ECDSA_P521         (0x0003U)

/* Raw public-key and signature lengths for the P-521 suite.
 *   pub : uncompressed X||Y (no 0x04 prefix) = 132 bytes
 *   sig : raw r||s                            = 132 bytes */
#define PROV_PUB_LEN_P521           (132U)
#define PROV_SIG_LEN_P521           (132U)

/* Optional PQC suite: ML-DSA-87 (FIPS 204, category 5) / SHA-256 message hash.
 * Compiled in on M6 only (PROV_ENABLE_MLDSA87). Wire contract:
 * Provisioning-Command-Reference §2. */
#define PROV_ALG_MLDSA_87           (0x0103U)

/* Raw key and signature lengths for the ML-DSA-87 suite (FIPS 204).
 *   pub  : 2592 bytes
 *   priv : 4896 bytes (raw secret key, AES-GCM wrapped in the key slot)
 *   sig  : 4627 bytes */
#define PROV_PUB_LEN_MLDSA87        (2592U)
#define PROV_PRIV_LEN_MLDSA87       (4896U)
#define PROV_SIG_LEN_MLDSA87        (4627U)

/* Length of a SHA-256 digest / the host nonce used by PROV_PROVE_CERT. */
#define PROV_SHA256_LEN             (32U)
#define PROV_NONCE_LEN              (32U)

/* Confirm constants guarding the destructive commands (little-endian on the
 * wire). The host must supply the exact value or the command is rejected with
 * PROV_ERR_CONFIRM. */
#define PROV_CONFIRM_ERASE          (0x45524153U)  /* 'E','R','A','S' */
#define PROV_CONFIRM_FINALIZE       (0x46524545U)  /* 'F','R','E','E' */

/* slot_id sentinel meaning "all slots" for PROV_ERASE. */
#define PROV_SLOT_ALL               (0xFFU)

/******************************************************************************
 * Enumerations
 *****************************************************************************/

/**
 * Per-slot role (Provisioning-Command-Reference §5, PROV_GET_STATUS).
 * Derived live from each slot's validity predicate and slot_magic — nothing
 * is read from a stored state value.
 */
typedef enum
{
    PROV_SLOT_FREE = 0x00U,  /* slot is empty / not yet assigned           */
    PROV_SLOT_KEY  = 0x01U,  /* slot holds a key pair (or symmetric key)   */
    PROV_SLOT_CERT = 0x02U   /* slot holds a device certificate            */
} prov_slot_type_t;

/**
 * Provisioning command result codes (Provisioning-Command-Reference §8).
 * Returned by every NSC function and surfaced to the host as the first byte
 * of the response payload (prov_cmd_status).
 */
typedef enum
{
    PROV_OK                    = 0x00U,  /* success                                    */
    PROV_ERR_FROZEN            = 0x01U,  /* assets frozen; mutating command rejected    */
    PROV_ERR_NOT_READY         = 0x02U,  /* structural pre-freeze check failed         */
    PROV_ERR_NO_KEY            = 0x03U,  /* key slot empty / invalid                   */
    PROV_ERR_ALG_UNSUPPORTED   = 0x04U,  /* requested alg_id not supported             */
    PROV_ERR_NO_CERT           = 0x05U,  /* cert slot empty / invalid                  */
    PROV_ERR_CRYPTO            = 0x06U,  /* key/CSR generation or signature failure    */
    PROV_ERR_NO_SLOT           = 0x07U,  /* slot_id out of range (>= num_slots)        */
    PROV_ERR_SLOT_TYPE         = 0x08U,  /* slot role mismatch                         */
    PROV_ERR_HASH_MISMATCH     = 0x09U,  /* streamed cert hash != declared hash        */
    PROV_ERR_LENGTH            = 0x0AU,  /* bad length / offset / overflow             */
    PROV_ERR_FLASH             = 0x0BU,  /* flash program / erase / ECC error          */
    PROV_ERR_CONFIRM           = 0x0CU,  /* missing / incorrect confirm constant       */
    PROV_ERR_PARAM             = 0x0DU,  /* malformed request payload                  */
    PROV_ERR_INTERNAL          = 0xFFU   /* unspecified secure-side fault              */
} prov_status_t;

/******************************************************************************
 * Data structures
 *****************************************************************************/

/**
 * Status snapshot returned by prov_nsc_get_status(). The non-secure router
 * serializes these fields into the PROV_GET_STATUS (0x50) response payload:
 *   prov_cmd_status(1) | provisioned(1) | num_slots(1) | pas_version(2 LE)
 *   | slot_type(1) * num_slots
 * All values are derived live from the freeze record and per-slot validity
 * predicates — nothing is read from a stored state value.
 */
typedef struct
{
    uint8_t  provisioned;                   /* 0 = NOT_PROVISIONED, 1 = PROVISIONED   */
    uint8_t  num_slots;                     /* number of configured PAS slots         */
    uint16_t pas_version;                   /* PROV_PAS_VERSION                       */
    uint8_t  slot_type[PROV_NUM_SLOTS];     /* prov_slot_type_t per slot              */
} prov_status_info_t;

/**
 * Per-algorithm capability descriptor used inside prov_info_report_t. The host
 * uses these to size its buffers without hard-coding any key/signature length.
 */
typedef struct
{
    uint16_t alg_id;        /* crypto suite id (e.g. PROV_ALG_ECDSA_P256)         */
    uint16_t pub_len;       /* public-key length in bytes (0 for symmetric)       */
    uint16_t sig_len;       /* signature length in bytes                          */
} prov_alg_info_t;

/**
 * Device UID / geometry / capability snapshot returned by
 * prov_nsc_get_info(). Serialized into the PROV_GET_INFO (0x51) response:
 *   prov_cmd_status(1) | uid(PROV_UID_LEN) | caps(2 LE) | num_slots(1)
 *   | slot_size(2 LE) * num_slots | n_algs(1)
 *   | { alg_id(2), pub_len(2), sig_len(2) } * n_algs | max_chunk(2 LE)
 */
typedef struct
{
    uint8_t         uid[PROV_UID_LEN];          /* device unique id               */
    uint16_t        caps;                       /* capability bit-field (0 today) */
    uint8_t         num_slots;                  /* configured PAS slots           */
    uint16_t        slot_size[PROV_NUM_SLOTS];  /* per-slot payload capacity (B)  */
    uint8_t         n_algs;                     /* number of advertised suites    */
    prov_alg_info_t algs[PROV_MAX_ALGS];        /* per-suite sizing               */
    uint16_t        max_chunk;                  /* PROV_MAX_CHUNK                  */
} prov_info_report_t;

/******************************************************************************
 * Wide-command argument blocks
 *
 * A cmse_nonsecure_entry veneer may only take arguments that fit in the four
 * core registers r0-r3 (the toolchain rejects any veneer whose arguments spill
 * onto the stack). Commands that need more inputs/outputs therefore pass a
 * single pointer to one of these argument blocks, which the non-secure caller
 * populates in non-secure memory. The veneer validates the block pointer and
 * every embedded buffer pointer before use.
 *****************************************************************************/

/** Arguments for prov_nsc_read_pubkey(). */
typedef struct
{
    uint8_t   slot_id;       /* in : key slot                                */
    uint16_t  offset;        /* in : byte offset into the public key         */
    uint8_t  *buf;           /* out: destination buffer                      */
    uint16_t  len;           /* in : bytes requested                         */
    uint16_t *alg_id_out;    /* out: slot alg_id                             */
    uint16_t *total_out;     /* out: full public-key length                  */
    uint16_t *ret_out;       /* out: bytes copied                            */
} prov_read_pubkey_args_t;

/** Arguments for prov_nsc_build_csr(). */
typedef struct
{
    uint8_t         slot_id;     /* in : key slot whose key signs the CSR    */
    const uint8_t  *templ;       /* in : subject template TLV (may be NULL)  */
    uint16_t        templ_len;   /* in : template length (0 => default CN)   */
    uint8_t        *out;         /* out: CSR destination buffer (non-secure) */
    uint16_t        out_cap;     /* in : capacity of out in bytes            */
    uint16_t       *total_out;   /* out: total CSR length written            */
} prov_build_csr_args_t;

/** Arguments for prov_nsc_cert_commit(). */
typedef struct
{
    uint8_t        slot_id;      /* in : target certificate slot             */
    uint8_t        key_slot;     /* in : bound key slot                      */
    const uint8_t *cert;         /* in : full certificate bytes              */
    uint32_t       total_len;    /* in : certificate length                  */
    const uint8_t *sha256;       /* in : declared SHA-256 (PROV_SHA256_LEN)  */
    uint16_t      *cert_len_out; /* out: stored certificate length           */
} prov_cert_commit_args_t;

/** Arguments for prov_nsc_read_cert(). */
typedef struct
{
    uint8_t   slot_id;       /* in : certificate slot                        */
    uint16_t  offset;        /* in : byte offset into the certificate        */
    uint8_t  *buf;           /* out: destination buffer                      */
    uint16_t  len;           /* in : bytes requested                         */
    uint16_t *total_out;     /* out: full certificate length                 */
    uint16_t *ret_out;       /* out: bytes copied                            */
} prov_read_cert_args_t;

/** Arguments for prov_nsc_prove_cert().
 *
 * The signature can be large (ML-DSA-87 is 4627 B), so it is written whole into
 * a non-secure staging buffer on the first (offset == 0) PROV_PROVE_CERT chunk
 * and streamed to the host from there; see prov_handle_prove_cert(). */
typedef struct
{
    uint8_t        slot_id;      /* in : certificate slot to prove           */
    const uint8_t *nonce;        /* in : host nonce (PROV_NONCE_LEN)         */
    uint8_t       *cert_sha256;  /* out: H = SHA256(cert) (PROV_SHA256_LEN)  */
    uint16_t      *alg_id_out;   /* out: signing suite id                    */
    uint8_t       *out;          /* out: signature destination (non-secure)  */
    uint16_t       out_cap;      /* in : capacity of out in bytes            */
    uint16_t      *sig_len_out;  /* out: total signature length written      */
} prov_prove_cert_args_t;

/******************************************************************************
 * NSC function prototypes
 *
 * These are implemented in the secure application as cmse_nonsecure_entry
 * veneers and called from the non-secure application. Each returns a
 * prov_status_t (PROV_OK on success). Every caller-supplied pointer is
 * validated with cmse_check_address_range() inside the secure world before
 * use, so a malicious non-secure caller cannot reach secure memory.
 *****************************************************************************/

/* ---- general (0x5X) ---- */

/**
 * @brief Return the current provisioning status (PROV_GET_STATUS / 0x50).
 *
 * Fills @p info with values derived live from the freeze record and per-slot
 * validity predicates. No stored state value is read.
 *
 * @param info  Non-secure buffer that receives the status snapshot.
 *
 * @return PROV_OK on success, PROV_ERR_PARAM if the buffer is invalid.
 */
prov_status_t prov_nsc_get_status(prov_status_info_t *info);

/**
 * @brief Return device UID, slot geometry and capabilities
 *        (PROV_GET_INFO / 0x51).
 * @param info  Non-secure buffer that receives the info report.
 * @return PROV_OK on success, PROV_ERR_PARAM if the buffer is invalid.
 */
prov_status_t prov_nsc_get_info(prov_info_report_t *info);

/**
 * @brief Erase one slot (or all slots) returning it to the free role
 *        (PROV_ERASE / 0x52). Rejected when frozen.
 * @param slot_id  Slot index, or PROV_SLOT_ALL for every slot.
 * @param confirm  Must equal PROV_CONFIRM_ERASE.
 * @return PROV_OK, PROV_ERR_CONFIRM, PROV_ERR_FROZEN, PROV_ERR_NO_SLOT,
 *         or PROV_ERR_FLASH.
 */
prov_status_t prov_nsc_erase(uint8_t slot_id, uint32_t confirm);

/**
 * @brief Freeze the store irreversibly after a structural pre-freeze check
 *        (PROV_FINALIZE / 0x53).
 * @param confirm          Must equal PROV_CONFIRM_FINALIZE.
 * @param provisioned_out  Receives the derived provisioned flag (1) on success.
 * @return PROV_OK, PROV_ERR_CONFIRM, PROV_ERR_NOT_READY, PROV_ERR_FROZEN,
 *         or PROV_ERR_FLASH.
 */
prov_status_t prov_nsc_finalize(uint32_t confirm, uint8_t *provisioned_out);

/* ---- key (0x6X) ---- */

/**
 * @brief Generate a device key into a slot (PROV_GEN_KEY / 0x60).
 * @param slot_id      Target key slot.
 * @param alg_id       Crypto suite to generate; one of the suites advertised by
 *                     PROV_GET_INFO for this build: PROV_ALG_ECDSA_P256 /
 *                     PROV_ALG_ECDSA_P384 / PROV_ALG_ECDSA_P521 (all devices) or
 *                     PROV_ALG_MLDSA_87 (M6/M8).
 * @param pub_len_out  Receives the public-key length in bytes (64 for P-256,
 *                     96 for P-384, 132 for P-521, 2592 for ML-DSA-87).
 * @return PROV_OK, PROV_ERR_FROZEN, PROV_ERR_NO_SLOT, PROV_ERR_ALG_UNSUPPORTED,
 *         PROV_ERR_SLOT_TYPE, PROV_ERR_CRYPTO, or PROV_ERR_FLASH.
 */
prov_status_t prov_nsc_gen_key(uint8_t slot_id, uint16_t alg_id,
                               uint16_t *pub_len_out);

/**
 * @brief Chunked read-back of a key slot's raw public key
 *        (PROV_READ_PUBKEY / 0x61).
 * @param args  Argument block (see prov_read_pubkey_args_t).
 * @return PROV_OK, PROV_ERR_NO_SLOT, PROV_ERR_NO_KEY, PROV_ERR_SLOT_TYPE,
 *         PROV_ERR_LENGTH, or PROV_ERR_PARAM.
 */
prov_status_t prov_nsc_read_pubkey(prov_read_pubkey_args_t *args);

/* ---- certificate & CSR (0x7X) ---- */

/**
 * @brief Build a PKCS#10 CSR on demand directly into a non-secure buffer
 *        (PROV_GET_CSR / 0x70, offset == 0).
 *
 * The CSR is public data (subject public key plus a proof-of-possession
 * signature); the private key never leaves secure RAM. The secure world
 * validates and writes the whole CSR into the caller-supplied non-secure
 * buffer in one call, so the caller can stream it out without any further
 * secure calls.
 *
 * @param args  Argument block (see prov_build_csr_args_t).
 * @return PROV_OK, PROV_ERR_NO_SLOT, PROV_ERR_NO_KEY, PROV_ERR_SLOT_TYPE,
 *         PROV_ERR_CRYPTO, PROV_ERR_PARAM, or PROV_ERR_LENGTH.
 */
prov_status_t prov_nsc_build_csr(prov_build_csr_args_t *args);

/**
 * @brief Commit a fully received certificate into a slot
 *        (PROV_CERT_WRITE final chunk / 0x71).
 * @param args  Argument block (see prov_cert_commit_args_t).
 * @return PROV_OK, PROV_ERR_FROZEN, PROV_ERR_NO_SLOT, PROV_ERR_SLOT_TYPE,
 *         PROV_ERR_NO_KEY, PROV_ERR_HASH_MISMATCH, PROV_ERR_LENGTH,
 *         PROV_ERR_FLASH, or PROV_ERR_PARAM.
 */
prov_status_t prov_nsc_cert_commit(prov_cert_commit_args_t *args);

/**
 * @brief Chunked read-back of a stored certificate (PROV_READ_CERT / 0x72).
 * @param args  Argument block (see prov_read_cert_args_t).
 * @return PROV_OK, PROV_ERR_NO_SLOT, PROV_ERR_NO_CERT, PROV_ERR_SLOT_TYPE,
 *         PROV_ERR_LENGTH, or PROV_ERR_PARAM.
 */
prov_status_t prov_nsc_read_cert(prov_read_cert_args_t *args);

/**
 * @brief Produce a signed certificate-integrity / key-binding proof
 *        (PROV_PROVE_CERT / 0x73). The device hashes and signs only; it never
 *        parses the X.509.
 * @param args  Argument block (see prov_prove_cert_args_t).
 * @return PROV_OK, PROV_ERR_NO_SLOT, PROV_ERR_NO_CERT, PROV_ERR_NO_KEY,
 *         PROV_ERR_SLOT_TYPE, PROV_ERR_CRYPTO, or PROV_ERR_PARAM.
 */
prov_status_t prov_nsc_prove_cert(prov_prove_cert_args_t *args);

#if defined(__cplusplus)
}
#endif

#endif /* PROV_NSC_H */

/* [] END OF FILE */
