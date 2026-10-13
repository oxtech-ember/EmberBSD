/* Origin: EmberBSD UEFI logo layout regression, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stddef.h>
#include "../../sys/arch/i386/stand/efiboot/ember_logo.h"

int
main(void)
{

	unsigned x = 123, y = 456;
	bool visible;

	visible = ember_logo_origin(640, 480, 80, 25, &x, &y);
	assert(!visible);
	assert(x == 123 && y == 456);
	visible = ember_logo_origin(1024, 768, 40, 25, &x, &y);
	assert(!visible);
	visible = ember_logo_origin(1024, 768, 100, 10, &x, &y);
	assert(!visible);
	visible = ember_logo_origin(800, 600, 80, 25, &x, &y);
	assert(visible);
	assert(x == 736 && y == 8);
	assert(x + EMBER_LOGO_WIDTH <= 800);
	assert(y + EMBER_LOGO_HEIGHT <= 600);
	visible = ember_logo_origin(1920, 1080, 100, 31, &x, &y);
	assert(visible);
	assert(x == 1856 && y == 8);
	assert(sizeof(ember_logo_pixels) == EMBER_LOGO_WIDTH * EMBER_LOGO_HEIGHT);
	for (size_t i = 0; i < sizeof(ember_logo_pixels); i++)
		assert(ember_logo_pixels[i] < sizeof(ember_logo_palette) /
		    sizeof(ember_logo_palette[0]));
	return 0;
}
