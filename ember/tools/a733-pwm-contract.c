/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute the real A733 PWM functions with controlled register providers. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <arpa/inet.h>

#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#ifndef __BITS
#define __BITS(hi, lo) ((UINT32_MAX >> (31 - (hi))) & (UINT32_MAX << (lo)))
#endif
#ifndef __SHIFTOUT
#define __SHIFTOUT(v, mask) (((v) & (mask)) >> __builtin_ctz(mask))
#endif
#ifndef __SHIFTIN
#define __SHIFTIN(v, mask) (((uint32_t)(v) << __builtin_ctz(mask)) & (mask))
#endif
#ifndef be32toh
#define be32toh(v) ntohl(v)
#endif
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define device_private(dev) (dev)

typedef unsigned int u_int;
typedef void *device_t;
typedef unsigned int bus_space_handle_t;
typedef int kmutex_t;
struct mmio {
	uint32_t registers[0x400 / 4];
	unsigned int writes[0x400 / 4];
	int drop_offset;
	unsigned int drop_after_write;
	int busy_after_period;
	unsigned int delays;
};
typedef struct mmio *bus_space_tag_t;
#include "pwmvar.h"

static unsigned int checks;
static struct mmio *active_mmio;
#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, unsigned int offset)
{
	assert(handle == 0 && offset < 0x400 && offset % 4 == 0);
	return tag->registers[offset / 4];
}

static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int offset, uint32_t value)
{
	assert(handle == 0 && offset < 0x400 && offset % 4 == 0);
	tag->writes[offset / 4]++;
	if ((int)offset != tag->drop_offset ||
	    tag->writes[offset / 4] < tag->drop_after_write)
		tag->registers[offset / 4] = value;
	if (tag->busy_after_period && offset >= 0x100 && offset % 0x40 == 4)
		tag->registers[(offset - 4) / 4] |= __BIT(11);
}

static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int offset, unsigned int size, int flags)
{
	assert(tag != NULL && handle == 0 && offset < 0x400 && size == 4);
	assert(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE));
}

static void
mutex_enter(kmutex_t *lock)
{
	assert(*lock == 0);
	*lock = 1;
}

static void
mutex_exit(kmutex_t *lock)
{
	assert(*lock == 1);
	*lock = 0;
}

static void
delay(unsigned int us)
{
	assert(us == 1 && active_mmio != NULL);
	active_mmio->delays++;
}

#include "a733-pwm-control.h"

static void
reset(struct a733_pwm_softc *sc, struct mmio *io)
{
	memset(sc, 0, sizeof(*sc));
	memset(io, 0, sizeof(*io));
	io->drop_offset = -1;
	sc->sc_bst = io;
	sc->sc_rate = 24000000;
	active_mmio = io;
}

static pwm_tag_t
acquire(struct a733_pwm_softc *sc, unsigned int index,
    unsigned int period, unsigned int flags)
{
	uint32_t cells[4] = { htonl(1), htonl(index), htonl(period), htonl(flags) };

	return a733_pwm_get_tag(sc, cells, sizeof(cells));
}

