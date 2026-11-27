/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "control.h"
#include "package.h"
#include "store.h"
#include "verify.h"

static void
persist(const char *name, struct eu_json *doc)
{
	if (eu_save(name, doc, 0600) < 0)
		fail("cannot store signed release floor");
	eu_json_free(doc);
}

static void
channel_floor(const struct delivery *d)
{
	char path[PATH_MAX];
	if (snprintf(path, sizeof(path), DATABASE "/channel-%s.json", d->channel) >=
	    (int)sizeof(path))
		fail("channel name is too long");
	struct eu_json *seen = read_record(path, true);
	if (seen) {
		int64_t issued = int_field(seen, "issued_at");
		const char *digest = text_field(seen, "sha256");
		if (issued <= 0 || !eu_hash(digest, 64) || d->ref.issued < issued ||
		    (d->ref.issued == issued && !eu_equal(digest, d->ref.sha)))
			fail("signed channel is older than the saved release or "
			     "conflicts with it");
		eu_json_free(seen);
	}
	seen = eu_new(EU_OBJECT);
	eu_put(seen, "issued_at", eu_int(d->ref.issued));
	eu_put(seen, "sha256", eu_text(d->ref.sha));
	persist(path, seen);
}

static int64_t
os_floor(const struct hardware *h, const struct artifact *head,
	 const struct os_update *u)
{
	const char *path = DATABASE "/os-seen.json";
	struct eu_json *seen = read_record(path, true);
	char commitment[65], input[(OS_ROLES + 1) * 64 + 1];
	char *end = input;
	memcpy(end, head->sha, 64);
	end += 64;
	for (int i = 0; i < OS_ROLES; i++)
		if (u->member[i]) {
			memcpy(end, u->member[i]->sha, 64);
			end += 64;
		}
	if (eu_digest(input, (size_t)(end - input), commitment) < 0)
		fail("cannot identify signed OS payloads");
	if (u->generation < h->generation)
		fail("signed OS generation precedes the installed OS");
	if (u->generation == h->generation && h->generation > 0) {
		char installed[65];
		if (file_sha(RECEIPT, installed) < 0 || !eu_equal(head->sha, installed))
			fail("published OS descriptor conflicts with the installed "
			     "generation");
	}
	if (seen) {
		int64_t highest = int_field(seen, "generation");
		if (!eu_equal(h->name, text_field(seen, "profile")) ||
		    !eu_hash(text_field(seen, "sha256"), 64) || highest < 1 ||
		    u->generation < highest ||
		    (u->generation == highest &&
		     !eu_equal(text_field(seen, "sha256"), commitment)))
			fail("signed OS set conflicts with the previously observed "
			     "generation");
		eu_json_free(seen);
	}
	seen = eu_new(EU_OBJECT);
	eu_put(seen, "profile", eu_text(h->name));
	eu_put(seen, "generation", eu_int(u->generation));
	eu_put(seen, "sha256", eu_text(commitment));
	persist(path, seen);
	struct eu_json *bad = read_record(DATABASE "/os-bad.json", true);
	int64_t floor = h->generation;
	if (bad) {
		int64_t failed = int_field(bad, "generation");
		if (failed < 0 || !eu_equal(text_field(bad, "profile"), h->name))
			fail("invalid rejected OS generation");
		if (failed > floor)
			floor = failed;
		eu_json_free(bad);
	}
	return floor;
}

static void
write_descriptor(const unsigned char *raw, size_t n)
{
	const char *path = INCOMING "/update.json";
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		fail("cannot create staging OS descriptor");
	for (size_t used = 0; used < n;) {
		ssize_t count = write(fd, raw + used, n - used);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			fail("cannot write staging OS descriptor");
		used += (size_t)count;
	}
	if (fsync(fd) < 0 || close(fd) < 0)
		fail("cannot save staging OS descriptor");
}

static void
drop_stage(void)
{
	const char *args[] = {"/bin/rm", "-rf", INCOMING, NULL};
	(void)eu_run(args, NULL, 0);
}

static void
clean_stage(void)
{
	const char *args[] = {"/bin/rm", "-rf", INCOMING, NULL};
	if (eu_run(args, NULL, 0) < 0)
		fail("cannot remove the previous download stage");
}

