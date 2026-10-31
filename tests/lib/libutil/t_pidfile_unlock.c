/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <util.h>
#ifdef PIDFILE_ATF
#include <atf-c.h>
#endif

static int
run_tests(void)
{
	char dir[] = "/tmp/pidfile-test.XXXXXXXX";
	char path[sizeof(dir) + 8];
	struct stat st;
	pid_t child;
	int status;

	if (pidfile_unlock() != -1 || errno != EBADF || mkdtemp(dir) == NULL)
		return 1;
	if (snprintf(path, sizeof(path), "%s/pid", dir) >= (int)sizeof(path))
		return 2;
	if (pidfile_lock(path) != 0)
		return 3;
	child = fork();
	if (child == -1)
		return 4;
	if (child == 0) {
		if (pidfile_unlock() != 0)
			_exit(1);
		if (pidfile_lock(path) != getppid() || errno != EEXIST)
			_exit(2);
		_exit(0);
	}
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0 || stat(path, &st) != 0 || st.st_size == 0)
		return 5;
	if (pidfile_unlock() != 0 || stat(path, &st) != 0 || st.st_size == 0)
		return 6;
	if (pidfile_unlock() != -1 || errno != EBADF || unlink(path) != 0)
		return 7;
	if (pidfile_lock(path) != 0)
		return 8;
	if (pidfile_clean() != 0 || access(path, F_OK) != -1 || errno != ENOENT)
		return 9;
	if (pidfile_lock(path) != 0)
		return 10;
	pidfile_unremoveable();
	if (pidfile_clean() != 0 || stat(path, &st) != 0 || st.st_size != 0)
		return 11;
	if (unlink(path) != 0 || rmdir(dir) != 0)
		return 12;
	return 0;
}

#ifdef PIDFILE_ATF
ATF_TC_WITHOUT_HEAD(unlock);
ATF_TC_BODY(unlock, tc)
{
	ATF_REQUIRE_EQ(run_tests(), 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, unlock);
	return atf_no_error();
}
#else
int
main(void)
{
	return run_tests();
}
#endif
