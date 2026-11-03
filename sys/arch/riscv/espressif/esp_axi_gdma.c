/* $NetBSD$ */
/* SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-FileCopyrightText: 2026 The EmberBSD Project */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ESP32-S31 AXI GDMA receive controller. Register and descriptor facts are
 * from Espressif ESP-IDF v6.1, commit fff9895c82d744c7237be8847347bdd1b07c6643,
 * components/soc/esp32s31/register/soc/axi_dma_reg.h and
 * components/esp_hal_dma/esp32s31/include/hal/axi_dma_ll.h (Apache-2.0).
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$EmberBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/intr.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/syscon.h>

#include "esp_axi_gdma_desc.h"

#define GDMA_CHANNELS 3
#define GDMA_MAX_DESC 1024
#define GDMA_DESC_BYTES (GDMA_MAX_DESC * sizeof(struct esp_axi_gdma_desc))
#define GDMA_STRIDE 0x68
#define GDMA_INT_STATUS 0x04
#define GDMA_INT_ENABLE 0x08
#define GDMA_INT_CLEAR 0x0c
#define GDMA_CONF0 0x10
#define GDMA_CONF1 0x14
#define GDMA_LINK1 0x20
#define GDMA_LINK2 0x24
#define GDMA_PERI_SEL 0x44
#define GDMA_INT_SUCCESS __BIT(1)
#define GDMA_INT_ERRORS __BITS(10,2)
#define GDMA_CONF0_RESET __BIT(0)
#define GDMA_CONF0_BURST_32 __SHIFTIN(2, __BITS(6,4))
#define GDMA_CONF0_DESC_BURST __BIT(9)
#define GDMA_CONF1_OWNER_CHECK __BIT(12)
#define GDMA_LINK_STOP __BIT(1)
#define GDMA_LINK_START __BIT(2)
#define GDMA_LINK_PARK __BIT(4)
#define GDMA_INTERNAL_START 0x27c
#define GDMA_INTERNAL_END 0x280
#define GDMA_EXTERNAL_START 0x284
#define GDMA_EXTERNAL_END 0x288
#define GDMA_MISC 0x2a8
#define GDMA_MISC_CLOCK __BIT(4)
#define GDMA_CLKRST 0x78
#define GDMA_SYS_CLOCK __BIT(0)
#define GDMA_REG_SIZE 0x2ac

struct esp_axi_gdma_softc;
struct esp_axi_gdma_channel {
	struct esp_axi_gdma_softc *sc;
	unsigned int index;
	bool claimed, running, done, broken, result_busy, result_valid, stopping;
	bool callback_running;
	bool desc_synced, callback_pending;
	int error;
	struct fdtbus_dma_result result;
	uint32_t trigger;
	void (*callback)(void *);
	void *callback_arg;
	struct esp_axi_gdma_desc *descs;
	bus_dma_segment_t memseg;
	bus_dmamap_t map;
	size_t ndesc;
	void *ih;
	void *softih;
};
struct esp_axi_gdma_softc {
	device_t dev;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	bus_dma_tag_t dmat;
	struct syscon *syscon;
	kmutex_t lock;
	int phandle;
	struct esp_axi_gdma_channel chan[GDMA_CHANNELS];
};

#define GR(sc, ch, reg) bus_space_read_4((sc)->bst, (sc)->bsh, \
    (ch)->index * GDMA_STRIDE + (reg))
#define GW(sc, ch, reg, val) bus_space_write_4((sc)->bst, (sc)->bsh, \
    (ch)->index * GDMA_STRIDE + (reg), (val))

static int esp_axi_gdma_intr(void *);
static void *esp_axi_gdma_acquire(device_t, const void *, size_t,
    void (*)(void *), void *);
static void esp_axi_gdma_release(device_t, void *);
static int esp_axi_gdma_transfer(device_t, void *, struct fdtbus_dma_req *);
static void esp_axi_gdma_halt(device_t, void *);
static int esp_axi_gdma_halt_checked(device_t, void *);
static int esp_axi_gdma_result(device_t, void *, struct fdtbus_dma_result *);

