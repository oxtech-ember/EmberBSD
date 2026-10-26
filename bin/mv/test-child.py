#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Emit a diagnostic for canonical copy/wait code with controlled processes."""
import argparse
from pathlib import Path


def diagnostic(source):
    start = source.index('\nstatic int\ncopy(')
    end = source.index('\nstatic void\nusage(', start)
    child = source[start:end]
    if '\nstatic int\nwait_child(' in source:
        begin = source.index('\nstatic int\nwait_child(')
        child = source[begin:start] + child
    return r'''/* Diagnostic only. No process or filesystem operation runs. */
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define _PATH_CP "selected-cp"
#define _PATH_RM "selected-rm"
static int vflg;
static int phase, spawn_error, wait_error, exit_error, signal_error;
static int fork_calls, vfork_calls;
static int interrupted_phase, interruptions, waits, warnings, wrong_pid;

static pid_t
controlled_spawn(void)
{

	phase++;
	assert(phase <= 2);
	if (phase == spawn_error) {
		errno = EAGAIN;
		return -1;
	}
	return 100 + phase;
}

static pid_t
controlled_fork(void)
{

	fork_calls++;
	return controlled_spawn();
}

static pid_t
controlled_vfork(void)
{

	vfork_calls++;
	return controlled_spawn();
}

static pid_t
controlled_wait(pid_t pid, int *status, int options)
{

	assert(options == 0);
	waits++;
	if (pid != 100 + phase) {
		wrong_pid++;
		errno = ECHILD;
		return -1;
	}
	if (phase == interrupted_phase && interruptions > 0) {
		interruptions--;
		errno = EINTR;
		return -1;
	}
	if (phase == wait_error) {
		errno = ECHILD;
		return -1;
	}
	*status = phase == signal_error ? SIGTERM :
	    phase == exit_error ? 7 << 8 : 0;
	return pid;
}

static void
controlled_warn(const char *format, ...)
{

	assert(format != NULL);
	warnings++;
}

static int
unreachable_exec(const char *path, const char *arg, ...)
{

	(void)path;
	(void)arg;
	abort();
}

#define fork controlled_fork
#define vfork controlled_vfork
#define waitpid controlled_wait
#define warn controlled_warn
#define warnx controlled_warn
#define execl unreachable_exec
#define _exit exit
''' + child + r'''

int
main(void)
{
	unsigned int i;
	(void)controlled_fork;
	(void)controlled_vfork;
	int result, failed, expected_phase, expected_waits;
	/* Success, both spawn errors, both wait errors, exits, signals, EINTR. */
	const int cases[][6] = {
		{0, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 0},
		{2, 0, 0, 0, 0, 0}, {0, 1, 0, 0, 0, 0},
		{0, 2, 0, 0, 0, 0}, {0, 0, 1, 0, 0, 0},
		{0, 0, 2, 0, 0, 0}, {0, 0, 0, 1, 0, 0},
		{0, 0, 0, 2, 0, 0}, {0, 0, 0, 0, 1, 2},
		{0, 0, 0, 0, 2, 2}
	};

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		spawn_error = cases[i][0];
		wait_error = cases[i][1];
		exit_error = cases[i][2];
		signal_error = cases[i][3];
		interrupted_phase = cases[i][4];
		interruptions = cases[i][5];
		phase = waits = warnings = wrong_pid = 0;
		fork_calls = vfork_calls = 0;
		result = copy("source", "destination");
#if HAVE_NBTOOL_CONFIG_H
		assert(fork_calls == phase && vfork_calls == 0);
#else
		assert(vfork_calls == phase && fork_calls == 0);
#endif
		failed = spawn_error || wait_error || exit_error || signal_error;
		expected_phase = spawn_error == 1 || wait_error == 1 ||
		    exit_error == 1 || signal_error == 1 ? 1 : 2;
		expected_waits = expected_phase - (spawn_error != 0) + cases[i][5];
		if (result != !!failed || phase != expected_phase || wrong_pid ||
		    waits != expected_waits || warnings != !!failed || interruptions) {
			fprintf(stderr, "child case %u failed: result=%d phase=%d waits=%d wrong_pid=%d\n",
			    i, result, phase, waits, wrong_pid);
			return 1;
		}
	}
	printf("%u child cases passed\n", i);
	return 0;
}
'''


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).with_name('mv.c'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with args.output.open('x') as stream:
        stream.write(diagnostic(args.source.read_text()))
