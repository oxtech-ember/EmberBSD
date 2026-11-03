/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). IEEE and C-locale formatting cases. */
#include <stdio.h>
#include <string.h>
#include "lint_fp.h"
struct sample { const char *input; unsigned digits; const char *expected; };
int main(void)
{
	static const struct sample cases[] = {
		{"1.5", 6, "1.5"}, {"1200000", 6, "1.2e+06"},
		{"1234567", 6, "1.23457e+06"}, {"0.0001", 6, "0.0001"},
		{"0.00001", 6, "1e-05"}, {"-0", 6, "-0"},
		{"0x1p16384", 6, "inf"},
		{"1267650600228229401496703205377", 36, "1267650600228229401496703205377"},
	};
	for (size_t i=0; i<sizeof(cases)/sizeof(cases[0]); i++) {
		lint_fp_t v = {0, 0}; unsigned status=0; char buf[128]="";
		if (!lint_fp_parse(cases[i].input,strlen(cases[i].input),LINT_FP128,&v,&status) ||
		    !lint_fp_format(v,cases[i].digits,buf,sizeof(buf)) || strcmp(buf,cases[i].expected)) {
			fprintf(stderr,"FAIL %zu: %s expected %s\n",i,buf,cases[i].expected); return 1;
		}
	}
	static const struct { const char *input; double val; enum lint_fp_format fmt; } refs[] = {
		{"0x1p0",1.0,LINT_FP64}, {"0x1.8p0",1.5,LINT_FP32},
		{"0x1.24f8p20",0x1.24f8p20,LINT_FP64}, {"0x1p-14",0x1p-14,LINT_FP32},
		{"0x1.fffffffffffffp1023",0x1.fffffffffffffp1023,LINT_FP64},
		{"-0x1.123456p25",-0x1.123456p25,LINT_FP32},
	};
	for (size_t i=0;i<sizeof(refs)/sizeof(refs[0]);i++) {
		lint_fp_t v = {0,0}; unsigned status=0; char got[128]="",expected[128];
		snprintf(expected,sizeof(expected),"%.6g",refs[i].val);
		if (!lint_fp_parse(refs[i].input,strlen(refs[i].input),refs[i].fmt,&v,&status) ||
		    !lint_fp_format(v,6,got,sizeof(got)) || strcmp(got,expected)) {
			fprintf(stderr,"FAIL reference %zu: %s expected %s\n",i,got,expected); return 1;
		}
	}
	lint_fp_t n = {UINT64_C(0x7fff800000000000),0}; char small[2], result[8];
	if (!lint_fp_format(n,6,result,sizeof(result)) || strcmp(result,"nan") || lint_fp_format(n,6,small,sizeof(small))) return 1;
	puts("PASS: gdtoa formatting, %g thresholds, precision, snprintf references and bounds"); return 0;
}
