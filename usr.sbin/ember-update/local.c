/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "local.h"

struct eu_json *
eu_member(struct eu_json *obj, const char *key)
{
	if (!obj || obj->type != EU_OBJECT)
		return NULL;
	for (struct eu_json *q = obj->child; q; q = q->next)
		if (!strcmp(q->key, key))
			return q;
	return NULL;
}

struct eu_json *
eu_new(enum eu_type type)
{
	struct eu_json *p = calloc(1, sizeof(*p));
	if (!p)
		abort();
	p->type = type;
	return p;
}

struct eu_json *
eu_text(const char *s)
{
	if (!s)
		return eu_new(EU_NULL);
	struct eu_json *p = eu_new(EU_STRING);
	if (!(p->text = strdup(s)))
		abort();
	return p;
}

struct eu_json *
eu_int(int64_t i)
{
	struct eu_json *p = eu_new(EU_INTEGER);
	p->integer = i;
	return p;
}

struct eu_json *
eu_bool(bool v)
{
	struct eu_json *p = eu_new(EU_BOOL);
	p->integer = v;
	return p;
}

int
eu_append(struct eu_json *array, struct eu_json *value)
{
	if (!array || array->type != EU_ARRAY || !value)
		return -1;
	struct eu_json **node = &array->child;
	while (*node)
		node = &(*node)->next;
	*node = value;
	return 0;
}

int
eu_put(struct eu_json *obj, const char *key, struct eu_json *value)
{
	if (!obj || obj->type != EU_OBJECT || !value)
		return -1;
	struct eu_json **node = &obj->child;
	while (*node && strcmp((*node)->key, key) != 0)
		node = &(*node)->next;
	if (*node) {
		struct eu_json *old = *node;
		value->next = old->next;
		old->next = NULL;
		eu_json_free(old);
	}
	if (!(value->key = strdup(key)))
		abort();
	*node = value;
	return 0;
}

static int
quote(FILE *f, const char *s)
{
	if (fputc('"', f) == EOF)
		return -1;
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') {
			if (fputc('\\', f) == EOF)
				return -1;
		}
		if (c < 32) {
			if (fprintf(f, "\\u%04x", c) < 0)
				return -1;
		} else if (fputc(c, f) == EOF)
			return -1;
	}
	return fputc('"', f) == EOF ? -1 : 0;
}

int
eu_emit(FILE *f, const struct eu_json *p)
{
	if (!p)
		return -1;
	switch (p->type) {
	case EU_NULL:
		return fputs("null", f) < 0 ? -1 : 0;
	case EU_BOOL:
		return fputs(p->integer ? "true" : "false", f) < 0 ? -1 : 0;
	case EU_INTEGER:
		return fprintf(f, "%" PRId64, p->integer) < 0 ? -1 : 0;
	case EU_STRING:
		return quote(f, p->text);
	case EU_ARRAY:
	case EU_OBJECT: {
		bool obj = p->type == EU_OBJECT;
		if (fputc(obj ? '{' : '[', f) == EOF)
			return -1;
		for (const struct eu_json *q = p->child; q; q = q->next) {
			if (q != p->child && fputc(',', f) == EOF)
				return -1;
			if (obj && (quote(f, q->key) < 0 || fputc(':', f) == EOF))
				return -1;
			if (eu_emit(f, q) < 0)
				return -1;
		}
		return fputc(obj ? '}' : ']', f) == EOF ? -1 : 0;
	}
	default:
		return -1;
	}
}

int
eu_save(const char *path, const struct eu_json *doc, unsigned mode)
{
	char *copy = strdup(path), *tmp = NULL, *sep;
	int parent = -1, fd = -1, ok = -1, saved;
	FILE *out = NULL;
	if (!copy || !(sep = strrchr(copy, '/')))
		goto done;
	*sep = 0;
	if ((parent = open(copy, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) <
		0 ||
	    asprintf(&tmp, "%s/.update.XXXXXX", copy) < 0 || (fd = mkstemp(tmp)) < 0 ||
	    fchmod(fd, mode) < 0 || !(out = fdopen(fd, "w")))
		goto done;
	if (eu_emit(out, doc) < 0 || fputc('\n', out) == EOF || fflush(out) < 0 ||
	    fsync(fd) < 0)
		goto done;
	if (fclose(out) < 0) {
		out = NULL;
		fd = -1;
		goto done;
	}
	out = NULL;
	fd = -1;
	if (rename(tmp, path) < 0 || fsync(parent) < 0)
		goto done;
	ok = 0;
done:
	saved = errno;
	if (out)
		fclose(out);
	else if (fd >= 0)
		close(fd);
	if (tmp)
		unlink(tmp);
	if (parent >= 0)
		close(parent);
	free(tmp);
	free(copy);
	errno = saved;
	return ok;
}

int
eu_run(const char *const argv[], char **output, size_t limit)
{
	int p[2] = {-1, -1}, st;
	pid_t child;
	size_t off = 0;
	char *buf = NULL;
	ssize_t n;
	if (output) {
		*output = NULL;
		if (pipe(p) < 0)
			return -1;
		if (!(buf = malloc(limit + 1))) {
			close(p[0]);
			close(p[1]);
			return -1;
		}
	}
	child = fork();
	if (child == 0) {
		if (output) {
			close(p[0]);
			if (dup2(p[1], STDOUT_FILENO) < 0)
				_exit(126);
			close(p[1]);
		}
		execv(argv[0], (char *const *)(uintptr_t)argv);
		_exit(127);
	}
	if (output)
		close(p[1]);
	if (child < 0) {
		if (output)
			close(p[0]);
		free(buf);
		return -1;
	}
	if (output) {
		for (;;) {
			n = read(p[0], buf + off, limit + 1 - off);
			if (n < 0 && errno == EINTR)
				continue;
			if (n <= 0)
				break;
			off += (size_t)n;
			if (off > limit) {
				kill(child, SIGTERM);
				break;
			}
		}
		close(p[0]);
	}
	while (waitpid(child, &st, 0) < 0)
		if (errno != EINTR) {
			free(buf);
			return -1;
		}
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0 ||
	    (output && (off > limit || n < 0))) {
		free(buf);
		errno = EIO;
		return -1;
	}
	if (output) {
		buf[off] = 0;
		*output = buf;
	}
	return 0;
}
