/* SPDX-License-Identifier: BSD-2-Clause */
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "catalog.h"
#include "verify.h"

const char *const os_role[OS_ROLES] = {"boot", "base", "etc", "comp"};

bool
eu_equal(const char *s, const char *t)
{
	return s != NULL && t != NULL && strcmp(s, t) == 0;
}

bool
eu_fields(const struct eu_json *o, const char *fields)
{
	const struct eu_json *p;
	const char *s, *end;
	size_t count = 0, seen = 0;
	if (o == NULL || o->type != EU_OBJECT)
		return false;
	for (s = fields; *s; s = end) {
		end = strchr(s, ' ');
		if (end == NULL)
			end = s + strlen(s);
		for (p = o->child; p; p = p->next)
			if (strlen(p->key) == (size_t)(end - s) &&
			    memcmp(p->key, s, end - s) == 0)
				break;
		if (p == NULL)
			return false;
		count++;
		end += *end != 0;
	}
	for (p = o->child; p; p = p->next)
		seen++;
	return seen == count;
}

bool
eu_slug(const char *s)
{
	if (s == NULL || *s == 0 || *s == '.' || *s == '-' || *s == '_')
		return false;
	return s[strspn(s, "abcdefghijklmnopqrstuvwxyz0123456789-._")] == 0;
}

bool
eu_hash(const char *s, size_t len)
{
	return s != NULL && strlen(s) == len && s[strspn(s, "0123456789abcdef")] == 0;
}

static bool
safe_path(const char *s)
{
	const char *end;
	size_t len;
	if (s == NULL || strlen(s) >= 4096)
		return false;
	for (;;) {
		end = strchr(s, '/');
		len = end ? (size_t)(end - s) : strlen(s);
		if (len == 0 || len > 255 || (len == 1 && *s == '.') ||
		    (len == 2 && s[0] == '.' && s[1] == '.') ||
		    strspn(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123"
			      "456789._+-") != len)
			return false;
		if (!end)
			return true;
		s = end + 1;
	}
}

static bool
text_ok(const char *s)
{
	bool nonblank = false;
	if (!s)
		return false;
	while (*s) {
		unsigned char c = (unsigned char)*s++;
		if (c < 32 || c == 127)
			return false;
		if (c != ' ')
			nonblank = true;
	}
	return nonblank;
}

static const char *
field(const struct eu_json *p, const char *s)
{
	return eu_string(eu_get(p, s));
}

static int64_t
number(const struct eu_json *p, const char *s)
{
	p = eu_get(p, s);
	return p && p->type == EU_INTEGER ? p->integer : -1;
}

static bool
platform(const struct eu_json *o, bool host)
{
	const struct eu_json *p;
	if (!eu_fields(o, host ? "os arch abi runtime" : "arch triplet abi runtime"))
		return false;
	for (p = o->child; p; p = p->next)
		if (!text_ok(eu_string(p)))
			return false;
	return true;
}

bool
eu_target_match(const struct eu_json *a, const struct eu_json *b)
{
	const struct eu_json *p;
	if (!platform(a, false) || !platform(b, false))
		return false;
	for (p = a->child; p; p = p->next)
		if (!eu_equal(p->text, field(b, p->key)))
			return false;
	return true;
}

const char *
eu_channel_error(const struct eu_json *o, const char *channel, time_t now,
	   struct snapshot_ref *ref)
{
	const struct eu_json *r = eu_get(o, "release");
	char canonical[256];
	if (!eu_fields(o, "schema channel issued_at expires_at release") ||
	    !eu_fields(r, "id path size sha256") ||
	    !eu_equal(field(o, "schema"), "emberbsd.channel.v2") || !eu_slug(channel) ||
	    !eu_equal(field(o, "channel"), channel))
		return "invalid signed channel";
	ref->id = field(r, "id");
	ref->path = field(r, "path");
	ref->sha = field(r, "sha256");
	ref->size = number(r, "size");
	ref->issued = number(o, "issued_at");
	ref->expires = number(o, "expires_at");
	if (!eu_slug(ref->id) || !eu_hash(ref->sha, 64) || ref->size < 1 ||
	    ref->size > EU_MAX_CMS || ref->issued <= 0 || ref->expires <= ref->issued ||
	    snprintf(canonical, sizeof(canonical), "%s/metadata/%s/release.p7m",
		     channel, ref->sha) >= (int)sizeof(canonical) ||
	    !eu_equal(ref->path, canonical))
		return "invalid signed channel";
	if (now < ref->issued)
		return "signed channel is not yet valid. Check the system clock.";
	if (now >= ref->expires)
		return "signed channel has expired. Check the system clock and the channel.";
	return NULL;
}

