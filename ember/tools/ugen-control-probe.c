/* SPDX-License-Identifier: BSD-2-Clause */
/* Measure control reads and recovery after an unsupported descriptor. */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <dev/usb/usb.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int
descriptor(int fd, unsigned int type, usb_device_descriptor_t *desc)
{
	struct usb_ctl_request req;

	memset(&req, 0, sizeof(req));
	memset(desc, 0, sizeof(*desc));
	req.ucr_request.bmRequestType = UT_READ_DEVICE;
	req.ucr_request.bRequest = UR_GET_DESCRIPTOR;
	USETW(req.ucr_request.wValue, type << 8);
	USETW(req.ucr_request.wLength, sizeof(*desc));
	req.ucr_data = desc;
	if (ioctl(fd, USB_DO_REQUEST, &req) == -1)
		return -1;
	if (req.ucr_actlen != sizeof(*desc)) {
		errno = EPROTO;
		return -1;
	}
	return 0;
}

static int
valid_descriptor(int fd)
{
	usb_device_descriptor_t desc;

	if (descriptor(fd, UDESC_DEVICE, &desc) == -1)
		return -1;
	if (desc.bLength != sizeof(desc) ||
	    desc.bDescriptorType != UDESC_DEVICE) {
		errno = EPROTO;
		return -1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct usb_device_info info;
	usb_device_descriptor_t desc;
	struct timespec start, end;
	uint64_t elapsed;
	unsigned long count, i;
	char *tail;
	int fd, timeout_ms = 1000, stall;

	if (argc != 4 || (strcmp(argv[2], "read") != 0 &&
	    strcmp(argv[2], "stall") != 0)) {
		fprintf(stderr, "usage: %s /dev/ugenN.00 read|stall count\n",
		    argv[0]);
		return EXIT_FAILURE;
	}
	errno = 0;
	count = strtoul(argv[3], &tail, 10);
	if (errno != 0 || *tail != '\0' || count == 0 || count > 10000) {
		fprintf(stderr, "count must be between 1 and 10000\n");
		return EXIT_FAILURE;
	}
	stall = strcmp(argv[2], "stall") == 0;
	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("open");
		return EXIT_FAILURE;
	}
	if (ioctl(fd, USB_GET_DEVICEINFO, &info) == -1 ||
	    ioctl(fd, USB_SET_TIMEOUT, &timeout_ms) == -1 ||
	    valid_descriptor(fd) == -1) {
		perror("initial device check");
		close(fd);
		return EXIT_FAILURE;
	}
	printf("vid=%04x pid=%04x bus=%u mode=%s count=%lu timeout_ms=%d\n",
	    info.udi_vendorNo, info.udi_productNo, info.udi_bus, argv[2],
	    count, timeout_ms);
	if (clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
		perror("clock_gettime");
		close(fd);
		return EXIT_FAILURE;
	}
	for (i = 0; i < count; i++) {
		if (stall) {
			/* Descriptor type 255 is not defined by USB 2.0. */
			if (descriptor(fd, 255, &desc) != -1 || errno != EPIPE) {
				fprintf(stderr, "request %lu: expected EPIPE, errno=%d\n",
				    i, errno);
				close(fd);
				return EXIT_FAILURE;
			}
		} else if (valid_descriptor(fd) == -1) {
			fprintf(stderr, "request %lu: %s\n", i, strerror(errno));
			close(fd);
			return EXIT_FAILURE;
		}
	}
	if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
		perror("clock_gettime");
		close(fd);
		return EXIT_FAILURE;
	}
	elapsed = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000000 +
	    end.tv_nsec - start.tv_nsec;
	if (valid_descriptor(fd) == -1) {
		perror("final descriptor recovery");
		close(fd);
		return EXIT_FAILURE;
	}
	if (close(fd) == -1) {
		perror("close");
		return EXIT_FAILURE;
	}
	printf("PASS: %lu requests, elapsed_ns=%" PRIu64
	    " mean_us=%.3f, final descriptor valid\n", count, elapsed,
	    (double)elapsed / count / 1000);
	return EXIT_SUCCESS;
}
