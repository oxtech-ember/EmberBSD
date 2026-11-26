/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_UPDATE_PACKAGE_H
#define EMBER_UPDATE_PACKAGE_H
#include "delivery.h"
struct package {
	const char *name, *root;
	char *base;
	const struct artifact *artifact;
	char *installed;
	struct eu_json *conflicts;
	unsigned selected;
};
struct packages {
	struct eu_json *catalog, *inventory;
	struct package *list, **order;
	size_t count, updates;
};
int eu_packages(const struct delivery *, const struct eu_json *, const char *,
		struct packages *);
bool eu_registered(const char *, const char *);
void eu_packages_free(struct packages *);
#endif
