/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), check font pointer ownership on every exit. */
#include <sys/types.h>
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"

static int fail_copy, load_error, calls, allocations;
static const char *expected_name;
static unsigned char pixels[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

static int
copyinstr(const char *src, char *dst, size_t size, int unused)
{

	(void)unused;
	if (strlen(src) >= size)
		return ENAMETOOLONG;
	strcpy(dst, src);
	return 0;
}

static int
copyin(const void *src, void *dst, size_t size)
{

	if (fail_copy)
		return EFAULT;
	memcpy(dst, src, size);
	return 0;
}

static void *
font_alloc(size_t size)
{

	allocations++;
	return malloc(size);
}

static void
font_free(void *ptr)
{

	allocations--;
	free(ptr);
}

static int
load_font(void *cookie, int screen, struct wsdisplay_font *font)
{

	(void)cookie;
	assert(screen == 0);
	assert(strcmp(font->name, expected_name) == 0);
	assert(font->data != pixels);
	assert(memcmp(font->data, pixels, sizeof(pixels)) == 0);
	calls++;
	return load_error;
}

struct accessops {
	int (*load_font)(void *, int, struct wsdisplay_font *);
};
struct softc {
	struct accessops *sc_accessops;
	void *sc_accesscookie;
};

static int
font_ioctl(struct softc *sc, void *data)
{
	int error;
	char typebuf[16];
	void *tbuf;
	u_int fontsz;
	struct wsdisplay_font font __attribute__((unused));

#define WSDISPLAYIO_LDFONT 1
#define malloc(size, type, flags) font_alloc(size)
#define free(ptr, type) font_free(ptr)
	switch (WSDISPLAYIO_LDFONT) {
#include "font-case.h"
	}
#undef malloc
#undef free
	return error;
}

int
main(void)
{
	struct accessops ops = { load_font };
	struct softc sc = { &ops, NULL };
	struct wsdisplay_font input, before;
	int test, expected, expected_calls;

	for (test = 0; test < 7; test++) {
		memset(&input, 0, sizeof(input));
		input.name = "test-font";
		input.fontheight = sizeof(pixels);
		input.stride = input.numchars = 1;
		input.data = pixels;
		expected_name = input.name;
		calls = fail_copy = load_error = 0;
		expected = 0;
		expected_calls = 1;
		ops.load_font = load_font;
		switch (test) {
		case 1: input.name = NULL; expected_name = "loaded"; break;
		case 2: load_error = expected = EIO; break;
		case 3:
			fail_copy = 1; expected = EFAULT; expected_calls = 0; break;
		case 4:
			input.fontheight = WSDISPLAY_MAXFONTSZ + 1;
			expected = EINVAL; expected_calls = 0; break;
		case 5:
			input.name = "font-name-longer-than-sixteen";
			expected = ENAMETOOLONG; expected_calls = 0; break;
		case 6:
			ops.load_font = NULL;
			expected = EINVAL; expected_calls = 0; break;
		}
		memcpy(&before, &input, sizeof(before));
		assert(font_ioctl(&sc, &input) == expected);
		assert(memcmp(&input, &before, sizeof(input)) == 0);
		assert(calls == expected_calls);
		assert(allocations == 0);
	}
	puts("PASS: font load, default name and five error paths preserve caller pointers");
	return 0;
}
