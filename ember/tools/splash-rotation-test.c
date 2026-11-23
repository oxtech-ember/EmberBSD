/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the kernel pixel writers with a fixed 3-by-2 RGB image. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "splash.h"

#define aprint_debug(...) ((void)0)
#define aprint_verbose(...) ((void)0)
#define aprint_error(...) ((void)0)
#define STBI_rgb 3

static unsigned char rgb[6 * 3];

static char *
stbi_load_from_memory(const unsigned char *data, size_t len, int *w, int *h,
    int *comp, int requested)
{
	assert(data == rgb && len == sizeof(rgb) && requested == 3);
	*w = 3;
	*h = 2;
	*comp = 3;
	return (char *)rgb;
}

#define stbi_image_free(data) ((void)0)
#include "splash-render-body.h"

static void
check_pixel(const uint8_t *pixel, int depth, unsigned source)
{
	const uint8_t *c = &rgb[source * 3];

	if (depth == 8) {
		assert(*pixel == SPLASH_CMAP_OFFSET +
		    ((c[0] >> 6) << 4) + ((c[1] >> 6) << 2) + (c[2] >> 6));
	} else if (depth == 16) {
		uint16_t result;
		memcpy(&result, pixel, sizeof(result));
		assert(result == ((c[0] >> 3) << 11 |
		    (c[1] >> 2) << 5 | (c[2] >> 3)));
	} else {
		assert(pixel[0] == c[2] && pixel[1] == c[1] &&
		    pixel[2] == c[0] && pixel[3] == 0);
	}
}

static void
check(int depth, int width, int height, int flags, const unsigned *order)
{
	uint32_t storage[6] = {0};
	uint8_t *pixels = (uint8_t *)storage;
	const int bytes = depth / 8;
	struct splash_info si = {
		.si_depth = depth, .si_bits = pixels, .si_width = width,
		.si_height = height, .si_stride = width * bytes
	};

	int error = splash_render(&si, flags);
	assert(error == 0);
	for (unsigned i = 0; i < 6; i++)
		check_pixel(&pixels[i * bytes], depth, order[i]);
}

static void
check_shadow(int depth, const unsigned *order, bool fill)
{
	uint32_t shadow[5 * 5], hardware[5 * 5];
	const int bytes = depth / 8;
	struct splash_info si = {
		.si_depth = depth, .si_bits = (uint8_t *)shadow,
		.si_hwbits = (uint8_t *)hardware, .si_width = 4,
		.si_height = 5, .si_stride = 5 * bytes
	};
	int flags = SPLASH_F_CENTER | SPLASH_F_ROTATE_CW;

	memset(shadow, 0x55, sizeof(shadow));
	memset(hardware, 0xaa, sizeof(hardware));
	int error = splash_render(&si, flags | (fill ? SPLASH_F_FILL : 0));
	assert(error == 0);
	for (int y = 0; y < si.si_height; y++) {
		for (int x = 0; x < si.si_width; x++) {
			const uint8_t *pixel = si.si_hwbits + si.si_stride * y + x * bytes;
			if (x >= 1 && x < 3 && y >= 1 && y < 4)
				check_pixel(pixel, depth, order[(y - 1) * 2 + x - 1]);
			else if (fill)
				check_pixel(pixel, depth, order[0]);
			else
				assert(*pixel == 0xaa);
		}
	}
}

int
main(void)
{
	const unsigned straight[] = {0, 1, 2, 3, 4, 5};
	const unsigned clockwise[] = {3, 0, 4, 1, 5, 2};
	for (unsigned i = 0; i < 6; i++) {
		rgb[i * 3] = (uint8_t)(i * 41);
		rgb[i * 3 + 1] = (uint8_t)(i * 71);
		rgb[i * 3 + 2] = (uint8_t)(i * 113);
	}
	int error = splash_setimage(rgb, sizeof(rgb));
	assert(error == 0);
	for (int depth = 8; depth <= 32; depth *= 2) {
		check(depth, 3, 2, 0, straight);
		check(depth, 2, 3, SPLASH_F_ROTATE_CW, clockwise);
		check_shadow(depth, clockwise, false);
		check_shadow(depth, clockwise, true);
		check(depth, 3, 2, SPLASH_F_ROTATE_CW, straight);
	}
	puts("Splash rotation: 8-, 16- and 32-bit pixels and native-size artwork passed");
	return 0;
}
