/* $NetBSD: t_dir.c,v 1.11 2018/06/19 09:20:46 gson Exp $ */

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fs/tmpfs/tmpfs_args.h>

#include <assert.h>
#include <atf-c.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



ATF_TC(seekdir_basic);
ATF_TC_HEAD(seekdir_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check telldir(3) and seekdir(3) "
	    "for correct behavior (PR lib/24324)");
}

ATF_TC_BODY(seekdir_basic, tc)
{
	DIR *dp;
	char *wasname;
	struct dirent *entry;
	long here;

#define	CREAT(x, m)	do {						\
		int _creat_fd;						\
		ATF_REQUIRE_MSG((_creat_fd = creat((x), (m))) != -1,	\
		    "creat(%s, %x) failed: %s", (x), (m),		\
		    strerror(errno));					\
		(void)close(_creat_fd);					\
	} while (0)

	ATF_REQUIRE_MSG(mkdir("t", 0755) == 0,
	    "mkdir failed: %s", strerror(errno));
	CREAT("t/a", 0600);
	CREAT("t/b", 0600);
	CREAT("t/c", 0600);

	dp = opendir("t");
	if ( dp == NULL)
		atf_tc_fail("Could not open temp directory.");

	/* skip two for . and .. */
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    ".", strerror(errno));

	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "..", strerror(errno));

	/* get first entry */
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "first", strerror(errno));

	here = telldir(dp);
	ATF_REQUIRE_MSG(here != -1, "telldir failed: %s", strerror(errno));

	/* get second entry */
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "second", strerror(errno));

	wasname = strdup(entry->d_name);
	if (wasname == NULL)
		atf_tc_fail("cannot allocate memory");

	/* get third entry */
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "third", strerror(errno));

	/* try to return to the position after the first entry */
	seekdir(dp, here);
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "first[1]", strerror(errno));
	if (strcmp(entry->d_name, wasname) != 0)
		atf_tc_fail("1st seekdir found wrong name");

	/* try again, and throw in a telldir() for good measure */
	seekdir(dp, here);
	here = telldir(dp);
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "second[1]", strerror(errno));
	if (strcmp(entry->d_name, wasname) != 0)
		atf_tc_fail("2nd seekdir found wrong name");

	/* One more time, to make sure that telldir() doesn't affect result */
	seekdir(dp, here);
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "third[1]", strerror(errno));

	if (strcmp(entry->d_name, wasname) != 0)
		atf_tc_fail("3rd seekdir found wrong name");

	closedir(dp);
	free(wasname);
}

ATF_TC(telldir_leak);
ATF_TC_HEAD(telldir_leak, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Check telldir(3) for memory leakage (PR lib/24324)");
}

ATF_TC_BODY(telldir_leak, tc)
{
	DIR *dp;
	char *memused;
	int i;
	int oktouse = 4096;

	dp = opendir(".");
	if (dp == NULL)
		atf_tc_fail("Could not open current directory");

	(void)telldir(dp);
	memused = sbrk(0);
	closedir(dp);

	for (i = 0; i < 1000; i++) {
		dp = opendir(".");
		if (dp == NULL)
			atf_tc_fail("Could not open current directory");

		(void)telldir(dp);
		closedir(dp);

		if ((char *)sbrk(0) - memused > oktouse) {
			(void)printf("Used %td extra bytes for %d telldir "
			    "calls", ((char *)sbrk(0) - memused), i);
			oktouse = (char *)sbrk(0) - memused;
		}
	}
	if (oktouse > 4096) {
		atf_tc_fail("Failure: leaked %d bytes", oktouse);
	} else {
		(void)printf("OK: used %td bytes\n", (char *)(sbrk(0))-memused);
	}
}

/*
 * Origin: EmberBSD (AI-assisted).  getdents(2) resumes at the position
 * of the next entry.  A remove or rename of that entry before the next
 * call must not make the call fail.  FFS and tmpfs returned EINVAL.
 * ext2fs returned the removed entry.  Run the test cases in a work
 * directory on each file system to check.
 */

#define	GD_NFILES	64
#define	GD_BIGNFILES	300
#define	GD_RUN		100
#define	GD_SMALLBUF	512
#define	GD_BIGBUF	65536
#define	GD_PAGEBUF	4096
#define	GD_EDGEBUF	4864
#define	GD_RACE_FILES	300
#define	GD_RACE_WRITERS	4
#define	GD_RACE_LOOPS	2000

