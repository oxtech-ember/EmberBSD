/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), bounded syscall workload for live DTrace. */
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

int
main(void)
{
	pid_t expected = getpid();
	unsigned int i;

	for (i = 0; i < 100; i++) {
		if (syscall(SYS_getpid) != expected)
			return 1;
		usleep(10000);
	}
	return 0;
}
