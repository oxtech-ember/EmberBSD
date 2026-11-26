/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "control.h"
#include "store.h"
#include "verify.h"

#define CACHE DATABASE "/catalog.json"
#define CACHE_SCHEMA "emberbsd.store-cache.v1"
#define INDEX "emberbsd.catalog-index.v1"

/* A root check publishes only display fields from the verified repository.
 * Installation always makes a new plan from the authenticated channel. */
static struct eu_json *
projection(const struct delivery *d, const struct eu_json *target,
	   const struct packages *list)
{
	struct eu_json *doc = eu_new(EU_OBJECT), *t = eu_new(EU_OBJECT),
		       *rows = eu_new(EU_ARRAY);
	eu_put(doc, "schema", eu_text(CACHE_SCHEMA));
	eu_put(doc, "channel", eu_text(d->channel));
	eu_put(doc, "release_id", eu_text(d->ref.id));
	eu_put(doc, "issued_at", eu_int(d->ref.issued));
	eu_put(doc, "expires_at", eu_int(d->ref.expires));
	eu_put(doc, "sha256", eu_text(d->ref.sha));
	for (const struct eu_json *p = target->child; p; p = p->next)
		eu_put(t, p->key, eu_text(eu_string(p)));
	eu_put(doc, "target", t);
	eu_put(doc, "repository", eu_text(text_field(list->catalog, "repository")));
	eu_put(doc, "packages", rows);
	for (size_t i = 0; i < list->count; i++) {
		const struct package *p = list->list + i;
		struct eu_json *r = eu_new(EU_OBJECT);
		eu_put(r, "root", eu_text(p->root));
		eu_put(r, "pkgname", eu_text(p->name));
		eu_put(r, "size", eu_int(p->artifact->size));
		eu_append(rows, r);
	}
	return doc;
}

void
store_cache(const struct delivery *d, const struct eu_json *target,
	    const struct packages *list)
{
	struct eu_json *doc = projection(d, target, list);
	if (eu_save(CACHE, doc, 0644) < 0)
		warn("save %s", CACHE);
	eu_json_free(doc);
}

static const char *
catalog_role(const struct package *p)
{
	static const char *const libs[] = {"archivers", "converters", "databases",
					   "devel",	"fonts",      "lang",
					   "print",	"security",   "textproc"};
	size_t n = strcspn(p->root, "/");
	for (size_t i = 0; i < sizeof(libs) / sizeof(libs[0]); i++)
		if (strlen(libs[i]) == n && !strncmp(libs[i], p->root, n))
			return "library";
	return !strncmp(p->base, "lib", 3) || !strncmp(p->base, "py-", 3) ||
		       !strncmp(p->base, "py3", 3)
		   ? "library"
		   : "package";
}

struct row {
	const struct package *p;
	int64_t size;
};
static int
by_root(const void *a, const void *b)
{
	return strcmp(((const struct row *)a)->p->root,
		      ((const struct row *)b)->p->root);
}

