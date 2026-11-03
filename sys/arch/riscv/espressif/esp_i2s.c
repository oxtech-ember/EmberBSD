/* $NetBSD$ */
/* SPDX-FileCopyrightText: 2026 The EmberBSD Project */
/* SPDX-License-Identifier: BSD-2-Clause */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/audioio.h>

#include <machine/cpufunc.h>

#include <dev/audio/audio_dai.h>
#include <dev/audio/audio_if.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/syscon.h>

#include "esp_ahb_gdma_desc.h"
#include "esp_i2s_state.h"

#define I2S_BASE		0x20387000
#define I2S_SIZE		0x100
#define I2S_FIFO		0
#define I2S_INT_CLR		0x18
#define I2S_RX_CONF		0x20
#define I2S_TX_CONF		0x24
#define I2S_RX_CONF1		0x28
#define I2S_TX_CONF1		0x2c
#define I2S_RX_TDM_CTRL	0x50
#define I2S_TX_TDM_CTRL	0x54

#define I2S_CONF_RESET		__BIT(0)
#define I2S_CONF_FIFO_RESET	__BIT(1)
#define I2S_CONF_START		__BIT(2)
#define I2S_RX_SLAVE_MOD	__BIT(3)
#define I2S_CONF_MONO		__BIT(6)
#define I2S_RX_MONO_FST_VLD	__BIT(9)
#define I2S_CONF_MSB_SHIFT	__BIT(13)
#define I2S_CONF_TDM		__BIT(19)
#define I2S_CONF_WS_IDLE_POL	__BIT(17)
#define I2S_CONF_BCK_NO_DLY	__BIT(14)
#define I2S_CONF_SIG_LOOPBACK	__BIT(30)
#define I2S_CONF_TX_STOP_EN	__BIT(4)
#define I2S_CONF_BCK_DIV	__BITS(26, 21)
#define I2S_CONF1_WS_WIDTH	__BITS(8, 0)
#define I2S_CONF1_BITS_MOD	__BITS(18, 14)
#define I2S_CONF1_HALF_BITS	__BITS(26, 19)
#define I2S_CONF1_CHAN_BITS	__BITS(31, 27)
#define I2S_TDM_MASK		__BITS(15, 0)
#define I2S_TDM_CHANS		__BITS(19, 16)

#define I2S0_CTRL0		0xe4
#define I2S0_RX_CTRL0		0xe8
#define I2S0_RX_DIV_CTRL0	0xec
#define I2S0_TX_CTRL0		0xf0
#define I2S0_TX_DIV_CTRL0	0xf4
#define I2S_CTRL_APB_CLK	__BIT(0)
#define I2S_CTRL_APB_RESET	__BIT(1)
#define I2S_CTRL_FORCE_NORST	__BIT(2)
#define I2S_CLK_ENABLE		__BIT(0)
#define I2S_CLK_SRC		__BITS(2, 1)
#define I2S_CLK_DIV_N		__BITS(10, 3)
#define I2S_RX_MCLK_TX		__BIT(11)
#define I2S_DIV_X		__BITS(8, 0)
#define I2S_DIV_Y		__BITS(17, 9)
#define I2S_DIV_Z		__BITS(26, 18)
#define I2S_DIV_YN1		__BIT(27)
#define I2S0_MCLK_PAD		0x8
#define I2S0_MCLK_PAD_ENABLE	__BIT(31)

#define ESP_I2S_PERIODS_MAX	512
#define ESP_I2S_DESC_MAX	512
#define ESP_I2S_TRIGGER		1
#define ESP_I2S_MCLK		12288000
#define ESP_I2S_RATE		48000

struct esp_i2s_softc;
struct esp_i2s_buffer {
	LIST_ENTRY(esp_i2s_buffer) link;
	void *kva;
	size_t size;
	bus_dma_segment_t seg;
	int nsegs;
	bus_dmamap_t map;
	unsigned int active;
};

struct esp_i2s_stream {
	struct esp_i2s_softc *sc;
	struct fdtbus_dma *dma;
	struct fdtbus_dma_req req;
	struct esp_i2s_buffer *buffer;
	bus_dma_segment_t periods[ESP_I2S_PERIODS_MAX];
	size_t nperiods;
	void (*intr)(void *);
	void *intrarg;
	size_t period_size;
	size_t period_index;
	bool running, dma_error;
	bool broken;
	int mode;
};

