/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; allocator fault and retained DMA controls. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef unsigned int u_int;
typedef uint64_t bus_addr_t;
typedef void *bus_dma_tag_t;
typedef struct { uint64_t ds_addr, ds_len; } bus_dma_segment_t;
typedef struct dma_map {
	int dm_nsegs;
	bus_dma_segment_t dm_segs[1];
} *bus_dmamap_t;
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define BUS_DMA_WAITOK 1
#define BUS_DMASYNC_PREWRITE 2
#define KM_SLEEP 1
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)
static int calls, fail_call, tags, allocations, cpus, maps, loads, syncs;
static int bad_alloc_segments, bad_map_segments, short_map, overlap;
static uint64_t address_override;
static int loaded_count;
static bool published_seen;
static void *kmem_zalloc(size_t n, int f)
{ (void)f; void *p = calloc(1,n); CHECK(p != NULL); return p; }
static void kmem_free(void *p, size_t n) { (void)n; free(p); }
static bool fails(void) { return ++calls == fail_call; }
static int bus_dmatag_subregion(bus_dma_tag_t p, uint64_t lo, uint64_t hi,
    bus_dma_tag_t *out, int flags)
{
	(void)p; (void)flags; CHECK(lo == 0 && hi == UINT32_MAX);
	if (fails()) return EIO;
	*out = malloc(1); CHECK(*out != NULL); tags++; return 0;
}
static void bus_dmatag_destroy(bus_dma_tag_t p) { CHECK(tags == 1); tags--; free(p); }
static int bus_dmamem_alloc(bus_dma_tag_t t, size_t n, size_t align,
    size_t boundary, bus_dma_segment_t *seg, int maxseg, int *count, int flags)
{
	(void)t; (void)flags; CHECK(align == 4096 && boundary == 0 && maxseg == 1);
	if (fails()) return EIO;
	seg->ds_len = n; seg->ds_addr = 0;
	*count = bad_alloc_segments ? 2 : 1; allocations++; return 0;
}
static void bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *s, int n)
{ (void)t; (void)s; CHECK(n > 0 && allocations > 0); allocations--; }
static int bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *s, int n,
    size_t bytes, void **out, int flags)
{
	(void)t; (void)s; (void)flags; CHECK(n == 1);
	if (fails()) return EIO;
	*out = malloc(bytes); CHECK(*out != NULL); memset(*out, 0xa5, bytes);
	cpus++; return 0;
}
static void bus_dmamem_unmap(bus_dma_tag_t t, void *p, size_t n)
{ (void)t; (void)n; CHECK(cpus > 0); cpus--; free(p); }
static int bus_dmamap_create(bus_dma_tag_t t, size_t n, int maxseg,
    size_t maxbytes, size_t boundary, int flags, bus_dmamap_t *out)
{
	(void)t; (void)flags; CHECK(n == maxbytes && maxseg == 1 && boundary == 0);
	if (fails()) return EIO;
	*out = calloc(1,sizeof(**out)); CHECK(*out != NULL); maps++; return 0;
}
static void bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t p)
{ (void)t; CHECK(maps > 0); maps--; free(p); }
static int bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t m, void *p,
    size_t n, void *proc, int flags)
{
	(void)t; (void)p; (void)proc; (void)flags;
	if (fails()) return EIO;
	m->dm_nsegs = bad_map_segments ? 2 : 1;
	m->dm_segs[0].ds_len = short_map ? n-4 : n;
	m->dm_segs[0].ds_addr = address_override ? address_override :
	    (loaded_count == 0 || overlap ? 0x100000 : 0x1000000);
	loaded_count++; loads++; return 0;
}
static void bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t p)
{ (void)t; (void)p; CHECK(loads > 0); loads--; }
static void bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t p,
    size_t off, size_t n, int flags)
{
	(void)t; CHECK(p != NULL && off == 0 && n == p->dm_segs[0].ds_len);
	CHECK(flags == BUS_DMASYNC_PREWRITE && !published_seen); syncs++;
}
#include "sun60i_a733_de352.h"
#include "sun60i_a733_de352.c"
#include "sun60i_a733_de352_dma.h"
#include "sun60i_a733_de352_dma.c"
static uint32_t get32(const void *vp)
{
	const uint8_t *p = vp;
	return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
	    (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void reset(void)
{
	CHECK(tags == 0 && allocations == 0 && cpus == 0 && maps == 0 && loads == 0);
	calls = fail_call = syncs = loaded_count = 0;
	bad_alloc_segments = bad_map_segments = short_map = overlap = 0;
	address_override = 0; published_seen = false;
}
static void reject(int expected)
{
	struct sun60i_a733_de352_dma *m = (void *)(uintptr_t)1;
	CHECK(sun60i_a733_de352_dma_prepare((void *)(uintptr_t)2,640,480,&m) == expected);
	CHECK(m == (void *)(uintptr_t)1 && syncs == 0);
	reset();
}
int main(void)
{
	struct sun60i_a733_de352_dma *m;
	reset();
	for (int i = 1; i <= 9; i++) { fail_call=i; reject(EIO); }
	bad_alloc_segments=1; reject(EINVAL);
	bad_map_segments=1; reject(EINVAL);
	short_map=1; reject(EINVAL);
	overlap=1; reject(EEXIST);
	address_override=UINT64_C(0x100000000); reject(ERANGE);
	address_override=UINT64_C(0xfffff000); reject(ERANGE);
	address_override=0x100004; reject(ERANGE);
	CHECK(sun60i_a733_de352_dma_prepare(NULL,640,480,&m) == EINVAL);
	CHECK(sun60i_a733_de352_dma_prepare((void *)1,0,480,&m) == EINVAL);
	CHECK(sun60i_a733_de352_dma_prepare((void *)1,1921,480,&m) == EINVAL);
	CHECK(sun60i_a733_de352_dma_prepare((void *)1,640,1081,&m) == EINVAL);
	CHECK(sun60i_a733_de352_dma_prepare((void *)1,640,480,NULL) == EINVAL);
	CHECK(calls == 0);
	CHECK(sun60i_a733_de352_dma_prepare((void *)1,640,480,&m) == 0);
	CHECK(m->pitch == 2560 && m->pixels.bytes == 1228800);
	CHECK(!m->published && !m->synchronized && syncs == 0);
	CHECK(get32(m->pixels.cpu) == 0xffffff);
	CHECK(get32((uint8_t *)m->pixels.cpu + 80*4) == 0xffff00);
	CHECK(get32((uint8_t *)m->pixels.cpu + 639*4) == 0);
	CHECK(get32((uint8_t *)m->pixels.cpu + 479*2560) == 0xffffff);
	CHECK(get32(m->queue.cpu) == 0x1000020);
	CHECK(get32((uint8_t *)m->queue.cpu+4) == 28);
	CHECK(get32((uint8_t *)m->queue.cpu+8) == 1);
	CHECK(get32((uint8_t *)m->queue.cpu+12) == 0x1c1000);
	CHECK(get32((uint8_t *)m->queue.cpu+16) == 0x1000040);
	CHECK(get32((uint8_t *)m->queue.cpu+20) == 4);
	CHECK(get32((uint8_t *)m->queue.cpu+28) == 0x281000);
	CHECK(get32((uint8_t *)m->queue.cpu+32) == 0xff000403);
	CHECK(get32((uint8_t *)m->queue.cpu+48) == 0x100000);
	CHECK(get32((uint8_t *)m->queue.cpu+64) == 0x100);
	for (size_t i=68;i<m->queue.bytes;i++) CHECK(((uint8_t *)m->queue.cpu)[i] == 0);
	CHECK(sun60i_a733_de352_dma_publish(m) == 0);
	CHECK(m->published && m->synchronized && syncs == 2);
	published_seen=true;
	CHECK(sun60i_a733_de352_dma_publish(m) == EBUSY && syncs == 2);
	CHECK(sun60i_a733_de352_dma_destroy(m) == EBUSY);
	CHECK(tags == 1 && allocations == 2 && loads == 2);
	/* Fixture models reboot after the retention assertion; production has no unpublish. */
	m->published=false;
	CHECK(sun60i_a733_de352_dma_destroy(m) == 0);
	reset();
	CHECK(sun60i_a733_de352_dma_prepare((void *)1,1,1,&m) == 0);
	CHECK(m->pitch == 64 && m->pixels.bytes == 4096);
	for (size_t i=4;i<m->pixels.bytes;i++) CHECK(((uint8_t *)m->pixels.cpu)[i] == 0);
	CHECK(sun60i_a733_de352_dma_destroy(m) == 0);
	reset();
	printf("A733 DE3.52 native DMA: %u checks passed\n",checks);
	return 0;
}