static const struct fdtbus_dma_controller_func esp_axi_gdma_funcs = {
	.acquire = esp_axi_gdma_acquire,
	.release = esp_axi_gdma_release,
	.transfer = esp_axi_gdma_transfer,
	.halt = esp_axi_gdma_halt,
	.result = esp_axi_gdma_result,
	.halt_checked = esp_axi_gdma_halt_checked,
};
static const struct device_compatible_entry esp_axi_gdma_compat[] = {
	{ .compat = "espressif,esp32s31-axi-gdma" },
	DEVICE_COMPAT_EOL
};

static int
esp_axi_gdma_match(device_t parent, cfdata_t match, void *aux)
{
	const struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, esp_axi_gdma_compat);
}

/* Only call after the channel has stopped and no callback can be running. */
static void
esp_axi_gdma_free(struct esp_axi_gdma_channel *ch)
{
	struct esp_axi_gdma_softc *sc = ch->sc;

	if (ch->map != NULL) {
		bus_dmamap_unload(sc->dmat, ch->map);
		bus_dmamap_destroy(sc->dmat, ch->map);
	}
	if (ch->descs != NULL)
		bus_dmamem_unmap(sc->dmat, ch->descs, GDMA_DESC_BYTES);
	bus_dmamem_free(sc->dmat, &ch->memseg, 1);
	ch->map = NULL;
	ch->descs = NULL;
}

static int
esp_axi_gdma_alloc(struct esp_axi_gdma_channel *ch)
{
	struct esp_axi_gdma_softc *sc = ch->sc;
	int error, nsegs;

	error = bus_dmamem_alloc(sc->dmat, GDMA_DESC_BYTES, 64, 0,
	    &ch->memseg, 1, &nsegs, BUS_DMA_WAITOK);
	if (error)
		return error;
	error = bus_dmamem_map(sc->dmat, &ch->memseg, nsegs,
	    GDMA_DESC_BYTES, (void **)&ch->descs, BUS_DMA_WAITOK);
	if (error)
		goto fail_mem;
	error = bus_dmamap_create(sc->dmat, GDMA_DESC_BYTES, 1,
	    GDMA_DESC_BYTES, 0, BUS_DMA_WAITOK, &ch->map);
	if (error)
		goto fail_mapping;
	error = bus_dmamap_load(sc->dmat, ch->map, ch->descs,
	    GDMA_DESC_BYTES, NULL, BUS_DMA_WAITOK);
	if (error)
		goto fail_map;
	return 0;
fail_map:
	bus_dmamap_destroy(sc->dmat, ch->map);
	ch->map = NULL;
fail_mapping:
	bus_dmamem_unmap(sc->dmat, ch->descs, GDMA_DESC_BYTES);
	ch->descs = NULL;
fail_mem:
	bus_dmamem_free(sc->dmat, &ch->memseg, nsegs);
	return error;
}

static void
esp_axi_gdma_attach(device_t parent, device_t self, void *aux)
{
	struct esp_axi_gdma_softc *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	struct esp_axi_gdma_channel *ch;
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
	    &size) != 0 || size < GDMA_REG_SIZE ||
	    bus_space_map(sc->bst, addr, size, 0, &sc->bsh) != 0) {
		aprint_error(": missing registers or clock controller\n");
		return;
	}
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_VM);
	syscon_lock(sc->syscon);
	control = syscon_read_4(sc->syscon, GDMA_CLKRST);
	syscon_write_4(sc->syscon, GDMA_CLKRST, control | GDMA_SYS_CLOCK);
	syscon_unlock(sc->syscon);
	control = bus_space_read_4(sc->bst, sc->bsh, GDMA_MISC);
	bus_space_write_4(sc->bst, sc->bsh, GDMA_MISC,
	    control | GDMA_MISC_CLOCK);
	bus_space_write_4(sc->bst, sc->bsh, GDMA_INTERNAL_START, 0x2f000000);
	bus_space_write_4(sc->bst, sc->bsh, GDMA_INTERNAL_END, 0x2f07ffff);
	bus_space_write_4(sc->bst, sc->bsh, GDMA_EXTERNAL_START, 0x40000000);
	bus_space_write_4(sc->bst, sc->bsh, GDMA_EXTERNAL_END, 0x53ffffff);
	for (i = 0; i < GDMA_CHANNELS; i++) {
		ch = &sc->chan[i];
		ch->sc = sc;
		ch->index = i;
		GW(sc, ch, GDMA_INT_ENABLE, 0);
		GW(sc, ch, GDMA_INT_CLEAR, UINT32_MAX);
		error = esp_axi_gdma_alloc(ch);
		if (error) {
			aprint_error(": descriptor allocation failed: %d\n", error);
			goto fail;
		}
		ch->ih = fdtbus_intr_establish_xname(sc->phandle, i, IPL_VM,
		    FDT_INTR_MPSAFE, esp_axi_gdma_intr, ch, device_xname(self));
		if (ch->ih == NULL) {
			esp_axi_gdma_free(ch);
			aprint_error(": missing RX interrupt %u\n", i);
			goto fail;
		}
	}
	fdtbus_register_dma_controller(self, sc->phandle, &esp_axi_gdma_funcs);
	aprint_normal(": AXI receive DMA (%u channels)\n", GDMA_CHANNELS);
	return;
