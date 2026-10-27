/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the actual allocator with a bounded __morecore provider.
 * Public symbols are renamed by the runner so host libc keeps its allocator.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#ifndef GNUMALLOC_RALLOC_SOURCE
#define GNUMALLOC_RALLOC_SOURCE "../../external/gpl2/libmalloc/dist/ralloc.c"
#endif
#include GNUMALLOC_RALLOC_SOURCE

static _Alignas(65536) unsigned char arena[4 * 1024 * 1024];
static size_t used;
char etext;
void cfree(void *);

void *
__default_morecore(ptrdiff_t __size)
{
	void *old = arena + used;

	if (__size < 0) {
		if (__size < -(ptrdiff_t)used)
			return NULL;
		used -= (size_t)-__size;
	} else {
		if ((size_t)__size > sizeof(arena) - used)
			return NULL;
		used += (size_t)__size;
	}
	return old;
}

/* A negative int must reach the existing ptrdiff_t hook sign-extended. */
static void
morecore_contract(void)
{
	used = 128 * 1024;
	heap_base.start = heap_base.bloc_start = arena;
	heap_base.end = arena + used;
	heap_base.prev = heap_base.next = NIL_HEAP;
	first_heap = last_heap = &heap_base;
	break_value = arena + 32 * 1024;
	extra_bytes = 16 * 1024;
	page_size = 4096;
	real_morecore = __default_morecore;
	relinquish();
	assert(used == 48 * 1024);
	assert(heap_base.end == arena + used);
	used = 0;
}

static void
allocations(void)
{
	unsigned char *p = calloc(31, 17), *q;
	size_t i;

	assert(p != NULL);
	for (i = 0; i < 31 * 17; i++)
		assert(p[i] == 0);
	memset(p, 0x53, 31 * 17);
	q = realloc(p, 8192);
	assert(q != NULL);
	for (i = 0; i < 31 * 17; i++)
		assert(q[i] == 0x53);
	q = realloc(q, 53);
	assert(q != NULL);
	for (i = 0; i < 53; i++)
		assert(q[i] == 0x53);
	cfree(q);
	p = memalign(256, 513);
	assert(p != NULL && (uintptr_t)p % 256 == 0);
	memset(p, 0x72, 513);
	free(p);
	p = valloc(321);
	assert(p != NULL && (uintptr_t)p % (size_t)getpagesize() == 0);
	free(p);
	assert(malloc(sizeof(arena) * 2) == NULL);
}

int
main(void)
{
	morecore_contract();
	allocations();
	puts("PASS");
	return 0;
}
