/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static jmp_buf error_exit;
static unsigned int transfer_count, close_count;
static int id_kind, fail_status, error_exit_value;
static char error_message[128];

#define main spinandctl_main
#include "../spinandctl.c"
#undef main

int
open(const char *path, int flags, ...)
{
	assert(strcmp(path, "/dev/spi0") == 0);
	assert(flags == O_RDWR);
	return 42;
}

int
close(int fd)
{
	assert(fd == 42);
	close_count++;
	return 0;
}

int
ioctl(int fd, unsigned long command, ...)
{
	va_list ap;
	void *data;

	assert(fd == 42);
	va_start(ap, command);
	data = va_arg(ap, void *);
	va_end(ap);
	if (command == SPI_IOCTL_CONFIGURE) {
		spi_ioctl_configure_t *config = data;
		assert(config->sic_addr == 0);
		assert(config->sic_mode == 0);
		assert(config->sic_speed == 1000000);
		return 0;
	}
	assert(command == SPI_IOCTL_TRANSFER);
	spi_ioctl_transfer_t *transfer = data;
	transfer_count++;
	assert(transfer->sit_addr == 0);
	assert(transfer->sit_sendlen == 2);
	if (transfer_count == 1) {
		const uint8_t *send = transfer->sit_send;
		uint8_t *recv = transfer->sit_recv;
		assert(send[0] == 0x9f);
		assert(send[1] == 0x00);
		assert(transfer->sit_recvlen == 2);
		if (id_kind == 1)
			memset(recv, 0x00, 2);
		else if (id_kind == 2)
			memset(recv, 0xff, 2);
		else {
			recv[0] = 0xc8;
			recv[1] = 0x91;
		}
		return 0;
	}
	assert(transfer_count == 2);
	const uint8_t *send = transfer->sit_send;
	assert(send[0] == 0x0f);
	assert(send[1] == 0xc0);
	assert(transfer->sit_recvlen == 1);
	if (fail_status) {
		errno = EIO;
		return -1;
	}
	*(uint8_t *)transfer->sit_recv = 0x40;
	return 0;
}

void
err(int eval, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(error_message, sizeof(error_message), fmt, ap);
	va_end(ap);
	error_exit_value = eval;
	longjmp(error_exit, 1);
}

void
errx(int eval, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(error_message, sizeof(error_message), fmt, ap);
	va_end(ap);
	error_exit_value = eval;
	longjmp(error_exit, 1);
}

static int
run(void)
{
	char *argv[] = { "spinandctl", "-d", "/dev/spi0", NULL };
	optind = 1;
	return spinandctl_main(3, argv);
}

int
main(void)
{
	/* Successful production path performs ID first, then status. */
	transfer_count = close_count = 0;
	id_kind = fail_status = 0;
	assert(run() == EXIT_SUCCESS);
	assert(transfer_count == 2 && close_count == 1);

	/* Empty and erased buses must not be reported as detected NAND. */
	for (id_kind = 1; id_kind <= 2; id_kind++) {
		transfer_count = close_count = 0;
		if (setjmp(error_exit) == 0) {
			(void)run();
			assert(0);
		}
		assert(error_exit_value == EXIT_FAILURE);
		assert(strcmp(error_message, "no SPI NAND ID response") == 0);
		assert(transfer_count == 1 && close_count == 0);
	}

	/* A failed second transaction propagates its specific stage. */
	transfer_count = close_count = 0;
	id_kind = 0;
	fail_status = 1;
	if (setjmp(error_exit) == 0) {
		(void)run();
		assert(0);
	}
	assert(error_exit_value == EXIT_FAILURE);
	assert(strcmp(error_message, "GET FEATURE status") == 0);
	assert(transfer_count == 2 && close_count == 0);
	return 0;
}
