/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted actual-source tests. */
/* Native allocation, DMA, mbuf and MMIO are explicit service boundaries. */
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "FAIL line %u: %s\n", (unsigned)__LINE__, #c); exit(1); \
} } while (0)
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define bswap16(x) __builtin_bswap16((uint16_t)(x))
#define ETHERCAP_VLAN_HWTAGGING 2U
#define __diagused
#define htole32(x) ((uint32_t)(x))
#define htole64(x) ((uint64_t)(x))
#define letoh32(x) ((uint32_t)(x))
#define KASSERTMSG(c,...) CHECK(c)
#define ETHERMTU 1500
#define ETHERMTU_JUMBO 9000
#define ETHERCAP_JUMBO_MTU 4U
#define IFF_RUNNING 1U
#define IFF_OACTIVE 2U
#define IFF_UP 4U
#define SIOCSIFFLAGS 1UL
#define M_DONTWAIT 0
#define MT_DATA 1
#define M_PKTHDR 1U
#define M_EXT 2U
#define M_EXT_RW 32U
#define M_EXTCOPYFLAGS (M_EXT|M_EXT_RW)
#define M_DEVBUF 0
#define M_NOWAIT 0
#define PAGE_SIZE 4096
#define round_page(n) (((n)+4095U)&~4095U)
#define KASSERT(c) CHECK(c)
#define mtod(m,t) ((t)(m)->m_data)
#define MHLEN 128
#define MLEN 128
#define MINCLSIZE 128
#define max_linkhdr 16
#define uimin(a,b) ((a)<(b)?(a):(b))
#define M_HASFCS 4U
#define M_CSUM_IPv4_BAD 8U
#define M_CSUM_TCP_UDP_BAD 16U
#define CLR(v,b) ((v)&=~(b))
#define CLLADDR(p) ((const uint8_t *)(p))
#define DELAY(n) ((void)(n))
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
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef void *device_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef void *pci_chipset_tag_t;
typedef unsigned int pcitag_t;
typedef unsigned int pci_intr_handle_t;
typedef unsigned int callout_t;
struct ifnet { void *if_softc, *if_sadl, *if_percpuq; unsigned int if_mtu, if_flags, if_capabilities; uint64_t if_ierrors, if_iqdrops; };
struct ethercom { struct ifnet ec_if; unsigned int ec_capabilities, ec_rx_mowner; };
struct ifmedia { unsigned int dummy; };
struct mbuf {
	struct mbuf *m_next;
	char *m_data;
	int m_len;
	unsigned int m_flags, vlan;
	struct { int len; unsigned int csum_flags; } m_pkthdr;
	struct { char *ext_buf; size_t ext_size; unsigned int ext_refcnt;
		void (*ext_free)(struct mbuf *,void *,size_t,void *); void *ext_arg;
	} m_ext;
	struct mbuf *m_ext_ref;
	unsigned int claims;
	char inline_data[256];
	struct ifnet *rcvif;
};

typedef struct { bus_addr_t ds_addr; bus_size_t ds_len; } bus_dma_segment_t;
struct test_tag { unsigned int dummy; };
typedef struct test_tag *bus_dma_tag_t;
struct test_map {
	size_t dm_mapsize, capacity;
	int dm_nsegs, maxsegs;
	bus_dma_segment_t dm_segs[32];
	void *cpu;
	bool loaded, data, armed;
	uint8_t *device;
};
typedef struct test_map *bus_dmamap_t;
#include "rge-header.inc"

static struct test_tag tag;
static struct test_map *maps[2 * RGE_RX_LIST_CNT + 8];
static unsigned int map_next, maps_live, alloc_live, kva_live, loaded_live;
static unsigned int step, fail_at;
static unsigned int mbufs, external, mbuf_calls, mbuf_fail_at, mbuf_fail_kind;
static unsigned int data_load_calls, data_fault_at, data_fault_kind;
static unsigned int received, chip_calls, enables, prefix_writes;
static struct mbuf *last_received;
static struct rge_softc *active_sc;
static uint16_t rxmax, eee_timer;
static unsigned int spl_entries, spl_exits;
static int ioctl_common_error;
static unsigned int diagnostic_calls;
static char diagnostic[160];

