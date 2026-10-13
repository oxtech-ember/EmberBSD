/* Origin: EmberBSD portable ASCII boot logo regression, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include "../../sys/lib/libsa/bootcfg.h"
#include "../../sys/lib/libsa/ember_boot_logo.h"

static unsigned color_calls;

static void
color(int event)
{
	if (color_calls == 0)
		assert(event == BOOTCFG_COLOR_SAVE);
	else if (color_calls == 17)
		assert(event == BOOTCFG_COLOR_RESTORE);
	else
		assert(event == ((color_calls & 1) ?
		    BOOTCFG_COLOR_FLAME : BOOTCFG_COLOR_WORDMARK));
	color_calls++;
}

int
main(int argc, char **argv)
{
	(void)argv;
	ember_boot_logo(argc > 1 ? color : NULL);
	assert(color_calls == (argc > 1 ? 18 : 0));
	return 0;
}
