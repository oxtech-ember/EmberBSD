/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sun60i_a733_gpu_fwcompute_vm.h"

struct fixture {
	uint8_t catalog[4096];
	uint8_t directories[A733_GPU_FWCOMPUTE_VM_WINDOWS][4096];
	uint8_t tables[A733_GPU_FWCOMPUTE_VM_WINDOWS][4096];
	void *directory[A733_GPU_FWCOMPUTE_VM_WINDOWS];
	void *table[A733_GPU_FWCOMPUTE_VM_WINDOWS];
	uint64_t directory_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS];
	uint64_t table_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS];
	struct a733_gpu_fwcompute_vm_region region;
	struct a733_gpu_fwcompute_vm_summary summary;
	struct {
		uint8_t before[16];
		uint8_t bytes[32];
		uint8_t after[16];
	} context;
};

static unsigned int checks, failures;

static void
check(int ok, const char *name)
{
	checks++;
	if (!ok) {
		fprintf(stderr, "FAIL: %s\n", name);
		failures++;
	}
}

static int
filled(const uint8_t *p, size_t size, uint8_t value)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (p[i] != value)
			return 0;
	}
	return 1;
}

static void
prepare(struct fixture *f, uint8_t poison)
{
	unsigned int i;

	memset(f, 0, sizeof(*f));
	for (i = 0; i < A733_GPU_FWCOMPUTE_VM_WINDOWS; i++) {
		f->directory[i] = f->directories[i];
		f->table[i] = f->tables[i];
		f->directory_pa[i] = UINT64_C(0x40166000) + i * 4096;
		f->table_pa[i] = UINT64_C(0x4016b000) + i * 4096;
	}
	f->region.va = UINT64_C(0x8000000000);
	f->region.pa = UINT64_C(0x123450000);
	f->region.pages = 1;
	f->region.flags = A733_GPU_FWCOMPUTE_VM_RO |
	    A733_GPU_FWCOMPUTE_VM_BYPASS;
	memset(&f->context, poison, sizeof(f->context));
}

static int
encode(struct fixture *f, uint64_t catalog_pa)
{
	return sun60i_a733_gpu_fwcompute_vm_encode(&f->region, 1,
	    f->catalog, f->directory, f->table, f->directory_pa,
	    f->table_pa, catalog_pa, f->context.bytes, &f->summary);
}

static void
guards(const struct fixture *f, uint8_t poison)
{
	check(filled(f->context.before, sizeof(f->context.before), poison),
	    "context write preserves preceding bytes");
	check(filled(f->context.after, sizeof(f->context.after), poison),
	    "context write preserves following bytes");
}

static void
success(void)
{
	static const uint8_t want[32] = {
		0x00, 0x50, 0x16, 0x40, 0x00, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff,
	};
	static const uint8_t next[32] = {
		0x00, 0x60, 0x45, 0x23, 0x01, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff,
	};
	static const uint8_t pce[4] = { 0x61, 0x16, 0x40, 0x00 };
	static const uint8_t pde[8] = { 0x01, 0xb0, 0x16, 0x40 };
	static const uint8_t pte[8] = { 0x0b, 0x00, 0x45, 0x23, 0x01 };
	struct fixture f;

	prepare(&f, 0xa5);
	check(encode(&f, UINT64_C(0x40165000)) == 0,
	    "valid mapping encodes");
	check(memcmp(f.context.bytes, want, sizeof(want)) == 0,
	    "success initializes all 32 firmware context bytes");
	guards(&f, 0xa5);
	check(f.summary.windows == 1 && f.summary.directories == 1 &&
	    f.summary.leaves == 1, "valid mapping keeps its summary");
	check(memcmp(f.catalog + 512 * 4, pce, sizeof(pce)) == 0,
	    "catalog points to the page directory");
	check(memcmp(f.directories[0], pde, sizeof(pde)) == 0,
	    "directory points to the page table");
	check(memcmp(f.tables[0], pte, sizeof(pte)) == 0,
	    "page table keeps the physical address and access policy");

	memset(&f.context, 0x5a, sizeof(f.context));
	check(encode(&f, UINT64_C(0x123456000)) == 0,
	    "reused context encodes a new catalog address");
	check(memcmp(f.context.bytes, next, sizeof(next)) == 0,
	    "reused context contains no previous flags or breakpoint data");
	guards(&f, 0x5a);
}

static void
rejected(void)
{
	struct fixture f;
	unsigned int i;

	/* Include errors before and after page-table encoding starts. */
	for (i = 0; i < 6; i++) {
		prepare(&f, 0xd3);
		switch (i) {
		case 0:
			f.region.pages = 0;
			break;
		case 1:
			f.region.va++;
			break;
		case 2:
			f.table[0] = NULL;
			break;
		case 3:
			f.table_pa[0]++;
			break;
		case 4:
			f.directory_pa[0]++;
			break;
		case 5:
			f.region.pa = UINT64_C(0xffffff000);
			f.region.pages = 2;
			break;
		}
		check(encode(&f, UINT64_C(0x40165000)) == EINVAL,
		    "invalid mapping is rejected");
		check(filled(f.context.bytes, sizeof(f.context.bytes), 0xd3),
		    "rejected mapping leaves the firmware context unchanged");
		guards(&f, 0xd3);
	}
}

int
main(void)
{
	success();
	rejected();
	if (failures != 0) {
		fprintf(stderr, "%u of %u checks failed\n", failures, checks);
		return 1;
	}
	printf("PASS: %u real-source firmware VM context checks\n", checks);
	return 0;
}
