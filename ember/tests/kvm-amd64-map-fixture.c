/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
typedef uint64_t paddr_t;
typedef uint64_t psize_t;
typedef struct { uint64_t ptdpaddr, nmemsegs; } cpu_kcore_hdr_t;
typedef struct { uint64_t start, size; } phys_ram_seg_t;
typedef struct { void *cpu_data; off_t dump_off; } kvm_t;
#define ALIGN(x) (((x) + 7) & ~(size_t)7)
static int fail_budget, allocations, searches;
static void *
checked_calloc(size_t count, size_t size)
{
	allocations++;
	assert(size == 24);
	if (fail_budget != 0) {
		if (fail_budget > 0)
			fail_budget--;
		return NULL;
	}
	return calloc(count, size);
}
static void *
checked_bsearch(const void *key, const void *base, size_t count, size_t size,
    int (*compare)(const void *, const void *))
{
	assert(base != NULL);
	searches++;
	return bsearch(key, base, count, size, compare);
}
#define calloc checked_calloc
#define bsearch checked_bsearch
@PRODUCTION@
#undef calloc
#undef bsearch
int
main(int argc, char **argv)
{
	struct {
		cpu_kcore_hdr_t cpu;
		phys_ram_seg_t segments[3];
	} data = {{0, 3}, {{0x1000, 0x100}, {0x3000, 0x200}, {0x9000, 0x80}}};
	const struct { paddr_t pa; off_t offset; } cases[] = {
		{0, 4096}, {0x1000, 4096}, {0x1001, 4097}, {0x10ff, 4351},
		{0x1100, 4096}, {0x2fff, 4096}, {0x3000, 4352},
		{0x31ff, 4863}, {0x3200, 4096}, {0x9000, 4864},
		{0x907f, 4991}, {0x9080, 4096}
	};
	kvm_t kd = {&data, 4096};
	assert(argc == 2);
	if (strcmp(argv[1], "oom") == 0)
		fail_budget = -1;
	else if (strcmp(argv[1], "retry") == 0)
		fail_budget = 1;
	else if (strcmp(argv[1], "empty-oom") == 0) {
		fail_budget = -1;
		data.cpu.nmemsegs = 0;
	} else if (strcmp(argv[1], "high-oom") == 0) {
		fail_budget = -1;
		data.cpu.nmemsegs = 1;
		data.segments[0].start = UINT64_MAX - 31;
		data.segments[0].size = 32;
		assert(_kvm_pa2off(&kd, UINT64_MAX) == 4127);
		assert(_kvm_pa2off(&kd, UINT64_MAX - 32) == 4096);
		assert(allocations == 2);
		assert(searches == 0);
		puts("high-oom: wrapped-end range boundary PASS");
		return 0;
	} else
		assert(strcmp(argv[1], "success") == 0);
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		off_t expected = data.cpu.nmemsegs == 0 ? 4096 : cases[i].offset;
		assert(_kvm_pa2off(&kd, cases[i].pa) == expected);
	}
	if (fail_budget == -1) {
		assert(allocations == 12);
		assert(searches == 0);
	} else {
		assert(allocations == (strcmp(argv[1], "retry") == 0 ? 2 : 1));
		assert(searches == (strcmp(argv[1], "retry") == 0 ? 11 : 12));
	}
	printf("%s: 12 offset cases PASS\n", argv[1]);
	return 0;
}
