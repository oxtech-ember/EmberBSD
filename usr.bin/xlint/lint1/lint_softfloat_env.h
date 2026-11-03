/* SoftFloat host environment for xlint; original notices remain with its
 * sources. */
#ifndef LINT_SOFTFLOAT_ENV_H
#define LINT_SOFTFLOAT_ENV_H
#include <stdint.h>
#include <sys/types.h>
#define FLOAT128
#define FLOATX80
#define BITS64
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LITTLEENDIAN
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#else
#define BIGENDIAN
#ifndef BYTE_ORDER
#define BYTE_ORDER BIG_ENDIAN
#endif
#endif
#define LIT64(a) a##LL
#define INLINE static inline
#define FP_RN 0
#define FP_RZ 1
#define FP_RM 2
#define FP_RP 3
#define FP_X_INV 1
#define FP_X_DZ 2
#define FP_X_OFL 4
#define FP_X_UFL 8
#define FP_X_IMP 16
extern unsigned lint_softfloat_status;
static inline int
fpgetround(void)
{
	return FP_RN;
}
static inline void
float_raise(int e)
{
	lint_softfloat_status |= (unsigned)e;
}
static inline void
float_set_inexact(void)
{
	float_raise(FP_X_IMP);
}
static inline void
float_set_invalid(void)
{
	float_raise(FP_X_INV);
}
typedef int flag, int8, int16;
typedef unsigned int uint8, uint16;
typedef unsigned int uint32;
typedef signed int int32;
typedef uint64_t uint64;
typedef int64_t int64;
typedef uint8_t bits8;
typedef int8_t sbits8;
typedef uint16_t bits16;
typedef int16_t sbits16;
typedef uint32_t bits32;
typedef int32_t sbits32;
typedef uint64_t bits64;
typedef int64_t sbits64;
#define float128_add lint_sf_float128_add
#define float128_div lint_sf_float128_div
#define float128_eq lint_sf_float128_eq
#define float128_eq_signaling lint_sf_float128_eq_signaling
#define float128_is_nan lint_sf_float128_is_nan
#define float128_is_signaling_nan lint_sf_float128_is_signaling_nan
#define float128_le lint_sf_float128_le
#define float128_le_quiet lint_sf_float128_le_quiet
#define float128_lt lint_sf_float128_lt
#define float128_lt_quiet lint_sf_float128_lt_quiet
#define float128_mul lint_sf_float128_mul
#define float128_rem lint_sf_float128_rem
#define float128_round_to_int lint_sf_float128_round_to_int
#define float128_sqrt lint_sf_float128_sqrt
#define float128_sub lint_sf_float128_sub
#define float128_to_float32 lint_sf_float128_to_float32
#define float128_to_float64 lint_sf_float128_to_float64
#define float128_to_floatx80 lint_sf_float128_to_floatx80
#define float128_to_int32 lint_sf_float128_to_int32
#define float128_to_int32_round_to_zero lint_sf_float128_to_int32_round_to_zero
#define float128_to_int64 lint_sf_float128_to_int64
#define float128_to_int64_round_to_zero lint_sf_float128_to_int64_round_to_zero
#define float32_add lint_sf_float32_add
#define float32_div lint_sf_float32_div
#define float32_eq lint_sf_float32_eq
#define float32_eq_signaling lint_sf_float32_eq_signaling
#define float32_is_signaling_nan lint_sf_float32_is_signaling_nan
#define float32_le lint_sf_float32_le
#define float32_le_quiet lint_sf_float32_le_quiet
#define float32_lt lint_sf_float32_lt
#define float32_lt_quiet lint_sf_float32_lt_quiet
#define float32_mul lint_sf_float32_mul
#define float32_rem lint_sf_float32_rem
#define float32_round_to_int lint_sf_float32_round_to_int
#define float32_sqrt lint_sf_float32_sqrt
#define float32_sub lint_sf_float32_sub
#define float32_to_float128 lint_sf_float32_to_float128
#define float32_to_float64 lint_sf_float32_to_float64
#define float32_to_floatx80 lint_sf_float32_to_floatx80
#define float32_to_int32 lint_sf_float32_to_int32
#define float32_to_int32_round_to_zero lint_sf_float32_to_int32_round_to_zero
#define float32_to_int64 lint_sf_float32_to_int64
#define float32_to_int64_round_to_zero lint_sf_float32_to_int64_round_to_zero
#define float32_to_uint32_round_to_zero lint_sf_float32_to_uint32_round_to_zero
#define float64_add lint_sf_float64_add
#define float64_div lint_sf_float64_div
#define float64_eq lint_sf_float64_eq
#define float64_eq_signaling lint_sf_float64_eq_signaling
#define float64_is_nan lint_sf_float64_is_nan
#define float64_is_signaling_nan lint_sf_float64_is_signaling_nan
#define float64_le lint_sf_float64_le
#define float64_le_quiet lint_sf_float64_le_quiet
#define float64_lt lint_sf_float64_lt
#define float64_lt_quiet lint_sf_float64_lt_quiet
#define float64_mul lint_sf_float64_mul
#define float64_rem lint_sf_float64_rem
#define float64_round_to_int lint_sf_float64_round_to_int
#define float64_sqrt lint_sf_float64_sqrt
#define float64_sub lint_sf_float64_sub
#define float64_to_float128 lint_sf_float64_to_float128
#define float64_to_float32 lint_sf_float64_to_float32
#define float64_to_floatx80 lint_sf_float64_to_floatx80
#define float64_to_int32 lint_sf_float64_to_int32
#define float64_to_int32_round_to_zero lint_sf_float64_to_int32_round_to_zero
#define float64_to_int64 lint_sf_float64_to_int64
#define float64_to_int64_round_to_zero lint_sf_float64_to_int64_round_to_zero
#define float64_to_uint32_round_to_zero lint_sf_float64_to_uint32_round_to_zero
#define float64_to_uint64 lint_sf_float64_to_uint64
#define float_detect_tininess lint_sf_float_detect_tininess
#define floatx80_add lint_sf_floatx80_add
#define floatx80_div lint_sf_floatx80_div
#define floatx80_eq lint_sf_floatx80_eq
#define floatx80_eq_signaling lint_sf_floatx80_eq_signaling
#define floatx80_is_signaling_nan lint_sf_floatx80_is_signaling_nan
#define floatx80_le lint_sf_floatx80_le
#define floatx80_le_quiet lint_sf_floatx80_le_quiet
#define floatx80_lt lint_sf_floatx80_lt
#define floatx80_lt_quiet lint_sf_floatx80_lt_quiet
#define floatx80_mul lint_sf_floatx80_mul
#define floatx80_rem lint_sf_floatx80_rem
#define floatx80_round_to_int lint_sf_floatx80_round_to_int
#define floatx80_rounding_precision lint_sf_floatx80_rounding_precision
#define floatx80_sqrt lint_sf_floatx80_sqrt
#define floatx80_sub lint_sf_floatx80_sub
#define floatx80_to_float128 lint_sf_floatx80_to_float128
#define floatx80_to_float32 lint_sf_floatx80_to_float32
#define floatx80_to_float64 lint_sf_floatx80_to_float64
#define floatx80_to_int32 lint_sf_floatx80_to_int32
#define floatx80_to_int32_round_to_zero lint_sf_floatx80_to_int32_round_to_zero
#define floatx80_to_int64 lint_sf_floatx80_to_int64
#define floatx80_to_int64_round_to_zero lint_sf_floatx80_to_int64_round_to_zero
#define int32_to_float128 lint_sf_int32_to_float128
#define int32_to_float32 lint_sf_int32_to_float32
#define int32_to_float64 lint_sf_int32_to_float64
#define int32_to_floatx80 lint_sf_int32_to_floatx80
#define int64_to_float128 lint_sf_int64_to_float128
#define int64_to_float32 lint_sf_int64_to_float32
#define int64_to_float64 lint_sf_int64_to_float64
#define int64_to_floatx80 lint_sf_int64_to_floatx80
#endif
