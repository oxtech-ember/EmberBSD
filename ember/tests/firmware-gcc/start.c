/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted freestanding compiler contract. */
#include "smoke.h"

#ifdef FIRMWARE_EDK2
_Static_assert(FIRMWARE_SMOKE_MODULE == 1, "Use the fixture AutoGen.h");
#endif

#ifdef FIRMWARE_HOST_TEST
int
main(void)
{

	return firmware_calculate(3) == UINT64_C(0x0400000000000008) ? 0 : 1;
}
#else
volatile uint64_t firmware_result;

void
_start(void)
{

	firmware_result = firmware_calculate(3);
}
#endif
