/* SPDX-License-Identifier: BSD-2-Clause */
/* Import one FFS cohort prepared by the former signed-release installer. */
#include <sys/file.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "control.h"
#include "verify.h"

#define LEGACY "/var/db/ember-os-update/"
#define OWNER "/etc/ember-os-update.json"
#define SEALS "/usr/share/ember-update/handoff"
#define JOURNAL DATABASE "/handoff.json"

static bool
same_json(const struct eu_json *a, const struct eu_json *b)
{
	if (!a || !b || a->type != b->type)
		return false;
	switch (a->type) {
	case EU_NULL:
		return true;
	case EU_STRING:
	case EU_REAL:
		return eu_equal(a->text, b->text);
	case EU_BOOL:
	case EU_INTEGER:
		return a->integer == b->integer;
	case EU_ARRAY: {
		a = a->child;
		b = b->child;
		while (a && b && same_json(a, b)) {
			a = a->next;
			b = b->next;
		}
		return !a && !b;
	}
	case EU_OBJECT:
		for (const struct eu_json *v = a->child; v; v = v->next)
			if (!same_json(v, eu_get(b, v->key)))
				return false;
		for (const struct eu_json *v = b->child; v; v = v->next)
			if (!eu_get(a, v->key))
				return false;
		return true;
	}
	return false;
}

/* The seal is part of the signed base set. Its etc hash has no self-reference. */
bool
handoff_inputs(const struct eu_json *seal, const struct eu_json *old,
	       const struct eu_json *receipt, const char *defaults)
{
	const struct eu_json *p = eu_get(seal, "profile");
	const char *name = text_field(p, "profile");
	const struct eu_json *sets = eu_get(receipt, "sets"),
			     *roles = eu_get(p, "sets");
	const char *fields = "profile model kernel watchdog module_directory modules "
			     "tree_directory trees sets";
	if (!eu_fields(seal, "profile generation etc_sha256") ||
	    !eu_equal(kernel_mode(p), "slots") ||
	    !eu_equal(text_field(p, "adapter"), "uboot-ffs") ||
	    !eu_equal(text_field(seal, "etc_sha256"), defaults) ||
	    !eu_hash(defaults, 64) ||
	    !eu_fields(receipt, "schema generation os_commit profile sets target") ||
	    !eu_equal(text_field(receipt, "schema"), "emberbsd.os-update.v1") ||
	    !eu_equal(name, text_field(receipt, "profile")) ||
	    !eu_hash(text_field(receipt, "os_commit"), 40) ||
	    int_field(receipt, "generation") < 1 ||
	    int_field(receipt, "generation") != int_field(seal, "generation") ||
	    !eu_target_match(eu_get(receipt, "target"), eu_get(p, "target")) ||
	    !eu_fields(old, fields) || !roles || roles->type != EU_ARRAY || !sets ||
	    sets->type != EU_OBJECT)
		return false;
	for (const struct eu_json *v = old->child; v; v = v->next)
		if (!same_json(v, eu_get(p, v->key)))
			return false;
	/* The old boot adapter used these fixed wedge labels. */
	const struct eu_json *roots = eu_get(p, "roots");
	if (!eu_equal(text_field(roots, "A"), "netbsd-root") ||
	    !eu_equal(text_field(roots, "B"), "ember-root-b") ||
	    !eu_equal(text_field(p, "esp"), "EFI"))
		return false;
	unsigned seen = 0;
	for (const struct eu_json *v = roles->child; v; v = v->next) {
		unsigned i = 0;
		for (; i < OS_ROLES; i++)
			if (eu_equal(eu_string(v), os_role[i]))
				break;
		if (i == OS_ROLES || (seen & (1U << i)) ||
		    !eu_slug(text_field(sets, os_role[i])))
			return false;
		seen |= 1U << i;
	}
	return seen == 7 ? eu_fields(sets, "boot base etc")
			 : seen == 15 && eu_fields(sets, "boot base etc comp");
}

static void
copy_record(const char *from, const char *to, const char *hash)
{
	struct stat st;
	char digest[65];
	if (lstat(to, &st) != 0) {
		if (errno != ENOENT)
			err(1, "%s", to);
		char tmp[PATH_MAX];
		if (snprintf(tmp, sizeof(tmp), "%s.XXXXXXXX", to) >= (int)sizeof(tmp))
			errx(1, "handoff path too long");
		int fd = mkstemp(tmp);
		if (fd < 0)
			err(1, "%s", to);
		FILE *out = fdopen(fd, "wb"), *in = fopen(from, "rb");
		if (!in || !out)
			err(1, "open handoff receipt");
		unsigned char buf[32768];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
			if (fwrite(buf, 1, n, out) != n)
				err(1, "write handoff receipt");
		if (ferror(in) || fclose(in) || fflush(out) || fsync(fd) || fclose(out))
			err(1, "copy handoff receipt");
		if (file_sha(tmp, digest) < 0 || !eu_equal(digest, hash))
			errx(1, "handoff source changed");
		if (rename(tmp, to) < 0)
			err(1, "%s", to);
	}
	trusted(to, false);
	if (lstat(to, &st) < 0 || (st.st_mode & 077) || file_sha(to, digest) < 0 ||
	    !eu_equal(digest, hash))
		fail("imported record differs from the legacy release");
}

