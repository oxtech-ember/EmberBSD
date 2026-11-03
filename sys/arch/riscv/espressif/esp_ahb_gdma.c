/* $NetBSD$ */
/* SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-FileCopyrightText: 2026 The EmberBSD Project */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ESP32-S31 AHB GDMA provider. Register layout and descriptor format follow
 * ESP-IDF 6.1 components/esp_hal_dma/esp32s31 and soc/esp32s31/register.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/intr.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <dev/fdt/syscon.h>

#include "esp_ahb_gdma_desc.h"
#include "esp_ahb_gdma_state.h"

#define AHB_GDMA_CHANNELS	5
#define AHB_GDMA_MAX_DESC	512
#define AHB_GDMA_DESC_BYTES	(AHB_GDMA_MAX_DESC * \
	 sizeof(struct esp_ahb_gdma_desc))
#define AHB_GDMA_IRQ_STRIDE	0x10
#define AHB_GDMA_CHANNEL_BASE	0x100
#define AHB_GDMA_CHANNEL_STRIDE	0x100
#define AHB_GDMA_IN_IRQ_BASE	0x00
#define AHB_GDMA_OUT_IRQ_BASE	0x50
#define AHB_GDMA_IRQ_RAW	0x00
#define AHB_GDMA_IRQ_STATUS	0x04
#define AHB_GDMA_IRQ_ENABLE	0x08
#define AHB_GDMA_IRQ_CLEAR	0x0c
#define AHB_GDMA_MISC		0xa4
#define AHB_GDMA_MEM_START	0x600
#define AHB_GDMA_MEM_END	0x604
#define AHB_GDMA_GLOBAL_CLK	0x7c
#define AHB_GDMA_INT_CLK_EN	__BIT(3)
#define AHB_GDMA_SYS_CLK_EN	__BIT(0)
#define AHB_GDMA_SYS_RESET	__BIT(1)

#define AHB_GDMA_IN_CONF0	0x00
#define AHB_GDMA_IN_CONF1	0x04
#define AHB_GDMA_IN_LINK	0x10
#define AHB_GDMA_IN_LINK_ADDR	0x14
#define AHB_GDMA_IN_SUC_EOF_ADDR	0x1c
#define AHB_GDMA_IN_PERI_SEL	0x38
#define AHB_GDMA_OUT_CONF0	0x80
#define AHB_GDMA_OUT_CONF1	0x84
#define AHB_GDMA_OUT_LINK	0x90
#define AHB_GDMA_OUT_LINK_ADDR	0x94
#define AHB_GDMA_OUT_EOF_ADDR	0x9c
#define AHB_GDMA_OUT_PERI_SEL	0xb8
#define AHB_GDMA_CONF_RESET	__BIT(0)
#define AHB_GDMA_IN_LINK_STOP	__BIT(1)
#define AHB_GDMA_IN_LINK_START	__BIT(2)
#define AHB_GDMA_IN_LINK_PARK	__BIT(4)
#define AHB_GDMA_OUT_LINK_STOP	__BIT(0)
#define AHB_GDMA_OUT_LINK_START	__BIT(1)
#define AHB_GDMA_OUT_LINK_PARK	__BIT(3)
#define AHB_GDMA_IN_IRQ_EOF	__BIT(1)
#define AHB_GDMA_IN_IRQ_ERRORS	(__BIT(2) | __BIT(3) | __BIT(4) | \
	__BIT(5) | __BIT(6) | __BIT(7))
#define AHB_GDMA_OUT_IRQ_EOF	__BIT(1)
#define AHB_GDMA_OUT_IRQ_ERRORS	(__BIT(2) | __BIT(4) | \
	__BIT(5) | __BIT(6))

struct esp_ahb_gdma_softc;
struct esp_ahb_gdma_dir {
	struct esp_ahb_gdma_channel *channel;
	bool write, running, done, broken, stopping, block_multi;
	bool callback_pending, callback_running, softintr_scheduled;
	bool result_busy, result_valid, desc_synced;
	int error;
	size_t ndesc, last_eof, callback_count, bytes;
	bool eof[AHB_GDMA_MAX_DESC];
	struct fdtbus_dma_result result;
	struct esp_ahb_gdma_desc *descs;
	bus_dma_segment_t memseg;
	int mem_nsegs;
	bus_dmamap_t map;
	void *ih, *softih;
};
struct esp_ahb_gdma_channel {
	struct esp_ahb_gdma_softc *sc;
	unsigned int index;
	bool claimed;
	uint32_t trigger;
	struct esp_ahb_gdma_dir *last_dir;
	void (*callback)(void *);
	void *callback_arg;
	struct esp_ahb_gdma_dir in, out;
};
struct esp_ahb_gdma_softc {
	device_t dev;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	bus_dma_tag_t dmat;
	struct syscon *syscon;
	kmutex_t lock;
	int phandle;
	struct esp_ahb_gdma_channel channel[AHB_GDMA_CHANNELS];
};

