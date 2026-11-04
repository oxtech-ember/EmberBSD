/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Beckhoff Automation GmbH & Co. KG
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/* Adapted from FreeBSD pam_xdg.c at 06986e899972ac3a127ab2ab46196672d0e1e5b2.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define PAM_SM_SESSION
#include <security/pam_appl.h>
#include <security/pam_modules.h>

#ifndef XDG_CONTROL
#define XDG_CONTROL "/var/run/xdg-control"
#endif
#ifndef XDG_RUNTIME
#define XDG_RUNTIME "/var/run/xdg"
#endif
#define DATA_KEY "emberbsd.pam_xdg.session"

struct xdg_session {
	int fd;
	char token[96];
	uid_t uid;
	gid_t gid;
	pid_t pid;
};

/* root-owned directories are checked via opened descriptors. */
static int
secure_dir(const char *path, mode_t mode)
{
	struct stat st;
	int fd, created = 0;
	if (mkdir(path, mode) == 0)
		created = 1;
	else if (errno != EEXIST)
		return (-1);
	fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd == -1)
		return (-1);
	if (fstat(fd, &st) == -1 || !S_ISDIR(st.st_mode) || st.st_uid != 0 ||
	    (created && fchmod(fd, mode) == -1) ||
	    (!created && (st.st_mode & 07777) != mode)) {
		close(fd);
		errno = EPERM;
		return (-1);
	}
	return (fd);
}

static int
lock_control(int control, uid_t uid)
{
	struct stat st;
	int created = 0;
	char name[48];
	int fd;
	if (snprintf(name, sizeof(name), "lock-%lu", (unsigned long)uid) >= (int)sizeof(name))
		return (-1);
	fd =
	    openat(control, name,
		   O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd >= 0)
		created = 1;
	else if (errno == EEXIST)
		fd = openat(control, name, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
	if (fd == -1)
		return (-1);
	if (fstat(fd, &st) == -1 || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
	    st.st_nlink != 1 || (created && fchmod(fd, 0600) == -1) ||
	    (!created && (st.st_mode & 07777) != 0600) ||
	    flock(fd, LOCK_EX) == -1) {
		close(fd);
		errno = EPERM;
		return (-1);
	}
	return (fd);
}

/* Never follow symlinks; fdopendir takes ownership of its duplicated fd. */
static int
remove_contents(int fd, uid_t owner)
{
	DIR *dir;
	struct dirent *ent;
	struct stat before, after, parent;
	int copy, child, result = 0;

	if (fstat(fd, &parent) == -1)
		return (-1);
	copy = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	if (copy == -1)
		return (-1);
	dir = fdopendir(copy);
	if (dir == NULL) {
		close(copy);
		return (-1);
	}
	errno = 0;
	while ((ent = readdir(dir)) != NULL) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;
		if (fstatat(fd, ent->d_name, &before, AT_SYMLINK_NOFOLLOW) ==
		    -1) {
			result = -1;
			break;
		}
		if (S_ISDIR(before.st_mode)) {
			child = openat(fd, ent->d_name,
				       O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
					   O_CLOEXEC);
			if (child == -1) {
				result = -1;
				break;
			}
			if (fstat(child, &after) == -1) {
				close(child);
				result = -1;
				break;
			}
			if (after.st_dev != parent.st_dev) {
				close(child);
				errno = EXDEV;
				result = -1;
				break;
			}
			if (before.st_dev != after.st_dev ||
			    before.st_ino != after.st_ino ||
			    after.st_uid != owner ||
			    remove_contents(child, owner) == -1) {
				close(child);
				result = -1;
				break;
			}
			close(child);
			if (unlinkat(fd, ent->d_name, AT_REMOVEDIR) == -1) {
				result = -1;
				break;
			}
		} else if (unlinkat(fd, ent->d_name, 0) == -1) {
			result = -1;
			break;
		}
		errno = 0;
	}
	if (ent == NULL && errno != 0)
		result = -1;
	if (closedir(dir) == -1)
		result = -1;
	return (result);
}