static bool
artifact(const struct eu_json *o, struct artifact *a)
{
	const struct eu_json *licenses, *p, *q;
	bool h, t;
	if (!eu_fields(
		o,
		"id path kind host target size unpacked_size sha256 depends licenses"))
		return false;
	a->object = o;
	a->id = field(o, "id");
	a->path = field(o, "path");
	a->kind = field(o, "kind");
	a->sha = field(o, "sha256");
	a->target = eu_get(o, "target");
	a->host = eu_get(o, "host");
	a->deps = eu_get(o, "depends");
	a->size = number(o, "size");
	a->unpacked = number(o, "unpacked_size");
	licenses = eu_get(o, "licenses");
	if (!eu_slug(a->id) || !safe_path(a->path) || !eu_hash(a->sha, 64) ||
	    a->size < 0 || a->unpacked < 0 || a->deps->type != EU_ARRAY ||
	    licenses->type != EU_ARRAY || !licenses->child)
		return false;
	h = a->host->type != EU_NULL;
	t = a->target->type != EU_NULL;
	if (h == t || (h && !platform(a->host, true)) ||
	    (t && !platform(a->target, false)))
		return false;
	if (!(eu_equal(a->kind, "package-index") ||
	      (h &&
	       (eu_equal(a->kind, "host-package") || eu_equal(a->kind, "bootstrap"))) ||
	      (t &&
	       (eu_equal(a->kind, "target-package") || eu_equal(a->kind, "sysroot") ||
		eu_equal(a->kind, "os-set") || eu_equal(a->kind, "image")))))
		return false;
	for (p = licenses->child; p; p = p->next)
		if (!text_ok(eu_string(p)))
			return false;
	for (p = a->deps->child; p; p = p->next) {
		if (!eu_slug(eu_string(p)))
			return false;
		for (q = a->deps->child; q != p; q = q->next)
			if (eu_equal(q->text, p->text))
				return false;
	}
	return true;
}

static int
by_id(const void *l, const void *r)
{
	return strcmp(((const struct artifact *)l)->id,
		      ((const struct artifact *)r)->id);
}

static int
by_path(const void *l, const void *r)
{
	return strcasecmp(*(const char *const *)l, *(const char *const *)r);
}

struct artifact *
eu_artifact(const struct release *r, const char *id)
{
	struct artifact key = {.id = id};
	return id ? bsearch(&key, r->items, r->count, sizeof(*r->items), by_id) : NULL;
}

/* Traverse the publisher's complete graph with a bounded explicit stack. */
static bool
graph(const struct release *r)
{
	struct frame {
		struct artifact *a;
		const struct eu_json *dep;
	};
	struct frame *stack = calloc(r->count, sizeof(*stack));
	size_t n = 0, i;
	bool ok = false;
	if (!stack)
		return false;
	for (i = 0; i < r->count; i++) {
		if (r->items[i].mark)
			continue;
		r->items[i].mark = 1;
		stack[n++] = (struct frame){r->items + i, r->items[i].deps->child};
		while (n) {
			struct frame *f = &stack[n - 1];
			if (f->dep == NULL) {
				f->a->mark = 2;
				n--;
				continue;
			}
			struct artifact *a = eu_artifact(r, f->dep->text);
			f->dep = f->dep->next;
			if (!a || a->mark == 1)
				goto done;
			if (a->mark == 2)
				continue;
			a->mark = 1;
			stack[n++] = (struct frame){a, a->deps->child};
		}
	}
	ok = true;
done:
	free(stack);
	return ok;
}

