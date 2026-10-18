/* SPDX-License-Identifier: BSD-2-Clause */
/* A target archive outside the permitted firmware search directories. */
#include <stdint.h>

uint64_t
firmware_external(uint64_t value)
{

	return value;
}
