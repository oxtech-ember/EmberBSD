/* SPDX-License-Identifier: BSD-2-Clause */
/* Real filesystem/flock test; PAM calls are minimal in-process doubles. */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <security/pam_appl.h>
#include <security/pam_modules.h>

static const char *test_user = "root";
struct pam_handle {
	void *data;
	void (*cleanup)(pam_handle_t *, void *, int);
	char env[512];
	int fail_env;
	int fail_data;
};
int pam_get_user(pam_handle_t *h, const char **user, const char *prompt)
{
	(void)h;
	(void)prompt;
	*user = test_user;
	return PAM_SUCCESS;
}
int pam_get_data(const pam_handle_t *h, const char *key, const void **v)
{
	(void)key;
	*v = h->data;
	return h->data ? PAM_SUCCESS : PAM_NO_MODULE_DATA;
}
int pam_set_data(pam_handle_t *h, const char *key, void *v,
		 void (*f)(pam_handle_t *, void *, int))
{
	(void)key;
	if (h->fail_data)
		return PAM_BUF_ERR;
	if (h->data && h->cleanup)
		h->cleanup(h, h->data, 0);
	h->data = v;
	h->cleanup = f;
	return PAM_SUCCESS;
}
int pam_putenv(pam_handle_t *h, const char *s)
{
	if (h->fail_env)
		return PAM_BUF_ERR;
	if (strchr(s, '=') == NULL)
		h->env[0] = '\0';
	else
		snprintf(h->env, sizeof(h->env), "%s", s);
	return PAM_SUCCESS;
}
static ino_t injected_inode;
static dev_t injected_device;
static int inject_fstat(int fd, struct stat *st)
{
	int rc = fstat(fd, st);
	if (rc == 0 && injected_inode && st->st_ino == injected_inode &&
	    st->st_dev == injected_device)
		st->st_dev++;
	return rc;
}
static int inject_fstatat(int fd, const char *name, struct stat *st, int flags)
{
	int rc = fstatat(fd, name, st, flags);
	if (rc == 0 && injected_inode && st->st_ino == injected_inode &&
	    st->st_dev == injected_device)
		st->st_dev++;
	return rc;
}
#define fstat inject_fstat
#define fstatat inject_fstatat
#if !defined(XDG_CONTROL) || !defined(XDG_RUNTIME)
#error "Select unique XDG_CONTROL and XDG_RUNTIME test paths"
#endif
#include "../pam_xdg.c"
#undef fstat
#undef fstatat
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "FAIL line %d: %s errno=%d\n",         \
				__LINE__, #x, errno);                          \
			exit(1);                                               \
		}                                                              \
	} while (0)
