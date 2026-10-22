/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8822CE actual-source resource tests, AI-assisted. */
#include "../../sys/dev/ic/rtw88.c"
#include "../../sys/dev/pci/if_rtw88_pci.c"
#include "rtw88-test-kernel.h"
#include <dev/ic/rtw88_net.h>
#include <setjmp.h>

static unsigned assertions, failures;
static int parent_tag, restricted_tag;
static bus_dmamap_t firmware_map;
static void *firmware_vaddr;
#define CHECK(x)                                                               \
	do {                                                                   \
		assertions++;                                                  \
		if (!(x)) {                                                    \
			failures++;                                            \
			fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__,     \
				#x);                                           \
		}                                                              \
	} while (0)
static struct {
	unsigned step, fail, injected, loads, bad_load, bad_kind;
	int bar, tag, heap, maps, mem, kva, loaded, vectors, irq, locks;
	int errors, io, writes, master_writes, removed, remove_read;
	int mask_stuck, small_bar, wrong_type, refuse_master_clear, remove_config;
	int msi_available, selected_irq, callbacks, pending;
	unsigned efuse_reads, efuse_writes, net_attach_calls, net_detach_calls;
	int efuse_enabled, efuse_stuck, net_owned;
	int shutdown_fault;
	unsigned config_writes;
	uint32_t command, reg[16384];
	uint8_t efuse[512];
	int (*handler)(void *);
	void *arg;
} f;
/* Model a detach task parked at the adaptive gate while diagnostics run. */
static struct rtw88_pci_softc *diag_race;
static jmp_buf diag_resume, diag_abort;
static int diag_detaching, diag_waiting;
static unsigned diag_waits, diag_wakes;
static struct rtw88_pci_softc *stats_latch;
static int stats_max;
static int acquire(void)

