/* Origin: EmberBSD - guarded audio(4) interface for the verified YS-M33 codec. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/evcnt.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/audioio.h>
#include <dev/audio/audio_if.h>
#include <dev/fdt/fdtvar.h>
#include <arm/sunxi/sun50i_a133_codec_io.h>

struct a133_memory {
	LIST_ENTRY(a133_memory) link;
	void *addr;
	size_t size;
	unsigned mode;
};
struct a133_channel {
	uint8_t *buffer;
	struct a133_pcm_ring ring;
	void (*callback)(void *);
	void *arg;
	bool running;
};
struct a133_softc {
	device_t dev;
	bus_space_tag_t bst;
	bus_space_handle_t bsh[3];
	unsigned mapped;
	int phandle;
	void *ih;
	bool initialized;
	kmutex_t lock, intr_lock;
	struct a133_codec_io io;
	struct a133_codec_lease lease;
	struct a133_channel play, record;
	struct a133_pcm_format format[2];
	LIST_HEAD(, a133_memory) memory;
	struct evcnt underrun, overrun, fault, interrupts;
};

static const bus_addr_t base[] = { 0x05096000, 0x03001000, 0x0300b000 };
static const bus_size_t mapsize[] = { 0x400, 0x1000, 0x400 };
static const struct audio_format formats[] = {
	{ .mode = AUMODE_PLAY | AUMODE_RECORD,
	  .encoding = AUDIO_ENCODING_SLINEAR_LE, .validbits = 16, .precision = 16,
	  .channels = 1, .channel_mask = AUFMT_MONAURAL,
	  .frequency_type = 2, .frequency = { 48000, 16000 } },
	{ .mode = AUMODE_PLAY,
	  .encoding = AUDIO_ENCODING_SLINEAR_LE, .validbits = 16, .precision = 16,
	  .channels = 2, .channel_mask = AUFMT_STEREO,
	  .frequency_type = 2, .frequency = { 48000, 16000 } }
};
static const struct device_compatible_entry root_compat[] = {
	{ .compat = "allwinner,a133" }, DEVICE_COMPAT_EOL
};
static int a133_halt(struct a133_softc *, unsigned);
static int a133_interrupt(void *);
static int a133_detach(device_t, int);

static uint32_t
a133_read(void *cookie, unsigned space, unsigned reg)
{
	struct a133_softc *sc = cookie;

	KASSERT(space < 3 && reg % 4 == 0 && reg + 4 <= mapsize[space]);
	return bus_space_read_4(sc->bst, sc->bsh[space], reg);
}

static void
a133_write(void *cookie, unsigned space, unsigned reg, uint32_t value)
{
	struct a133_softc *sc = cookie;

	KASSERT(space < 3 && reg % 4 == 0 && reg + 4 <= mapsize[space]);
	bus_space_write_4(sc->bst, sc->bsh[space], reg, value);
	bus_space_barrier(sc->bst, sc->bsh[space], reg, 4, BUS_SPACE_BARRIER_WRITE);
}

static void
a133_delay(void *cookie, unsigned us)
{
	struct a133_softc *sc = cookie;

	if (us >= 10000) {
		KASSERT(!mutex_owned(&sc->intr_lock));
		kpause("a133settle", false, MAX(1, mstohz(us / 1000)), NULL);
	} else {
		delay(us);
	}
}

static int
a133_params(unsigned mode, const audio_params_t *p, struct a133_pcm_format *f)
{

	if (p == NULL || p->encoding != AUDIO_ENCODING_SLINEAR_LE ||
	    p->precision != 16 || p->validbits != 16)
		return EINVAL;
	return a133_pcm_format(mode, p->sample_rate, p->channels, f);
}

static int
a133_query_format(void *priv, audio_format_query_t *q)
{

	(void)priv;
	return audio_query_format(formats, __arraycount(formats), q);
}

static int
a133_set_format(void *priv, int mode, const audio_params_t *play,
    const audio_params_t *record, audio_filter_reg_t *pf, audio_filter_reg_t *rf)
{
	struct a133_softc *sc = priv;
	struct a133_pcm_format next[2];
	int error;

	(void)pf;
	(void)rf;
	if (mode == 0 || (mode & ~(AUMODE_PLAY | AUMODE_RECORD)) != 0)
		return EINVAL;
	memcpy(next, sc->format, sizeof(next));
	if ((mode & AUMODE_PLAY) && (error = a133_params(A133_PCM_PLAY, play, &next[0])) != 0)
		return error;
	if ((mode & AUMODE_RECORD) && (error = a133_params(A133_PCM_RECORD, record, &next[1])) != 0)
		return error;
	mutex_enter(&sc->intr_lock);
	if (sc->play.running || sc->record.running) {
		mutex_exit(&sc->intr_lock);
		return EBUSY;
	}
	memcpy(sc->format, next, sizeof(next));
	mutex_exit(&sc->intr_lock);
	return 0;
}

static int
a133_round_block(void *priv, int block, int mode, const audio_params_t *p)
{

	(void)priv;
	(void)mode;
	(void)p;
	if (block < 1024)
		return 1024;
	if (block > 16384)
		return 16384;
	return (block + 1023) & ~1023;
}

static void *
a133_alloc(void *priv, int mode, size_t size)
{
	struct a133_softc *sc = priv;
	struct a133_memory *m;

	if (size == 0 || size > 1024 * 1024 ||
	    (mode != AUMODE_PLAY && mode != AUMODE_RECORD))
		return NULL;
	m = kmem_alloc(sizeof(*m), KM_SLEEP);
	m->addr = kmem_alloc(size, KM_SLEEP);
	m->size = size;
	m->mode = mode;
	mutex_enter(&sc->intr_lock);
	LIST_INSERT_HEAD(&sc->memory, m, link);
	mutex_exit(&sc->intr_lock);
	return m->addr;
}

static void
a133_free(void *priv, void *addr, size_t size)
{
	struct a133_softc *sc = priv;
	struct a133_memory *m;

	(void)size;
	mutex_enter(&sc->intr_lock);
	LIST_FOREACH(m, &sc->memory, link)
		if (m->addr == addr)
			break;
	if (m == NULL || (sc->play.running && sc->play.buffer == addr) ||
	    (sc->record.running && sc->record.buffer == addr)) {
		mutex_exit(&sc->intr_lock);
		return;
	}
	LIST_REMOVE(m, link);
	mutex_exit(&sc->intr_lock);
	kmem_free(m->addr, m->size);
	kmem_free(m, sizeof(*m));
}

static int
a133_halt(struct a133_softc *sc, unsigned mode)
{
	struct a133_channel *ch;
	uint32_t value;
	unsigned reg;

	KASSERT(mutex_owned(&sc->intr_lock));
	if (mode != AUMODE_PLAY && mode != AUMODE_RECORD)
		return EINVAL;
	ch = mode == AUMODE_PLAY ? &sc->play : &sc->record;
	if (!ch->running)
		return 0;
	reg = mode == AUMODE_PLAY ? 0x10 : 0x30;
	value = sc->io.read(sc->io.cookie, A133_CODEC_REG, reg);
	value &= mode == AUMODE_PLAY ? ~0x1eU : ~0x1000000eU;
	sc->io.write(sc->io.cookie, A133_CODEC_REG, reg, value);
	if (mode == AUMODE_PLAY) {
		value = sc->io.read(sc->io.cookie, A133_CODEC_REG, 0) & ~0x80000000U;
		sc->io.write(sc->io.cookie, A133_CODEC_REG, 0, value);
	}
	ch->running = false;
	ch->callback = NULL;
	ch->arg = NULL;
	ch->buffer = NULL;
	return 0;
}

static int
a133_trigger(struct a133_softc *sc, unsigned mode, void *start, void *end,
    int block, void (*callback)(void *), void *arg, const audio_params_t *p)
{
	struct a133_memory *m;
	struct a133_channel *ch;
	struct a133_pcm_format f;
	struct a133_pcm_ring ring;
	size_t size, bytes;
	unsigned reg, i;
	uint32_t word;
	bool done;
	int error;

	KASSERT(mutex_owned(&sc->intr_lock));
	if (mode != AUMODE_PLAY && mode != AUMODE_RECORD)
		return EINVAL;
	/* Disabled routes reject all stream attempts without touching registers. */
	if (!sc->lease.prepared || (sc->lease.routes & mode) == 0)
		return EACCES;
	ch = mode == AUMODE_PLAY ? &sc->play : &sc->record;
	if (ch->running)
		return EBUSY;
	if (start == NULL || callback == NULL || (uintptr_t)end <= (uintptr_t)start ||
	    block < 1024 || block > 16384)
		return EINVAL;
	error = a133_params(mode, p, &f);
	if (error != 0)
		return error;
	size = (uintptr_t)end - (uintptr_t)start;
	LIST_FOREACH(m, &sc->memory, link)
		if (m->addr == start && m->mode == mode)
			break;
	if (m == NULL || size > m->size || (uintptr_t)start % f.frame_bytes != 0 ||
	    a133_pcm_ring_init(&ring, size, block, f.frame_bytes) != 0)
		return EINVAL;
	ch->buffer = start;
	ch->ring = ring;
	ch->callback = callback;
	ch->arg = arg;
	ch->running = true;
	reg = mode == AUMODE_PLAY ? 0x10 : 0x30;
	word = mode == AUMODE_PLAY ? f.dac_fifoc : f.adc_fifoc;
	sc->io.write(sc->io.cookie, A133_CODEC_REG, reg, word | 1);
	for (i = 0; i < 100; i++) {
		if ((sc->io.read(sc->io.cookie, A133_CODEC_REG, reg) & 1) == 0)
			break;
		sc->io.delay_us(sc->io.cookie, 1);
	}
	if (i == 100) {
		a133_halt(sc, mode);
		return ETIMEDOUT;
	}
	sc->io.write(sc->io.cookie, A133_CODEC_REG,
	    mode == AUMODE_PLAY ? 0x14 : 0x38, mode == AUMODE_PLAY ? 0x0e : 0x0a);
	if (mode == AUMODE_PLAY) {
		error = a133_codec_transfer(&sc->io, &sc->lease, mode,
		    ch->buffer, &ch->ring, 128, &bytes, &done);
		if (error != 0 || bytes == 0) {
			a133_halt(sc, mode);
			return error != 0 ? error : EIO;
		}
		KASSERT(!done);
		word = sc->io.read(sc->io.cookie, A133_CODEC_REG, 0);
		sc->io.write(sc->io.cookie, A133_CODEC_REG, 0,
		    (word & ~0x8003f000U) | 0x80002000U);
		sc->io.write(sc->io.cookie, A133_CODEC_REG, reg, f.dac_fifoc | 0x0c);
	} else {
		sc->io.write(sc->io.cookie, A133_CODEC_REG, reg, f.adc_fifoc | 0x10000006);
	}
	return 0;
}

