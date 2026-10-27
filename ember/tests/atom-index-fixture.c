/* SPDX-License-Identifier: BSD-2-Clause */
#include <stddef.h>
#include <stdint.h>
#include "atom-types.h"
#include "atombios.h"
#include <assert.h>

_Static_assert(sizeof(USHORT) == 2, "ATOM table entries are 16-bit");
_Static_assert(GetIndexIntoMasterTable(COMMAND, ASIC_Init) == 0,
    "first command index");
_Static_assert(GetIndexIntoMasterTable(COMMAND, GetDisplaySurfaceSize) == 1,
    "second command index");
_Static_assert(GetIndexIntoMasterTable(DATA, FirmwareInfo) == 4,
    "firmware information index");

int
main(void)
{
	const volatile unsigned indices[] = {
	    GetIndexIntoMasterTable(COMMAND, ASIC_Init),
	    GetIndexIntoMasterTable(COMMAND, GetDisplaySurfaceSize),
	    GetIndexIntoMasterTable(DATA, FirmwareInfo),
	};

	assert(indices[0] == 0 && indices[1] == 1 && indices[2] == 4);
	return 0;
}
