/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 EmberBSD contributors.
 * AI-assisted host fixture. The included functions retain their IPFilter license.
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#ifdef NDEBUG
#error This fixture requires assertions
#endif
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "ipf-types.inc"
#define MIN(a,b) ((a) < (b) ? (a) : (b))
static int pfd, swapped;
static unsigned char wire[sizeof(packetheader_t) + 16];
static size_t offset, chunk, available;
static int read_error;
static ssize_t
fragment_read(int fd, void *dst, size_t n)
{
	(void)fd;
	if (read_error)
		return -1;
	if (n > chunk)
		n = chunk;
	if (n > available - offset)
		n = available - offset;
	memcpy(dst, wire + offset, n);
	offset += n;
	return (ssize_t)n;
}
#define read fragment_read
#include "ipf-functions.inc"
#undef read


static struct llc *llcp;
static int last_open = -1;
static int
record_open(const char *path, int flags)
{
	last_open = open(path, flags);
	return last_open;
}
#define open record_open
#include "ipf-open.inc"
#undef open

static void
open_case(const fileheader_t *header, size_t bytes, int good)
{
	char name[] = "pcap-open-XXXXXX";
	int fd = mkstemp(name);
	assert(fd >= 0);
	assert(write(fd, header, bytes) == (ssize_t)bytes);
	assert(close(fd) == 0);
	pfd = -1;
	swapped = 0;
	llcp = NULL;
	int result = ipcap_open(name);
	assert(unlink(name) == 0);
	assert(last_open >= 0);
	if (good) {
		assert(result == last_open && pfd == last_open);
		assert(fcntl(last_open, F_GETFD) >= 0);
		assert(close(pfd) == 0);
	} else {
		assert(result == -2 && pfd == -1);
		errno = 0;
		assert(fcntl(last_open, F_GETFD) == -1 && errno == EBADF);
	}
	pfd = -1;
}

static void
open_cases(void)
{
	fileheader_t header = { 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1 };
	for (size_t n = 0; n < sizeof(header); n++)
		open_case(&header, n, 0);
	open_case(&header, sizeof(header), 1);
	header.id = 0;
	open_case(&header, sizeof(header), 0);
	header.id = 0xa1b2c3d4;
	header.type = 999;
	open_case(&header, sizeof(header), 0);
	errno = 0;
	int result = ipcap_open("/nonexistent/ipf-contract/input");
	assert(result == -1);
	puts("PCAP open: short, invalid and unknown-link headers close their descriptors");
}

static void
record(size_t fragment, int swap)
{
	packetheader_t want = { 1234, 5678, 64, 100 }, out;
	packetheader_t encoded = want;
	if (swap) {
		encoded.seconds = SWAPLONG(want.seconds);
		encoded.microseconds = SWAPLONG(want.microseconds);
		encoded.caplen = SWAPLONG(want.caplen);
		encoded.wirelen = SWAPLONG(want.wirelen);
	}
	memset(wire, 0xa5, sizeof(wire));
	memcpy(wire, &encoded, sizeof(encoded));
	offset = 0;
	available = sizeof(wire);
	chunk = fragment;
	swapped = swap;
	memset(&out, 0, sizeof(out));
	assert(ipcap_read_rec(&out) == 64);
	assert(offset == sizeof(out));
	assert(memcmp(&out, &want, sizeof(out)) == 0);
}

static void
pcap_cases(void)
{
	packetheader_t out, bad = { 0, 0, 0, 100 };
	for (size_t n = 1; n <= sizeof(out); n++) {
		record(n, 0);
		record(n, 1);
	}
	for (size_t n = 0; n < sizeof(out); n++) {
		offset = 0;
		available = n;
		chunk = 3;
		assert(ipcap_read_rec(&out) == -2);
	}
	offset = 0;
	read_error = 1;
	assert(ipcap_read_rec(&out) == -2);
	read_error = 0;
	swapped = 0;
	chunk = available = sizeof(out);
	for (int i = 0; i < 2; i++) {
		offset = 0;
		memcpy(wire, &bad, sizeof(bad));
		assert(ipcap_read_rec(&out) == (i == 0 ? -3 : -4));
		bad.caplen = 65537;
	}
	puts("PCAP: all fragment sizes, byte orders, EOF, error and length checks pass");
}

static void
icmp_cases(void)
{
	icmp_subtype_t sub[] = { { 0, "zero" }, { 7, "seven" },
	    { 255, "last" }, { -2, NULL } };
	icmp_type_t type[] = { { 0, NULL, 0, "zero" },
	    { 7, sub, 4, "seven" }, { 255, sub, 4, "last" },
	    { -2, NULL, 0, NULL } };
	for (int n = -1; n <= 256; n++) {
		int index = n == 0 ? 0 : n == 7 ? 1 : n == 255 ? 2 : -1;
		assert(find_icmpsubtype(n, sub, 4) ==
		    (index < 0 ? NULL : sub + index));
		assert(find_icmptype(n, type, 4) ==
		    (index < 0 ? NULL : type + index));
	}
	assert(find_icmpsubtype(0, NULL, 0) == NULL);
	assert(find_icmptype(0, NULL, 0) == NULL);
	assert(find_icmpsubtype(INT_MAX, sub, 4) == NULL);
	assert(find_icmptype(INT_MAX, type, 4) == NULL);
	puts("ICMP: sparse types 0..255, missing and invalid types pass");
}

int
main(int argc, char **argv)
{
	assert(argc == 2);
	if (strcmp(argv[1], "pcap") == 0)
		pcap_cases();
	else if (strcmp(argv[1], "pcap-open") == 0)
		open_cases();
	else if (strcmp(argv[1], "icmp") == 0)
		icmp_cases();
	else
		return 2;
	return 0;
}
