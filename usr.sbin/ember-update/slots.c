/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/stat.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "control.h"

static bool
slot(const char *s)
{
	return eu_equal(s, "A") || eu_equal(s, "B");
}

void
slot_status(struct slots *s)
{
	char *raw = NULL;
	const char *args[] = {BOOT, "status", NULL};
	trusted(BOOT, false);
	if (eu_run(args, &raw, 2048) < 0)
		fail("cannot read boot slot status");
	struct eu_json *d = eu_json_parse(raw, strlen(raw));
	free(raw);
	const struct eu_json *p = eu_get(d, "prepared"), *trial = eu_get(d, "trial"),
			     *next = eu_get(d, "next");
	const char *running = text_field(d, "running"),
		   *confirmed = text_field(d, "confirmed");
	const char *n = eu_string(next);
	if (!eu_fields(d, "running confirmed next trial prepared") || !slot(running) ||
	    !slot(confirmed) || trial->type != EU_BOOL ||
	    (next->type != EU_NULL && (!slot(n) || eu_equal(n, confirmed))) ||
	    (!eu_equal(running, confirmed) && (!trial->integer || n)))
		fail("invalid boot slot status");
	memset(s, 0, sizeof(*s));
	s->running[0] = *running;
	s->confirmed[0] = *confirmed;
	if (n)
		s->next[0] = *n;
	s->trial = trial->integer;
	if (p->type != EU_NULL) {
		if (!eu_fields(p, "slot sha256") || !slot(text_field(p, "slot")) ||
		    !eu_hash(text_field(p, "sha256"), 64))
			fail("invalid prepared slot identity");
		s->prepared[0] = *text_field(p, "slot");
		memcpy(s->digest, text_field(p, "sha256"), 65);
	}
	eu_json_free(d);
	eu_put(ui, "running_slot", eu_text(s->running));
	eu_put(ui, "confirmed_slot", eu_text(s->confirmed));
}

static void
boot_command(const char *op, const char *name)
{
	checkpoint();
	const char *args[] = {BOOT, op, name, NULL};
	if (eu_run(args, NULL, 0) < 0)
		fail("boot loader could not perform slot operation");
}

static char *
boot_id(void)
{
	const char *path = "/var/run/ember-update.boot";
	struct stat st;
	if (lstat(path, &st) < 0 && errno == ENOENT) {
		struct eu_json *v = eu_new(EU_OBJECT);
		unsigned char bytes[16];
		char id[33];
		arc4random_buf(bytes, sizeof(bytes));
		for (size_t i = 0; i < sizeof(bytes); i++)
			snprintf(id + 2 * i, 3, "%02x", bytes[i]);
		eu_put(v, "id", eu_text(id));
		if (eu_save(path, v, 0600) < 0)
			fail("cannot record boot session");
		eu_json_free(v);
	}
	struct eu_json *v = read_record(path, false);
	if (!eu_hash(text_field(v, "id"), 32))
		fail("invalid boot session");
	char *id = strdup(text_field(v, "id"));
	if (!id)
		err(1, "boot session");
	eu_json_free(v);
	return id;
}

static struct eu_json *
identity(const char *name, const char *hash)
{
	struct eu_json *o = eu_new(EU_OBJECT);
	eu_put(o, "slot", eu_text(name));
	eu_put(o, "sha256", eu_text(hash));
	return o;
}

static void
ready(bool reboot)
{
	eu_put(ui, "reboot_required", eu_bool(true));
	report("ready", NULL);
	if (reboot) {
		const char *args[] = {"/sbin/shutdown", "-r", "now", NULL};
		checkpoint();
		if (eu_run(args, NULL, 0) < 0)
			fail("system restart request failed");
	}
}

