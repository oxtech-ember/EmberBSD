/* Origin: EmberBSD - service the YS-M33 external MCU watchdog over I2C. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <dev/i2c/i2c_io.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <util.h>

static volatile sig_atomic_t stopping;

static void
stop(int signo)
{
	(void)signo;
	stopping = 1;
}

static int
transfer(int fd, i2c_op_t op, void *data, size_t size)
{
	i2c_ioctl_exec_t io = {
		.iie_op = op,
		.iie_addr = 0x16,
		.iie_buf = data,
		.iie_buflen = size,
	};

	return ioctl(fd, I2C_IOCTL_EXEC, &io);
}

int
main(int argc, char **argv)
{
	unsigned char version_command[7] = {0xbb, 0x56, 0, 0, 0, 0, 0xee};
	unsigned char feed_command[7] = {0xbb, 0x46, 60, 0, 0, 0, 0xee};
	unsigned char version[3];
	const unsigned char supported[3] = {0x17, 0x03, 0x07};
	struct timespec delay = {0, 20000000}, remaining;
	bool background;
	int fd;

	if (argc != 3 ||
	    (strcmp(argv[1], "--once") != 0 &&
	    strcmp(argv[1], "--daemon") != 0))
		errx(2, "usage: mcu7502-keepalive --once|--daemon /dev/iicN");
	background = strcmp(argv[1], "--daemon") == 0;
	fd = open(argv[2], O_RDWR);
	if (fd == -1)
		err(1, "open I2C controller");
	if (transfer(fd, I2C_OP_WRITE_WITH_STOP, version_command,
	    sizeof(version_command)) == -1)
		err(1, "request MCU firmware version");
	if (nanosleep(&delay, NULL) == -1)
		err(1, "wait for MCU response");
	if (transfer(fd, I2C_OP_READ_WITH_STOP, version, sizeof(version)) == -1)
		err(1, "read MCU firmware version");
	if (memcmp(version, supported, sizeof(version)) != 0)
		errx(1, "unsupported MCU firmware %02x.%02x.%02x",
		    version[0], version[1], version[2]);
	if (transfer(fd, I2C_OP_WRITE_WITH_STOP, feed_command,
	    sizeof(feed_command)) == -1)
		err(1, "initial watchdog keepalive");
	if (!background) {
		puts("MCU7502 17.03.07: keepalive acknowledged");
		close(fd);
		return 0;
	}
	if (daemon(0, 0) == -1)
		err(1, "daemon");
	openlog("mcu7502-keepalive", LOG_PID, LOG_DAEMON);
	if (pidfile("/var/run/mcu7502_keepalive.pid") == -1) {
		syslog(LOG_ERR, "cannot create PID file: %m");
		close(fd);
		return 1;
	}
	signal(SIGTERM, stop);
	signal(SIGINT, stop);
	syslog(LOG_NOTICE, "servicing MCU7502 17.03.07 every 20 seconds");
	while (!stopping) {
		remaining.tv_sec = 20;
		remaining.tv_nsec = 0;
		while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR &&
		    !stopping)
			continue;
		if (stopping)
			break;
		if (transfer(fd, I2C_OP_WRITE_WITH_STOP, feed_command,
		    sizeof(feed_command)) == -1) {
			syslog(LOG_ERR, "watchdog keepalive failed: %m");
			close(fd);
			return 1;
		}
	}
	syslog(LOG_NOTICE, "keepalive stopped; hardware expiry remains enabled");
	close(fd);
	return 0;
}
