/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_UPDATE_JSON_H
#define EMBER_UPDATE_JSON_H

#include <stddef.h>
#include <stdint.h>

enum eu_type { EU_NULL, EU_BOOL, EU_INTEGER, EU_REAL, EU_STRING, EU_ARRAY, EU_OBJECT };

struct eu_json {
	enum eu_type type;
	char *key;
	char *text;
	int64_t integer;
	struct eu_json *child;
	struct eu_json *next;
};

int eu_utf8(const unsigned char *, size_t);
struct eu_json *eu_json_parse(const void *, size_t);
struct eu_json *eu_json_file(const char *, size_t);
const struct eu_json *eu_get(const struct eu_json *, const char *);
const char *eu_string(const struct eu_json *);
void eu_json_free(struct eu_json *);

#endif
