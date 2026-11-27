/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/stat.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "control.h"
#include "profile.h"
#include "verify.h"

struct eu_json *ui;

const char *
text_field(const struct eu_json *o, const char *key)
{
	return eu_string(eu_get(o, key));
}

int64_t
int_field(const struct eu_json *o, const char *key)
{
	const struct eu_json *v = eu_get(o, key);
	return v && v->type == EU_INTEGER ? v->integer : -1;
}

/* Root owns the file and each directory that can replace it. */
void
trusted(const char *path, bool directory)
{
	char buf[PATH_MAX];
	struct stat st;
	size_t len = strlen(path);
	if (len >= sizeof(buf) || path[0] != '/')
		errx(1, "invalid local path");
	memcpy(buf, path, len + 1);
	for (char *c = buf + 1;; c++) {
		if (*c && *c != '/')
			continue;
		char end = *c;
		*c = 0;
		if (lstat(buf, &st) < 0)
			err(1, "%s", buf);
		if (st.st_uid != 0 || (st.st_mode & 022) ||
		    ((end || directory) ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)))
			errx(1, "unexpected owner, mode, or file type: %s", buf);
		if (!end)
			break;
		*c = end;
	}
}

struct eu_json *
read_record(const char *path, bool optional)
{
	struct stat st;
	if (lstat(path, &st) < 0 && errno == ENOENT && optional)
		return NULL;
	trusted(path, false);
	struct eu_json *p = eu_json_file(path, 1024 * 1024);
	if (!p || p->type != EU_OBJECT)
		errx(1, "invalid JSON object: %s", path);
	return p;
}

struct eu_json *
load_ui(void)
{
	struct eu_json *j = read_record(STATE, true), *a;
	if (j) {
		if (!eu_equal(text_field(j, "schema"), "emberbsd.update-state.v1") ||
		    !(a = eu_member(j, "history")) || a->type != EU_ARRAY)
			errx(1, "invalid %s", STATE);
		return j;
	}
	j = eu_new(EU_OBJECT);
	eu_put(j, "schema", eu_text("emberbsd.update-state.v1"));
	eu_put(j, "status", eu_text("idle"));
	eu_put(j, "channel", eu_text("current"));
	const char *nullable[] = {"checked_at",	    "release_id", "running_slot",
				  "confirmed_slot", "attempt",	  "os_hold",
				  "last_error"};
	for (size_t i = 0; i < sizeof(nullable) / sizeof(nullable[0]); i++)
		eu_put(j, nullable[i], eu_new(EU_NULL));
	eu_put(j, "history", eu_new(EU_ARRAY));
	eu_put(j, "reboot_required", eu_bool(false));
	eu_put(j, "available", a = eu_new(EU_OBJECT));
	eu_put(a, "os_generation", eu_new(EU_NULL));
	eu_put(a, "packages", eu_new(EU_ARRAY));
	return j;
}

void
history(const char *event, const char *detail)
{
	struct eu_json *a = eu_member(ui, "history"), *item = eu_new(EU_OBJECT);
	eu_put(item, "time", eu_int(time(NULL)));
	eu_put(item, "event", eu_text(event));
	eu_put(item, "detail", eu_text(detail));
	eu_append(a, item);
	size_t n = 0;
	for (item = a->child; item; item = item->next)
		n++;
	while (n-- > 32) {
		item = a->child;
		a->child = item->next;
		item->next = NULL;
		eu_json_free(item);
	}
}

void
report(const char *status, const char *error)
{
	eu_put(ui, "status", eu_text(status));
	eu_put(ui, "last_error", eu_text(error));
	if (eu_save(STATE, ui, 0644) < 0)
		err(1, "write %s", STATE);
}

void
fail(const char *detail)
{
	if (stopping)
		exit(0);
	if (ui) {
		history("error", detail);
		report("error", detail);
	}
	errx(1, "%s", detail);
}

void
checkpoint(void)
{
	if (stopping)
		exit(0);
}

