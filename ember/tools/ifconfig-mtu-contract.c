/* SPDX-License-Identifier: BSD-2-Clause */
/* Original AI-assisted tests of native ifconfig MTU error propagation. */
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mtu-request.inc"

static unsigned int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #c); \
	exit(1); } } while (0)

/* Only property storage and external service calls are host boundaries. */
struct property { int64_t mtu; bool has_mtu; };
typedef struct property *prop_dictionary_t;
typedef struct property *prop_object_t;
static unsigned int copies, releases, live, calls, warnings, before, after;
static int syscall_error, requested, installed, warning_errno;

static bool
prop_dictionary_get_int64(prop_dictionary_t d, const char *key, int64_t *out)
{
	CHECK(d != NULL && strcmp(key, "mtu") == 0);
	if (!d->has_mtu)
		return false;
	*out = d->mtu;
	return true;
}

static prop_dictionary_t
prop_dictionary_augment(prop_dictionary_t d, prop_dictionary_t extra)
{
	CHECK(d != NULL && extra != NULL && !extra->has_mtu);
	prop_dictionary_t copy = malloc(sizeof(*copy));
	CHECK(copy != NULL);
	*copy = *d;
	copies++; live++;
	return copy;
}

static void
prop_object_release(prop_object_t d)
{
	CHECK(d != NULL && live != 0);
	free(d); releases++; live--;
}

static int getsock(int family) { CHECK(family == AF_UNSPEC); return 7; }
static const char *getifname(prop_dictionary_t d)
{ CHECK(d != NULL); return "mtutest0"; }
static void
estrlcpy(void *out, const char *name, size_t size)
{
	CHECK(strlen(name) < size);
	memcpy(out, name, strlen(name) + 1);
}

static int
prog_ioctl(int fd, unsigned long command, void *data)
{
	struct ifreq *ifr = data;
	CHECK(fd == 7 && command == SIOCSIFMTU);
	CHECK(strcmp(ifr->ifr_name, "mtutest0") == 0);
	requested = ifr->ifr_mtu; calls++;
	if (syscall_error != 0) {
		errno = syscall_error;
		return -1;
	}
	installed = requested;
	return 0;
}

static void
warn(const char *format, ...)
{
	CHECK(strcmp(format, "SIOCSIFMTU") == 0);
	warnings++; warning_errno = errno;
	/* Diagnostics may alter errno; main must still see the ioctl error. */
	errno = EIO;
}

static void
err(int status, const char *format, ...)
{
	(void)status; (void)format;
	abort();
}

#define dbg_warnx(...) ((void)0)
#include "mtu-parser-types.inc"
#include "mtu-source.inc"

static int
before_command(prop_dictionary_t env, prop_dictionary_t oenv)
{ (void)env; (void)oenv; before++; return 0; }
static int
after_command(prop_dictionary_t env, prop_dictionary_t oenv)
{ (void)env; (void)oenv; after++; return 0; }

static void
run_case(int mtu, int error, bool fallback)
{
	struct property env = { .mtu = mtu, .has_mtu = true }, output = {0};
	const struct parser prefix = { .p_exec = before_command };
	const struct parser setter = { .p_exec = fallback ? NULL : setifmtu };
	const struct parser suffix = { .p_exec = after_command };
	const struct match matches[] = {
		{ .m_env = &env, .m_parser = &prefix },
		{ .m_env = &env, .m_parser = &setter, .m_exec = setifmtu },
		{ .m_env = &env, .m_parser = &suffix }
	};

	CHECK(live == 0);
	copies = releases = calls = warnings = before = after = 0;
	syscall_error = error; requested = 0; installed = 1500;
	warning_errno = 0; errno = 0;
	int rc = matches_exec(matches, &output, 3);
	CHECK(rc == (error != 0 ? -1 : 0));
	CHECK(before == 1 && calls == 1 && requested == mtu);
	CHECK(after == (error != 0 ? 0U : 1U));
	CHECK(installed == (error != 0 ? 1500 : mtu));
	CHECK(warnings == (error != 0 ? 1U : 0U));
	if (error != 0)
		CHECK(errno == error && warning_errno == error);
	CHECK(copies == (error != 0 ? 2U : 3U));
	CHECK(releases == copies && live == 0);
	CHECK(env.mtu == mtu && env.has_mtu && !output.has_mtu);
}

int
main(void)
{
	const int failures[] = { EINVAL, EPERM, ENODEV, ENOBUFS };

	for (unsigned int fallback = 0; fallback < 2; fallback++) {
		run_case(1500, 0, fallback != 0);
		run_case(9000, 0, fallback != 0);
		for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
			run_case(9000, failures[i], fallback != 0);
			run_case(9001, failures[i], fallback != 0);
		}
	}
	printf("ifconfig MTU: %u checks, 0 failures\n", checks);
	return 0;
}
