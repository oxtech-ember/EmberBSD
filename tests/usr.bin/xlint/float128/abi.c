/* Clang oracle for i386 and x86_64; _Float128 is a GCC/lint input alias. */
typedef __float128 qreal;
typedef __float128 qalias;
typedef __float128 _Complex qcomplex;
typedef _Complex __float128 qcomplex_reverse;
_Static_assert(sizeof(qreal) == 16, "binary128 size");
_Static_assert(sizeof(qalias) == 16, "alias size");
_Static_assert(sizeof(qcomplex) == 32, "complex size");
_Static_assert(sizeof(qcomplex_reverse) == 32, "complex reverse size");
_Static_assert(__alignof__(qreal) == 16, "real alignment");
_Static_assert(__alignof__(qcomplex) == 16, "complex alignment");
_Static_assert(__LDBL_MANT_DIG__ == 64, "x87 long double mantissa");
struct real_layout { char c; qreal x; };
struct complex_layout { char c; qcomplex x; };
_Static_assert(__builtin_offsetof(struct real_layout, x) == 16, "real offset");
_Static_assert(__builtin_offsetof(struct complex_layout, x) == 16, "complex offset");