static void
check_encoding(void)
{
	struct pwm_config c = { 25000, 12500, PWM_ACTIVE_HIGH }, actual;
	uint32_t div, ctl, period;
	unsigned int rate, duty;

	for (rate = 24000000; rate <= 26000000; rate += 2000000) {
		for (duty = 0; duty <= 25000; duty += 12500) {
			c.duty_cycle = duty;
			CHECK(a733_pwm_encode(&c, rate, -1, &div, &ctl,
			    &period, &actual) == 0);
			CHECK(actual.period == 25000 && actual.duty_cycle == duty);
			CHECK(__SHIFTOUT(period, A733_PWM_PERIOD) + 1 == rate / 40000);
			CHECK(__SHIFTOUT(period, A733_PWM_DUTY) == duty * (rate / 1000000) / 1000);
			CHECK(div == 0 && ctl == A733_PWM_ACTIVE_HIGH);
		}
	}
	c.period = 0;
	CHECK(a733_pwm_encode(&c, 24000000, -1, &div, &ctl, &period, &actual) == EINVAL);
	c.period = 25000;
	c.duty_cycle = 25001;
	CHECK(a733_pwm_encode(&c, 24000000, -1, &div, &ctl, &period, &actual) == EINVAL);
	c.duty_cycle = 0;
	c.polarity = (enum pwm_polarity)2;
	CHECK(a733_pwm_encode(&c, 24000000, -1, &div, &ctl, &period, &actual) == EINVAL);
	c.polarity = PWM_ACTIVE_LOW;
	CHECK(a733_pwm_encode(&c, 0, -1, &div, &ctl, &period, &actual) == EINVAL);
	CHECK(a733_pwm_encode(&c, 24000000, 9, &div, &ctl, &period, &actual) == EBUSY);
	c.period = 1;
	CHECK(a733_pwm_encode(&c, 24000000, -1, &div, &ctl, &period, &actual) == ERANGE);
	c.period = UINT_MAX;
	c.duty_cycle = UINT_MAX;
	CHECK(a733_pwm_encode(&c, UINT_MAX, -1, &div, &ctl, &period, &actual) == ERANGE);
	CHECK(a733_pwm_encode(&c, 24000000, -1, &div, &ctl, &period, &actual) == 0);
	CHECK(actual.duty_cycle == actual.period && actual.period > 4000000000U);
	/* A whole active period never wraps the sixteen-bit active count. */
	c.period = 2730667;
	c.duty_cycle = c.period;
	CHECK(a733_pwm_encode(&c, 24000000, -1, &div, &ctl, &period, &actual) == 0);
	CHECK(__SHIFTOUT(period, A733_PWM_DUTY) == __SHIFTOUT(period, A733_PWM_PERIOD) + 1);
	CHECK(__SHIFTOUT(ctl, A733_PWM_PRESCALE) == 1);
	CHECK(a733_pwm_round(UINT64_MAX, 2) == (UINT64_C(1) << 63));
}

static void
check_api(void)
{
	struct a733_pwm_softc sc;
	struct mmio io;
	struct pwm_config c = { 25000, 12500, PWM_ACTIVE_HIGH }, actual;
	uint32_t cells[4] = { 0 };
	pwm_tag_t pwm;
	unsigned int n;

	reset(&sc, &io);
	CHECK(a733_pwm_get_tag(&sc, cells, sizeof(cells) - 1) == NULL);
	CHECK(acquire(&sc, 10, 25000, 0) == NULL);
	CHECK(acquire(&sc, UINT_MAX, 25000, 0) == NULL);
	CHECK(acquire(&sc, 5, 0, 0) == NULL);
	CHECK(acquire(&sc, 5, 25000, 2) == NULL);
	for (n = 0; n < A733_PWM_NCHANNELS; n++) {
		pwm = acquire(&sc, n, 25000, n & 1);
		CHECK(pwm != NULL);
		CHECK(acquire(&sc, n, 25000, 0) == NULL);
		CHECK(pwm->pwm_get_config(pwm, &actual) == 0);
		CHECK(actual.period == 25000 && actual.duty_cycle == 0);
		CHECK(actual.polarity == ((n & 1) ? PWM_ACTIVE_LOW : PWM_ACTIVE_HIGH));
		CHECK(pwm->pwm_enable(pwm, true) == EINVAL);
		CHECK(pwm->pwm_set_config(pwm, &c) == 0);
		CHECK(pwm->pwm_enable(pwm, true) == 0);
		CHECK(io.registers[A733_PWM_PPR(n) / 4] == (599U << 16 | 300));
		CHECK(pwm->pwm_enable(pwm, false) == 0);
	}
	CHECK(sc.sc_lock == 0);
	pwm = &sc.sc_channel[5].pwm;
	sc.sc_channel[5].index = 10;
	CHECK(pwm->pwm_set_config(pwm, &c) == EINVAL);
	CHECK(pwm->pwm_get_config(pwm, &actual) == EINVAL);
	CHECK(pwm->pwm_enable(pwm, false) == EINVAL);
	pwm->pwm_priv = NULL;
	CHECK(pwm->pwm_set_config(pwm, &c) == EINVAL);
	CHECK(pwm->pwm_get_config(pwm, &actual) == EINVAL);
	CHECK(pwm->pwm_enable(pwm, false) == EINVAL);
}

