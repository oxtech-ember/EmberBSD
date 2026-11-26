/* SPDX-License-Identifier: BSD-2-Clause */
/* jsmn locates tokens. Check grammar, decoded keys, UTF-8 and bounded numbers. */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <inttypes.h>
#include <jsmn.h>

#include "json.h"

#define JSON_DEPTH 64
#define JSON_NODES 262144
#define JSON_LIMIT (16U * 1024 * 1024)

struct parser {
	const unsigned char *s;
	size_t n, pos;
	jsmntok_t *tokens;
	int token, count;
};

/* Reject invalid encodings, surrogate code points, and embedded NULs. */
static int
utf8(const unsigned char *s, size_t n)
{
	size_t i;
	uint32_t c, minimum;
	unsigned int extra;

	for (i = 0; i < n;) {
		c = s[i++];
		if (c == 0)
			return 0;
		if (c < 128)
			continue;
		if (c >= 0xc2 && c <= 0xdf) {
			c &= 31;
			extra = 1;
			minimum = 128;
		} else if (c >= 0xe0 && c <= 0xef) {
			c &= 15;
			extra = 2;
			minimum = 2048;
		} else if (c >= 0xf0 && c <= 0xf4) {
			c &= 7;
			extra = 3;
			minimum = 65536;
		} else
			return 0;
		if (n - i < extra)
			return 0;
		while (extra-- != 0) {
			if ((s[i] & 0xc0) != 0x80)
				return 0;
			c = (c << 6) | (s[i++] & 63);
		}
		if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
			return 0;
	}
	return 1;
}

void
eu_json_free(struct eu_json *node)
{
	struct eu_json *next;

	while (node != NULL) {
		next = node->next;
		eu_json_free(node->child);
		free(node->key);
		free(node->text);
		free(node);
		node = next;
	}
}

const struct eu_json *
eu_get(const struct eu_json *node, const char *key)
{
	const struct eu_json *child;

	if (node == NULL || node->type != EU_OBJECT)
		return NULL;
	for (child = node->child; child != NULL; child = child->next)
		if (strcmp(child->key, key) == 0)
			return child;
	return NULL;
}

const char *
eu_string(const struct eu_json *node)
{
	return node != NULL && node->type == EU_STRING ? node->text : NULL;
}

static void
space(struct parser *p)
{
	while (p->pos < p->n && strchr(" \n\r\t", p->s[p->pos]) != NULL)
		p->pos++;
}

static int
take(struct parser *p, unsigned char c)
{
	space(p);
	if (p->pos == p->n || p->s[p->pos] != c)
		return 0;
	p->pos++;
	return 1;
}

static int
hex4(const unsigned char *s, uint32_t *v)
{
	unsigned i, c;
	*v = 0;
	for (i = 0; i < 4; i++) {
		c = s[i];
		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'A' && c <= 'F')
			c -= 'A' - 10;
		else if (c >= 'a' && c <= 'f')
			c -= 'a' - 10;
		else
			return 0;
		*v = (*v << 4) | c;
	}
	return 1;
}

static char *
string(struct parser *p)
{
	const unsigned char *s = p->s;
	jsmntok_t *t;
	size_t i;
	uint32_t c, low;
	char *buf, *out;
	static const char escape[] = "\"\\/bfnrt";

	if (p->token >= p->count || !take(p, '"'))
		return NULL;
	t = &p->tokens[p->token++];
	if (t->type != JSMN_STRING || (size_t)t->start != p->pos ||
	    (size_t)t->end >= p->n)
		return NULL;
	buf = malloc((size_t)(t->end - t->start) + 1);
	if (buf == NULL)
		return NULL;
	out = buf;
	for (i = p->pos; i < (size_t)t->end; i++) {
		c = s[i];
		if (c < 0x20)
			goto bad;
		if (c != '\\') {
			*out++ = (char)c;
			continue;
		}
		if (++i >= (size_t)t->end)
			goto bad;
		if (s[i] != 'u') {
			const char *e = strchr(escape, s[i]);
			if (e == NULL)
				goto bad;
			*out++ = "\"\\/\b\f\n\r\t"[e - escape];
			continue;
		}
		if ((size_t)t->end - i < 5 || !hex4(s + i + 1, &c))
			goto bad;
		i += 4;
		if (c >= 0xd800 && c <= 0xdbff) {
			if ((size_t)t->end - i < 7 || s[i + 1] != '\\' ||
			    s[i + 2] != 'u' || !hex4(s + i + 3, &low) || low < 0xdc00 ||
			    low > 0xdfff)
				goto bad;
			c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00;
			i += 6;
		}
		if (c == 0 || (c >= 0xdc00 && c <= 0xdfff))
			goto bad;
		if (c < 0x80)
			*out++ = c;
		else {
			if (c >= 0x10000) {
				*out++ = 0xf0 | (c >> 18);
				*out++ = 0x80 | ((c >> 12) & 63);
			} else if (c >= 0x800)
				*out++ = 0xe0 | (c >> 12);
			else
				*out++ = 0xc0 | (c >> 6);
			if (c >= 0x800)
				*out++ = 0x80 | ((c >> 6) & 63);
			*out++ = 0x80 | (c & 63);
		}
	}
	*out = 0;
	p->pos = (size_t)t->end + 1;
	return buf;
bad:
	free(buf);
	return NULL;
}

