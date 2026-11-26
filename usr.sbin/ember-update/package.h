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
	bool manual;
};
struct packages {
	struct eu_json *catalog, *inventory;
	struct package *list, **order;
	size_t count, updates;
};
int eu_package_index(const struct delivery *, const struct eu_json *,
		     struct packages *);
int eu_inventory(const char *, struct packages *);
int eu_plan_packages(struct packages *, const char *, const char *, const char *);
const char *eu_package_action(const struct package *);
bool eu_pkgname(const char *);
bool eu_pkgroot(const char *);
bool eu_registered(const char *, const char *);
void eu_packages_free(struct packages *);
#endif