static void
check_preservation(void)
{
	struct a733_pwm_softc sc;
	struct mmio io;
	struct pwm_config c = { 25000, 12500, PWM_ACTIVE_HIGH };
	pwm_tag_t pwm;
	uint32_t pair, gate;
	unsigned int before;

	reset(&sc, &io);
	pwm = acquire(&sc, 5, 25000, 0);
	io.registers[A733_PWM_PCCR(5) / 4] = __BIT(25) | 1;
	io.registers[A733_PWM_PCGR / 4] = __BIT(4) | __BIT(20);
	io.registers[A733_PWM_PER / 4] = __BIT(4);
	io.registers[A733_PWM_PCR(4) / 4] = 0x12345678;
	io.registers[A733_PWM_PPR(4) / 4] = 0x87654321;
	pair = io.registers[A733_PWM_PCCR(5) / 4];
	gate = io.registers[A733_PWM_PCGR / 4];
	CHECK(a733_pwm_set_config(pwm, &c) == 0);
	CHECK(io.registers[A733_PWM_PCCR(5) / 4] == pair);
	CHECK(io.registers[A733_PWM_PCGR / 4] == (gate | __BIT(5)));
	CHECK(io.registers[A733_PWM_PER / 4] == __BIT(4));
	CHECK(io.registers[A733_PWM_PCR(4) / 4] == 0x12345678);
	CHECK(io.registers[A733_PWM_PPR(4) / 4] == 0x87654321);
	CHECK(a733_pwm_enable(pwm, true) == 0);
	before = io.writes[A733_PWM_PER / 4];
	c.duty_cycle = 18750;
	CHECK(a733_pwm_set_config(pwm, &c) == 0);
	CHECK(io.writes[A733_PWM_PER / 4] == before);
	c.duty_cycle = 0;
	CHECK(a733_pwm_set_config(pwm, &c) == 0);
	CHECK(__SHIFTOUT(io.registers[A733_PWM_PPR(5) / 4], A733_PWM_DUTY) == 0);
	c.duty_cycle = c.period;
	CHECK(a733_pwm_set_config(pwm, &c) == 0);
	CHECK(__SHIFTOUT(io.registers[A733_PWM_PPR(5) / 4], A733_PWM_DUTY) == 300);
	CHECK(io.writes[A733_PWM_PER / 4] == before);
	CHECK(io.registers[A733_PWM_PER / 4] == (__BIT(4) | __BIT(5)));
	c.polarity = PWM_ACTIVE_LOW;
	CHECK(a733_pwm_set_config(pwm, &c) == 0);
	CHECK(io.writes[A733_PWM_PER / 4] == before + 2);
	CHECK(io.registers[A733_PWM_PER / 4] == (__BIT(4) | __BIT(5)));
	CHECK(a733_pwm_enable(pwm, false) == 0);
	CHECK(io.registers[A733_PWM_PER / 4] == __BIT(4));
	/* Unsafe source changes and invalid firmware divider values fail. */
	io.registers[A733_PWM_PCCR(5) / 4] |= __BIT(7);
	CHECK(a733_pwm_set_config(pwm, &c) == EBUSY);
	io.registers[A733_PWM_PCCR(5) / 4] = 15;
	CHECK(a733_pwm_set_config(pwm, &c) == EBUSY);
	io.registers[A733_PWM_PER / 4] = 0;
	io.registers[A733_PWM_CER / 4] = __BIT(4);
	io.registers[A733_PWM_PCCR(5) / 4] = __BIT(7);
	CHECK(a733_pwm_set_config(pwm, &c) == EBUSY);
	CHECK(io.registers[A733_PWM_PCCR(5) / 4] == __BIT(7));
	io.registers[A733_PWM_CER / 4] = 0;
	io.registers[A733_PWM_PCGR / 4] = __BIT(4) | __BIT(20);
	CHECK(a733_pwm_set_config(pwm, &c) == EBUSY);
	CHECK(io.registers[A733_PWM_PCCR(5) / 4] == __BIT(7));
}

