/* SPDX-License-Identifier: BSD-2-Clause */
/* Reuse actual backing sources. Only controlled heap is reset between boots. */
#include <stdlib.h>
#include <stdio.h>
static void *boot_allocations[128];
static void *boot_track(void *p)
{
	if (p == NULL) abort();
	for (unsigned i = 0; i < 128; i++) {
		if (boot_allocations[i] == NULL) { boot_allocations[i] = p; return p; }
	}
	abort();
}
static void *boot_calloc(size_t n, size_t z) { return boot_track(calloc(n,z)); }
static void *boot_aligned(size_t a, size_t n) { return boot_track(aligned_alloc(a,n)); }
static void boot_free(void *p)
{
	for (unsigned i = 0; i < 128; i++) {
		if (boot_allocations[i] == p) { boot_allocations[i] = NULL; free(p); return; }
	}
	abort();
}
#define calloc boot_calloc
#define aligned_alloc boot_aligned
#define free boot_free
#include "bcmv3d-mmu4-native-backing.c"
#undef calloc
#undef aligned_alloc
#undef free

/* Simulated reboot only. No production destroy/unmap is invoked or added. */
void bootfb_backing_reboot(void)
{
	for (unsigned i = 0; i < 128; i++) { free(boot_allocations[i]); boot_allocations[i] = NULL; }
	memset(regions,0,sizeof(regions)); memset(region_maps,0,sizeof(region_maps));
	bo = NULL; consumer = NULL; current = NULL; allocations = maps = 0;
}
