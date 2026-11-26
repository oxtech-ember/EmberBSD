/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_UPDATE_LOCAL_H
#define EMBER_UPDATE_LOCAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "json.h"
struct eu_json *eu_member(struct eu_json *, const char *);
struct eu_json *eu_new(enum eu_type);
struct eu_json *eu_text(const char *);
struct eu_json *eu_int(int64_t);
struct eu_json *eu_bool(bool);
int eu_put(struct eu_json *, const char *, struct eu_json *);
int eu_append(struct eu_json *, struct eu_json *);
int eu_emit(FILE *, const struct eu_json *);
int eu_save(const char *, const struct eu_json *, unsigned);
int eu_run(const char *const *, char **, size_t);
#endif
