/*****************************************************************************
 * File Name        : pas.h
 *
 * Description      : Provisioning Asset Store (PAS) — public API.
 *
 *                    Generic, secure-only flash asset manager.  Knows nothing
 *                    about keys, certificates, X.509 or any specific asset
 *                    type.  Callers supply a slot index, a role tag
 *                    (slot_magic) and an opaque payload; the PAS guarantees
 *                    CRC integrity, role tagging, per-slot erase/rewrite and
 *                    an irreversible lock (freeze).
 *
 *                    Deployment geometry (base address, region size, per-slot
 *                    row counts) is in pas_config.h.
 *
 * Related Document : PAS-Design.md
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

#ifndef PAS_H
#define PAS_H

#include <stdint.h>
#include <stdbool.h>

#include "pas_config.h"

#if defined(__cplusplus)
extern "C" {
#endif

/******************************************************************************
 * Flash geometry constants
 *****************************************************************************/

/** Minimum erase/program unit on PSoC Control C3 (512 B). */
#define PAS_FLASH_ROW_SIZE  512u

/** PAS on-flash structure version.  Bump when layout changes. */
#define PAS_VERSION         0x0003u

/** Derived region size in bytes. */
#define PAS_REGION_SIZE     ((uint32_t)PAS_REGION_ROWS * PAS_FLASH_ROW_SIZE)

/******************************************************************************
 * Slot magic values  (set the role of a slot)
 *****************************************************************************/

/** Slot is erased / unassigned (flash-erase state reads as 0x00 on this device). */
#define SLOT_MAGIC_FREE     0x00000000u
/** Slot holds a key pair or symmetric key ("_KEY"). */
#define SLOT_MAGIC_KEY      0x5F4B4559u
/** Slot holds a certificate ("CERT"). */
#define SLOT_MAGIC_CERT     0x43455254u

/** assoc_slot value when there is no binding to another slot. */
#define SLOT_ASSOC_NONE     0xFFu

/******************************************************************************
 * Enumerations
 *****************************************************************************/

/** Runtime role of a slot, derived from slot_magic. */
typedef enum {
    SLOT_TYPE_FREE = 0x00,  /**< Unassigned (erased)                          */
    SLOT_TYPE_KEY  = 0x01,  /**< Holds a key pair (or symmetric key)          */
    SLOT_TYPE_CERT = 0x02   /**< Holds a certificate, bound to a key slot     */
} slot_type_t;

/** PAS manager status codes. */
typedef enum {
    PAS_OK           = 0,   /**< Success                                      */
    PAS_ERR_FROZEN,         /**< Store locked; mutating call rejected          */
    PAS_ERR_NO_SLOT,        /**< Slot index out of range                      */
    PAS_ERR_SLOT_TYPE,      /**< Role mismatch / bad association              */
    PAS_ERR_CRC,            /**< Slot / header / freeze CRC mismatch         */
    PAS_ERR_LENGTH,         /**< Payload/offset exceeds slot capacity         */
    PAS_ERR_FLASH,          /**< Program / erase / ECC error                  */
    PAS_ERR_PARAM           /**< Malformed argument                           */
} pas_status_t;

/******************************************************************************
 * On-flash structures
 *****************************************************************************/

/**
 * Common header at the start of every generic slot (row-aligned within the
 * slot's allocated rows).  A slot is PRESENT/VALID iff slot_magic is a known
 * type AND crc32 verifies over the first 16 bytes of this struct + payload.
 */
typedef struct {
    uint32_t slot_magic;    /**< SLOT_MAGIC_KEY | _CERT | _FREE               */
    uint16_t pas_version;   /**< PAS_VERSION                                  */
    uint8_t  slot_id;       /**< This slot's index (0..PAS_NUM_SLOTS-1)       */
    uint8_t  assoc_slot;    /**< Bound slot id (e.g. cert->key); SLOT_ASSOC_NONE */
    uint16_t alg_id;        /**< Caller tag (crypto suite id); 0 if unused    */
    uint16_t reserved;      /**< Must be 0                                    */
    uint32_t payload_len;   /**< Bytes of payload following this header       */
    uint32_t crc32;         /**< CRC-32 over bytes[0..15] + payload           */
} slot_hdr_t;