static void
exclude_peer(const char *path)
{
	int fd = open(path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0)
		err(1, "%s", path);
	trusted(path, false);
	if (flock(fd, LOCK_EX | LOCK_NB) < 0)
		fail("a legacy update service is active");
	/* Keep the legacy service lock until the handoff process exits. */
}

void
handoff(void)
{
	struct slots s, after;
	struct hardware h;
	exclude_peer("/var/run/ember_os_update.pid");
	exclude_peer("/var/run/ember_pkg_update.pid");
	struct eu_json *control = read_record(OWNER, false),
		       *receipt = read_record(LEGACY "release.json", false);
	struct eu_json *old = read_record("/etc/ember-uboot-slots.json", false);
	const char *name = text_field(control, "profile");
	char sealpath[PATH_MAX], hash[65], defaults[65];
	if (!eu_fields(control, "profile backend health") || !eu_slug(name) ||
	    !eu_equal(name, text_field(receipt, "profile")) ||
	    !eu_equal(text_field(control, "backend"), BOOT) ||
	    !eu_equal(text_field(control, "health"), HEALTH) ||
	    snprintf(sealpath, sizeof(sealpath), SEALS "/%s.json", name) >=
		(int)sizeof(sealpath))
		fail("unrecognized legacy update controller");
	struct eu_json *seal = read_record(sealpath, false);
	trusted(LEGACY "etc.tar.xz", false);
	if (file_sha(LEGACY "release.json", hash) < 0 ||
	    file_sha(LEGACY "etc.tar.xz", defaults) < 0 ||
	    !handoff_inputs(seal, old, receipt, defaults))
		fail("legacy release, defaults, or hardware differs from the signed "
		     "transition");
	if (read_record(ATTEMPT, true) || read_record(LOCAL, true) ||
	    read_record(LEGACY "attempt.json", true))
		fail("transition requires the clean root prepared by the legacy "
		     "installer");
	struct eu_json *p = eu_member(seal, "profile"),
		       *current = read_record(PROFILE, true);
	if (current) {
		if (!same_json(current, p))
			fail("installed hardware profile differs from transition");
	} else if (eu_save(PROFILE, p, 0644) < 0)
		fail("cannot install the hardware profile");
	slot_status(&s);
	struct eu_json *prior = read_record(JOURNAL, true);
	bool resume = prior && eu_fields(prior, "slot sha256") &&
		      eu_equal(text_field(prior, "slot"), s.running) &&
		      eu_equal(text_field(prior, "sha256"), hash);
	if ((prior && !resume) || s.next[0] || !eu_equal(s.prepared, s.running) ||
	    !eu_equal(s.digest, hash) ||
	    (eu_equal(s.running, s.confirmed) ? !resume : !s.trial))
		fail("legacy transition requires its consumed and identified trial");
	copy_record(LEGACY "release.json", RECEIPT, hash);
	copy_record(LEGACY "etc.tar.xz", DATABASE "/etc.tar.xz", defaults);
	copy_record(LEGACY "release.json", INSTALLED, hash);
	load_hardware(&h);
	if (h.single || h.custom)
		fail("legacy trial kernel differs from its signed release");
	trial_health();
	trusted("/etc/rc.d/ember_update", false);
	trusted("/etc/rc.conf", false);
	const char *policy[] = {"/etc/rc.d/ember_update", "handoff-policy", NULL};
	if (eu_run(policy, NULL, 0) < 0)
		fail("cannot migrate service policy; configure "
		     "/etc/rc.conf.d/ember_update");
	const char *archived = "/etc/ember-os-update.migrated.json";
	if (link(OWNER, archived) < 0 && errno != EEXIST)
		err(1, "archive legacy controller configuration");
	struct stat original, backup;
	if (lstat(OWNER, &original) || lstat(archived, &backup) ||
	    original.st_dev != backup.st_dev || original.st_ino != backup.st_ino)
		fail("a different archived legacy configuration exists");
	int policyfd = open("/etc/rc.conf", O_RDONLY | O_NOFOLLOW);
	int parentfd = open("/etc", O_RDONLY | O_DIRECTORY);
	if (policyfd < 0 || fsync(policyfd) < 0 || close(policyfd) < 0 ||
	    parentfd < 0 || fsync(parentfd) < 0 || close(parentfd) < 0)
		err(1, "sync transition policy");
	if (!prior) {
		prior = eu_new(EU_OBJECT);
		eu_put(prior, "slot", eu_text(s.running));
		eu_put(prior, "sha256", eu_text(hash));
		if (eu_save(JOURNAL, prior, 0600) < 0)
			fail("cannot save transition confirmation");
	}
	/* The journal is durable after the imports. Legacy G4 could only arm a
	 * generation above its installed and attempted generations. */
	checkpoint();
	if (s.trial) {
		const char *args[] = {BOOT, "confirm", s.running, NULL};
		if (eu_run(args, NULL, 0) < 0)
			fail("cannot confirm the migrated trial");
	}
	slot_status(&after);
	if (!eu_equal(after.running, s.running) ||
	    !eu_equal(after.confirmed, s.running) || after.next[0] ||
	    !eu_equal(after.prepared, s.running) || !eu_equal(after.digest, hash))
		fail("boot loader did not confirm the transition");
	if (unlink(OWNER) < 0)
		err(1, "retire legacy controller");
	int directory = open("/etc", O_RDONLY | O_DIRECTORY);
	if (directory < 0 || fsync(directory) < 0 || close(directory) < 0)
		err(1, "sync retired controller");
	history("confirmed", s.running);
	report("idle", NULL);
}