struct memory { void *cpu; size_t size; bus_addr_t bus; bool allocated, mapped; };
#define MEM_COUNT (RGE_RX_LIST_CNT+3)
static struct memory memory[MEM_COUNT];
static unsigned int owner_live, owner_calls, owner_fail_at, callback_calls;
static unsigned int headers_freed, copy_claims;
static bool copy_active;
static bool failing(void) { return ++step == fail_at; }
static void aprint_error_dev(device_t d, const char *s, ...)
{
	(void)d;
	va_list args;
	va_start(args,s);
	CHECK(vsnprintf(diagnostic,sizeof(diagnostic),s,args)>0);
	va_end(args);
	diagnostic_calls++;
}
static void check_init_diagnostic(unsigned int wanted_index,
    unsigned int wanted_size,int wanted_error)
{
	unsigned int index,size; int error;
	CHECK(diagnostic_calls==1);
	/* Numeric payload matters; prose is not an asserted protocol. */
	CHECK(sscanf(diagnostic,"%*[^0-9]%u%*[^0-9]%u%*[^0-9]%d",
	    &index,&size,&error)==3);
	CHECK(index==wanted_index && size==wanted_size && error==wanted_error);
}
static struct memory *find_mem(bus_addr_t addr)
{
	for (unsigned int i=0;i<MEM_COUNT;i++)
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
	CHECK(t==&tag && (align==RGE_ALIGN || align==PAGE_SIZE) && boundary==0 && maxseg==1 && flags==BUS_DMA_NOWAIT);
	if (failing()) return ENOMEM;
	for (unsigned int i=0;i<MEM_COUNT;i++) if (!memory[i].allocated) {
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
	CHECK(t==&tag && nseg==1 && (flags==BUS_DMA_NOWAIT || flags==(BUS_DMA_NOWAIT|BUS_DMA_COHERENT)));
	if (failing()) { *out=(void *)(uintptr_t)0xbad; return ENOMEM; }
	struct memory *p=find_mem(seg->ds_addr); CHECK(!p->mapped && p->size==n);
	p->mapped=true; kva_live++; *out=p->cpu; return 0;
}
static void bus_dmamem_unmap(bus_dma_tag_t t, void *cpu, size_t n)
{
	CHECK(t==&tag && kva_live>0);
	for (unsigned int i=0;i<MEM_COUNT;i++) if (memory[i].mapped && memory[i].cpu==cpu) {
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
	for(unsigned int i=0;i<MEM_COUNT;i++) if(memory[i].cpu==cpu && memory[i].mapped) {
		m->cpu=cpu; m->loaded=true; m->dm_mapsize=n; m->dm_nsegs=1;
		m->dm_segs[0]=(bus_dma_segment_t){memory[i].bus,n}; loaded_live++; return 0;
	}
	CHECK(false); return ENOMEM;
}
static void bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t m)
{
	CHECK(t==&tag && m!=NULL && m->loaded && loaded_live>0);
	if(m->data) {
		for(unsigned int i=0;i<MEM_COUNT;i++)
			if(memory[i].mapped && memory[i].cpu==m->cpu) CHECK(!m->armed);
		free(m->device); m->device=NULL;
	}
	m->armed=m->data=false; m->loaded=false; m->dm_mapsize=0; m->dm_nsegs=0; m->cpu=NULL; loaded_live--;
}
static struct mbuf *m_gethdr(int how,int type)
{
	CHECK(how==M_DONTWAIT && type==MT_DATA); mbuf_calls++;
	if(mbuf_calls==mbuf_fail_at && mbuf_fail_kind==1) return NULL;
	struct mbuf *m=calloc(1,sizeof(*m)); CHECK(m!=NULL);
	m->m_flags=M_PKTHDR; m->m_data=m->inline_data; m->m_ext.ext_size=128; mbufs++; return m;
}
static void extmalloc(struct mbuf *m,size_t n,int how)
{
	CHECK(m!=NULL && how==M_DONTWAIT && n>0);
	if(mbuf_calls==mbuf_fail_at && mbuf_fail_kind==2) return;
	m->m_ext.ext_buf=malloc(n); CHECK(m->m_ext.ext_buf!=NULL);
	memset(m->m_ext.ext_buf,0xcc,n); m->m_ext.ext_size=n;
	if(mbuf_calls==mbuf_fail_at && mbuf_fail_kind==3) m->m_ext.ext_size--;
	m->m_data=m->m_ext.ext_buf; m->m_flags|=M_EXT; external++;
}
#define MGETHDR(m,h,t) ((m)=m_gethdr((h),(t)))
#define MEXTMALLOC(m,n,h) extmalloc((m),(n),(h))
#define MCLAIM(m,o) do { CHECK((m)!=NULL); (void)(o); (m)->claims++; } while(0)
static void m_claimm(struct mbuf *m,unsigned int *owner)
{
	CHECK(owner==&active_sc->sc_ec.ec_rx_mowner);
	for(;m!=NULL;m=m->m_next) { MCLAIM(m,owner); copy_claims++; }
}
#define mowner_ref(m,f) do { CHECK((m)!=NULL && (f)==M_EXT); external++; } while(0)
#include "rge-mbuf.inc"
static const unsigned int mb_cache=1;
static void pool_cache_put(unsigned int cache,struct mbuf *m)
{
	CHECK(cache==mb_cache && m!=NULL && mbufs>0);
	headers_freed++; mbufs--; free(m);
}
static void m_freem(struct mbuf *m)
{
	while(m!=NULL) {
		struct mbuf *next=m->m_next; CHECK(mbufs>0);
		if(m->m_flags&M_EXT) { CHECK(external>0); external--;
			if(m->m_ext.ext_free!=NULL) {
				CHECK(m->m_ext.ext_refcnt==1 && m->m_ext_ref==m);
				callback_calls++;
				m->m_ext.ext_free(m,m->m_ext.ext_buf,m->m_ext.ext_size,m->m_ext.ext_arg);
				m=next; continue;
			}
			free(m->m_ext.ext_buf); }
		free(m); mbufs--; m=next;
	}
}
static struct mbuf *m_get(int how,int type)
{
	struct mbuf *m=m_gethdr(how,type);
	if(m!=NULL) m->m_flags=0;
	return m;
}
static struct mbuf *m_free(struct mbuf *m)
{
	struct mbuf *next=m->m_next;
	m->m_next=NULL; m_freem(m); return next;
}
#define MCLGET(m,h) extmalloc((m),MCLBYTES,(h))
static void m_set_rcvif(struct mbuf *m,struct ifnet *ifp) { m->rcvif=ifp; }
static void vlan_set_tag(struct mbuf *m,unsigned int tagvalue) { m->vlan=tagvalue; }
#define if_statinc(p,f) ((p)->f++)
static void if_percpuq_enqueue(void *queue,struct mbuf *m)
{
	CHECK(queue==active_sc && last_received==NULL && m->rcvif==&active_sc->sc_ec.ec_if);
	CHECK(m->m_pkthdr.len>=18 && (m->m_flags&M_HASFCS)!=0);
	size_t sum=0;
	for(const struct mbuf *part=m;part!=NULL;part=part->m_next) {
		CHECK(part->m_len>0 && (size_t)part->m_len<=part->m_ext.ext_size);
		CHECK(part->m_ext.ext_free==NULL);
		sum+=(size_t)part->m_len;
	}
	CHECK(sum==(size_t)m->m_pkthdr.len);
	last_received=m; received++;
}
static int bus_dmamap_load_mbuf(bus_dma_tag_t t,bus_dmamap_t m,struct mbuf *b,int flags)
{
	CHECK(t==&tag && m!=NULL && !m->loaded && flags==BUS_DMA_NOWAIT);
	CHECK(b->m_len>0 && b->m_pkthdr.len==b->m_len);
	CHECK((size_t)b->m_len<=b->m_ext.ext_size && (size_t)b->m_len<=m->capacity);
	data_load_calls++;
	unsigned int fault=data_load_calls==data_fault_at ? data_fault_kind : 0;
	if(fault==1 || fault==2) return fault==1 ? ENOMEM : EFBIG;
	/* Independent physical page walk: ordinary malloc VA is not PA-contiguous.
	 * Registered DMA allocations have an explicit contiguous physical extent. */
	bus_addr_t previous=0;
	unsigned int segments=0;
	for(size_t off=0;off<(size_t)b->m_len;off+=4096) {
		bus_addr_t physical=0x400000000ULL+off*2;
		for(unsigned int j=0;j<MEM_COUNT;j++)
			if(memory[j].mapped && memory[j].cpu==b->m_data)
				physical=memory[j].bus+off;
		if(off==0 || physical!=previous+4096) segments++;
		if(segments>(unsigned int)m->maxsegs) return EFBIG;
		previous=physical;
	}
	m->loaded=m->data=true; m->cpu=b->m_data;
 m->dm_mapsize=b->m_len;
	m->dm_nsegs=fault==3 ? 2 : 1;
	m->dm_segs[0]=(bus_dma_segment_t){(bus_addr_t)(uintptr_t)m+0x100000000ULL,b->m_len};
	m->device=calloc(1,b->m_len); CHECK(m->device!=NULL); loaded_live++;
	if(fault==4) m->dm_mapsize--;
	if(fault==5) m->dm_segs[0].ds_len--;
	return 0;
}
static void bus_dmamap_sync(bus_dma_tag_t t,bus_dmamap_t map,size_t off,size_t len,int ops)
{
	CHECK(t==&tag && map!=NULL && map->loaded && off<=map->dm_mapsize && len<=map->dm_mapsize-off);
	if(map->data) {
		CHECK(off==0 && len==map->dm_mapsize);
		if(ops==BUS_DMASYNC_POSTREAD) {
			CHECK(map->armed); memcpy(map->cpu,map->device,len); map->armed=false;
		} else if(ops==BUS_DMASYNC_PREREAD) { CHECK(!copy_active); map->armed=true; }
		else CHECK(false);
	} else if(active_sc!=NULL && map==active_sc->sc_queues->q_rx.rge_rx_list_map &&
	    ops==(BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE)) {
		struct rge_queues *q=active_sc->sc_queues;
		CHECK(off%sizeof(struct rge_rx_desc)==0 && len==sizeof(struct rge_rx_desc));
		unsigned int i=off/sizeof(struct rge_rx_desc);
		struct rge_rx_desc *r=&q->q_rx.rge_rx_list[i];
		struct test_map *data=q->q_rx.rge_rxq[i].rxq_dmamap;
		CHECK(data!=NULL && data->loaded && data->armed && data->dm_nsegs==1);
		CHECK((r->hi_qword1.rx_qword4.rge_cmdsts&RGE_RDCMDSTS_FRAGLEN)==data->dm_mapsize);
		CHECK(r->hi_qword0.rge_addr==data->dm_segs[0].ds_addr);
		CHECK(!!(r->hi_qword1.rx_qword4.rge_cmdsts&RGE_RDCMDSTS_EOR)==(i==RGE_RX_LIST_CNT-1));
	}
}
static void bus_space_write_1(bus_space_tag_t t,bus_space_handle_t h,unsigned int off,uint8_t v)
{ (void)t;(void)h; if(off==RGE_CMD && (v&(RGE_CMD_TXENB|RGE_CMD_RXENB))) enables++; }
static void bus_space_write_2(bus_space_tag_t t,bus_space_handle_t h,unsigned int off,uint16_t v)
{ (void)t;(void)h; if(off==RGE_RXMAXSIZE) rxmax=v; if(off==RGE_EEE_TXIDLE_TIMER) eee_timer=v; }
static void bus_space_write_4(bus_space_tag_t t,bus_space_handle_t h,unsigned int off,uint32_t v)
{ (void)t;(void)h;(void)off;(void)v; }
static uint8_t bus_space_read_1(bus_space_tag_t t,bus_space_handle_t h,unsigned int off)
{ (void)t;(void)h;(void)off;return 0; }
static uint16_t bus_space_read_2(bus_space_tag_t t,bus_space_handle_t h,unsigned int off)
{ (void)t;(void)h;(void)off;return 0; }
static uint32_t bus_space_read_4(bus_space_tag_t t,bus_space_handle_t h,unsigned int off)
{ (void)t;(void)h;(void)off;return 0; }
#include "rge-devget.inc"
static struct mbuf *copy_devget(char *buf,int len,int off,struct ifnet *ifp)
{
	CHECK(!copy_active); copy_active=true;
	bool found=false;
	for(unsigned int i=0;i<map_next;i++)
		if(maps[i]!=NULL && maps[i]->loaded && maps[i]->cpu==buf) {
			CHECK(maps[i]->data && !maps[i]->armed); found=true;
		}
	CHECK(found && len>=18 && len<=9216 && off==0);
	struct mbuf *m=m_devget(buf,len,off,ifp);
	copy_active=false; return m;
}
static void *owner_alloc(size_t n,int type,int flags)
{
	CHECK(type==M_DEVBUF && flags==M_NOWAIT); owner_calls++;
	if(owner_calls==owner_fail_at) return NULL;
	void *p=malloc(n); CHECK(p!=NULL); owner_live++; return p;
}
static void owner_free(void *p,int type)
{
	CHECK(type==M_DEVBUF && p!=NULL && owner_live>0); owner_live--; free(p);
}
static void rge_rx_list_fini(struct rge_queues *);
static void rge_stop(struct ifnet *p,int disable)
{ (void)disable; p->if_flags&=~IFF_RUNNING; prefix_writes++; rge_rx_list_fini(active_sc->sc_queues); }
static void rge_set_macaddr(struct rge_softc *sc,const uint8_t *mac)
{ CHECK(sc==active_sc);(void)mac;prefix_writes++; }
static int rge_chipinit(struct rge_softc *sc) { CHECK(sc==active_sc);chip_calls++;return 0; }
static int rge_phy_config(struct rge_softc *sc) { CHECK(sc==active_sc);return 0; }
static void rge_disable_aspm_clkreq(struct rge_softc *sc) { CHECK(sc==active_sc); }
static uint32_t rge_read_csi(struct rge_softc *sc,uint32_t off) { CHECK(sc==active_sc);(void)off;return 0; }
static void rge_write_csi(struct rge_softc *sc,uint32_t off,uint32_t v) { CHECK(sc==active_sc);(void)off;(void)v; }
static uint16_t rge_read_mac_ocp(struct rge_softc *sc,uint16_t off) { CHECK(sc==active_sc);(void)off;return 0; }
static void rge_write_mac_ocp(struct rge_softc *sc,uint16_t off,uint16_t v) { CHECK(sc==active_sc);(void)off;(void)v; }
static void rge_write_phy_ocp(struct rge_softc *sc,uint16_t off,uint16_t v) { CHECK(sc==active_sc);(void)off;(void)v; }
static void rge_iff(struct rge_softc *sc) { CHECK(sc==active_sc); }
static int splnet(void) { spl_entries++; return 7; }
static void splx(int level) { CHECK(level==7); spl_exits++; }
static int ifioctl_common(struct ifnet *p,u_long cmd,void *data)
{ CHECK(p==&active_sc->sc_ec.ec_if && cmd==SIOCSIFFLAGS && data==NULL); return ioctl_common_error; }
static int ether_ioctl(struct ifnet *p,u_long cmd,void *data)
{ CHECK(p==&active_sc->sc_ec.ec_if && cmd!=SIOCSIFFLAGS && data==NULL); return EOPNOTSUPP; }
static int rge_ifmedia_upd(struct ifnet *p) { CHECK(p==&active_sc->sc_ec.ec_if);return 0; }
static void rge_setup_intr(struct rge_softc *sc,int type) { CHECK(sc==active_sc && type==RGE_IMTYPE_SIM); }
static void callout_schedule(callout_t *c,unsigned int n) { CHECK(c==&active_sc->sc_timeout && n==1); }
static int rge_deallocmem(struct rge_softc *);
static void rge_rx_list_fini(struct rge_queues *);
#define malloc(n,t,f) owner_alloc((n),(t),(f))
#define free(p,t) owner_free((p),(t))
#define m_devget copy_devget
#include "rge-source.inc"
#undef m_devget
#undef free
#undef malloc

static void fixture(struct rge_softc *sc, struct rge_queues *q)
{
	CHECK(maps_live==0 && alloc_live==0 && kva_live==0 && loaded_live==0);
	memset(sc,0,sizeof(*sc)); memset(q,0,sizeof(*q)); memset(maps,0,sizeof(maps));
	memset(memory,0,sizeof(memory)); map_next=step=fail_at=0;
	sc->sc_queues=q; sc->sc_dmat=&tag; sc->rge_type=MAC_R26_1; q->q_sc=sc;
	CHECK(mbufs==0 && external==0 && owner_live==0 && last_received==NULL);
	mbuf_calls=mbuf_fail_at=mbuf_fail_kind=data_load_calls=data_fault_at=data_fault_kind=0;
	received=chip_calls=enables=prefix_writes=0; rxmax=eee_timer=0; active_sc=sc;
	spl_entries=spl_exits=0; ioctl_common_error=0;
	owner_calls=owner_fail_at=callback_calls=headers_freed=copy_claims=0; copy_active=false;
	diagnostic_calls=0; diagnostic[0]='\0';
	sc->sc_ec.ec_if.if_softc=sc; sc->sc_ec.ec_if.if_mtu=1500; sc->sc_ec.ec_if.if_percpuq=sc;
	CHECK(rge_jumbo_capabilities(sc)==ETHERCAP_JUMBO_MTU);
}
static void rx_tests(const char *mode)
{
	struct rge_softc sc; struct rge_queues q;
	fixture(&sc,&q); CHECK(rge_allocmem(&sc)==0);
	if(strcmp(mode,"init-failure")==0) {
		mbuf_fail_at=1; mbuf_fail_kind=1;
		CHECK(rge_init(&sc.sc_ec.ec_if)==ENOBUFS);
		CHECK(prefix_writes==2 && chip_calls==0 && enables==0 && !(sc.sc_ec.ec_if.if_flags&IFF_RUNNING));
	} else if(strcmp(mode,"retain")==0) {
		CHECK(rge_rx_list_init(&q)==0);
		struct mbuf *old=q.q_rx.rge_rxq[0].rxq_mbuf;
		bus_dmamap_t map=q.q_rx.rge_rxq[0].rxq_dmamap;
		bus_dmamap_sync(&tag,map,0,map->dm_mapsize,BUS_DMASYNC_POSTREAD);
		data_fault_at=data_load_calls+1; data_fault_kind=1;
		CHECK(rge_newbuf(&q,0)==ENOMEM);
		CHECK(q.q_rx.rge_rxq[0].rxq_mbuf==old && q.q_rx.rge_rxq[0].rxq_dmamap==map && map->loaded);
	} else {
		sc.sc_ec.ec_if.if_mtu=9000;
		CHECK(rge_init(&sc.sc_ec.ec_if)==0);
		CHECK(q.q_rx.rge_rxq[0].rxq_mbuf->m_ext.ext_size>=9216 && rxmax==9216);
		CHECK(eee_timer==2070);
		struct test_map *map=q.q_rx.rge_rxq[0].rxq_dmamap;
		CHECK(map->dm_mapsize==9216 && map->armed);
		for(unsigned int i=0;i<9022;i++) map->device[i]=(uint8_t)(i*17+3);
		q.q_rx.rge_rx_list[0].hi_qword1.rx_qword4.rge_cmdsts=RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF|9022;
		CHECK(rge_rxeof(&sc)!=0 && received==1 && last_received->m_pkthdr.len==9022);
		unsigned int position=0;
		for(const struct mbuf *part=last_received;part!=NULL;part=part->m_next)
			for(int j=0;j<part->m_len;j++,position++)
				CHECK((uint8_t)part->m_data[j]==(uint8_t)(position*17+3));
		CHECK(position==9022 && copy_claims==5);
		m_freem(last_received); last_received=NULL;
	}
	rge_rx_list_fini(&q); CHECK(mbufs==0 && external==0);
	CHECK(rge_deallocmem(&sc)==0 && loaded_live==0 && maps_live==0);
}

static void rx_init_failures(void)
{
	struct rge_softc sc; struct rge_queues q;
	fixture(&sc,&q); CHECK(rge_allocmem(&sc)==0);
	for(unsigned int kind=1;kind<=8;kind++) {
		for(unsigned int at=1;at<=RGE_RX_LIST_CNT;at++) {
			mbuf_calls=data_load_calls=0;
			mbuf_fail_at=mbuf_fail_kind=data_fault_at=data_fault_kind=0;
			if(kind<=3) { mbuf_fail_at=at; mbuf_fail_kind=kind; }
			else { data_fault_at=at; data_fault_kind=kind-3; }
			prefix_writes=chip_calls=enables=diagnostic_calls=0;
			int expected=kind<=3 ? ENOBUFS : (kind==4 ? ENOMEM : EFBIG);
			CHECK(rge_init(&sc.sc_ec.ec_if)==expected);
			CHECK(prefix_writes==2 && chip_calls==0 && enables==0);
			CHECK(!(sc.sc_ec.ec_if.if_flags&IFF_RUNNING));
			CHECK(mbufs==0 && external==0 && loaded_live==2);
			CHECK(!q.q_rx.rge_rx_sparemap->loaded);
			check_init_diagnostic(at-1,2046,expected);
		}
	}
	mbuf_fail_at=data_fault_at=0;
	diagnostic_calls=0;
	CHECK(rge_init(&sc.sc_ec.ec_if)==0);
	CHECK(diagnostic_calls==0);
	rge_stop(&sc.sc_ec.ec_if,1);
	CHECK(mbufs==0 && external==0 && loaded_live==2);
	CHECK(rge_deallocmem(&sc)==0 && loaded_live==0 && maps_live==0);
}

static void receive_one(struct rge_softc *sc,unsigned int len,uint32_t flags,
    bool accepted)
{
	struct rge_queues *q=sc->sc_queues;
	unsigned int idx=q->q_rx.rge_rxq_considx;
	struct test_map *map=q->q_rx.rge_rxq[idx].rxq_dmamap;
	struct mbuf *old=q->q_rx.rge_rxq[idx].rxq_mbuf;
	unsigned int before=received;
	CHECK(map->armed && map->loaded);
	if(len<=map->dm_mapsize)
		for(unsigned int i=0;i<len;i++) map->device[i]=(uint8_t)(i*17+3);
	q->q_rx.rge_rx_list[idx].hi_qword1.rx_qword4.rge_cmdsts=flags|len|
	    (idx==RGE_RX_LIST_CNT-1 ? RGE_RDCMDSTS_EOR : 0);
	CHECK(rge_rxeof(sc)==1);
	CHECK(q->q_rx.rge_rxq_considx==(int)RGE_NEXT_RX_DESC(idx));
	struct test_map *newmap=q->q_rx.rge_rxq[idx].rxq_dmamap;
	CHECK(newmap->armed && newmap->loaded && !q->q_rx.rge_rx_sparemap->loaded);
	if(accepted) {
		CHECK(received==before+1 && last_received->m_pkthdr.len==(int)len);
		if(map->dm_mapsize==9216) {
			CHECK(last_received!=old && q->q_rx.rge_rxq[idx].rxq_mbuf==old && newmap==map);
			CHECK(last_received->m_len<=(int)MCLBYTES);
		} else CHECK(last_received==old);
		unsigned int position=0;
		for(const struct mbuf *part=last_received;part!=NULL;part=part->m_next)
			for(int j=0;j<part->m_len;j++,position++)
				CHECK((uint8_t)part->m_data[j]==(uint8_t)(position*17+3));
		CHECK(position==len);
		m_freem(last_received); last_received=NULL;
	} else {
		CHECK(received==before && last_received==NULL);
		CHECK(q->q_rx.rge_rxq[idx].rxq_mbuf==old && newmap==map);
	}
}

static void rx_geometry_and_reuse(void)
{
	const enum rge_mac_type types[]={MAC_R25,MAC_R25B,MAC_R25D_1,MAC_R25D_2,
	    MAC_R26_1,MAC_R26_2,MAC_R27};
	const uint32_t whole=RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF;
	struct rge_softc sc; struct rge_queues q;
	for(unsigned int t=0;t<sizeof(types)/sizeof(types[0]);t++) {
		fixture(&sc,&q); sc.rge_type=types[t];
		bool jumbo=types[t]==MAC_R26_1 || types[t]==MAC_R26_2;
		CHECK(rge_jumbo_capabilities(&sc)==(jumbo ? ETHERCAP_JUMBO_MTU : 0));
		CHECK(rge_frame_limit(&sc)==(jumbo ? 9216U : 2046U));
		CHECK(rge_allocmem(&sc)==0);
		for(unsigned int i=0;i<RGE_RX_LIST_CNT;i++) CHECK(q.q_rx.rge_rxq[i].rxq_dmamap->capacity==(jumbo ? 9216U : 2046U));
		for(unsigned int i=0;i<RGE_TX_LIST_CNT;i++) CHECK(q.q_tx.rge_txq[i].txq_dmamap->capacity==(jumbo ? 9216U : 2046U));
		CHECK(rge_init(&sc.sc_ec.ec_if)==0 && rxmax==2046 && eee_timer==2070);
		receive_one(&sc,1518,whole,true);
		receive_one(&sc,2047,whole,false);
		CHECK(sc.sc_ec.ec_if.if_ierrors==1);
		unsigned int rounds=jumbo ? 3 : 1;
		for(unsigned int round=0;round<rounds;round++) {
			sc.sc_ec.ec_if.if_mtu=round==0 ? 9000 : (round==1 ? 1501 : 1500);
			prefix_writes=chip_calls=enables=0;
			if(!jumbo) {
				CHECK(rge_init(&sc.sc_ec.ec_if)==EINVAL);
				CHECK(prefix_writes==1 && chip_calls==0 && enables==0 && mbufs==0);
				break;
			}
			unsigned int size=round<2 ? 9216 : 2046;
			CHECK(rge_init(&sc.sc_ec.ec_if)==0 && rxmax==size && eee_timer==2070);
			CHECK(q.q_rx.rge_rxq[0].rxq_mbuf->m_ext.ext_size==size);
			receive_one(&sc,size==9216 ? 9022 : 1518,whole,true);
			unsigned int errors=sc.sc_ec.ec_if.if_ierrors;
			const unsigned int lengths[]={0,1,17,size+1};
			for(unsigned int i=0;i<4;i++) receive_one(&sc,lengths[i],whole,false);
			receive_one(&sc,1518,RGE_RDCMDSTS_SOF,false);
			receive_one(&sc,1518,RGE_RDCMDSTS_EOF,false);
			receive_one(&sc,1518,whole|RGE_RDCMDSTS_RXERRSUM,false);
			CHECK(sc.sc_ec.ec_if.if_ierrors==errors+7);
			for(unsigned int kind=1;kind<=(size==9216 ? 2U : 8U);kind++) {
				mbuf_fail_at=mbuf_fail_kind=data_fault_at=data_fault_kind=0;
				if(kind<=3) { mbuf_fail_at=mbuf_calls+1;mbuf_fail_kind=kind; }
				else { data_fault_at=data_load_calls+1;data_fault_kind=kind-3; }
				unsigned int drops=sc.sc_ec.ec_if.if_iqdrops;
				receive_one(&sc,1518,whole,false);
				CHECK(sc.sc_ec.ec_if.if_iqdrops==drops+1);
			}
			mbuf_fail_at=data_fault_at=0;
			for(unsigned int i=0;i<RGE_RX_LIST_CNT+3;i++) receive_one(&sc,18,whole,true);
			CHECK(diagnostic_calls==0); /* No per-packet failure log flood. */
		}
		sc.sc_ec.ec_if.if_mtu=9001; prefix_writes=chip_calls=enables=0;
		CHECK(rge_init(&sc.sc_ec.ec_if)==EINVAL && prefix_writes==1 && chip_calls==0 && enables==0);
		CHECK(mbufs==0 && external==0);
		CHECK(rge_deallocmem(&sc)==0 && maps_live==0 && loaded_live==0);
		CHECK(rge_deallocmem(&sc)==0);
	}
}

static void rx_ioctl_failure(void)
{
	struct rge_softc sc; struct rge_queues q;
	fixture(&sc,&q); CHECK(rge_allocmem(&sc)==0);
	struct ifnet *p=&sc.sc_ec.ec_if;
	p->if_flags=IFF_UP; ioctl_common_error=EINTR;
	CHECK(rge_ioctl(p,SIOCSIFFLAGS,NULL)==EINTR);
	CHECK(prefix_writes==0 && chip_calls==0 && enables==0);
	ioctl_common_error=0; mbuf_fail_at=1; mbuf_fail_kind=1;
	CHECK(rge_ioctl(p,SIOCSIFFLAGS,NULL)==ENOBUFS);
	CHECK(prefix_writes==2 && chip_calls==0 && enables==0 && !(p->if_flags&IFF_RUNNING));
	CHECK(mbufs==0 && external==0);
	mbuf_fail_at=0;
	CHECK(rge_ioctl(p,SIOCSIFFLAGS,NULL)==0 && (p->if_flags&IFF_RUNNING));
	CHECK(chip_calls==1 && enables==1);
	CHECK(rge_ioctl(p,SIOCSIFFLAGS,NULL)==0 && chip_calls==1 && enables==1);
	p->if_flags&=~IFF_UP;
	CHECK(rge_ioctl(p,SIOCSIFFLAGS,NULL)==0 && !(p->if_flags&IFF_RUNNING));
	CHECK(rge_ioctl(p,2,NULL)==EOPNOTSUPP);
	CHECK(spl_entries==6 && spl_exits==spl_entries);
	CHECK(mbufs==0 && external==0);
	CHECK(rge_deallocmem(&sc)==0 && maps_live==0 && loaded_live==0);
}

static void rx_init_map_error(void)
{
	struct rge_softc sc; struct rge_queues q;
	fixture(&sc,&q); CHECK(rge_allocmem(&sc)==0);
	sc.sc_ec.ec_if.if_mtu=9000;
	data_fault_at=18; data_fault_kind=1;
	CHECK(rge_init(&sc.sc_ec.ec_if)==ENOMEM);
	CHECK(prefix_writes==2 && chip_calls==0 && enables==0);
	CHECK(!(sc.sc_ec.ec_if.if_flags&IFF_RUNNING));
	CHECK(mbufs==0 && external==0 && loaded_live==2);
	CHECK(!q.q_rx.rge_rx_sparemap->loaded);
	check_init_diagnostic(17,9216,ENOMEM);
	CHECK(rge_deallocmem(&sc)==0 && maps_live==0 && loaded_live==0);
}
/* Every constructor/load failure at every descriptor slot preserves ownership. */
static void
jumbo_faults(void)
{
	struct rge_softc sc; struct rge_queues q;
	fixture(&sc,&q); CHECK(rge_allocmem(&sc)==0);
	sc.sc_ec.ec_if.if_mtu=9000;
	for(unsigned int i=0;i<RGE_RX_LIST_CNT;i++) {
		for(unsigned int kind=0;kind<9;kind++) {
			owner_fail_at=mbuf_fail_at=data_fault_at=fail_at=0;
			if(kind==0) owner_fail_at=owner_calls+1;
			if(kind==1) { mbuf_fail_at=mbuf_calls+1;mbuf_fail_kind=1; }
			if(kind==2 || kind==3) fail_at=step+kind-1;
			if(kind>=4) { data_fault_at=data_load_calls+1;data_fault_kind=kind-3; }
			int expected=kind==1 ? ENOBUFS : (kind<=4 ? ENOMEM : EFBIG);
			bus_dmamap_t spare=q.q_rx.rge_rx_sparemap;
			CHECK(rge_newbuf(&q,i)==expected);
			CHECK(q.q_rx.rge_rxq[i].rxq_mbuf==NULL && q.q_rx.rge_rx_sparemap==spare);
			CHECK(!spare->loaded && loaded_live==2 && owner_live==0);
			CHECK(alloc_live==2 && kva_live==2 && mbufs==0 && external==0);
		}
	}
	owner_fail_at=mbuf_fail_at=data_fault_at=fail_at=0;
	CHECK(rge_deallocmem(&sc)==0 && alloc_live==0 && kva_live==0);
}

static void
jumbo_copy_lifetime(void)
{
	struct rge_softc sc; struct rge_queues q;
	fixture(&sc,&q); CHECK(rge_allocmem(&sc)==0);
	sc.sc_ec.ec_if.if_mtu=9000;
	CHECK(rge_init(&sc.sc_ec.ec_if)==0);
	CHECK(owner_live==1024 && alloc_live==1026 && kva_live==1026);
	const struct mbuf *internal=q.q_rx.rge_rxq[0].rxq_mbuf;
	struct test_map *map=q.q_rx.rge_rxq[0].rxq_dmamap;
	for(unsigned int i=0;i<9216;i++) map->device[i]=(uint8_t)(i*31+7);
	q.q_rx.rge_rx_list[0].hi_qword1.rx_qword4.rge_cmdsts=
	    RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF|9216;
	q.q_rx.rge_rx_list[0].hi_qword1.rx_qword4.rge_extsts=
	    RGE_RDEXTSTS_IPV4|RGE_RDEXTSTS_IPCSUMERR|RGE_RDEXTSTS_VTAG|0x1234;
	unsigned int loads=data_load_calls,owners=owner_calls;
	CHECK(rge_rxeof(&sc)==1 && received==1);
	CHECK(data_load_calls==loads && owner_calls==owners && callback_calls==0);
	CHECK(q.q_rx.rge_rxq[0].rxq_mbuf==internal && map->armed);
	CHECK(last_received->m_pkthdr.len==9216 && last_received->m_len==2048);
	CHECK(last_received->m_pkthdr.csum_flags==M_CSUM_IPv4_BAD);
	CHECK(last_received->vlan==bswap16(0x1234));
	memset(map->cpu,0xa5,9216); memset(map->device,0xa5,9216);
	/* Disposal must use callback identity, even if interface policy changed. */
	sc.sc_ec.ec_if.if_mtu=1500;
	rge_stop(&sc.sc_ec.ec_if,1);
	CHECK(owner_live==0 && alloc_live==2 && callback_calls==1024 && headers_freed==1024);
	CHECK(mbufs==5 && external==5 && copy_claims==5);
	CHECK(rge_deallocmem(&sc)==0 && alloc_live==0 && loaded_live==0 && maps_live==0);
	unsigned int position=0;
	for(const struct mbuf *part=last_received;part!=NULL;part=part->m_next) {
		CHECK(part->claims==1 && part->m_ext.ext_free==NULL);
		for(int j=0;j<part->m_len;j++,position++)
			CHECK((uint8_t)part->m_data[j]==(uint8_t)(position*31+7));
	}
	CHECK(position==9216);
	m_freem(last_received); last_received=NULL;
	CHECK(mbufs==0 && external==0 && owner_live==0);
}

static void
jumbo_copy_failures(void)
{
	struct rge_softc sc; struct rge_queues q;
	fixture(&sc,&q); CHECK(rge_allocmem(&sc)==0);
	sc.sc_ec.ec_if.if_mtu=9000; CHECK(rge_init(&sc.sc_ec.ec_if)==0);
	for(unsigned int kind=1;kind<=2;kind++) {
		for(unsigned int at=1;at<=5;at++) {
			unsigned int idx=q.q_rx.rge_rxq_considx;
			const struct mbuf *internal=q.q_rx.rge_rxq[idx].rxq_mbuf;
			const struct test_map *map=q.q_rx.rge_rxq[idx].rxq_dmamap;
			unsigned int drops=sc.sc_ec.ec_if.if_iqdrops;
			unsigned int loads=data_load_calls,owners=owner_calls;
			mbuf_fail_kind=kind; mbuf_fail_at=mbuf_calls+at;
			receive_one(&sc,9216,RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF,false);
			CHECK(sc.sc_ec.ec_if.if_iqdrops==drops+1);
			CHECK(data_load_calls==loads && owner_calls==owners && callback_calls==0);
			CHECK(q.q_rx.rge_rxq[idx].rxq_mbuf==internal && map->armed);
			CHECK(mbufs==1024 && external==1024 && owner_live==1024);
		}
	}
	mbuf_fail_at=0;
	rge_stop(&sc.sc_ec.ec_if,1);
	CHECK(callback_calls==1024 && headers_freed==1024 && owner_live==0);
	CHECK(rge_deallocmem(&sc)==0 && alloc_live==0 && loaded_live==0);
}

int main(int argc, char **argv)

{
	struct rge_softc sc; struct rge_queues q;
	CHECK(argc==2);
	if(strcmp(argv[1],"init-map-error")==0) {
		rx_init_map_error(); printf("rge-rx: %u checks, 0 failures\n",checks);return 0;
	}
	if(strcmp(argv[1],"jumbo")==0 || strcmp(argv[1],"init-failure")==0 || strcmp(argv[1],"retain")==0) {
		rx_tests(argv[1]); printf("rge-rx: %u checks, 0 failures\n",checks);return 0;
	}
	if(strcmp(argv[1],"rx")==0) {
		rx_init_failures(); rx_geometry_and_reuse();
		printf("rge-rx: %u checks, 0 failures\n",checks); return 0;
	}
	if(strcmp(argv[1],"ioctl")==0) {
		rx_ioctl_failure(); printf("rge-rx: %u checks, 0 failures\n",checks); return 0;
	}
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
	if(strcmp(argv[1],"copyout")==0) {
		jumbo_faults(); jumbo_copy_lifetime(); jumbo_copy_failures();
	}
	if(strcmp(argv[1],"all")==0) {
		jumbo_faults(); jumbo_copy_lifetime(); jumbo_copy_failures();
		rx_tests("jumbo"); rx_tests("init-failure"); rx_tests("retain");
		rx_init_failures(); rx_geometry_and_reuse();
		rx_ioctl_failure();
		rx_init_map_error();
	}
	printf("rge-rx actual source: %u checks, 0 failures\n",checks); return 0;
}
