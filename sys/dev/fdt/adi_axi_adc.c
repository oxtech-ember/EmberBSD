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
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/lwp.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/queue.h>
#include <sys/systm.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/adi_axi_adcvar.h>
#include <dev/fdt/adi_axi_adc_private.h>
#include <dev/fdt/adi_axi_dmacvar.h>

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
	struct adi_axi_dmac_guard *l_guards[8];
	unsigned int l_nguards;
};

static LIST_HEAD(, adi_axi_adc_softc) adcs = LIST_HEAD_INITIALIZER(adcs);

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

int
adi_axi_adc_acquire(int phandle, device_t consumer,
    const struct adi_axi_capture_ops *ops, void *cookie,
    struct adi_axi_adc_lease **result)
{
	struct adi_axi_adc_softc *sc;
	struct adi_axi_converter *c;
	struct adi_axi_adc_lease *l;
	int error = ENXIO;

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
		while (l->l_nguards != 0)
			adi_axi_dmac_unreserve(l->l_guards[--l->l_nguards]);
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
	if (error != 0)
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
quiesce_locked(struct adi_axi_adc_lease *l, unsigned int timeout_ms)
{
	int error;

	if (l->l_poisoned)
		return EIO;
	if (timeout_ms == 0 || timeout_ms > 60000)
		return EINVAL;
	error = check_dmas(l);
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
	}
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, ADC_CHANNEL(0),
	    ADC_CHANNEL(3) - ADC_CHANNEL(0) + 4, BUS_SPACE_BARRIER_WRITE);
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
	int error;

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
	l->l_configurer = curlwp;
	error = c->c_ops->configure(c->c_cookie, config, &io);
	l->l_configurer = NULL;
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
	int error = 0;

	mutex_enter(&c->c_lock);
	if (l->l_poisoned)
		error = EIO;
	else if (!l->l_configured || !l->l_quiet || l->l_running)
		error = EBUSY;
	else {
		error = check_dmas(l);
		if (error != 0)
			poison_locked(l);
		else {
			l->l_running = true;
			l->l_quiet = false;
		}
	}
	mutex_exit(&c->c_lock);
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
	if (l->l_poisoned || l->l_running || !l->l_quiet) {
		mutex_exit(&c->c_lock);
		return EBUSY;
	}
	if (check_dmas(l) != 0) {
		poison_locked(l);
		mutex_exit(&c->c_lock);
		return EIO;
	}
	while (l->l_nguards != 0)
		adi_axi_dmac_unreserve(l->l_guards[--l->l_nguards]);
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