struct esp_i2s_softc {
	device_t dev;
	int phandle;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	bus_dma_tag_t dmat;
	bus_addr_t base;
	struct syscon *syscon;
	struct syscon *alive_syscon;
	struct esp_i2s_stream play;
	struct esp_i2s_stream record;
	LIST_HEAD(, esp_i2s_buffer) buffers;
	kmutex_t lock;
	kmutex_t intr_lock;
	struct audio_format format;
	struct audio_dai_device dai;
	u_int mclk;
	u_int polarity;
	bool dai_format_set;
	bool configured;
};

#define I2S_READ(sc, reg) bus_space_read_4((sc)->bst, (sc)->bsh, (reg))
#define I2S_WRITE(sc, reg, val) bus_space_write_4((sc)->bst, (sc)->bsh, \
	(reg), (val))

static int esp_i2s_match(device_t, cfdata_t, void *);
static void esp_i2s_attach(device_t, device_t, void *);
static audio_dai_tag_t esp_i2s_get_tag(device_t, const void *, size_t);
static void esp_i2s_dma_callback(void *);
static int esp_i2s_trigger(struct esp_i2s_stream *, void *, void *, int,
	void (*)(void *), void *, const audio_params_t *);
static int esp_i2s_halt(struct esp_i2s_stream *);

static const struct device_compatible_entry esp_i2s_compat[] = {
	{ .compat = "espressif,esp32s31-i2s0" },
	DEVICE_COMPAT_EOL
};

CFATTACH_DECL_NEW(espi2s, sizeof(struct esp_i2s_softc), esp_i2s_match,
	esp_i2s_attach, NULL, NULL);

static int
esp_i2s_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, esp_i2s_compat);
}

static int
esp_i2s_params_supported(const audio_params_t *p)
{
	return p != NULL && p->sample_rate == ESP_I2S_RATE && p->channels == 1 &&
	    p->precision == 16 && p->validbits == 16 &&
	    p->encoding == AUDIO_ENCODING_SLINEAR_LE;
}

static int
esp_i2s_query_format(void *priv, audio_format_query_t *afp)
{
	struct esp_i2s_softc *sc = priv;

	return audio_query_format(&sc->format, 1, afp);
}

static int
esp_i2s_round_blocksize(void *priv, int bs, int mode,
	const audio_params_t *params)
{
	const size_t align = esp_i2s_dma_alignment(riscv_dcache_align);

	(void)priv;
	(void)mode;
	(void)params;
	if (!powerof2(align) || bs <= 0 || bs > INT_MAX - (int)(align - 1))
		return bs;
	return (bs + (int)align - 1) & ~((int)align - 1);
}

static size_t
esp_i2s_round_buffersize(void *priv, int dir, size_t size)
{
	(void)priv;
	(void)dir;
	return size;
}

static int
esp_i2s_set_format(void *priv, int mode, const audio_params_t *play,
	const audio_params_t *record, audio_filter_reg_t *pfil,
	audio_filter_reg_t *rfil)
{
	struct esp_i2s_softc *sc = priv;
	(void)pfil;
	(void)rfil;
	if (mode == 0 || (mode & ~(AUMODE_PLAY | AUMODE_RECORD)) != 0 ||
	    ((mode & AUMODE_PLAY) != 0 && !esp_i2s_params_supported(play)) ||
	    ((mode & AUMODE_RECORD) != 0 && !esp_i2s_params_supported(record)) ||
	    sc->polarity != AUDIO_DAI_POLARITY_NB_NF ||
	    !sc->dai_format_set || sc->mclk != ESP_I2S_MCLK)
		return EINVAL;

	/* audio(4) enters set_format with its thread lock held. */
	KASSERT(mutex_owned(&sc->lock));
	mutex_enter(&sc->intr_lock);
	if (sc->play.running || sc->record.running) {
		mutex_exit(&sc->intr_lock);
		return EBUSY;
	}
	/* Configure two 16-bit slots; expose only the mono left slot to audio(4). */
	I2S_WRITE(sc, I2S_RX_CONF, I2S_RX_SLAVE_MOD |
	    I2S_CONF_MONO | I2S_RX_MONO_FST_VLD |
	    I2S_CONF_MSB_SHIFT |
	    I2S_CONF_TDM | I2S_CONF_BCK_NO_DLY |
	    __SHIFTIN(7, I2S_CONF_BCK_DIV));
	I2S_WRITE(sc, I2S_TX_CONF, I2S_CONF_MONO | I2S_CONF_MSB_SHIFT |
	    I2S_CONF_TDM | I2S_CONF_BCK_NO_DLY | I2S_CONF_SIG_LOOPBACK |
	    I2S_CONF_TX_STOP_EN | __SHIFTIN(7, I2S_CONF_BCK_DIV));
	I2S_WRITE(sc, I2S_RX_CONF1, __SHIFTIN(15, I2S_CONF1_WS_WIDTH) |
	    __SHIFTIN(15, I2S_CONF1_BITS_MOD) |
	    __SHIFTIN(15, I2S_CONF1_HALF_BITS) |
	    __SHIFTIN(15, I2S_CONF1_CHAN_BITS));
	I2S_WRITE(sc, I2S_TX_CONF1, __SHIFTIN(15, I2S_CONF1_WS_WIDTH) |
	    __SHIFTIN(15, I2S_CONF1_BITS_MOD) |
	    __SHIFTIN(15, I2S_CONF1_HALF_BITS) |
	    __SHIFTIN(15, I2S_CONF1_CHAN_BITS));
	I2S_WRITE(sc, I2S_RX_TDM_CTRL, __SHIFTIN(1, I2S_TDM_CHANS) | 1);
	I2S_WRITE(sc, I2S_TX_TDM_CTRL, __SHIFTIN(1, I2S_TDM_CHANS) | 1);
	I2S_WRITE(sc, I2S_TX_CONF,
	    I2S_READ(sc, I2S_TX_CONF) | __BIT(5));
	/* TX generates the shared BCLK/WS; NB_NF is the supported bus format. */
	I2S_WRITE(sc, I2S_INT_CLR, UINT32_MAX);
	sc->configured = true;
	mutex_exit(&sc->intr_lock);
	return 0;
}

