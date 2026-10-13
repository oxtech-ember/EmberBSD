/* Origin: EmberBSD RGB theme for the firmware-rendered boot console, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _LIBSA_EMBER_CONSOLE_THEME_H
#define _LIBSA_EMBER_CONSOLE_THEME_H

/* Preserve glyphs, reserved bits and colors outside our text palette. */
static inline uint32_t
ember_console_theme_pixel(uint32_t pixel, bool rgb_bytes)
{
	uint32_t red, green, blue, color;

	red = rgb_bytes ? pixel & 255 : (pixel >> 16) & 255;
	green = (pixel >> 8) & 255;
	blue = rgb_bytes ? (pixel >> 16) & 255 : pixel & 255;
	if (red == 0 && green == 0 && blue == 0)
		color = 0x101110;
	else if (red == green && blue == 0)
		color = red == 255 ? 0xffe466 : 0xffab42;
	else if (red > 0 && green == 0 && blue == 0)
		color = 0xff463b;
	else if (red == green && green == blue)
		color = red == 255 ? 0xeee8de : 0x9f9a90;
	else
		return pixel;
	if (rgb_bytes)
		color = (color >> 16) | (color & 0xff00) | ((color & 255) << 16);
	return (pixel & 0xff000000) | color;
}

/* Run only at an input boundary, after firmware has finished its text. */
static inline bool
ember_console_theme_frame(volatile uint32_t *pixels, size_t bytes,
    uint32_t width, uint32_t height, uint32_t stride, bool rgb_bytes)
{
	if (pixels == NULL || width == 0 || height == 0 || stride < width ||
	    stride == 0 || height > bytes / sizeof(*pixels) / stride)
		return false;
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			size_t offset = (size_t)y * stride + x;
			uint32_t old = pixels[offset];
			uint32_t color = ember_console_theme_pixel(old, rgb_bytes);
			if (color != old)
				pixels[offset] = color;
		}
	}
	return true;
}

#endif /* _LIBSA_EMBER_CONSOLE_THEME_H */
