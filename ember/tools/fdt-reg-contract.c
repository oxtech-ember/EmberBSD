/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual fdtbus_get_reg body. The lower uint64 decoder is controlled. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

typedef unsigned int u_int;

#if FDT_REG_BITS == 32
typedef uint32_t bus_addr_t;
typedef uint32_t bus_size_t;
#elif FDT_REG_BITS == 64
typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;
#else
#error Unsupported test width
#endif

static uint64_t raw_address, raw_size;
static int decoder_error;
static unsigned int calls, checks;

#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "FDT range check failed at line %d: %s\n", \
	    __LINE__, #x); exit(1); } } while (0)

static int
fdtbus_get_reg64(int phandle, unsigned int index, uint64_t *address,
    uint64_t *size)
{
	CHECK(phandle == 17 && index == 3);
	calls++;
	if (decoder_error != 0)
		return decoder_error;
	*address = raw_address;
	*size = raw_size;
	return 0;
}

#include "fdt-reg-actual.h"

struct range_case {
	uint64_t address, size;
	int error32, error64;
};

static void
run_case(const struct range_case *test, unsigned int outputs)
{
	const bus_addr_t saved_address = (bus_addr_t)0x13579bdfU;
	const bus_size_t saved_size = (bus_size_t)0x2468ace0U;
	bus_addr_t address = saved_address;
	bus_size_t size = saved_size;
	const unsigned int before = calls;
	int expected, error;

	raw_address = test->address;
	raw_size = test->size;
	expected = FDT_REG_BITS == 32 ? test->error32 : test->error64;
	error = fdtbus_get_reg(17, 3, (outputs & 1) ? &address : NULL,
	    (outputs & 2) ? &size : NULL);
	CHECK(calls == before + 1);
	CHECK(error == expected);
	CHECK(address == (error == 0 && (outputs & 1) ?
	    (bus_addr_t)test->address : saved_address));
	CHECK(size == (error == 0 && (outputs & 2) ?
	    (bus_size_t)test->size : saved_size));
}

int
main(void)
{
	static const struct range_case tests[] = {
		/* This wrapped sum used to pass and truncate to address0/size0x1000. */
		{ UINT64_C(0xfffffffe00000000), UINT64_C(0x200001000), ERANGE, ERANGE },
		{ 0, 4096, 0, 0 },
		{ 0, 0, 0, 0 },
		{ UINT64_C(0xffffffff), 1, 0, 0 },
		{ UINT64_C(0xffffffff), 2, ERANGE, 0 },
		{ UINT64_C(0x100000000), 0, ERANGE, 0 },
		{ 0, UINT64_C(0x100000000), ERANGE, 0 },
		{ UINT64_C(0xfffffffe), 2, 0, 0 },
		{ UINT64_C(0xfffffffe), 3, ERANGE, 0 },
		{ UINT64_MAX, 0, ERANGE, 0 },
		{ UINT64_MAX, 1, ERANGE, ERANGE },
		{ UINT64_MAX - 4095, 4095, ERANGE, 0 },
		{ UINT64_MAX - 4095, 4096, ERANGE, ERANGE },
		{ 1, UINT64_MAX, ERANGE, ERANGE },
		{ 0, UINT64_MAX, ERANGE, 0 },
	};
	unsigned int i, outputs;

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		for (outputs = 0; outputs < 4; outputs++)
			run_case(&tests[i], outputs);
	}
	decoder_error = EIO;
	for (outputs = 0; outputs < 4; outputs++) {
		struct range_case test = { 0, 0, EIO, EIO };
		run_case(&test, outputs);
	}
	printf("FDT register range %u-bit: %u checks PASS\n",
	    (unsigned int)FDT_REG_BITS, checks);
	return 0;
}
