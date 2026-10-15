/* Origin: EmberBSD temporary console presentation interface, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SYSINST_WIZARD_CONSOLE_H
#define SYSINST_WIZARD_CONSOLE_H
#include <stdbool.h>
bool wizard_console_setup(void);
void wizard_console_restore(void);
#endif
