/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Native A733 PWM controller. Register facts were checked against the
 * Orange Pi vendor PWM v203 description at revision
 * 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f. No Linux code is used here.
 * Only continuous output from HOSC is supported. Capture, grouped pulses,
 * dead-zone pairs, and clock bypass are not exposed by this driver.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/pwm/pwmvar.h>
#include <dev/fdt/fdtvar.h>

/* BEGIN PWM CONTRACT */
#define	A733_PWM_NCHANNELS	10
#define	A733_PWM_SIZE		0x400
#define	A733_PWM_PCCR(n)		(0x20 + ((n) / 2) * 4)
#define	A733_PWM_SOURCE		__BITS(8, 7)
#define	A733_PWM_DIVIDER		__BITS(3, 0)
#define	A733_PWM_PCGR		0x40
#define	A733_PWM_GATE(n)		__BIT(n)
#define	A733_PWM_BYPASS(n)	__BIT((n) + 16)
#define	A733_PWM_PDZCR(n)	(0x60 + ((n) / 2) * 4)
#define	A733_PWM_DEAD_ZONE	__BIT(0)
#define	A733_PWM_PER		0x80
#define	A733_PWM_PGR(n)		(0x90 + (n) * 4)
#define	A733_PWM_GROUP_ENABLE	__BIT(16)
#define	A733_PWM_CCCER		0xa0
#define	A733_PWM_CER		0xc0
#define	A733_PWM_PCR(n)		(0x100 + (n) * 0x40)
#define	A733_PWM_PPR(n)		(0x104 + (n) * 0x40)
#define	A733_PWM_PRESCALE	__BITS(7, 0)
#define	A733_PWM_ACTIVE_HIGH	__BIT(8)
#define	A733_PWM_PULSE_MODE	__BIT(9)
#define	A733_PWM_PULSE_START	__BIT(10)
#define	A733_PWM_READY		__BIT(11)
#define	A733_PWM_PULSE_COUNT	__BITS(31, 16)
#define	A733_PWM_PERIOD		__BITS(31, 16)
#define	A733_PWM_DUTY		__BITS(15, 0)
#define	A733_PWM_READY_US	1000

struct a733_pwm_channel {
	struct pwm_controller pwm;
	struct pwm_config conf;
	u_int index;
	bool acquired;
	bool configured;
};

struct a733_pwm_softc {
	device_t sc_dev;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	kmutex_t sc_lock;
	u_int sc_rate;
	struct a733_pwm_channel sc_channel[A733_PWM_NCHANNELS];
};

#define	PWM_READ(sc, reg) \
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	PWM_WRITE(sc, reg, value) do { \
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (value)); \
	bus_space_barrier((sc)->sc_bst, (sc)->sc_bsh, (reg), 4, \
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE); \
} while (0)

static int
a733_pwm_wait(struct a733_pwm_softc *sc, u_int index)
{
	u_int n;

	for (n = 0; n < A733_PWM_READY_US; n++) {
		if ((PWM_READ(sc, A733_PWM_PCR(index)) & A733_PWM_READY) == 0)
			return 0;
		delay(1);
	}
	return ETIMEDOUT;
}

/* Return EBUSY without changing firmware-owned modes or sibling channels. */
static int
a733_pwm_owned(struct a733_pwm_softc *sc, u_int index)
{
	uint32_t value;
	u_int n;

	value = PWM_READ(sc, A733_PWM_CER) | PWM_READ(sc, A733_PWM_CCCER);
	if ((value & __BIT(index)) != 0 ||
	    (PWM_READ(sc, A733_PWM_PDZCR(index)) & A733_PWM_DEAD_ZONE) != 0)
		return EBUSY;
	for (n = 0; n < 4; n++) {
		value = PWM_READ(sc, A733_PWM_PGR(n));
		if ((value & A733_PWM_GROUP_ENABLE) != 0 &&
		    (value & __BIT(index)) != 0)
			return EBUSY;
	}
	return 0;
}

/* Round after division. Adding half the divisor could overflow uint64_t. */
static uint64_t
a733_pwm_round(uint64_t value, uint64_t divisor)
{
	return value / divisor + (value % divisor >= (divisor + 1) / 2);
}

static int
a733_pwm_encode(const struct pwm_config *conf, u_int rate, int fixed_div,
    uint32_t *divider, uint32_t *control, uint32_t *period,
    struct pwm_config *actual)
{
	uint64_t product, denom, cycles, duty, ns;
	u_int div, pre;

