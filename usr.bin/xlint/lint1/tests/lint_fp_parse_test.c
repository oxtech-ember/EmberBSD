/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). Independent IEEE literal boundaries. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "lint_fp.h"

struct sample { enum lint_fp_format fmt; const char *text; uint64_t hi, lo; };
int
main(void)
{
	static const struct sample cases[] = {
		{LINT_FP32, "1.5", UINT64_C(0x3fff800000000000), 0},
		{LINT_FP32, "0x1.000001p0", UINT64_C(0x3fff000000000000), 0},
		{LINT_FP32, "0x1.000003p0", UINT64_C(0x3fff000004000000), 0},
		{LINT_FP32, "0x1p-149", UINT64_C(0x3f6a000000000000), 0},
		{LINT_FP32, "0x1p128", UINT64_C(0x7fff000000000000), 0},
		{LINT_FP64, "0x1.00000000000008p0", UINT64_C(0x3fff000000000000), 0},
		{LINT_FP64, "0x1.00000000000018p0", UINT64_C(0x3fff000000000000), UINT64_C(0x2000000000000000)},
		{LINT_FP64, "0x1p-1074", UINT64_C(0x3bcd000000000000), 0},
		{LINT_FP64, "0x1p1024", UINT64_C(0x7fff000000000000), 0},
		{LINT_FP80, "0x1.0000000000000001p0", UINT64_C(0x3fff000000000000), 0},
		{LINT_FP80, "0x1.0000000000000003p0", UINT64_C(0x3fff000000000000), UINT64_C(0x0004000000000000)},
		{LINT_FP80, "0x1p-16445", 0, UINT64_C(0x0002000000000000)},
		{LINT_FP80, "0x1p16384", UINT64_C(0x7fff000000000000), 0},
		{LINT_FP128, "0x1.00000000000000000000000000008p0", UINT64_C(0x3fff000000000000), 0},
		{LINT_FP128, "0x1.00000000000000000000000000018p0", UINT64_C(0x3fff000000000000), 2},
		{LINT_FP128, "1267650600228229401496703205377", UINT64_C(0x4063000000000000), 0x1000},
		{LINT_FP128, "0x1p-16494", 0, 1},
		{LINT_FP128, "0x1p-16495", 0, 0},
		{LINT_FP128, "0x1p-16382", UINT64_C(0x0001000000000000), 0},
		{LINT_FP128, "0x1.ffffffffffffffffffffffffffffp16383", UINT64_C(0x7ffeffffffffffff), UINT64_MAX},
		{LINT_FP128, "0x1p16384", UINT64_C(0x7fff000000000000), 0},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		lint_fp_t val = {0, 0};
		unsigned status = ~0U;
		if (!lint_fp_parse(cases[i].text, strlen(cases[i].text), cases[i].fmt, &val, &status) ||
		    val.hi != cases[i].hi || val.lo != cases[i].lo) {
			fprintf(stderr, "FAIL %zu %s: %016" PRIx64 " %016" PRIx64 " status=%u\n", i, cases[i].text, val.hi, val.lo, status);
			return 1;
		}
	}
	lint_fp_t val = {0, 0};
	unsigned status = 0;
	if (lint_fp_parse("1xyz", 4, LINT_FP128, &val, &status) || !(status & LINT_FP_INVALID))
		return 1;
	status = ~0U;
	if (!lint_fp_parse("1", 1, LINT_FP32, &val, &status) || status != 0)
		return 1;
	if (!lint_fp_parse("1e99999", 7, LINT_FP128, &val, &status) ||
	    (status & (LINT_FP_OVERFLOW | LINT_FP_INEXACT)) !=
	    (LINT_FP_OVERFLOW | LINT_FP_INEXACT))
		return 1;
	if (!lint_fp_parse("0x1p-16495", 10, LINT_FP128, &val, &status) ||
	    (status & (LINT_FP_UNDERFLOW | LINT_FP_INEXACT)) !=
	    (LINT_FP_UNDERFLOW | LINT_FP_INEXACT))
		return 1;
	if (!lint_fp_parse("-0", 2, LINT_FP64, &val, &status) ||
	    val.hi != UINT64_C(0x8000000000000000) || val.lo != 0)
		return 1;
	puts("PASS: four target formats, exact rounding, boundaries, status and complete input");
	return 0;
}
