/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "install.h"

/* Both files are quiescent. Peek at the type before opening a possible device. */
int
install_files_equal(const char *a, const char *b)
{
	struct stat sa, sb;
	unsigned char left[32768], right[32768];
	int x, y, result = -1;
	ssize_t n, m;

	if (lstat(a, &sa) || lstat(b, &sb))
		return -1;
	if (!S_ISREG(sa.st_mode) || !S_ISREG(sb.st_mode)) {
		errno = EINVAL;
		return -1;
	}
	x = open(a, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (x < 0)
		return -1;
	y = open(b, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (y < 0 || fstat(x, &sa) || fstat(y, &sb) ||
	    !S_ISREG(sa.st_mode) || !S_ISREG(sb.st_mode))
		goto out;
	if (sa.st_size != sb.st_size) {
		result = 0;
		goto out;
	}
	for (;;) {
		n = read(x, left, sizeof(left));
		if (n < 0)
			break;
		m = read(y, right, n ? (size_t)n : 1);
		if (m < 0)
			break;
		if (m != n || memcmp(left, right, (size_t)n) != 0) {
			result = 0;
			break;
		}
		if (n == 0) {
			result = 1;
			break;
		}
	}
out:
	if (y >= 0)
		close(y);
	close(x);
	return result;
}
