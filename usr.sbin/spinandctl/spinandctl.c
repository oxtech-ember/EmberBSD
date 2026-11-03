/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <dev/spi/spi_io.h>

#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SPINAND_CMD_READ_ID	0x9f
#define SPINAND_CMD_GET_FEATURE	0x0f
#define SPINAND_FEATURE_STATUS	0xc0
#define SPINAND_READ_ID_DUMMY	0x00
#define SPINAND_READ_ID_LEN	2

static int
spinand_transfer(int fd, const uint8_t *command, size_t command_len,
    uint8_t *response, size_t response_len)
{
	spi_ioctl_transfer_t transfer = {
		.sit_addr = 0,
		.sit_send = command,
		.sit_sendlen = command_len,
		.sit_recv = response,
		.sit_recvlen = response_len,
	};

	return ioctl(fd, SPI_IOCTL_TRANSFER, &transfer);
}

int
main(int argc, char **argv)
{
	static const char usage[] = "usage: spinandctl -d /dev/spiN";
	struct spi_ioctl_configure configure = {
		.sic_addr = 0,
		.sic_mode = 0,
		.sic_speed = 1000000,
	};
	uint8_t command[2], id[SPINAND_READ_ID_LEN], status;
	const char *device = NULL;
	int ch, fd;

	while ((ch = getopt(argc, argv, "d:")) != -1) {
		switch (ch) {
		case 'd':
			device = optarg;
			break;
		default:
			errx(EXIT_FAILURE, "%s", usage);
		}
	}
	if (device == NULL || optind != argc)
		errx(EXIT_FAILURE, "%s", usage);

	fd = open(device, O_RDWR);
	if (fd == -1)
		err(EXIT_FAILURE, "%s", device);
	if (ioctl(fd, SPI_IOCTL_CONFIGURE, &configure) == -1)
		err(EXIT_FAILURE, "SPI configure");

	command[0] = SPINAND_CMD_READ_ID;
	command[1] = SPINAND_READ_ID_DUMMY;
	if (spinand_transfer(fd, command, sizeof(command), id, sizeof(id)) == -1)
		err(EXIT_FAILURE, "READ ID");
	if ((id[0] == 0x00 && id[1] == 0x00) ||
	    (id[0] == 0xff && id[1] == 0xff))
		errx(EXIT_FAILURE, "no SPI NAND ID response");

	command[0] = SPINAND_CMD_GET_FEATURE;
	command[1] = SPINAND_FEATURE_STATUS;
	if (spinand_transfer(fd, command, sizeof(command), &status,
	    sizeof(status)) == -1)
		err(EXIT_FAILURE, "GET FEATURE status");

	(void)printf("Manufacturer ID: %02x\nDevice ID: %02x\n",
	    id[0], id[1]);
	(void)printf("status register C0: %02x\n", status);

	close(fd);
	return EXIT_SUCCESS;
}
