/* SPDX-License-Identifier: BSD-2-Clause */
/* Verify the base libc linker aliases resolve to the same bytes. */
#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static ssize_t
read_block(int fd, unsigned char *buffer, size_t size)
{
	size_t used;
	ssize_t count;

	used = 0;
	while (used < size) {
		count = read(fd, buffer + used, size - used);
		if (count == -1) {
			if (errno == EINTR)
				continue;
			return (-1);
		}
		if (count == 0)
			break;
		used += (size_t)count;
	}
	return ((ssize_t)used);
}

static int
same_contents(const char *left, const char *right)
{
	unsigned char left_buffer[16384], right_buffer[16384];
	struct stat left_stat, right_stat;
	ssize_t left_count, right_count;
	int left_fd, right_fd, result;

	left_fd = open(left, O_RDONLY);
	if (left_fd == -1)
		return (-1);
	right_fd = open(right, O_RDONLY);
	if (right_fd == -1) {
		(void)close(left_fd);
		return (-1);
	}
	if (fstat(left_fd, &left_stat) == -1 || fstat(right_fd, &right_stat) == -1) {
		result = -1;
		goto done;
	}
	if (!S_ISREG(left_stat.st_mode) || !S_ISREG(right_stat.st_mode)) {
		result = 0;
		goto done;
	}
	if (left_stat.st_size != right_stat.st_size) {
		result = 0;
		goto done;
	}
	for (;;) {
		left_count = read_block(left_fd, left_buffer, sizeof(left_buffer));
		right_count = read_block(right_fd, right_buffer, sizeof(right_buffer));
		if (left_count == -1 || right_count == -1) {
			result = -1;
			goto done;
		}
		if (left_count != right_count ||
		    memcmp(left_buffer, right_buffer, (size_t)left_count) != 0) {
			result = 0;
			goto done;
		}
		if (left_count == 0)
			break;
	}
	result = 1;
done:
	(void)close(left_fd);
	(void)close(right_fd);
	return (result);
}

int
main(void)
{
	static const char *const aliases[] = {
	    "/lib/libc.so", "/lib/libc.so.12",
	    "/usr/lib/libc.so", "/usr/lib/libc.so.12"
	};
	char resolved[4][PATH_MAX];
	struct stat info;
	size_t i;

	for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
		if (realpath(aliases[i], resolved[i]) == NULL) {
			perror(aliases[i]);
			return (1);
		}
		if (resolved[i][0] != '/' || stat(resolved[i], &info) == -1) {
			perror(aliases[i]);
			return (1);
		}
		if (!S_ISREG(info.st_mode)) {
			fprintf(stderr, "libc alias is not a regular file: %s\n", aliases[i]);
			return (1);
		}
		if (i != 0) {
			int equal = same_contents(resolved[0], resolved[i]);

			if (equal == -1) {
				perror("compare libc alias bytes");
				return (1);
			}
			if (equal == 0) {
				fprintf(stderr, "libc aliases resolve to different bytes: %s\n",
				    aliases[i]);
				return (1);
			}
		}
	}

	puts("libc alias paths resolve to identical base bytes");
	return (0);
}
