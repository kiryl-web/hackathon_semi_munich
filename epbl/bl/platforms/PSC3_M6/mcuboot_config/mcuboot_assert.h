/*
 * mcuboot_assert.h
 *
 * (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG.  All rights reserved.
 *
 * Cypress-specific assert() macro redefinition
 *
 */

#ifndef MCUBOOT_ASSERT_H
#define MCUBOOT_ASSERT_H

#define CYBL_ASSERT(...) Cy_BLServ_Assert(__VA_ARGS__)

#if !defined(NDEBUG)
#undef assert
#define assert(...) CYBL_ASSERT(__VA_ARGS__)
#else
#define assert
#endif

#endif /* MCUBOOT_ASSERT_H */
