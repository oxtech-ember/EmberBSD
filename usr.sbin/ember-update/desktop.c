/* SPDX-License-Identifier: BSD-2-Clause */
/* Bind launch entries to the installed package's packing list. */
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "control.h"
#include "store.h"

static char *
trim(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	size_t n = strlen(s);
	while (n && isspace((unsigned char)s[n - 1]))
		s[--n] = 0;
	return s;
}

static char *
regular_inside(const char *root, const char *path)
{
	char realroot[PATH_MAX], realfile[PATH_MAX];
	struct stat st;
	if (!realpath(root, realroot) || !realpath(path, realfile))
		return NULL;
	size_t n = strlen(realroot);
	if (strncmp(realroot, realfile, n) || realfile[n] != '/' ||
	    stat(realfile, &st) < 0 || !S_ISREG(st.st_mode))
		return NULL;
	return strdup(realfile);
}

/* Desktop strings escape space, newlines, tabs, return, and backslash. */
static void
unescape(char *s)
{
	char *d = s;
	while (*s) {
		char c = *s++;
		if (c == '\\' && *s) {
			c = *s++;
			switch (c) {
			case 's':
				c = ' ';
				break;
			case 'n':
				c = '\n';
				break;
			case 't':
				c = '\t';
				break;
			case 'r':
				c = '\r';
				break;
			}
		}
		*d++ = c;
	}
	*d = 0;
}

static struct eu_json *
fields(const char *path)
{
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
	struct stat st;
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
	    st.st_size > 256 * 1024) {
		close(fd);
		return NULL;
	}
	size_t n = (size_t)st.st_size;
	char *raw = malloc(n + 1);
	if (!raw) {
		close(fd);
		return NULL;
	}
	size_t off = 0;
	while (off < n) {
		ssize_t r = read(fd, raw + off, n - off);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		off += (size_t)r;
	}
	char extra;
	bool complete = off == n && read(fd, &extra, 1) == 0;
	close(fd);
	if (!complete || !eu_utf8((const unsigned char *)raw, n)) {
		free(raw);
		return NULL;
	}
	raw[n] = 0;
	struct eu_json *obj = eu_new(EU_OBJECT);
	bool entry = false;
	size_t retained = 0;
	char *line, *pos = raw;
	while ((line = strsep(&pos, "\n"))) {
		line = trim(line);
		if (!*line || *line == '#')
			continue;
		size_t len = strlen(line);
		if (*line == '[' && line[len - 1] == ']') {
			entry = !strcmp(line, "[Desktop Entry]");
			continue;
		}
		char *eq = strchr(line, '=');
		if (!entry || !eq)
			continue;
		*eq++ = 0;
		char *key = trim(line), *value = trim(eq);
		if (strlen(key) > 128)
			continue;
		const char *keys[] = {
		    "Name",	   "Exec",	"TryExec",    "Type",	    "Comment",
		    "GenericName", "Icon",	"Categories", "OnlyShowIn", "NotShowIn",
		    "Hidden",	   "NoDisplay", "Terminal"};
		size_t i = 0;
		for (; i < sizeof(keys) / sizeof(keys[0]); i++)
			if (eu_equal(key, keys[i]))
				break;
		if (i == sizeof(keys) / sizeof(keys[0]) && strncmp(key, "Name[", 5) &&
		    strncmp(key, "Comment[", 8))
			continue;
		if (++retained > 512) {
			eu_json_free(obj);
			free(raw);
			return NULL;
		}
		unescape(value);
		eu_put(obj, key, eu_text(value));
	}
	free(raw);
	return obj;
}

static bool
boolean(const struct eu_json *f, const char *key)
{
	const char *s = text_field(f, key);
	return s && !strcasecmp(s, "true");
}

static bool
has_item(const struct eu_json *list, const char *s)
{
	for (const struct eu_json *v = list->child; v; v = v->next)
		if (eu_equal(eu_string(v), s))
			return true;
	return false;
}

static struct eu_json *
strings(const char *s)
{
	struct eu_json *items = eu_new(EU_ARRAY);
	if (!s)
		return items;
	char *copy = strdup(s), *pos = copy, *word;
	if (!copy)
		abort();
	while ((word = strsep(&pos, ";"))) {
		word = trim(word);
		if (*word && !strchr(word, '\n') && !has_item(items, word))
			eu_append(items, eu_text(word));
	}
	free(copy);
	return items;
}

