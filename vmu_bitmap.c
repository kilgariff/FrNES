#include "vmu_bitmap.h"

#include <kos.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

static bool vmu_lcd_bitmap_loaded = false;
static uint8_t vmu_lcd_bitmap[192];

int load_vmu_lcd_bitmap(const char *filename)
{
	FILE *fp = NULL;

	// 48x32 bit image:
	// 48x32 = 1536 bits = 192 bytes
	// With padding to ensure each line starts on a 4-byte boundary:
	// 64x32 = 2048 bits = 256 bytes

	uint8_t bmp[256];
	int i, j, k;

	fp = fopen(filename, "r");
	if (!fp) goto error;

	// Skip the BMP header and the palette.
	fseek(fp, 128, SEEK_SET);

	if (fread(bmp, sizeof(bmp), 1, fp) != 1) 
	goto error;

	fclose(fp);

	for (i = 0; i < 32; ++i)
	{
		for (j = 0; j < 6; ++j)
		{
			uint8_t const val = bmp[i * 8 + j + 2];

			uint8_t flipped_byte = 0;
			for (k = 0; k < 4; ++k)
			{
				int const shift_amt = 2 * k + 1;
				int const mask_shift_amt = 3 - k;
				flipped_byte |= (val >> shift_amt) & (1u << mask_shift_amt);
				flipped_byte |= (val << shift_amt) & (128u >> mask_shift_amt);
			}

			vmu_lcd_bitmap[i * 6 + (5 - j)] = flipped_byte;
		}
	}

    vmu_lcd_bitmap_loaded = true;
	return 0;

error:
	if (fp) fclose(fp);
	memset(vmu_lcd_bitmap, 0, 192);
	return -1;
}

void draw_vmu_bitmap_all_devices()
{
    if (vmu_lcd_bitmap_loaded == false) return;

	int n = 0;
	while (1)
	{
		maple_device_t *dev;
		
		dev = maple_enum_type(n, MAPLE_FUNC_LCD);
		if (!dev) break;
		++n;

		vmu_draw_lcd(dev, vmu_lcd_bitmap);
	}
}