/* Called only while control mutex is held. Stale tokens have no flock. */
static int
live_tokens(int control, uid_t uid)
{
	char prefix[48];
	struct stat st;
	DIR *dir;
	struct dirent *ent;
	int d, fd, live = 0, error = 0;
	if (snprintf(prefix, sizeof(prefix), "%lu-", (unsigned long)uid) >=
	    (int)sizeof(prefix))
		return (-1);
	d = fcntl(control, F_DUPFD_CLOEXEC, 0);
	if (d == -1)
		return (-1);
	dir = fdopendir(d);
	if (dir == NULL) {
		close(d);
		return (-1);
	}
	errno = 0;
	while ((ent = readdir(dir)) != NULL) {
		if (strncmp(ent->d_name, prefix, strlen(prefix)))
			continue;
		fd = openat(control, ent->d_name,
			    O_RDWR | O_NOFOLLOW | O_CLOEXEC);
		if (fd == -1 || fstat(fd, &st) == -1 || !S_ISREG(st.st_mode) ||
		    st.st_uid != 0 || st.st_nlink != 1 ||
		    (st.st_mode & 07777) != 0600) {
			if (fd != -1)
				close(fd);
			error = 1;
			break;
		}
		if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				live++;
			else
				error = 1;
		} else if (unlinkat(control, ent->d_name, 0) == -1)
			error = 1;
		close(fd);
		if (error)
			break;
		errno = 0;
	}
	if (ent == NULL && errno != 0)
		error = 1;
	if (closedir(dir) == -1)
		error = 1;
	return (error ? -1 : live);
}

static void
free_session(pam_handle_t *pamh, void *data, int status)
{
	struct xdg_session *s = data;
	(void)pamh;
	(void)status;
	if (s != NULL) {
		if (s->fd >= 0)
			close(s->fd);
		free(s);
	}
}