static bool
visible(const struct eu_json *f)
{
	struct eu_json *only = strings(text_field(f, "OnlyShowIn")),
		       *hidden = strings(text_field(f, "NotShowIn"));
	bool ok = (!only->child || has_item(only, "KDE") || has_item(only, "Plasma")) &&
		  !has_item(hidden, "KDE") && !has_item(hidden, "Plasma");
	eu_json_free(only);
	eu_json_free(hidden);
	return ok;
}

/* Produce an argument vector. The desktop file never runs through a shell. */
static struct eu_json *
command(const char *s, const char *prefix)
{
	if (!s || !*s || strchr(s, '\n') || strchr(s, '\r'))
		return NULL;
	struct eu_json *args = eu_new(EU_ARRAY);
	char *buf = malloc(strlen(s) + 1), *path = NULL, *resolved = NULL;
	if (!buf)
		abort();
	size_t used = 0;
	char quote = 0;
	for (;;) {
		char c = *s;
		if (!c && quote)
			goto bad;
		if (!c || (!quote && (c == ' ' || c == '\t'))) {
			if (used) {
				buf[used] = 0;
				if (!strcmp(buf, "%%"))
					eu_append(args, eu_text("%"));
				else if (!(used == 2 && buf[0] == '%' &&
					   strchr("fFuUdDnNickvm", buf[1])))
					eu_append(args, eu_text(buf));
				used = 0;
			}
			if (!c)
				break;
		} else if (quote && c == quote)
			quote = 0;
		else if (c == '\\' && s[1])
			buf[used++] = *++s;
		else if (!quote && (c == '\'' || c == '"'))
			quote = c;
		else
			buf[used++] = c;
		s++;
	}
	if (!args->child)
		goto bad;
	const char *prog = eu_string(args->child);
	if (*prog == '/')
		path = strdup(prog);
	else if (asprintf(&path, "%s/bin/%s", prefix, prog) < 0)
		goto bad;
	if (!path || !(resolved = regular_inside(prefix, path)) ||
	    access(resolved, X_OK) < 0)
		goto bad;
	free(args->child->text);
	args->child->text = resolved;
	free(path);
	free(buf);
	return args;
bad:
	free(resolved);
	free(path);
	free(buf);
	eu_json_free(args);
	return NULL;
}

static struct eu_json *
icon(const char *s, const char *prefix)
{
	char *f = NULL;
	struct eu_json *result;
	if (s && *s && strlen(s) <= 512 && !strpbrk(s, "\r\n")) {
		if (!strchr(s, '/') && !strchr(s, '\\') && !strstr(s, ".."))
			return eu_text(s);
		if (s[0] == '/')
			f = regular_inside(prefix, s);
	}
	result = eu_text(f);
	free(f);
	return result;
}