bool
eu_release(const struct eu_json *o, const struct snapshot_ref *ref, struct release *r)
{
	const struct eu_json *records = eu_get(o, "artifacts"),
			     *sources = eu_get(o, "sources"), *p;
	char *copy = NULL, *sep;
	const char **paths = NULL, *query;
	size_t i = 0;
	bool ok = false;
	memset(r, 0, sizeof(*r));
	if (!eu_fields(o, "schema release_id created_at sources artifacts") ||
	    !eu_equal(field(o, "schema"), "emberbsd.release.v1") ||
	    !eu_equal(field(o, "release_id"), ref->id) || number(o, "created_at") < 1 ||
	    !eu_fields(sources, "os ports sdk") || records->type != EU_ARRAY ||
	    !records->child)
		return false;
	for (p = sources->child; p; p = p->next)
		if (!eu_hash(eu_string(p), 40))
			return false;
	r->id = ref->id;
	for (p = records->child; p; p = p->next)
		r->count++;
	r->items = calloc(r->count, sizeof(*r->items));
	paths = calloc(r->count, sizeof(*paths));
	if (!r->items || !paths)
		goto done;
	for (p = records->child; p; p = p->next, i++) {
		if (!artifact(p, &r->items[i]))
			goto done;
		paths[i] = r->items[i].path;
	}
	qsort(r->items, r->count, sizeof(*r->items), by_id);
	qsort(paths, r->count, sizeof(*paths), by_path);
	for (i = 0; i < r->count; i++) {
		if (i && (strcmp(r->items[i - 1].id, r->items[i].id) == 0 ||
			  strcasecmp(paths[i - 1], paths[i]) == 0))
			goto done;
		if (!(copy = strdup(paths[i])))
			goto done;
		query = copy;
		for (sep = copy; (sep = strchr(sep, '/')) != NULL; sep++) {
			*sep = 0;
			if (bsearch(&query, paths, r->count, sizeof(*paths), by_path))
				goto done;
			*sep = '/';
		}
		free(copy);
		copy = NULL;
	}
	ok = graph(r);
done:
	free(copy);
	free(paths);
	if (!ok) {
		free(r->items);
		memset(r, 0, sizeof(*r));
	}
	return ok;
}

bool
eu_os_update(const struct eu_json *o, const struct artifact *head,
	     const struct release *release, const char *profile,
	     const struct eu_json *target, struct os_update *result)
{
	const struct eu_json *sets = eu_get(o, "sets"), *p;
	const char *leaf;
	char name[512];
	int i, j;
	size_t count = 0;
	if (!eu_fields(o, "schema profile generation target os_commit sets") ||
	    !eu_equal(field(o, "schema"), "emberbsd.os-update.v1") ||
	    !eu_equal(field(o, "profile"), profile) || !eu_slug(profile) ||
	    !eu_equal(head->kind, "os-set") || head->size <= 0 || head->size > 32768 ||
	    strncmp(head->id, "os-update-", 10) || !eu_equal(head->id + 10, profile) ||
	    !eu_target_match(target, head->target) ||
	    !eu_target_match(target, eu_get(o, "target")) ||
	    !eu_hash(result->commit = field(o, "os_commit"), 40) ||
	    (result->generation = number(o, "generation")) < 1 ||
	    !(eu_fields(sets, "boot base etc") ||
	      eu_fields(sets, "boot base etc comp")))
		return false;
	leaf = strrchr(head->path, '/');
	leaf = leaf ? leaf + 1 : head->path;
	if (snprintf(name, sizeof(name), "%s-g%" PRId64 ".json", head->id,
		     result->generation) >= (int)sizeof(name) ||
	    strcmp(name, leaf) != 0)
		return false;
	for (i = 0; i < OS_ROLES; i++) {
		struct artifact *a = eu_artifact(release, field(sets, os_role[i]));
		result->member[i] = a;
		if (i == OS_COMP && !eu_get(sets, "comp"))
			continue;
		if (!a || !eu_equal(a->kind, "os-set") ||
		    !eu_target_match(target, a->target) || a->deps->child ||
		    a->size <= 0 || strncmp(a->id, "os-update-", 10) == 0 ||
		    strlen(a->path) < 7 ||
		    strcmp(a->path + strlen(a->path) - 7, ".tar.xz"))
			return false;
		for (j = 0; j < i; j++)
			if (result->member[j] == a)
				return false;
		for (p = head->deps->child; p; p = p->next)
			if (eu_equal(p->text, a->id))
				break;
		if (p == NULL)
			return false;
		count++;
	}
	for (p = head->deps->child; p; p = p->next) {
		if (count == 0)
			return false;
		count--;
	}
	return count == 0;
}