PAM_EXTERN int
pam_sm_open_session(pam_handle_t *pamh, int flags, int argc,
				   const char *argv[])
{
	const char *user;
	struct passwd *pw;
	struct stat st, base_st;
	struct xdg_session *s = NULL;
	char uid[32], env[256];
	uint32_t random[4];
	int control = -1, lock = -1, base = -1, dir = -1;
	int created = 0, rc = PAM_SESSION_ERR, n, tries, live;
	const void *existing;
	(void)flags;
	(void)argc;
	(void)argv;
	if (pam_get_data(pamh, DATA_KEY, &existing) == PAM_SUCCESS &&
	    existing != NULL &&
	    ((const struct xdg_session *)existing)->fd >= 0)
		return (PAM_SESSION_ERR);
	if (pam_get_user(pamh, &user, NULL) != PAM_SUCCESS || user == NULL ||
	    (pw = getpwnam(user)) == NULL)
		return (PAM_SESSION_ERR);
	if (snprintf(uid, sizeof(uid), "%lu", (unsigned long)pw->pw_uid) >=
	    (int)sizeof(uid))
		return (PAM_SESSION_ERR);
	control = secure_dir(XDG_CONTROL, 0700);
	if (control == -1)
		goto out;
	lock = lock_control(control, pw->pw_uid);
	if (lock == -1)
		goto out;
	base = secure_dir(XDG_RUNTIME, 0711);
	if (base == -1)
		goto out;
	if (mkdirat(base, uid, 0700) == 0)
		created = 1;
	else if (errno != EEXIST)
		goto out;
	dir =
	    openat(base, uid, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (dir == -1)
		goto out;
	if (created && (fchown(dir, pw->pw_uid, pw->pw_gid) == -1 ||
			fchmod(dir, 0700) == -1))
		goto out;
	if (fstat(base, &base_st) == -1 || fstat(dir, &st) == -1 ||
	    st.st_dev != base_st.st_dev || !S_ISDIR(st.st_mode) ||
	    st.st_uid != pw->pw_uid || st.st_gid != pw->pw_gid ||
	    (st.st_mode & 07777) != 0700)
		goto out;
	live = live_tokens(control, pw->pw_uid);
	if (live < 0)
		goto out;
	if (live == 0 && !created && remove_contents(dir, pw->pw_uid) == -1)
		goto out;
	s = calloc(1, sizeof(*s));
	if (s == NULL)
		goto out;
	s->fd = -1;
	s->uid = pw->pw_uid;
	s->gid = pw->pw_gid;
	s->pid = getpid();
	for (tries = 0; tries < 32; tries++) {
		arc4random_buf(random, sizeof(random));
		n = snprintf(s->token, sizeof(s->token), "%s-%08x%08x%08x%08x",
			     uid, random[0], random[1], random[2], random[3]);
		if (n < 0 || n >= (int)sizeof(s->token))
			goto out;
		s->fd = openat(
		    control, s->token,
		    O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (s->fd >= 0)
			break;
		if (errno != EEXIST)
			goto out;
	}
	if (s->fd < 0 || fchmod(s->fd, 0600) == -1 ||
	    flock(s->fd, LOCK_EX | LOCK_NB) == -1)
		goto out;
	if (snprintf(env, sizeof(env), "XDG_RUNTIME_DIR=%s/%s", XDG_RUNTIME,
		     uid) >= (int)sizeof(env))
		goto out;
	if (pam_putenv(pamh, env) != PAM_SUCCESS)
		goto out;
	if (pam_set_data(pamh, DATA_KEY, s, free_session) != PAM_SUCCESS) {
		(void)pam_putenv(pamh, "XDG_RUNTIME_DIR");
		goto out;
	}
	s = NULL;
	rc = PAM_SUCCESS;
out:
	if (s != NULL) {
		if (s->fd >= 0) {
			unlinkat(control, s->token, 0);
			close(s->fd);
		}
		free(s);
	}
	if (created && rc != PAM_SUCCESS && base >= 0)
		(void)unlinkat(base, uid, AT_REMOVEDIR);
	if (dir >= 0)
		close(dir);
	if (base >= 0)
		close(base);
	if (lock >= 0)
		close(lock);
	if (control >= 0)
		close(control);
	return (rc);
}

PAM_EXTERN int
pam_sm_close_session(pam_handle_t *pamh, int flags, int argc,
				    const char *argv[])
{
	const void *data;
	struct xdg_session *s;
	struct stat st, now, base_st;
	char uid[32];
	int control = -1, lock = -1, base = -1, dir = -1, live,
	    rc = PAM_SESSION_ERR;
	(void)flags;
	(void)argc;
	(void)argv;
	if (pam_get_data(pamh, DATA_KEY, &data) != PAM_SUCCESS || data == NULL)
		return (PAM_SESSION_ERR);
	s = (struct xdg_session *)(uintptr_t)data;
	if (s->fd < 0 || s->pid != getpid())
		return (PAM_SESSION_ERR);
	if (snprintf(uid, sizeof(uid), "%lu", (unsigned long)s->uid) >=
	    (int)sizeof(uid))
		return (PAM_SESSION_ERR);
	control = secure_dir(XDG_CONTROL, 0700);
	if (control == -1)
		goto out;
	lock = lock_control(control, s->uid);
	if (lock == -1)
		goto out;
	if (unlinkat(control, s->token, 0) == -1)
		goto out;
	close(s->fd);
	s->fd = -1;
	live = live_tokens(control, s->uid);
	if (live < 0)
		goto out;
	if (live == 0) {
		base = secure_dir(XDG_RUNTIME, 0711);
		if (base == -1)
			goto out;
		dir = openat(base, uid,
			     O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (dir == -1)
			goto out;
		if (fstat(base, &base_st) == -1 || fstat(dir, &st) == -1 ||
		    st.st_dev != base_st.st_dev || st.st_uid != s->uid ||
		    st.st_gid != s->gid || (st.st_mode & 07777) != 0700)
			goto out;
		if (remove_contents(dir, s->uid) == -1 ||
		    fstatat(base, uid, &now, AT_SYMLINK_NOFOLLOW) == -1 ||
		    st.st_dev != now.st_dev || st.st_ino != now.st_ino ||
		    unlinkat(base, uid, AT_REMOVEDIR) == -1)
			goto out;
	}
	(void)pam_putenv(pamh, "XDG_RUNTIME_DIR");
	rc = PAM_SUCCESS;
out:
	if (dir >= 0)
		close(dir);
	if (base >= 0)
		close(base);
	if (lock >= 0)
		close(lock);
	if (control >= 0)
		close(control);
	return (rc);
}
PAM_MODULE_ENTRY("pam_xdg");