fail:
	while (i > 0) {
		ch = &sc->chan[--i];
		fdtbus_intr_disestablish(sc->phandle, ch->ih);
		esp_axi_gdma_free(ch);
	}
	mutex_destroy(&sc->lock);
	bus_space_unmap(sc->bst, sc->bsh, size);
}

CFATTACH_DECL_NEW(espaxigdma, sizeof(struct esp_axi_gdma_softc),
    esp_axi_gdma_match, esp_axi_gdma_attach, NULL, NULL);

static void
esp_axi_gdma_softintr(void *arg)
{
	struct esp_axi_gdma_channel *ch = arg;
	struct esp_axi_gdma_softc *sc = ch->sc;
	void (*callback)(void *);
	void *callback_arg;

	mutex_enter(&sc->lock);
	callback = ch->claimed && ch->done && ch->callback_pending ?
	    ch->callback : NULL;
	callback_arg = ch->callback_arg;
	if (callback != NULL)
		ch->callback_running = true;
	/* Consume this completion before calling out, preventing duplicate events. */
	ch->callback_pending = false;
	mutex_exit(&sc->lock);
	if (callback != NULL) {
		callback(callback_arg);
		mutex_enter(&sc->lock);
		ch->callback_running = false;
		mutex_exit(&sc->lock);
	}
}

static void *
esp_axi_gdma_acquire(device_t dev, const void *data, size_t len,
    void (*callback)(void *), void *arg)
{
	struct esp_axi_gdma_softc *sc = device_private(dev);
	struct esp_axi_gdma_channel *ch = NULL;
	uint32_t trigger;
	unsigned int i;

	if (data == NULL || len != sizeof(uint32_t) || callback == NULL)
		return NULL;
	trigger = be32dec(data);
	if (trigger > 5)
		return NULL;
	mutex_enter(&sc->lock);
	for (i = 0; i < GDMA_CHANNELS; i++) {
		if (sc->chan[i].claimed && sc->chan[i].trigger == trigger) {
			mutex_exit(&sc->lock);
			return NULL;
		}
	}
	for (i = 0; i < GDMA_CHANNELS; i++) {
		if (!sc->chan[i].claimed && !sc->chan[i].broken &&
		    !sc->chan[i].result_busy && !sc->chan[i].stopping) {
			ch = &sc->chan[i];
			ch->claimed = true;
			ch->trigger = trigger;
			break;
		}
	}
	mutex_exit(&sc->lock);
	if (ch == NULL)
		return NULL;
	ch->softih = softint_establish(SOFTINT_SERIAL | SOFTINT_MPSAFE,
	    esp_axi_gdma_softintr, ch);
	if (ch->softih == NULL) {
		mutex_enter(&sc->lock);
		ch->claimed = false;
		mutex_exit(&sc->lock);
		return NULL;
	}
	ch->callback = callback;
	ch->callback_arg = arg;
	return ch;
}

