/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * ADI AXI ADC attachment and exclusive converter/capture ownership.
 * Register layout: Analog Devices HDL up_adc_common/up_adc_channel,
 * https://analogdevicesinc.github.io/hdl/library/axi_ad9361/index.html
 * No converter initialization sequence or application HDL is embedded here.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/kmem.h>
#include <sys/lwp.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/adi_axi_adcvar.h>
#include <dev/fdt/adi_axi_adc_private.h>
#include <dev/fdt/adi_axi_dmacvar.h>
#include <dev/fdt/adi_axi_adc.h>

#define ADC_VERSION	0x000
#define ADC_RESET	0x040
#define ADC_CONTROL	0x044
#define ADC_STATUS	0x05c
#define ADC_DMA_STATUS	0x088
#define ADC_CHANNEL(n)	(0x400 + (n) * 0x40)
#define ADC_CH_STATUS(n)	(0x404 + (n) * 0x40)
#define ADC_CH_PN(n)	(0x418 + (n) * 0x40)
#define ADC_FORMAT_MASK	0x370
#define ADC_NCHANNELS	4
#define ADC_DAC_CONTROL	0x4048
#define ADC_DAC_RATE	0x404c

struct adi_axi_adc_softc {
	device_t sc_dev;
	int sc_phandle;
	int sc_converter_phandle;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	bus_size_t sc_size;
	struct clk *sc_clk;
	struct adi_axi_adc_lease *sc_lease;
	kmutex_t sc_session_lock;
	kcondvar_t sc_session_cv;
	bool sc_session_open;
	bool sc_session_busy;
	bool sc_configured;
	bool sc_capture_pending;
	bool sc_capture_valid;
	uint32_t sc_capture_samples;
	unsigned int sc_capture_timeout;
	size_t sc_capture_bytes;
	size_t sc_capture_offset;
	int sc_capture_error;
	struct adi_axi_dmac_buffer *sc_capture_buffer;
	uint8_t *sc_capture_data;
	LIST_ENTRY(adi_axi_adc_softc) sc_next;
};


struct adi_axi_adc_lease {
	struct adi_axi_adc_softc *l_adc;
	struct adi_axi_converter *l_converter;
	device_t l_consumer;
	const struct adi_axi_capture_ops *l_ops;
	void *l_cookie;
	struct lwp *l_configurer;
	bool l_configured;
	bool l_running;
	bool l_quiet;
	bool l_poisoned;
	uint32_t l_channel_mask;
	uint32_t l_channel_format;
	struct adi_axi_dmac_guard *l_capture_guard;
	struct adi_axi_dmac_guard *l_guards[8];
	unsigned int l_nguards;
};

static LIST_HEAD(, adi_axi_adc_softc) adcs = LIST_HEAD_INITIALIZER(adcs);
static int adiaxiadc_cdev_open(dev_t, int, int, struct lwp *);
static int adiaxiadc_cdev_close(dev_t, int, int, struct lwp *);
static int adiaxiadc_cdev_read(dev_t, struct uio *, int);
static int adiaxiadc_cdev_ioctl(dev_t, u_long, void *, int, struct lwp *);
static int adiaxiadc_dma_drain(void *, unsigned int);
static int adiaxiadc_cdev_quiesce(void *, unsigned int);
static int adiaxiadc_capture_locked(struct adi_axi_adc_softc *);
static int adi_axi_adc_start_locked(struct adi_axi_adc_lease *);

static const struct adi_axi_dmac_drain_ops adiaxiadc_drain_ops = {
	.drain = adiaxiadc_dma_drain
};

static const struct adi_axi_capture_ops adiaxiadc_cdev_capture_ops = {
	.quiesce = adiaxiadc_cdev_quiesce
};

static int adc_disable_channels(struct adi_axi_adc_lease *);

dev_type_open(adiaxiadc_cdev_open);
dev_type_close(adiaxiadc_cdev_close);
dev_type_read(adiaxiadc_cdev_read);
dev_type_ioctl(adiaxiadc_cdev_ioctl);

const struct cdevsw adiadc_cdevsw = {
	.d_open = adiaxiadc_cdev_open,
	.d_close = adiaxiadc_cdev_close,
	.d_read = adiaxiadc_cdev_read,
	.d_write = nowrite,
	.d_ioctl = adiaxiadc_cdev_ioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = nodiscard,
	.d_flag = D_OTHER
};

static int
reserve_dmas(struct adi_axi_adc_lease *l)
{
	struct adi_axi_adc_softc *sc = l->l_adc;
	uint32_t *cells;
	uint32_t count;
	int length, phandle, error = EINVAL;

	length = OF_getproplen(sc->sc_phandle, "dmas");
	if (length <= 0 || length % 8 != 0 || length > 8 * 8)
		return EINVAL;
	cells = kmem_alloc(length, KM_SLEEP);
	if (OF_getprop(sc->sc_phandle, "dmas", cells, length) != length)
		goto out;
	for (unsigned int i = 0; i < (unsigned int)length / 4; i += 2) {
		phandle = fdtbus_get_phandle_from_native(be32toh(cells[i]));
		if (phandle <= 0 ||
		    of_getprop_uint32(phandle, "#dma-cells", &count) != 0 ||
		    count != 1 || be32toh(cells[i + 1]) != 0) {
			error = EINVAL;
			goto out;
		}
		error = adi_axi_dmac_reserve(phandle, l->l_consumer,
		    &l->l_guards[l->l_nguards]);
		if (error != 0)
			goto out;
		l->l_nguards++;
		error = adi_axi_dmac_guard_bind(
		    l->l_guards[l->l_nguards - 1], &adiaxiadc_drain_ops, l);
		if (error != 0)
			goto out;
	}
	error = 0;
out:
	kmem_free(cells, length);
	return error;
}