/**
 * Store header — occupies region row 0.  Contains no provisioning-state
 * field; progress is inferred from slot presence + the freeze record.
 */
typedef struct {
    uint32_t magic;             /**< PAS_HEADER_MAGIC                         */
    uint16_t pas_version;       /**< PAS_VERSION                              */
    uint16_t reserved0;         /**< Must be 0                                */
    uint16_t write_attempts;    /**< Anti-abuse counter: slot-write calls      */
    uint16_t erase_attempts;    /**< Anti-abuse counter: slot-erase calls      */
    uint32_t reserved1;         /**< Must be 0                                */
    uint32_t header_crc32;      /**< CRC-32 over bytes[0..15] of this struct  */
} pas_header_t;

/**
 * Freeze record — occupies region row 1 in isolation so the write is atomic.
 * The store is locked iff ALL of: magic_lo/hi match, inv fields match, CRC ok.
 */
typedef struct {
    uint32_t magic_lo;          /**< PAS_FREEZE_MAGIC_LO                      */
    uint32_t magic_hi;          /**< PAS_FREEZE_MAGIC_HI                      */
    uint32_t magic_lo_inv;      /**< ~PAS_FREEZE_MAGIC_LO (redundancy)        */
    uint32_t magic_hi_inv;      /**< ~PAS_FREEZE_MAGIC_HI (redundancy)        */
    uint32_t frozen_at_uid;     /**< Optional station/UID stamp (0 = unused)  */
    uint32_t crc32;             /**< CRC-32 over bytes[0..19] of this struct  */
} pas_freeze_record_t;

/** Magic constants for the store header and freeze record. */
#define PAS_HEADER_MAGIC        0x53504456u  /**< "SPDV"                      */
#define PAS_FREEZE_MAGIC_LO     0xF00DBEEFu
#define PAS_FREEZE_MAGIC_HI     0xC3A55A3Cu

/******************************************************************************
 * Runtime slot descriptor (not stored on flash)
 *****************************************************************************/

/**
 * Filled by pas_slot_query().  Captures the slot role, binding and validity
 * derived live from the slot_hdr_t in flash (never from a stored state value).
 */
typedef struct {
    slot_type_t type;       /**< FREE / KEY / CERT (from slot_magic)          */
    uint8_t     slot_id;    /**< Slot index                                   */
    uint8_t     assoc_slot; /**< Binding (e.g. cert->key); SLOT_ASSOC_NONE    */
    uint16_t    alg_id;     /**< Caller tag                                   */
    uint32_t    payload_len;/**< Stored payload length                        */
    bool        valid;      /**< magic present AND crc32 verifies             */
} pas_slot_info_t;

/******************************************************************************
 * PAS manager API
 *****************************************************************************/

/**
 * @brief Initialize the PAS module.
 *
 * Validates the build-time geometry (base address alignment, row budget) and
 * the on-flash store header (magic + CRC).  If the region is erased
 * (fresh device), writes the initial header.
 *
 * Must be called once from secure main() before any other PAS function.
 *
 * @return PAS_OK on success.
 *         PAS_ERR_PARAM  if PAS_BASE_ADDR is not row-aligned or the geometry
 *                        budget is exceeded (build-time configuration error).
 *         PAS_ERR_CRC    if a non-erased header has an invalid CRC.
 *         PAS_ERR_FLASH  if the initial header write fails on a fresh device.
 */
pas_status_t pas_init(void);

/**
 * @brief Return the number of configured slots (resolved PAS_NUM_SLOTS).
 *
 * @return Number of slots (always >= 1).
 */
uint32_t pas_slot_count(void);

