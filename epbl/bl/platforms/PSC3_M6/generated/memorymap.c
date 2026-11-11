#include "memorymap.h"
#include "flash_map_backend.h"

struct flash_device flash_devices[] =
{
	[FLASH] = {
		.address      = 0x32000000U,
		.size         = 0x80000U,
		.erase_size   = 0x200U,
		.erase_val    = 0x0U,
		.device_id    = FLASH,
	},
};

struct flash_area flash_areas[] =
{
	{
		.fa_id        = FLASH_AREA_BOOTLOADER,
		.fa_device_id = FLASH,
		.fa_off       = 0x0U,
		.fa_size      = 0x1b000U,
	},
	{
		.fa_id        = FLASH_AREA_IMG_1_PRIMARY,
		.fa_device_id = FLASH,
		.fa_off       = 0x1b000U,
		.fa_size      = 0x20000U,
	},
	{
		.fa_id        = FLASH_AREA_IMG_1_SECONDARY,
		.fa_device_id = FLASH,
		.fa_off       = 0x40000U,
		.fa_size      = 0x20000U,
	},
};

struct flash_area *boot_area_descs[] =
{
	&flash_areas[0U],
	&flash_areas[1U],
	&flash_areas[2U],
	NULL
};

uint8_t memory_areas_primary[] =
{
	FLASH_AREA_IMG_1_PRIMARY, 
};

uint8_t memory_areas_secondary[] =
{
	FLASH_AREA_IMG_1_SECONDARY, 
};

image_boot_config_t image_boot_config[BOOT_IMAGE_NUMBER] = {
	{
		.mode     = IMAGE_BOOT_MODE_FLASH,
		.address  = 0x3201b000,
		.size     = 0x20000,
	},
};
