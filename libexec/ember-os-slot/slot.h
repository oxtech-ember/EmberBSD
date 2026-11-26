/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_SLOT_H
#define EMBER_SLOT_H

#include <sys/param.h>
#include "json.h"

struct slot {
	struct eu_json *profile;
	const char *label[2], *esp, *markers;
	char device[2][32];
	int running, efi, trial, good, back, next, prepared, mounted[2];
	char digest[65];
};

struct eu_json *slot_profile(void);
void slot_layout(struct slot *);
void slot_begin_prepare(const struct slot *, int);
void slot_inactive(struct slot *, int);
void slot_lock(void);
int slot_id(const char *);
const char *field(const struct eu_json *, const char *);
void slot_marker(const struct slot *, const char *, int);
void slot_receipt(const struct slot *, int, const char *);
void slot_path(char *, size_t, const char *, const char *);
int slot_hash(const char *);
int efi_trial(const char *);
int efi_reset_capable(const char *);
#endif
