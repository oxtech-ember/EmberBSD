/* SPDX-License-Identifier: BSD-2-Clause */
#include <atf-c.h>
#include <complex.h>
#include <math.h>

extern long double _Complex __divxc3(long double, long double, long double,
    long double);
extern long double _Complex __mulxc3(long double, long double, long double,
    long double);

ATF_TC(complex_helpers);
ATF_TC_HEAD(complex_helpers, tc)
{
	atf_tc_set_md_var(tc, "descr", "historic long double complex helpers");
}
ATF_TC_BODY(complex_helpers, tc)
{
	long double _Complex z;

	z = __mulxc3(2.0L, 3.0L, 4.0L, -1.0L);
	ATF_REQUIRE(creall(z) == 11.0L && cimagl(z) == 10.0L);
	z = __divxc3(11.0L, 10.0L, 4.0L, -1.0L);
	ATF_REQUIRE(fabsl(creall(z) - 2.0L) < 1e-15L);
	ATF_REQUIRE(fabsl(cimagl(z) - 3.0L) < 1e-15L);
#if !defined(__vax__)
	z = __mulxc3(-0.0L, 0.0L, 1.0L, 0.0L);
	ATF_REQUIRE(creall(z) == 0.0L && signbit(creall(z)));
	z = __mulxc3(INFINITY, 0.0L, 1.0L, 1.0L);
	ATF_REQUIRE(isinf(creall(z)) && isinf(cimagl(z)));
	z = __divxc3(NAN, NAN, 1.0L, 0.0L);
	ATF_REQUIRE(isnan(creall(z)) && isnan(cimagl(z)));
#endif
}
ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, complex_helpers);
	return atf_no_error();
}
