/* SPDX-License-Identifier: BSD-2-Clause */
/* IEEE quiet NaN words in host byte order for gdtoa. */
#define f_QNAN 0x7fc00000
#ifdef IEEE_LITTLE_ENDIAN
#define d_QNAN0 0x0
#define d_QNAN1 0x7ff80000
#define ld_QNAN0 0x0
#define ld_QNAN1 0x0
#define ld_QNAN2 0x0
#define ld_QNAN3 0x7fff8000
#else
#define d_QNAN0 0x7ff80000
#define d_QNAN1 0x0
#define ld_QNAN0 0x7fff8000
#define ld_QNAN1 0x0
#define ld_QNAN2 0x0
#define ld_QNAN3 0x0
#endif
