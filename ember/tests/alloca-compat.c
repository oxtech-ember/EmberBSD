/* SPDX-License-Identifier: BSD-2-Clause */
/* Dynamic alloca must remain valid until its caller returns. */
#define _NETBSD_SOURCE 1
#include <stdlib.h>
#ifdef __cplusplus
#include <cstdlib>
#endif

__attribute__((noinline)) static unsigned
exercise(unsigned n)
{
#ifdef __cplusplus
	volatile unsigned char *p = (volatile unsigned char *)::alloca(n);
#else
	volatile unsigned char *p = (volatile unsigned char *)alloca(n);
#endif
	unsigned sum = 0;

	for (unsigned i = 0; i < n; i++)
		p[i] = (unsigned char)(i ^ n);
	for (unsigned i = 0; i < n; i++)
		sum += p[i];
	return sum;
}

int
main(int argc, char **argv)
{
	volatile unsigned n = (unsigned)argc + 31;
	unsigned expected = 0;
	(void)argv;
#ifdef __cplusplus
	void *p = std::malloc(8);
	std::free(p);
#endif
	for (unsigned i = 0; i < n; i++)
		expected += (unsigned char)(i ^ n);
	return exercise(n) != expected;
}