/* Every trial must identify the exact plan installed on its running root. */
static void
confirm(const struct slots *s, const struct hardware *h)
{
	char digest[65];
	struct eu_json *i = read_record(INSTALLED, false);
	if (!eu_equal(s->prepared, s->running) || file_sha(INSTALLED, digest) < 0 ||
	    !eu_equal(digest, s->digest))
		fail("running trial differs from prepared slot receipt");
	if (eu_equal(text_field(i, "schema"), PLAN)) {
		const struct eu_json *os = eu_get(i, "os_update_sha256");
		struct eu_json *local = read_record(LOCAL, true);
		if (!eu_equal(h->name, text_field(i, "profile")) ||
		    !eu_target_match(h->target, eu_get(i, "target")) ||
		    h->generation != int_field(i, "os_generation") ||
		    !eu_equal(h->commit, text_field(i, "os_commit")) ||
		    (h->custom && !local) || !os ||
		    (os->type != EU_NULL &&
		     (h->custom || h->generation < 1 || file_sha(RECEIPT, digest) < 0 ||
		      !eu_equal(digest, eu_string(os)))))
			fail("running OS does not match its signed installation plan");
		eu_json_free(local);
	} else {
		char localhash[65];
		if (!h->custom || !eu_equal(text_field(i, "schema"), CUSTOM) ||
		    file_sha(LOCAL, localhash) < 0 || !eu_equal(localhash, s->digest))
			fail("local kernel trial differs from its receipt");
	}
	eu_json_free(i);
	report("trial", NULL);
	trial_health();
	/* T stays in /chosen or EFI until restart. Record only an unfinished commit. */
	struct eu_json *pending = identity(s->running, s->digest);
	if (eu_save("/var/run/ember-update-confirm.json", pending, 0600) < 0)
		fail("cannot record trial confirmation");
	eu_json_free(pending);
	boot_command("confirm", s->running);
	struct slots after;
	slot_status(&after);
	if (after.next[0] || !eu_equal(after.confirmed, s->running) ||
	    !eu_equal(after.running, s->running))
		fail("boot loader did not confirm the running trial");
	eu_put(ui, "attempt", eu_new(EU_NULL));
	eu_put(ui, "reboot_required", eu_bool(false));
	if (unlink("/var/run/ember-update-confirm.json") < 0 && errno != ENOENT)
		fail("cannot clear trial confirmation");
	struct eu_json *available = eu_new(EU_OBJECT);
	eu_put(available, "os_generation", eu_new(EU_NULL));
	eu_put(available, "packages", eu_new(EU_ARRAY));
	eu_put(ui, "available", available);
	history("confirmed", s->running);
	report("idle", NULL);
}

bool
reconcile(const struct slots *s, const struct hardware *h, bool reboot)
{
	struct eu_json *unfinished =
	    read_record("/var/run/ember-update-confirm.json", true);
	bool retry = unfinished && s->trial && !s->next[0] &&
		     eu_equal(s->prepared, s->running) &&
		     eu_equal(text_field(unfinished, "slot"), s->running) &&
		     eu_equal(text_field(unfinished, "sha256"), s->digest);
	eu_json_free(unfinished);
	if (!eu_equal(s->running, s->confirmed) || retry) {
		confirm(s, h);
		return true;
	}
	const char *other = *s->running == 'A' ? "B" : "A";
	struct eu_json *a = read_record(ATTEMPT, true);
	if (!a) {
		if (s->next[0])
			fail("boot loader has an unrecognized pending trial");
		eu_put(ui, "attempt", eu_new(EU_NULL));
		eu_put(ui, "reboot_required", eu_bool(false));
		return false;
	}
	const char *hash = text_field(a, "sha256"), *phase = text_field(a, "phase"),
		   *boot = text_field(a, "boot");
	if (!eu_equal(other, text_field(a, "slot")) ||
	    !eu_equal(h->name, text_field(a, "profile")) || !eu_hash(hash, 64) ||
	    !eu_hash(boot, 32) || int_field(a, "os_generation") < 0 ||
	    !(eu_equal(phase, "prepared") || eu_equal(phase, "armed") ||
	      eu_equal(phase, "failed")))
		fail("invalid saved update attempt");
	char *id = boot_id();
	bool same = eu_equal(boot, id);
	free(id);
	if (s->next[0] || (same && eu_equal(phase, "prepared"))) {
		if (!eu_equal(other, s->prepared) || !eu_equal(s->digest, hash) ||
		    eu_equal(phase, "failed"))
			fail("pending slot has a different installation plan");
		if (!same) {
			boot_command("cancel", other);
			struct slots after;
			slot_status(&after);
			if (after.next[0] || !eu_equal(after.confirmed, s->running))
				fail("boot loader did not cancel the stale trial");
		} else {
			if (!s->next[0]) {
				boot_command("arm", other);
				struct slots after;
				slot_status(&after);
				if (!eu_equal(after.next, other) ||
				    !eu_equal(after.digest, hash))
					fail("boot loader did not arm the saved "
					     "attempt");
			}
			eu_put(a, "phase", eu_text("armed"));
			if (eu_save(ATTEMPT, a, 0600) < 0)
				fail("cannot store armed attempt");
			eu_put(ui, "attempt", identity(other, hash));
			ready(reboot);
			eu_json_free(a);
			return true;
		}
	}
	if (!eu_equal(phase, "failed")) {
		int64_t failed = int_field(a, "os_generation");
		if (failed > 0) {
			struct eu_json *floor =
			    read_record(DATABASE "/os-bad.json", true);
			if (!floor)
				floor = eu_new(EU_OBJECT);
			if (int_field(floor, "generation") < failed) {
				eu_put(floor, "profile", eu_text(h->name));
				eu_put(floor, "generation", eu_int(failed));
				if (eu_save(DATABASE "/os-bad.json", floor, 0600) < 0)
					fail("cannot store rejected OS generation");
			}
			eu_json_free(floor);
		}
		eu_put(a, "phase", eu_text("failed"));
		if (eu_save(ATTEMPT, a, 0600) < 0)
			fail("cannot store rejected attempt");
		history("rolled-back", other);
	}
	eu_put(ui, "attempt", eu_new(EU_NULL));
	eu_put(ui, "reboot_required", eu_bool(false));
	report("idle", NULL);
	eu_json_free(a);
	return false;
}

