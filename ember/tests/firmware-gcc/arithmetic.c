/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted freestanding compiler contract. */
#include "smoke.h"

#ifdef FIRMWARE_FOREIGN_LIBRARY
uint64_t firmware_external(uint64_t);
#endif

struct firmware_pair {
	uint8_t first;
	uint64_t second;
};

static uint64_t
firmware_sum(unsigned int count, ...)
{
	va_list args;
	uint64_t result = 0;
	unsigned int i;

	va_start(args, count);
	for (i = 0; i < count; i++)
		result += va_arg(args, uint64_t);
	va_end(args);
	return result;
}

uint64_t
firmware_calculate(uint64_t input)
{
	_Atomic uint64_t value;
	uint64_t sum;

	atomic_init(&value, input);
	sum = firmware_sum(2, atomic_load_explicit(&value,
	    memory_order_relaxed), UINT64_C(1));
	sum = __builtin_bswap64(sum) ^
	    (uint64_t)offsetof(struct firmware_pair, second);
#ifdef FIRMWARE_FOREIGN_LIBRARY
	return firmware_external(sum);
#else
	return sum;
#endif
}