struct eu_json *
store_rows(const struct eu_json *doc, const char *prefix, const char *db)
{
	const struct eu_json *rows = eu_get(doc, "packages");
	const char *repo = text_field(doc, "repository");
	struct packages list = {0};
	struct row *order = NULL;
	struct eu_json *result = NULL;
	if (!eu_slug(repo) || !eu_slug(text_field(doc, "release_id")) || !rows ||
	    rows->type != EU_ARRAY)
		goto done;
	for (const struct eu_json *p = rows->child; p; p = p->next)
		list.count++;
	list.list = calloc(list.count + 1, sizeof(*list.list));
	order = calloc(list.count + 1, sizeof(*order));
	if (!list.list || !order)
		goto done;
	size_t i = 0;
	for (const struct eu_json *r = rows->child; r; r = r->next, i++) {
		struct package *p = list.list + i;
		p->name = text_field(r, "pkgname");
		p->root = text_field(r, "root");
		if (!eu_pkgname(p->name) || !eu_pkgroot(p->root) ||
		    int_field(r, "size") <= 0)
			goto done;
		p->base = strndup(p->name, (size_t)(strrchr(p->name, '-') - p->name));
		if (!p->base)
			goto done;
		order[i] = (struct row){p, int_field(r, "size")};
	}
	qsort(order, list.count, sizeof(*order), by_root);
	if (eu_inventory(db, &list) < 0)
		goto done;
	result = eu_new(EU_OBJECT);
	struct eu_json *counts = eu_new(EU_OBJECT), *out = eu_new(EU_ARRAY);
	const char *keys[] = {"total",	   "package",	      "library",
			      "desktop",   "installed",	      "upgrade",
			      "available", "installed_newer", "version_uncompared"};
	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		eu_put(counts, keys[i], eu_int(0));
	eu_put(result, "schema", eu_text(INDEX));
	eu_put(result, "release_id", eu_text(text_field(doc, "release_id")));
	eu_put(result, "desktop_metadata", eu_bool(false));
	struct stat ds, ps;
	bool binding = !stat(db, &ds) && S_ISDIR(ds.st_mode) && !stat(prefix, &ps) &&
		       S_ISDIR(ps.st_mode);
	eu_put(result, "desktop_binding",
	       eu_text(binding ? "installed-pkgdb" : "unavailable"));
	eu_put(result, "counts", counts);
	eu_put(result, "packages", out);
	for (i = 0; i < list.count; i++) {
		const struct package *p = order[i].p;
		const char *action = eu_package_action(p), *cls = catalog_role(p);
		struct eu_json *r = eu_new(EU_OBJECT),
			       *desktops = desktop_entries(prefix, db, p->installed);
		const char *role = desktops->child ? "desktop" : cls;
		eu_put(r, "repository", eu_text(repo));
		eu_put(r, "root", eu_text(p->root));
		eu_put(r, "pkgname", eu_text(p->name));
		eu_put(r, "base", eu_text(p->base));
		eu_put(r, "version", eu_text(strrchr(p->name, '-') + 1));
		eu_put(r, "role", eu_text(role));
		eu_put(r, "catalog_role", eu_text(cls));
		eu_put(r, "action", eu_text(action));
		eu_put(r, "installed_pkgname", eu_text(p->installed));
		eu_put(r, "size", eu_int(order[i].size));
		eu_put(r, "prefix", eu_text(prefix));
		eu_put(r, "desktops", desktops);
		eu_append(out, r);
		eu_member(counts, "total")->integer++;
		const char *key = !strcmp(action, "installed-newer") ? "installed_newer"
				  : !strcmp(action, "version-uncompared")
				      ? "version_uncompared"
				      : action;
		eu_member(counts, key)->integer++;
		if (desktops->child) {
			for (const struct eu_json *it = desktops->child; it;
			     it = it->next)
				eu_member(counts, "desktop")->integer++;
		} else
			eu_member(counts, role)->integer++;
	}
done:
	free(order);
	eu_packages_free(&list);
	return result;
}

int
store_catalog(void)
{
	struct policy policy;
	read_policy(&policy);
	struct eu_json *profile = read_record(PROFILE, false);
	const struct eu_json *target = eu_get(profile, "target");
	if (!eu_target_match(target, target))
		errx(1, "invalid catalog target");
	struct eu_json *cached = NULL, *index = NULL;
	struct stat st;
	if (lstat(CACHE, &st) == 0) {
		trusted(CACHE, false);
		cached = eu_json_file(CACHE, EU_MAX_CONTENT);
		if (!cached)
			errx(1, "invalid root-owned package catalog");
	} else if (errno != ENOENT)
		err(1, "%s", CACHE);
	bool compatible = cached &&
			  eu_equal(text_field(cached, "schema"), CACHE_SCHEMA) &&
			  eu_equal(text_field(cached, "channel"), policy.channel) &&
			  eu_target_match(target, eu_get(cached, "target"));
	time_t now = time(NULL);
	if (compatible && int_field(cached, "issued_at") <= now &&
	    now < int_field(cached, "expires_at"))
		index = store_rows(cached, "/usr/pkg", "/usr/pkg/pkgdb");
	else {
		struct delivery d;
		struct packages list;
		trusted(ANCHOR, false);
		if (eu_snapshot(&d, ORIGIN, policy.channel, ANCHOR) < 0)
			errx(1, "cannot verify package catalog release");
		if (compatible &&
		    (d.ref.issued < int_field(cached, "issued_at") ||
		     (d.ref.issued == int_field(cached, "issued_at") &&
		      !eu_equal(d.ref.sha, text_field(cached, "sha256")))))
			errx(1, "package catalog predates the cached release");
		if (eu_package_index(&d, target, &list) < 0)
			errx(1, "invalid or unavailable package catalog");
		struct eu_json *view = projection(&d, target, &list);
		index = store_rows(view, "/usr/pkg", "/usr/pkg/pkgdb");
		eu_json_free(view);
		eu_packages_free(&list);
		eu_snapshot_free(&d);
	}
	if (!index)
		errx(1, "cannot read installed catalog inventory");
	if (eu_emit(stdout, index) < 0 || putchar('\n') == EOF || fflush(stdout) < 0)
		err(1, "catalog output");
	eu_json_free(index);
	eu_json_free(cached);
	eu_json_free(profile);
	free(policy.channel);
	free(policy.window);
	return 0;
}
