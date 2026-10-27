/* SPDX-License-Identifier: BSD-2-Clause */
#include <stddef.h>
#include <stdint.h>
#include "platform/acenv.h"
#include "actypes.h"
#ifndef __ACNETBSD_H__
#error The fixture must use the actual NetBSD ACPICA platform header.
#endif
#include <assert.h>

struct ordinary { uint8_t tag; uint32_t value; };
struct __attribute__((__packed__)) packed { uint8_t tag; uint32_t value; };

static void *
once(void *pointer, unsigned *calls)
{

	(*calls)++;
	return pointer;
}

int
main(void)
{
	int object = 0;
	unsigned calls = 0;
	uintptr_t address = ACPI_TO_INTEGER(once(&object, &calls));

	assert(calls == 1 && address == (uintptr_t)&object);
	assert(ACPI_TO_INTEGER(NULL) == 0);
	assert(ACPI_PTR_TO_PHYSADDR(NULL) == 0);
	_Static_assert(ACPI_OFFSET(struct ordinary, value) ==
	    offsetof(struct ordinary, value), "ordinary offset");
	_Static_assert(ACPI_OFFSET(struct packed, value) ==
	    offsetof(struct packed, value), "packed offset");
	_Static_assert(ACPI_OFFSET(struct packed, value) == 1, "packed byte offset");
	_Static_assert(ACPI_OFFSET(struct ordinary, tag) == 0, "first member offset");
	_Static_assert(ACPI_OFFSET(struct packed, tag) == 0, "packed first offset");
	return 0;
}