static struct eu_json *
entry(const char *file, const char *relative, const char *pkg, const char *prefix)
{
	struct eu_json *f = fields(file), *output = NULL, *argv = NULL, *test = NULL;
	const char *name = text_field(f, "Name");
	if (!eu_equal(text_field(f, "Type"), "Application") || !name || !*name ||
	    strchr(name, '\n') || boolean(f, "NoDisplay") || boolean(f, "Hidden") ||
	    boolean(f, "Terminal") || !visible(f))
		goto done;
	argv = command(text_field(f, "Exec"), prefix);
	if (!argv || (eu_get(f, "TryExec") &&
		      !(test = command(text_field(f, "TryExec"), prefix))))
		goto done;
	output = eu_new(EU_OBJECT);
	const char *leaf = strrchr(relative, '/') + 1;
	char *id = strndup(leaf, strlen(leaf) - strlen(".desktop"));
	if (!id)
		abort();
	eu_put(output, "id", eu_text(id));
	free(id);
	eu_put(output, "name", eu_text(name));
	struct eu_json *names = eu_new(EU_OBJECT);
	eu_put(names, "C", eu_text(name));
	bool localized_comment = false;
	for (struct eu_json *p = f->child; p; p = p->next) {
		size_t n = strlen(p->key);
		if (n > 9 && !strncmp(p->key, "Comment[", 8) && p->key[n - 1] == ']' &&
		    *eu_string(p))
			localized_comment = true;
		if (n > 6 && !strncmp(p->key, "Name[", 5) && p->key[n - 1] == ']' &&
		    *eu_string(p)) {
			char *locale = strndup(p->key + 5, n - 6);
			if (!locale)
				abort();
			eu_put(names, locale, eu_text(p->text));
			free(locale);
		}
	}
	eu_put(output, "names", names);
	const char *comment = text_field(f, "Comment");
	if ((!comment || !*comment) && !localized_comment)
		comment = text_field(f, "GenericName");
	eu_put(output, "comment", eu_text(comment ? comment : ""));
	eu_put(output, "icon", icon(text_field(f, "Icon"), prefix));
	eu_put(output, "exec", argv);
	argv = NULL;
	eu_put(output, "categories", strings(text_field(f, "Categories")));
	eu_put(output, "file", eu_text(relative));
	eu_put(output, "source", eu_text("installed-pkgdb"));
	eu_put(output, "pkgname", eu_text(pkg));
done:
	eu_json_free(f);
	eu_json_free(argv);
	eu_json_free(test);
	return output;
}

struct eu_json *
desktop_entries(const char *prefix, const char *db, const char *pkg)
{
	struct eu_json *out = eu_new(EU_ARRAY);
	char path[PATH_MAX], apps[PATH_MAX], cwd[PATH_MAX] = "";
	if (!eu_pkgname(pkg) ||
	    snprintf(path, sizeof(path), "%s/%s/+CONTENTS", db, pkg) >=
		(int)sizeof(path) ||
	    snprintf(apps, sizeof(apps), "%s/share/applications", prefix) >=
		(int)sizeof(apps))
		return out;
	char *safe = regular_inside(db, path);
	if (!safe)
		return out;
	FILE *f = fopen(safe, "r");
	free(safe);
	if (!f)
		return out;
	char *line = NULL;
	size_t capacity = 0;
	bool ignore = false, owner = false;
	while (getline(&line, &capacity, f) >= 0) {
		char *s = trim(line);
		if (ignore) {
			ignore = false;
			continue;
		}
		if (!strcmp(s, "@ignore")) {
			ignore = true;
			continue;
		}
		if (!strncmp(s, "@name ", 6)) {
			owner = eu_equal(s + 6, pkg);
			if (!owner)
				break;
			continue;
		}
		if (!strncmp(s, "@cwd ", 5)) {
			const char *p = trim(s + 5);
			if (strlen(p) >= sizeof(cwd))
				cwd[0] = 0;
			else
				strcpy(cwd, p);
			continue;
		}
		size_t n = strlen(s);
		if (!*cwd || *s == '/' || *s == '@' || n < 9 ||
		    strcmp(s + n - 8, ".desktop"))
			continue;
		if (snprintf(path, sizeof(path), "%s/%s", cwd, s) >=
			(int)sizeof(path) ||
		    strncmp(path, apps, strlen(apps)) || path[strlen(apps)] != '/')
			continue;
		const char *leaf = path + strlen(apps) + 1;
		if (!*leaf || *leaf == '.' || strchr(leaf, '/') ||
		    !eu_utf8((const unsigned char *)leaf, strlen(leaf)))
			continue;
		safe = regular_inside(prefix, path);
		if (!safe)
			continue;
		struct eu_json *app =
		    entry(safe, path + strlen(prefix) + 1, pkg, prefix);
		free(safe);
		if (!app)
			continue;
		struct eu_json **at = &out->child;
		while (*at && strcmp(text_field(*at, "id"), text_field(app, "id")) < 0)
			at = &(*at)->next;
		if (*at && eu_equal(text_field(*at, "id"), text_field(app, "id")))
			eu_json_free(app);
		else {
			app->next = *at;
			*at = app;
		}
	}
	if (!owner || ferror(f)) {
		eu_json_free(out);
		out = eu_new(EU_ARRAY);
	}
	free(line);
	fclose(f);
	return out;
}
