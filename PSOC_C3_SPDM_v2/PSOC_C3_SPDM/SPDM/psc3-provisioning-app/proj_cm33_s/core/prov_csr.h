/*****************************************************************************
 * File Name        : prov_csr.h
 *
 * Description      : PKCS#10 Certificate Signing Request (CSR) builder for the
 *                    secure provisioning service.
 *
 *                    Builds a DER-encoded CSR for a device key held (wrapped)
 *                    in a PAS key slot. The private key is imported into a
 *                    volatile PSA key and presented to mbedTLS as an opaque PK,
 *                    so the CSR is signed inside the secure world and the key
 *                    is never exported in the clear.
 *
 *                    The subject is described by a compact TLV template
 *                    (see prov_csr.c); when no template is supplied the
 *                    subject defaults to CN = hex(device UID).
 *
 * Related Document : docs/design_and_implementation.md
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

#ifndef PROV_CSR_H
#define PROV_CSR_H

#include <stddef.h>
#include <stdint.h>

#include "prov_crypto.h"   /* prov_ec_curve_t for the EC CSR builder */

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Subject-template field identifiers (1-byte tag in the TLV blob).
 */
typedef enum
{
    PROV_CSR_FIELD_CN     = 0x01U,  /**< commonName            */
    PROV_CSR_FIELD_O      = 0x02U,  /**< organizationName      */
    PROV_CSR_FIELD_OU     = 0x03U,  /**< organizationalUnit    */
    PROV_CSR_FIELD_C      = 0x04U,  /**< countryName           */
    PROV_CSR_FIELD_SERIAL = 0x05U   /**< serialNumber          */
} prov_csr_field_t;

/**
 * @brief Build a DER-encoded PKCS#10 CSR for a wrapped EC device key.
 *
 * Single curve-parameterised CSR builder for P-256 / P-384 / P-521. The private
 * key is imported via prov_crypto_import_priv_ec() and the CSR message digest
 * is paired with the curve strength (P-256 -> SHA-256, P-384 -> SHA-384,
 * P-521 -> SHA-512).
 *
 * @param curve        Curve selector (PROV_EC_CURVE_P256 / _P384 / _P521).
 * @param wrapped      Wrapped private key (from the key slot).
 * @param wrapped_len  Wrapped private-key length.
 * @param templ        Subject-template TLV blob; may be NULL.
 * @param templ_len    Template length (0 => default CN = hex(UID)).
 * @param uid          Device unique id (for the default CN).
 * @param uid_len      Device UID length.
 * @param out          Destination buffer for the DER CSR.
 * @param out_cap      Capacity of @p out.
 * @param out_len      Receives the CSR length.
 * @return 0 on success, negative on failure.
 */
#if defined(PROV_ENABLE_EC256) || defined(PROV_ENABLE_EC384) || \
    defined(PROV_ENABLE_EC521)
int prov_csr_build_ec(prov_ec_curve_t curve,
                      const uint8_t *wrapped, size_t wrapped_len,
                      const uint8_t *templ, size_t templ_len,
                      const uint8_t *uid, size_t uid_len,
                      uint8_t *out, size_t out_cap, size_t *out_len);
#endif /* PROV_ENABLE_EC256 || PROV_ENABLE_EC384 || PROV_ENABLE_EC521 */

#if defined(PROV_ENABLE_MLDSA87)
/**
 * @brief Build a DER-encoded PKCS#10 CSR for an ML-DSA-87 device key.
 *
 * wolfCrypt is built with WOLFSSL_DILITHIUM_NO_ASN1, so mbedTLS/wolfSSL cannot
 * assemble the request for us. This routine hand-builds the DER: the
 * CertificationRequestInfo (TBS) is written back-to-front into @p out, then the
 * signatureAlgorithm and an empty-header BIT STRING are appended, the TBS is
 * signed in place with prov_crypto_sign_mldsa87(), and finally the outer
 * SEQUENCE header is prepended. The raw public key is embedded verbatim in the
 * SubjectPublicKeyInfo.
 *
 * @param wrapped      Wrapped private key (from the key slot).
 * @param wrapped_len  Wrapped private-key length.
 * @param pub          Raw ML-DSA-87 public key.
 * @param pub_len      Public-key length (PROV_PUB_LEN_MLDSA87).
 * @param templ        Subject-template TLV blob; may be NULL.
 * @param templ_len    Template length (0 => default CN = hex(UID)).
 * @param uid          Device unique id (for the default CN).
 * @param uid_len      Device UID length.
 * @param out          Destination buffer for the DER CSR.
 * @param out_cap      Capacity of @p out.
 * @param out_len      Receives the CSR length.
 * @return 0 on success, negative on failure.
 */
int prov_csr_build_mldsa87(const uint8_t *wrapped, size_t wrapped_len,
                           const uint8_t *pub, size_t pub_len,
                           const uint8_t *templ, size_t templ_len,
                           const uint8_t *uid, size_t uid_len,
                           uint8_t *out, size_t out_cap, size_t *out_len);
#endif /* PROV_ENABLE_MLDSA87 */

#if defined(__cplusplus)
}
#endif

#endif /* PROV_CSR_H */

/* [] END OF FILE */
