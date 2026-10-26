/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Required native interfaces, not an ls clone. */
#include "nbtool_config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <grp.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vis.h>
#include <wchar.h>
#include <wctype.h>

#include "compat_stat_time.h"

static int
compare(const FTSENT **a, const FTSENT **b)
{

	return (strcmp((*a)->fts_name, (*b)->fts_name));
}

int
main(void)
{
	struct stat st;
	struct timespec times[2] = { { 1700000000, 123456789 },
	    { 1700000000, 987654321 } };
	mbstate_t state;
	FTS *tree = NULL;
	FTSENT *item;
	char *paths[] = { "ls-control-fixture", NULL };
	char escaped[32], mode[16], encoded[MB_LEN_MAX];
	wchar_t wc;
	int dir = -1, fd = -1, files = 0, links = 0, result = 1, saved;

	if (setlocale(LC_ALL, "C") == NULL ||
	    strcmp(localeconv()->decimal_point, ".") != 0)
		return (1);
	setprogname("ls-control");
	if (strcmp(getprogname(), "ls-control") != 0 ||
	    user_from_uid(getuid(), 0) == NULL ||
	    group_from_gid(getgid(), 0) == NULL)
		return (2);
	strmode(S_IFREG | 0600, mode);
	if (strcmp(mode, "-rw------- ") != 0 ||
	    strvis(escaped, "a\n", VIS_NL | VIS_OCTAL | VIS_WHITE) != 5 ||
	    strcmp(escaped, "a\\012") != 0)
		return (3);
	memset(&state, 0, sizeof(state));
	if (mbrtowc(&wc, "A", 1, &state) != 1 || wc != L'A' ||
	    !iswprint(wc) || wcwidth(wc) != 1)
		return (4);
	memset(&state, 0, sizeof(state));
	if (wcrtomb(encoded, wc, &state) != 1 || encoded[0] != 'A')
		return (5);
	if (mkdir(paths[0], 0700) == -1)
		return (6);
	dir = open(paths[0], O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (dir == -1)
		goto out;
	fd = openat(dir, "file", O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	if (fd == -1 || write(fd, "x", 1) != 1 ||
	    futimens(fd, times) == -1 || fstatat(dir, "file", &st, AT_SYMLINK_NOFOLLOW) == -1)
		goto out;
	if (!S_ISREG(st.st_mode) ||
	    __nbcompat_stat_atime(&st).tv_sec != times[0].tv_sec ||
	    __nbcompat_stat_atime(&st).tv_nsec != times[0].tv_nsec ||
	    __nbcompat_stat_mtime(&st).tv_sec != times[1].tv_sec ||
	    __nbcompat_stat_mtime(&st).tv_nsec != times[1].tv_nsec ||
	    __nbcompat_stat_ctime(&st).tv_nsec < 0 ||
	    __nbcompat_stat_ctime(&st).tv_nsec >= 1000000000)
		goto out;
	if (symlinkat("file", dir, "link") == -1)
		goto out;
	tree = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, compare);
	if (tree == NULL || fts_children(tree, 0) == NULL)
		goto out;
	errno = 0;
	while ((item = fts_read(tree)) != NULL) {
		if (item->fts_info == FTS_F) {
			files++;
			if (fts_set(tree, item, FTS_NOINSTR) != 0)
				goto out;
		} else if (item->fts_info == FTS_SL)
			links++;
		else if (item->fts_info != FTS_D && item->fts_info != FTS_DP)
			goto out;
	}
	if (errno != 0 || files != 1 || links != 1)
		goto out;
	result = 0;
out:
	saved = errno;
	if (tree != NULL && fts_close(tree) == -1)
		result = 1;
	if (fd != -1 && close(fd) == -1)
		result = 1;
	if (dir != -1) {
		if (unlinkat(dir, "link", 0) == -1 && errno != ENOENT)
			result = 1;
		if (unlinkat(dir, "file", 0) == -1 && errno != ENOENT)
			result = 1;
		if (close(dir) == -1)
			result = 1;
	}
	if (rmdir(paths[0]) == -1)
		result = 1;
	errno = saved;
	return (result);
}