static void
check_disabled_sibling(void)
{
	struct a733_pwm_softc sc;
	struct mmio io;
	struct pwm_config slow = { 1000000000, 500000000, PWM_ACTIVE_HIGH };
	struct pwm_config fast = { 25000, 12500, PWM_ACTIVE_HIGH }, actual;
	pwm_tag_t a, b;
	uint32_t pair, control, period;
	uint64_t ticks, divider, prescale, ns;

	reset(&sc, &io);
	a = acquire(&sc, 4, slow.period, 0);
	b = acquire(&sc, 5, fast.period, 0);
	CHECK(a != NULL && b != NULL);
	CHECK(a733_pwm_set_config(a, &slow) == 0);
	CHECK(a733_pwm_get_config(a, &actual) == 0);
	pair = io.registers[A733_PWM_PCCR(4) / 4];
	control = io.registers[A733_PWM_PCR(4) / 4];
	period = io.registers[A733_PWM_PPR(4) / 4];
	CHECK(__SHIFTOUT(pair, A733_PWM_DIVIDER) > 0);
	CHECK(io.registers[A733_PWM_PER / 4] == 0);
	/* B must retain the common divider needed by disabled A. */
	CHECK(a733_pwm_set_config(b, &fast) == 0);
	CHECK(io.registers[A733_PWM_PCCR(4) / 4] == pair);
	CHECK(io.registers[A733_PWM_PCR(4) / 4] == control);
	CHECK(io.registers[A733_PWM_PPR(4) / 4] == period);
	CHECK(a733_pwm_enable(a, true) == 0);
	ticks = __SHIFTOUT(io.registers[A733_PWM_PPR(4) / 4],
	    A733_PWM_PERIOD) + 1;
	divider = UINT64_C(1) << __SHIFTOUT(
	    io.registers[A733_PWM_PCCR(4) / 4], A733_PWM_DIVIDER);
	prescale = __SHIFTOUT(io.registers[A733_PWM_PCR(4) / 4],
	    A733_PWM_PRESCALE) + 1;
	ns = (ticks * divider * prescale * UINT64_C(1000000000) +
	    sc.sc_rate / 2) / sc.sc_rate;
	CHECK(ns == actual.period);
	CHECK(a733_pwm_enable(a, false) == 0);
	/* The inverse order cannot represent B without disturbing A. */
	reset(&sc, &io);
	a = acquire(&sc, 4, fast.period, 0);
	b = acquire(&sc, 5, slow.period, 0);
	CHECK(a != NULL && b != NULL);
	CHECK(a733_pwm_set_config(a, &fast) == 0);
	pair = io.registers[A733_PWM_PCCR(4) / 4];
	period = io.registers[A733_PWM_PPR(4) / 4];
	CHECK(a733_pwm_set_config(b, &slow) == ERANGE);
	CHECK(io.registers[A733_PWM_PCCR(4) / 4] == pair);
	CHECK(io.registers[A733_PWM_PPR(4) / 4] == period);
	CHECK(sc.sc_channel[4].configured && !sc.sc_channel[5].configured);
	CHECK(a733_pwm_enable(a, true) == 0);
	CHECK(io.registers[A733_PWM_PER / 4] == __BIT(4));
}

