/* Origin: EmberBSD; ARM short-descriptor DMA coherency regression. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pte.h"
#include "contract-defs.h"

typedef uint32_t vaddr_t;
typedef uint32_t paddr_t;
typedef unsigned int u_int;

#define PGSHIFT 12
#define PAGE_SIZE (1U << PGSHIFT)
#define PAGE_MASK (PAGE_SIZE - 1)
#define KDASSERT(x) assert(x)

struct l2_bucket {
	pt_entry_t *l2b_kva;
};
struct l2_dtable {
	struct l2_bucket l2_bucket[16];
};
struct pmap {
	pd_entry_t l1[4096];
	struct l2_dtable *pm_l2[256];
};
typedef struct pmap *pmap_t;

static struct pmap kernel, user;
static struct l2_dtable table;
static pt_entry_t entries[256];
static unsigned int preempt, locks, checks, failures;
static pt_entry_t pte_l1_s_nocache_mode;
static pt_entry_t pte_l2_l_nocache_mode;
static pt_entry_t pte_l2_s_nocache_mode;

#define pmap_kernel() (&kernel)
#define pmap_l1_kva(pm) ((pm)->l1)
#define kpreempt_disable() (++preempt)
#define kpreempt_enable() do { assert(preempt != 0); --preempt; } while (0)
#define pmap_acquire_pmap_lock(pm) (++locks)
#define pmap_release_pmap_lock(pm) do { assert(locks != 0); --locks; } while (0)

/* Compile the production function, including its lookup and kernel-only gate. */
#include "contract-function.h"

static void
check(bool ok, const char *name, unsigned int type, unsigned int texcb,
    unsigned int shared)
{

	++checks;
	if (ok)
		return;
	if (++failures <= 24)
		fprintf(stderr, "FAIL %s type=%u TEXCB=%u S=%u\n", name,
		    type, texcb, shared);
}

static void
reset(pmap_t pm, vaddr_t va)
{
	unsigned int slot = l1pte_index(va);

	memset(pm, 0, sizeof(*pm));
	memset(&table, 0, sizeof(table));
	memset(entries, 0, sizeof(entries));
	pm->pm_l2[L2_IDX(slot)] = &table;
	table.l2_bucket[L2_BUCKET(slot)].l2b_kva = entries;
	assert(preempt == 0 && locks == 0);
}

static void
mapping(pmap_t pm, unsigned int type, unsigned int texcb, unsigned int shared)
{
	const vaddr_t va = 0x12345abc;
	const paddr_t frame = 0x56000000;
	pt_entry_t attr, descriptor;
	paddr_t pa = 0, offset;
	bool coherent = false, expected;
	bool found;

	reset(pm, va);
	if (type >= 2) {
		attr = L1_S_XS_TEX(texcb >> 2) | ((texcb & 3) << 2);
		descriptor = frame | L1_TYPE_S | attr;
		if (shared != 0)
			descriptor |= L1_S_V6_S;
		if (type == 3)
			descriptor |= L1_S_V6_SUPER;
		pm->l1[l1pte_index(va)] = descriptor;
		offset = va & (type == 3 ? L1_SS_OFFSET : L1_S_OFFSET);
	} else {
		attr = (texcb & 3) << 2;
		attr |= type == 0 ? L2_XS_T_TEX(texcb >> 2) :
		    L2_XS_L_TEX(texcb >> 2);
		descriptor = frame | (type == 0 ? L2_TYPE_S : L2_TYPE_L) | attr;
		if (shared != 0)
			descriptor |= L2_XS_S;
		entries[l2pte_index(va)] = descriptor;
		offset = va & (type == 0 ? PAGE_MASK : L2_L_OFFSET);
	}
#if CONTRACT_EXTENDED
	/* ARMv6/v7: strong order and the selected normal-NC encoding only. */
	expected = texcb == 0 || texcb == 4;
#elif CONTRACT_XSCALE
	/* XScale: preserve the old predicate, including its X bit. */
	expected = (texcb & 7) == 0;
#else
	/* Legacy MMUs: only C/B are cache attributes, not AP/unused bits. */
	expected = (texcb & 3) == 0;
#endif
	if (pm != pmap_kernel())
		expected = false;
	found = pmap_extract_coherency(pm, va, &pa, &coherent);
	check(found, "mapping exists", type, texcb, shared);
	check(pa == (frame | offset), "physical address", type, texcb, shared);
	check(coherent == expected, "DMA coherency", type, texcb, shared);
	check(preempt == 0 && locks == 0, "balanced locking", type, texcb, shared);
	check(pmap_extract_coherency(pm, va, NULL, NULL), "optional outputs",
	    type, texcb, shared);
}

static void
missing(void)
{
	const vaddr_t va = 0x12345abc;
	paddr_t pa;
	bool coherent;
	unsigned int i;

	for (i = 0; i < 3; ++i) {
		reset(&kernel, va);
		if (i == 1)
			table.l2_bucket[L2_BUCKET(l1pte_index(va))].l2b_kva = NULL;
		if (i == 2)
			kernel.pm_l2[L2_IDX(l1pte_index(va))] = NULL;
		pa = 0xdeadbeef;
		coherent = true;
		check(!pmap_extract_coherency(&kernel, va, &pa, &coherent),
		    "absent mapping", i, 0, 0);
		check(pa == 0xdeadbeef && coherent,
		    "absent mapping leaves outputs", i, 0, 0);
		check(preempt == 0 && locks == 0, "absent balanced locking", i, 0, 0);
	}
}

int
main(void)
{
	unsigned int type, texcb, shared;

	/* Use the actual platform initialization assignments from pmap.c. */
#include "contract-init.h"
#if CONTRACT_EXTENDED
	assert(pte_l1_s_nocache_mode == L1_S_XS_TEX(1));
	assert(pte_l2_l_nocache_mode == L2_XS_L_TEX(1));
	assert(pte_l2_s_nocache_mode == L2_XS_T_TEX(1));
#else
	assert(pte_l1_s_nocache_mode == 0);
	assert(pte_l2_l_nocache_mode == 0);
	assert(pte_l2_s_nocache_mode == 0);
#endif
	for (type = 0; type < (CONTRACT_EXTENDED ? 4U : 3U); ++type)
		for (texcb = 0; texcb < 32; ++texcb)
			for (shared = 0; shared < 2; ++shared) {
				mapping(&kernel, type, texcb, shared);
				if (type < 2)
					mapping(&user, type, texcb, shared);
			}
	missing();
	printf("%s: %u checks, %u failures\n", CONTRACT_NAME, checks, failures);
	return failures != 0;
}