static int
esp_i2s_set_sysclk(audio_dai_tag_t dai, u_int rate, int dir)
{
	struct esp_i2s_softc *sc = audio_dai_private(dai);
	uint32_t v;

	if (dir != AUDIO_DAI_CLOCK_OUT || rate != ESP_I2S_MCLK)
		return EINVAL;

	/* 40 MHz XTAL / (3 + 49/192) = 12.288 MHz. */
	syscon_lock(sc->syscon);
	v = syscon_read_4(sc->syscon, I2S0_RX_CTRL0);
	syscon_write_4(sc->syscon, I2S0_RX_CTRL0,
	    (v & ~(I2S_CLK_SRC | I2S_CLK_DIV_N | I2S_RX_MCLK_TX)) |
	    I2S_RX_MCLK_TX);
	/* IDF workaround: set a small divider before programming the fraction. */
	v = syscon_read_4(sc->syscon, I2S0_TX_CTRL0);
	syscon_write_4(sc->syscon, I2S0_TX_CTRL0,
	    (v & ~(I2S_CLK_SRC | I2S_CLK_DIV_N)) | I2S_CLK_ENABLE |
	    __SHIFTIN(2, I2S_CLK_DIV_N));
	syscon_write_4(sc->syscon, I2S0_TX_DIV_CTRL0,
	    __SHIFTIN(1, I2S_DIV_Y));
	syscon_write_4(sc->syscon, I2S0_TX_DIV_CTRL0,
		__SHIFTIN(2, I2S_DIV_X) | __SHIFTIN(45, I2S_DIV_Y) |
	    __SHIFTIN(49, I2S_DIV_Z));
	v = syscon_read_4(sc->syscon, I2S0_TX_CTRL0);
	syscon_write_4(sc->syscon, I2S0_TX_CTRL0,
	    (v & ~I2S_CLK_DIV_N) | __SHIFTIN(3, I2S_CLK_DIV_N));
	/* RX derives MCLK from the TX source on S31; keep both module clocks live. */
	v = syscon_read_4(sc->syscon, I2S0_RX_CTRL0);
	syscon_write_4(sc->syscon, I2S0_RX_CTRL0, v | I2S_CLK_ENABLE);
	syscon_unlock(sc->syscon);

	syscon_lock(sc->alive_syscon);
	v = syscon_read_4(sc->alive_syscon, I2S0_MCLK_PAD);
	syscon_write_4(sc->alive_syscon, I2S0_MCLK_PAD,
	    v | I2S0_MCLK_PAD_ENABLE);
	syscon_unlock(sc->alive_syscon);
	sc->mclk = rate;
	return 0;
}