static int
check_dmas(struct adi_axi_adc_lease *l)
{
	int error;

	for (unsigned int i = 0; i < l->l_nguards; i++) {
		error = adi_axi_dmac_guard_check(l->l_guards[i]);
		if (error != 0)
			return error;
	}
	return 0;
}

static bool
release_dmas(struct adi_axi_adc_lease *l)
{
	while (l->l_nguards != 0) {
		unsigned int i = l->l_nguards - 1;

		if (adi_axi_dmac_unreserve(l->l_guards[i]) != 0)
			return false;
		l->l_guards[i] = NULL;
		l->l_nguards--;
	}
	return true;
}

int
adi_axi_adc_acquire(int phandle, device_t consumer,
    const struct adi_axi_capture_ops *ops, void *cookie,
    struct adi_axi_adc_lease **result)
{
	struct adi_axi_adc_softc *sc;
	struct adi_axi_converter *c;
	struct adi_axi_adc_lease *l;
	int error = ENXIO;
	bool retained = false;

	if (consumer == NULL || ops == NULL || ops->quiesce == NULL ||
	    result == NULL)
		return EINVAL;
	*result = NULL;
	adi_axi_registry_init();
	l = kmem_zalloc(sizeof(*l), KM_SLEEP);
	mutex_enter(&adi_axi_registry_lock);
	LIST_FOREACH(sc, &adcs, sc_next)
		if (sc->sc_phandle == phandle)
			break;
	if (sc == NULL)
		goto out;
	LIST_FOREACH(c, &adi_axi_converters, c_next)
		if (c->c_phandle == sc->sc_converter_phandle)
			break;
	if (c == NULL)
		goto out;
	mutex_enter(&c->c_lock);
	if (c->c_poisoned || c->c_lease != NULL || sc->sc_lease != NULL) {
		error = c->c_poisoned ? EIO : EBUSY;
		mutex_exit(&c->c_lock);
		goto out;
	}
	l->l_adc = sc;
	l->l_converter = c;
	l->l_consumer = consumer;
	l->l_ops = ops;
	l->l_cookie = cookie;
	error = reserve_dmas(l);
	if (error != 0) {
		if (!release_dmas(l)) {
			/* A failed drain keeps every reachable owner and lease pinned. */
			l->l_poisoned = true;
			l->l_quiet = false;
			c->c_poisoned = true;
			c->c_lease = sc->sc_lease = l;
			retained = true;
			device_acquire(sc->sc_dev);
			device_acquire(consumer);
			mutex_exit(&c->c_lock);
			error = EIO;
			goto out;
		}
		if (error == EIO)
			c->c_poisoned = true;
		mutex_exit(&c->c_lock);
		goto out;
	}
	c->c_lease = sc->sc_lease = l;
	device_acquire(sc->sc_dev);
	device_acquire(consumer);
	mutex_exit(&c->c_lock);
	*result = l;
	error = 0;
out:
	mutex_exit(&adi_axi_registry_lock);
	if (error != 0 && !retained)
		kmem_free(l, sizeof(*l));
	return error;
}

static void
poison_locked(struct adi_axi_adc_lease *l)
{
	l->l_poisoned = true;
	l->l_quiet = false;
	l->l_converter->c_poisoned = true;
}

static int
adiaxiadc_dma_drain(void *cookie, unsigned int timeout_ms)
{
	struct adi_axi_adc_lease *l = cookie;
	int error;

	KASSERT(mutex_owned(&l->l_converter->c_lock));
	if (l->l_poisoned)
		return EIO;
	error = adc_disable_channels(l);
	if (error == 0)
		error = l->l_ops->quiesce(l->l_cookie, timeout_ms);
	if (error != 0)
		poison_locked(l);
	return error;
}

/* FIFO sources can keep producing while the DMAC drops idle input.
 * The witness below proves memory-side AXI drain, not analog RX silence.
 * Hold every guard and the converter lease across the shared deadline.
 */
static int
adiaxiadc_cdev_quiesce(void *cookie, unsigned int timeout_ms)
{
	struct adi_axi_adc_softc *sc = cookie;
	struct adi_axi_adc_lease *l = sc->sc_lease;
	unsigned int started, budget, elapsed, remaining_ms;
	int error;

	if (l == NULL || l->l_nguards == 0)
		return ENXIO;
	if (timeout_ms == 0 || timeout_ms > 60000)
		return EINVAL;
	started = (unsigned int)getticks();
	budget = MAX(1, mstohz(timeout_ms));
	for (unsigned int i = 0; i < l->l_nguards; i++) {
		elapsed = (unsigned int)getticks() - started;
		if (elapsed >= budget)
			return ETIMEDOUT;
		remaining_ms = MAX(1, (uint64_t)(budget - elapsed) * 1000 / hz);
		error = adi_axi_dmac_guard_wait_idle(l->l_guards[i], remaining_ms);
		if (error != 0)
			return error;
	}
	return 0;
}