	if (rate == 0 || conf->period == 0 ||
	    conf->duty_cycle > conf->period ||
	    (conf->polarity != PWM_ACTIVE_HIGH && conf->polarity != PWM_ACTIVE_LOW))
		return EINVAL;
	if (fixed_div < -1 || fixed_div > 8)
		return EBUSY;
	product = (uint64_t)conf->period * rate;
	for (div = 0; div <= 8; div++) {
		if (fixed_div >= 0 && div != (u_int)fixed_div)
			continue;
		for (pre = 1; pre <= 256; pre++) {
			denom = UINT64_C(1000000000) * (1U << div) * pre;
			cycles = a733_pwm_round(product, denom);
			/* 65536 cannot encode a full active period. */
			if (cycles == 0 || cycles > UINT16_MAX)
				continue;
			ns = a733_pwm_round(cycles * denom, rate);
			if (ns == 0 || ns > UINT_MAX)
				continue;
			actual->period = (u_int)ns;
			duty = a733_pwm_round(cycles * conf->duty_cycle,
			    conf->period);
			actual->duty_cycle = (u_int)a733_pwm_round(duty * denom, rate);
			actual->polarity = conf->polarity;
			*divider = __SHIFTIN(div, A733_PWM_DIVIDER);
			*control = __SHIFTIN(pre - 1, A733_PWM_PRESCALE);
			if (conf->polarity == PWM_ACTIVE_HIGH)
				*control |= A733_PWM_ACTIVE_HIGH;
			*period = __SHIFTIN(cycles - 1, A733_PWM_PERIOD) |
			    __SHIFTIN(duty, A733_PWM_DUTY);
			return 0;
		}
	}
	return ERANGE;
}

