/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define ZLIB_CONST
#include <zlib.h>

#include "local.h"
#include "package.h"
#include "verify.h"

static const char *
field(const struct eu_json *p, const char *key)
{
	return eu_string(eu_get(p, key));
}

/* Use the native pkg_install rules for Dewey versions and match expressions. */
static int
match(const char *pattern, const char *name)
{
	pid_t child = fork();
	int st;
	if (child < 0)
		return -1;
	if (child == 0) {
		execl("/usr/sbin/pkg_admin", "pkg_admin", "pmatch", pattern, name,
		      (char *)NULL);
		_exit(127);
	}
	while (waitpid(child, &st, 0) < 0)
		if (errno != EINTR)
			return -1;
	if (!WIFEXITED(st) || WEXITSTATUS(st) > 1)
		return -1;
	return WEXITSTATUS(st) == 0;
}

static bool
pkgname(const char *s)
{
	const char *v;
	return s && strlen(s) <= 240 && isalnum((unsigned char)s[0]) &&
	       s[strspn(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456"
			   "789_+.-")] == 0 &&
	       (v = strrchr(s, '-')) && v != s && v[1] >= '0' && v[1] <= '9';
}

static struct package *
by_name(struct packages *list, const char *s, bool base)
{
	for (size_t i = 0; i < list->count; i++)
		if (eu_equal(base ? list->list[i].base : list->list[i].name, s))
			return list->list + i;
	return NULL;
}

static struct package *
by_record(struct packages *list, const char *s)
{
	for (size_t i = 0; i < list->count; i++)
		if (list->list[i].artifact && eu_equal(list->list[i].artifact->id, s))
			return list->list + i;
	return NULL;
}

static bool
valid_root(const char *root)
{
	if (!root || strlen(root) > 255 || !strchr(root, '/'))
		return false;
	for (const char *p = root;;) {
		size_t n = strcspn(p, "/");
		if (!n || (n == 1 && *p == '.') || (n == 2 && !strncmp(p, "..", 2)) ||
		    n != strspn(p, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxy"
				   "z0123456789_+.-"))
			return false;
		if (!p[n])
			return true;
		p += n + 1;
	}
}

bool
eu_registered(const char *db, const char *name)
{
	char *path = NULL, *line = NULL;
	size_t cap = 0;
	int fd = -1;
	bool found = false;
	if (!pkgname(name) || asprintf(&path, "%s/%s/+CONTENTS", db, name) < 0)
		return false;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	free(path);
	if (fd < 0)
		return false;
	struct stat st;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		return false;
	}
	FILE *f = fdopen(fd, "r");
	if (!f) {
		close(fd);
		return false;
	}
	while (getline(&line, &cap, f) >= 0) {
		if (strncmp(line, "@name ", 6))
			continue;
		line[strcspn(line, "\r\n")] = 0;
		found = eu_equal(line + 6, name);
		break;
	}
	free(line);
	fclose(f);
	return found;
}

static bool
catalog_sets(const struct packages *list, const struct eu_json *sets)
{
	for (const struct eu_json *set = sets->child; set; set = set->next) {
		const char *key = set->key;
		if (!isalnum((unsigned char)*key) ||
		    key[strspn(key, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwx"
				    "yz0123456789_-")] ||
		    set->type != EU_ARRAY)
			return false;
		for (const struct eu_json *v = set->child; v; v = v->next) {
			size_t i = 0;
			for (; i < list->count; i++)
				if (eu_equal(eu_string(v), list->list[i].root))
					break;
			if (i == list->count)
				return false;
			for (const struct eu_json *q = set->child; q != v; q = q->next)
				if (eu_equal(eu_string(q), eu_string(v)))
					return false;
		}
	}
	return true;
}