static struct eu_json *value(struct parser *, unsigned int);

static int
container(struct parser *p, struct eu_json *node, unsigned depth)
{
	struct eu_json **tail = &node->child, *child;
	int object = node->type == EU_OBJECT;
	unsigned char close = object ? '}' : ']';
	unsigned fields = 0;
	char *key;

	if (take(p, close))
		return 1;
	do {
		key = NULL;
		if (object) {
			if (++fields > 128 || (key = string(p)) == NULL)
				return 0;
			if (!take(p, ':') || eu_get(node, key) != NULL) {
				free(key);
				return 0;
			}
		}
		if ((child = value(p, depth + 1)) == NULL) {
			free(key);
			return 0;
		}
		child->key = key;
		*tail = child;
		tail = &child->next;
		if (take(p, close))
			return 1;
	} while (take(p, ','));
	return 0;
}

static int
primitive(struct eu_json *node, const unsigned char *s, size_t n)
{
	size_t i = 0;
	char number[32], *end;

	if (n == 4 && memcmp(s, "null", 4) == 0)
		return 1;
	if ((n == 4 && memcmp(s, "true", 4) == 0) ||
	    (n == 5 && memcmp(s, "false", 5) == 0)) {
		node->type = EU_BOOL;
		node->integer = s[0] == 't';
		return 1;
	}
	if (n == 0 || n >= sizeof(number))
		return 0;
	if (s[i] == '-')
		i++;
	if (i == n || (s[i] == '0' && i + 1 != n))
		return 0;
	for (; i < n; i++)
		if (s[i] < '0' || s[i] > '9')
			return 0;
	memcpy(number, s, n);
	number[n] = 0;
	errno = 0;
	node->integer = strtoimax(number, &end, 10);
	node->type = EU_INTEGER;
	return errno != ERANGE && *end == 0;
}

static struct eu_json *
value(struct parser *p, unsigned depth)
{
	struct eu_json *node;
	jsmntok_t t;

	space(p);
	if (depth >= JSON_DEPTH || p->token >= p->count ||
	    (node = calloc(1, sizeof(*node))) == NULL)
		return NULL;
	t = p->tokens[p->token];
	if (t.type == JSMN_STRING) {
		node->type = EU_STRING;
		if ((node->text = string(p)) != NULL)
			return node;
	} else if ((size_t)t.start == p->pos && (size_t)t.end <= p->n) {
		p->token++;
		if (t.type == JSMN_PRIMITIVE) {
			if (primitive(node, p->s + p->pos, (size_t)t.end - p->pos)) {
				p->pos = (size_t)t.end;
				return node;
			}
		} else if (t.type == JSMN_ARRAY || t.type == JSMN_OBJECT) {
			node->type = t.type == JSMN_ARRAY ? EU_ARRAY : EU_OBJECT;
			p->pos++;
			if (container(p, node, depth) && p->pos == (size_t)t.end)
				return node;
		}
	}
	eu_json_free(node);
	return NULL;
}

struct eu_json *
eu_json_parse(const void *data, size_t len)
{
	struct parser p = {.s = data, .n = len};
	jsmn_parser lexer;
	struct eu_json *root = NULL;

	if (data == NULL || len == 0 || len > JSON_LIMIT || !utf8(data, len))
		goto done;
	jsmn_init(&lexer);
	p.count = jsmn_parse(&lexer, data, len, NULL, 0);
	if (p.count <= 0 || p.count > JSON_NODES ||
	    (p.tokens = calloc((size_t)p.count, sizeof(*p.tokens))) == NULL)
		goto done;
	jsmn_init(&lexer);
	if (jsmn_parse(&lexer, data, len, p.tokens, (unsigned)p.count) != p.count)
		goto done;
	root = value(&p, 0);
	space(&p);
	if (p.pos != p.n || p.token != p.count) {
		eu_json_free(root);
		root = NULL;
	}
done:
	free(p.tokens);
	if (root == NULL)
		errno = EINVAL;
	return root;
}

struct eu_json *
eu_json_file(const char *path, size_t max)
{
	struct stat st;
	struct eu_json *result = NULL;
	char *buf = NULL, extra;
	ssize_t r;
	size_t n = 0;
	int fd, saved;

	if ((fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)) == -1)
		return NULL;
	if (fstat(fd, &st) == -1)
		goto done;
	if (!S_ISREG(st.st_mode) || st.st_size <= 0 || (uintmax_t)st.st_size > max ||
	    (uintmax_t)st.st_size > JSON_LIMIT) {
		errno = EINVAL;
		goto done;
	}
	if ((buf = malloc((size_t)st.st_size)) == NULL)
		goto done;
	while (n < (size_t)st.st_size) {
		r = read(fd, buf + n, (size_t)st.st_size - n);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0) {
			if (r == 0)
				errno = EIO;
			goto done;
		}
		n += (size_t)r;
	}
	if (read(fd, &extra, 1) != 0) {
		errno = EIO;
		goto done;
	}
	result = eu_json_parse(buf, n);
done:
	saved = errno;
	free(buf);
	close(fd);
	errno = saved;
	return result;
}
