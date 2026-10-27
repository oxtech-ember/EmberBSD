/* SPDX-License-Identifier: BSD-2-Clause */
/* Check lock-free C11 compare-and-swap state shared across fork(). */
#define _POSIX_C_SOURCE 200809L
#define _NETBSD_SOURCE 1

#include <sys/mman.h>
#include <sys/wait.h>

#include <stdatomic.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ITERATIONS 425
#define EXPECTED ((unsigned long)ITERATIONS * 2)

struct shared_state {
	_Atomic unsigned long value;
};

static void
increment(struct shared_state *state)
{
	unsigned long expected, desired;

	expected = atomic_load_explicit(&state->value, memory_order_relaxed);
	do {
		desired = expected + 1;
	} while (!atomic_compare_exchange_weak_explicit(&state->value, &expected,
	    desired, memory_order_relaxed, memory_order_relaxed));
}

static int
run_shared_cas(void)
{
	struct shared_state *state;
	pid_t child, result;
	int status;
	unsigned int i;

	state = mmap(NULL, sizeof(*state), PROT_READ | PROT_WRITE,
	    MAP_SHARED | MAP_ANON, -1, 0);
	if (state == MAP_FAILED) {
		perror("mmap shared CAS state");
		return (1);
	}
	atomic_init(&state->value, 0);
	if (!atomic_is_lock_free(&state->value)) {
		fprintf(stderr, "shared CAS object is not lock-free\n");
		(void)munmap(state, sizeof(*state));
		return (1);
	}

	child = fork();
	if (child == -1) {
		perror("fork shared CAS test");
		(void)munmap(state, sizeof(*state));
		return (1);
	}
	if (child == 0) {
		for (i = 0; i < ITERATIONS; i++)
			increment(state);
		_exit(0);
	}

	for (i = 0; i < ITERATIONS; i++)
		increment(state);
	do {
		result = waitpid(child, &status, 0);
	} while (result == -1 && errno == EINTR);
	if (result != child) {
		perror("waitpid shared CAS test");
		(void)munmap(state, sizeof(*state));
		return (1);
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "forked CAS worker did not exit successfully\n");
		(void)munmap(state, sizeof(*state));
		return (1);
	}
	if (atomic_load_explicit(&state->value, memory_order_relaxed) != EXPECTED) {
		fprintf(stderr, "shared CAS total differs from %lu\n", EXPECTED);
		(void)munmap(state, sizeof(*state));
		return (1);
	}
	if (munmap(state, sizeof(*state)) == -1) {
		perror("munmap shared CAS state");
		return (1);
	}
	return (0);
}

int
main(void)
{
	(void)alarm(30);
	if (run_shared_cas() != 0)
		return (1);
	puts("shared fork CAS completed 850 updates");
	return (0);
}