static int
esp_axi_gdma_transfer(device_t dev, void *priv, struct fdtbus_dma_req *req)
{
	struct esp_axi_gdma_softc *sc = device_private(dev);
	struct esp_axi_gdma_channel *ch = priv;
	int i;
	int error;

	if (req == NULL || req->dreq_nsegs <= 0 ||
	    req->dreq_nsegs > GDMA_MAX_DESC || req->dreq_segs == NULL)
		return EINVAL;
	if (req->dreq_dir != FDT_DMA_READ || req->dreq_block_multi ||
	    (req->dreq_block_irq && req->dreq_nsegs > 1) ||
	    (req->dreq_sel != 0 && req->dreq_sel != (int)ch->trigger) ||
	    (req->dreq_mem_opt.opt_bus_width != 0 &&
	    req->dreq_mem_opt.opt_bus_width != 8) ||
	    (req->dreq_dev_opt.opt_bus_width != 0 &&
	    req->dreq_dev_opt.opt_bus_width != 8) ||
	    req->dreq_mem_opt.opt_burst_len != 0 ||
	    req->dreq_dev_opt.opt_burst_len != 0 ||
	    req->dreq_mem_opt.opt_swap || req->dreq_dev_opt.opt_swap ||
	    req->dreq_mem_opt.opt_dblbuf || req->dreq_dev_opt.opt_dblbuf ||
	    req->dreq_mem_opt.opt_wrap_len || req->dreq_dev_opt.opt_wrap_len)
		return ENOTSUP;
	/* Cover external-memory encryption/ECC and cache-line constraints. */
	for (i = 0; i < req->dreq_nsegs; i++) {
		if ((req->dreq_segs[i].ds_addr & 63) != 0 ||
		    (req->dreq_segs[i].ds_len & 63) != 0)
			return EINVAL;
	}
	mutex_enter(&sc->lock);
	if (!ch->claimed || ch->running || ch->broken || ch->result_busy ||
	    ch->stopping || ch->callback_pending || ch->callback_running ||
	    ch->desc_synced) {
		mutex_exit(&sc->lock);
		return EBUSY;
	}
	error = esp_axi_gdma_desc_build(ch->descs, GDMA_MAX_DESC,
	    ch->map->dm_segs[0].ds_addr, req->dreq_segs, req->dreq_nsegs,
	    &ch->ndesc);
	if (error) {
		mutex_exit(&sc->lock);
		return error;
	}
	ch->done = false;
	ch->result_valid = false;
	ch->error = 0;
	bus_dmamap_sync(sc->dmat, ch->map, 0, GDMA_DESC_BYTES,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	ch->desc_synced = true;
	GW(sc, ch, GDMA_INT_ENABLE, 0);
	GW(sc, ch, GDMA_INT_CLEAR, UINT32_MAX);
	GW(sc, ch, GDMA_CONF0, GDMA_CONF0_RESET);
	GW(sc, ch, GDMA_CONF0, GDMA_CONF0_DESC_BURST | GDMA_CONF0_BURST_32);
	GW(sc, ch, GDMA_CONF1, GDMA_CONF1_OWNER_CHECK);
	GW(sc, ch, GDMA_PERI_SEL, ch->trigger);
	GW(sc, ch, GDMA_LINK2, ch->map->dm_segs[0].ds_addr);
	GW(sc, ch, GDMA_INT_ENABLE, GDMA_INT_SUCCESS | GDMA_INT_ERRORS);
	ch->running = true;
	GW(sc, ch, GDMA_LINK1, GDMA_LINK_START);
	mutex_exit(&sc->lock);
	return 0;
}

static int
esp_axi_gdma_intr(void *arg)
{
	struct esp_axi_gdma_channel *ch = arg;
	struct esp_axi_gdma_softc *sc = ch->sc;
	uint32_t status;

	mutex_enter(&sc->lock);
	status = GR(sc, ch, GDMA_INT_STATUS);
	if ((status & (GDMA_INT_SUCCESS | GDMA_INT_ERRORS)) == 0) {
		mutex_exit(&sc->lock);
		return 0;
	}
	GW(sc, ch, GDMA_INT_CLEAR, status);
	GW(sc, ch, GDMA_INT_ENABLE, 0);
	GW(sc, ch, GDMA_LINK1, GDMA_LINK_STOP);
	if (ch->claimed && ch->running) {
		ch->running = false;
		ch->done = true;
		ch->error = (status & GDMA_INT_ERRORS) ? EIO : 0;
		ch->callback_pending = true;
		softint_schedule(ch->softih);
	}
	mutex_exit(&sc->lock);
	return 1;
}

static bool
esp_axi_gdma_parked(struct esp_axi_gdma_channel *ch)
{
	struct esp_axi_gdma_softc *sc = ch->sc;
	unsigned int n;

	for (n = 0; n < 10000; n++) {
		if (GR(sc, ch, GDMA_LINK1) & GDMA_LINK_PARK)
			return true;
		delay(1);
	}
	return false;
}

static int
esp_axi_gdma_result(device_t dev, void *priv, struct fdtbus_dma_result *result)
{
	struct esp_axi_gdma_softc *sc = device_private(dev);
	struct esp_axi_gdma_channel *ch = priv;
	size_t bytes = 0;
	int error;

	mutex_enter(&sc->lock);
	if (!ch->claimed || !ch->done || ch->result_busy || ch->stopping) {
		mutex_exit(&sc->lock);
		return EAGAIN;
	}
	if (ch->result_valid) {
		*result = ch->result;
		mutex_exit(&sc->lock);
		return 0;
	}
	ch->result_busy = true;
	mutex_exit(&sc->lock);
	if (!esp_axi_gdma_parked(ch)) {
		mutex_enter(&sc->lock);
		ch->broken = true;
		ch->result_busy = false;
		mutex_exit(&sc->lock);
		return EIO;
	}
	mutex_enter(&sc->lock);
	if (!ch->claimed || !ch->done) {
		ch->result_busy = false;
		mutex_exit(&sc->lock);
		return EAGAIN;
	}
	bus_dmamap_sync(sc->dmat, ch->map, 0, GDMA_DESC_BYTES,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	ch->desc_synced = false;
	error = esp_axi_gdma_desc_result(ch->descs, ch->ndesc, &bytes);
	result->dres_error = ch->error ? ch->error : error;
	result->dres_bytes = result->dres_error ? 0 : bytes;
	ch->result = *result;
	ch->result_valid = true;
	ch->result_busy = false;
	mutex_exit(&sc->lock);
	return 0;
}

static int
esp_axi_gdma_halt_checked(device_t dev, void *priv)
{
	struct esp_axi_gdma_softc *sc = device_private(dev);
	struct esp_axi_gdma_channel *ch = priv;
	bool failed, stop;
	unsigned int n;

	mutex_enter(&sc->lock);
	GW(sc, ch, GDMA_INT_ENABLE, 0);
	GW(sc, ch, GDMA_INT_CLEAR, UINT32_MAX);
	stop = ch->running || ch->done || ch->desc_synced;
	ch->stopping = true;
	if (stop)
		GW(sc, ch, GDMA_LINK1, GDMA_LINK_STOP);
	ch->running = false;
	ch->done = false;
	ch->result_valid = false;
	ch->callback_pending = false;
	mutex_exit(&sc->lock);
	failed = stop && !esp_axi_gdma_parked(ch);
	for (n = 0; n < 10000; n++) {
		bool callback_running, result_busy;

		mutex_enter(&sc->lock);
		result_busy = ch->result_busy;
		callback_running = ch->callback_running;
		mutex_exit(&sc->lock);
		if (!result_busy && !callback_running)
			break;
		delay(1);
	}
	failed |= n == 10000;
	mutex_enter(&sc->lock);
	if (!failed && stop)
		GW(sc, ch, GDMA_CONF0, GDMA_CONF0_RESET);
	if (!failed && ch->desc_synced) {
		bus_dmamap_sync(sc->dmat, ch->map, 0, GDMA_DESC_BYTES,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		/* PARK retires the active chain; descriptors beyond EOF can stay owned. */
		ch->desc_synced = false;
	}
	if (failed)
		ch->broken = true;
	ch->stopping = false;
	mutex_exit(&sc->lock);
	return failed ? EBUSY : 0;
}

static void
esp_axi_gdma_halt(device_t dev, void *priv)
{
	struct esp_axi_gdma_channel *ch = priv;

	if (esp_axi_gdma_halt_checked(dev, priv) != 0)
		panic("%s: DMA channel %u failed to park or drain callbacks",
		    device_xname(dev), ch->index);
}

static void
esp_axi_gdma_release(device_t dev, void *priv)
{
	struct esp_axi_gdma_softc *sc = device_private(dev);
	struct esp_axi_gdma_channel *ch = priv;

	esp_axi_gdma_halt(dev, ch);
	softint_disestablish(ch->softih);
	mutex_enter(&sc->lock);
	ch->softih = NULL;
	ch->callback = NULL;
	ch->callback_arg = NULL;
	ch->claimed = false;
	mutex_exit(&sc->lock);
}