struct gd_names {
	size_t	n;
	char	name[GD_BIGNFILES + 4][MAXNAMLEN + 1];
};

static void
gd_touch(const char *path)
{
	int fd;

	fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	ATF_REQUIRE_MSG(fd != -1, "open(%s): %s", path, strerror(errno));
	(void)close(fd);
}

static void
gd_create(const char *dir, int nfiles)
{
	char path[MAXPATHLEN];
	int i;

	ATF_REQUIRE_MSG(mkdir(dir, 0755) == 0, "mkdir(%s): %s", dir,
	    strerror(errno));
	for (i = 0; i < nfiles; i++) {
		snprintf(path, sizeof(path), "%s/entry-%03d", dir, i);
		gd_touch(path);
	}
}

/* Read one block of entries; return the number of bytes. */
static ssize_t
gd_read(int fd, size_t bufsize, struct gd_names *gn)
{
	static char buf[GD_BIGBUF];
	struct dirent *dp;
	ssize_t len, off;

	ATF_REQUIRE(bufsize <= sizeof(buf));
	len = getdents(fd, buf, bufsize);
	ATF_REQUIRE_MSG(len != -1, "getdents: %s", strerror(errno));
	for (off = 0; off < len; off += dp->d_reclen) {
		dp = (struct dirent *)(void *)(buf + off);
		if (dp->d_reclen == 0)
			break;
		if (dp->d_fileno == 0 || strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;
		ATF_REQUIRE(gn->n < __arraycount(gn->name));
		strlcpy(gn->name[gn->n++], dp->d_name, sizeof(gn->name[0]));
	}
	return len;
}

static size_t
gd_count(const struct gd_names *gn, const char *name)
{
	size_t i, n;

	for (i = n = 0; i < gn->n; i++)
		if (strcmp(gn->name[i], name) == 0)
			n++;
	return n;
}

static void
gd_remove(const char *dir, const struct gd_names *gn)
{
	char path[MAXPATHLEN];
	size_t i;

	for (i = 0; i < gn->n; i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, gn->name[i]);
		(void)unlink(path);
	}
	snprintf(path, sizeof(path), "%s/source", dir);
	(void)unlink(path);
	ATF_REQUIRE_MSG(rmdir(dir) == 0, "rmdir(%s): %s", dir,
	    strerror(errno));
}

/*
 * Read part of a directory of NFILES files with a FIRSTBUF byte buffer.
 * Then remove the NREMOVE entries from the position of the next
 * getdents(2) call: unlink them, or rename another file over the first.
 * Read the rest with a RESTBUF byte buffer and check that each other
 * entry is returned once.
 */
static void
gd_modify_one(const char *dir, int nfiles, size_t firstbuf,
    bool rename_over, size_t nremove, size_t restbuf)
{
	static struct gd_names before, all, after;
	char path[MAXPATHLEN], src[MAXPATHLEN];
	size_t i, j, n, first;
	int fd;

	memset(&before, 0, sizeof(before));
	memset(&all, 0, sizeof(all));
	memset(&after, 0, sizeof(after));
	gd_create(dir, nfiles);
	snprintf(src, sizeof(src), "%s/source", dir);
	if (rename_over)
		gd_touch(src);

	ATF_REQUIRE((fd = open(dir, O_RDONLY | O_DIRECTORY)) != -1);
	while (gd_read(fd, GD_BIGBUF, &all) > 0)
		continue;
	(void)close(fd);

	ATF_REQUIRE((fd = open(dir, O_RDONLY | O_DIRECTORY)) != -1);
	ATF_REQUIRE(gd_read(fd, firstbuf, &before) > 0);
	ATF_REQUIRE(before.n > 0);
	for (i = 0; i < all.n; i++)
		if (strcmp(all.name[i], before.name[before.n - 1]) == 0)
			break;
	first = i + 1;
	ATF_REQUIRE_MSG(first + nremove < all.n,
	    "first read returned too much");

	for (j = first; j < first + nremove; j++) {
		snprintf(path, sizeof(path), "%s/%s", dir, all.name[j]);
		if (rename_over)
			ATF_REQUIRE_MSG(rename(src, path) == 0, "rename: %s",
			    strerror(errno));
		else
			ATF_REQUIRE_MSG(unlink(path) == 0, "unlink: %s",
			    strerror(errno));
	}

	while (gd_read(fd, restbuf, &after) > 0)
		continue;
	(void)close(fd);

	for (i = 0; i < all.n; i++) {
		if (strcmp(all.name[i], "source") == 0)
			continue;
		n = gd_count(&before, all.name[i]) +
		    gd_count(&after, all.name[i]);
		if (i >= first && i < first + nremove)
			ATF_CHECK_MSG(n <= (rename_over ? 1 : 0),
			    "removed %s returned %zu times (first read %zu "
			    "bytes)", all.name[i], n, firstbuf);
		else
			ATF_CHECK_MSG(n == 1, "%s returned %zu times (first "
			    "read %zu bytes)", all.name[i], n, firstbuf);
	}
	ATF_CHECK_MSG(gd_count(&after, "source") == 0,
	    "renamed source returned");
	gd_remove(dir, &all);
}