static int
a133_trigger_output(void *priv, void *start, void *end, int block,
    void (*callback)(void *), void *arg, const audio_params_t *p)
{

	return a133_trigger(priv, AUMODE_PLAY, start, end, block, callback, arg, p);
}

static int
a133_trigger_input(void *priv, void *start, void *end, int block,
    void (*callback)(void *), void *arg, const audio_params_t *p)
{

	return a133_trigger(priv, AUMODE_RECORD, start, end, block, callback, arg, p);
}

static int
a133_halt_output(void *priv)
{

	return a133_halt(priv, AUMODE_PLAY);
}

static int
a133_halt_input(void *priv)
{

	return a133_halt(priv, AUMODE_RECORD);
}

static void
a133_service(struct a133_softc *sc, unsigned mode)
{
	struct a133_channel *ch = mode == AUMODE_PLAY ? &sc->play : &sc->record;
	unsigned budget = 128;
	size_t bytes;
	bool done;
	int error;

	while (budget != 0 && ch->running) {
		error = a133_codec_transfer(&sc->io, &sc->lease, mode,
		    ch->buffer, &ch->ring, budget, &bytes, &done);
		if (error != 0) {
			sc->fault.ev_count++;
			a133_halt(sc, mode);
			break;
		}
		if (bytes == 0)
			break;
		budget -= bytes / 2;
		if (done && ch->callback != NULL)
			ch->callback(ch->arg);
	}
}