static int
adc_disable_channels(struct adi_axi_adc_lease *l)
{
	struct adi_axi_adc_softc *sc = l->l_adc;
	uint32_t value;

	for (unsigned int i = 0; i < ADC_NCHANNELS; i++) {
		value = bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(i));
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(i), value & ~1U);
		if (bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(i)) & 1U)
			return EIO;
	}
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(0),
	    ADC_CHANNEL(3) - ADC_CHANNEL(0) + 4, BUS_SPACE_BARRIER_WRITE);
	return 0;
}

static int
quiesce_locked(struct adi_axi_adc_lease *l, unsigned int timeout_ms)
{
	int error;

	if (l->l_poisoned)
		return EIO;
	if (timeout_ms == 0 || timeout_ms > 60000)
		return EINVAL;
	error = check_dmas(l);
	if (error == 0)
		error = adc_disable_channels(l);
	if (error == 0)
		error = l->l_ops->quiesce(l->l_cookie, timeout_ms);
	if (error == 0)
		error = check_dmas(l);
	if (error != 0) {
		poison_locked(l);
		return error;
	}
	l->l_running = false;
	l->l_quiet = true;
	return 0;
}

static bool
rx_config_valid(const struct adi_axi_rx_config *config)
{
	return config != NULL && config->frequency_hz != 0 &&
	    config->sample_rate_hz != 0 && config->bandwidth_hz != 0 &&
	    config->rx_mask != 0 && !(config->rx_mask & ~3U) &&
	    config->channel_mask != 0 && !(config->channel_mask & ~0xfU);
}

int
adi_axi_adc_bootstrap(struct adi_axi_adc_lease *l,
    const struct adi_axi_rx_config *config, unsigned int timeout_ms)
{
	struct adi_axi_converter *c = l->l_converter;
	int error;

	if (!rx_config_valid(config) || timeout_ms == 0 || timeout_ms > 60000)
		return EINVAL;
	mutex_enter(&c->c_lock);
	if (l->l_poisoned) {
		error = EIO;
		goto out;
	}
	if (c->c_bootstrap_used || l->l_running || l->l_quiet || l->l_configured) {
		error = EALREADY;
		goto out;
	}
	if (c->c_ops->bootstrap == NULL || l->l_ops->cold_bootstrap == NULL) {
		error = EOPNOTSUPP;
		goto out;
	}
	c->c_bootstrap_used = true;
	error = 0;
	for (unsigned int i = 0; i < l->l_nguards && error == 0; i++)
		error = adi_axi_dmac_guard_cold_check(l->l_guards[i]);
	if (error == 0)
		error = l->l_ops->cold_bootstrap(l->l_cookie);
	if (error == 0)
		error = check_dmas(l);
	if (error == 0)
		error = c->c_ops->bootstrap(c->c_cookie, config);
	/* No ADC I/O capability is issued until a genuine drain is observed. */
	if (error == 0)
		error = quiesce_locked(l, timeout_ms);
	if (error != 0)
		poison_locked(l);
out:
	mutex_exit(&c->c_lock);
	return error;
}

static bool
io_allowed(struct adi_axi_adc_lease *l)
{
	return mutex_owned(&l->l_converter->c_lock) &&
	    l->l_configurer == curlwp && l->l_quiet && !l->l_poisoned;
}

static int
adc_reset(void *cookie)
{
	struct adi_axi_adc_lease *l = cookie;
	struct adi_axi_adc_softc *sc = l->l_adc;

	if (!io_allowed(l))
		return EPERM;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_RESET, 0);
	bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_RESET);
	delay(10000);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_RESET, 2);
	bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_RESET);
	delay(10000);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_RESET, 3);
	return bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_RESET) == 3 ? 0 : EIO;
}

static int
adc_channels(void *cookie, uint32_t mask, uint32_t format)
{
	struct adi_axi_adc_lease *l = cookie;
	struct adi_axi_adc_softc *sc = l->l_adc;

	if (!io_allowed(l))
		return EPERM;
	if ((mask & ~0xfU) || (format & ~ADC_FORMAT_MASK))
		return EINVAL;
	for (unsigned int i = 0; i < ADC_NCHANNELS; i++) {
		if (mask == 0) {
			/* Failure cleanup only disables capture; preserve its setup. */
			uint32_t value = bus_space_read_4(sc->sc_bst, sc->sc_bsh,
			    ADC_CHANNEL(i));
			bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(i),
			    value & ~1U);
			continue;
		}
		/* Neutral DC offset and unity I/Q correction before enabling it. */
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(i) + 0x10, 0);
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(i) + 0x14,
		    (i & 1) ? 0x00004000 : 0x40000000);
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(i),
		    format | ((mask >> i) & 1));
		if ((bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(i)) & 1U) !=
		    ((mask >> i) & 1U))
			return EIO;
	}
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(0),
	    ADC_CHANNEL(3) - ADC_CHANNEL(0) + 4, BUS_SPACE_BARRIER_WRITE);
	if (mask != 0) {
		l->l_channel_mask = mask;
		l->l_channel_format = format;
	}
	return 0;
}

