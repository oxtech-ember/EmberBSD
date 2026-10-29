/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). */
#include <math.h>
#include <stdio.h>

int
main(void)
{
	volatile double input = 9.0;
	double (*volatile root)(double) = sqrt;
	double (*volatile length)(double, double) = hypot;
	double result = root(input);
	double distance = length(result, 4.0);

	if (!isfinite(result) || !isfinite(distance) ||
	    fabs(result - 3.0) > 1e-12 ||
	    fabs(distance - 5.0) > 1e-12) {
		fputs("libm valid-input check failed\n", stderr);
		return 1;
	}
	puts("libm valid-input check passed");
	return 0;
}