static int
a133_interrupt(void *arg)
{
	struct a133_softc *sc = arg;
	uint32_t status;
	int handled = 0;

	mutex_enter(&sc->intr_lock);
	if (sc->play.running) {
		status = a133_read(sc, A133_CODEC_REG, 0x14) & 0x0e;
		if (status != 0) {
			handled = 1;
			if (status & 4)
				sc->underrun.ev_count++;
			if (status & 2)
				sc->fault.ev_count++;
			a133_write(sc, A133_CODEC_REG, 0x14, status);
			a133_service(sc, AUMODE_PLAY);
		}
	}
	if (sc->record.running) {
		status = a133_read(sc, A133_CODEC_REG, 0x38) & 0x0a;
		if (status != 0) {
			handled = 1;
			if (status & 2)
				sc->overrun.ev_count++;
			a133_write(sc, A133_CODEC_REG, 0x38, status);
			a133_service(sc, AUMODE_RECORD);
		}
	}
	if (handled)
		sc->interrupts.ev_count++;
	mutex_exit(&sc->intr_lock);
	return handled;
}

static int
a133_set_port(void *priv, mixer_ctrl_t *mc)
{
	struct a133_softc *sc = priv;
	unsigned mode;
	bool enable;
	int error;

	KASSERT(mutex_owned(&sc->lock));
	if ((mc->dev != 2 && mc->dev != 3) || mc->type != AUDIO_MIXER_ENUM ||
	    (mc->un.ord != 0 && mc->un.ord != 1))
		return EINVAL;
	mode = mc->dev == 2 ? AUMODE_PLAY : AUMODE_RECORD;
	enable = mc->un.ord == 1;
	mutex_enter(&sc->intr_lock);
	if (sc->play.running || sc->record.running) {
		mutex_exit(&sc->intr_lock);
		return EBUSY;
	}
	mutex_exit(&sc->intr_lock);
	if (((sc->lease.routes & mode) != 0) == enable)
		return 0;
	if (!sc->lease.prepared) {
		error = a133_codec_prepare(&sc->io, &sc->lease);
		if (error != 0)
			return error;
		sc->ih = fdtbus_intr_establish_xname(sc->phandle, 0, IPL_SCHED,
		    FDT_INTR_MPSAFE, a133_interrupt, sc, device_xname(sc->dev));
		if (sc->ih == NULL) {
			a133_codec_restore(&sc->io, &sc->lease);
			return ENXIO;
		}
	}
	error = a133_codec_route(&sc->io, &sc->lease, mode, enable);
	if (error != 0 || sc->lease.routes == 0) {
		fdtbus_intr_disestablish(sc->phandle, sc->ih);
		sc->ih = NULL;
		a133_codec_restore(&sc->io, &sc->lease);
	}
	return error;
}