static struct eu_json *
make_plan(const struct hardware *h, const struct os_update *u,
	  const struct artifact *head, const struct packages *pkgs)
{
	struct eu_json *plan = eu_new(EU_OBJECT), *target = eu_new(EU_OBJECT),
		       *items = eu_new(EU_ARRAY);
	eu_put(plan, "schema", eu_text(PLAN));
	eu_put(plan, "profile", eu_text(h->name));
	const char *keys[] = {"arch", "triplet", "abi", "runtime"};
	for (size_t i = 0; i < 4; i++)
		eu_put(target, keys[i], eu_text(text_field(h->target, keys[i])));
	eu_put(plan, "target", target);
	eu_put(plan, "os_generation", eu_int(head ? u->generation : h->generation));
	eu_put(plan, "os_commit", eu_text(head ? u->commit : h->commit));
	eu_put(plan, "os_update_sha256", eu_text(head ? head->sha : NULL));
	eu_put(plan, "packages", items);
	for (size_t i = 0; i < pkgs->updates; i++) {
		const struct package *p = pkgs->order[i];
		struct eu_json *item = eu_new(EU_OBJECT);
		eu_put(item, "name", eu_text(p->name));
		if (p->manual)
			eu_put(item, "manual", eu_bool(true));
		eu_put(item, "sha256", eu_text(p->artifact->sha));
		eu_put(item, "size", eu_int(p->artifact->size));
		eu_append(items, item);
	}
	return plan;
}

static void
stage(const struct delivery *d, const struct artifact *head, const unsigned char *raw,
      size_t len, const struct os_update *os, const struct packages *packages)
{
	char path[PATH_MAX];
	if (head) {
		write_descriptor(raw, len);
		for (size_t i = 0; i < OS_ROLES; i++) {
			if (!os->member[i])
				continue;
			checkpoint();
			snprintf(path, sizeof(path), INCOMING "/%s.tar.xz", os_role[i]);
			if (eu_payload(d, os->member[i], path, NULL, NULL) < 0)
				fail("OS set download failed verification");
		}
	}
	if (mkdir(INCOMING "/packages", 0700) < 0)
		fail("cannot create package stage");
	for (size_t i = 0; i < packages->updates; i++) {
		const struct package *p = packages->order[i];
		checkpoint();
		if (snprintf(path, sizeof(path), INCOMING "/packages/%s.tgz",
			     p->name) >= (int)sizeof(path) ||
		    eu_payload(d, p->artifact, path, NULL, NULL) < 0)
			fail("package archive download failed verification");
	}
}