{
	f.step++;
	if (f.step == f.fail) {
		f.injected++;
		return ENOMEM;
	}
	return 0;
}
static void balanced(void)
{
	CHECK(f.bar == 0 && f.tag == 0 && f.heap == 0 && f.maps == 0);
	CHECK(f.mem == 0 && f.kva == 0 && f.loaded == 0);
	CHECK(f.vectors == 0 && f.irq == 0 && f.locks == 0);
	CHECK(f.handler == NULL);
	CHECK(f.net_owned == 0);
	CHECK(rtwce_diag_lock.initialized && !rtwce_diag_lock.held);
}
void *device_private(device_t d)
{
	return d;
}
const char *device_xname(device_t d)
{
	(void)d;
	return "rtw88test";
}
void aprint_normal_dev(device_t d, const char *s, ...)
{
	(void)d;
	(void)s;
}
void aprint_normal(const char *s, ...)
{
	(void)s;
}
void aprint_error_dev(device_t d, const char *s, ...)
{
	(void)d;
	(void)s;
	f.errors++;
}
void *kmem_zalloc(size_t n, int flags)
{
	(void)flags;
	if (acquire())
		return NULL;
	f.heap++;
	void *p = calloc(1, n);
	assert(p != NULL);
	return p;
}
void kmem_free(void *p, size_t n)
{
	(void)n;
	CHECK(p && f.heap > 0);
	f.heap--;
	free(p);
}
void mutex_init(kmutex_t *m, int type, int ipl)
{
	(void)type;
	CHECK(ipl == IPL_NET || ipl == IPL_NONE);
	CHECK(!m->initialized);
	m->ipl = ipl;
	m->initialized = 1;
	if (ipl == IPL_NET)
		f.locks++;
}
void mutex_destroy(kmutex_t *m)
{
	CHECK(m->initialized && !m->held);
	m->initialized = 0;
	if (m->ipl == IPL_NET)
		f.locks--;
}
void mutex_enter(kmutex_t *m)
{
	if (m->ipl == IPL_NONE && m->held && diag_detaching) {
		CHECK(f.net_owned && f.bar && f.irq && f.locks == 1);
		CHECK(f.net_detach_calls == 0);
		diag_waits++;
		diag_waiting = 1;
		/* Park detach without acquiring the held gate; run diagnostics. */
		longjmp(diag_resume, 1);
	}
	if (m->ipl == IPL_NET && diag_race != NULL) {
		struct rtw88_pci_softc *psc = diag_race;

		diag_race = NULL;
		diag_detaching = 1;
		if (setjmp(diag_resume) == 0)
			CHECK(rtw88_pci_detach(psc, 0) == 0);
		diag_detaching = 0;
		/* No resource teardown may pass the active diagnostic. */
		CHECK(diag_waits == 1 && psc->sc_lock.initialized);
		CHECK(f.net_owned == 1 && f.bar == 1 && f.irq == 1);
		CHECK(f.net_detach_calls == 0);
		CHECK(psc->sc_core.sc_resources_ready);
		if (!psc->sc_lock.initialized)
			longjmp(diag_abort, 1); /* Avoid the known unsafe access in RED. */
	}
	CHECK(m->initialized && !m->held);
	m->held = 1;
	if (stats_latch != NULL && m == &stats_latch->sc_lock) {
		stats_latch->sc_core.sc_basic_rates = 0x55a;
		stats_latch = NULL;
	}
}
void mutex_exit(kmutex_t *m)
{
	CHECK(m->initialized && m->held);
	m->held = 0;
	if (m->ipl == IPL_NONE && diag_waiting) {
		diag_waiting = 0;
		diag_wakes++;
	}
}
int _init_once(once_t *once, int (*init)(void))
{
	once->o_error = init();
	once->o_status = 1;
	return once->o_error;
}
void delay(unsigned n)
{
	(void)n;
}
pcireg_t pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int r)
{
	(void)pc;
	(void)tag;
	if (f.remove_config && r == PCI_COMMAND_STATUS_REG)
		f.removed = 1;
	if (f.removed)
		return UINT32_MAX;
	return r == PCI_ID_REG ? 0xc82210ecU : f.command;
}
void pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int r, pcireg_t v)
{
	(void)pc;
	(void)tag;
	CHECK(r == PCI_COMMAND_STATUS_REG);
	f.config_writes++;
	CHECK((v & 0xffff0000U) == 0);
	CHECK(!f.removed);
	if (v & PCI_COMMAND_MASTER_ENABLE)
		f.master_writes++;
	if (!f.refuse_master_clear)
		f.command = v;
}
pcireg_t pci_mapreg_type(pci_chipset_tag_t pc, pcitag_t tag, int r)
{
	(void)pc;
	(void)tag;
	CHECK(r == PCI_BAR(2));
	return f.wrong_type ? 1 : 0;
}
int pci_mapreg_map(const struct pci_attach_args *pa, int r, pcireg_t type,
		   int flags, bus_space_tag_t *st, bus_space_handle_t *sh,
		   bus_addr_t *base, bus_size_t *sz)
{
	(void)pa;
	(void)flags;
	(void)base;
	CHECK(r == PCI_BAR(2) && type == 0);
	CHECK(f.removed || !(f.command & PCI_COMMAND_MASTER_ENABLE));
	if (acquire())
		return ENOMEM;
	f.bar++;
	*st = 1;
	*sh = 1;
	*sz = f.small_bar ? 0xffff : 0x10000;
	f.command |= PCI_COMMAND_MEM_ENABLE;
	return 0;
}
void bus_space_unmap(bus_space_tag_t st, bus_space_handle_t sh, bus_size_t n)
{
	(void)st;
	(void)sh;
	(void)n;
	CHECK(f.bar == 1 && f.irq == 0 && f.loaded == 0);
	f.bar--;
}
static uint32_t io_read(bus_size_t r)
{
	CHECK(f.bar == 1 && !f.removed && r < 0x10000);
	f.io++;
	if (f.remove_read)
		f.removed = 1;
	return f.reg[r / 4];
}
uint8_t bus_space_read_1(bus_space_tag_t st, bus_space_handle_t sh,
			 bus_size_t r)
{
	(void)st;
	(void)sh;
	return (uint8_t)io_read(r);
}
uint16_t bus_space_read_2(bus_space_tag_t st, bus_space_handle_t sh,
			  bus_size_t r)
{
	(void)st;
	(void)sh;
	return (uint16_t)io_read(r);
}
uint32_t bus_space_read_4(bus_space_tag_t st, bus_space_handle_t sh,
			  bus_size_t r)
{
	(void)st;
	(void)sh;
	return io_read(r);
}
static void io_write(bus_size_t r, uint32_t v)
{
	CHECK(f.bar == 1 && !f.removed);
	f.io++;
	f.writes++;
	/* Identification may access only EFUSE and the existing interrupt masks. */
	if (r == 0xcf) {
		CHECK(v == 0 || v == 0x69);
		f.efuse_enabled = v != 0;
		f.efuse_writes++;
		return;
	}
	if (r == 0x30) {
		unsigned addr = (v >> 8) & 0x3ff;

		CHECK(f.efuse_enabled && addr < sizeof(f.efuse));
		CHECK((v & 0x80000000U) == 0);
		f.efuse_reads++;
		f.reg[r / 4] = v;
		if (!f.efuse_stuck)
			f.reg[r / 4] = (v & ~0xffU) | 0x80000000U | f.efuse[addr];
		return;
	}
	if (f.shutdown_fault && (r == 0x330 || r == 0x328 || r == 0x320 ||
	    r == 0x318 || r == 0x308 || r == 0x310 || r == 0x340 ||
	    r == 0x1320 || r == 0x338)) {
		CHECK(v == 0);
		f.reg[r / 4] = 0;
		return;
	}
	CHECK((r == 0xb0 || r == 0xb8 || r == 0x10b8) && v == 0);
	if (!f.mask_stuck)
		f.reg[r / 4] = v;
}
void bus_space_write_1(bus_space_tag_t st, bus_space_handle_t sh, bus_size_t r,
		       uint8_t v)
{
	(void)st;
	(void)sh;
	io_write(r, v);
}
void bus_space_write_2(bus_space_tag_t st, bus_space_handle_t sh, bus_size_t r,
		       uint16_t v)
{
	(void)st;
	(void)sh;
	io_write(r, v);
}
void bus_space_write_4(bus_space_tag_t st, bus_space_handle_t sh, bus_size_t r,
		       uint32_t v)
{
	(void)st;
	(void)sh;
	io_write(r, v);
}
void bus_space_barrier(bus_space_tag_t st, bus_space_handle_t sh, bus_size_t r,
		       bus_size_t n, int flags)
{
	(void)st;
	(void)sh;
	(void)r;
	(void)n;
	(void)flags;
	CHECK(f.bar == 1);
}
int bus_dmatag_subregion(bus_dma_tag_t old, bus_addr_t lo, bus_addr_t hi,
			 bus_dma_tag_t *newtag, int flags)
{
	(void)old;
	(void)flags;
	CHECK(lo == 0 && hi == UINT32_MAX);
	if (acquire())
		return ENOMEM;
	*newtag = &restricted_tag;
	f.tag++;
	return 0;
}
void bus_dmatag_destroy(bus_dma_tag_t t)
{
	CHECK(t == &restricted_tag && f.tag == 1 && f.maps == 0);
	f.tag--;
}
int bus_dmamap_create(bus_dma_tag_t t, bus_size_t n, int ns, bus_size_t max,
		      bus_size_t boundary, int flags, bus_dmamap_t *m)
{
	(void)boundary;
	(void)flags;
	CHECK(t == &restricted_tag && ns == 1 && max == n);
	if (acquire())
		return ENOMEM;
	*m = calloc(1, sizeof(**m));
	assert(*m != NULL);
	f.maps++;
	return 0;
}
void bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t m)
{
	CHECK(t == &restricted_tag && !m->loaded && f.maps > 0);
	f.maps--;
	free(m);
}
int bus_dmamem_alloc(bus_dma_tag_t t, bus_size_t n, bus_size_t align,
		     bus_size_t boundary, bus_dma_segment_t *s, int ns,
		     int *actual, int flags)
{
	(void)boundary;
	(void)flags;
	CHECK(t == &restricted_tag && ns == 1 && align >= 256);
	if (acquire())
		return ENOMEM;
	s->ds_addr = 0x100000;
	s->ds_len = n;
	*actual = 1;
	f.mem++;
	return 0;
}
void bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *s, int ns)
{
	(void)s;
	CHECK(t == &restricted_tag && ns == 1 && f.mem > 0);
	f.mem--;
}
int bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *s, int ns, size_t n,
		   void **p, int flags)
{
	(void)s;
	CHECK(t == &restricted_tag && ns == 1 && (flags & BUS_DMA_COHERENT));
	if (acquire())
		return ENOMEM;
	*p = malloc(n);
	assert(*p != NULL);
	memset(*p, 0xa5, n);
	f.kva++;
	return 0;
}
void bus_dmamem_unmap(bus_dma_tag_t t, void *p, size_t n)
{
	(void)n;
	CHECK(t == &restricted_tag && f.kva > 0);
	CHECK(f.irq == 0 || p == firmware_vaddr);
	f.kva--;
	free(p);
}
int bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t m, void *p, bus_size_t n,
		    void *proc, int flags)
{
	(void)p;
	(void)proc;
	(void)flags;
	CHECK(t == &restricted_tag && !m->loaded);
	if (acquire())
		return ENOMEM;
	f.loads++;
	m->dm_nsegs = 1;
	m->dm_mapsize = n;
	m->dm_segs[0].ds_addr = 0x200000 + f.loads * 0x10000;
	m->dm_segs[0].ds_len = n;
	if (f.loads == f.bad_load) {
		if (f.bad_kind == 0)
			m->dm_segs[0].ds_addr = UINT64_C(0x100000000);
		if (f.bad_kind == 1)
			m->dm_segs[0].ds_addr = UINT64_C(0xffffff00);
		if (f.bad_kind == 2)
			m->dm_segs[0].ds_len--;
		if (f.bad_kind == 3)
			m->dm_nsegs = 2;
		if (f.bad_kind == 4)
			m->dm_segs[0].ds_addr++;
	}
	m->loaded = 1;
	f.loaded++;
	return 0;
}
void bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t m)
{
	CHECK(f.removed || !(f.command & PCI_COMMAND_MASTER_ENABLE));
	CHECK(t == &restricted_tag && m->loaded);
	CHECK(f.irq == 0 || m == firmware_map);
	m->loaded = 0;
	f.loaded--;
}
void bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t m, bus_addr_t off,
		     bus_size_t n, int flags)
{
	(void)flags;
	CHECK(t == &restricted_tag && m->loaded && off == 0 && n == m->dm_mapsize);
}
int pci_intr_alloc(const struct pci_attach_args *pa, pci_intr_handle_t **v,
		   int *counts, pci_intr_type_t max)
{
	(void)pa;
	CHECK(max == PCI_INTR_TYPE_MSI && counts[0] == 1 && counts[1] == 1 &&
	      counts[2] == 0);
	if (acquire())
		return ENOMEM;
	*v = calloc(1, sizeof(**v));
	assert(*v != NULL);
	f.selected_irq = f.msi_available ? 1 : 0;
	**v = f.selected_irq;
	counts[0] = f.selected_irq == 0;
	counts[1] = f.selected_irq == 1;
	f.vectors++;
	return 0;
}
void pci_intr_release(pci_chipset_tag_t pc, pci_intr_handle_t *v, int n)
{
	(void)pc;
	CHECK(n == 1 && f.vectors == 1 && !f.irq && !f.pending);
	f.vectors--;
	free(v);
}
void *pci_intr_establish_xname(pci_chipset_tag_t pc, pci_intr_handle_t ih,
			       int ipl, int (*handler)(void *), void *arg,
			       const char *name)
{
	(void)pc;
	(void)ih;
	(void)name;
	CHECK(ipl == IPL_NET);
	CHECK(f.loaded == RTW88_NRING + RTW88_RX_BUFS + RTW88_NTXQ * RTW88_TX_SLOTS && f.reg[0xb0 / 4] == 0 && f.reg[0xb8 / 4] == 0 &&
	      f.reg[0x10b8 / 4] == 0);
	if (acquire())
		return NULL;
	f.handler = handler;
	f.arg = arg;
	f.irq++;
	return &f;
}
void pci_intr_disestablish(pci_chipset_tag_t pc, void *ih)
{
	(void)pc;
	CHECK(ih == &f && f.irq == 1 && f.loaded == RTW88_NRING + RTW88_RX_BUFS + RTW88_NTXQ * RTW88_TX_SLOTS);
	/* A CPU has already fetched the handler and its argument. */
	f.pending = 1;
	f.irq--;
}
void xc_barrier(unsigned flags)
{
	CHECK(flags == 0 && f.pending && f.irq == 0);
	CHECK(f.loaded == RTW88_NRING + RTW88_RX_BUFS + RTW88_NTXQ * RTW88_TX_SLOTS && f.locks == 1 && f.bar == 1);
	const struct rtw88_pci_softc *p = f.arg;
	CHECK(!p->sc_lock.held && p->sc_stopping);
	int io = f.io;
	CHECK(f.handler(f.arg) == 0);
	CHECK(f.io == io);
	f.callbacks++;
	f.handler = NULL;
	f.arg = NULL;
	f.pending = 0;
}
static struct pci_attach_args fixture_pa = {.pa_id = 0xc82210ecU, .pa_dmat = &parent_tag};
static void reset(struct rtw88_pci_softc *p)
{
	/* Extended EFUSE records: logical MAC0x120 and RFE0xca, then terminator. */
	static const uint8_t physical[] = {
		0x8f, 0x48, 0x02, 0x12, 0x23, 0x34, 0x45, 0x56,
		0x2f, 0x3d, 0x05, 'U', 0xff
	};

	CHECK(diag_race == NULL);
	diag_waits = diag_wakes = 0;
	memset(p, 0, sizeof(*p));
	memset(&f, 0, sizeof(f));
	f.command = 0x140 | PCI_COMMAND_MASTER_ENABLE;
	f.msi_available = 1;
	f.reg[0xb0 / 4] = f.reg[0xb8 / 4] = f.reg[0x10b8 / 4] = UINT32_MAX;
	memset(f.efuse, 0xff, sizeof(f.efuse));
	memcpy(f.efuse, physical, sizeof(physical));
	f.reg[0xf0 / 4] = 0x2000;
}

