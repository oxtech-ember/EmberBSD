/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_UPDATE_CATALOG_H
#define EMBER_UPDATE_CATALOG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "json.h"

struct artifact {
	const char *id, *path, *sha, *kind;
	int64_t size, unpacked;
	const struct eu_json *object, *target, *host, *deps;
	unsigned mark;
};
struct release {
	const char *id;
	struct artifact *items;
	size_t count;
};
struct snapshot_ref {
	const char *id, *path, *sha;
	int64_t size, issued, expires;
};
enum { OS_BOOT, OS_BASE, OS_ETC, OS_COMP, OS_ROLES };
extern const char *const os_role[OS_ROLES];
struct os_update {
	const char *commit;
	int64_t generation;
	const struct artifact *member[OS_ROLES];
};

bool eu_fields(const struct eu_json *, const char *);
bool eu_equal(const char *, const char *);
bool eu_slug(const char *);
bool eu_hash(const char *, size_t);
bool eu_target_match(const struct eu_json *, const struct eu_json *);
bool eu_channel(const struct eu_json *, const char *, time_t, struct snapshot_ref *);
bool eu_release(const struct eu_json *, const struct snapshot_ref *, struct release *);
struct artifact *eu_artifact(const struct release *, const char *);
bool eu_os_update(const struct eu_json *, const struct artifact *,
		  const struct release *, const char *, const struct eu_json *,
		  struct os_update *);
#endif
