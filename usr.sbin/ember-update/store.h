/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_UPDATE_STORE_H
#define EMBER_UPDATE_STORE_H
#include "package.h"
void store_cache(const struct delivery *, const struct eu_json *,
		 const struct packages *);
int store_catalog(void);
struct eu_json *store_rows(const struct eu_json *, const char *, const char *);
struct eu_json *desktop_entries(const char *, const char *, const char *);
#endif