struct rtw88_net { int owned; };
static struct rtw88_net fake_net;

void
rtw88_net_get_stats(struct rtw88_net *net, struct rtw88_net_stats *stats)
{

	CHECK(net == &fake_net && net->owned);
	CHECK(rtwce_diag_lock.held && f.locks == 1);
	CHECK(((struct rtw88_pci_softc *)f.arg)->sc_lock.held);
	memset(stats, 0, sizeof(*stats));
	stats->rx.accepted = 7;
	stats->rx.delivery_epoch = 3;
	stats->rx.inactive = 2;
	stats->chanmismatch = 11;
	stats->badchan = 13;
	stats->elem_missing = 17;
	stats->elem_toobig = 19;
	stats->elem_toosmall = 23;
	if (stats_max)
		memset(stats, 0xff, sizeof(*stats));
}

int
rtw88_net_attach(struct rtw88_softc *core, device_t dev, kmutex_t *lock,
    const uint8_t mac[6], struct rtw88_net **out)
{
	static const uint8_t expected[6] = {2, 0x12, 0x23, 0x34, 0x45, 0x56};
	const struct rtw88_pci_softc *psc = dev;

	f.net_attach_calls++;
	CHECK(core == &psc->sc_core && lock == &psc->sc_lock);
	CHECK(core->sc_resources_ready && !core->sc_started && !core->sc_powered);
	CHECK(!lock->held && lock->initialized && f.irq == 1);
	CHECK(f.efuse_reads == 512 && !f.efuse_enabled);
	CHECK(core->sc_rfe == 5 && memcmp(mac, expected, 6) == 0);
	/* The global diagnostics must not expose a half-attached instance. */
	CHECK(rtwce_bootstrap(1) == ENXIO);
	if (acquire())
		return ENOMEM;
	CHECK(!f.net_owned);
	f.net_owned = 1;
	fake_net.owned = 1;
	*out = &fake_net;
	return 0;
}

