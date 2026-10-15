/* Origin: EmberBSD temporary installer console presentation, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <curses.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef WSKBD
#include <dev/wscons/wsconsio.h>
#endif
#include "wizard_ui.h"
#include "wizard_console.h"

static bool palette_changed, font_changed;
#ifdef WSKBD
static bool registered;
static char old_font[128];
static unsigned char old_red[16], old_green[16], old_blue[16];
#endif

void
wizard_console_restore(void)
{
#ifdef WSKBD
	struct wsdisplay_cmap cmap = { .index = 0, .count = 16,
	    .red = old_red, .green = old_green, .blue = old_blue };
	struct wsdisplay_usefontdata font = { .name = old_font };

	if (palette_changed && ioctl(STDIN_FILENO, WSDISPLAYIO_PUTCMAP, &cmap) != 0)
		fprintf(stderr, "sysinst: cannot restore the console palette\n");
	if (font_changed && ioctl(STDIN_FILENO, WSDISPLAYIO_SFONT, &font) != 0)
		fprintf(stderr, "sysinst: cannot restore the console font\n");
#endif
	palette_changed = font_changed = false;
}

bool
wizard_console_setup(void)
{
#ifdef WSKBD
	struct wsdisplay_fbinfo info;
	struct wsdisplay_getfont current = { .gf_name = old_font,
	    .gf_size = sizeof(old_font) };
	struct wsdisplay_usefontdata font;
	struct wsdisplay_cmap cmap = { .index = 0, .count = 16,
	    .red = old_red, .green = old_green, .blue = old_blue };
	unsigned char red[16], green[16], blue[16];
	char font_name[32];
	struct wsdisplay_param capability = {
	    .param = WSDISPLAYIO_PARAM_RGB_PALETTE
	};

	if (palette_changed)
		return true;
	if (ioctl(STDIN_FILENO, WSDISPLAYIO_GETPARAM, &capability) != 0 ||
	    capability.curval != 1)
		return false;
	if (!registered) {
		if (atexit(wizard_console_restore) != 0)
			return false;
		registered = true;
	}
	if (!font_changed && ioctl(STDIN_FILENO, WSDISPLAYIO_GINFO, &info) == 0 &&
	    ioctl(STDIN_FILENO, WSDISPLAYIO_GFONT, &current) == 0 &&
	    current.gf_actual <= sizeof(old_font) && old_font[0] != '\0') {
		const char *name = info.width >= 1280 && info.height >= 800 ? "Spleen 16x32" :
		    (info.width >= 960 && info.height >= 600 ? "Spleen 12x24" : NULL);
		if (name != NULL)
			strlcpy(font_name, name, sizeof(font_name));
		font.name = font_name;
		if (name != NULL &&
		    ioctl(STDIN_FILENO, WSDISPLAYIO_SFONT, &font) == 0)
			font_changed = true;
	}
	if (ioctl(STDIN_FILENO, WSDISPLAYIO_GETCMAP, &cmap) != 0)
		return false;
	for (int i = 0; i < 16; i++) {
		red[i] = wizard_console_rgb[i][0];
		green[i] = wizard_console_rgb[i][1];
		blue[i] = wizard_console_rgb[i][2];
	}
	cmap.red = red;
	cmap.green = green;
	cmap.blue = blue;
	if (ioctl(STDIN_FILENO, WSDISPLAYIO_PUTCMAP, &cmap) == 0)
		palette_changed = true;
#endif
	return palette_changed;
}
