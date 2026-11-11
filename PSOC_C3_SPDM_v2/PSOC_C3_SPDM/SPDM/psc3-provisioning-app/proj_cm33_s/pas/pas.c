/*****************************************************************************
 * File Name        : pas.c
 *
 * Description      : Provisioning Asset Store (PAS) — implementation.
 *
 *                    Manages row-aligned, CRC-protected, typed slots in
 *                    secure flash plus an atomic store-lock (freeze).
 *                    Asset-agnostic: callers choose slot_magic and payload.
 *
 *                    Flash operations use the ModusToolbox PDL flash driver:
 *                      Cy_Flash_WriteRow(addr, data)   — erase+program one 512-B row
 *                      Cy_Flash_EraseRow(addr)         — erases one 512-B row
 *                    Reads are direct dereferences of the secure S-bus alias
 *                    (PAS_BASE_ADDR).
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

/******************************************************************************
 * Header Files
 *****************************************************************************/

#include "pas.h"

#include <stddef.h>
#include <string.h>

#include "cy_flash.h"
#include "mtb_hal.h"
#include "ppc_config.h"

/******************************************************************************
 * Internal macros
 *****************************************************************************/

/** Minimum of two values. */
#define PAS_MIN(a, b)   ((a) < (b) ? (a) : (b))

/** Maximum number of slots this version of the module supports
 *  (bounds s_slot_start_row[]). Fixed property — not user-configurable. */
#define PAS_SLOTS_MAX   8u

/** Row offset of the header within the PAS region. */
#define PAS_HDR_ROW     0u
/** Row offset of the freeze record within the PAS region. */
#define PAS_FREEZE_ROW  1u
/** First row available for slots. */
#define PAS_FIRST_SLOT_ROW  2u

/******************************************************************************
 * Slot row table (derived from PAS_SLOT_ROWS_INIT at compile time)
 *****************************************************************************/

/* Per-slot row counts, one entry per slot.  Sized at compile time by the
 * deployment configuration.  PAS_NUM_SLOTS is derived from the array length. */
static const uint8_t pas_slot_rows[] = PAS_SLOT_ROWS_INIT;

#define PAS_NUM_SLOTS   ((uint8_t)(sizeof(pas_slot_rows) / sizeof(pas_slot_rows[0])))

/* Build-time enforcement of the fixed slot limit: this PAS version supports at
 * most PAS_SLOTS_MAX (8) slots. More entries in PAS_SLOT_ROWS_INIT would
 * overflow s_slot_start_row[] and exceed the slot-id range used by the
 * provisioning command transactions. */
_Static_assert(PAS_NUM_SLOTS <= PAS_SLOTS_MAX,
               "PAS: PAS_SLOT_ROWS_INIT defines more slots than this PAS "
               "version supports (PAS_SLOTS_MAX = 8).");

/* Derived: PAS_SLOT_SIZE(i) = number of bytes in slot i. */
#define PAS_SLOT_SIZE(i)  ((uint32_t)pas_slot_rows[(i)] * PAS_FLASH_ROW_SIZE)

/******************************************************************************
 * Module state
 *****************************************************************************/

/* Precomputed start-row index for each slot (row relative to PAS_BASE_ADDR).
 * Populated by pas_init(). */
static uint8_t  s_slot_start_row[PAS_SLOTS_MAX];

/* Single 512-byte work buffer for row preparation / header updates.
 * Static to avoid stack pressure in the secure world. */
static uint8_t  s_row_buf[PAS_FLASH_ROW_SIZE];

/******************************************************************************
 * CRC-32 (ISO-HDLC / IEEE 802.3, poly 0xEDB88320)
 *****************************************************************************/