void
rtw88_net_detach(struct rtw88_net *net)
{
	f.net_detach_calls++;
	CHECK(net == &fake_net && net->owned && f.net_owned == 1);
	CHECK(f.locks == 1 && f.irq == 1 && f.bar == 1 && f.loaded != 0);
	CHECK(rtwce_bootstrap(1) == ENXIO);
	f.net_owned = 0;
	net->owned = 0;
}
static void clean_detach(struct rtw88_pci_softc *p)
{
	CHECK(rtw88_pci_detach(p, 0) == 0);
	balanced();
	CHECK(f.removed || !(f.command & PCI_COMMAND_MASTER_ENABLE));
	CHECK(f.master_writes == 0);
	int io = f.io;
	CHECK(rtw88_pci_detach(p, 0) == 0);
	balanced();
	CHECK(io == f.io);
}
int firmware_open(const char *drv, const char *name, firmware_handle_t *fh)
{
	(void)drv;
	(void)name;
	(void)fh;
	return ENOENT;
}
int firmware_close(firmware_handle_t fh)
{
	(void)fh;
	return 0;
}
long firmware_get_size(firmware_handle_t fh)
{
	(void)fh;
	return 0;
}
int firmware_read(firmware_handle_t fh, long off, void *buf, size_t len)
{
	(void)fh;
	(void)off;
	(void)buf;
	(void)len;
	return ENOENT;
}
int main(void)
{
	struct rtw88_pci_softc p;
	unsigned stages;
	reset(&p);
	CHECK(rtwce_bootstrap(1) == ENXIO);
	CHECK(rtwce_started() == 0);
	CHECK(rtw88_pci_match(NULL, NULL, &fixture_pa) == 1);
	struct pci_attach_args other = fixture_pa;
	other.pa_id = 0xc82110ecU;
	CHECK(rtw88_pci_match(NULL, NULL, &other) == 0);
	rtw88_pci_attach(NULL, &p, &fixture_pa);
	CHECK(p.sc_core.sc_resources_ready && p.sc_core.sc_nring == 9);
	CHECK(f.irq == 1 && f.errors == 0 && f.master_writes == 0);
	CHECK(f.net_attach_calls == 1 && f.net_owned == 1);
	CHECK(rtwce_bootstrap(1) == EBUSY);
	CHECK(rtwce_bootstrap(0) == EBUSY);
	int old_io = f.io;
	int ch = 36;
	char stats[4096] = { 0 };
	CHECK(rtwce_h2c(0x1234) == EBUSY);
	CHECK(rtwce_lbmode(1) == EBUSY);
	CHECK(rtwce_loopback(1) == EBUSY);
	CHECK(rtwce_channel(&ch, 1) == EBUSY);
	CHECK(rtwce_channel(&ch, 0) == 0);
	memcpy(p.sc_core.sc_stats.last_ssid, "private-fixture", 16);
	CHECK(rtwce_stats(stats, sizeof(stats)) == 0);
	CHECK(strstr(stats, "nrx 7") != NULL && strstr(stats, "ndepoch 3") != NULL);
	CHECK(strstr(stats, "ninactive 2") != NULL);
	CHECK(strstr(stats, "chanmismatch 11 badchan 13") != NULL);
	CHECK(strstr(stats, "elem_missing 17 elem_toobig 19 elem_toosmall 23") != NULL);
	CHECK(strstr(stats, "ssid") == NULL && strstr(stats, "private-fixture") == NULL);
	p.sc_core.sc_basic_rates = 0x0f;
	CHECK(rtwce_stats(stats, sizeof(stats)) == 0);
	CHECK(strstr(stats, " basic 0xf") != NULL);
	CHECK(strstr(stats, " txdata 0 txrate 0 txbasic 0") != NULL);
	CHECK(strstr(stats, " fwra_tx 0 protection_tx 0") != NULL);
	CHECK(strstr(stats, " ra_reports 0 unattributed 0") != NULL);
	CHECK(strstr(stats, " hw_requested 0/0 hw_observed 0/0 hw_known 0") != NULL);
	p.sc_core.sc_hw_feature.requested = 1;
	p.sc_core.sc_hw_feature.requested_generation = 17;
	p.sc_core.sc_hw_feature.observed = 1;
	p.sc_core.sc_hw_feature.observed_generation = 17;
	p.sc_core.sc_hw_feature.report.known = 15;
	p.sc_core.sc_hw_feature.report.hci = 9;
	p.sc_core.sc_hw_feature.report.bw = 7;
	p.sc_core.sc_hw_feature.report.nss = 2;
	p.sc_core.sc_hw_feature.report.antennas = 2;
	p.sc_core.sc_hw_feature.report.protocol = 3;
	p.sc_core.sc_hw_feature.report.bandwidths = 7;
	CHECK(rtwce_stats(stats, sizeof(stats)) == 0);
	CHECK(strstr(stats, " hw_requested 1/17 hw_observed 1/17 hw_known 0xf") != NULL);
	CHECK(strstr(stats, " hw_raw 9/7/2/2/3 hw_bandwidths 0x7") != NULL);
	CHECK(strstr(stats, " txr0 0") != NULL && strstr(stats, " txr11 0") != NULL);
	CHECK(strstr(stats, " txpost0 0") != NULL && strstr(stats, " txpost4 0") != NULL);
	p.sc_core.sc_stats.signal = (struct rtw88_rx_signal){
	    .generation=17,.valid=1,.page=1,.length=32,.rate=4,.channel=8,
	    .paths=3,.quality=50,.pwdb={60,50},.power={-50,-60}};
	p.sc_core.sc_phy = (struct rtw88_phy_state){.generation=17,
	    .gain_valid=1,.gain_low=20,.gain_high=40,.paths=3};
	CHECK(rtwce_stats(stats,sizeof(stats))==0);
	CHECK(strstr(stats," signal 1/0/17 page 1 len 32 rate 4 channel 8")!=NULL);
	CHECK(strstr(stats," quality 50 pwdb 60/50 gain 0/0 power_est -50/-60")!=NULL);
	CHECK(strstr(stats," gainctx 17/1/20/40/0")!=NULL);
	CHECK(strstr(stats," sig0 0")!=NULL && strstr(stats," sig10 0")!=NULL);
#ifdef RTW88_RFK_EXPERIMENT
	p.sc_core.sc_stats.rfk=(struct rtw88_rfk_diag){.token=9,.generation=3,
	    .baseline_generation=2,.flags=7,.global=2,.error=EIO,.cleanup_error=ETIMEDOUT,
	    .kind=4,.addr=0x2d9c,.size=1,.raw=0xaa,.raw_valid=1,.non_target=1,
	    .operations=777,.requested_delay_us=888,.sleeps=9};
	for(unsigned i=0;i<7;i++) { p.sc_core.sc_stats.rfk.arms[i]=i+2;p.sc_core.sc_stats.rfk.done[i]=i+1; }
	CHECK(rtwce_stats(stats,sizeof(stats))==0);
	{
		char expected[96];
		(void)snprintf(expected,sizeof(expected)," rfk 9/3/2 flags 0x7 global 0x2 error %d/%d",EIO,ETIMEDOUT);
		CHECK(strstr(stats,expected)!=NULL);
	}
	CHECK(strstr(stats," rfk_report 4/0x2d9c/1/0xaa/1/1 rfk_cost 777/888/9")!=NULL);
	CHECK(strstr(stats," rfk1 2/1")!=NULL && strstr(stats," rfk7 8/7")!=NULL);
#endif
	stats_latch = &p;
	CHECK(rtwce_stats(stats, sizeof(stats)) == 0);
	CHECK(stats_latch == NULL && strstr(stats, " basic 0x55a") != NULL);
	{
		struct rtw88_stats saved = p.sc_core.sc_stats;
		struct rtw88_hw_feature_state saved_hw = p.sc_core.sc_hw_feature;
		char probe[4098];
		size_t required;

		memset(&p.sc_core.sc_stats, 0xff, sizeof(p.sc_core.sc_stats));
		memset(&p.sc_core.sc_hw_feature, 0xff, sizeof(p.sc_core.sc_hw_feature));
		stats_max = 1;
		CHECK(rtwce_stats(stats, sizeof(stats)) == 0);
		CHECK(strstr(stats, " txr11 18446744073709551615") != NULL);
		CHECK(strstr(stats, " txpost4 18446744073709551615") != NULL);
		required = strlen(stats) + 1;
		CHECK(required < sizeof(stats));
		for (size_t capacity = 0; capacity <= sizeof(stats); capacity++) {
			memset(probe, 0x5a, sizeof(probe));
			CHECK(rtwce_stats(probe + 1, capacity) ==
			    (capacity < required ? ENOSPC : 0));
			CHECK(probe[0] == 0x5a && probe[capacity + 1] == 0x5a);
			CHECK(capacity == 0 || memchr(probe + 1, 0, capacity) != NULL);
			CHECK(!p.sc_lock.held && !rtwce_diag_lock.held);
		}
		p.sc_core.sc_stats = saved;
		p.sc_core.sc_hw_feature = saved_hw;
		stats_max = 0;
	}

	{
		char bounded[34];

		memset(bounded, 0x5a, sizeof(bounded));
		CHECK(rtwce_stats(bounded + 1, 32) == ENOSPC);
		CHECK(bounded[0] == 0x5a && bounded[33] == 0x5a);
		CHECK(bounded[32] == 0);
		CHECK(!p.sc_lock.held && !rtwce_diag_lock.held);
	}

	CHECK(f.io == old_io);
	CHECK(rtwce_started() == 0 && f.master_writes == 0);
	stages = f.step;
	CHECK(stages >= 40);
	if (p.sc_core.sc_resources_ready) {
		static const size_t sizes[] = {2048, 4096, 2048, 2048, 16,
					       2048, 2048, 2048, 4096};
		for (unsigned i = 0; i < 9; i++) {
			CHECK(p.sc_core.sc_ring[i].size == sizes[i]);
			const unsigned char *b = p.sc_core.sc_ring[i].vaddr;
			int zero = 1;
			for (size_t j = 0; j < sizes[i]; j++)
				if (b[j])
					zero = 0;
			CHECK(zero);
		}
		CHECK(p.sc_core.sc_nrxbuf == RTW88_RX_BUFS);
		for (unsigned i = 0; i < RTW88_RX_BUFS; i++)
			CHECK(p.sc_core.sc_rxbuf[i].size == RTW88_RX_BUF_SIZE &&
			      p.sc_core.sc_rxbuf[i].addr <= UINT32_MAX);
		CHECK(rtw88_core_attach(&p.sc_core) == EBUSY);
		int io = f.io;
		CHECK(f.handler(f.arg) == 0);
		CHECK(io == f.io);
	}
	clean_detach(&p);
	CHECK(f.net_detach_calls == 1);
	CHECK(f.command == 0x140);
	/* Failed disable retains the full instance and diagnostics for a retry. */
	reset(&p);
	rtw88_pci_attach(NULL, &p, &fixture_pa);
	CHECK(p.sc_core.sc_ops->dma_alloc(&p, &p.sc_core.sc_fw_dma, 4096) == 0);
	p.sc_core.sc_fw_dma_owned = 1;
	firmware_map = ((struct rtw88_pci_dma *)p.sc_core.sc_fw_dma.cookie)->map;
	firmware_vaddr = p.sc_core.sc_fw_dma.vaddr;
	p.sc_core.sc_dma_master = 1;
	f.command |= PCI_COMMAND_MASTER_ENABLE;
	f.refuse_master_clear = f.shutdown_fault = 1;
	unsigned loaded = (unsigned)f.loaded, config_writes = f.config_writes;
	CHECK(rtw88_pci_detach(&p, 0) == EIO);
	CHECK(f.config_writes == config_writes + 1); /* Disable only, no restore. */
	CHECK(p.sc_net == NULL && !f.net_owned && f.net_detach_calls == 1);
	CHECK(p.sc_core.sc_resources_ready && p.sc_core.sc_dma_error == EIO);
	CHECK(p.sc_core.sc_fw_dma_owned && p.sc_core.sc_dma_master);
	CHECK(f.loaded == (int)loaded && f.bar == 1 && f.tag == 1 && f.locks == 1);
	CHECK(f.irq == 1 && f.handler != NULL);
	CHECK(rtwce_stats(stats, sizeof(stats)) == 0 && strstr(stats, "dma_error 5"));
	CHECK(rtwce_bootstrap(1) == EBUSY);
	CHECK(rtwce_bootstrap(0) == EIO); /* Propagate the retained error. */
	f.refuse_master_clear = 0;
	CHECK(rtwce_bootstrap(0) == 0); /* Operator can retry a safe shutdown. */
	CHECK(rtwce_bootstrap(1) == EBUSY); /* A torn-down net wrapper cannot restart. */
	clean_detach(&p);
	firmware_map = NULL;
	firmware_vaddr = NULL;
	/* A detach attempt must wait until each diagnostic releases its gate. */
	for (unsigned op = 0; op < 8; op++) {
		reset(&p);
		rtw88_pci_attach(NULL, &p, &fixture_pa);
		diag_race = &p;
		ch = 36;
		if (setjmp(diag_abort) == 0) {
			switch (op) {
			case 0:
				CHECK(rtwce_started() == 0);
				break;
			case 1:
				CHECK(rtwce_bootstrap(1) == EBUSY);
				break;
			case 2:
				CHECK(rtwce_stats(stats, sizeof(stats)) == 0);
				break;
			case 3:
				CHECK(rtwce_channel(&ch, 0) == 0);
				break;
			case 4:
				CHECK(rtwce_channel(&ch, 1) == EBUSY);
				break;
			case 5:
				CHECK(rtwce_lbmode(1) == EBUSY);
				break;
			case 6:
				CHECK(rtwce_loopback(1) == EBUSY);
				break;
			case 7:
				CHECK(rtwce_h2c(0x1234) == EBUSY);
				break;
			}
		}
		CHECK(diag_race == NULL && diag_waits == 1 && diag_wakes == 1);
		diag_race = NULL;
		/* Resume detach after the diagnostic releases the gate. */
		clean_detach(&p);
		CHECK(rtwce_started() == 0);
		CHECK(rtwce_bootstrap(1) == ENXIO);
		CHECK(rtwce_stats(stats, sizeof(stats)) == ENXIO);
		CHECK(rtwce_channel(&ch, 0) == ENXIO);
		CHECK(rtwce_channel(&ch, 1) == ENXIO);
		CHECK(rtwce_lbmode(1) == ENXIO);
		CHECK(rtwce_loopback(1) == ENXIO);
		CHECK(rtwce_h2c(0x1234) == ENXIO);
	}
	for (unsigned i = 1; i <= stages; i++) {
		reset(&p);
		f.fail = i;
		rtw88_pci_attach(NULL, &p, &fixture_pa);
		CHECK(f.injected == 1);
		CHECK(!p.sc_core.sc_resources_ready && f.errors == 1);
		if (i == stages) {
			/* The last acquisition is the opaque net wrapper. */
			CHECK(f.net_attach_calls == 1 && f.net_detach_calls == 0);
			CHECK(rtwce_bootstrap(1) == ENXIO);
		}
		balanced();
		clean_detach(&p);
	}
	for (unsigned kind = 0; kind < 5; kind++) {
		reset(&p);
		f.bad_load = 3;
		f.bad_kind = kind;
		rtw88_pci_attach(NULL, &p, &fixture_pa);
		CHECK(!p.sc_core.sc_resources_ready && f.errors == 1);
		balanced();
		clean_detach(&p);
	}
	for (unsigned mode = 0; mode < 4; mode++) {
		reset(&p);
		if (mode == 0)
			f.small_bar = 1;
		if (mode == 1)
			f.mask_stuck = 1;
		if (mode == 2)
			f.wrong_type = 1;
		if (mode == 3)
			f.refuse_master_clear = 1;
		rtw88_pci_attach(NULL, &p, &fixture_pa);
		CHECK(!p.sc_core.sc_resources_ready && f.errors == 1);
		balanced();
		CHECK(f.net_attach_calls == 0 && f.efuse_reads == 0);
		f.refuse_master_clear = 0;
		f.command &= ~PCI_COMMAND_MASTER_ENABLE;
		clean_detach(&p);
	}
	reset(&p);
	f.remove_config = 1;
	rtw88_pci_attach(NULL, &p, &fixture_pa);
	CHECK(f.errors == 1 && !p.sc_core.sc_resources_ready);
	CHECK(f.io == 0);
	balanced();
	clean_detach(&p);
	/* Real identity decoding must reject bad EFUSE before net80211 attaches. */
	for (unsigned kind = 0; kind < 3; kind++) {
		reset(&p);
		if (kind == 0)
			f.efuse[2] = 1; /* Multicast MAC. */
		if (kind == 1)
			f.efuse[10] = 0xff; /* Unsupported RFE. */
		if (kind == 2)
			f.efuse_stuck = 1;
		rtw88_pci_attach(NULL, &p, &fixture_pa);
		CHECK(!p.sc_core.sc_resources_ready && f.errors == 1);
		CHECK(f.net_attach_calls == 0 && !f.efuse_enabled);
		balanced();
		clean_detach(&p);
	}
	reset(&p);
	f.msi_available = 0;
	rtw88_pci_attach(NULL, &p, &fixture_pa);
	CHECK(p.sc_core.sc_resources_ready && f.selected_irq == 0);
	clean_detach(&p);
	/* Read failure preserves values, then removal makes detach avoid MMIO.
	 */
	reset(&p);
	rtw88_pci_attach(NULL, &p, &fixture_pa);
	if (p.sc_core.sc_resources_ready) {
		uint32_t v = 0x12345678;
		uint16_t v2 = 0x5678;
		uint8_t v1 = 0x78;
		const struct rtw88_bus_ops *op = p.sc_core.sc_ops;
		CHECK(op->read_4(&p, 0x10000, &v) == EINVAL && v == 0x12345678);
		CHECK(op->read_1(&p, 0xff1a, &v1) == 0);
		CHECK(op->read_4(&p, 0xfffc, &v) == 0);
		v = 0x12345678;
		v1 = 0x78;
		CHECK(op->read_4(&p, 1, &v) == EINVAL && v == 0x12345678);
		CHECK(op->read_4(&p, 0, NULL) == EINVAL);
		f.reg[0xf0 / 4] = UINT32_MAX;
		CHECK(op->read_4(&p, 0xf0, &v) == 0 && v == UINT32_MAX);
		v = 0x12345678;
		f.remove_read = 1;
		CHECK(op->read_4(&p, 0xf0, &v) == ENXIO && v == 0x12345678);
		int io = f.io;
		CHECK(op->read_1(&p, 0xf0, &v1) == ENXIO && v1 == 0x78);
		CHECK(op->read_2(&p, 0xf0, &v2) == ENXIO && v2 == 0x5678);
		CHECK(op->write_4(&p, 0xb0, 0) == ENXIO);
		CHECK(f.io == io);
		clean_detach(&p);
		CHECK(f.io == io);
	} else
		clean_detach(&p);
	printf("rtw88 PCI lifecycle: %u assertions, %u failures\n", assertions,
	       failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

/* Host native-atomic observation service; never a kernel API. */
volatile unsigned *rtw88_test_rfk_atomic_word;