static int
esp_i2s_set_dai_format(audio_dai_tag_t dai, u_int format)
{
	struct esp_i2s_softc *sc = audio_dai_private(dai);
	u_int fmt = __SHIFTOUT(format, AUDIO_DAI_FORMAT_MASK);
	u_int clk = __SHIFTOUT(format, AUDIO_DAI_CLOCK_MASK);
	u_int pol = __SHIFTOUT(format, AUDIO_DAI_POLARITY_MASK);

	if (fmt != AUDIO_DAI_FORMAT_I2S || clk != AUDIO_DAI_CLOCK_CBS_CFS ||
	    pol != AUDIO_DAI_POLARITY_NB_NF)
		return EINVAL;
	sc->polarity = pol;
	sc->dai_format_set = true;
	return 0;
}

static int
esp_i2s_open(void *priv, int flags)
{
	struct esp_i2s_softc *sc = priv;

	(void)flags;
	return sc->configured ? 0 : EIO;
}

static int
esp_i2s_find_buffer(struct esp_i2s_softc *sc, void *start, size_t size,
	struct esp_i2s_buffer **result)
{
	struct esp_i2s_buffer *buf;

	LIST_FOREACH(buf, &sc->buffers, link) {
		if (buf->kva == start && size <= buf->size) {
			*result = buf;
			return 0;
		}
	}
	return EINVAL;
}

