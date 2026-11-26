/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "control.h"
#include "store.h"

static void
usage(FILE *f)
{
	fprintf(f, "usage: ember-update status | check | apply [--reboot] | history | "
		   "catalog | kernel-mode\n"
		   "       ember-update install PKGPATH [--pkgname FULLNAME]\n"
		   "       ember-update apply-kernel DIRECTORY [--reboot]\n");
}

static volatile sig_atomic_t worker;

static void
stop(int signo)
{
	(void)signo;
	int saved = errno;
	stopping = 1;
	if (worker > 0)
		kill((pid_t)worker, SIGTERM);
	errno = saved;
}

static void
signals(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = stop;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0)
		err(1, "sigaction");
}

static int
take_lock(const char *name)
{
	int fd = open(name, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
	struct stat st;
	if (fd < 0 || fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
	    (st.st_mode & 077))
		errx(1, "unsafe update lock: %s", name);
	if (flock(fd, LOCK_EX | LOCK_NB) < 0)
		err(1, "update operation is active");
	return fd;
}

static int
cycle(const char *op, const char *directory, bool immediate,
      const struct request *request)
{
	struct policy policy;
	struct hardware hw;
	struct slots s;
	if (mkdir(DATABASE, 0755) < 0 && errno != EEXIST)
		err(1, "%s", DATABASE);
	trusted(DATABASE, true);
	int lock = take_lock("/var/run/ember-update.lock");
	ui = load_ui();
	read_policy(&policy);
	load_hardware(&hw);
	if (!hw.single)
		slot_status(&s);
	else
		memset(&s, 0, sizeof(s));
	bool mutate = strcmp(op, "check") != 0, boot = eu_equal(op, "boot");
	bool reboot = reboot_policy(op, immediate, policy.window, time(NULL));
	if (!mutate && (!eu_equal(s.running, s.confirmed) || s.next[0])) {
		/* A check can publish slot state without changing a boot marker. */
		eu_put(ui, "reboot_required", eu_bool(s.next[0] != 0));
		report(!eu_equal(s.running, s.confirmed) ? "trial" : "ready", NULL);
	} else if (hw.single) {
		if (directory)
			fail("local kernel installation requires boot slots");
		if (!boot)
			release_cycle(&s, &hw, &policy, mutate, false, request);
	} else if (!mutate || !reconcile(&s, &hw, reboot)) {
		if (directory)
			prepare(&s, directory, NULL, true, 0, reboot, NULL);
		else if (!boot)
			release_cycle(&s, &hw, &policy, mutate, reboot, request);
	} else if (request) {
		struct eu_json *a = read_record(ATTEMPT, true);
		if (!eu_equal(text_field(ui, "status"), "ready") ||
		    !eu_equal(text_field(a, "requested_root"), request->root) ||
		    (request->pin &&
		     !eu_equal(request->pin, text_field(a, "requested_pkgname"))))
			errx(1, "finish the active update before installing another "
				"application");
		eu_json_free(a);
	}
	int result = eu_equal(text_field(ui, "status"), "ready") ? 4 : 0;
	free(policy.channel);
	free(policy.window);
	eu_json_free(hw.profile);
	eu_json_free(hw.receipt);
	eu_json_free(ui);
	ui = NULL;
	close(lock);
	return result;
}

static int
watch(void)
{
	int fd = take_lock("/var/run/ember_update.pid");
	if (daemon(0, 0) < 0 || ftruncate(fd, 0) < 0 ||
	    dprintf(fd, "%ld\n", (long)getpid()) < 0)
		err(1, "start update service");
	openlog("ember-update", LOG_PID, LOG_DAEMON);
	unsigned backoff = 30;
	while (!stopping) {
		pid_t child = fork();
		if (child == 0) {
			close(fd);
			_exit(cycle("auto", NULL, false, NULL));
		}
		int status = 0;
		if (child > 0) {
			worker = child;
			pid_t r;
			do {
				if (stopping)
					(void)kill(child, SIGTERM);
				r = waitpid(child, &status, 0);
			} while (r < 0 && errno == EINTR);
			worker = 0;
			if (r < 0)
				status = -1;
		}
		if (stopping)
			break;
		unsigned delay;
		if (child < 0 || status == -1 || !WIFEXITED(status) ||
		    (WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 4)) {
			delay = backoff;
			backoff = backoff < 900 ? backoff * 2 : 1800;
			syslog(LOG_WARNING, "Update check failed. Retry in %u seconds.",
			       delay);
		} else {
			delay = WEXITSTATUS(status) == 4 ? 60 : 3600;
			backoff = 30;
		}
		while (delay && !stopping)
			delay = sleep(delay);
	}
	unlink("/var/run/ember_update.pid");
	close(fd);
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc == 2 && eu_equal(argv[1], "--help")) {
		usage(stdout);
		return 0;
	}
	if (argc < 2) {
		usage(stderr);
		return 2;
	}
	const char *op = argv[1];
	if (argc == 2 && (eu_equal(op, "status") || eu_equal(op, "history"))) {
		struct eu_json *j = load_ui();
		const struct eu_json *p =
		    eu_equal(op, "history") ? eu_get(j, "history") : j;
		if (eu_emit(stdout, p) < 0 || putchar('\n') == EOF ||
		    fflush(stdout) < 0)
			err(1, "output");
		eu_json_free(j);
		return 0;
	}
	if (argc == 2 && eu_equal(op, "kernel-mode")) {
		struct eu_json *profile = read_record(PROFILE, false);
		const char *mode = kernel_mode(profile);
		if (!mode)
			errx(1, "invalid kernel installation profile");
		if (puts(mode) == EOF || fflush(stdout) < 0)
			err(1, "output");
		eu_json_free(profile);
		return 0;
	}
	if (argc == 2 && eu_equal(op, "catalog"))
		return store_catalog();
	if (geteuid() != 0)
		errx(1, "this update operation requires root");
	umask(022);
	signals();
	if (eu_equal(op, "install")) {
		struct request request = {.root = argc >= 3 ? argv[2] : NULL,
					  .pin = argc == 5 ? argv[4] : NULL};
		if (!(argc == 3 || (argc == 5 && eu_equal(argv[3], "--pkgname") &&
				    eu_pkgname(request.pin))) ||
		    !eu_pkgroot(request.root)) {
			usage(stderr);
			return 2;
		}
		int result = cycle(op, NULL, false, &request);
		if (puts(result == 4 ? "ready" : "idle") == EOF || fflush(stdout) < 0)
			err(1, "output");
		return 0;
	}
	if (argc == 2 && eu_equal(op, "watch"))
		return watch();
	bool local = eu_equal(op, "apply-kernel"), apply = eu_equal(op, "apply"),
	     now = false;
	if ((local && argc == 4) || (apply && argc == 3)) {
		now = eu_equal(argv[argc - 1], "--reboot");
		if (!now) {
			usage(stderr);
			return 2;
		}
		argc--;
	}
	if (!((local && argc == 3) || (argc == 2 && (apply || eu_equal(op, "check") ||
						     eu_equal(op, "boot"))))) {
		usage(stderr);
		return 2;
	}
	int ret = cycle(op, local ? argv[2] : NULL, now, NULL);
	if (!eu_equal(op, "boot")) {
		struct eu_json *j = load_ui();
		printf("%s\n", text_field(j, "status"));
		eu_json_free(j);
	}
	return ret == 4 ? 0 : ret;
}
