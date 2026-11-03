/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). Target-format conversion via libc gdtoa. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../common/lint_fp.h"
#include "gdtoa_lint_host.h"
#include "gdtoa.h"

static void
setbit(lint_fp_t *v, unsigned bit)
{
	if (bit < 64)
		v->lo |= UINT64_C(1) << bit;
	else
		v->hi |= UINT64_C(1) << (bit - 64);
}

bool
lint_fp_parse(const char *text, size_t len, enum lint_fp_format fmt,
    lint_fp_t *out, unsigned *status)
{
	static const FPI formats[] = {
		{24, -149, 104, FPI_Round_near, 0},
		{53, -1074, 971, FPI_Round_near, 0},
		{64, -16445, 16320, FPI_Round_near, 0},
		{113, -16494, 16271, FPI_Round_near, 0},
	};
	uint32_t bits[4] = {0, 0, 0, 0};
	int32_t exp = 0;
	char *buf, *end;
	int kind, highest, shift, unbiased;
	unsigned i;
	lint_fp_t value = {0, 0};

	if (text == NULL || out == NULL || status == NULL ||
	    (unsigned)fmt >= sizeof(formats) / sizeof(formats[0]) ||
	    len == SIZE_MAX)
		return false;
	*status = 0;
	buf = malloc(len + 1);
	if (buf == NULL)
		return false;
	memcpy(buf, text, len);
	buf[len] = '\0';
	kind = strtodg(buf, &end, &formats[fmt], &exp, bits, (GDTOA_LOCALE_TYPE)0);
	if (end != buf + len || kind == STRTOG_NoMemory ||
	    (kind & STRTOG_Retmask) == STRTOG_NoNumber) {
		free(buf);
		*status = LINT_FP_INVALID;
		return false;
	}
	free(buf);
	if (kind & STRTOG_Inexact)
		*status |= LINT_FP_INEXACT;
	if (kind & STRTOG_Underflow)
		*status |= LINT_FP_UNDERFLOW;
	if (kind & STRTOG_Overflow)
		*status |= LINT_FP_OVERFLOW;
	switch (kind & STRTOG_Retmask) {
	case STRTOG_Zero:
		break;
	case STRTOG_Infinite:
		value.hi = UINT64_C(0x7fff000000000000);
		break;
	case STRTOG_NaN:
	case STRTOG_NaNbits:
		value.hi = UINT64_C(0x7fff800000000000);
		break;
	case STRTOG_Normal:
	case STRTOG_Denormal:
		for (highest = 127; highest >= 0; highest--)
			if (bits[highest / 32] & (UINT32_C(1) << (highest % 32)))
				break;
		if (highest < 0)
			return false;
		unbiased = exp + highest;
		if (unbiased >= -16382) {
			shift = 112 - highest;
			value.hi = (uint64_t)(unbiased + 16383) << 48;
		} else
			shift = exp + 16494;
		for (i = 0; i <= (unsigned)highest; i++) {
			int bit = (int)i + shift;
			if (bit >= 0 && bit < 112 &&
			    (bits[i / 32] & (UINT32_C(1) << (i % 32))))
				setbit(&value, (unsigned)bit);
		}
		break;
	default:
		*status = LINT_FP_INVALID;
		return false;
	}
	if (kind & STRTOG_Neg)
		value.hi |= UINT64_C(0x8000000000000000);
	*out = value;
	return true;
}
