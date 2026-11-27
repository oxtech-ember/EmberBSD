/* SPDX-License-Identifier: BSD-2-Clause */
/* Run the early enable with a simulated GPIOv2 bank and external pull-up. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef unsigned int u_int;
struct gpio_chipset_tag {
	int (*gp_pin_read)(void *, int);
};
struct rk_gpio_softc {
	struct gpio_chipset_tag sc_gp;
	struct { int pin_caps, pin_state, pin_flags; } sc_pins[32];
};
typedef struct rk_gpio_softc *device_t;
#define	device_private(dev)	(dev)
#define	__arraycount(a)		(sizeof(a) / sizeof((a)[0]))
#define	__BIT(n)		(UINT32_C(1) << (n))
#define	__SHIFTOUT(v, mask)	(((v) & (mask)) != 0)
#define	KASSERT(x)		assert(x)
#define	GPIO_PIN_OUTPUT		2
#define	RD4(sc, reg)		((void)(sc), regs[(reg) / 4])

static uint32_t regs[32];
static u_int step;
static bool stuck_low, stuck_high;
static void delay(u_int);
static void gpiobus_pin_write(struct gpio_chipset_tag *, u_int, int);
static void gpiobus_pin_ctl(struct gpio_chipset_tag *, u_int, int);

#include "sequence.inc"

static void
gpiobus_pin_write(struct gpio_chipset_tag *gp, u_int pin, int val)
{
	(void)gp;
	assert(pin == 21 && (step == 0 || step == 3));
	assert(val == (step == 3));
	regs[GPIOV2_SWPORT_DR_REG(pin) / 4] = val ? GPIOV2_DATA_MASK(pin) : 0;
	step++;
}

static void
gpiobus_pin_ctl(struct gpio_chipset_tag *gp, u_int pin, int flags)
{
	(void)gp;
	assert(pin == 21 && flags == GPIO_PIN_OUTPUT);
	assert(step == 1 || step == 4);
	regs[GPIOV2_SWPORT_DDR_REG(pin) / 4] = GPIOV2_DATA_MASK(pin);
	regs[GPIOV2_EXT_PORT_REG / 4] =
	    (step == 4 && !stuck_low) || stuck_high ? __BIT(pin) : 0;
	step++;
}

static void
delay(u_int usec)
{
	assert(usec == 20000 && (step == 2 || step == 5));
	step++;
}

int
main(void)
{
	struct rk_gpio_softc bank = { 0 };
	bool preserved = false;

	assert(rk3506_radio_enable(&bank, 21, &preserved) == ENXIO);
	bank.sc_gp.gp_pin_read = rk_gpio_v2_pin_read;
	assert(rk3506_radio_enable(&bank, 32, &preserved) == ENXIO);
	assert(rk3506_radio_enable(&bank, 21, &preserved) == ENXIO);
	assert(step == 0);
	bank.sc_pins[21].pin_caps = GPIO_PIN_OUTPUT;
	/* The unclaimed pin reads high even if a previous latch holds one. */
	regs[0x04 / 4] = __BIT(5);
	regs[0x70 / 4] = __BIT(21);
	assert(rk3506_radio_enable(&bank, 21, &preserved) == 0);
	assert(!preserved && step == 6);
	assert(rk3506_radio_enable(&bank, 21, &preserved) == 0);
	assert(preserved && step == 6);
	/* An externally held low pad with a high output must stay enabled. */
	regs[0x70 / 4] = 0;
	assert(rk3506_radio_enable(&bank, 21, &preserved) == EIO);
	assert(step == 6);
	/* A driven low pin still needs both delays. Detect a stuck pad. */
	regs[0x04 / 4] = 0;
	step = 0;
	stuck_low = true;
	assert(rk3506_radio_enable(&bank, 21, &preserved) == EIO);
	assert(!preserved && step == 6);
	/* Restore high even if the pad never followed the low command. */
	regs[0x04 / 4] = 0;
	step = 0;
	stuck_low = false;
	stuck_high = true;
	assert(rk3506_radio_enable(&bank, 21, &preserved) == EIO);
	assert(!preserved && step == 6 && (regs[0x04 / 4] & __BIT(5)) != 0);
	puts("Lyra radio enable: pulse, settle, readback and retention passed");
	return 0;
}
