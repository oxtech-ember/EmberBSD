/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted actual-source tests. */
/* Native allocation, DMA, mbuf and MMIO are explicit service boundaries. */
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "FAIL line %u: %s\n", (unsigned)__LINE__, #c); exit(1); \
} } while (0)
#define MCLBYTES 2048
#define ETHER_ALIGN 2
#define ETHER_HDR_LEN 14
#define ETHER_CRC_LEN 4
#define ETHER_VLAN_ENCAP_LEN 4
#define ETHER_ADDR_LEN 6
#define BUS_DMA_NOWAIT 1
#define BUS_DMA_COHERENT 2
#define BUS_DMA_ALLOCNOW 4
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_POSTWRITE 8

typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef void *device_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef void *pci_chipset_tag_t;
typedef unsigned int pcitag_t;
typedef unsigned int pci_intr_handle_t;
typedef unsigned int callout_t;
struct ifnet { void *if_softc; unsigned int if_mtu; };
struct ethercom { struct ifnet ec_if; unsigned int ec_capabilities; };
struct ifmedia { unsigned int dummy; };
struct mbuf;
typedef struct { bus_addr_t ds_addr; bus_size_t ds_len; } bus_dma_segment_t;
struct test_tag { unsigned int dummy; };
typedef struct test_tag *bus_dma_tag_t;
struct test_map {
	size_t dm_mapsize, capacity;
	int dm_nsegs, maxsegs;
	bus_dma_segment_t dm_segs[32];
	void *cpu;
	bool loaded;
};
typedef struct test_map *bus_dmamap_t;
#include "rge-header.inc"

static struct test_tag tag;
static struct test_map *maps[2 * RGE_RX_LIST_CNT + 8];
static unsigned int map_next, maps_live, alloc_live, kva_live, loaded_live;
static unsigned int step, fail_at;
struct memory { void *cpu; size_t size; bus_addr_t bus; bool allocated, mapped; };
static struct memory memory[2];
static bool failing(void) { return ++step == fail_at; }
static void aprint_error_dev(device_t d, const char *s) { (void)d; (void)s; }
static struct memory *find_mem(bus_addr_t addr)
{
	for (unsigned int i=0;i<2;i++)
		if (memory[i].allocated && memory[i].bus==addr) return &memory[i];
	CHECK(false); return NULL;
}
static int bus_dmamap_create(bus_dma_tag_t t, size_t n, int segs,
    size_t maxseg, size_t boundary, int flags, bus_dmamap_t *out)
{
	(void)flags;
	CHECK(t==&tag && n>0 && maxseg==n && boundary==0);
	CHECK(segs==1 || segs==32);
	if (failing()) { *out=(void *)(uintptr_t)0xbad; return ENOMEM; }
	CHECK(map_next<sizeof(maps)/sizeof(maps[0]));
	struct test_map *m=calloc(1,sizeof(*m)); CHECK(m!=NULL);
	m->capacity=n; m->maxsegs=segs; maps[map_next++]=m; maps_live++;
	*out=m; return 0;
}
static void bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t m)
{
	CHECK(t==&tag && m!=NULL && !m->loaded && maps_live>0);
	for (unsigned int i=0;i<map_next;i++) if (maps[i]==m) {
		maps[i]=NULL; maps_live--; free(m); return;
	}
	CHECK(false);
}
static int bus_dmamem_alloc(bus_dma_tag_t t, size_t n, size_t align,
    size_t boundary, bus_dma_segment_t *seg, int maxseg, int *nseg, int flags)
{
	CHECK(t==&tag && align==RGE_ALIGN && boundary==0 && maxseg==1 && flags==BUS_DMA_NOWAIT);
	if (failing()) return ENOMEM;
	for (unsigned int i=0;i<2;i++) if (!memory[i].allocated) {
		struct memory *p=&memory[i]; p->cpu=calloc(1,n); CHECK(p->cpu!=NULL);
		p->size=n; p->bus=0x100000+(bus_addr_t)i*0x100000;
		p->allocated=true; alloc_live++; *nseg=1;
		*seg=(bus_dma_segment_t){p->bus,n}; return 0;
	}
	CHECK(false); return ENOMEM;
}
static void bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *seg, int nseg)
{
	CHECK(t==&tag && nseg==1 && alloc_live>0);
	struct memory *p=find_mem(seg->ds_addr);
	CHECK(!p->mapped); free(p->cpu); memset(p,0,sizeof(*p)); alloc_live--;
}
static int bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *seg,
    int nseg, size_t n, void **out, int flags)
{
	CHECK(t==&tag && nseg==1 && flags==(BUS_DMA_NOWAIT|BUS_DMA_COHERENT));
	if (failing()) { *out=(void *)(uintptr_t)0xbad; return ENOMEM; }
	struct memory *p=find_mem(seg->ds_addr); CHECK(!p->mapped && p->size==n);
	p->mapped=true; kva_live++; *out=p->cpu; return 0;
}
static void bus_dmamem_unmap(bus_dma_tag_t t, void *cpu, size_t n)
{
	CHECK(t==&tag && kva_live>0);
	for (unsigned int i=0;i<2;i++) if (memory[i].mapped && memory[i].cpu==cpu) {
		CHECK(memory[i].size==n);
		for(unsigned int j=0;j<map_next;j++)
			if(maps[j]!=NULL) CHECK(!maps[j]->loaded || maps[j]->cpu!=cpu);
		memory[i].mapped=false; kva_live--; return;
	}
	CHECK(false); /* A map handle is not a CPU VA. */
}
static int bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t m, void *cpu,
    size_t n, void *process, int flags)
{
	CHECK(t==&tag && m!=NULL && !m->loaded && n==m->capacity && process==NULL && flags==BUS_DMA_NOWAIT);
	if (failing()) return ENOMEM;
	for(unsigned int i=0;i<2;i++) if(memory[i].cpu==cpu && memory[i].mapped) {
		m->cpu=cpu; m->loaded=true; m->dm_mapsize=n; m->dm_nsegs=1;
		m->dm_segs[0]=(bus_dma_segment_t){memory[i].bus,n}; loaded_live++; return 0;
	}
	CHECK(false); return ENOMEM;
}
static void bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t m)
{
	CHECK(t==&tag && m!=NULL && m->loaded && loaded_live>0);
	m->loaded=false; m->dm_mapsize=0; m->dm_nsegs=0; m->cpu=NULL; loaded_live--;
}
static int rge_deallocmem(struct rge_softc *);
#include "rge-source.inc"