static int
inventory(const char *db, struct packages *list)
{
	DIR *d = opendir(db);
	struct dirent *entry;
	int ok = -1;
	if (!d)
		return errno == ENOENT ? 0 : -1;
	while ((entry = readdir(d)) != NULL) {
		char *copy, *ver;
		struct stat st;
		if (!pkgname(entry->d_name))
			continue;
		if (fstatat(dirfd(d), entry->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0)
			goto done;
		if (!S_ISDIR(st.st_mode))
			continue;
		if (!eu_registered(db, entry->d_name))
			goto done;
		eu_append(list->inventory, eu_text(entry->d_name));
		if (!(copy = strdup(entry->d_name)))
			goto done;
		ver = strrchr(copy, '-');
		*ver = 0;
		struct package *p = by_name(list, copy, true);
		free(copy);
		if (p) {
			if (p->installed || !(p->installed = strdup(entry->d_name)))
				goto done;
		}
	}
	ok = 0;
done:
	closedir(d);
	return ok;
}

/* Read the signed pkg_summary archive with a strict decompressed size bound. */
static int
summary(struct packages *list, const unsigned char *compressed, size_t len, char **out)
{
	z_stream z = {0};
	unsigned char *buf = malloc(EU_MAX_CONTENT + 1);
	if (!buf || len > UINT_MAX) {
		free(buf);
		return -1;
	}
	if (inflateInit2(&z, MAX_WBITS + 16) != Z_OK) {
		free(buf);
		return -1;
	}
	z.next_in = compressed;
	z.avail_in = (uInt)len;
	z.next_out = buf;
	z.avail_out = EU_MAX_CONTENT + 1;
	int ret = inflate(&z, Z_FINISH);
	bool ok = ret == Z_STREAM_END && !z.avail_in && z.total_out <= EU_MAX_CONTENT &&
		  eu_utf8(buf, z.total_out);
	size_t n = z.total_out;
	inflateEnd(&z);
	if (!ok) {
		free(buf);
		return -1;
	}
	buf[n] = 0;
	(void)list;
	*out = (char *)buf;
	return 0;
}

static int
check_summary(struct packages *list, char *text)
{
	char *record, *line, *name = NULL, *file = NULL;
	uint64_t filesize = 0;
	bool have_size = false, have_fields = false;
	size_t count = 0;
	bool *seen = calloc(list->count + 1, sizeof(*seen));
	int ok = -1;
	char *keys[128];
	size_t keycount = 0;
	struct eu_json *conflicts = eu_new(EU_ARRAY);
	if (!seen)
		return -1;
	for (;;) {
		line = strsep(&text, "\n");
		if (line && *line) {
			record = strchr(line, '=');
			if (!record || line == record)
				goto done;
			*record++ = 0;
			if (strspn(line, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
			    strlen(line))
				goto done;
			if (*line < 'A' || *line > 'Z')
				goto done;
			const char *repeat[] = {"DEPENDS",     "CONFLICTS",
						"REQUIRES",    "PROVIDES",
						"DESCRIPTION", "SUPERSEDES"};
			size_t r = 0;
			for (; r < sizeof(repeat) / sizeof(repeat[0]); r++)
				if (eu_equal(line, repeat[r]))
					break;
			if (r == sizeof(repeat) / sizeof(repeat[0])) {
				if (keycount == sizeof(keys) / sizeof(keys[0]))
					goto done;
				for (size_t i = 0; i < keycount; i++)
					if (eu_equal(line, keys[i]))
						goto done;
				keys[keycount++] = line;
			}
			if (eu_equal(line, "CONFLICTS")) {
				if (!*record || strlen(record) > 255)
					goto done;
				eu_append(conflicts, eu_text(record));
			}
			have_fields = true;
			if (!strcmp(line, "PKGNAME")) {
				if (name)
					goto done;
				name = record;
			}
			if (!strcmp(line, "FILE_NAME")) {
				if (file)
					goto done;
				file = record;
			}
			if (!strcmp(line, "FILE_SIZE")) {
				if (have_size || !*record ||
				    strspn(record, "0123456789") != strlen(record))
					goto done;
				char *end;
				errno = 0;
				filesize = strtoull(record, &end, 10);
				if (errno || *end)
					goto done;
				have_size = true;
			}
			continue;
		}
		if (have_fields) {
			struct package *p = by_name(list, name, false);
			const char *leaf;
			if (!p || seen[p - list->list] || !have_size || !file)
				goto done;
			leaf = strrchr(p->artifact->path, '/');
			leaf = leaf ? leaf + 1 : p->artifact->path;
			if (strcmp(leaf, file) ||
			    filesize != (uint64_t)p->artifact->size)
				goto done;
			p->conflicts = conflicts;
			conflicts = eu_new(EU_ARRAY);
			keycount = 0;
			seen[p - list->list] = true;
			count++;
			have_fields = false;
			have_size = false;
			name = NULL;
			file = NULL;
		}
		if (!line)
			break;
	}
	ok = count == list->count ? 0 : -1;
done:
	eu_json_free(conflicts);
	free(seen);
	return ok;
}

static int
select_pkg(struct packages *packages, struct package *p, size_t depth)
{
	if (p->selected == 2)
		return 0;
	if (p->selected == 1 || depth > 128)
		return -1;
	if (p->installed && !eu_equal(p->installed, p->name)) {
		char *pattern;
		if (asprintf(&pattern, "%s>%s", p->base,
			     strrchr(p->installed, '-') + 1) < 0)
			return -1;
		int newer = match(pattern, p->name);
		free(pattern);
		if (newer < 0)
			return -1;
		if (!newer)
			return depth == 0 ? 0 : -1;
	}
	p->selected = 1;
	for (const struct eu_json *d = p->artifact->deps->child; d; d = d->next) {
		struct package *q = by_record(packages, d->text);
		if (!q || select_pkg(packages, q, depth + 1) < 0)
			return -1;
	}
	if (!eu_equal(p->installed, p->name))
		packages->order[packages->updates++] = p;
	p->selected = 2;
	return 0;
}

int
eu_packages(const struct delivery *d, const struct eu_json *target, const char *db,
	    struct packages *list)
{
	const struct artifact *cat = NULL, *sum;
	const struct eu_json *o, *items, *entry;
	unsigned char *data = NULL;
	char *text = NULL, *want = NULL;
	size_t n, i = 0;
	int ok = -1;
	memset(list, 0, sizeof(*list));
	for (size_t j = 0; j < d->release.count; j++) {
		const struct artifact *a = &d->release.items[j];
		const char *leaf = strrchr(a->path, '/');
		if (eu_equal(a->kind, "package-index") &&
		    eu_equal(leaf, "/catalog.json") &&
		    eu_target_match(a->target, target)) {
			if (cat)
				goto done;
			cat = a;
		}
	}
	if (!cat || eu_payload(d, cat, NULL, &data, &n) < 0)
		goto done;
	o = list->catalog = eu_json_parse(data, n);
	free(data);
	data = NULL;
	if (!eu_fields(o, "schema repository summary packages sets") ||
	    !eu_equal(field(o, "schema"), "emberbsd.packages.v1") ||
	    !eu_slug(field(o, "repository")) || eu_get(o, "sets")->type != EU_OBJECT)
		goto done;
	if (asprintf(&want, "%s/catalog.json", field(o, "repository")) < 0 ||
	    !eu_equal(want, cat->path))
		goto done;
	free(want);
	want = NULL;
	sum = eu_artifact(&d->release, field(o, "summary"));
	if (!sum || !eu_equal(sum->kind, "package-index") ||
	    !eu_target_match(sum->target, target) ||
	    asprintf(&want, "%s/pkg_summary.gz", field(o, "repository")) < 0 ||
	    !eu_equal(want, sum->path))
		goto done;
	items = eu_get(o, "packages");
	if (items->type != EU_ARRAY)
		goto done;
	for (entry = items->child; entry; entry = entry->next)
		list->count++;
	list->list = calloc(list->count + 1, sizeof(*list->list));
	list->order = calloc(list->count + 1, sizeof(*list->order));
	if (!list->list || !list->order)
		goto done;
	for (entry = items->child; entry; entry = entry->next, i++) {
		const char *name = field(entry, "pkgname"),
			   *root = field(entry, "root");
		struct artifact *a = eu_artifact(&d->release, field(entry, "artifact"));
		if (!eu_fields(entry, "root pkgname artifact") || !valid_root(root) ||
		    !pkgname(name) || !a || !eu_target_match(target, a->target) ||
		    !eu_equal(a->kind, "target-package") || a->size <= 0)
			goto done;
		const char *leaf = strrchr(a->path, '/');
		leaf = leaf ? leaf + 1 : a->path;
		if (strlen(leaf) != strlen(name) + 4 ||
		    strncmp(leaf, name, strlen(name)) ||
		    strcmp(leaf + strlen(name), ".tgz"))
			goto done;
		char *base = strndup(name, (size_t)(strrchr(name, '-') - name));
		if (!base)
			goto done;
		bool repeated_root = false;
		for (size_t j = 0; j < i; j++)
			if (eu_equal(root, list->list[j].root))
				repeated_root = true;
		if (repeated_root || by_name(list, base, true) ||
		    by_record(list, a->id)) {
			free(base);
			goto done;
		}
		list->list[i] = (struct package){
		    .name = name, .root = root, .base = base, .artifact = a};
	}
	for (i = 0; i < list->count; i++)
		for (const struct eu_json *dep = list->list[i].artifact->deps->child;
		     dep; dep = dep->next)
			if (!by_record(list, dep->text))
				goto done;
	if (!catalog_sets(list, eu_get(o, "sets")))
		goto done;
	list->inventory = eu_new(EU_ARRAY);
	if (eu_payload(d, sum, NULL, &data, &n) < 0 ||
	    summary(list, data, n, &text) < 0 || check_summary(list, text) < 0 ||
	    inventory(db, list) < 0)
		goto done;
	for (i = 0; i < list->count; i++)
		if (list->list[i].installed && select_pkg(list, &list->list[i], 0) < 0)
			goto done;
	for (size_t a = 0; a < list->updates; a++) {
		struct package *p = list->order[a];
		for (const struct eu_json *c = p->conflicts->child; c; c = c->next) {
			for (const struct eu_json *active = list->inventory->child;
			     active; active = active->next) {
				const char *name = eu_string(active);
				if (eu_equal(p->installed, name))
					continue;
				/* Earlier pkg_add calls replace these active names. */
				for (size_t j = 0; j < a; j++)
					if (eu_equal(list->order[j]->installed, name))
						name = list->order[j]->name;
				if (match(eu_string(c), name) != 0)
					goto done;
			}
			for (size_t j = 0; j < list->updates; j++)
				if (j != a &&
				    match(eu_string(c), list->order[j]->name) != 0)
					goto done;
		}
	}
	ok = 0;
done:
	free(want);
	free(data);
	free(text);
	if (ok) {
		eu_packages_free(list);
		errno = EINVAL;
	}
	return ok;
}

void
eu_packages_free(struct packages *p)
{
	for (size_t i = 0; p->list && i < p->count; i++) {
		eu_json_free(p->list[i].conflicts);
		free(p->list[i].base);
		free(p->list[i].installed);
	}
	free(p->list);
	free(p->order);
	eu_json_free(p->catalog);
	eu_json_free(p->inventory);
	memset(p, 0, sizeof(*p));
}