static int
adc_interface(void *cookie, bool two_rx, bool lvds, bool half_rate)
{
	struct adi_axi_adc_lease *l = cookie;
	struct adi_axi_adc_softc *sc = l->l_adc;
	uint32_t value, rate;

	if (!io_allowed(l))
		return EPERM;
	if (sc->sc_size < ADC_DAC_RATE + 4)
		return EOPNOTSUPP;
	value = bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_CONTROL);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_CONTROL,
	    two_rx ? (value & ~4U) : (value | 4U));
	value = bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_DAC_CONTROL);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_DAC_CONTROL,
	    two_rx ? (value & ~32U) : (value | 32U));
	rate = two_rx ? ((!half_rate && lvds) ? 3 : 1) :
	    ((!half_rate && lvds) ? 1 : 0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_DAC_RATE, rate);
	bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_DAC_RATE);
	return 0;
}

static int
adc_pn_select(void *cookie, unsigned int channel, unsigned int mode)
{
	struct adi_axi_adc_lease *l = cookie;
	struct adi_axi_adc_softc *sc = l->l_adc;
	uint32_t value;

	if (!io_allowed(l))
		return EPERM;
	if (channel >= ADC_NCHANNELS || mode > 15)
		return EINVAL;
	value = bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_CH_PN(channel));
	value = (value & ~0xf0000U) | (mode << 16);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_CH_PN(channel), value);
	return 0;
}

static int
adc_pn_status(void *cookie, unsigned int channel, bool clear, uint32_t *status)
{
	struct adi_axi_adc_lease *l = cookie;
	struct adi_axi_adc_softc *sc = l->l_adc;

	if (!io_allowed(l))
		return EPERM;
	if (channel >= ADC_NCHANNELS || status == NULL)
		return EINVAL;
	*status = bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_CH_STATUS(channel));
	if (clear)
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, ADC_CH_STATUS(channel), 7);
	return (bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_STATUS) & 1) ? 0 : EIO;
}

int
adi_axi_adc_configure(struct adi_axi_adc_lease *l,
    const struct adi_axi_rx_config *config, unsigned int timeout_ms)
{
	struct adi_axi_converter *c = l->l_converter;
	const struct adi_axi_adc_io io = {
		.cookie = l, .reset = adc_reset, .channels = adc_channels,
		.interface = adc_interface,
		.pn_select = adc_pn_select, .pn_status = adc_pn_status,
	};
	int error, cleanup_error;

	if (!rx_config_valid(config))
		return EINVAL;
	mutex_enter(&c->c_lock);
	if (l->l_running) {
		error = EBUSY;
		goto out;
	}
	c->c_bootstrap_used = true;
	l->l_configured = false;
	error = quiesce_locked(l, timeout_ms);
	if (error != 0)
		goto out;
	l->l_channel_mask = 0;
	l->l_configurer = curlwp;
	error = c->c_ops->configure(c->c_cookie, config, &io);
	if (error == 0 && l->l_channel_mask != config->channel_mask)
		error = EINVAL;
	if (error == 0) {
		for (unsigned int i = 0; i < l->l_nguards; i++) {
			error = adi_axi_dmac_guard_prepare(l->l_guards[i]);
			if (error == EOPNOTSUPP)
				continue;
			if (error == 0)
				l->l_capture_guard = l->l_guards[i];
			break;
		}
	}
	l->l_configurer = NULL;
	if (error == 0)
		error = quiesce_locked(l, timeout_ms);
	else if (!l->l_poisoned) {
		/* A rejected setup still has to leave every ADC channel disabled. */
		cleanup_error = quiesce_locked(l, timeout_ms);
		if (cleanup_error != 0)
			error = cleanup_error;
	}
	if (error == 0)
		l->l_configured = true;
out:
	mutex_exit(&c->c_lock);
	return error;
}

int
adi_axi_adc_read_rx_status(struct adi_axi_adc_lease *l,
    struct adi_axi_rx_status *status)
{
	struct adi_axi_converter *c;
	int error;

	if (l == NULL || status == NULL)
		return EINVAL;
	c = l->l_converter;
	memset(status, 0, sizeof(*status));
	mutex_enter(&c->c_lock);
	if (l->l_poisoned || c->c_poisoned)
		error = EIO;
	else if (!l->l_configured || c->c_lease != l)
		error = EBUSY;
	else if (c->c_ops->read_rx_status == NULL)
		error = EOPNOTSUPP;
	else
		error = c->c_ops->read_rx_status(c->c_cookie, status);
	if (error != 0)
		memset(status, 0, sizeof(*status));
	mutex_exit(&c->c_lock);
	return error;
}

int
adi_axi_adc_retune_rx(struct adi_axi_adc_lease *l, uint64_t frequency)
{
	struct adi_axi_converter *c;
	int error;

	if (l == NULL || frequency < 70000000 || frequency > 6000000000ULL)
		return EINVAL;
	c = l->l_converter;
	mutex_enter(&c->c_lock);
	if (l->l_poisoned || c->c_poisoned)
		error = EIO;
	else if (c->c_lease != l || !l->l_configured || !l->l_quiet || l->l_running)
		error = EBUSY;
	else if (c->c_ops->retune_rx == NULL)
		error = EOPNOTSUPP;
	else {
		error = check_dmas(l);
		if (error == 0)
			error = c->c_ops->retune_rx(c->c_cookie, frequency);
		if (error != 0)
			poison_locked(l);
	}
	mutex_exit(&c->c_lock);
	return error;
}

int
adi_axi_adc_start(struct adi_axi_adc_lease *l)
{
	struct adi_axi_converter *c = l->l_converter;
	int error;

	mutex_enter(&c->c_lock);
	error = adi_axi_adc_start_locked(l);
	mutex_exit(&c->c_lock);
	return error;
}