#define AHB_CH_BASE(ch) (AHB_GDMA_CHANNEL_BASE + \
	(ch)->index * AHB_GDMA_CHANNEL_STRIDE)
#define AHB_READ(sc, off) bus_space_read_4((sc)->bst, (sc)->bsh, (off))
#define AHB_WRITE(sc, off, val) bus_space_write_4((sc)->bst, (sc)->bsh, \
	(off), (val))
#define AHB_IRQ_BASE(dir, ch) ((dir)->write ? AHB_GDMA_OUT_IRQ_BASE : \
	AHB_GDMA_IN_IRQ_BASE) + (ch)->index * AHB_GDMA_IRQ_STRIDE
#define AHB_DIR_BASE(dir) AHB_CH_BASE((dir)->channel) + \
	((dir)->write ? AHB_GDMA_OUT_CONF0 : AHB_GDMA_IN_CONF0)
#define AHB_DIR_REG(dir, reg) AHB_DIR_BASE(dir) + (reg)
#define AHB_LINK_REG(dir) AHB_DIR_REG(dir, 0x10)
#define AHB_LINK_ADDR_REG(dir) AHB_DIR_REG(dir, 0x14)
#define AHB_EOF_ADDR_REG(dir) AHB_DIR_REG(dir, 0x1c)
#define AHB_LINK_STOP(dir) ((dir)->write ? AHB_GDMA_OUT_LINK_STOP : \
	AHB_GDMA_IN_LINK_STOP)
#define AHB_LINK_START(dir) ((dir)->write ? AHB_GDMA_OUT_LINK_START : \
	AHB_GDMA_IN_LINK_START)
#define AHB_LINK_PARK(dir) ((dir)->write ? AHB_GDMA_OUT_LINK_PARK : \
	AHB_GDMA_IN_LINK_PARK)
#define AHB_IRQ_EOF(dir) ((dir)->write ? AHB_GDMA_OUT_IRQ_EOF : \
	AHB_GDMA_IN_IRQ_EOF)
#define AHB_IRQ_ERRORS(dir) ((dir)->write ? AHB_GDMA_OUT_IRQ_ERRORS : \
	AHB_GDMA_IN_IRQ_ERRORS)
#define AHB_PERI_SEL_REG(dir) AHB_DIR_BASE(dir) + \
	((dir)->write ? (AHB_GDMA_OUT_PERI_SEL - AHB_GDMA_OUT_CONF0) : \
	 (AHB_GDMA_IN_PERI_SEL - AHB_GDMA_IN_CONF0))

static int esp_ahb_gdma_intr(void *);
static void esp_ahb_gdma_softintr(void *);
static void *esp_ahb_gdma_acquire(device_t, const void *, size_t,
	void (*)(void *), void *);
static void esp_ahb_gdma_release(device_t, void *);
static int esp_ahb_gdma_transfer(device_t, void *, struct fdtbus_dma_req *);
static void esp_ahb_gdma_halt(device_t, void *);
static int esp_ahb_gdma_halt_checked(device_t, void *);
static int esp_ahb_gdma_result(device_t, void *, struct fdtbus_dma_result *);

static const struct fdtbus_dma_controller_func esp_ahb_gdma_funcs = {
	.acquire = esp_ahb_gdma_acquire,
	.release = esp_ahb_gdma_release,
	.transfer = esp_ahb_gdma_transfer,
	.halt = esp_ahb_gdma_halt,
	.result = esp_ahb_gdma_result,
	.halt_checked = esp_ahb_gdma_halt_checked,
};
static const struct device_compatible_entry esp_ahb_gdma_compat[] = {
	{ .compat = "espressif,esp32s31-ahb-gdma" },
	DEVICE_COMPAT_EOL
};