static void end(pam_handle_t *h)
{
	if (h->cleanup) {
		h->cleanup(h, h->data, 0);
		h->data = NULL;
		h->cleanup = NULL;
	}
}
static int exists(const char *p)
{
	struct stat st;
	return lstat(p, &st) == 0;
}
int main(void)
{
	pam_handle_t a = {0}, b = {0}, c = {0};
	char dir[512], target[512], outside[512], sub[512];
	int fd, status;
	pid_t pid;
	CHECK(geteuid() == 0);
	CHECK(!exists(XDG_CONTROL) && !exists(XDG_RUNTIME));
	snprintf(dir, sizeof(dir), "%s/0", XDG_RUNTIME);
	snprintf(outside, sizeof(outside), "%s-outside", XDG_RUNTIME);
	snprintf(target, sizeof(target), "%s/marker", outside);
	CHECK(mkdir(outside, 0700) == 0);
	fd = open(target, O_WRONLY | O_CREAT | O_EXCL, 0600);
	CHECK(fd >= 0);
	CHECK(write(fd, "safe", 4) == 4);
	close(fd);
	umask(0777);
	CHECK(pam_sm_open_session(&a, 0, 0, NULL) == PAM_SUCCESS);
	umask(0022);
	CHECK(pam_sm_open_session(&a, 0, 0, NULL) == PAM_SESSION_ERR);
	pid = fork();
	CHECK(pid >= 0);
	if (pid == 0)
		_exit(pam_sm_close_session(&a, 0, 0, NULL) == PAM_SESSION_ERR
			  ? 0
			  : 1);
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
	      WEXITSTATUS(status) == 0);
	CHECK(exists(dir));
	CHECK(pam_sm_open_session(&b, 0, 0, NULL) == PAM_SUCCESS);
	CHECK(!strcmp(a.env, "XDG_RUNTIME_DIR=" XDG_RUNTIME "/0"));
	snprintf(sub, sizeof(sub), "%s/escape", dir);
	CHECK(symlink(outside, sub) == 0);
	CHECK(pam_sm_close_session(&a, 0, 0, NULL) == PAM_SUCCESS);
	CHECK(a.env[0] == '\0');
	end(&a);
	CHECK(exists(dir));
	CHECK(exists(target));
	CHECK(pam_sm_close_session(&b, 0, 0, NULL) == PAM_SUCCESS);
	CHECK(!exists(dir));
	CHECK(exists(target));
	CHECK(pam_sm_open_session(&b, 0, 0, NULL) == PAM_SUCCESS);
	CHECK(pam_sm_close_session(&b, 0, 0, NULL) == PAM_SUCCESS);
	end(&b);
	CHECK(!exists(dir));
	CHECK(mkdir(dir, 0755) == 0);
	CHECK(pam_sm_open_session(&a, 0, 0, NULL) == PAM_SESSION_ERR);
	CHECK(rmdir(dir) == 0);
	CHECK(symlink(outside, dir) == 0);
	CHECK(pam_sm_open_session(&a, 0, 0, NULL) == PAM_SESSION_ERR);
	CHECK(unlink(dir) == 0);
	a.fail_data = 1;
	CHECK(pam_sm_open_session(&a, 0, 0, NULL) == PAM_SESSION_ERR);
	CHECK(a.env[0] == '\0');
	CHECK(!exists(dir));
	a.fail_data = 0;
	a.fail_env = 1;
	CHECK(pam_sm_open_session(&a, 0, 0, NULL) == PAM_SESSION_ERR);
	a.fail_env = 0;
	CHECK(!exists(dir));
	pid = fork();
	CHECK(pid >= 0);
	if (pid == 0) {
		pam_handle_t child = {0};
		_exit(pam_sm_open_session(&child, 0, 0, NULL) == PAM_SUCCESS
			  ? 0
			  : 1);
	}
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
	      WEXITSTATUS(status) == 0);
	CHECK(exists(dir));
	snprintf(sub, sizeof(sub), "%s/stale", dir);
	fd = open(sub, O_CREAT | O_EXCL | O_WRONLY, 0600);
	CHECK(fd >= 0);
	close(fd);
	CHECK(pam_sm_open_session(&c, 0, 0, NULL) == PAM_SUCCESS);
	CHECK(!exists(sub));
	CHECK(pam_sm_close_session(&c, 0, 0, NULL) == PAM_SUCCESS);
	end(&c);
	CHECK(!exists(dir));
	CHECK(exists(target));
	{
		struct passwd *pw = getpwnam("nobody");
		struct stat st;
		CHECK(pw != NULL && pw->pw_uid != 0);
		test_user = "nobody";
		CHECK(pam_sm_open_session(&a, 0, 0, NULL) == PAM_SUCCESS);
		snprintf(dir, sizeof(dir), "%s/%lu", XDG_RUNTIME,
			 (unsigned long)pw->pw_uid);
		CHECK(stat(dir, &st) == 0 && st.st_uid == pw->pw_uid &&
		      st.st_gid == pw->pw_gid && (st.st_mode & 07777) == 0700);
		snprintf(sub, sizeof(sub), "%s/foreign", dir);
		CHECK(mkdir(sub, 0700) == 0);
		CHECK(pam_sm_close_session(&a, 0, 0, NULL) == PAM_SESSION_ERR);
		CHECK(exists(sub));
		CHECK(rmdir(sub) == 0);
		end(&a);
		CHECK(pam_sm_open_session(&a, 0, 0, NULL) == PAM_SUCCESS);
		CHECK(pam_sm_close_session(&a, 0, 0, NULL) == PAM_SUCCESS);
		end(&a);
		CHECK(!exists(dir));
	}
	{
		struct stat st;
		pam_handle_t mounted = {0};
		test_user = "root";
		snprintf(dir, sizeof(dir), "%s/0", XDG_RUNTIME);
		CHECK(mkdir(dir, 0700) == 0);
		CHECK(stat(dir, &st) == 0);
		injected_inode = st.st_ino;
		injected_device = st.st_dev;
		CHECK(pam_sm_open_session(&mounted, 0, 0, NULL) ==
		      PAM_SESSION_ERR);
		CHECK(exists(dir));
		injected_inode = 0;
		CHECK(rmdir(dir) == 0);
		CHECK(pam_sm_open_session(&mounted, 0, 0, NULL) == PAM_SUCCESS);
		CHECK(stat(dir, &st) == 0);
		injected_inode = st.st_ino;
		injected_device = st.st_dev;
		CHECK(pam_sm_close_session(&mounted, 0, 0, NULL) ==
		      PAM_SESSION_ERR);
		CHECK(exists(dir));
		injected_inode = 0;
		end(&mounted);
		CHECK(pam_sm_open_session(&mounted, 0, 0, NULL) == PAM_SUCCESS);
		CHECK(pam_sm_close_session(&mounted, 0, 0, NULL) ==
		      PAM_SUCCESS);
		end(&mounted);
		CHECK(pam_sm_open_session(&mounted, 0, 0, NULL) == PAM_SUCCESS);
		snprintf(sub, sizeof(sub), "%s/mount", dir);
		CHECK(mkdir(sub, 0700) == 0);
		{
			char marker[600];
			snprintf(marker, sizeof(marker), "%s/marker", sub);
			fd = open(marker, O_WRONLY | O_CREAT | O_EXCL, 0600);
			CHECK(fd >= 0);
			close(fd);
		}
		CHECK(stat(sub, &st) == 0);
		injected_inode = st.st_ino;
		injected_device = st.st_dev;
		CHECK(pam_sm_close_session(&mounted, 0, 0, NULL) ==
		      PAM_SESSION_ERR);
		CHECK(exists(sub));
		{
			char marker[600];
			snprintf(marker, sizeof(marker), "%s/marker", sub);
			CHECK(exists(marker));
			CHECK(unlink(marker) == 0);
		}
		injected_inode = 0;
		CHECK(rmdir(sub) == 0);
		end(&mounted);
		CHECK(pam_sm_open_session(&mounted, 0, 0, NULL) == PAM_SUCCESS);
		CHECK(pam_sm_close_session(&mounted, 0, 0, NULL) ==
		      PAM_SUCCESS);
		end(&mounted);
	}
	{
		int ready[2], release[2];
		char v;
		pam_handle_t parent = {0};
		test_user = "root";
		snprintf(dir, sizeof(dir), "%s/0", XDG_RUNTIME);
		CHECK(pipe(ready) == 0 && pipe(release) == 0);
		pid = fork();
		CHECK(pid >= 0);
		if (pid == 0) {
			pam_handle_t worker = {0};
			close(ready[0]);
			close(release[1]);
			if (pam_sm_open_session(&worker, 0, 0, NULL) !=
				PAM_SUCCESS ||
			    write(ready[1], "x", 1) != 1 ||
			    read(release[0], &v, 1) != 1 ||
			    pam_sm_close_session(&worker, 0, 0, NULL) !=
				PAM_SUCCESS)
				_exit(1);
			end(&worker);
			_exit(0);
		}
		close(ready[1]);
		close(release[0]);
		CHECK(read(ready[0], &v, 1) == 1);
		CHECK(pam_sm_open_session(&parent, 0, 0, NULL) == PAM_SUCCESS);
		CHECK(pam_sm_close_session(&parent, 0, 0, NULL) == PAM_SUCCESS);
		end(&parent);
		CHECK(exists(dir));
		CHECK(write(release[1], "x", 1) == 1);
		close(release[1]);
		close(ready[0]);
		CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
		      WEXITSTATUS(status) == 0);
		CHECK(!exists(dir));
	}
	CHECK(unlink(target) == 0 && rmdir(outside) == 0);
	puts("PASS sessions, crash recovery, mode, symlink, unwind, outside "
	     "sentinel");
	return 0;
}
