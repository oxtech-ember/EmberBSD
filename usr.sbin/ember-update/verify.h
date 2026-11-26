/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_UPDATE_VERIFY_H
#define EMBER_UPDATE_VERIFY_H

#include <stddef.h>
#include "json.h"

#define EU_MAX_CMS (16U * 1024 * 1024)
#define EU_MAX_CONTENT (15U * 1024 * 1024)

struct eu_json *eu_cms_json(const void *, size_t, const char *);
int eu_digest(const void *, size_t, char[65]);

#endif