static bool
yes(const char *s)
{
	if (s && (!strcasecmp(s, "yes") || !strcasecmp(s, "on") || !strcmp(s, "1") ||
		  !strcasecmp(s, "true")))
		return true;
	if (s && (!strcasecmp(s, "no") || !strcasecmp(s, "off") || !strcmp(s, "0") ||
		  !strcasecmp(s, "false")))
		return false;
	fail("invalid yes/no update policy in rc.conf");
}

static int
minute(const char *s)
{
	if (s[0] < '0' || s[0] > '2' || s[1] < '0' || s[1] > '9' || s[2] != ':' ||
	    s[3] < '0' || s[3] > '5' || s[4] < '0' || s[4] > '9')
		return -1;
	int h = (s[0] - '0') * 10 + s[1] - '0';
	return h <= 23 ? h * 60 + (s[3] - '0') * 10 + s[4] - '0' : -1;
}

bool
window_open(const char *s, time_t now)
{
	if (eu_equal(s, "ask"))
		return false;
	if (eu_equal(s, "always"))
		return true;
	if (!s || strlen(s) != 11 || s[5] != '-')
		fail("invalid reboot window");
	int a = minute(s), b = minute(s + 6);
	if (a < 0 || b < 0 || a == b)
		fail("invalid reboot window");
	struct tm local;
	if (!localtime_r(&now, &local))
		fail("cannot read local reboot time");
	int m = local.tm_hour * 60 + local.tm_min;
	return a < b ? (a <= m && m < b) : (a <= m || m < b);
}

bool
reboot_policy(const char *operation, bool immediate, const char *window, time_t now)
{
	if (eu_equal(operation, "install") || eu_equal(operation, "check") ||
	    eu_equal(operation, "boot"))
		return false;
	return immediate || window_open(window, now);
}

void
read_policy(struct policy *p)
{
	char *output, *pos, *channel, *window, *pkg, *os;
	const char *args[] = {
	    "/bin/sh", "-ec",
	    ". /etc/rc.subr; load_rc_config ember_update; printf '%s\\n' "
	    "\"$ember_update_channel\" \"$ember_update_reboot\" "
	    "\"$ember_update_packages\" \"$ember_update_os\"",
	    NULL};
	if (eu_run(args, &output, 512) < 0)
		fail("cannot read ember_update policy from rc.conf");
	pos = output;
	channel = strsep(&pos, "\n");
	window = strsep(&pos, "\n");
	pkg = strsep(&pos, "\n");
	os = strsep(&pos, "\n");
	if (!pos || *pos || !eu_slug(channel) || strlen(channel) > 127 || !window)
		fail("invalid ember_update policy in rc.conf");
	p->channel = strdup(channel);
	p->window = strdup(window);
	p->packages = yes(pkg);
	p->os = yes(os);
	if (!p->channel || !p->window)
		err(1, "policy");
	(void)window_open(p->window, time(NULL));
	if (ui)
		eu_put(ui, "channel", eu_text(channel));
	free(output);
}

const char *
kernel_mode(const struct eu_json *profile)
{
	const struct eu_json *target = eu_get(profile, "target");
	if (!eu_slug(text_field(profile, "profile")) ||
	    !eu_target_match(target, target))
		return NULL;
	switch (eu_profile_adapter(profile)) {
	case EU_ADAPTER_SINGLE:
		return "single";
	case EU_ADAPTER_UBOOT:
	case EU_ADAPTER_EFI: {
		const struct eu_json *roots = eu_get(profile, "roots");
		const char *a = text_field(roots, "A"), *b = text_field(roots, "B"),
			   *esp = text_field(profile, "esp");
		if (!eu_fields(roots, "A B") || !a || !*a || !b || !*b || !esp ||
		    !*esp || eu_equal(a, b) || eu_equal(a, esp) || eu_equal(b, esp))
			return NULL;
		return "slots";
	}
	default:
		return NULL;
	}
}

