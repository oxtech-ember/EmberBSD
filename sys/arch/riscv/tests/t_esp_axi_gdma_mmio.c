/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 The EmberBSD Project. */
/* Host model; the runner substitutes unmodified production functions. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/bus.h>
#include "esp_axi_gdma_desc.h"

#define __BIT(n) (1U << (n))
#define __BITS(h,l) (((1U << ((h) + 1)) - 1) & ~((1U << (l)) - 1))
#define __SHIFTIN(v,m) ((v) << __builtin_ctz(m))
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_PREWRITE 2
#define BUS_DMASYNC_POSTREAD 4
#define BUS_DMASYNC_POSTWRITE 8
#define SOFTINT_SERIAL 1
#define SOFTINT_MPSAFE 2
#define device_private(d) (d)
#define mutex_enter(p) ((void)(p))
#define mutex_exit(p) ((void)(p))
#define aprint_error_dev(...) ((void)0)

typedef void *device_t;
static const char *device_xname(device_t dev) { (void)dev; return "gdma-test"; }
static void panic(const char *fmt, ...) { (void)fmt; abort(); }
typedef int bus_space_tag_t;
typedef int bus_space_handle_t;
typedef int bus_dma_tag_t;
typedef int kmutex_t;
struct syscon;
struct dma_map { bus_dma_segment_t dm_segs[1]; };
typedef struct dma_map *bus_dmamap_t;
struct fdtbus_dma_opt {
	int opt_bus_width, opt_burst_len, opt_swap, opt_dblbuf, opt_wrap_len;
};
enum fdtbus_dma_dir { FDT_DMA_READ, FDT_DMA_WRITE };
struct fdtbus_dma_req {
	bus_dma_segment_t *dreq_segs;
	int dreq_nsegs;
	uint64_t dreq_dev_phys;
	int dreq_sel;
	enum fdtbus_dma_dir dreq_dir;
	int dreq_block_irq, dreq_block_multi, dreq_flow;
	struct fdtbus_dma_opt dreq_mem_opt, dreq_dev_opt;
};
struct fdtbus_dma_result { int dres_error; size_t dres_bytes; };
static int esp_axi_gdma_transfer(device_t, void *, struct fdtbus_dma_req *);

static uint32_t regs[512];
static unsigned int pre, post, scheduled;
static bool park_on_stop = true;
static bool callback_try_transfer;
static unsigned int callbacks;
static void *callback_dev;
static void *callback_priv;
static struct fdtbus_dma_req *callback_req;
static bool *delay_drain_flag;

static void
test_delay(unsigned int ticks)
{
	(void)ticks;
	if (delay_drain_flag != NULL) {
		*delay_drain_flag = false;
		delay_drain_flag = NULL;
	}
}

#define delay(n) test_delay(n)

static uint32_t
bus_space_read_4(int tag, int handle, size_t off)
{
	(void)tag;
	(void)handle;
	return regs[off / 4];
}

static void
bus_space_write_4(int tag, int handle, size_t off, uint32_t value)
{
	(void)tag;
	(void)handle;
	regs[off / 4] = value;
	if (off == 0x20 && value == (1U << 1) && park_on_stop)
		regs[off / 4] |= 1U << 4;
}

static void
bus_dmamap_sync(int tag, bus_dmamap_t map, size_t off, size_t len, int flags)
{
	(void)tag;
	(void)map;
	(void)off;
	(void)len;
	if (flags == (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE))
		pre++;
	else if (flags == (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE))
		post++;
	else
		abort();
}
static void softint_schedule(void *p) { assert(p != NULL); scheduled++; }
static void *
softint_establish(int flags, void (*fn)(void *), void *arg)
{
	(void)flags;
	(void)fn;
	return arg;
}
static uint32_t
be32dec(const void *p)
{
	const unsigned char *b = p;

	return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
	    ((uint32_t)b[2] << 8) | b[3];
}
static void
callback(void *p)
{
	(void)p;
	callbacks++;
	if (callback_try_transfer)
		assert(esp_axi_gdma_transfer(callback_dev, callback_priv,
		    callback_req) == EBUSY);
}

/* INCLUDE_PRODUCTION */

