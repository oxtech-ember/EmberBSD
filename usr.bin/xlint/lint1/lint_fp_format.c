/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). C-locale %g presentation using libc gdtoa. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../common/lint_fp.h"
#include "gdtoa_lint_host.h"
#include "gdtoa.h"

static bool
put(char *buf, size_t cap, size_t *pos, char ch)
{
	if (*pos + 1 >= cap)
		return false;
	buf[(*pos)++] = ch;
	return true;
}

bool
lint_fp_format(lint_fp_t value, unsigned digits, char *buf, size_t cap)
{
	static const FPI fpi = {113, -16494, 16271, FPI_Round_near, 0};
	uint32_t bits[4];
	uint64_t frac = value.hi & UINT64_C(0x0000ffffffffffff);
	unsigned exponent = (unsigned)((value.hi >> 48) & 0x7fff);
	bool sign = (value.hi >> 63) != 0, ok = true;
	size_t pos = 0, n;
	char *s, *end;
	int kind, decpt, be;

	if (buf == NULL || cap == 0 || digits == 0 || digits > 36)
		return false;
	if (sign && !put(buf, cap, &pos, '-'))
		return false;
	if (exponent == 0x7fff) {
		const char *special = (frac || value.lo) ? "nan" : "inf";
		if (strlen(special) >= cap - pos)
			return false;
		strcpy(buf + pos, special);
		return true;
	}
	if (exponent == 0 && frac == 0 && value.lo == 0) {
		if (!put(buf, cap, &pos, '0'))
			return false;
		buf[pos] = '\0';
		return true;
	}
	bits[0] = (uint32_t)value.lo;
	bits[1] = (uint32_t)(value.lo >> 32);
	bits[2] = (uint32_t)frac;
	bits[3] = (uint32_t)(frac >> 32);
	if (exponent != 0) {
		bits[3] |= 0x10000;
		kind = STRTOG_Normal;
		be = (int)exponent - 16383 - 112;
	} else {
		kind = STRTOG_Denormal;
		be = -16494;
	}
	s = gdtoa(&fpi, be, bits, &kind, 2, (int)digits, &decpt, &end);
	if (s == NULL)
		return false;
	n = (size_t)(end - s);
	if (decpt <= -4 || decpt > (int)digits) {
		ok = put(buf, cap, &pos, s[0]);
		if (n > 1) {
			ok = ok && put(buf, cap, &pos, '.');
			for (size_t i = 1; ok && i < n; i++)
				ok = put(buf, cap, &pos, s[i]);
		}
		if (ok) {
			int written = snprintf(buf + pos, cap - pos, "e%+03d", decpt - 1);
			ok = written >= 0 && (size_t)written < cap - pos;
			if (ok)
				pos += (size_t)written;
		}
	} else if (decpt <= 0) {
		ok = put(buf, cap, &pos, '0') && put(buf, cap, &pos, '.');
		for (int i = 0; ok && i < -decpt; i++)
			ok = put(buf, cap, &pos, '0');
		for (size_t i = 0; ok && i < n; i++)
			ok = put(buf, cap, &pos, s[i]);
	} else {
		for (int i = 0; ok && i < decpt; i++)
			ok = put(buf, cap, &pos, (size_t)i < n ? s[i] : '0');
		if (ok && (size_t)decpt < n) {
			ok = put(buf, cap, &pos, '.');
			for (size_t i = (size_t)decpt; ok && i < n; i++)
				ok = put(buf, cap, &pos, s[i]);
		}
	}
	freedtoa(s);
	if (!ok)
		return false;
	buf[pos] = '\0';
	return true;
}