void
load_hardware(struct hardware *h)
{
	memset(h, 0, sizeof(*h));
	h->profile = read_record(PROFILE, false);
	h->name = text_field(h->profile, "profile");
	h->target = eu_get(h->profile, "target");
	const char *mode = kernel_mode(h->profile);
	if (!mode)
		fail("invalid device profile, storage, or boot adapter");
	if (eu_equal(mode, "single")) {
		h->single = true;
		eu_put(ui, "running_slot", eu_new(EU_NULL));
		eu_put(ui, "confirmed_slot", eu_new(EU_NULL));
		eu_put(ui, "attempt", eu_new(EU_NULL));
		eu_put(ui, "os_hold", eu_new(EU_NULL));
		eu_put(ui, "reboot_required", eu_bool(false));
		return;
	}

	h->receipt = read_record(RECEIPT, false);
	h->commit = text_field(h->receipt, "os_commit");
	h->generation = int_field(h->receipt, "generation");
	if (!eu_slug(h->name) || !eu_target_match(h->target, h->target) ||
	    !eu_equal(h->name, text_field(h->receipt, "profile")) ||
	    !eu_hash(h->commit, 40) || h->generation < 0)
		fail("OS receipt does not match the installed device profile");
	struct eu_json *local = read_record(LOCAL, true);
	if (local) {
		char digest[65];
		if (!eu_fields(local, "schema sha256") ||
		    !eu_equal(text_field(local, "schema"), CUSTOM) ||
		    file_sha("/netbsd", digest) < 0 ||
		    !eu_equal(digest, text_field(local, "sha256")))
			fail("local kernel does not match its installed receipt");
		h->custom = true;
		eu_json_free(local);
	} else {
		char *build = NULL, *want = NULL;
		/* newvers adds a newline. Read the kernel string without display formatting. */
		const char *args[] = {"/sbin/sysctl", "-r", "kern.buildinfo", NULL};
		if (eu_run(args, &build, 512) < 0 ||
		    asprintf(&want, "EmberBSD:%s\n", h->commit) < 0)
			fail("cannot read the running kernel build identity");
		h->custom = strcmp(build, want) != 0;
		char *version = NULL;
		const char *sysctl[] = {"/sbin/sysctl", "-n", "kern.version", NULL};
		const char *tag = text_field(h->profile, "kernel");
		if (!tag || tag[0] != '(' || strlen(tag) < 3 ||
		    tag[strlen(tag) - 1] != ')' || eu_run(sysctl, &version, 4096) < 0)
			fail("cannot compare the running kernel configuration");
		version[strcspn(version, "\n")] = 0;
		if (!strstr(version, tag))
			h->custom = true;
		free(version);
		free(build);
		free(want);
	}
	eu_put(ui, "os_hold", eu_text(h->custom ? "custom-kernel" : NULL));
}

static int64_t
monotonic_ms(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		return -1;
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Keep the group leader waitable until all of the hook's children are stopped. */
static void
reap_probe(pid_t child)
{
	(void)kill(-child, SIGKILL);
	while (waitpid(child, NULL, 0) < 0)
		if (errno != EINTR)
			fail("cannot reap trial health check");
}

void
trial_health(void)
{
	const struct timespec poll = {0, 100000000};
	int64_t start = monotonic_ms(), next = start;
	pid_t child = 0;

	if (start < 0)
		fail("cannot time trial health check");
	for (;;) {
		siginfo_t result = {0};
		if (child && waitid(P_PID, child, &result, WEXITED | WNOHANG | WNOWAIT) < 0) {
			if (errno == EINTR)
				continue;
			fail("cannot wait for trial health check");
		}
		if (result.si_pid) {
			reap_probe(child);
			child = 0;
		}
		int64_t now = monotonic_ms();
		if (stopping || now < 0 || now - start >= 30000)
			break;
		if (result.si_pid) {
			if (result.si_code != CLD_EXITED || result.si_status > 1)
				fail("trial health check failed");
			if (result.si_status == 0) {
				checkpoint();
				return;
			}
			next = now + 1000;
		}
		if (!child && now >= next) {
			trusted(HEALTH, false);
			checkpoint();
			child = fork();
			if (child < 0)
				fail("cannot start trial health check");
			if (child == 0) {
				if (setpgid(0, 0) == 0)
					execl(HEALTH, HEALTH, (char *)NULL);
				_exit(127);
			}
			(void)setpgid(child, child);
		}
		(void)nanosleep(&poll, NULL);
	}
	if (child)
		reap_probe(child);
	checkpoint();
	fail("trial health check timed out");
}
