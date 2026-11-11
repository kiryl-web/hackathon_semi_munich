/*****************************************************************************
 * File Name        : pas_config.h
 *
 * Description      : Deployment-specific configuration for the Provisioning
 *                    Asset Store (PAS) module. Override this file to
 *                    relocate, resize, or change the slot layout for a
 *                    different deployment.
 *
 *                    Two deployments are selected at compile time by the
 *                    device-guard macro (see the "Region placement and slot
 *                    geometry" section below):
 *
 *                      DEVICE_PSC3M5 — generic 256 KB flash, fixed 8 KB PAS
 *                                      window at the last 8 KB of flash
 *                                      (offset 0x3E000), EC256-only slots
 *                                      (key 1 KB, cert 4 KB).
 *                      DEVICE_PSC3M6 — PAS window owned by the Device
 *                      / PSC3M8        Configurator (design.modus); base and
 *                                      size taken from the generated
 *                                      CYMEM_CM33_0_S_pas_* symbols, with slots
 *                                      sized for ML-DSA-87 (key 8 KB,
 *                                      cert 12 KB).
 *
 *                    Address aliases for the same physical flash:
 *                      C-bus secure execute : 0x1200_0000 + offset
 *                      S-bus secure data    : 0x3200_0000 + offset  <-- used here
 *
 *                    PSC3M5 resolved layout (PAS_SLOT_ROWS_INIT = { 2, 8 }):
 *
 *                      Row  0  @ 0x3203_E000  pas_header_t         (512 B)
 *                      Row  1  @ 0x3203_E200  pas_freeze_record_t  (512 B)
 *                      Row 2-3 @ 0x3203_E400  Slot 0 – key pair    (1 KB)
 *                      Row 4-11@ 0x3203_E800  Slot 1 – certificate (4 KB)
 *                      Row12-15@ 0x3203_F800  Spare                (2 KB)
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

#ifndef PAS_CONFIG_H
#define PAS_CONFIG_H

/******************************************************************************
 * Region placement and slot geometry
 *
 * Two layouts are selected at compile time by the device-guard macro (set in
 * common.mk from the TARGET):
 *
 *   DEVICE_PSC3M5 — generic 256 KB layout: an 8 KB PAS window at the last
 *                   8 KB of flash with EC256-only slots (key 1 KB, cert 4 KB).
 *                   Addresses are fixed here because PSC3M5 has no
 *                   Device-Configurator-generated memory map.
 *
 *   DEVICE_PSC3M6 — the PAS window is owned by the Device Configurator
 *   / DEVICE_PSC3M8  (design.modus).  Base address and size are taken from the
 *                   generated CYMEM_CM33_0_S_pas_* symbols so the firmware
 *                   automatically follows whatever region the DC produces.
 *                   Slots are sized for ML-DSA-87 (key 8 KB, cert 15 KB),
 *                   which also accommodates EC256.
 *
 * Row-aligned: PAS_BASE_ADDR % PAS_FLASH_ROW_SIZE == 0.
 * Must satisfy: PAS_REGION_ROWS >= 2 + sum(PAS_SLOT_ROWS_INIT).
 *****************************************************************************/

#if defined(DEVICE_PSC3M5)

/* PSC3M5: fixed 8 KB PAS window (last 8 KB of flash, physical offset 0x3E000),
 * secure S-bus alias 0x3203_E000.  Layout (PAS_SLOT_ROWS_INIT = { 2, 8 }):
 *   Row 0 header, row 1 freeze, slot 0 key (2 rows / 1 KB),
 *   slot 1 cert (8 rows / 4 KB); 4 rows (2 KB) spare.  2 + 2 + 8 = 12 <= 16. */
#ifndef PAS_BASE_ADDR
#define PAS_BASE_ADDR       0x3203E000u
#endif
#ifndef PAS_REGION_ROWS
#define PAS_REGION_ROWS     16u
#endif
#ifndef PAS_SLOT_ROWS_INIT
#define PAS_SLOT_ROWS_INIT  { 2u, 8u }
#endif

#else /* DEVICE_PSC3M6 / DEVICE_PSC3M8 (and later DC-owned devices) */

/* PSC3M6/PSC3M8: the PAS window is defined by the Device Configurator.  The
 * generated header supplies the secure S-bus base (CYMEM_CM33_0_S_pas_S_START)
 * and the region size in bytes (CYMEM_CM33_0_S_pas_SIZE); the region row count
 * is derived from the size so the geometry tracks the DC layout automatically.
 *
 * Slots are sized for ML-DSA-87 (also valid for EC256):
 *   slot 0 key  : 16 rows / 8 KB  (slot_hdr + pub 2592 + wrapped priv 4924),
 *   slot 1 cert : 24 rows / 12 KB (ML-DSA-87 leaf / SPDM chain).
 * A 22 KB DC window (44 rows) holds 2 (header+freeze) + 16 + 24 = 42 rows with
 * 2 rows spare. */
#include "cymem_CM33_0_S.h"

#ifndef PAS_BASE_ADDR
#define PAS_BASE_ADDR       CYMEM_CM33_0_S_pas_S_START
#endif
#ifndef PAS_REGION_ROWS
/* Row size is the 512 B flash row (PAS_FLASH_ROW_SIZE, defined in pas.h after
 * this header is included), so the literal 512 is used here. */
#define PAS_REGION_ROWS     (CYMEM_CM33_0_S_pas_SIZE / 512u)
#endif
#ifndef PAS_SLOT_ROWS_INIT
#define PAS_SLOT_ROWS_INIT  { 16u, 30u }
#endif

#endif /* DEVICE_PSC3M5 / DEVICE_PSC3M6 / DEVICE_PSC3M8 */

#endif /* PAS_CONFIG_H */

/* [] END OF FILE */