void
prepare(const struct slots *s, const char *dir, const char *expected, bool local,
	int64_t generation, bool reboot)
{
	const char *other = *s->running == 'A' ? "B" : "A";
	struct eu_json *profile = read_record(PROFILE, false);
	struct eu_json *old = read_record(ATTEMPT, true);
	if (expected && old && eu_equal(text_field(old, "sha256"), expected)) {
		eu_json_free(old);
		eu_json_free(profile);
		report("idle", NULL);
		return;
	}
	eu_json_free(old);
	trusted(INSTALL, false);
	checkpoint();
	report("preparing", NULL);
	const char *args[] = {INSTALL, local ? "prepare-kernel" : "prepare", other, dir,
			      NULL};
	if (eu_run(args, NULL, 0) < 0)
		fail("installation into the inactive slot failed");
	struct slots result;
	slot_status(&result);
	if (!eu_equal(result.running, s->running) ||
	    !eu_equal(result.confirmed, s->running) || result.next[0] ||
	    !eu_equal(result.prepared, other) || !eu_hash(result.digest, 64) ||
	    (expected && !eu_equal(expected, result.digest)))
		fail("inactive slot readback differs from prepared plan");
	checkpoint();
	struct eu_json *a = identity(other, result.digest);
	char *id = boot_id();
	eu_put(a, "boot", eu_text(id));
	free(id);
	eu_put(a, "profile", eu_text(text_field(profile, "profile")));
	eu_json_free(profile);
	eu_put(a, "phase", eu_text("prepared"));
	eu_put(a, "os_generation", eu_int(generation));
	if (eu_save(ATTEMPT, a, 0600) < 0)
		fail("cannot record prepared slot");
	boot_command("arm", other);
	struct slots after;
	slot_status(&after);
	if (!eu_equal(after.next, other) || !eu_equal(after.running, s->running) ||
	    !eu_equal(after.confirmed, s->running) ||
	    !eu_equal(after.prepared, other) || !eu_equal(after.digest, result.digest))
		fail("boot loader did not arm the prepared plan");
	eu_put(a, "phase", eu_text("armed"));
	if (eu_save(ATTEMPT, a, 0600) < 0)
		fail("cannot record armed slot");
	eu_json_free(a);
	eu_put(ui, "attempt", identity(other, result.digest));
	history("prepared", other);
	ready(reboot);
}
