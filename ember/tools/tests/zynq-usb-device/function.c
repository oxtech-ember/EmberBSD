/* SPDX-License-Identifier: BSD-2-Clause */
/* Explicit-identity counter or loopback function for physical qualification. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <dev/usb/usbperiph.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "pattern.h"

static unsigned long long
number(const char *s, unsigned long long max)
{
	char *end;
	unsigned long long value;
	errno = 0;
	value = strtoull(s, &end, 0);
	if (errno != 0 || s == end || *end != '\0' || *s == '-' || value > max)
		errx(1, "invalid number: %s", s);
	return value;
}

int
main(int argc, char **argv)
{
	struct usbperiph_config config = { .version = USBPERIPH_ABI };
	struct usbperiph_event event;
	uint8_t buffer[USBPERIPH_MAX_XFER];
	uint64_t bytes, total = 0;
	unsigned vid, pid;
	bool loopback;
	int fd;
	const uint8_t device[18] = {18, 1, 0, 2, 0, 0, 0, 64,
	    0, 0, 0, 0, 0, 1, 0, 0, 0, 1};
	const uint8_t desc[32] = {9, 2, 32, 0, 1, 1, 0, 0x80, 50,
	    9, 4, 0, 0, 2, 0xff, 0, 0, 0,
	    7, 5, 0x81, 2, 0, 2, 0, 7, 5, 1, 2, 0, 2, 0};

	const uint8_t strings[] = {
	    4, 3, 9, 4, 18, 3, 69, 0, 109, 0, 98, 0,
	    101, 0, 114, 0, 66, 0, 83, 0, 68, 0, 48, 3,
	    69, 0, 109, 0, 98, 0, 101, 0, 114, 0, 66, 0,
	    83, 0, 68, 0, 32, 0, 85, 0, 83, 0, 66, 0,
	    32, 0, 100, 0, 105, 0, 97, 0, 103, 0, 110, 0,
	    111, 0, 115, 0, 116, 0, 105, 0, 99, 0
	};

	if (argc != 6 || (strcmp(argv[2], "counter") != 0 &&
	    strcmp(argv[2], "loopback") != 0))
		errx(1, "usage: %s device counter|loopback owned-vid owned-pid bytes",
		    argv[0]);
	loopback = strcmp(argv[2], "loopback") == 0;
	vid = number(argv[3], 65535);
	pid = number(argv[4], 65535);
	bytes = number(argv[5], UINT64_MAX);
	if (vid == 0 || pid == 0 || bytes == 0)
		errx(1, "identity and byte count must be nonzero");
	memcpy(config.device, device, sizeof(device));
	config.device[14] = 1;
	config.device[15] = 2;
	config.device[8] = vid;
	config.device[9] = vid >> 8;
	config.device[10] = pid;
	config.device[11] = pid >> 8;
	memcpy(config.config, desc, sizeof(desc));
	config.config_length = sizeof(desc);
	memcpy(config.strings, strings, sizeof(strings));
	config.strings_length = sizeof(strings);
	fd = open(argv[1], O_RDWR);
	if (fd < 0)
		err(1, "open");
	if (ioctl(fd, USBPERIPH_IOC_CONFIGURE, &config) < 0 ||
	    ioctl(fd, USBPERIPH_IOC_CONNECT) < 0)
		err(1, "configure/connect");
	for (;;) {
		if (ioctl(fd, USBPERIPH_IOC_GET_EVENT, &event) < 0) {
			if (errno == EWOULDBLOCK)
				continue;
			err(1, "enumeration event");
		}
		if (event.type == USBPERIPH_EVENT_CONFIGURE && event.configured == 1)
			break;
	}
	while (total < bytes) {
		struct usbperiph_xfer x = {
			.buffer = (uintptr_t)buffer, .timeout_ms = 10000,
			.length = bytes - total < sizeof(buffer) ?
			    bytes - total : sizeof(buffer)
		};
		if (loopback) {
			x.endpoint = 1;
			if (ioctl(fd, USBPERIPH_IOC_XFER, &x) < 0)
				err(1, "loopback receive");
			if (x.actual == 0)
				continue;
			x.length = x.actual;
		} else {
			usbperiph_counter(buffer, x.length, total);
		}
		x.endpoint = 0x81;
		if (ioctl(fd, USBPERIPH_IOC_XFER, &x) < 0)
			err(1, "bulk transmit");
		if (x.actual != x.length)
			errx(1, "incomplete transmit");
		total += x.actual;
	}
	printf("completed %llu bytes\n", (unsigned long long)total);
	if (ioctl(fd, USBPERIPH_IOC_DISCONNECT) < 0 || close(fd) < 0)
		err(1, "disconnect/close");
	return 0;
}
