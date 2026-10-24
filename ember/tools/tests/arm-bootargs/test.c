/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors, AI-assisted by OpenAI Codex (GPT-6). */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int u_int;
#define __noasan
#define NETHER 0
#include "options.h"
#include "function.inc"

int
main(void)
{
	char banner[] = "console=ttyPS0,115200 root=/dev/ram0 uboot=U-Boot PlutoSDR (Oct 9 2025 - 18:39)";
	char single[] = "  !single\tdebug=0 root=md0 memory=512 singleextra";
	int value = 77;
	char *text;
	assert(get_bootconf_option(banner, "-d", BOOTOPT_TYPE_BOOLEAN, &value) == 0);
	assert(value == 77);
	assert(get_bootconf_option(banner, "-a", BOOTOPT_TYPE_BOOLEAN, &value) == 0);
	assert(get_bootconf_option("sin", "single", BOOTOPT_TYPE_BOOLEAN, &value) == 0);
	assert(get_bootconf_option("singleextra", "single", BOOTOPT_TYPE_BOOLEAN, &value) == 0);
	assert(get_bootconf_option(single, "single", BOOTOPT_TYPE_BOOLEAN, &value) == 1 && value == 0);
	assert(get_bootconf_option("single=1", "single", BOOTOPT_TYPE_BOOLEAN, &value) == 1 && value == 1);
	assert(get_bootconf_option(single, "memory", BOOTOPT_TYPE_INT, &value) == 1 && value == 512);
	assert(get_bootconf_option("mem=512", "memory", BOOTOPT_TYPE_INT, &value) == 0);
	assert(get_bootconf_option(single, "root", BOOTOPT_TYPE_STRING, &text) == 1 && strncmp(text, "md0", 3) == 0);
	assert(get_bootconf_option("-d", "-d", BOOTOPT_TYPE_BOOLEAN, &value) == 1 && value == 1);
	puts("PASS real ARM boot parser: exact names, bare dash, banner, negation, values and flags");
	return 0;
}
