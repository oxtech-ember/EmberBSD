/* Origin: EmberBSD portable ASCII boot logo regression, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "../../sys/lib/libsa/bootcfg.h"
#include "../../sys/lib/libsa/ember_boot_logo.h"

static unsigned color_calls, palette;
static bool saved, restored;

static void
color(int event)
{
	assert(!restored);
	if (event == BOOTCFG_COLOR_SAVE) {
		assert(color_calls == 0);
		saved = true;
	} else {
		assert(saved);
		if (event == BOOTCFG_COLOR_RESTORE)
			restored = true;
		else {
			assert(event == BOOTCFG_COLOR_FLAME ||
			    event == BOOTCFG_COLOR_WORDMARK ||
			    event == BOOTCFG_COLOR_OUTER ||
			    event == BOOTCFG_COLOR_INNER);
			palette |= 1U << event;
		}
	}
	color_calls++;
}

int
main(int argc, char **argv)
{
	(void)argv;
	ember_boot_logo(argc > 1 ? color : NULL);
	if (argc > 1) {
		assert(saved && restored);
		assert(palette == ((1U << BOOTCFG_COLOR_FLAME) |
		    (1U << BOOTCFG_COLOR_WORDMARK) |
		    (1U << BOOTCFG_COLOR_OUTER) |
		    (1U << BOOTCFG_COLOR_INNER)));
	} else
		assert(color_calls == 0);
	return 0;
}
