/*****************************************************************************
 * File Name        : key_slot.h
 *
 * Description      : Serialisation helpers for the payload stored in a PAS key
 *                    slot. Keeps the on-flash key-slot layout in one place,
 *                    independent of both the PAS (which treats the payload as
 *                    opaque bytes) and the crypto layer.
 *
 *                    Key-slot payload layout (little-endian):
 *
 *                      offset 0 : pub_len      (uint16)  raw public-key length
 *                      offset 2 : wrapped_len  (uint16)  wrapped private length
 *                      offset 4 : pub[pub_len]           raw public key
 *                      offset 4+pub_len : wrapped[wrapped_len]  wrapped private
 *
 *                    The public key is stored in the clear (it is public); the
 *                    private key is AES-GCM wrapped by the crypto layer before
 *                    it ever reaches here.
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

#ifndef KEY_SLOT_H
#define KEY_SLOT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__cplusplus)
extern "C" {
#endif

/** Fixed header preceding the public/private blobs in a key-slot payload. */
#define KEY_SLOT_HDR_LEN    (4U)

/**
 * @brief Serialise a public key + wrapped private key into a slot payload.
 *
 * @param pub          Raw public key bytes.
 * @param pub_len      Public-key length.
 * @param wrapped      Wrapped private-key bytes.
 * @param wrapped_len  Wrapped private-key length.
 * @param out          Destination payload buffer.
 * @param out_cap      Capacity of @p out.
 * @param out_len      Receives the serialised payload length.
 * @return 0 on success, -1 on bad argument / overflow.
 */
static inline int key_slot_pack(const uint8_t *pub, uint16_t pub_len,
                                const uint8_t *wrapped, uint16_t wrapped_len,
                                uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t need = (size_t)KEY_SLOT_HDR_LEN + (size_t)pub_len +
                  (size_t)wrapped_len;

    if ((pub == NULL) || (wrapped == NULL) || (out == NULL) ||
        (out_len == NULL) || (out_cap < need))
    {
        return -1;
    }

    out[0] = (uint8_t)(pub_len & 0xFFU);
    out[1] = (uint8_t)((pub_len >> 8) & 0xFFU);
    out[2] = (uint8_t)(wrapped_len & 0xFFU);
    out[3] = (uint8_t)((wrapped_len >> 8) & 0xFFU);

    (void)memcpy(&out[KEY_SLOT_HDR_LEN], pub, pub_len);
    (void)memcpy(&out[KEY_SLOT_HDR_LEN + pub_len], wrapped, wrapped_len);

    *out_len = need;
    return 0;
}

/**
 * @brief Locate the public key inside a serialised key-slot payload.
 *
 * Returns pointers into @p payload (no copy). Validates that the declared
 * lengths fit within @p payload_len.
 *
 * @param payload      Serialised key-slot payload.
 * @param payload_len  Payload length.
 * @param pub_out      Receives a pointer to the public key (may be NULL).
 * @param pub_len_out  Receives the public-key length (may be NULL).
 * @return 0 on success, -1 on malformed payload.
 */
static inline int key_slot_get_pub(const uint8_t *payload, size_t payload_len,
                                   const uint8_t **pub_out,
                                   uint16_t *pub_len_out)
{
    uint16_t pub_len;
    uint16_t wrapped_len;

    if ((payload == NULL) || (payload_len < KEY_SLOT_HDR_LEN))
    {
        return -1;
    }

    pub_len = (uint16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8));
    wrapped_len = (uint16_t)((uint16_t)payload[2] | ((uint16_t)payload[3] << 8));

    if (((size_t)KEY_SLOT_HDR_LEN + (size_t)pub_len + (size_t)wrapped_len) >
        payload_len)
    {
        return -1;
    }

    if (pub_out != NULL)
    {
        *pub_out = &payload[KEY_SLOT_HDR_LEN];
    }
    if (pub_len_out != NULL)
    {
        *pub_len_out = pub_len;
    }
    return 0;
}

/**
 * @brief Locate the wrapped private key inside a serialised key-slot payload.
 *
 * Returns pointers into @p payload (no copy). Validates the declared lengths.
 *
 * @param payload          Serialised key-slot payload.
 * @param payload_len      Payload length.
 * @param wrapped_out      Receives a pointer to the wrapped key (may be NULL).
 * @param wrapped_len_out  Receives the wrapped-key length (may be NULL).
 * @return 0 on success, -1 on malformed payload.
 */
static inline int key_slot_get_wrapped(const uint8_t *payload,
                                       size_t payload_len,
                                       const uint8_t **wrapped_out,
                                       uint16_t *wrapped_len_out)
{
    uint16_t pub_len;
    uint16_t wrapped_len;

    if ((payload == NULL) || (payload_len < KEY_SLOT_HDR_LEN))
    {
        return -1;
    }

    pub_len = (uint16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8));
    wrapped_len = (uint16_t)((uint16_t)payload[2] | ((uint16_t)payload[3] << 8));

    if (((size_t)KEY_SLOT_HDR_LEN + (size_t)pub_len + (size_t)wrapped_len) >
        payload_len)
    {
        return -1;
    }

    if (wrapped_out != NULL)
    {
        *wrapped_out = &payload[KEY_SLOT_HDR_LEN + pub_len];
    }
    if (wrapped_len_out != NULL)
    {
        *wrapped_len_out = wrapped_len;
    }
    return 0;
}

#if defined(__cplusplus)
}
#endif

#endif /* KEY_SLOT_H */

/* [] END OF FILE */