static int
adi_axi_adc_start_locked(struct adi_axi_adc_lease *l)
{
	int error = 0;

	KASSERT(mutex_owned(&l->l_converter->c_lock));
	if (l->l_poisoned)
		error = EIO;
	else if (!l->l_configured || !l->l_quiet || l->l_running)
		error = EBUSY;
	else {
		error = check_dmas(l);
		if (error != 0)
			poison_locked(l);
		else {
			if (l->l_channel_mask == 0) {
				error = EIO;
				poison_locked(l);
			} else {
				l->l_configurer = curlwp;
				error = adc_channels(l, l->l_channel_mask,
				    l->l_channel_format);
				l->l_configurer = NULL;
				if (error != 0) {
					int setup_error = error;

					if (quiesce_locked(l, 1000) != 0)
						error = EIO;
					else
						error = setup_error;
				} else {
					l->l_running = true;
					l->l_quiet = false;
				}
			}
		}
	}
	return error;
}

int
adi_axi_adc_stop(struct adi_axi_adc_lease *l, unsigned int timeout_ms)
{
	int error;

	mutex_enter(&l->l_converter->c_lock);
	error = quiesce_locked(l, timeout_ms);
	mutex_exit(&l->l_converter->c_lock);
	return error;
}

int
adi_axi_adc_release(struct adi_axi_adc_lease *l)
{
	struct adi_axi_converter *c = l->l_converter;

	mutex_enter(&c->c_lock);
	if (l->l_poisoned || l->l_running ||
	    (!l->l_quiet && l->l_configured)) {
		mutex_exit(&c->c_lock);
		return EBUSY;
	}
	if (check_dmas(l) != 0) {
		poison_locked(l);
		mutex_exit(&c->c_lock);
		return EIO;
	}
	if (!release_dmas(l)) {
		poison_locked(l);
		mutex_exit(&c->c_lock);
		return EIO;
	}
	l->l_adc->sc_lease = NULL;
	c->c_lease = NULL;
	mutex_exit(&c->c_lock);
	device_release(l->l_adc->sc_dev);
	device_release(l->l_consumer);
	kmem_free(l, sizeof(*l));
	return 0;
}

void
adi_axi_adc_poison(struct adi_axi_adc_lease *l)
{
	mutex_enter(&l->l_converter->c_lock);
	poison_locked(l);
	mutex_exit(&l->l_converter->c_lock);
}

/* Only successful attachments enter this registry. Built-in providers do
 * not detach, so the returned softc and initialized locks remain live.
 */
static struct adi_axi_adc_softc *
adiaxiadc_cdev_lookup(dev_t dev)
{
	struct adi_axi_adc_softc *sc;

	adi_axi_registry_init();
	mutex_enter(&adi_axi_registry_lock);
	LIST_FOREACH(sc, &adcs, sc_next)
		if (device_unit(sc->sc_dev) == minor(dev))
			break;
	mutex_exit(&adi_axi_registry_lock);
	return sc;
}

static int
adiaxiadc_session_claim(struct adi_axi_adc_softc *sc)
{
	int error = 0;

	mutex_enter(&sc->sc_session_lock);
	if (!sc->sc_session_open)
		error = EBADF;
	else if (sc->sc_session_busy)
		error = EBUSY;
	else
		sc->sc_session_busy = true;
	mutex_exit(&sc->sc_session_lock);
	return error;
}

static void
adiaxiadc_session_release(struct adi_axi_adc_softc *sc)
{
	mutex_enter(&sc->sc_session_lock);
	sc->sc_session_busy = false;
	cv_broadcast(&sc->sc_session_cv);
	mutex_exit(&sc->sc_session_lock);
}

static bool
adiaxiadc_reserved_zero(const uint32_t *reserved, size_t count)
{
	for (size_t i = 0; i < count; i++)
		if (reserved[i] != 0)
			return false;
	return true;
}

static int
adiaxiadc_cdev_open(dev_t dev, int flags, int mode, struct lwp *l)
{
	struct adi_axi_adc_softc *sc = adiaxiadc_cdev_lookup(dev);
	struct adi_axi_adc_lease *lease;
	int error;

	(void)mode;
	(void)l;
	if (sc == NULL)
		return ENXIO;
	if ((flags & FREAD) == 0 || (flags & FWRITE) != 0)
		return EACCES;
	mutex_enter(&sc->sc_session_lock);
	if (sc->sc_session_open) {
		mutex_exit(&sc->sc_session_lock);
		return EBUSY;
	}
	sc->sc_session_open = true;
	mutex_exit(&sc->sc_session_lock);
	error = adi_axi_adc_acquire(sc->sc_phandle, sc->sc_dev,
	    &adiaxiadc_cdev_capture_ops, sc, &lease);
	if (error != 0) {
		mutex_enter(&sc->sc_session_lock);
		sc->sc_session_open = false;
		mutex_exit(&sc->sc_session_lock);
		return error;
	}
	mutex_enter(&sc->sc_session_lock);
	sc->sc_configured = false;
	sc->sc_capture_pending = false;
	sc->sc_capture_valid = false;
	sc->sc_capture_samples = 0;
	sc->sc_capture_timeout = 0;
	sc->sc_capture_bytes = 0;
	sc->sc_capture_offset = 0;
	sc->sc_capture_error = 0;
	sc->sc_capture_buffer = NULL;
	sc->sc_capture_data = NULL;
	mutex_exit(&sc->sc_session_lock);
	return 0;
}