/**
 * @brief Return the size in bytes of an individual slot.
 *
 * @param slot  Slot index (0..pas_slot_count()-1).
 * @return Slot size in bytes, or 0 if slot is out of range.
 */
uint32_t pas_slot_size(uint8_t slot);

/**
 * @brief Query a slot's current role and validity without reading payload.
 *
 * All fields are derived live from the slot_hdr_t in flash; no stored state
 * is consulted.
 *
 * @param slot  Slot index.
 * @param out   Filled with the slot descriptor on success.
 * @return PAS_OK, PAS_ERR_NO_SLOT, or PAS_ERR_PARAM.
 */
pas_status_t pas_slot_query(uint8_t slot, pas_slot_info_t *out);

/**
 * @brief Read bytes from the payload of a slot.
 *
 * The slot must be PRESENT/VALID (pas_slot_info_t.valid == true).
 * Offset is relative to the start of the payload (i.e. after slot_hdr_t).
 *
 * @param slot  Slot index.
 * @param off   Byte offset into the payload.
 * @param buf   Destination buffer.
 * @param len   Maximum bytes to read.
 * @param ret   Actual bytes copied (may be less than len near end of payload).
 * @return PAS_OK, PAS_ERR_NO_SLOT, PAS_ERR_SLOT_TYPE (slot empty/CRC bad),
 *         PAS_ERR_LENGTH (offset >= payload_len), or PAS_ERR_PARAM.
 */
pas_status_t pas_slot_read(uint8_t slot, uint32_t off,
                           void *buf, uint32_t len, uint32_t *ret);

/**
 * @brief Erase the current slot contents and write new header + payload.
 *
 * Computes the CRC over the header fields and the payload, then programs the
 * slot rows.  The slot role is set by @p magic (SLOT_MAGIC_KEY or
 * SLOT_MAGIC_CERT).  Rejected if the store is frozen.
 *
 * @param slot       Slot index.
 * @param magic      SLOT_MAGIC_KEY or SLOT_MAGIC_CERT.
 * @param alg_id     Caller tag (e.g. crypto suite); 0 if unused.
 * @param assoc_slot Bound slot id (SLOT_ASSOC_NONE if no binding).
 * @param payload    Opaque payload bytes (may be NULL if len == 0).
 * @param len        Payload length in bytes.
 * @return PAS_OK, PAS_ERR_FROZEN, PAS_ERR_NO_SLOT, PAS_ERR_LENGTH,
 *         PAS_ERR_PARAM, or PAS_ERR_FLASH.
 */
pas_status_t pas_slot_write(uint8_t slot, uint32_t magic, uint16_t alg_id,
                            uint8_t assoc_slot,
                            const void *payload, uint32_t len);

/**
 * @brief Erase a slot, returning it to the FREE role.
 *
 * Rejected if the store is frozen.
 *
 * @param slot  Slot index.
 * @return PAS_OK, PAS_ERR_FROZEN, PAS_ERR_NO_SLOT, or PAS_ERR_FLASH.
 */
pas_status_t pas_slot_erase(uint8_t slot);

/**
 * @brief Return true iff the store is locked (freeze record is valid).
 *
 * Derived live from the freeze record in flash — no stored state is read.
 *
 * @return true if frozen, false otherwise.
 */
bool pas_is_frozen(void);

/**
 * @brief Lock the store irreversibly.
 *
 * Writes the freeze record to region row 1.  After this call:
 *   - pas_is_frozen() returns true.
 *   - All pas_slot_write() / pas_slot_erase() / pas_freeze() calls return
 *     PAS_ERR_FROZEN.
 *   - Read/query operations continue to work.
 *
 * Also reconfigures the MPC to make all slot rows secure read-only (see
 * implementation note in pas.c).
 *
 * @return PAS_OK, PAS_ERR_FROZEN (already frozen), or PAS_ERR_FLASH.
 */
pas_status_t pas_freeze(void);

#if defined(__cplusplus)
}
#endif

#endif /* PAS_H */

/* [] END OF FILE */
