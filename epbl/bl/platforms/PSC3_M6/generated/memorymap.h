/* AUTO-GENERATED FILE, DO NOT EDIT. ALL CHANGES WILL BE LOST! */
#pragma once

#include <stdint.h>
#include "bootutil/bootutil.h"
#include "flash_map_backend.h"

#define MEMORYMAP_GENERATED_AREAS 1

extern struct flash_device flash_devices[];
extern struct flash_area *boot_area_descs[];

extern uint8_t memory_areas_primary[];
extern uint8_t memory_areas_secondary[];

enum 
{
	FLASH,
};

enum 
{
	FLASH_AREA_BOOTLOADER = 0U,
	FLASH_AREA_IMG_1_PRIMARY = 1U,
	FLASH_AREA_IMG_1_SECONDARY = 2U,
};

typedef enum 
{
	IMAGE_BOOT_MODE_FLASH = 0U,
	IMAGE_BOOT_MODE_RAM = 1U,
} image_boot_mode_t;

typedef struct image_boot_config_s 
{
	image_boot_mode_t mode;
	uint32_t address;
	uint32_t size;
} image_boot_config_t;

extern image_boot_config_t image_boot_config[BOOT_IMAGE_NUMBER];

