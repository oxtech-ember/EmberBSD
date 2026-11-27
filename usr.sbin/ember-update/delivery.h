/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_UPDATE_DELIVERY_H
#define EMBER_UPDATE_DELIVERY_H
#include "catalog.h"
struct delivery {
	char *base, *channel, *metadata;
	struct eu_json *pointer, *manifest;
	struct snapshot_ref ref;
	struct release release;
};
const char *eu_snapshot(struct delivery *, const char *, const char *, const char *);
void eu_snapshot_free(struct delivery *);
int eu_payload(const struct delivery *, const struct artifact *, const char *,
	       unsigned char **, size_t *);
#endif