static int
a133_get_port(void *priv, mixer_ctrl_t *mc)
{
	struct a133_softc *sc = priv;

	if ((mc->dev != 2 && mc->dev != 3) || mc->type != AUDIO_MIXER_ENUM)
		return EINVAL;
	mc->un.ord = (sc->lease.routes & (mc->dev == 2 ? AUMODE_PLAY : AUMODE_RECORD)) != 0;
	return 0;
}

static int
a133_query_devinfo(void *priv, mixer_devinfo_t *di)
{

	(void)priv;
	di->prev = di->next = AUDIO_MIXER_LAST;
	if (di->index == 0 || di->index == 1) {
		di->type = AUDIO_MIXER_CLASS;
		di->mixer_class = di->index;
		strlcpy(di->label.name, di->index == 0 ? AudioCoutputs : AudioCrecord,
		    sizeof(di->label.name));
	} else if (di->index == 2 || di->index == 3) {
		di->type = AUDIO_MIXER_ENUM;
		di->mixer_class = di->index - 2;
		strlcpy(di->label.name, "route", sizeof(di->label.name));
		di->un.e.num_mem = 2;
		di->un.e.member[0].ord = 0;
		di->un.e.member[1].ord = 1;
		strlcpy(di->un.e.member[0].label.name, "disabled",
		    sizeof(di->un.e.member[0].label.name));
		strlcpy(di->un.e.member[1].label.name,
		    di->index == 2 ? "speaker" : "mic1",
		    sizeof(di->un.e.member[1].label.name));
	} else {
		return ENXIO;
	}
	return 0;
}

static int
a133_getdev(void *priv, struct audio_device *dev)
{
	static const struct audio_device info = { "A133 codec", "PIO guarded", "a133codec" };

	(void)priv;
	*dev = info;
	return 0;
}

static int
a133_props(void *priv)
{

	(void)priv;
	return AUDIO_PROP_PLAYBACK | AUDIO_PROP_CAPTURE |
	    AUDIO_PROP_INDEPENDENT | AUDIO_PROP_FULLDUPLEX;
}

static void
a133_get_locks(void *priv, kmutex_t **intr, kmutex_t **thread)
{
	struct a133_softc *sc = priv;

	*intr = &sc->intr_lock;
	*thread = &sc->lock;
}