int
main(void)
{
	struct esp_axi_gdma_softc sc = {0};
	struct esp_axi_gdma_channel *ch = &sc.chan[0];
	struct esp_axi_gdma_desc desc[GDMA_MAX_DESC];
	struct dma_map map = { .dm_segs = {{ 0x50000000, 16384 }} };
	bus_dma_segment_t seg = { 0x50010000, 64 };
	struct fdtbus_dma_req req = { .dreq_segs = &seg, .dreq_nsegs = 1,
	    .dreq_dir = FDT_DMA_READ };
	struct fdtbus_dma_result result;
	const unsigned char camera[4] = { 0, 0, 0, 0 };
	const unsigned char spi[4] = { 0, 0, 0, 1 };
	unsigned int i;

	ch->sc = &sc;
	ch->claimed = true;
	ch->softih = &sc;
	ch->callback = callback;
	ch->descs = desc;
	ch->map = &map;
	callback_dev = &sc;
	callback_priv = ch;
	callback_req = &req;
	assert(esp_axi_gdma_transfer(&sc, ch, &req) == 0);
	assert(pre == 1 && ch->running && regs[GDMA_LINK2 / 4] == 0x50000000);
	assert(regs[GDMA_PERI_SEL / 4] == 0 &&
	    regs[GDMA_LINK1 / 4] == GDMA_LINK_START);
	assert(esp_axi_gdma_transfer(&sc, ch, &req) == EBUSY);
	desc[0].control = 64 | (64U << 12) | ESP_AXI_GDMA_DESC_SUC_EOF;
	regs[GDMA_INT_STATUS / 4] = GDMA_INT_SUCCESS;
	assert(esp_axi_gdma_intr(ch) == 1 && scheduled == 1);
	assert(esp_axi_gdma_result(&sc, ch, &result) == 0);
	assert(result.dres_error == 0 && result.dres_bytes == 64 && post == 1);
	assert(esp_axi_gdma_result(&sc, ch, &result) == 0 && post == 1);
	callback_try_transfer = true;
	esp_axi_gdma_softintr(ch);
	callback_try_transfer = false;
	assert(callbacks == 1 && !ch->callback_running &&
	    !ch->callback_pending);
	/* Model another CPU finishing a callback while halt waits to reuse it. */
	ch->callback_running = true;
	delay_drain_flag = &ch->callback_running;
	assert(esp_axi_gdma_halt_checked(&sc, ch) == 0);
	assert(!ch->callback_running && !ch->done);
	assert(esp_axi_gdma_transfer(&sc, ch, &req) == 0);
	desc[0].control = 64 | (64U << 12) | ESP_AXI_GDMA_DESC_SUC_EOF;
	regs[GDMA_INT_STATUS / 4] = __BIT(3);
	assert(esp_axi_gdma_intr(ch) == 1);
	assert(esp_axi_gdma_result(&sc, ch, &result) == 0 &&
	    result.dres_error == EIO && result.dres_bytes == 0);
	esp_axi_gdma_softintr(ch);
	assert(callbacks == 2 && !ch->callback_running);
	/* A duplicate queued soft interrupt must consume no second callback. */
	esp_axi_gdma_softintr(ch);
	assert(callbacks == 2);
	assert(esp_axi_gdma_transfer(&sc, ch, &req) == 0);
	park_on_stop = false;
	/* A stop timeout retains the DMA mapping; later PARK allows retirement. */
	assert(esp_axi_gdma_halt_checked(&sc, ch) == EBUSY && ch->broken);
	park_on_stop = true;
	assert(esp_axi_gdma_halt_checked(&sc, ch) == 0);
	esp_axi_gdma_halt(&sc, ch);
	seg.ds_addr++;
	assert(esp_axi_gdma_transfer(&sc, ch, &req) == EINVAL);

	/* A short EOF reports the completed prefix; an untouched tail stays DMA-owned. */
	memset(&sc, 0, sizeof(sc));
	memset(regs, 0, sizeof(regs));
	ch = &sc.chan[0];
	ch->sc = &sc;
	ch->claimed = true;
	ch->softih = &sc;
	ch->callback = callback;
	ch->descs = desc;
	ch->map = &map;
	seg.ds_addr = 0x50010000;
	seg.ds_len = 8192;
	assert(esp_axi_gdma_transfer(&sc, ch, &req) == 0 && ch->ndesc == 3);
	desc[0].control = 4032 | (100U << 12) | ESP_AXI_GDMA_DESC_SUC_EOF;
	regs[GDMA_INT_STATUS / 4] = GDMA_INT_SUCCESS;
	assert(esp_axi_gdma_intr(ch) == 1);
	assert(esp_axi_gdma_result(&sc, ch, &result) == 0 &&
	    result.dres_error == 0 && result.dres_bytes == 100 && !ch->broken);
	assert((desc[1].control & ESP_AXI_GDMA_DESC_OWNER) != 0 &&
	    (desc[2].control & ESP_AXI_GDMA_DESC_OWNER) != 0);
	assert(esp_axi_gdma_halt_checked(&sc, ch) == 0 && !ch->broken);
	assert(!ch->desc_synced && post == pre);
	/* Cancel before EOF: parked/reset permits retiring the untouched descriptor chain. */
	assert(esp_axi_gdma_transfer(&sc, ch, &req) == 0);
	assert(esp_axi_gdma_halt_checked(&sc, ch) == 0 && !ch->broken);
	assert(!ch->desc_synced && post == pre);

	/* A peripheral trigger cannot be leased by two channels. */
	memset(&sc, 0, sizeof(sc));
	for (i = 0; i < GDMA_CHANNELS; i++) {
		sc.chan[i].sc = &sc;
		sc.chan[i].index = i;
	}
	assert(esp_axi_gdma_acquire(&sc, camera, 4, callback, NULL) != NULL);
	assert(esp_axi_gdma_acquire(&sc, camera, 4, callback, NULL) == NULL);
	assert(esp_axi_gdma_acquire(&sc, spi, 4, callback, NULL) != NULL);
	return 0;
}