static uint32_t crc32_step(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len--)
    {
        uint8_t byte = *p++;
        crc ^= (uint32_t)byte;
        for (int i = 0; i < 8; i++)
        {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc;
}

/** Compute CRC-32 over a single contiguous block. */
static uint32_t crc32_compute(const void *data, uint32_t len)
{
    return crc32_step(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

/** Compute CRC-32 over two non-contiguous blocks (header fields + payload). */
static uint32_t crc32_compute2(const void *a, uint32_t alen,
                               const void *b, uint32_t blen)
{
    uint32_t ctx = crc32_step(0xFFFFFFFFu, a, alen);
    ctx = crc32_step(ctx, b, blen);
    return ctx ^ 0xFFFFFFFFu;
}

/******************************************************************************
 * Address helpers
 *****************************************************************************/

/** Byte address of the PAS region row at index @p row. */
static inline uint32_t pas_row_addr(uint8_t row)
{
    return PAS_BASE_ADDR + ((uint32_t)row * PAS_FLASH_ROW_SIZE);
}

/** Byte address of the start of slot @p slot (first byte of slot_hdr_t). */
static inline uint32_t pas_slot_addr(uint8_t slot)
{
    return pas_row_addr(s_slot_start_row[slot]);
}

/******************************************************************************
 * Flash abstraction
 *****************************************************************************/

/**
 * Make the flash controller secure-accessible for the duration of a PAS
 * mutating operation.  The PPC partitioning established at start-up keeps
 * FLASHC (PROT_CPUSS) non-secure; the secure world must explicitly claim it
 * before any erase/program and release it afterwards.  Bracketing whole
 * operations (rather than individual rows) keeps the flash controller secure
 * across multi-row writes/erases.
 *
 * @return PAS_OK if the controller is now secure, PAS_ERR_FLASH otherwise.
 */
static pas_status_t pas_flash_access_begin(void)
{
    return (config_ppc_flash_enable() == CY_PPC_SUCCESS) ? PAS_OK : PAS_ERR_FLASH;
}

/** Return the flash controller to non-secure after a mutating operation. */
static void pas_flash_access_end(void)
{
    (void)config_ppc_flash_disable();
}

/** Program one 512-byte row.  @p addr must be PAS_FLASH_ROW_SIZE-aligned.
 *  Cy_Flash_WriteRow performs pre-program, erase and program in one blocking
 *  call, so the row need not be erased beforehand. */
static pas_status_t pas_flash_program_row(uint32_t addr,
                                          const uint8_t data[PAS_FLASH_ROW_SIZE])
{
    uint32_t istate = mtb_hal_system_critical_section_enter();
    cy_en_flashdrv_status_t st =
        Cy_Flash_WriteRow(addr, (const uint32_t *)(const void *)data);
    mtb_hal_system_critical_section_exit(istate);
    return (st == CY_FLASH_DRV_SUCCESS) ? PAS_OK : PAS_ERR_FLASH;
}

/** Erase one 512-byte row to the blank state (all-0x00).  @p addr must be row-aligned. */
static pas_status_t pas_flash_erase_row(uint32_t addr)
{
    uint32_t istate = mtb_hal_system_critical_section_enter();
    cy_en_flashdrv_status_t st = Cy_Flash_EraseRow(addr);
    mtb_hal_system_critical_section_exit(istate);
    return (st == CY_FLASH_DRV_SUCCESS) ? PAS_OK : PAS_ERR_FLASH;
}

/******************************************************************************
 * Store header helpers
 *****************************************************************************/

static inline const pas_header_t *pas_header_ptr(void)
{
    return (const pas_header_t *)PAS_BASE_ADDR;
}

/** Return true iff all bytes of the header row are 0x00 (fresh/erased). */
static bool pas_header_is_erased(void)
{
    const uint8_t *p = (const uint8_t *)PAS_BASE_ADDR;
    for (uint32_t i = 0; i < PAS_FLASH_ROW_SIZE; i++)
    {
        if (p[i] != 0x00u) { return false; }
    }
    return true;
}

/** Return true iff the header has a valid magic and CRC. */
static bool pas_header_is_valid(void)
{
    const pas_header_t *hdr = pas_header_ptr();
    if (hdr->magic != PAS_HEADER_MAGIC) { return false; }
    uint32_t crc = crc32_compute(hdr, offsetof(pas_header_t, header_crc32));
    return (crc == hdr->header_crc32);
}

/** Write a fresh header to row 0 (called on a blank device). */
static pas_status_t pas_header_write_initial(void)
{
    memset(s_row_buf, 0x00u, sizeof(s_row_buf));
    pas_header_t *hdr = (pas_header_t *)s_row_buf;
    hdr->magic           = PAS_HEADER_MAGIC;
    hdr->pas_version     = PAS_VERSION;
    hdr->reserved0       = 0u;
    hdr->write_attempts  = 0u;
    hdr->erase_attempts  = 0u;
    hdr->reserved1       = 0u;
    hdr->header_crc32    = crc32_compute(hdr, offsetof(pas_header_t, header_crc32));
    return pas_flash_program_row(PAS_BASE_ADDR, s_row_buf);
}

/**
 * Increment the header write or erase counter.
 *
 * Performs a read-modify-write on the header row.  Failures are non-fatal —
 * the counter is best-effort traceability data.
 *
 * @param is_write  true = increment write_attempts; false = erase_attempts.
 */
static void pas_header_increment_counter(bool is_write)
{
    if (!pas_header_is_valid()) { return; }   /* skip if header is corrupted */

    /* Read current header row into the work buffer. */
    memcpy(s_row_buf, (const void *)PAS_BASE_ADDR, PAS_FLASH_ROW_SIZE);
    pas_header_t *hdr = (pas_header_t *)s_row_buf;

    if (is_write)
    {
        if (hdr->write_attempts < 0xFFFFu) { hdr->write_attempts++; }
    }
    else
    {
        if (hdr->erase_attempts < 0xFFFFu) { hdr->erase_attempts++; }
    }
    hdr->header_crc32 = crc32_compute(hdr, offsetof(pas_header_t, header_crc32));

    /* Re-program the header row (Cy_Flash_WriteRow erases first). */
    (void)pas_flash_program_row(PAS_BASE_ADDR, s_row_buf);
}

/******************************************************************************
 * Freeze record helpers
 *****************************************************************************/

static inline const pas_freeze_record_t *pas_freeze_record_ptr(void)
{
    return (const pas_freeze_record_t *)pas_row_addr(PAS_FREEZE_ROW);
}

/** Return true iff the freeze record is valid (all magic, complement, CRC check). */
static bool pas_freeze_record_is_valid(void)
{
    const pas_freeze_record_t *fr = pas_freeze_record_ptr();

    if (fr->magic_lo     != PAS_FREEZE_MAGIC_LO)    { return false; }
    if (fr->magic_hi     != PAS_FREEZE_MAGIC_HI)    { return false; }
    if (fr->magic_lo_inv != (~PAS_FREEZE_MAGIC_LO)) { return false; }
    if (fr->magic_hi_inv != (~PAS_FREEZE_MAGIC_HI)) { return false; }

    uint32_t crc = crc32_compute(fr, offsetof(pas_freeze_record_t, crc32));
    return (crc == fr->crc32);
}

/******************************************************************************
 * Slot validity predicate (internal)
 *****************************************************************************/

/**
 * Return the slot's type if its header is present and its CRC verifies,
 * otherwise SLOT_TYPE_FREE.
 *
 * The CRC covers:
 *   bytes[0..offsetof(slot_hdr_t,crc32)-1]  (16 bytes of header fields)
 *   followed by the payload bytes            (read directly from flash)
 */
static slot_type_t pas_slot_get_type(uint8_t slot)
{
    const slot_hdr_t *hdr = (const slot_hdr_t *)pas_slot_addr(slot);

    /* Erased slot. */
    if (hdr->slot_magic == SLOT_MAGIC_FREE) { return SLOT_TYPE_FREE; }

    /* Determine expected type from magic. */
    slot_type_t type;
    if      (hdr->slot_magic == SLOT_MAGIC_KEY)  { type = SLOT_TYPE_KEY;  }
    else if (hdr->slot_magic == SLOT_MAGIC_CERT) { type = SLOT_TYPE_CERT; }
    else { return SLOT_TYPE_FREE; }   /* unknown magic */

    /* Validate payload length fits within slot. */
    uint32_t capacity = PAS_SLOT_SIZE(slot);
    if (hdr->payload_len > capacity - sizeof(slot_hdr_t)) { return SLOT_TYPE_FREE; }

    /* Verify CRC over header prefix + payload (both read directly from flash). */
    const uint8_t *payload = (const uint8_t *)hdr + sizeof(slot_hdr_t);
    uint32_t crc = crc32_compute2(hdr, offsetof(slot_hdr_t, crc32),
                                  payload, hdr->payload_len);
    if (crc != hdr->crc32) { return SLOT_TYPE_FREE; }

    return type;
}

/******************************************************************************
 * Public API — pas_init
 *****************************************************************************/

pas_status_t pas_init(void)
{
    /* ------------------------------------------------------------------
     * 0. Initialise the PDL flash driver.
     *
     * Cy_Flash_Init() populates the flash driver's boot-ROM context
     * (callback pointers) and registers the flash IRQ. It must run once
     * before any Cy_Flash_WriteRow/EraseRow call. Refresh (wear-levelling)
     * is left disabled — the PAS store is small and not written often
     * enough to need it.
     * ------------------------------------------------------------------ */
    if (Cy_Flash_Init(false) != CY_FLASH_DRV_SUCCESS)
    {
        return PAS_ERR_FLASH;
    }

    /* ------------------------------------------------------------------
     * 1. Geometry validation (build-time configuration check).
     * ------------------------------------------------------------------ */
    if ((PAS_BASE_ADDR % PAS_FLASH_ROW_SIZE) != 0u)
    {
        return PAS_ERR_PARAM;   /* base address not row-aligned */
    }

    /* Verify: 2 (header + freeze) + sum(pas_slot_rows) <= PAS_REGION_ROWS. */
    uint32_t total_rows = PAS_FIRST_SLOT_ROW;
    for (uint8_t i = 0u; i < PAS_NUM_SLOTS; i++)
    {
        total_rows += pas_slot_rows[i];
    }
    if (total_rows > PAS_REGION_ROWS)
    {
        return PAS_ERR_PARAM;   /* slot row budget exceeds region size */
    }

    /* ------------------------------------------------------------------
     * 2. Build the per-slot start-row table (prefix sum).
     * ------------------------------------------------------------------ */
    uint8_t row = PAS_FIRST_SLOT_ROW;
    for (uint8_t i = 0u; i < PAS_NUM_SLOTS; i++)
    {
        s_slot_start_row[i] = row;
        row = (uint8_t)(row + pas_slot_rows[i]);
    }

    /* ------------------------------------------------------------------
     * 3. Initialise or validate the store header (region row 0).
     * ------------------------------------------------------------------ */
    if (pas_header_is_erased())
    {
        /* Fresh device: program the initial header. */
        pas_status_t st = pas_flash_access_begin();
        if (st == PAS_OK)
        {
            st = pas_header_write_initial();
            pas_flash_access_end();
        }
        if (st != PAS_OK) { return st; }
    }
    else if (!pas_header_is_valid())
    {
        /* Header present but CRC/magic mismatch — store corrupted. */
        return PAS_ERR_CRC;
    }

    return PAS_OK;
}

/******************************************************************************
 * Public API — geometry queries
 *****************************************************************************/

uint32_t pas_slot_count(void)
{
    return (uint32_t)PAS_NUM_SLOTS;
}

uint32_t pas_slot_size(uint8_t slot)
{
    if (slot >= PAS_NUM_SLOTS) { return 0u; }
    return PAS_SLOT_SIZE(slot);
}

/******************************************************************************
 * Public API — slot query
 *****************************************************************************/

pas_status_t pas_slot_query(uint8_t slot, pas_slot_info_t *out)
{
    if (slot >= PAS_NUM_SLOTS) { return PAS_ERR_NO_SLOT; }
    if (out == NULL)           { return PAS_ERR_PARAM;   }

    slot_type_t type = pas_slot_get_type(slot);

    /* Read the slot header directly from flash. */
    const slot_hdr_t *hdr = (const slot_hdr_t *)pas_slot_addr(slot);
    bool valid = (type != SLOT_TYPE_FREE) && (hdr->slot_magic != SLOT_MAGIC_FREE);

    out->type        = type;
    out->slot_id     = slot;
    out->assoc_slot  = valid ? hdr->assoc_slot   : SLOT_ASSOC_NONE;
    out->alg_id      = valid ? hdr->alg_id       : 0u;
    out->payload_len = valid ? hdr->payload_len  : 0u;
    out->valid       = valid;

    return PAS_OK;
}

/******************************************************************************
 * Public API — slot read
 *****************************************************************************/

pas_status_t pas_slot_read(uint8_t slot, uint32_t off,
                           void *buf, uint32_t len, uint32_t *ret)
{
    if (slot >= PAS_NUM_SLOTS)            { return PAS_ERR_NO_SLOT; }
    if (buf == NULL || ret == NULL)       { return PAS_ERR_PARAM;   }

    slot_type_t type = pas_slot_get_type(slot);
    if (type == SLOT_TYPE_FREE)           { return PAS_ERR_SLOT_TYPE; }

    /* Read the slot header directly from flash. */
    const slot_hdr_t *hdr = (const slot_hdr_t *)pas_slot_addr(slot);
    uint32_t payload_len = hdr->payload_len;

    if (off >= payload_len)               { return PAS_ERR_LENGTH;  }

    uint32_t available = payload_len - off;
    uint32_t to_read   = PAS_MIN(len, available);

    const uint8_t *payload =
        (const uint8_t *)hdr + sizeof(slot_hdr_t) + off;
    memcpy(buf, payload, to_read);
    *ret = to_read;

    return PAS_OK;
}

/******************************************************************************
 * Public API — slot write
 *****************************************************************************/

pas_status_t pas_slot_write(uint8_t slot, uint32_t magic, uint16_t alg_id,
                            uint8_t assoc_slot,
                            const void *payload, uint32_t len)
{
    if (pas_is_frozen())        { return PAS_ERR_FROZEN;  }
    if (slot >= PAS_NUM_SLOTS)  { return PAS_ERR_NO_SLOT; }
    if (magic == SLOT_MAGIC_FREE || (magic != SLOT_MAGIC_KEY && magic != SLOT_MAGIC_CERT))
    {
        return PAS_ERR_PARAM;
    }
    if (payload == NULL || len == 0u) { return PAS_ERR_PARAM; }

    /* Validate payload fits in the slot (must leave room for the header). */
    uint32_t capacity = PAS_SLOT_SIZE(slot);
    if (len > capacity - sizeof(slot_hdr_t)) { return PAS_ERR_LENGTH; }

    /* ------------------------------------------------------------------
     * 1. Build the slot header in SRAM; compute CRC over header + payload.
     * ------------------------------------------------------------------ */
    slot_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.slot_magic  = magic;
    hdr.pas_version = PAS_VERSION;
    hdr.slot_id     = slot;
    hdr.assoc_slot  = assoc_slot;
    hdr.alg_id      = alg_id;
    hdr.reserved    = 0u;
    hdr.payload_len = len;
    hdr.crc32       = crc32_compute2(&hdr, offsetof(slot_hdr_t, crc32),
                                     payload, len);

    /* ------------------------------------------------------------------
     * 2. Program slot rows: first row = header + up to 492 B of payload;
     *    subsequent rows = remaining payload, padded with 0x00.
     * ------------------------------------------------------------------ */
    uint32_t base_addr = pas_slot_addr(slot);
    uint32_t payload_off = 0u;

    pas_status_t st = pas_flash_access_begin();
    if (st != PAS_OK) { return st; }

    for (uint8_t row = 0u; row < pas_slot_rows[slot]; row++)
    {
        memset(s_row_buf, 0x00u, sizeof(s_row_buf));

        if (row == 0u)
        {
            /* Write slot header into the first bytes of the row. */
            memcpy(s_row_buf, &hdr, sizeof(hdr));
            /* Fill the remainder of this row with payload bytes. */
            uint32_t first_chunk = PAS_MIN(len, PAS_FLASH_ROW_SIZE - sizeof(hdr));
            if (first_chunk > 0u)
            {
                memcpy(s_row_buf + sizeof(hdr), payload, first_chunk);
                payload_off = first_chunk;
            }
        }
        else
        {
            /* Continue streaming payload into subsequent rows. */
            uint32_t remaining = len - payload_off;
            uint32_t chunk     = PAS_MIN(remaining, PAS_FLASH_ROW_SIZE);
            if (chunk > 0u)
            {
                memcpy(s_row_buf,
                       (const uint8_t *)payload + payload_off,
                       chunk);
                payload_off += chunk;
            }
        }

        st = pas_flash_program_row(
            base_addr + (uint32_t)row * PAS_FLASH_ROW_SIZE, s_row_buf);
        if (st != PAS_OK)
        {
            pas_flash_access_end();
            return st;
        }
    }

    /* ------------------------------------------------------------------
     * 4. Increment header write counter (best-effort).
     * ------------------------------------------------------------------ */
    pas_header_increment_counter(true);

    pas_flash_access_end();
    return PAS_OK;
}

/******************************************************************************
 * Public API — slot erase
 *****************************************************************************/

pas_status_t pas_slot_erase(uint8_t slot)
{
    if (pas_is_frozen())       { return PAS_ERR_FROZEN;  }
    if (slot >= PAS_NUM_SLOTS) { return PAS_ERR_NO_SLOT; }

    uint32_t base_addr = pas_slot_addr(slot);

    /* Hold the flash controller secure across the whole multi-row erase. */
    pas_status_t st = pas_flash_access_begin();
    if (st != PAS_OK) { return st; }

    for (uint8_t row = 0u; row < pas_slot_rows[slot]; row++)
    {
        st = pas_flash_erase_row(base_addr + row * PAS_FLASH_ROW_SIZE);
        if (st != PAS_OK)
        {
            pas_flash_access_end();
            return st;
        }
    }

    pas_header_increment_counter(false);

    pas_flash_access_end();
    return PAS_OK;
}

/******************************************************************************
 * Public API — freeze
 *****************************************************************************/

bool pas_is_frozen(void)
{
    return pas_freeze_record_is_valid();
}

pas_status_t pas_freeze(void)
{
    if (pas_is_frozen()) { return PAS_ERR_FROZEN; }

    /* ------------------------------------------------------------------
     * 1. Build the freeze record in the work buffer.
     * ------------------------------------------------------------------ */
    memset(s_row_buf, 0x00u, sizeof(s_row_buf));
    pas_freeze_record_t *fr = (pas_freeze_record_t *)s_row_buf;

    fr->magic_lo     = PAS_FREEZE_MAGIC_LO;
    fr->magic_hi     = PAS_FREEZE_MAGIC_HI;
    fr->magic_lo_inv = ~PAS_FREEZE_MAGIC_LO;
    fr->magic_hi_inv = ~PAS_FREEZE_MAGIC_HI;
    fr->frozen_at_uid = 0u;    /* TODO: stamp with device UID when available  */
    fr->crc32 = crc32_compute(fr, offsetof(pas_freeze_record_t, crc32));

    /* ------------------------------------------------------------------
     * 2. Program the freeze record into region row 1.
     * ------------------------------------------------------------------ */
    uint32_t freeze_addr = pas_row_addr(PAS_FREEZE_ROW);

    pas_status_t st = pas_flash_access_begin();
    if (st != PAS_OK) { return st; }

    st = pas_flash_program_row(freeze_addr, s_row_buf);

    pas_flash_access_end();
    if (st != PAS_OK) { return st; }

    /* ------------------------------------------------------------------
     * 3. Reconfigure MPC to make all slot rows secure read-only.
     *
     * TODO: implement using Cy_Mpc_ConfigRotMpc() once the production
     *       MPC region list for the PAS window is confirmed.  The MPC
     *       granularity on PSoC Control C3 is 512 bytes (one row), so
     *       rows PAS_FIRST_SLOT_ROW .. (PAS_REGION_ROWS-1) should be
     *       set to SECURE READ-ONLY for all non-boot protection contexts.
     *
     * Example (pseudo-code):
     *   for each slot row:
     *       cy_stc_mpc_rot_cfg_t cfg = {
     *           .startAddr = pas_row_addr(row),
     *           .endAddr   = pas_row_addr(row) + PAS_FLASH_ROW_SIZE - 1,
     *           .secure    = CY_MPC_ACCESS_ENABLED,
     *           .nsPriv    = CY_MPC_ACCESS_DISABLED,
     *           .nsPub     = CY_MPC_ACCESS_DISABLED,
     *       };
     *       Cy_Mpc_ConfigRotMpc(FLASHC_MPC0, &cfg);
     * ------------------------------------------------------------------ */

    return PAS_OK;
}

/* [] END OF FILE */
