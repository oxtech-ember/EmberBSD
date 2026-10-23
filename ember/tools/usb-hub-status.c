/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). Read live hub port status, without resets. */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <dev/usb/usb.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	struct usb_device_info info = { 0 }, after = { 0 };
	unsigned long address;
	char *end;
	int fd, port, result = EXIT_SUCCESS;

	if (argc != 3) {
		fprintf(stderr, "usage: %s /dev/usbN hub-address\n", argv[0]);
		return EXIT_FAILURE;
	}
	errno = 0;
	address = strtoul(argv[2], &end, 10);
	if (errno != 0 || end == argv[2] || *end != '\0' ||
	    address >= USB_MAX_DEVICES) {
		fprintf(stderr, "invalid hub address\n");
		return EXIT_FAILURE;
	}
	/* The ioctl requires FWRITE even for a read-only control request. */
	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("open");
		return EXIT_FAILURE;
	}
	alarm(30);
	info.udi_addr = address;
	if (ioctl(fd, USB_DEVICEINFO, &info) == -1) {
		perror("USB_DEVICEINFO");
		close(fd);
		return EXIT_FAILURE;
	}
	if (info.udi_class != UDCLASS_HUB || info.udi_nports < 1 ||
	    info.udi_nports > 255) {
		fprintf(stderr, "device is not a hub with a valid port count\n");
		close(fd);
		return EXIT_FAILURE;
	}
	printf("bus=%u address=%u vid=%04x pid=%04x speed=%u ports=%d\n",
	    info.udi_bus, info.udi_addr, info.udi_vendorNo, info.udi_productNo,
	    info.udi_speed, info.udi_nports);
	fflush(stdout);
	for (port = 1; port <= info.udi_nports; port++) {
		struct usb_ctl_request req = { 0 };
		usb_port_status_t status = { 0 };

		req.ucr_addr = address;
		req.ucr_request.bmRequestType = UT_READ_CLASS_OTHER;
		req.ucr_request.bRequest = UR_GET_STATUS;
		USETW(req.ucr_request.wIndex, port);
		USETW(req.ucr_request.wLength, sizeof(status));
		req.ucr_data = &status;
		if (ioctl(fd, USB_REQUEST, &req) == -1) {
			fprintf(stderr, "port=%d: %s\n", port, strerror(errno));
			result = EXIT_FAILURE;
		} else if (req.ucr_actlen != sizeof(status)) {
			fprintf(stderr, "port=%d: short status (%d bytes)\n",
			    port, req.ucr_actlen);
			result = EXIT_FAILURE;
		} else {
			printf("port=%d status=0x%04x change=0x%04x\n", port,
			    UGETW(status.wPortStatus), UGETW(status.wPortChange));
		}
		fflush(stdout);
	}
	after.udi_addr = address;
	if (ioctl(fd, USB_DEVICEINFO, &after) == -1 ||
	    after.udi_cookie.cookie != info.udi_cookie.cookie) {
		fprintf(stderr, "hub changed during observation; discard results\n");
		result = EXIT_FAILURE;
	}
	close(fd);
	return result;
}
