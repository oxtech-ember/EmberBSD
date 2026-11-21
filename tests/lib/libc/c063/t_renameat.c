/*	$NetBSD: t_renameat.c,v 1.2 2013/03/17 04:46:06 jmmv Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emmanuel Dreyfus.
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
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_renameat.c,v 1.2 2013/03/17 04:46:06 jmmv Exp $");

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define ODIR "olddir"
#define NDIR "newdir"
#define FILE "olddir/old"
#define BASEFILE "old"
#define RELFILE "../olddir/old"
#define TARGET "newdir/new"
#define BASETARGET "new"
#define FILEERR "olddir/olderr" 

ATF_TC(renameat_fd);
ATF_TC_HEAD(renameat_fd, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that renameat works with fd");
}
ATF_TC_BODY(renameat_fd, tc)
{
	int ofd, nfd, fd;
	struct stat ost, nst;

	ATF_REQUIRE(mkdir(ODIR, 0755) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) != -1);

	ATF_REQUIRE(stat(FILE, &ost) == 0);

	ATF_REQUIRE((ofd = open(ODIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE((nfd = open(NDIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(renameat(ofd, BASEFILE, nfd, BASETARGET) == 0);
	ATF_REQUIRE(close(ofd) == 0);
	ATF_REQUIRE(close(nfd) == 0);

	ATF_REQUIRE(stat(TARGET, &nst) == 0);
	ATF_REQUIRE(ost.st_ino == nst.st_ino);
}

ATF_TC(renameat_fdcwd);
ATF_TC_HEAD(renameat_fdcwd, tc)
{
	atf_tc_set_md_var(tc, "descr", 
			  "See that renameat works with fd as AT_FDCWD");
}

ATF_TC_BODY(renameat_fdcwd, tc)
{
	int fd;
	struct stat ost, nst;

	ATF_REQUIRE(mkdir(ODIR, 0755) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) != -1);

	ATF_REQUIRE(stat(FILE, &ost) == 0);

	ATF_REQUIRE(renameat(AT_FDCWD, FILE, AT_FDCWD, TARGET) == 0);

	ATF_REQUIRE(stat(TARGET, &nst) == 0);
	ATF_REQUIRE(ost.st_ino == nst.st_ino);
}

ATF_TC(renameat_fdcwderr);
ATF_TC_HEAD(renameat_fdcwderr, tc)
{
	atf_tc_set_md_var(tc, "descr", 
		  "See that renameat fails with fd as AT_FDCWD and bad path");
}
ATF_TC_BODY(renameat_fdcwderr, tc)
{
	int fd;

	ATF_REQUIRE(mkdir(ODIR, 0755) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) != -1);

	ATF_REQUIRE(renameat(AT_FDCWD, FILEERR, AT_FDCWD, TARGET) == -1);
}

ATF_TC(renameat_fderr);
ATF_TC_HEAD(renameat_fderr, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that renameat fails with fd as -1");
}
ATF_TC_BODY(renameat_fderr, tc)
{
	int fd;

	ATF_REQUIRE(mkdir(ODIR, 0755) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) != -1);

	ATF_REQUIRE(renameat(-1, FILE, AT_FDCWD, TARGET) == -1);
	ATF_REQUIRE(renameat(AT_FDCWD, FILE, -1, TARGET) == -1);
	ATF_REQUIRE(renameat(-1, FILE, -1, TARGET) == -1);
}

ATF_TC_WITHOUT_HEAD(renameat2_noreplace);
ATF_TC_BODY(renameat2_noreplace, tc)
{
	struct stat source, target, result;
	int fromfd, tofd;

	ATF_REQUIRE(mkdir(ODIR, 0700) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0700) == 0);
	ATF_REQUIRE((fromfd = open(ODIR, O_RDONLY | O_DIRECTORY)) != -1);
	ATF_REQUIRE((tofd = open(NDIR, O_RDONLY | O_DIRECTORY)) != -1);
	ATF_REQUIRE(mkdirat(fromfd, "stage", 0700) == 0);
	ATF_REQUIRE(mkdirat(tofd, "existing", 0700) == 0);
	ATF_REQUIRE(fstatat(fromfd, "stage", &source, 0) == 0);
	ATF_REQUIRE(fstatat(tofd, "existing", &target, 0) == 0);

	ATF_REQUIRE_ERRNO(EINVAL,
	    renameat2(fromfd, "stage", tofd, "new", ~0U) == -1);
	ATF_REQUIRE_ERRNO(EEXIST, renameat2(fromfd, "stage", tofd, "existing",
	    RENAME_NOREPLACE) == -1);
	ATF_REQUIRE_ERRNO(EEXIST, renameat2(fromfd, "stage", fromfd, "stage",
	    RENAME_NOREPLACE) == -1);
	ATF_REQUIRE(fstatat(tofd, "existing", &result, 0) == 0);
	ATF_REQUIRE_EQ(target.st_ino, result.st_ino);

	ATF_REQUIRE(renameat2(fromfd, "stage", tofd, "new",
	    RENAME_NOREPLACE) == 0);
	ATF_REQUIRE_ERRNO(ENOENT, fstatat(fromfd, "stage", &result, 0) == -1);
	ATF_REQUIRE(fstatat(tofd, "new", &result, 0) == 0);
	ATF_REQUIRE_EQ(source.st_ino, result.st_ino);
	ATF_REQUIRE(renameat2(tofd, "new", tofd, "existing", 0) == 0);
	ATF_REQUIRE(fstatat(tofd, "existing", &result, 0) == 0);
	ATF_REQUIRE_EQ(source.st_ino, result.st_ino);
	ATF_REQUIRE(close(fromfd) == 0);
	ATF_REQUIRE(close(tofd) == 0);
}

ATF_TC_WITHOUT_HEAD(renameat2_competing_publishers);
ATF_TC_BODY(renameat2_competing_publishers, tc)
{
	const char *names[] = { "first", "second" };
	struct stat before[2], after;
	pid_t children[2];
	int gate[2], status, winners = 0;

	ATF_REQUIRE(pipe(gate) == 0);
	for (size_t i = 0; i < __arraycount(names); i++) {
		ATF_REQUIRE(mkdir(names[i], 0700) == 0);
		ATF_REQUIRE(stat(names[i], &before[i]) == 0);
		ATF_REQUIRE((children[i] = fork()) != -1);
		if (children[i] == 0) {
			char byte;

			(void)close(gate[1]);
			if (read(gate[0], &byte, 1) != 0)
				_exit(2);
			if (renameat2(AT_FDCWD, names[i], AT_FDCWD, "final",
			    RENAME_NOREPLACE) == 0)
				_exit(0);
			_exit(errno == EEXIST ? 1 : 2);
		}
	}
	ATF_REQUIRE(close(gate[0]) == 0);
	ATF_REQUIRE(close(gate[1]) == 0);
	for (size_t i = 0; i < __arraycount(names); i++) {
		ATF_REQUIRE(waitpid(children[i], &status, 0) == children[i]);
		ATF_REQUIRE(WIFEXITED(status));
		if (WEXITSTATUS(status) == 0) {
			winners++;
			ATF_REQUIRE_ERRNO(ENOENT, stat(names[i], &after) == -1);
			ATF_REQUIRE(stat("final", &after) == 0);
		} else {
			ATF_REQUIRE_EQ(1, WEXITSTATUS(status));
			ATF_REQUIRE(stat(names[i], &after) == 0);
		}
		ATF_REQUIRE_EQ(before[i].st_ino, after.st_ino);
	}
	ATF_REQUIRE_EQ(1, winners);
}

ATF_TC_WITHOUT_HEAD(renameat2_competing_mkdir);
ATF_TC_BODY(renameat2_competing_mkdir, tc)
{
	struct stat source, target, remaining;
	pid_t children[2];
	int gate[2], status, result[2];
	char byte;

	(void)umask(0);
	for (size_t attempt = 0; attempt < 64; attempt++) {
		ATF_REQUIRE(mkdir("stage", 0700) == 0);
		ATF_REQUIRE(stat("stage", &source) == 0);
		ATF_REQUIRE(pipe(gate) == 0);

		/* Release both independent processes at the same time. */
		for (size_t i = 0; i < __arraycount(children); i++) {
			ATF_REQUIRE((children[i] = fork()) != -1);
			if (children[i] != 0)
				continue;
			(void)close(gate[1]);
			if (read(gate[0], &byte, 1) != 0)
				_exit(255);
			int rv = i == 0 ? renameat2(AT_FDCWD, "stage", AT_FDCWD,
			    "final", RENAME_NOREPLACE) : mkdir("final", 0750);
			_exit(rv == 0 ? 0 : errno);
		}
		ATF_REQUIRE(close(gate[0]) == 0);
		ATF_REQUIRE(close(gate[1]) == 0);
		for (size_t i = 0; i < __arraycount(children); i++) {
			ATF_REQUIRE(waitpid(children[i], &status, 0) == children[i]);
			ATF_REQUIRE(WIFEXITED(status));
			result[i] = WEXITSTATUS(status);
		}

		ATF_REQUIRE_MSG((result[0] == 0 && result[1] == EEXIST) ||
		    (result[0] == EEXIST && result[1] == 0),
		    "attempt %zu: renameat2 status %d, mkdir status %d",
		    attempt, result[0], result[1]);
		ATF_REQUIRE(stat("final", &target) == 0);
		ATF_REQUIRE(S_ISDIR(target.st_mode));
		if (result[0] == 0) {
			ATF_REQUIRE_EQ(source.st_ino, target.st_ino);
			ATF_REQUIRE_ERRNO(ENOENT, stat("stage", &remaining) == -1);
		} else {
			ATF_REQUIRE(source.st_ino != target.st_ino);
			ATF_REQUIRE_EQ(0750, target.st_mode & 0777);
			ATF_REQUIRE(stat("stage", &remaining) == 0);
			ATF_REQUIRE_EQ(source.st_ino, remaining.st_ino);
			ATF_REQUIRE(rmdir("stage") == 0);
		}
		ATF_REQUIRE(rmdir("final") == 0);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, renameat_fd);
	ATF_TP_ADD_TC(tp, renameat_fdcwd);
	ATF_TP_ADD_TC(tp, renameat_fdcwderr);
	ATF_TP_ADD_TC(tp, renameat_fderr);
	ATF_TP_ADD_TC(tp, renameat2_noreplace);
	ATF_TP_ADD_TC(tp, renameat2_competing_publishers);
	ATF_TP_ADD_TC(tp, renameat2_competing_mkdir);

	return atf_no_error();
}