static int
adiaxiadc_cdev_close(dev_t dev, int flags, int mode, struct lwp *l)
{
	struct adi_axi_adc_softc *sc = adiaxiadc_cdev_lookup(dev);
	struct adi_axi_adc_lease *lease;
	int error = 0;

	(void)flags;
	(void)mode;
	(void)l;
	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->sc_session_lock);
	if (!sc->sc_session_open) {
		mutex_exit(&sc->sc_session_lock);
		return EBADF;
	}
	while (sc->sc_session_busy)
		cv_wait(&sc->sc_session_cv, &sc->sc_session_lock);
	sc->sc_session_busy = true;
	lease = sc->sc_lease;
	mutex_exit(&sc->sc_session_lock);

	if (lease != NULL && lease->l_configured)
		error = adi_axi_adc_stop(lease, 1000);
	if (error == 0 && sc->sc_capture_buffer != NULL) {
		if (lease == NULL || lease->l_capture_guard == NULL)
			error = EIO;
		else
			error = adi_axi_dmac_guard_buffer_free(lease->l_capture_guard,
			    sc->sc_capture_buffer);
		if (error == 0)
			sc->sc_capture_buffer = NULL;
	}
	if (error == 0 && lease != NULL)
		error = adi_axi_adc_release(lease);
	if (error == 0) {
		if (sc->sc_capture_data != NULL) {
			memset(sc->sc_capture_data, 0, sc->sc_capture_bytes);
			kmem_free(sc->sc_capture_data, sc->sc_capture_bytes);
			sc->sc_capture_data = NULL;
		}
		mutex_enter(&sc->sc_session_lock);
		sc->sc_session_open = false;
		sc->sc_configured = false;
		sc->sc_capture_pending = false;
		sc->sc_capture_valid = false;
		sc->sc_capture_bytes = 0;
		sc->sc_capture_offset = 0;
		sc->sc_capture_error = 0;
		mutex_exit(&sc->sc_session_lock);
	}
	mutex_enter(&sc->sc_session_lock);
	sc->sc_session_busy = false;
	cv_broadcast(&sc->sc_session_cv);
	mutex_exit(&sc->sc_session_lock);
	return error;
}

static int
adiaxiadc_capture_locked(struct adi_axi_adc_softc *sc)
{
	struct adi_axi_adc_lease *lease = sc->sc_lease;
	struct adi_axi_converter *converter;
	int error, cleanup_error;

	if (lease == NULL || lease->l_capture_guard == NULL ||
	    sc->sc_capture_buffer == NULL || sc->sc_capture_data == NULL)
		return EINVAL;
	converter = lease->l_converter;
	mutex_enter(&converter->c_lock);
	if (lease->l_poisoned || !lease->l_configured || !lease->l_quiet ||
	    lease->l_running) {
		error = lease->l_poisoned ? EIO : EBUSY;
		goto out;
	}
	error = adi_axi_adc_start_locked(lease);
	if (error == 0)
		error = adi_axi_dmac_guard_transfer(lease->l_capture_guard,
		    sc->sc_capture_buffer, sc->sc_capture_bytes,
		    sc->sc_capture_timeout);
	if (!lease->l_poisoned) {
		cleanup_error = quiesce_locked(lease, sc->sc_capture_timeout);
		if (cleanup_error != 0)
			error = cleanup_error;
	}
	if (error == 0)
		error = adi_axi_dmac_guard_buffer_read(lease->l_capture_guard,
		    sc->sc_capture_buffer, sc->sc_capture_data,
		    sc->sc_capture_bytes);
out:
	mutex_exit(&converter->c_lock);
	if (error != 0)
		memset(sc->sc_capture_data, 0, sc->sc_capture_bytes);
	return error;
}

static int
adiaxiadc_cdev_read(dev_t dev, struct uio *uio, int flags)
{
	struct adi_axi_adc_softc *sc = adiaxiadc_cdev_lookup(dev);
	size_t capture_bytes, remaining, length, moved, resid_before;
	bool capture;
	int error = 0;

	(void)flags;
	if (sc == NULL)
		return ENXIO;
	if (uio->uio_resid == 0)
		return 0;
	mutex_enter(&sc->sc_session_lock);
	if (!sc->sc_session_open) {
		error = EBADF;
		goto unlock;
	}
	if (sc->sc_session_busy) {
		error = EBUSY;
		goto unlock;
	}
	if (!sc->sc_capture_valid && !sc->sc_capture_pending) {
		error = EINVAL;
		goto unlock;
	}
	capture = sc->sc_capture_pending;
	capture_bytes = sc->sc_capture_bytes;
	if (capture && (uio->uio_resid < capture_bytes ||
	    capture_bytes % ADI_AXI_ADC_CAPTURE_ALIGNMENT != 0)) {
		error = EINVAL;
		goto unlock;
	}
	if (capture) {
		/* A failed retry cannot expose the preceding IQ frame. */
		sc->sc_capture_valid = false;
		sc->sc_capture_pending = false;
		sc->sc_capture_offset = 0;
	}
	sc->sc_session_busy = true;
	mutex_exit(&sc->sc_session_lock);

	if (capture) {
		error = adiaxiadc_capture_locked(sc);
		mutex_enter(&sc->sc_session_lock);
		sc->sc_capture_error = error;
		if (error == 0)
			sc->sc_capture_valid = true;
		mutex_exit(&sc->sc_session_lock);
	}
	if (error == 0) {
		mutex_enter(&sc->sc_session_lock);
		remaining = sc->sc_capture_bytes - sc->sc_capture_offset;
		length = uimin(uio->uio_resid, remaining);
		mutex_exit(&sc->sc_session_lock);
		resid_before = uio->uio_resid;
		error = uiomove(sc->sc_capture_data + sc->sc_capture_offset,
		    length, uio);
		moved = resid_before - uio->uio_resid;
		mutex_enter(&sc->sc_session_lock);
		if (moved <= length)
			sc->sc_capture_offset += moved;
		mutex_exit(&sc->sc_session_lock);
	}
	adiaxiadc_session_release(sc);
	return error;
unlock:
	mutex_exit(&sc->sc_session_lock);
	return error;
}

