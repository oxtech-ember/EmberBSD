/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). Exercise the actual read-only probe. */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#ifndef __CTASSERT
#define __CTASSERT(x) _Static_assert((x), #x)
#endif
#include <dev/usb/usb.h>

enum fault {
	NONE, OPEN, INFO, NOT_HUB, REQUEST, SHORT_STATUS, CHANGED, FINAL_INFO
};
static enum fault fault;
static int ports, opened, closed, infos, requests;

static int
probe_open(const char *path, int flags, ...)
{
	assert(strcmp(path, "/dev/fixture") == 0 && flags == O_RDWR);
	opened++;
	if (fault == OPEN) {
		errno = EACCES;
		return -1;
	}
	return 17;
}

static int
probe_close(int fd)
{
	assert(fd == 17);
	closed++;
	return 0;
}

static unsigned
probe_alarm(unsigned seconds)
{
	assert(seconds == 30);
	return 0;
}

static int
probe_ioctl(int fd, unsigned long command, ...)
{
	va_list ap;
	void *data;

	assert(fd == 17);
	va_start(ap, command);
	data = va_arg(ap, void *);
	va_end(ap);
	if (command == USB_DEVICEINFO) {
		struct usb_device_info *info = data;
		infos++;
		assert(info->udi_addr == 2);
		if (fault == INFO || (fault == FINAL_INFO && infos == 2)) {
			errno = ENXIO;
			return -1;
		}
		info->udi_class = fault == NOT_HUB ? 0 : UDCLASS_HUB;
		info->udi_nports = ports;
		info->udi_cookie.cookie = fault == CHANGED ? infos : 1;
		return 0;
	}
	assert(command == USB_REQUEST);
	struct usb_ctl_request *req = data;
	usb_port_status_t *status = req->ucr_data;
	requests++;
	assert(req->ucr_addr == 2 && req->ucr_flags == 0);
	assert(req->ucr_request.bmRequestType == UT_READ_CLASS_OTHER);
	assert(req->ucr_request.bRequest == UR_GET_STATUS);
	assert(UGETW(req->ucr_request.wValue) == 0);
	assert(UGETW(req->ucr_request.wIndex) == requests);
	assert(UGETW(req->ucr_request.wLength) == sizeof(*status));
	if (fault == REQUEST && requests == 2) {
		errno = EIO;
		return -1;
	}
	USETW(status->wPortStatus, 0x0503);
	USETW(status->wPortChange, 0x0001);
	req->ucr_actlen = fault == SHORT_STATUS ? 2 : sizeof(*status);
	return 0;
}

#define open probe_open
#define close probe_close
#define alarm probe_alarm
#define ioctl probe_ioctl
#define main hub_status_main
#include "../tools/usb-hub-status.c"
#undef main

int
main(void)
{
	char *args[] = { "usb-hub-status", "/dev/fixture", "2", NULL };
	const char *invalid[] = { "", "-1", "128", "2x", "999999999999999999999" };

	assert(freopen("/dev/null", "w", stdout) != NULL);
	for (fault = NONE; fault <= FINAL_INFO; fault++) {
		ports = 4;
		opened = closed = infos = requests = 0;
		assert(hub_status_main(3, args) == (fault == NONE ? 0 : 1));
		assert(opened == 1 && closed == (fault == OPEN ? 0 : 1));
		assert(requests == (fault <= NOT_HUB && fault != NONE ? 0 : 4));
	}
	fault = NONE;
	for (ports = 0; ports <= 256; ports += ports == 0 ? 255 : 1) {
		opened = closed = infos = requests = 0;
		assert(hub_status_main(3, args) == (ports == 255 ? 0 : 1));
		assert(opened == 1 && closed == 1);
		assert(requests == (ports == 255 ? 255 : 0));
	}
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		args[2] = (char *)invalid[i];
		opened = 0;
		assert(hub_status_main(3, args) == 1 && opened == 0);
	}
	assert(hub_status_main(1, args) == 1);
	fprintf(stderr, "PASS: live hub probe admission, read-only requests and errors\n");
	return 0;
}
