/* Origin: EmberBSD bounded UEFI console theme contracts, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../../sys/lib/libsa/ember_console_theme.h"
#include "../../usr.sbin/sysinst/wizard_ui.h"

int
main(void)
{
	uint32_t pixels[] = { 0xff989800, 0xff0000, 0xffff00, 0x123456,
	    0, 0xffffff, 0x989898, 0xdeadbeef };
	uint32_t saved[8];

	assert(ember_console_theme_pixel(0xff989800, false) == 0xffffab42);
	assert(ember_console_theme_pixel(0x009898, true) == 0x42abff);
	assert(ember_console_theme_pixel(0xffab42, false) == 0xffab42);
	assert(ember_console_theme_pixel(0x989800, false) ==
	    ((uint32_t)wizard_console_rgb[3][0] << 16 |
	    (uint32_t)wizard_console_rgb[3][1] << 8 | wizard_console_rgb[3][2]));
	for (size_t i = 0; i < 8; i++)
		saved[i] = pixels[i];
	assert(!ember_console_theme_frame(pixels, sizeof(pixels), 5, 2, 4, false));
	assert(!ember_console_theme_frame(pixels, 20, 3, 2, 4, false));
	assert(!ember_console_theme_frame(pixels, sizeof(pixels), 3, 2, 0, false));
	for (size_t i = 0; i < 8; i++)
		assert(saved[i] == pixels[i]);
	assert(ember_console_theme_frame(pixels, sizeof(pixels), 3, 2, 4, false));
	assert(pixels[0] == 0xffffab42 && pixels[1] == 0xff463b);
	assert(pixels[2] == 0xffe466 && pixels[3] == 0x123456);
	assert(pixels[4] == 0x101110 && pixels[5] == 0xeee8de);
	assert(pixels[6] == 0x9f9a90 && pixels[7] == 0xdeadbeef);
	for (size_t i = 0; i < 8; i++)
		saved[i] = pixels[i];
	assert(ember_console_theme_frame(pixels, sizeof(pixels), 3, 2, 4, false));
	for (size_t i = 0; i < 8; i++)
		assert(saved[i] == pixels[i]);
	return 0;
}
