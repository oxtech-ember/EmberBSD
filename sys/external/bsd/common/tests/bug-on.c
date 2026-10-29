/* SPDX-License-Identifier: BSD-2-Clause */
/* EmberBSD contributors (AI-assisted): actual Linux compatibility BUG_ON. */
#include <setjmp.h>
#include <stdlib.h>

static jmp_buf panic_env;
static int panics;

void
test_panic(const char *fmt, ...)
{
	(void)fmt;
	panics++;
	longjmp(panic_env, 1);
}

#define panic test_panic
#include "../include/asm/bug.h"

static int calls;
static int lock_held;

int
fake_trylock(void)
{
	calls++;
	lock_held = 1;
	return 1;
}

int
fake_unlock(void)
{
	if (!lock_held)
		return 0;
	lock_held = 0;
	return 1;
}

int
main(void)
{
	int *ptr = NULL;

	if (1)
		BUG_ON(!fake_trylock());
	else
		return 1;
	if (calls != 1 || !lock_held || panics)
		return 2;
	if (!fake_unlock() || lock_held)
		return 7;
	BUG_ON(ptr);
	if (setjmp(panic_env) == 0) {
		BUG_ON(++calls == 2);
		return 3;
	}
	if (panics != 1 || calls != 2)
		return 4;
	ptr = &calls;
	if (setjmp(panic_env) == 0) {
		BUG_ON(ptr);
		return 5;
	}
	return panics == 2 ? 0 : 6;
}
