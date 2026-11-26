/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/wdog.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <sha2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "slot.h"

static const char *
name(int i)
{
	return i < 0 ? "null" : (i == 0 ? "\"A\"" : "\"B\"");
}

static void
confirm(struct slot *s, int dest)
{
	struct wdog_mode saved, mode;
	char sha[SHA256_DIGEST_STRING_LENGTH];
	int fd, status;
	pid_t pid;

	if (!s->trial || s->next >= 0 || s->running != dest || s->prepared != dest ||
	    SHA256_File("/var/db/ember-update/installed.json", sha) == NULL ||
	    strcmp(sha, s->digest))
		errx(1, "expected the prepared and consumed trial");
	if (s->good == dest) {
		/* Retry a failed secondary marker write after the commit. */
		slot_marker(s, "osback.dat", dest);
		return;
	}
	fd = open("/dev/watchdog", O_RDWR | O_CLOEXEC);
	if (fd < 0 || ioctl(fd, WDOGIOC_WHICH, &saved) < 0 ||
	    strcmp(saved.wm_name, field(s->profile, "watchdog")) ||
	    (saved.wm_mode & WDOG_MODE_MASK) != WDOG_MODE_ETICKLE)
		errx(1, "trial watchdog is unavailable");
	mode = saved;
	mode.wm_mode = WDOG_MODE_DISARMED;
	/* The parent can restore the original timer if committing the first marker fails. */
	pid = fork();
	if (pid < 0)
		err(1, "fork");
	if (pid == 0) {
		if (ioctl(fd, WDOGIOC_SMODE, &mode) < 0)
			err(1, "disarm trial watchdog");
		slot_marker(s, "osgood.dat", dest);
		_exit(0);
	}
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			err(1, "waitpid");
	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
		(void)ioctl(fd, WDOGIOC_SMODE, &saved);
		sync();
		reboot(RB_AUTOBOOT, NULL);
		err(1, "restart after confirmation failure");
	}
	close(fd);
	slot_marker(s, "osback.dat", dest);
}

int
main(int argc, char **argv)
{
	struct slot s;
	int dest, confirmed;

	if (argc < 2 || argc > 3)
		errx(1, "usage: ember-os-slot status | arm|cancel|confirm A|B");
	if (argc == 3) {
		if (geteuid() != 0)
			errx(1, "slot changes require root");
		slot_lock();
	}
	slot_layout(&s);
	confirmed = s.good >= 0 ? s.good : s.back;
	if (argc == 2 && !strcmp(argv[1], "status")) {
		printf("{\"running\":%s,\"confirmed\":%s,\"next\":%s,\"trial\":%s,\"prepared\":",
		    name(s.running), name(confirmed), name(s.next), s.trial ? "true" : "false");
		if (s.prepared < 0)
			puts("null}");
		else
			printf("{\"slot\":%s,\"sha256\":\"%s\"}}\n", name(s.prepared), s.digest);
		return 0;
	}
	if (getuid() != 0 || argc != 3)
		errx(1, "slot changes require root and a destination");
	dest = slot_id(argv[2]);
	if (!strcmp(argv[1], "arm")) {
		slot_inactive(&s, dest);
		if (s.prepared != dest || s.good != confirmed || s.back != confirmed)
			errx(1, "incomplete slot preparation");
		slot_marker(&s, "osnext.dat", dest);
	} else if (!strcmp(argv[1], "cancel")) {
		if (s.running != confirmed || s.running == dest || s.next != dest)
			errx(1, "expected a pending trial on the confirmed root");
		slot_marker(&s, "osnext.dat", -1);
	} else if (!strcmp(argv[1], "confirm"))
		confirm(&s, dest);
	else
		errx(1, "unknown boot operation");
	return 0;
}