static void fixture(struct rge_softc *sc, struct rge_queues *q)
{
	CHECK(maps_live==0 && alloc_live==0 && kva_live==0 && loaded_live==0);
	memset(sc,0,sizeof(*sc)); memset(q,0,sizeof(*q)); memset(maps,0,sizeof(maps));
	memset(memory,0,sizeof(memory)); map_next=step=fail_at=0;
	sc->sc_queues=q; sc->sc_dmat=&tag; sc->rge_type=MAC_R26_1; q->q_sc=sc;
}
int main(int argc, char **argv)
{
	struct rge_softc sc; struct rge_queues q;
	CHECK(argc==2);
	fixture(&sc,&q);
	if(strcmp(argv[1],"leak")==0) {
		fail_at=2; CHECK(rge_allocmem(&sc)==ENOMEM);
		CHECK(maps_live==0 && alloc_live==0 && kva_live==0 && loaded_live==0);
	} else {
		CHECK(rge_allocmem(&sc)==0);
		unsigned int calls=step;
		CHECK(rge_deallocmem(&sc)==0);
		CHECK(maps_live==0 && alloc_live==0 && kva_live==0 && loaded_live==0);
		CHECK(rge_deallocmem(&sc)==0);
		for(unsigned int at=1;at<=calls;at++) {
			fixture(&sc,&q); fail_at=at;
			CHECK(rge_allocmem(&sc)==ENOMEM);
			CHECK(maps_live==0 && alloc_live==0 && kva_live==0 && loaded_live==0);
			CHECK(rge_deallocmem(&sc)==0);
		}
	}
	printf("rge-rx actual source: %u checks, 0 failures\n",checks); return 0;
}