/*
 * Vary the first buffer, so that the next entry is also the last entry
 * of a directory block.  A first buffer of a page or more stops in, at
 * the end of, or after the first 4 KB ext2fs block.
 */
static void
gd_modify_next(const char *base, bool rename_over)
{
	char dir[MAXPATHLEN];
	size_t firstbuf;

	for (firstbuf = GD_SMALLBUF; firstbuf < 2 * GD_SMALLBUF;
	    firstbuf += 8) {
		snprintf(dir, sizeof(dir), "%s/d%zu", base, firstbuf);
		gd_modify_one(dir, GD_NFILES, firstbuf, rename_over, 1,
		    GD_SMALLBUF);
	}
	for (firstbuf = GD_PAGEBUF; firstbuf < GD_PAGEBUF + GD_SMALLBUF;
	    firstbuf += 64) {
		snprintf(dir, sizeof(dir), "%s/d%zu", base, firstbuf);
		gd_modify_one(dir, GD_BIGNFILES, firstbuf, rename_over, 1,
		    GD_SMALLBUF);
	}
	for (firstbuf = GD_EDGEBUF; firstbuf < GD_EDGEBUF + 80;
	    firstbuf += 8) {
		snprintf(dir, sizeof(dir), "%s/d%zu", base, firstbuf);
		gd_modify_one(dir, GD_BIGNFILES, firstbuf, rename_over, 1,
		    GD_SMALLBUF);
	}
}

/*
 * Remove a run of entries from the read position to a later block and
 * read the rest with a small buffer.  The file system must read the
 * next blocks when it skips all entries of the blocks read.
 */
static void
gd_unlink_run(const char *base)
{
	char dir[MAXPATHLEN];
	size_t firstbuf;

	for (firstbuf = GD_SMALLBUF; firstbuf < GD_PAGEBUF + GD_SMALLBUF;
	    firstbuf += 256) {
		snprintf(dir, sizeof(dir), "%s/r%zu", base, firstbuf);
		gd_modify_one(dir, GD_BIGNFILES, firstbuf, false, GD_RUN,
		    GD_SMALLBUF);
	}
}

/*
 * Read the directory with readdir(3) while other processes create,
 * rename over and remove files in it, as parallel make(1) jobs do.
 */
static void
gd_race(const char *base)
{
	char dir[MAXPATHLEN], p[MAXPATHLEN], q[MAXPATHLEN];
	pid_t parent, pid[GD_RACE_WRITERS];
	unsigned int n;
	long failed, lasterr;
	DIR *dp;
	int i, w;

	snprintf(dir, sizeof(dir), "%s/race", base);
	gd_create(dir, GD_RACE_FILES);

	parent = getpid();
	for (w = 0; w < GD_RACE_WRITERS; w++) {
		pid[w] = fork();
		ATF_REQUIRE(pid[w] != -1);
		if (pid[w] != 0)
			continue;
		for (n = 0; getppid() == parent; n++) {
			snprintf(p, sizeof(p), "%s/t-%d-%u.h-t", dir, w,
			    n % 50);
			snprintf(q, sizeof(q), "%s/t-%d-%u.h", dir, w,
			    n % 50);
			(void)close(open(p, O_CREAT | O_WRONLY | O_TRUNC,
			    0644));
			(void)rename(p, q);
			if (n % 3 == 0)
				(void)unlink(q);
		}
		_exit(0);
	}

	failed = lasterr = 0;
	for (i = 0; i < GD_RACE_LOOPS; i++) {
		if ((dp = opendir(dir)) == NULL) {
			lasterr = errno;
			failed++;
			continue;
		}
		errno = 0;
		while (readdir(dp) != NULL)
			continue;
		if (errno != 0) {
			lasterr = errno;
			failed++;
		}
		(void)closedir(dp);
	}

	for (w = 0; w < GD_RACE_WRITERS; w++) {
		(void)kill(pid[w], SIGKILL);
		(void)waitpid(pid[w], NULL, 0);
	}
	ATF_CHECK_MSG(failed == 0, "%ld of %d directory reads failed: %s",
	    failed, GD_RACE_LOOPS, strerror((int)lasterr));
}

