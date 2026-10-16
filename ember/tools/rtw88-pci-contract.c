/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8822CE actual-source resource tests, AI-assisted. */
#include "../../sys/dev/ic/rtw88.c"
#include "../../sys/dev/pci/if_rtw88_pci.c"
#include "rtw88-test-kernel.h"

static unsigned assertions, failures;
static int parent_tag, restricted_tag;
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
	uint32_t command, reg[2048];
	int (*handler)(void *);
	void *arg;
} f;
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
	CHECK(ipl == IPL_NET);
	CHECK(!m->initialized);
	m->initialized = 1;
	f.locks++;
}
void mutex_destroy(kmutex_t *m)
{
	CHECK(m->initialized && !m->held);
	m->initialized = 0;
	f.locks--;
}
void mutex_enter(kmutex_t *m)
{
	CHECK(m->initialized && !m->held);
	m->held = 1;
}
void mutex_exit(kmutex_t *m)
{
	CHECK(m->initialized && m->held);
	m->held = 0;
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
	*sz = f.small_bar ? 0x100 : 0x4000;
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
	CHECK(f.bar == 1 && !f.removed && r < 8192);
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
	/* Task4 may only silence the evidenced interrupt masks. */
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
	CHECK(t == &restricted_tag && f.kva > 0 && f.irq == 0);
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
	CHECK(t == &restricted_tag && m->loaded && f.irq == 0);
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
	memset(p, 0, sizeof(*p));
	memset(&f, 0, sizeof(f));
	f.command = 0x140 | PCI_COMMAND_MASTER_ENABLE;
	f.msi_available = 1;
	f.reg[0xb0 / 4] = f.reg[0xb8 / 4] = f.reg[0x10b8 / 4] = UINT32_MAX;
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
	CHECK(rtwce_bootstrap(1) == ENOENT);
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
	CHECK(f.command == 0x140);
	for (unsigned i = 1; i <= stages; i++) {
		reset(&p);
		f.fail = i;
		rtw88_pci_attach(NULL, &p, &fixture_pa);
		CHECK(f.injected == 1);
		CHECK(!p.sc_core.sc_resources_ready && f.errors == 1);
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
		CHECK(op->read_4(&p, 0x4000, &v) == EINVAL && v == 0x12345678);
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