static int
adiaxiadc_cdev_ioctl(dev_t dev, u_long command, void *data, int flags,
	struct lwp *l)
{
	struct adi_axi_adc_softc *sc = adiaxiadc_cdev_lookup(dev);
	struct adi_axi_adc_lease *lease;
	int error;

	(void)flags;
	(void)l;
	if (sc == NULL)
		return ENXIO;
	error = adiaxiadc_session_claim(sc);
	if (error != 0)
		return error;
	lease = sc->sc_lease;
	if (data == NULL || lease == NULL) {
		error = EINVAL;
		goto out;
	}
	switch (command) {
	case ADI_AXI_ADCIOC_CONFIG: {
		struct adi_axi_adc_config *user = data;
		struct adi_axi_rx_config config;

		if (user->abi_version != ADI_AXI_ADC_ABI_VERSION ||
		    user->struct_size != sizeof(*user) ||
		    user->frequency_hz < 70000000ULL ||
		    user->frequency_hz > 6000000000ULL ||
		    user->sample_rate_hz == 0 || user->bandwidth_hz == 0 ||
		    user->rf_port > 1 ||
		    user->rx_mask != ADI_AXI_ADC_RX1_MASK ||
		    user->channel_mask != ADI_AXI_ADC_RX1_IQ_CHANNEL_MASK ||
		    user->timeout_ms == 0 || user->timeout_ms > 60000 ||
		    user->gain_mode[0] > ADI_AXI_ADC_GAIN_FAST ||
		    user->gain_mode[1] > ADI_AXI_ADC_GAIN_FAST ||
		    !adiaxiadc_reserved_zero(user->reserved,
		    __arraycount(user->reserved))) {
			error = EINVAL;
			break;
		}
		sc->sc_capture_pending = false;
		sc->sc_capture_valid = false;
		sc->sc_capture_offset = 0;
		if (sc->sc_capture_buffer != NULL) {
			error = adi_axi_dmac_guard_buffer_free(
			    lease->l_capture_guard, sc->sc_capture_buffer);
			if (error != 0)
				break;
			sc->sc_capture_buffer = NULL;
		}
		if (sc->sc_capture_data != NULL) {
			memset(sc->sc_capture_data, 0, sc->sc_capture_bytes);
			kmem_free(sc->sc_capture_data, sc->sc_capture_bytes);
			sc->sc_capture_data = NULL;
		}
		sc->sc_capture_pending = false;
		sc->sc_capture_valid = false;
		sc->sc_capture_bytes = 0;
		sc->sc_capture_offset = 0;
		config.frequency_hz = user->frequency_hz;
		config.sample_rate_hz = user->sample_rate_hz;
		config.bandwidth_hz = user->bandwidth_hz;
		config.rf_port = user->rf_port;
		config.rx_mask = user->rx_mask;
		config.channel_mask = user->channel_mask;
		memcpy(config.gain_mdB, user->gain_mdB,
		    sizeof(config.gain_mdB));
		config.gain_mode[0] = user->gain_mode[0];
		config.gain_mode[1] = user->gain_mode[1];
		error = adi_axi_adc_configure(lease, &config, user->timeout_ms);
		sc->sc_configured = error == 0;
		break;
	}
	case ADI_AXI_ADCIOC_CAPTURE: {
		struct adi_axi_adc_capture *request = data;
		size_t bytes;

		if (request->abi_version != ADI_AXI_ADC_ABI_VERSION ||
		    request->struct_size != sizeof(*request) ||
		    request->sample_count == 0 ||
		    request->sample_count > ADI_AXI_ADC_CAPTURE_MAX_BYTES /
		    ADI_AXI_ADC_IQ_SAMPLE_BYTES ||
		    request->timeout_ms == 0 || request->timeout_ms > 60000 ||
		    !adiaxiadc_reserved_zero(request->reserved,
		    __arraycount(request->reserved))) {
			error = EINVAL;
			break;
		}
		bytes = (size_t)request->sample_count *
		    ADI_AXI_ADC_IQ_SAMPLE_BYTES;
		if (!sc->sc_configured || lease->l_capture_guard == NULL ||
		    bytes % ADI_AXI_ADC_CAPTURE_ALIGNMENT != 0) {
			error = EINVAL;
			break;
		}
		sc->sc_capture_pending = false;
		sc->sc_capture_valid = false;
		sc->sc_capture_offset = 0;
		if (sc->sc_capture_buffer != NULL) {
			error = adi_axi_dmac_guard_buffer_free(
			    lease->l_capture_guard, sc->sc_capture_buffer);
			if (error != 0)
				break;
			sc->sc_capture_buffer = NULL;
		}
		if (sc->sc_capture_data != NULL) {
			memset(sc->sc_capture_data, 0, sc->sc_capture_bytes);
			kmem_free(sc->sc_capture_data, sc->sc_capture_bytes);
			sc->sc_capture_data = NULL;
		}
		error = adi_axi_dmac_guard_buffer_alloc(lease->l_capture_guard,
		    bytes, &sc->sc_capture_buffer);
		if (error != 0)
			break;
		sc->sc_capture_data = kmem_alloc(bytes, KM_SLEEP);
		memset(sc->sc_capture_data, 0, bytes);
		sc->sc_capture_samples = request->sample_count;
		sc->sc_capture_timeout = request->timeout_ms;
		sc->sc_capture_bytes = bytes;
		sc->sc_capture_offset = 0;
		sc->sc_capture_error = 0;
		sc->sc_capture_valid = false;
		sc->sc_capture_pending = true;
		break;
	}
	case ADI_AXI_ADCIOC_STATUS: {
		struct adi_axi_adc_status *status = data;
		struct adi_axi_rx_status rxstatus;

		if (status->abi_version != ADI_AXI_ADC_ABI_VERSION ||
		    status->struct_size != sizeof(*status)) {
			error = EINVAL;
			break;
		}
		memset(&rxstatus, 0, sizeof(rxstatus));
		error = sc->sc_configured ?
		    adi_axi_adc_read_rx_status(lease, &rxstatus) : 0;
		if (error == EOPNOTSUPP)
			error = 0;
		if (error != 0)
			break;
		memset(status, 0, sizeof(*status));
		status->abi_version = ADI_AXI_ADC_ABI_VERSION;
		status->struct_size = sizeof(*status);
		status->valid_mask = rxstatus.valid_mask;
		memcpy(status->rssi_mdB, rxstatus.rssi_mdB,
		    sizeof(status->rssi_mdB));
		memcpy(status->gain_mdB, rxstatus.gain_mdB,
		    sizeof(status->gain_mdB));
		status->state = sc->sc_capture_error != 0 ?
		    ADI_AXI_ADC_STATE_ERROR : sc->sc_capture_valid ?
		    ADI_AXI_ADC_STATE_CAPTURED : sc->sc_configured ?
		    ADI_AXI_ADC_STATE_CONFIGURED : ADI_AXI_ADC_STATE_IDLE;
		status->captured_samples = sc->sc_capture_valid ?
		    sc->sc_capture_samples : 0;
		status->last_capture_error = sc->sc_capture_error;
		break;
	}
	default:
		error = ENOTTY;
		break;
	}
out:
	adiaxiadc_session_release(sc);
	return error;
}

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "adi,axi-ad9361-6.00.a" },
	DEVICE_COMPAT_EOL
};