ATF_TC(getdents_unlink_next);
ATF_TC_HEAD(getdents_unlink_next, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check getdents(2) after unlink(2) "
	    "of the entry at the read position");
	atf_tc_set_md_var(tc, "timeout", "300");
}

ATF_TC_BODY(getdents_unlink_next, tc)
{

	gd_modify_next(".", false);
}

ATF_TC(getdents_rename_next);
ATF_TC_HEAD(getdents_rename_next, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check getdents(2) after rename(2) "
	    "over the entry at the read position");
	atf_tc_set_md_var(tc, "timeout", "300");
}

ATF_TC_BODY(getdents_rename_next, tc)
{

	gd_modify_next(".", true);
}

ATF_TC(getdents_unlink_run);
ATF_TC_HEAD(getdents_unlink_run, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check getdents(2) after unlink(2) "
	    "of a run of entries from the read position");
	atf_tc_set_md_var(tc, "timeout", "300");
}

ATF_TC_BODY(getdents_unlink_run, tc)
{

	gd_unlink_run(".");
}

ATF_TC(readdir_race);
ATF_TC_HEAD(readdir_race, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check readdir(3) while other "
	    "processes create, rename and remove files");
	atf_tc_set_md_var(tc, "timeout", "300");
}

ATF_TC_BODY(readdir_race, tc)
{

	gd_race(".");
}

#define	GD_TMPFS	"tmpfs"

static void
gd_mount_tmpfs(void)
{
	struct tmpfs_args args;

	ATF_REQUIRE(mkdir(GD_TMPFS, 0755) == 0);
	memset(&args, 0, sizeof(args));
	args.ta_version = TMPFS_ARGS_VERSION;
	args.ta_root_mode = 0755;
	if (mount(MOUNT_TMPFS, GD_TMPFS, 0, &args, sizeof(args)) == -1) {
		if (errno == EOPNOTSUPP || errno == ENODEV)
			atf_tc_skip("tmpfs is not available");
		atf_tc_fail("mount tmpfs: %s", strerror(errno));
	}
}

ATF_TC_WITH_CLEANUP(getdents_tmpfs);
ATF_TC_HEAD(getdents_tmpfs, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check getdents(2) and readdir(3) "
	    "with concurrent changes on tmpfs");
	atf_tc_set_md_var(tc, "require.user", "root");
	atf_tc_set_md_var(tc, "timeout", "300");
}

ATF_TC_BODY(getdents_tmpfs, tc)
{

	gd_mount_tmpfs();
	ATF_REQUIRE(mkdir(GD_TMPFS "/unlink", 0755) == 0);
	gd_modify_next(GD_TMPFS "/unlink", false);
	ATF_REQUIRE(mkdir(GD_TMPFS "/rename", 0755) == 0);
	gd_modify_next(GD_TMPFS "/rename", true);
	gd_unlink_run(GD_TMPFS);
	gd_race(GD_TMPFS);
}

ATF_TC_CLEANUP(getdents_tmpfs, tc)
{

	(void)unmount(GD_TMPFS, MNT_FORCE);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, seekdir_basic);
	ATF_TP_ADD_TC(tp, telldir_leak);
	ATF_TP_ADD_TC(tp, getdents_unlink_next);
	ATF_TP_ADD_TC(tp, getdents_rename_next);
	ATF_TP_ADD_TC(tp, getdents_unlink_run);
	ATF_TP_ADD_TC(tp, readdir_race);
	ATF_TP_ADD_TC(tp, getdents_tmpfs);

	return atf_no_error();
}