static int
a733_pwm_set_config(pwm_tag_t pwm, const struct pwm_config *conf)
{
	struct a733_pwm_softc * const sc = device_private(pwm->pwm_dev);
	struct a733_pwm_channel * const channel = pwm->pwm_priv;
	struct pwm_config actual;
	uint32_t pair, gate, enabled, ctrl, newpair, newctrl, period, div, ctl;
	u_int index;
	bool restart;
	int error, fixed_div;

	if (channel == NULL || channel->index >= A733_PWM_NCHANNELS)
		return EINVAL;
	index = channel->index;
	mutex_enter(&sc->sc_lock);
	error = a733_pwm_owned(sc, index);
	if (error != 0)
		goto out;
	pair = PWM_READ(sc, A733_PWM_PCCR(index));
	gate = PWM_READ(sc, A733_PWM_PCGR);
	enabled = PWM_READ(sc, A733_PWM_PER);
	fixed_div = -1;
	/* A configured sibling retains its clock even while disabled. */
	if (sc->sc_channel[index ^ 1U].configured ||
	    ((enabled | PWM_READ(sc, A733_PWM_CER) |
	    PWM_READ(sc, A733_PWM_CCCER)) & __BIT(index ^ 1U)) != 0 ||
	    (gate & (A733_PWM_GATE(index ^ 1U) |
	    A733_PWM_BYPASS(index ^ 1U))) == (A733_PWM_GATE(index ^ 1U) |
	    A733_PWM_BYPASS(index ^ 1U))) {
		if ((pair & A733_PWM_SOURCE) != 0) {
			error = EBUSY;
			goto out;
		}
		fixed_div = __SHIFTOUT(pair, A733_PWM_DIVIDER);
	}
	error = a733_pwm_encode(conf, sc->sc_rate, fixed_div,
	    &div, &ctl, &period, &actual);
	if (error != 0)
		goto out;
	error = a733_pwm_wait(sc, index);
	if (error != 0)
		goto out;
	ctrl = PWM_READ(sc, A733_PWM_PCR(index));
	newpair = (pair & ~(A733_PWM_SOURCE | A733_PWM_DIVIDER)) | div;
	newctrl = (ctrl & ~(A733_PWM_PRESCALE | A733_PWM_ACTIVE_HIGH |
	    A733_PWM_PULSE_MODE | A733_PWM_PULSE_START |
	    A733_PWM_PULSE_COUNT | A733_PWM_READY)) | ctl;
	restart = (enabled & __BIT(index)) != 0 &&
	    (newpair != pair || newctrl != ctrl ||
	    (gate & A733_PWM_BYPASS(index)) != 0);
	channel->configured = false;
	if (restart) {
		PWM_WRITE(sc, A733_PWM_PER, enabled & ~__BIT(index));
		if ((PWM_READ(sc, A733_PWM_PER) & __BIT(index)) != 0) {
			error = EIO;
			goto out;
		}
	}
	if (newpair != pair)
		PWM_WRITE(sc, A733_PWM_PCCR(index), newpair);
	if (newctrl != ctrl)
		PWM_WRITE(sc, A733_PWM_PCR(index), newctrl);
	/* Open this channel's clock, including when firmware left it gated. */
	gate = (gate & ~A733_PWM_BYPASS(index)) | A733_PWM_GATE(index);
	PWM_WRITE(sc, A733_PWM_PCGR, gate);
	PWM_WRITE(sc, A733_PWM_PPR(index), period);
	error = a733_pwm_wait(sc, index);
	if (error != 0)
		goto out;
	if (PWM_READ(sc, A733_PWM_PCCR(index)) != newpair ||
	    (PWM_READ(sc, A733_PWM_PCR(index)) & ~A733_PWM_READY) != newctrl ||
	    PWM_READ(sc, A733_PWM_PPR(index)) != period ||
	    PWM_READ(sc, A733_PWM_PCGR) != gate) {
		error = EIO;
		goto out;
	}
	if (restart) {
		PWM_WRITE(sc, A733_PWM_PER, enabled);
		if (PWM_READ(sc, A733_PWM_PER) != enabled) {
			error = EIO;
			goto out;
		}
	}
	channel->conf = actual;
	channel->configured = true;
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
a733_pwm_enable(pwm_tag_t pwm, bool enable)
{
	struct a733_pwm_softc * const sc = device_private(pwm->pwm_dev);
	const struct a733_pwm_channel * const channel = pwm->pwm_priv;
	uint32_t value;
	int error;

	if (channel == NULL || channel->index >= A733_PWM_NCHANNELS)
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	error = a733_pwm_owned(sc, channel->index);
	if (error != 0)
		goto out;
	if (enable && !channel->configured) {
		error = EINVAL;
		goto out;
	}
	value = PWM_READ(sc, A733_PWM_PER);
	if (enable)
		value |= __BIT(channel->index);
	else
		value &= ~__BIT(channel->index);
	PWM_WRITE(sc, A733_PWM_PER, value);
	error = PWM_READ(sc, A733_PWM_PER) == value ? 0 : EIO;
	/* Keep the counter clock available for the next configuration write. */
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
a733_pwm_get_config(pwm_tag_t pwm, struct pwm_config *conf)
{
	struct a733_pwm_softc * const sc = device_private(pwm->pwm_dev);
	const struct a733_pwm_channel * const channel = pwm->pwm_priv;

	if (channel == NULL || channel->index >= A733_PWM_NCHANNELS)
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	*conf = channel->conf;
	mutex_exit(&sc->sc_lock);
	return 0;
}

static pwm_tag_t
a733_pwm_get_tag(device_t dev, const void *data, size_t len)
{
	struct a733_pwm_softc * const sc = device_private(dev);
	struct a733_pwm_channel *channel;
	struct pwm_config conf, actual;
	uint32_t cells[4], div, ctl, period;
	u_int index, flags;

	if (len != sizeof(cells))
		return NULL;
	memcpy(cells, data, sizeof(cells));
	index = be32toh(cells[1]);
	flags = be32toh(cells[3]);
	if (index >= A733_PWM_NCHANNELS || flags > 1)
		return NULL;
	conf.period = be32toh(cells[2]);
	conf.duty_cycle = 0;
	conf.polarity = flags == 0 ? PWM_ACTIVE_HIGH : PWM_ACTIVE_LOW;
	if (a733_pwm_encode(&conf, sc->sc_rate, -1, &div, &ctl, &period,
	    &actual) != 0)
		return NULL;
	mutex_enter(&sc->sc_lock);
	channel = &sc->sc_channel[index];
	if (channel->acquired) {
		mutex_exit(&sc->sc_lock);
		return NULL;
	}
	channel->index = index;
	channel->conf = conf;
	channel->acquired = true;
	channel->pwm.pwm_dev = dev;
	channel->pwm.pwm_priv = channel;
	channel->pwm.pwm_enable = a733_pwm_enable;
	channel->pwm.pwm_get_config = a733_pwm_get_config;
	channel->pwm.pwm_set_config = a733_pwm_set_config;
	mutex_exit(&sc->sc_lock);
	return &channel->pwm;
}
/* END PWM CONTRACT */

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-pwm" },
	DEVICE_COMPAT_EOL
};

static const struct fdtbus_pwm_controller_func a733_pwm_funcs = {
	.get_tag = a733_pwm_get_tag
};

static int
a733_pwm_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
a733_pwm_attach(device_t parent, device_t self, void *aux)
{
	struct a733_pwm_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	struct clk *bus, *hosc;
	struct fdtbus_reset *reset;
	bus_addr_t addr;
	bus_size_t size;

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    size < A733_PWM_SIZE) {
		aprint_error(": invalid registers\n");
		return;
	}
	bus = fdtbus_clock_get(phandle, "bus");
	hosc = fdtbus_clock_get(phandle, "hosc");
	reset = fdtbus_reset_get_index(phandle, 0);
	if (bus == NULL || hosc == NULL || reset == NULL ||
	    clk_get_rate(hosc) == 0) {
		aprint_error(": missing clocks or reset\n");
		return;
	}
	/* Deassert only. A reset pulse would disrupt firmware-owned channels. */
	if (clk_enable(bus) != 0 || clk_enable(hosc) != 0 ||
	    fdtbus_reset_deassert(reset) != 0) {
		aprint_error(": cannot enable controller resources\n");
		return;
	}
	sc->sc_dev = self;
	sc->sc_rate = clk_get_rate(hosc);
	sc->sc_bst = faa->faa_bst;
	if (bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": cannot map registers\n");
		return;
	}
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	aprint_naive("\n");
	aprint_normal(": A733 PWM, %u channels, HOSC %u Hz\n",
	    A733_PWM_NCHANNELS, sc->sc_rate);
	fdtbus_register_pwm_controller(self, phandle, &a733_pwm_funcs);
}

CFATTACH_DECL_NEW(sun60i_a733_pwm, sizeof(struct a733_pwm_softc),
    a733_pwm_match, a733_pwm_attach, NULL, NULL);