static void *
esp_i2s_allocm(void *priv, int dir, size_t size)
{
	struct esp_i2s_softc *sc = priv;
	struct esp_i2s_buffer *buf;
	int error;

	(void)dir;
	if (size == 0)
		return NULL;
	buf = kmem_zalloc(sizeof(*buf), KM_SLEEP);
	buf->size = size;
	error = bus_dmamem_alloc(sc->dmat, size,
	    esp_i2s_dma_alignment(riscv_dcache_align), 0, &buf->seg, 1,
	    &buf->nsegs, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail;
	if (buf->nsegs != 1) {
		bus_dmamem_free(sc->dmat, &buf->seg, buf->nsegs);
		buf->nsegs = 0;
		goto fail;
	}
	/* Use normal cacheable mappings and the standard per-period DMA syncs. */
	error = bus_dmamem_map(sc->dmat, &buf->seg, buf->nsegs, size,
	    &buf->kva, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail_mem;
	error = bus_dmamap_create(sc->dmat, size, 1, size, 0, BUS_DMA_WAITOK,
	    &buf->map);
	if (error != 0)
		goto fail_unmap;
	error = bus_dmamap_load(sc->dmat, buf->map, buf->kva, size, NULL,
	    BUS_DMA_WAITOK);
	if (error != 0)
		goto fail_map;
	if (buf->map->dm_nsegs != 1 ||
	    buf->map->dm_segs[0].ds_addr < AHB_GDMA_ADDR_MIN ||
	    buf->map->dm_segs[0].ds_addr > AHB_GDMA_ADDR_MAX ||
	    size - 1 > AHB_GDMA_ADDR_MAX - buf->map->dm_segs[0].ds_addr ||
	    (buf->map->dm_segs[0].ds_addr & 3) != 0) {
		error = EFBIG;
		goto fail_loaded;
	}
	mutex_enter(&sc->lock);
	LIST_INSERT_HEAD(&sc->buffers, buf, link);
	mutex_exit(&sc->lock);
	return buf->kva;

fail_loaded:
	bus_dmamap_unload(sc->dmat, buf->map);
fail_map:
	bus_dmamap_destroy(sc->dmat, buf->map);
fail_unmap:
	bus_dmamem_unmap(sc->dmat, buf->kva, size);
fail_mem:
	if (buf->nsegs != 0)
		bus_dmamem_free(sc->dmat, &buf->seg, buf->nsegs);
fail:
	kmem_free(buf, sizeof(*buf));
	return NULL;
}

static void
esp_i2s_freem(void *priv, void *addr, size_t size)
{
	struct esp_i2s_softc *sc = priv;
	struct esp_i2s_buffer *buf;

	(void)size;
	mutex_enter(&sc->lock);
	LIST_FOREACH(buf, &sc->buffers, link) {
		if (buf->kva != addr)
			continue;
		if (buf->active) {
			device_printf(sc->dev,
			    "retaining active DMA buffer after stop failure\n");
			mutex_exit(&sc->lock);
			return;
		}
		LIST_REMOVE(buf, link);
		mutex_exit(&sc->lock);
		bus_dmamap_unload(sc->dmat, buf->map);
		bus_dmamap_destroy(sc->dmat, buf->map);
		bus_dmamem_unmap(sc->dmat, buf->kva, buf->size);
		bus_dmamem_free(sc->dmat, &buf->seg, buf->nsegs);
		kmem_free(buf, sizeof(*buf));
		return;
	}
	mutex_exit(&sc->lock);
}

static int
esp_i2s_get_props(void *priv)
{
	struct esp_i2s_softc *sc = priv;
	return sc->play.dma != NULL && sc->record.dma != NULL ?
	    AUDIO_PROP_PLAYBACK | AUDIO_PROP_CAPTURE | AUDIO_PROP_FULLDUPLEX :
	    sc->play.dma != NULL ? AUDIO_PROP_PLAYBACK :
	    sc->record.dma != NULL ? AUDIO_PROP_CAPTURE : 0;
}

static int
esp_i2s_trigger(struct esp_i2s_stream *stream, void *start, void *end,
	int blksize, void (*intr)(void *), void *intrarg,
	const audio_params_t *params)
{
	struct esp_i2s_softc *sc = stream->sc;
	struct esp_i2s_buffer *buf;
	bus_addr_t paddr;
	size_t length, nperiods, i, align;
	int error;

	if (stream->dma == NULL || start == NULL || end == NULL ||
	    (uintptr_t)end <= (uintptr_t)start || blksize <= 0 ||
	    !esp_i2s_params_supported(params))
		return EINVAL;
	KASSERT(mutex_owned(&sc->lock));
	KASSERT(mutex_owned(&sc->intr_lock));
	length = (uintptr_t)end - (uintptr_t)start;
	align = esp_i2s_dma_alignment(riscv_dcache_align);
	if (!powerof2(align) || (uintptr_t)start % align != 0 ||
	    length % (size_t)blksize != 0 ||
	    (size_t)blksize % align != 0)
		return EINVAL;
	nperiods = length / (size_t)blksize;
	if (nperiods == 0 || nperiods > ESP_I2S_PERIODS_MAX)
		return EFBIG;
	/* audio(4) calls trigger with the supplied thread/intr locks held. */
	error = esp_i2s_find_buffer(sc, start, length, &buf);
	if (error != 0)
		return error;
	if (buf->active)
		return EBUSY;
	if (stream->broken || stream->dma_error)
		return EIO;
	paddr = buf->map->dm_segs[0].ds_addr;
	if (length > buf->map->dm_segs[0].ds_len ||
	    paddr > AHB_GDMA_ADDR_MAX || length - 1 > AHB_GDMA_ADDR_MAX - paddr) {
		return EFBIG;
	}
	if (stream->running || !sc->configured) {
		return EBUSY;
	}
	for (i = 0; i < nperiods; i++) {
		stream->periods[i].ds_addr = paddr + i * (size_t)blksize;
		stream->periods[i].ds_len = (size_t)blksize;
	}
	stream->req.dreq_segs = stream->periods;
	stream->req.dreq_nsegs = (int)nperiods;
	bus_dmamap_sync(sc->dmat, buf->map, 0, length,
	    stream->mode == AUMODE_PLAY ? BUS_DMASYNC_PREWRITE :
	    BUS_DMASYNC_PREREAD);
	if (stream->mode == AUMODE_PLAY) {
		I2S_WRITE(sc, I2S_TX_CONF,
		    I2S_READ(sc, I2S_TX_CONF) | I2S_CONF_FIFO_RESET);
		I2S_WRITE(sc, I2S_TX_CONF,
		    I2S_READ(sc, I2S_TX_CONF) & ~I2S_CONF_FIFO_RESET);
	} else {
		I2S_WRITE(sc, I2S_RX_CONF,
		    I2S_READ(sc, I2S_RX_CONF) | I2S_CONF_FIFO_RESET);
		I2S_WRITE(sc, I2S_RX_CONF,
		    I2S_READ(sc, I2S_RX_CONF) & ~I2S_CONF_FIFO_RESET);
	}
	stream->intr = intr;
	stream->intrarg = intrarg;
	stream->nperiods = nperiods;
	stream->period_size = (size_t)blksize;
	stream->period_index = 0;
	stream->running = true;
	buf->active |= (unsigned int)stream->mode;
	stream->buffer = buf;
	/* Start DMA before enabling I2S requests so the first sample is retained. */
	error = fdtbus_dma_transfer(stream->dma, &stream->req);
	if (error == 0) {
		if (stream->mode == AUMODE_PLAY) {
			/* Keep clocks continuous if capture is sharing the TX clock. */
			if (sc->record.running)
				I2S_WRITE(sc, I2S_TX_CONF,
				    I2S_READ(sc, I2S_TX_CONF) &
				    ~I2S_CONF_TX_STOP_EN);
			I2S_WRITE(sc, I2S_TX_CONF,
			    I2S_READ(sc, I2S_TX_CONF) | I2S_CONF_START);
		} else {
			/* TX is the master clock source even for record-only streams. */
			I2S_WRITE(sc, I2S_TX_CONF,
			    (I2S_READ(sc, I2S_TX_CONF) & ~I2S_CONF_TX_STOP_EN) |
			    I2S_CONF_START);
			I2S_WRITE(sc, I2S_RX_CONF,
			    I2S_READ(sc, I2S_RX_CONF) | I2S_CONF_START);
		}
	} else {
		stream->running = false;
		stream->intr = NULL;
		stream->intrarg = NULL;
		buf->active &= ~(unsigned int)stream->mode;
		stream->buffer = NULL;
		bus_dmamap_sync(sc->dmat, buf->map, 0, length,
		    stream->mode == AUMODE_PLAY ? BUS_DMASYNC_POSTWRITE :
		    BUS_DMASYNC_POSTREAD);
	}
	return error;
}

static int
esp_i2s_trigger_output(void *priv, void *start, void *end, int blksize,
	void (*intr)(void *), void *intrarg, const audio_params_t *params)
{
	return esp_i2s_trigger(&((struct esp_i2s_softc *)priv)->play, start,
	    end, blksize, intr, intrarg, params);
}

static int
esp_i2s_trigger_input(void *priv, void *start, void *end, int blksize,
	void (*intr)(void *), void *intrarg, const audio_params_t *params)
{
	return esp_i2s_trigger(&((struct esp_i2s_softc *)priv)->record, start,
	    end, blksize, intr, intrarg, params);
}

static int
esp_i2s_halt(struct esp_i2s_stream *stream)
{
	struct esp_i2s_softc *sc = stream->sc;
	struct esp_i2s_buffer *buf;
	int error;

	if (stream->dma == NULL)
		return EIO;
	/* audio(4) calls halt with both supplied locks held. */
	KASSERT(mutex_owned(&sc->lock));
	KASSERT(mutex_owned(&sc->intr_lock));
	if (!esp_i2s_halt_required(stream->running, stream->buffer != NULL,
	    stream->broken)) {
		return 0;
	}
	stream->running = false;
	stream->intr = NULL;
	stream->intrarg = NULL;
	if (stream->mode == AUMODE_PLAY) {
		if (esp_i2s_stop_tx_clock(sc->play.running, sc->record.running))
			I2S_WRITE(sc, I2S_TX_CONF,
			    (I2S_READ(sc, I2S_TX_CONF) &
			    ~I2S_CONF_START) | I2S_CONF_TX_STOP_EN);
	} else {
		I2S_WRITE(sc, I2S_RX_CONF,
		    I2S_READ(sc, I2S_RX_CONF) & ~I2S_CONF_START);
		if (esp_i2s_stop_tx_clock(sc->play.running, sc->record.running))
			I2S_WRITE(sc, I2S_TX_CONF,
			    (I2S_READ(sc, I2S_TX_CONF) &
			    ~I2S_CONF_START) | I2S_CONF_TX_STOP_EN);
	}
	/*
	 * DMA period callbacks also take the audio interrupt lock. Drop only this
	 * lock while the provider drains callbacks; audio's thread lock remains held,
	 * and the cleared callback makes any late period a no-op.
	 */
	mutex_exit(&sc->intr_lock);
	error = fdtbus_dma_halt_checked(stream->dma);
	mutex_enter(&sc->intr_lock);
	if (error != 0) {
		stream->broken = true;
		return error;
	}
	buf = stream->buffer;
	if (buf != NULL) {
		bus_dmamap_sync(sc->dmat, buf->map, 0, buf->size,
		    stream->mode == AUMODE_PLAY ? BUS_DMASYNC_POSTWRITE :
		    BUS_DMASYNC_POSTREAD);
		buf->active &= ~(unsigned int)stream->mode;
		stream->buffer = NULL;
	}
	stream->broken = false;
	stream->dma_error = false;
	return 0;
}

static int
esp_i2s_halt_output(void *priv)
{
	return esp_i2s_halt(&((struct esp_i2s_softc *)priv)->play);
}

static int
esp_i2s_halt_input(void *priv)
{
	return esp_i2s_halt(&((struct esp_i2s_softc *)priv)->record);
}

static void
esp_i2s_get_locks(void *priv, kmutex_t **intr, kmutex_t **thread)
{
	struct esp_i2s_softc *sc = priv;
	*intr = &sc->intr_lock;
	*thread = &sc->lock;
}

static void
esp_i2s_dma_callback(void *arg)
{
	struct esp_i2s_stream *stream = arg;
	struct esp_i2s_softc *sc = stream->sc;
	struct esp_i2s_buffer *buf;
	struct fdtbus_dma_result result;
	void (*intr)(void *);
	void *intrarg;
	size_t offset;
	int error;

	/* Cyclic success is EAGAIN; a terminal provider error is cached. */
	error = fdtbus_dma_result(stream->dma, &result);
	if (error == 0 && result.dres_error != 0) {
		mutex_enter(&sc->intr_lock);
		if (stream->running) {
			stream->dma_error = true;
			if (stream->mode == AUMODE_PLAY) {
				if (!sc->record.running)
					I2S_WRITE(sc, I2S_TX_CONF,
					    (I2S_READ(sc, I2S_TX_CONF) &
					    ~I2S_CONF_START) | I2S_CONF_TX_STOP_EN);
			} else {
				I2S_WRITE(sc, I2S_RX_CONF,
				    I2S_READ(sc, I2S_RX_CONF) & ~I2S_CONF_START);
				if (!sc->play.running)
					I2S_WRITE(sc, I2S_TX_CONF,
					    (I2S_READ(sc, I2S_TX_CONF) &
					    ~I2S_CONF_START) | I2S_CONF_TX_STOP_EN);
			}
		}
		mutex_exit(&sc->intr_lock);
		device_printf(sc->dev, "%s DMA failed: %d\n",
		    stream->mode == AUMODE_PLAY ? "playback" : "capture",
		    result.dres_error);
		return;
	}

	mutex_enter(&sc->intr_lock);
	intr = stream->running ? stream->intr : NULL;
	intrarg = stream->intrarg;
	buf = stream->buffer;
	if (intr != NULL && buf != NULL) {
		offset = stream->period_index * stream->period_size;
		KASSERT(offset <= buf->size &&
		    stream->period_size <= buf->size - offset);
		if (stream->mode == AUMODE_RECORD)
			bus_dmamap_sync(sc->dmat, buf->map, offset,
			    stream->period_size, BUS_DMASYNC_POSTREAD);
		intr(intrarg);
		/*
		 * Capture sync follows the completed period. audio(4) may update a
		 * different playback slot while handling this callback; publish the
		 * small playback ring rather than depend on mixer ring internals.
		 */
		if (stream->running) {
			if (stream->mode == AUMODE_PLAY) {
				bus_dmamap_sync(sc->dmat, buf->map, 0, buf->size,
				    BUS_DMASYNC_PREWRITE);
			} else {
				bus_dmamap_sync(sc->dmat, buf->map, offset,
				    stream->period_size, BUS_DMASYNC_PREREAD);
			}
			stream->period_index++;
			if (stream->period_index == stream->nperiods)
				stream->period_index = 0;
		}
	}
	mutex_exit(&sc->intr_lock);
}

static const struct audio_hw_if esp_i2s_hw_if = {
	.open = esp_i2s_open,
	.query_format = esp_i2s_query_format,
	.set_format = esp_i2s_set_format,
	.round_blocksize = esp_i2s_round_blocksize,
	.allocm = esp_i2s_allocm,
	.freem = esp_i2s_freem,
	.round_buffersize = esp_i2s_round_buffersize,
	.get_props = esp_i2s_get_props,
	.trigger_output = esp_i2s_trigger_output,
	.trigger_input = esp_i2s_trigger_input,
	.halt_output = esp_i2s_halt_output,
	.halt_input = esp_i2s_halt_input,
	.get_locks = esp_i2s_get_locks,
};

static int
esp_i2s_chan_init(struct esp_i2s_softc *sc, struct esp_i2s_stream *stream,
	int mode, const char *name)
{
	stream->sc = sc;
	stream->mode = mode;
	stream->dma = fdtbus_dma_get(sc->phandle, name,
	    esp_i2s_dma_callback, stream);
	if (stream->dma == NULL)
		return ENXIO;
	stream->req.dreq_sel = ESP_I2S_TRIGGER;
	stream->req.dreq_dir = mode == AUMODE_PLAY ? FDT_DMA_WRITE : FDT_DMA_READ;
	stream->req.dreq_dev_phys = sc->base;
	stream->req.dreq_block_irq = 1;
	stream->req.dreq_block_multi = 1;
	stream->req.dreq_mem_opt.opt_bus_width = 32;
	stream->req.dreq_dev_opt.opt_bus_width = 32;
	return 0;
}

static audio_dai_tag_t
esp_i2s_get_tag(device_t dev, const void *data, size_t len)
{
	struct esp_i2s_softc *sc = device_private(dev);

	(void)data;
	return len == sizeof(uint32_t) ? &sc->dai : NULL;
}

static const struct fdtbus_dai_controller_func esp_i2s_dai_funcs = {
	.get_tag = esp_i2s_get_tag,
};

static void
esp_i2s_attach(device_t parent, device_t self, void *aux)
{
	struct esp_i2s_softc *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	static const struct audio_format format = {
		.mode = AUMODE_PLAY | AUMODE_RECORD,
		.encoding = AUDIO_ENCODING_SLINEAR_LE,
		.validbits = 16,
		.precision = 16,
		.channels = 1,
		.channel_mask = AUFMT_MONAURAL,
		.frequency_type = 1,
		.frequency = { ESP_I2S_RATE },
	};
	bus_addr_t addr;
	bus_size_t size;
	uint32_t v;

	sc->dev = self;
	sc->phandle = faa->faa_phandle;
	sc->bst = faa->faa_bst;
	sc->dmat = faa->faa_dmat;
	sc->format = format;
	LIST_INIT(&sc->buffers);
	if (fdtbus_get_reg(sc->phandle, 0, &addr, &size) != 0 ||
	    size < I2S_SIZE || bus_space_map(sc->bst, addr, size, 0,
	    &sc->bsh) != 0) {
		aprint_error(": missing I2S registers\n");
		return;
	}
	if (fdtbus_pinctrl_set_config_index(sc->phandle, 0) != 0) {
		aprint_error(": unable to configure I2S pinmux\n");
		bus_space_unmap(sc->bst, sc->bsh, size);
		return;
	}
	sc->base = addr;
	sc->syscon = fdtbus_syscon_acquire(sc->phandle, "espressif,syscon");
	sc->alive_syscon = fdtbus_syscon_acquire(sc->phandle,
	    "espressif,alive-syscon");
	if (sc->syscon == NULL || sc->alive_syscon == NULL) {
		aprint_error(": missing clock syscon\n");
		goto fail_map;
	}
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->intr_lock, MUTEX_DEFAULT, IPL_VM);
	syscon_lock(sc->syscon);
	v = syscon_read_4(sc->syscon, I2S0_CTRL0);
	syscon_write_4(sc->syscon, I2S0_CTRL0,
	    v | I2S_CTRL_APB_CLK | I2S_CTRL_APB_RESET | I2S_CTRL_FORCE_NORST);
	syscon_write_4(sc->syscon, I2S0_CTRL0,
	    (v | I2S_CTRL_APB_CLK | I2S_CTRL_FORCE_NORST) &
	    ~I2S_CTRL_APB_RESET);
	syscon_unlock(sc->syscon);
	if (esp_i2s_chan_init(sc, &sc->play, AUMODE_PLAY, "tx") != 0 ||
	    esp_i2s_chan_init(sc, &sc->record, AUMODE_RECORD, "rx") != 0) {
		aprint_error(": unable to acquire I2S DMA channels\n");
		goto fail_locks;
	}
	sc->dai.dai_hw_if = &esp_i2s_hw_if;
	sc->dai.dai_set_sysclk = esp_i2s_set_sysclk;
	sc->dai.dai_set_format = esp_i2s_set_dai_format;
	sc->dai.dai_dev = self;
	sc->dai.dai_priv = sc;
	fdtbus_register_dai_controller(self, sc->phandle, &esp_i2s_dai_funcs);
	aprint_naive("\n");
	aprint_normal(": ESP32-S31 I2S0, 16-bit mono playback/capture\n");
	return;

fail_locks:
	if (sc->play.dma != NULL)
		fdtbus_dma_put(sc->play.dma);
	if (sc->record.dma != NULL)
		fdtbus_dma_put(sc->record.dma);
	mutex_destroy(&sc->intr_lock);
	mutex_destroy(&sc->lock);
fail_map:
	bus_space_unmap(sc->bst, sc->bsh, size);
}