static int
adiaxiadc_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
adiaxiadc_attach(device_t parent, device_t self, void *aux)
{
	struct adi_axi_adc_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	bus_addr_t address;
	uint32_t core_version;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_converter_phandle = fdtbus_get_phandle(sc->sc_phandle, "spibus-connected");
	sc->sc_bst = faa->faa_bst;
	if (sc->sc_converter_phandle <= 0 ||
	    fdtbus_get_reg(sc->sc_phandle, 0, &address, &sc->sc_size) != 0 ||
	    sc->sc_size < ADC_CH_PN(3) + 4) {
		aprint_error(": invalid converter/register description\n");
		return;
	}
	sc->sc_clk = fdtbus_clock_get(sc->sc_phandle, "s_axi_aclk");
	if (sc->sc_clk == NULL)
		sc->sc_clk = fdtbus_clock_get_index(sc->sc_phandle, 0);
	if (sc->sc_clk == NULL || clk_enable(sc->sc_clk) != 0) {
		aprint_error(": register clock unavailable\n");
		return;
	}
	if (bus_space_map(sc->sc_bst, address, sc->sc_size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": register mapping failed\n");
		clk_disable(sc->sc_clk);
		return;
	}
	core_version = bus_space_read_4(sc->sc_bst, sc->sc_bsh, ADC_VERSION);
	if (((core_version >> 16) & 0xff) != 10) {
		aprint_error(": unsupported ADC core_version %#x\n", core_version);
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, sc->sc_size);
		clk_disable(sc->sc_clk);
		return;
	}
	mutex_init(&sc->sc_session_lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&sc->sc_session_cv, "adiadc");
	adi_axi_registry_init();
	mutex_enter(&adi_axi_registry_lock);
	LIST_INSERT_HEAD(&adcs, sc, sc_next);
	mutex_exit(&adi_axi_registry_lock);
	aprint_naive("\n");
	aprint_normal(": ADI AXI ADC %u.%u.%c, capture lease required\n",
	    (core_version >> 16) & 0xff, (core_version >> 8) & 0xff, core_version & 0xff);
}

/* Built-in provider: no detach/suspend until clock/IRQ teardown is qualified. */
CFATTACH_DECL_NEW(adiaxiadc, sizeof(struct adi_axi_adc_softc),
    adiaxiadc_match, adiaxiadc_attach, NULL, NULL);
