/* $NetBSD$ */
# 3 "d_gcc_float128.c"

/* Distinct binary128 types, including both complex specifier orders. */
/* lint1-flags: -g -Ac11 -w -X 351 */
/* lint1-only-if: float128 */

typedef float _Complex fwd_float;
typedef _Complex float rev_float;
typedef double _Complex fwd_double;
typedef _Complex double rev_double;
typedef long double _Complex fwd_long_double;
typedef _Complex long double rev_long_double;
typedef _Complex double long rev_long_double_alt;

_Static_assert(sizeof(fwd_float) == sizeof(rev_float), "float orders");
_Static_assert(sizeof(fwd_double) == sizeof(rev_double), "double orders");
_Static_assert(sizeof(fwd_long_double) == sizeof(rev_long_double), "long double orders");
_Static_assert(sizeof(fwd_long_double) == sizeof(rev_long_double_alt), "long double alternate order");

typedef __float128 q1;
typedef _Float128 q2;
typedef __float128 _Complex qc1;
typedef _Complex __float128 qc2;
typedef _Float128 _Complex qc3;
typedef _Complex _Float128 qc4;

_Static_assert(sizeof(q1) == 16, "real size");
_Static_assert(sizeof(q2) == 16, "alternate real size");
_Static_assert(sizeof(qc1) == 32, "complex size");
_Static_assert(sizeof(qc2) == 32, "complex reverse size");
_Static_assert(sizeof(qc3) == 32, "alternate complex size");
_Static_assert(sizeof(qc4) == 32, "alternate complex reverse size");

extern q1 real_value;
extern qc1 complex_value;

/* Retain each target format's precision independently of the host. */
_Static_assert(0x1p100Q + 1.0Q - 0x1p100Q == 1.0Q, "quad precision");
_Static_assert(0x1p100L + 1.0L - 0x1p100L == 0.0L, "x87 precision");
_Static_assert(1.0L + 0x1p-63L != 1.0L, "x87 mantissa");
_Static_assert(1.0 + 0x1p-53 == 1.0, "double ties to even");
_Static_assert(1.0F + 0x1p-24F == 1.0F, "float ties to even");
_Static_assert(1.0000000000000000000000000000000002Q ==
    1.0Q + 0x1p-112Q, "decimal quad rounding");
_Static_assert(1.0f128 == 1.0Q, "C suffix");
_Static_assert(1.0F128 == 1.0q, "alternate suffixes");
_Static_assert(0x1p-16494Q != 0.0Q, "smallest subnormal");
_Static_assert(0x1p-16494Q * 2.0Q == 0x1p-16493Q, "subnormal multiply");
_Static_assert(8.0Q / 4.0Q == 2.0Q, "divide");
_Static_assert(-1.0Q < 0.0Q, "negative comparison");
_Static_assert(-0.0Q == 0.0Q, "signed zero equality");
_Static_assert((-0.0Q ? 10 : 20) == 20, "signed zero condition");
_Static_assert((0x1p-16494Q ? 10 : 20) == 10, "subnormal condition");
_Static_assert(-2.0Q <= -1.0Q, "less than or equal");
_Static_assert(2.0Q > 1.0Q && 2.0Q >= 2.0Q, "greater comparisons");
_Static_assert((long long)-9223372036854775808.0Q ==
    (-9223372036854775807LL - 1), "signed lower boundary");
_Static_assert((long long)9223372036854775807.0Q ==
    9223372036854775807LL, "signed upper boundary");
_Static_assert((unsigned long long)18446744073709551615.0Q ==
    18446744073709551615ULL, "unsigned upper boundary");

_Static_assert(_Generic(real_value + 1.0L, q1: 1, default: 0),
    "quad has greater rank than long double");
_Static_assert(_Generic(real_value + complex_value, qc1: 1, default: 0),
    "complex result");
_Static_assert(_Generic(__real__ complex_value, q1: 1, default: 0),
    "real component type");
_Static_assert(_Generic(__imag__ complex_value, q1: 1, default: 0),
    "imaginary component type");
