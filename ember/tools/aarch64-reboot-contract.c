/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Execute the actual cpu_reboot body with terminal hardware services replaced.
 * This checks shutdown flags and order. It does not test firmware power or wake.
 */
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reboot.h"

int cold, boothowto;
static int shutdown_flags, reset_flags, shutdown_calls;
static char events[32];
static size_t nevents;
static jmp_buf terminal;
static unsigned int checks, failures;

static void
event(char value)
{

	if (nevents + 1 >= sizeof(events))
		abort();
	events[nevents++] = value;
	events[nevents] = '\0';
}

static void bootsync(void) { event('S'); }
static int splhigh(void) { event('H'); return 17; }
static void dumpsys(void) { event('D'); }
static void doshutdownhooks(void) { event('K'); }
static void disable_interrupt(void) { event('I'); }
static void
splx(int value)
{

	if (value != 17)
		abort();
	event('X');
}

static void
pmf_system_shutdown(int flags)
{

	shutdown_flags = flags;
	shutdown_calls++;
	event('P');
}

static _Noreturn void
docpureset(int flags)
{

	reset_flags = flags;
	event('R');
	longjmp(terminal, 1);
}

#define DISABLE_INTERRUPT() disable_interrupt()
#define __unreachable() abort()
#include "cpu_reboot.inc"

#define CHECK(test) do { \
	checks++; \
	if (!(test)) { \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); \
		failures++; \
	} \
} while (0)

static void
run_case(int requested, int boot_flags, bool is_cold, const char *order)
{

	cold = is_cold;
	boothowto = boot_flags;
	shutdown_flags = reset_flags = -1;
	shutdown_calls = 0;
	nevents = 0;
	events[0] = '\0';
	if (setjmp(terminal) == 0)
		cpu_reboot(requested, NULL);
	CHECK(strcmp(events, order) == 0);
	CHECK(reset_flags == (requested | (is_cold ? RB_HALT : 0)));
	CHECK(shutdown_calls == (is_cold ? 0 : 1));
	if (!is_cold)
		CHECK(shutdown_flags == requested);
	CHECK(boothowto == boot_flags);
}

int
main(void)
{

	run_case(RB_POWERDOWN, AB_QUIET | RB_SINGLE, false, "SHXPHKIR");
	run_case(RB_AUTOBOOT, RB_POWERDOWN, false, "SHXPHKIR");
	run_case(RB_HALT, AB_VERBOSE, false, "SHXPHKIR");
	run_case(RB_DUMP, RB_HALT, false, "SHDXPHKIR");
	run_case(RB_DUMP | RB_HALT, RB_AUTOBOOT, false, "SHXPHKIR");
	run_case(RB_POWERDOWN | RB_NOSYNC, RB_SINGLE, false, "HXPHKIR");
	run_case(RB_AUTOBOOT, AB_QUIET, true, "KR");
	printf("aarch64 reboot contract: %u checks, %u failures\n",
	    checks, failures);
	return failures != 0;
}