void
release_cycle(const struct slots *s, const struct hardware *h,
	      const struct policy *policy, bool apply, bool reboot,
	      const struct request *request)
{
	struct delivery d;
	struct os_update os = {0};
	struct packages pkgs = {0};
	const struct artifact *head = NULL;
	unsigned char *raw = NULL;
	struct eu_json *desc = NULL;
	size_t n = 0;
	char id[256];
	struct stat st;
	if (apply && lstat(INCOMING, &st) == 0)
		clean_stage();
	trusted(ANCHOR, false);
	checkpoint();
	report("checking", NULL);
	const char *error = eu_snapshot(&d, ORIGIN, policy->channel, ANCHOR);
	if (error != NULL)
		fail(error);
	channel_floor(&d);
	checkpoint();
	if (!request && !h->single && policy->os && !h->custom) {
		if (snprintf(id, sizeof(id), "os-update-%s", h->name) >=
		    (int)sizeof(id))
			fail("invalid OS profile name");
		head = eu_artifact(&d.release, id);
		if (head) {
			if (!eu_equal(head->kind, "os-set") ||
			    !eu_target_match(h->target, head->target) ||
			    head->size < 1 || head->size > 32768 ||
			    eu_payload(&d, head, NULL, &raw, &n) < 0 ||
			    !(desc = eu_json_parse(raw, n)) ||
			    !eu_os_update(desc, head, &d.release, h->name, h->target,
					  &os))
				fail("OS update descriptor does not match profile or "
				     "signed set roles");
			const struct eu_json *roles = eu_get(h->profile, "sets");
			if (!roles || roles->type != EU_ARRAY)
				fail("device profile has no OS set selection");
			unsigned mask = 0;
			for (const struct eu_json *role = roles->child; role;
			     role = role->next) {
				unsigned i = 0;
				for (; i < OS_ROLES &&
				       !eu_equal(eu_string(role), os_role[i]);
				     i++)
					continue;
				if (i == OS_ROLES || !os.member[i] ||
				    (mask & (1U << i)))
					fail("OS update differs from profile set "
					     "selection");
				mask |= 1U << i;
			}
			for (unsigned i = 0; i < OS_ROLES; i++)
				if (os.member[i] && !(mask & (1U << i)))
					fail("OS update has an unexpected set role");
			if (os.generation <= os_floor(h, head, &os))
				head = NULL;
		}
	}
	if (policy->packages || request) {
		bool has_catalog = false;
		for (size_t i = 0; i < d.release.count; i++) {
			const struct artifact *a = d.release.items + i;
			if (eu_target_match(a->target, h->target) &&
			    eu_equal(a->kind, "package-index"))
				has_catalog = true;
		}
		if (request && !has_catalog)
			fail("this release has no package catalog for the target");
		if (has_catalog) {
			if (eu_package_index(&d, h->target, &pkgs) < 0)
				fail("signed package catalog or summary is invalid");
			store_cache(&d, h->target, &pkgs);
			if (eu_plan_packages(&pkgs, "/usr/pkg/pkgdb",
					     request ? request->root : NULL,
					     request ? request->pin : NULL) < 0)
				fail(request ? "requested package, version or "
					       "dependency plan is unavailable"
					     : "cannot select compatible installed "
					       "package upgrades");
		}
	}
	struct eu_json *available = eu_new(EU_OBJECT), *names = eu_new(EU_ARRAY);
	eu_put(available, "os_generation",
	       head ? eu_int(os.generation) : eu_new(EU_NULL));
	for (size_t i = 0; i < pkgs.updates; i++)
		eu_append(names, eu_text(pkgs.order[i]->name));
	eu_put(available, "packages", names);
	eu_put(ui, "available", available);
	eu_put(ui, "checked_at", eu_int(time(NULL)));
	eu_put(ui, "release_id", eu_text(d.ref.id));
	if (!head && !pkgs.updates) {
		report("idle", NULL);
		goto done;
	}
	report("available", NULL);
	if (!apply)
		goto done;
	checkpoint();
	if (atexit(drop_stage) != 0)
		fail("cannot register staging cleanup");
	if (mkdir(INCOMING, 0700) < 0)
		fail("cannot create download stage");
	struct eu_json *plan = make_plan(h, &os, head, &pkgs);
	if (eu_save(INCOMING "/plan.json", plan, 0600) < 0)
		fail("cannot save verified install plan");
	eu_json_free(plan);
	char digest[65];
	if (file_sha(INCOMING "/plan.json", digest) < 0)
		fail("cannot identify verified install plan");
	struct eu_json *bad = read_record(ATTEMPT, true);
	if (bad && eu_equal(text_field(bad, "sha256"), digest)) {
		if (request)
			fail("this application plan already failed its boot trial");
		report("idle", NULL);
		eu_json_free(bad);
		clean_stage();
		goto done;
	}
	eu_json_free(bad);
	report("preparing", NULL);
	stage(&d, head, raw, n, &os, &pkgs);
	if (time(NULL) >= d.ref.expires)
		fail("signed channel expired during download");
	if (h->single) {
		trusted(INSTALL, false);
		checkpoint();
		const char *args[] = {INSTALL, "packages", INCOMING, NULL};
		int status = eu_run(args, NULL, 0);
		for (size_t i = 0; i < pkgs.updates; i++) {
			if (eu_registered("/usr/pkg/pkgdb", pkgs.order[i]->name))
				history("confirmed", pkgs.order[i]->name);
			else
				status = -1;
		}
		if (status < 0)
			fail("package update stopped; the next check resumes from the "
			     "installed package database");
		eu_put(available, "packages", eu_new(EU_ARRAY));
		report("idle", NULL);
	} else {
		struct request exact = {0};
		if (request) {
			exact.root = request->root;
			for (size_t i = 0; i < pkgs.updates; i++)
				if (pkgs.order[i]->manual)
					exact.pin = pkgs.order[i]->name;
			if (!exact.pin)
				fail("cannot identify the requested package in the "
				     "prepared plan");
		}
		prepare(s, INCOMING, digest, false, head ? os.generation : 0, reboot,
			request ? &exact : NULL);
	}
	clean_stage();
done:
	free(raw);
	eu_json_free(desc);
	eu_packages_free(&pkgs);
	eu_snapshot_free(&d);
}
