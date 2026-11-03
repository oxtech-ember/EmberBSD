/* SPDX-License-Identifier: BSD-2-Clause */
/* Host double layout used internally by gdtoa; target layout is independent. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define IEEE_LITTLE_ENDIAN
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define IEEE_BIG_ENDIAN
#else
#error Unsupported host byte order
#endif

#if !defined(__DBL_MANT_DIG__) || __DBL_MANT_DIG__ != 53 || \
    !defined(__DBL_MAX_EXP__) || __DBL_MAX_EXP__ != 1024
#error gdtoa requires IEEE binary64 host double
#endif
