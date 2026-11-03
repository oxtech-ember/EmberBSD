/* SPDX-License-Identifier: BSD-2-Clause */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#endif

#include <sys/types.h>

#include "bmm150_math.h"

int32_t
bmm150_raw_to_nanotesla(int16_t raw)
{
	int32_t scaled = (int32_t)raw * 1000;

	/* Round symmetrically to the nearest nT from 16 LSB per microtesla. */
	if (scaled < 0)
		return -((-scaled + 8) / 16);
	return (scaled + 8) / 16;
}
