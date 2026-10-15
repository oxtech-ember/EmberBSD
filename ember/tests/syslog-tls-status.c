/* SPDX-License-Identifier: BSD-2-Clause
 * Origin: EmberBSD (AI-assisted), actual syslogd TLS status format regression.
 * Only the library version and allocation observer are controlled here.
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *owned[2];
static unsigned frees, checks, failures;
static void
observed_free(void *p)
{
	assert(p != NULL && (p == owned[0] || p == owned[1]));
	if (p == owned[0])
		owned[0] = NULL;
	else
		owned[1] = NULL;
	frees++;
	free(p);
}
#define free observed_free
#define DPRINTF(...) ((void)0)
#define SSLEAY_VERSION 0
static const char *
SSLeay_version(int version)
{
	assert(version == SSLEAY_VERSION);
	return "fixture TLS";
}
#include "freeptr.h"
static void
check(int ok, const char *name)
{
	checks++;
	if (!ok) {
		fprintf(stderr, "FAIL: %s\n", name);
		failures++;
	}
}
static void
run_case(const char *input_cn, const char *input_fp, const char *expected)
{
	char *cn = input_cn ? strdup(input_cn) : NULL;
	char *fp = input_fp ? strdup(input_fp) : NULL;
	const char *certfilename = "fixture.pem";
	char statusmsg[1024];

	assert((!input_cn || cn) && (!input_fp || fp));
	owned[0] = cn;
	owned[1] = fp;
	frees = 0;
#include "status-format.c"
	check(strcmp(statusmsg, expected) == 0, "exact status text");
	check(cn == NULL && fp == NULL, "actual FREEPTR clears original pointers");
	check(frees == (unsigned)(!!input_cn + !!input_fp),
	    "each real allocation freed once");
	check(owned[0] == NULL && owned[1] == NULL,
	    "no retained allocation or free of fallback literals");
}
int
main(void)
{
	run_case("example.test", "sha-1:AA:BB:",
	    "Initialized TLS settings using library \"fixture TLS\". "
	    "Use certificate from file \"fixture.pem\" with CN \"example.test\" "
	    "and fingerprint \"sha-1:AA:BB:\"");
	run_case(NULL, "sha-1:AA:BB:",
	    "Initialized TLS settings using library \"fixture TLS\". "
	    "Use certificate from file \"fixture.pem\" with CN \"unavailable\" "
	    "and fingerprint \"sha-1:AA:BB:\"");
	run_case("example.test", NULL,
	    "Initialized TLS settings using library \"fixture TLS\". "
	    "Use certificate from file \"fixture.pem\" with CN \"example.test\" "
	    "and fingerprint \"unavailable\"");
	run_case(NULL, NULL,
	    "Initialized TLS settings using library \"fixture TLS\". "
	    "Use certificate from file \"fixture.pem\" with CN \"unavailable\" "
	    "and fingerprint \"unavailable\"");
	printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
