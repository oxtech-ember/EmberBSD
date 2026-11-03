/* Target-format arithmetic backed by the OS SoftFloat implementation. */
#include <assert.h>
#include <limits.h>
#include "../common/lint_fp.h"
#define SOFTFLOAT_ENV_HEADER "lint_softfloat_env.h"
#include "../../../sys/lib/libkern/softfloat.h"

unsigned lint_softfloat_status;

static float128
sf(lint_fp_t x)
{
	return (float128){x.hi, x.lo};
}

static lint_fp_t
lf(float128 x)
{
	return (lint_fp_t){x.high, x.low};
}

static void
begin(void)
{
	lint_softfloat_status = 0;
}

static void
end(unsigned *status)
{
	if (status != NULL)
		*status = lint_softfloat_status;
}

static float128
round_sf(float128 v, enum lint_fp_format fmt)
{
	switch (fmt) {
	case LINT_FP32:
		return float32_to_float128(float128_to_float32(v));
	case LINT_FP64:
		return float64_to_float128(float128_to_float64(v));
	case LINT_FP80:
		return floatx80_to_float128(float128_to_floatx80(v));
	case LINT_FP128:
		return v;
	}
	assert(0);
	return v;
}

lint_fp_t
lint_fp_round(lint_fp_t v, enum lint_fp_format fmt, unsigned *status)
{
	float128 z;

	begin();
	z = round_sf(sf(v), fmt);
	end(status);
	return lf(z);
}

lint_fp_t
lint_fp_from_i64(int64_t v, enum lint_fp_format fmt, unsigned *status)
{
	float128 z;

	begin();
	z = round_sf(int64_to_float128(v), fmt);
	end(status);
	return lf(z);
}

lint_fp_t
lint_fp_from_u64(uint64_t v, enum lint_fp_format fmt, unsigned *status)
{
	float128 z;

	begin();
	z = int64_to_float128((int64_t)(v >> 1));
	z = float128_add(z, z);
	if (v & 1)
		z = float128_add(z, int32_to_float128(1));
	z = round_sf(z, fmt);
	end(status);
	return lf(z);
}

bool
lint_fp_is_zero(lint_fp_t v)
{
	return !(v.hi & UINT64_C(0x7fffffffffffffff)) && !v.lo;
}

bool
lint_fp_signbit(lint_fp_t v)
{
	return (v.hi >> 63) != 0;
}

bool
lint_fp_is_nan(lint_fp_t v)
{
	return (v.hi & UINT64_C(0x7fff000000000000)) ==
		   UINT64_C(0x7fff000000000000) &&
	       ((v.hi & UINT64_C(0x0000ffffffffffff)) || v.lo);
}

bool
lint_fp_is_finite(lint_fp_t v)
{
	return (v.hi & UINT64_C(0x7fff000000000000)) !=
	       UINT64_C(0x7fff000000000000);
}

lint_fp_t
lint_fp_negate(lint_fp_t v)
{
	v.hi ^= UINT64_C(0x8000000000000000);
	return v;
}

int
lint_fp_compare(lint_fp_t a, lint_fp_t b)
{
	if (lint_fp_is_nan(a) || lint_fp_is_nan(b))
		return 2;
	if (float128_eq(sf(a), sf(b)))
		return 0;
	return float128_lt(sf(a), sf(b)) ? -1 : 1;
}

bool
lint_fp_to_i64(lint_fp_t v, int64_t *out)
{
	const lint_fp_t min = {UINT64_C(0xc03e000000000000), 0},
			maxplus = {UINT64_C(0x403e000000000000), 0};
	const lint_fp_t belowmin =
	    lf(float128_sub(sf(min), int32_to_float128(1)));
	if (!lint_fp_is_finite(v) || lint_fp_compare(v, belowmin) <= 0 ||
	    lint_fp_compare(v, maxplus) >= 0)
		return false;
	if (lint_fp_compare(v, min) < 0)
		*out = INT64_MIN;
	else
		*out = float128_to_int64_round_to_zero(sf(v));
	return true;
}

bool
lint_fp_to_u64(lint_fp_t v, uint64_t *out)
{
	const lint_fp_t zero = {0, 0}, mid = {UINT64_C(0x403e000000000000), 0},
			limit = {UINT64_C(0x403f000000000000), 0};
	const lint_fp_t minusone = {UINT64_C(0xbfff000000000000), 0};
	if (!lint_fp_is_finite(v) || lint_fp_compare(v, minusone) <= 0 ||
	    lint_fp_compare(v, limit) >= 0)
		return false;
	if (lint_fp_compare(v, zero) < 0)
		*out = 0;
	else if (lint_fp_compare(v, mid) < 0)
		*out = (uint64_t)float128_to_int64_round_to_zero(sf(v));
	else
		*out = (UINT64_C(1) << 63) +
		       (uint64_t)float128_to_int64_round_to_zero(
			   float128_sub(sf(v), sf(mid)));
	return true;
}

lint_fp_t
lint_fp_max_finite(enum lint_fp_format fmt)
{
	float128 v;

	begin();
	switch (fmt) {
	case LINT_FP32:
		v = float32_to_float128(UINT32_C(0x7f7fffff));
		break;
	case LINT_FP64:
		v = float64_to_float128(UINT64_C(0x7fefffffffffffff));
		break;
	case LINT_FP80: {
		floatx80 x;
		x.high = 0x7ffe;
		x.low = UINT64_MAX;
		v = floatx80_to_float128(x);
		break;
	}
	case LINT_FP128:
		v = (float128){UINT64_C(0x7ffeffffffffffff), UINT64_MAX};
		break;
	default:
		assert(0);
		v = (float128){0, 0};
	}
	return lf(v);
}
#define DO_OP(prefix, x, y)                                                    \
	((op) == '+'   ? prefix##_add(x, y)                                    \
	 : (op) == '-' ? prefix##_sub(x, y)                                    \
	 : (op) == '*' ? prefix##_mul(x, y)                                    \
		       : prefix##_div(x, y))
lint_fp_t
lint_fp_binary(char op, lint_fp_t a, lint_fp_t b, enum lint_fp_format fmt,
	       unsigned *status)
{
	assert(op == '+' || op == '-' || op == '*' || op == '/');
	begin();
	float128 z;
	switch (fmt) {
	case LINT_FP32: {
		float32 x = float128_to_float32(sf(a)),
			y = float128_to_float32(sf(b));
		z = float32_to_float128(DO_OP(float32, x, y));
		break;
	}
	case LINT_FP64: {
		float64 x = float128_to_float64(sf(a)),
			y = float128_to_float64(sf(b));
		z = float64_to_float128(DO_OP(float64, x, y));
		break;
	}
	case LINT_FP80: {
		floatx80 x = float128_to_floatx80(sf(a)),
			 y = float128_to_floatx80(sf(b));
		z = floatx80_to_float128(DO_OP(floatx80, x, y));
		break;
	}
	case LINT_FP128:
		z = DO_OP(float128, sf(a), sf(b));
		break;
	default:
		assert(0);
		z = (float128){0, 0};
	}
	end(status);
	return lf(z);
}
