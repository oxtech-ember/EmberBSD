/* Origin: EmberBSD portable ASCII boot logo regression, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include "../../sys/lib/libsa/ember_boot_logo.h"
int
main(void)
{
	ember_boot_logo();
	return 0;
}
