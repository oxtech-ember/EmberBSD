/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). */
#include <setjmp.h>
#include <stdio.h>

static _Noreturn void
resume(jmp_buf state)
{
	longjmp(state, 7);
}

int
main(void)
{
	jmp_buf state;
	volatile int retained = 1;

	switch (setjmp(state)) {
	case 0:
		retained = 42;
		resume(state);
	case 7:
		if (retained == 42)
			break;
		/* FALLTHROUGH */
	default:
		fputs("setjmp valid-input check failed\n", stderr);
		return 1;
	}
	puts("setjmp valid-input check passed");
	return 0;
}