static int
esp_ahb_gdma_match(device_t parent, cfdata_t match, void *aux)
{
	const struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, esp_ahb_gdma_compat);
}

static int
esp_ahb_gdma_alloc_dir(struct esp_ahb_gdma_channel *ch,
    struct esp_ahb_gdma_dir *dir, bool write)
{
	struct esp_ahb_gdma_softc *sc = ch->sc;
	int error, nsegs;

	dir->channel = ch;
	dir->write = write;
	error = bus_dmamem_alloc(sc->dmat, AHB_GDMA_DESC_BYTES, 64, 0,
	    &dir->memseg, 1, &nsegs, BUS_DMA_WAITOK);
	if (error != 0)
		return error;
	dir->mem_nsegs = nsegs;
	if (nsegs != 1) {
		bus_dmamem_free(sc->dmat, &dir->memseg, nsegs);
		dir->mem_nsegs = 0;
		return EFBIG;
	}
	error = bus_dmamem_map(sc->dmat, &dir->memseg, nsegs,
	    AHB_GDMA_DESC_BYTES, (void **)&dir->descs, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail_mem;
	error = bus_dmamap_create(sc->dmat, AHB_GDMA_DESC_BYTES, 1,
	    AHB_GDMA_DESC_BYTES, 0, BUS_DMA_WAITOK, &dir->map);
	if (error != 0)
		goto fail_unmap;
	error = bus_dmamap_load(sc->dmat, dir->map, dir->descs,
	    AHB_GDMA_DESC_BYTES, NULL, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail_map;
	if (dir->map->dm_nsegs != 1 ||
	    dir->map->dm_segs[0].ds_addr < AHB_GDMA_ADDR_MIN ||
	    dir->map->dm_segs[0].ds_addr > AHB_GDMA_ADDR_MAX ||
	    (dir->map->dm_segs[0].ds_addr & 3) != 0 ||
	    AHB_GDMA_DESC_BYTES - 1 > AHB_GDMA_ADDR_MAX -
	    dir->map->dm_segs[0].ds_addr) {
		error = EFBIG;
		goto fail_loaded;
	}
	return 0;

fail_loaded:
	bus_dmamap_unload(sc->dmat, dir->map);
fail_map:
	bus_dmamap_destroy(sc->dmat, dir->map);
	dir->map = NULL;
fail_unmap:
	bus_dmamem_unmap(sc->dmat, dir->descs, AHB_GDMA_DESC_BYTES);
	dir->descs = NULL;
fail_mem:
	bus_dmamem_free(sc->dmat, &dir->memseg, nsegs);
	dir->mem_nsegs = 0;
	return error;
}

static void
esp_ahb_gdma_free_dir(struct esp_ahb_gdma_channel *ch,
    struct esp_ahb_gdma_dir *dir)
{
	struct esp_ahb_gdma_softc *sc = ch->sc;

	if (dir->map != NULL) {
		bus_dmamap_unload(sc->dmat, dir->map);
		bus_dmamap_destroy(sc->dmat, dir->map);
	}
	if (dir->descs != NULL)
		bus_dmamem_unmap(sc->dmat, dir->descs, AHB_GDMA_DESC_BYTES);
	if (dir->mem_nsegs != 0) {
		bus_dmamem_free(sc->dmat, &dir->memseg, dir->mem_nsegs);
		dir->mem_nsegs = 0;
	}
}

static void
esp_ahb_gdma_attach(device_t parent, device_t self, void *aux)
{
	struct esp_ahb_gdma_softc *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	struct esp_ahb_gdma_channel *ch;
	bus_addr_t addr;
	bus_size_t size;
	uint32_t control;
	unsigned int i;
	int error;

	sc->dev = self;
	sc->bst = faa->faa_bst;
	sc->dmat = faa->faa_dmat;
	sc->phandle = faa->faa_phandle;
	sc->syscon = fdtbus_syscon_acquire(sc->phandle, "espressif,syscon");
	if (sc->syscon == NULL || fdtbus_get_reg(sc->phandle, 0, &addr,
	    &size) != 0 || size < 0x628 ||
	    bus_space_map(sc->bst, addr, size, 0, &sc->bsh) != 0) {
		aprint_error(": missing registers or clock controller\n");
		return;
	}
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_VM);
	syscon_lock(sc->syscon);
	control = syscon_read_4(sc->syscon, AHB_GDMA_GLOBAL_CLK);
	syscon_write_4(sc->syscon, AHB_GDMA_GLOBAL_CLK,
	    control | AHB_GDMA_SYS_CLK_EN);
	syscon_write_4(sc->syscon, AHB_GDMA_GLOBAL_CLK,
	    control | AHB_GDMA_SYS_CLK_EN | AHB_GDMA_SYS_RESET);
	syscon_write_4(sc->syscon, AHB_GDMA_GLOBAL_CLK,
	    control | AHB_GDMA_SYS_CLK_EN);
	syscon_unlock(sc->syscon);
	control = AHB_READ(sc, AHB_GDMA_MISC);
	AHB_WRITE(sc, AHB_GDMA_MISC, control | AHB_GDMA_INT_CLK_EN);
	AHB_WRITE(sc, AHB_GDMA_MEM_START, (uint32_t)AHB_GDMA_ADDR_MIN);
	AHB_WRITE(sc, AHB_GDMA_MEM_END, (uint32_t)AHB_GDMA_ADDR_MAX);
	for (i = 0; i < AHB_GDMA_CHANNELS; i++) {
		ch = &sc->channel[i];
		ch->sc = sc;
		ch->index = i;
		ch->in.channel = ch;
		ch->out.channel = ch;
	}
	for (i = 0; i < AHB_GDMA_CHANNELS; i++) {
		ch = &sc->channel[i];
		error = esp_ahb_gdma_alloc_dir(ch, &ch->in, false);
		if (error != 0) {
			aprint_error(": RX descriptor allocation failed: %d\n", error);
			goto fail;
		}
		error = esp_ahb_gdma_alloc_dir(ch, &ch->out, true);
		if (error != 0) {
			aprint_error(": TX descriptor allocation failed: %d\n", error);
			goto fail;
		}
		AHB_WRITE(sc, AHB_IRQ_BASE(&ch->in, ch) + AHB_GDMA_IRQ_ENABLE, 0);
		AHB_WRITE(sc, AHB_IRQ_BASE(&ch->out, ch) + AHB_GDMA_IRQ_ENABLE, 0);
		AHB_WRITE(sc, AHB_IRQ_BASE(&ch->in, ch) + AHB_GDMA_IRQ_CLEAR,
		    UINT32_MAX);
		AHB_WRITE(sc, AHB_IRQ_BASE(&ch->out, ch) + AHB_GDMA_IRQ_CLEAR,
		    UINT32_MAX);
		ch->in.ih = fdtbus_intr_establish_xname(sc->phandle, i, IPL_VM,
		    FDT_INTR_MPSAFE, esp_ahb_gdma_intr, &ch->in,
		    device_xname(self));
		ch->out.ih = fdtbus_intr_establish_xname(sc->phandle,
		    AHB_GDMA_CHANNELS + i, IPL_VM, FDT_INTR_MPSAFE,
		    esp_ahb_gdma_intr, &ch->out, device_xname(self));
		if (ch->in.ih == NULL || ch->out.ih == NULL) {
			aprint_error(": missing channel interrupts\n");
			goto fail;
		}
		ch->in.softih = softint_establish(SOFTINT_SERIAL | SOFTINT_MPSAFE,
		    esp_ahb_gdma_softintr, &ch->in);
		ch->out.softih = softint_establish(SOFTINT_SERIAL | SOFTINT_MPSAFE,
		    esp_ahb_gdma_softintr, &ch->out);
		if (ch->in.softih == NULL || ch->out.softih == NULL) {
			aprint_error(": soft interrupt allocation failed\n");
			goto fail;
		}
	}
	fdtbus_register_dma_controller(self, sc->phandle, &esp_ahb_gdma_funcs);
	aprint_normal(": AHB DMA (%u channel pairs)\n", AHB_GDMA_CHANNELS);
	return;

fail:
	for (i = 0; i < AHB_GDMA_CHANNELS; i++) {
		ch = &sc->channel[i];
		AHB_WRITE(sc, AHB_IRQ_BASE(&ch->in, ch) + AHB_GDMA_IRQ_ENABLE, 0);
		AHB_WRITE(sc, AHB_IRQ_BASE(&ch->out, ch) + AHB_GDMA_IRQ_ENABLE, 0);
		if (ch->in.softih != NULL)
			softint_disestablish(ch->in.softih);
		if (ch->out.softih != NULL)
			softint_disestablish(ch->out.softih);
		if (ch->in.ih != NULL)
			fdtbus_intr_disestablish(sc->phandle, ch->in.ih);
		if (ch->out.ih != NULL)
			fdtbus_intr_disestablish(sc->phandle, ch->out.ih);
		if (ch->in.mem_nsegs != 0)
			esp_ahb_gdma_free_dir(ch, &ch->in);
		if (ch->out.mem_nsegs != 0)
			esp_ahb_gdma_free_dir(ch, &ch->out);
	}
	mutex_destroy(&sc->lock);
	bus_space_unmap(sc->bst, sc->bsh, size);
}

CFATTACH_DECL_NEW(espahbgdma, sizeof(struct esp_ahb_gdma_softc),
	 esp_ahb_gdma_match, esp_ahb_gdma_attach, NULL, NULL);

static void
esp_ahb_gdma_softintr(void *arg)
{
	struct esp_ahb_gdma_dir *dir = arg;
	struct esp_ahb_gdma_channel *ch = dir->channel;
	struct esp_ahb_gdma_softc *sc = ch->sc;
	void (*callback)(void *);
	void *callback_arg;
	size_t count;

	mutex_enter(&sc->lock);
	dir->softintr_scheduled = false;
	count = esp_ahb_gdma_dispatch_count(dir->callback_pending,
	    dir->stopping, dir->callback_count);
	dir->callback_count = 0;
	dir->callback_pending = false;
	callback = count != 0 && ch->claimed ? ch->callback : NULL;
	callback_arg = ch->callback_arg;
	if (callback != NULL)
		dir->callback_running = true;
	mutex_exit(&sc->lock);
	while (callback != NULL && count-- != 0)
		callback(callback_arg);
	if (callback != NULL) {
		mutex_enter(&sc->lock);
		dir->callback_running = false;
		mutex_exit(&sc->lock);
	}
}

static void *
esp_ahb_gdma_acquire(device_t dev, const void *data, size_t len,
	void (*callback)(void *), void *arg)
{
	struct esp_ahb_gdma_softc *sc = device_private(dev);
	struct esp_ahb_gdma_channel *ch = NULL;
	uint32_t trigger;
	unsigned int i;

	if (data == NULL || len != sizeof(uint32_t) || callback == NULL)
		return NULL;
	trigger = be32dec(data);
	if (trigger > 15)
		return NULL;
	mutex_enter(&sc->lock);
	for (i = 0; i < AHB_GDMA_CHANNELS; i++) {
		if (!sc->channel[i].claimed && !sc->channel[i].in.broken &&
		    !sc->channel[i].out.broken) {
			ch = &sc->channel[i];
			ch->claimed = true;
			ch->last_dir = NULL;
			ch->trigger = trigger;
			ch->callback = callback;
			ch->callback_arg = arg;
			break;
		}
	}
	mutex_exit(&sc->lock);
	return ch;
}

static int
esp_ahb_gdma_transfer(device_t dev, void *priv, struct fdtbus_dma_req *req)
{
	struct esp_ahb_gdma_softc *sc = device_private(dev);
	struct esp_ahb_gdma_channel *ch = priv;
	struct esp_ahb_gdma_dir *dir;
	bus_addr_t descbase;
	size_t i, n;
	size_t total_bytes = 0;
	int error;

	if (req == NULL || req->dreq_segs == NULL || req->dreq_nsegs <= 0 ||
	    req->dreq_nsegs > AHB_GDMA_MAX_DESC ||
	    (req->dreq_sel != 0 && req->dreq_sel != (int)ch->trigger) ||
	    req->dreq_flow != 0 ||
	    (req->dreq_mem_opt.opt_bus_width != 0 &&
	    req->dreq_mem_opt.opt_bus_width != 32) ||
	    (req->dreq_dev_opt.opt_bus_width != 0 &&
	    req->dreq_dev_opt.opt_bus_width != 32) ||
	    req->dreq_mem_opt.opt_burst_len != 0 ||
	    req->dreq_dev_opt.opt_burst_len != 0 ||
	    req->dreq_mem_opt.opt_swap || req->dreq_dev_opt.opt_swap ||
	    req->dreq_mem_opt.opt_dblbuf || req->dreq_dev_opt.opt_dblbuf ||
	    req->dreq_mem_opt.opt_wrap_len || req->dreq_dev_opt.opt_wrap_len)
		return ENOTSUP;
	dir = req->dreq_dir == FDT_DMA_READ ? &ch->in :
	    req->dreq_dir == FDT_DMA_WRITE ? &ch->out : NULL;
	if (dir == NULL || (req->dreq_block_multi && !req->dreq_block_irq))
		return EINVAL;
	for (i = 0; i < (size_t)req->dreq_nsegs; i++) {
		if (req->dreq_segs[i].ds_len > SIZE_MAX - total_bytes)
			return EFBIG;
		total_bytes += req->dreq_segs[i].ds_len;
	}

	mutex_enter(&sc->lock);
	if (!esp_ahb_gdma_can_start(ch->claimed, dir->running, dir->broken,
	    dir->stopping, dir->callback_pending, dir->callback_running,
	    dir->softintr_scheduled, dir->result_busy, dir->desc_synced)) {
		mutex_exit(&sc->lock);
		return EBUSY;
	}
	descbase = dir->map->dm_segs[0].ds_addr;
	error = esp_ahb_gdma_desc_build(dir->descs, AHB_GDMA_MAX_DESC,
	    descbase, req->dreq_segs, req->dreq_nsegs, dir->write,
	    req->dreq_block_irq, req->dreq_block_multi, &dir->ndesc);
	if (error != 0) {
		mutex_exit(&sc->lock);
		return error;
	}
	n = 0;
	for (i = 0; i < (size_t)req->dreq_nsegs; i++) {
		size_t block_descs = req->dreq_segs[i].ds_len /
		    AHB_DESC_MAX_BYTES +
		    (req->dreq_segs[i].ds_len % AHB_DESC_MAX_BYTES != 0);
		for (size_t j = 0; j < block_descs; j++, n++)
			dir->eof[n] = req->dreq_block_irq ?
			    j + 1 == block_descs :
			    i + 1 == (size_t)req->dreq_nsegs &&
			    j + 1 == block_descs;
	}
	dir->callback_count = 0;
	dir->last_eof = dir->ndesc - 1;
	dir->block_multi = req->dreq_block_multi != 0;
	dir->result_valid = false;
	dir->done = false;
	dir->error = 0;
	dir->bytes = total_bytes;
	bus_dmamap_sync(sc->dmat, dir->map, 0, AHB_GDMA_DESC_BYTES,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	dir->desc_synced = true;
	AHB_WRITE(sc, AHB_IRQ_BASE(dir, ch) + AHB_GDMA_IRQ_ENABLE, 0);
	AHB_WRITE(sc, AHB_IRQ_BASE(dir, ch) + AHB_GDMA_IRQ_CLEAR, UINT32_MAX);
	AHB_WRITE(sc, AHB_DIR_REG(dir, 0), AHB_GDMA_CONF_RESET);
	/* Cyclic buffers are reused; leave the optional owner check disabled. */
	AHB_WRITE(sc, AHB_DIR_REG(dir, 0), 0);
	AHB_WRITE(sc, AHB_DIR_BASE(dir) + 4, 0);
	AHB_WRITE(sc, AHB_PERI_SEL_REG(dir), ch->trigger);
	AHB_WRITE(sc, AHB_LINK_ADDR_REG(dir), (uint32_t)descbase);
	AHB_WRITE(sc, AHB_IRQ_BASE(dir, ch) + AHB_GDMA_IRQ_ENABLE,
	    AHB_IRQ_EOF(dir) | AHB_IRQ_ERRORS(dir));
	dir->running = true;
	ch->last_dir = dir;
	AHB_WRITE(sc, AHB_LINK_REG(dir), AHB_LINK_START(dir));
	bus_space_barrier(sc->bst, sc->bsh, AHB_DIR_BASE(dir), 0x100,
	    BUS_SPACE_BARRIER_WRITE);
	mutex_exit(&sc->lock);
	return 0;
}

static int
esp_ahb_gdma_intr(void *arg)
{
	struct esp_ahb_gdma_dir *dir = arg;
	struct esp_ahb_gdma_channel *ch = dir->channel;
	struct esp_ahb_gdma_softc *sc = ch->sc;
	const bus_size_t irqbase = AHB_IRQ_BASE(dir, ch);
	const uint32_t errors = AHB_IRQ_ERRORS(dir);
	uint32_t status, eofaddr, base;
	size_t index, callbacks = 0;
	bool complete;
	int handled = 0;

	mutex_enter(&sc->lock);
	status = AHB_READ(sc, irqbase + AHB_GDMA_IRQ_STATUS);
	if ((status & (AHB_IRQ_EOF(dir) | errors)) == 0) {
		mutex_exit(&sc->lock);
		return 0;
	}
	AHB_WRITE(sc, irqbase + AHB_GDMA_IRQ_CLEAR, status);
	handled = 1;
	if (dir->running && !dir->stopping) {
		if ((status & errors) != 0) {
			dir->error = EIO;
			AHB_WRITE(sc, AHB_LINK_REG(dir), AHB_LINK_STOP(dir));
			dir->running = false;
			dir->done = true;
			dir->callback_count = 1;
		} else if ((status & AHB_IRQ_EOF(dir)) != 0) {
			eofaddr = AHB_READ(sc, AHB_EOF_ADDR_REG(dir));
			base = dir->map->dm_segs[0].ds_addr;
			if (eofaddr < base || eofaddr >= base +
			    dir->ndesc * sizeof(*dir->descs) ||
			    ((eofaddr - base) % sizeof(*dir->descs)) != 0) {
				dir->error = EIO;
				AHB_WRITE(sc, AHB_LINK_REG(dir), AHB_LINK_STOP(dir));
				dir->running = false;
				dir->done = true;
				dir->callback_count = 1;
			} else {
				index = (eofaddr - base) / sizeof(*dir->descs);
				if (esp_ahb_gdma_advance_eof(&dir->last_eof,
				    dir->eof, dir->ndesc, index,
				    dir->block_multi, &callbacks, &complete) != 0) {
					dir->error = EIO;
					AHB_WRITE(sc, AHB_LINK_REG(dir),
					    AHB_LINK_STOP(dir));
					dir->running = false;
					dir->done = true;
					dir->callback_count = 1;
				} else if (callbacks > SIZE_MAX - dir->callback_count) {
					dir->error = EOVERFLOW;
					AHB_WRITE(sc, AHB_LINK_REG(dir),
					    AHB_LINK_STOP(dir));
					dir->running = false;
					dir->done = true;
					dir->callback_count = 1;
				} else {
					dir->callback_count += callbacks;
					if (complete) {
						dir->running = false;
						dir->done = true;
					}
				}
			}
		}
		if (dir->callback_count != 0) {
			dir->callback_pending = true;
			if (!dir->softintr_scheduled) {
				dir->softintr_scheduled = true;
				softint_schedule(dir->softih);
			}
		}
	}
	mutex_exit(&sc->lock);
	return handled;
}

static bool
esp_ahb_gdma_parked(struct esp_ahb_gdma_dir *dir)
{
	struct esp_ahb_gdma_softc *sc = dir->channel->sc;
	unsigned int n;

	for (n = 0; n < 10000; n++) {
		if (AHB_READ(sc, AHB_LINK_REG(dir)) & AHB_LINK_PARK(dir))
			return true;
		delay(1);
	}
	return false;
}

static int
esp_ahb_gdma_result(device_t dev, void *priv, struct fdtbus_dma_result *result)
{
	struct esp_ahb_gdma_channel *ch = priv;
	struct esp_ahb_gdma_softc *sc = device_private(dev);
	struct esp_ahb_gdma_dir *dir;

	mutex_enter(&sc->lock);
	dir = ch->last_dir;
	if (dir == NULL) {
		mutex_exit(&sc->lock);
		return EAGAIN;
	}
	if (dir->running || !dir->done || dir->stopping) {
		mutex_exit(&sc->lock);
		return EAGAIN;
	}
	if (dir->result_valid) {
		*result = dir->result;
		mutex_exit(&sc->lock);
		return 0;
	}
	/* Errors are published by the IRQ before the callback is queued. */
	if (esp_ahb_gdma_error_ready(dir->done, dir->stopping, dir->error)) {
		dir->result.dres_error = dir->error;
		dir->result.dres_bytes = 0;
		dir->result_valid = true;
		*result = dir->result;
		mutex_exit(&sc->lock);
		return 0;
	}
	if (dir->callback_running || dir->result_busy) {
		mutex_exit(&sc->lock);
		return EBUSY;
	}
	dir->result_busy = true;
	mutex_exit(&sc->lock);
	if (!esp_ahb_gdma_parked(dir)) {
		mutex_enter(&sc->lock);
		dir->broken = true;
		dir->result_busy = false;
		mutex_exit(&sc->lock);
		return EIO;
	}
	mutex_enter(&sc->lock);
	if (dir->stopping || !dir->desc_synced) {
		dir->result_busy = false;
		mutex_exit(&sc->lock);
		return EAGAIN;
	}
	bus_dmamap_sync(sc->dmat, dir->map, 0, AHB_GDMA_DESC_BYTES,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	dir->desc_synced = false;
	dir->result.dres_error = dir->error;
	dir->result.dres_bytes = dir->result.dres_error ? 0 : dir->bytes;
	dir->result_valid = true;
	*result = dir->result;
	dir->result_busy = false;
	mutex_exit(&sc->lock);
	return 0;
}

static int
esp_ahb_gdma_halt_dir(struct esp_ahb_gdma_dir *dir)
{
	struct esp_ahb_gdma_channel *ch = dir->channel;
	struct esp_ahb_gdma_softc *sc = ch->sc;
	unsigned int n;
	bool stop, failed;

	mutex_enter(&sc->lock);
	AHB_WRITE(sc, AHB_IRQ_BASE(dir, ch) + AHB_GDMA_IRQ_ENABLE, 0);
	AHB_WRITE(sc, AHB_IRQ_BASE(dir, ch) + AHB_GDMA_IRQ_CLEAR, UINT32_MAX);
	stop = esp_ahb_gdma_halt_required(dir->running, dir->done,
	    dir->desc_synced, dir->broken);
	dir->stopping = true;
	if (stop)
		AHB_WRITE(sc, AHB_LINK_REG(dir), AHB_LINK_STOP(dir));
	dir->running = false;
	dir->done = false;
	dir->callback_pending = false;
	dir->callback_count = 0;
	mutex_exit(&sc->lock);
	failed = stop && !esp_ahb_gdma_parked(dir);
	for (n = 0; n < 10000; n++) {
		bool callback_running, result_busy, softintr_scheduled;

		mutex_enter(&sc->lock);
		callback_running = dir->callback_running;
		result_busy = dir->result_busy;
		softintr_scheduled = dir->softintr_scheduled;
		mutex_exit(&sc->lock);
		if (esp_ahb_gdma_callbacks_drained(callback_running, result_busy,
		    softintr_scheduled))
			break;
		delay(1);
	}
	failed |= n == 10000;
	mutex_enter(&sc->lock);
	if (!failed) {
		if (stop) {
			AHB_WRITE(sc, AHB_DIR_REG(dir, 0), AHB_GDMA_CONF_RESET);
			if (dir->desc_synced) {
				bus_dmamap_sync(sc->dmat, dir->map, 0,
				    AHB_GDMA_DESC_BYTES,
				    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
				dir->desc_synced = false;
			}
		}
		dir->result_valid = false;
		dir->broken = false;
	}
	if (failed)
		dir->broken = true;
	dir->stopping = false;
	mutex_exit(&sc->lock);
	return failed ? EBUSY : 0;
}

static int
esp_ahb_gdma_halt_checked(device_t dev, void *priv)
{
	struct esp_ahb_gdma_channel *ch = priv;
	int error;

	(void)dev;
	error = esp_ahb_gdma_halt_dir(&ch->in);
	if (error != 0)
		return error;
	return esp_ahb_gdma_halt_dir(&ch->out);
}

static void
esp_ahb_gdma_halt(device_t dev, void *priv)
{
	struct esp_ahb_gdma_channel *ch = priv;

	if (esp_ahb_gdma_halt_checked(dev, priv) != 0)
		panic("%s: AHB DMA channel pair %u failed to stop", device_xname(dev),
		    ch->index);
}

static void
esp_ahb_gdma_release(device_t dev, void *priv)
{
	struct esp_ahb_gdma_softc *sc = device_private(dev);
	struct esp_ahb_gdma_channel *ch = priv;

	esp_ahb_gdma_halt(dev, ch);
	softint_disestablish(ch->in.softih);
	softint_disestablish(ch->out.softih);
	mutex_enter(&sc->lock);
	ch->in.softih = NULL;
	ch->out.softih = NULL;
	ch->callback = NULL;
	ch->callback_arg = NULL;
	ch->last_dir = NULL;
	ch->claimed = false;
	mutex_exit(&sc->lock);
}