static const struct audio_hw_if hw_if = {
	.query_format = a133_query_format, .set_format = a133_set_format,
	.round_blocksize = a133_round_block, .allocm = a133_alloc, .freem = a133_free,
	.trigger_output = a133_trigger_output, .trigger_input = a133_trigger_input,
	.halt_output = a133_halt_output, .halt_input = a133_halt_input,
	.set_port = a133_set_port, .get_port = a133_get_port,
	.query_devinfo = a133_query_devinfo, .getdev = a133_getdev,
	.get_props = a133_props, .get_locks = a133_get_locks
};

static int
a133_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *faa = aux;

	(void)parent;
	(void)cf;
	return of_compatible_match(OF_finddevice("/"), root_compat) != 0 &&
	    OF_getproplen(faa->faa_phandle, "ember,ys-m33-audio") == 0;
}

static void
a133_attach(device_t parent, device_t self, void *aux)
{
	struct a133_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	bus_addr_t addr;
	bus_size_t size;
	unsigned i;

	(void)parent;
	sc->dev = self;
	sc->phandle = faa->faa_phandle;
	sc->bst = faa->faa_bst;
	if (fdtbus_get_reg(sc->phandle, 0, &addr, &size) != 0 ||
	    addr != base[0] || size != 0x32c) {
		aprint_error(": unexpected codec register resource\n");
		return;
	}
	for (i = 0; i < 3; i++) {
		if (bus_space_map(sc->bst, base[i], mapsize[i], 0, &sc->bsh[i]) != 0) {
			while (sc->mapped != 0) {
				unsigned n = --sc->mapped;
				bus_space_unmap(sc->bst, sc->bsh[n], mapsize[n]);
			}
			aprint_error(": cannot map codec resources\n");
			return;
		}
		sc->mapped++;
	}
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->intr_lock, MUTEX_DEFAULT, IPL_SCHED);
	LIST_INIT(&sc->memory);
	sc->io = (struct a133_codec_io){ sc, a133_read, a133_write, a133_delay };
	evcnt_attach_dynamic(&sc->underrun, EVCNT_TYPE_MISC, NULL, device_xname(self), "TX underruns");
	evcnt_attach_dynamic(&sc->overrun, EVCNT_TYPE_MISC, NULL, device_xname(self), "RX overruns");
	evcnt_attach_dynamic(&sc->fault, EVCNT_TYPE_MISC, NULL, device_xname(self), "FIFO faults");
	evcnt_attach_dynamic(&sc->interrupts, EVCNT_TYPE_INTR, NULL, device_xname(self), "FIFO interrupts");
	sc->initialized = true;
	/* Mapping/querying performs no register I/O, clocking or analog enable. */
	aprint_naive("\n");
	aprint_normal(": YS-M33 A133 experimental codec (routes disabled)\n");
	if (audio_attach_mi(&hw_if, sc, self) == NULL)
		(void)a133_detach(self, 0);
}

static int
a133_detach(device_t self, int flags)
{
	struct a133_softc *sc = device_private(self);
	struct a133_memory *m;
	unsigned n;
	int error;

	if (!sc->initialized)
		return 0;
	error = config_detach_children(self, flags);
	if (error != 0)
		return error;
	mutex_enter(&sc->lock);
	mutex_enter(&sc->intr_lock);
	a133_halt(sc, AUMODE_PLAY);
	a133_halt(sc, AUMODE_RECORD);
	mutex_exit(&sc->intr_lock);
	if (sc->ih != NULL) {
		fdtbus_intr_disestablish(sc->phandle, sc->ih);
		sc->ih = NULL;
	}
	a133_codec_restore(&sc->io, &sc->lease);
	while ((m = LIST_FIRST(&sc->memory)) != NULL) {
		LIST_REMOVE(m, link);
		kmem_free(m->addr, m->size);
		kmem_free(m, sizeof(*m));
	}
	mutex_exit(&sc->lock);
	evcnt_detach(&sc->underrun);
	evcnt_detach(&sc->overrun);
	evcnt_detach(&sc->fault);
	evcnt_detach(&sc->interrupts);
	mutex_destroy(&sc->intr_lock);
	mutex_destroy(&sc->lock);
	for (n = sc->mapped; n > 0; n--)
		bus_space_unmap(sc->bst, sc->bsh[n - 1], mapsize[n - 1]);
	sc->mapped = 0;
	sc->initialized = false;
	return 0;
}

CFATTACH_DECL_NEW(sun50i_a133_codec, sizeof(struct a133_softc),
    a133_match, a133_attach, a133_detach, NULL);