static void
check_failures(void)
{
	struct a733_pwm_softc sc;
	struct mmio io;
	struct pwm_config c = { 25000, 12500, PWM_ACTIVE_HIGH };
	pwm_tag_t pwm;
	const unsigned int modes[] = { A733_PWM_CER, A733_PWM_CCCER,
	    A733_PWM_PDZCR(5), A733_PWM_PGR(0), A733_PWM_PGR(3) };
	const unsigned int dropped[] = { A733_PWM_PCCR(5), A733_PWM_PCR(5),
	    A733_PWM_PCGR, A733_PWM_PPR(5) };
	unsigned int n;

	for (n = 0; n < sizeof(modes) / sizeof(modes[0]); n++) {
		reset(&sc, &io);
		pwm = acquire(&sc, 5, 25000, 0);
		io.registers[modes[n] / 4] = n == 2 ? 1 : __BIT(5) | __BIT(16);
		CHECK(a733_pwm_set_config(pwm, &c) == EBUSY);
		CHECK(a733_pwm_enable(pwm, false) == EBUSY);
		CHECK(io.writes[A733_PWM_PPR(5) / 4] == 0);
		CHECK(sc.sc_lock == 0);
	}
	for (n = 0; n < sizeof(dropped) / sizeof(dropped[0]); n++) {
		reset(&sc, &io);
		pwm = acquire(&sc, 5, 25000, 0);
		io.registers[A733_PWM_PCCR(5) / 4] = __BIT(7);
		io.drop_offset = (int)dropped[n];
		CHECK(a733_pwm_set_config(pwm, &c) == EIO);
		CHECK(!sc.sc_channel[5].configured);
		CHECK(a733_pwm_enable(pwm, true) == EINVAL);
		CHECK(sc.sc_lock == 0);
	}
	reset(&sc, &io);
	pwm = acquire(&sc, 5, 25000, 0);
	io.registers[A733_PWM_PCR(5) / 4] = A733_PWM_READY;
	CHECK(a733_pwm_set_config(pwm, &c) == ETIMEDOUT);
	CHECK(io.delays == A733_PWM_READY_US);
	CHECK(io.writes[A733_PWM_PPR(5) / 4] == 0);
	io.registers[A733_PWM_PCR(5) / 4] = 0;
	io.busy_after_period = 1;
	CHECK(a733_pwm_set_config(pwm, &c) == ETIMEDOUT);
	CHECK(io.delays == 2 * A733_PWM_READY_US);
	io.busy_after_period = 0;
	io.registers[A733_PWM_PCR(5) / 4] &= ~A733_PWM_READY;
	CHECK(a733_pwm_set_config(pwm, &c) == 0);
	io.drop_offset = A733_PWM_PER;
	CHECK(a733_pwm_enable(pwm, true) == EIO);
	io.drop_offset = -1;
	CHECK(a733_pwm_enable(pwm, true) == 0);
	c.polarity = PWM_ACTIVE_LOW;
	io.drop_offset = A733_PWM_PER;
	CHECK(a733_pwm_set_config(pwm, &c) == EIO);
	CHECK(io.registers[A733_PWM_PER / 4] == __BIT(5));
	io.drop_after_write = io.writes[A733_PWM_PER / 4] + 2;
	CHECK(a733_pwm_set_config(pwm, &c) == EIO);
	CHECK(io.registers[A733_PWM_PER / 4] == 0);
	CHECK(!sc.sc_channel[5].configured);
	io.drop_offset = -1;
	CHECK(a733_pwm_set_config(pwm, &c) == 0);
	CHECK(a733_pwm_enable(pwm, true) == 0);
	CHECK(sc.sc_lock == 0);
}

int
main(void)
{
	check_encoding();
	check_api();
	check_preservation();
	check_disabled_sibling();
	check_failures();
	printf("A733 PWM production functions: %u checks passed\n", checks);
	return 0;
}
