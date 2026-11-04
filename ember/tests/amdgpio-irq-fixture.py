# SPDX-License-Identifier: BSD-2-Clause
"""AI-assisted register model for actual AMD GPIO routines; no hardware access."""

from pathlib import Path
import re
import sys

src = Path(sys.argv[1]).read_text()


def grab(name):
    pattern = r"\nstatic\s+(?:void\s*\*|void|int)\s*\n" + name + r"\s*\("
    match = re.search(pattern, src)
    assert match, name
    start = match.start() + 1
    begin = src.index("{", match.end())
    level = 0
    for index in range(begin, len(src)):
        level += (src[index] == "{") - (src[index] == "}")
        if level == 0:
            return src[start:index + 1]
    raise RuntimeError(name)


pre=src[:src.index('#include')] + r'''#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/queue.h>
#include "amdgpioreg.h"
#define GPIO_INTR_POS_EDGE 1
#define GPIO_INTR_NEG_EDGE 2
#define GPIO_INTR_DOUBLE_EDGE 4
#define GPIO_INTR_HIGH_LEVEL 8
#define GPIO_INTR_LOW_LEVEL 16
#define GPIO_INTR_EDGE_MASK 7
#define GPIO_INTR_LEVEL_MASK 24
#define IPL_VM 1
#define KM_SLEEP 0
#define __BIT(x) (1ULL << (x))
#define __BITS(a, b) ((1ULL << ((b) + 1)) - 1)
#define KASSERT(x) assert(x)
#define kmem_alloc(n, f) calloc(1, n)
#define kmem_free(p, n) free(p)
#define mutex_enter(x) ((void)0)
#define mutex_exit(x) ((void)0)
#define device_printf(...) ((void)0)
struct amdgpio_config {
	unsigned int num_pins;
};
struct amdgpio_intr_handler {
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_pin;
	LIST_ENTRY(amdgpio_intr_handler) ih_list;
};
struct amdgpio_softc {
	int sc_dev;
	const struct amdgpio_config *sc_config;
	LIST_HEAD(, amdgpio_intr_handler) sc_intrs;
	int sc_lock;
};
static uint32_t regs[256];
static uint32_t readreg(unsigned int addr) { return regs[addr / 4]; }
static void writereg(unsigned int addr, uint32_t val)
{
	if (addr == AMDGPIO_PIN_REG(6)) {
		uint32_t old = regs[addr / 4];
		regs[addr / 4] = (val & ~AMDGPIO_CONF_INTR_STATUS) |
		                 (old & AMDGPIO_CONF_INTR_STATUS &
		                  ~(val & AMDGPIO_CONF_INTR_STATUS));
	} else
		regs[addr / 4] = val;
}
#define RD4(sc, reg) readreg(reg)
#define WR4(sc, reg, val) writereg(reg, val)
'''
parts = [grab(name) for name in (
    "amdgpio_intr_establish", "amdgpio_intr_mask", "amdgpio_intr_unmask",
    "amdgpio_intr",
)]
post=r'''
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);   \
			exit(1);                                               \
		}                                                              \
	} while (0)
static struct amdgpio_softc *gsc;
static struct amdgpio_intr_handler *gh;
static int callbacks, defer;
static int callback(void *unused)
{
	(void)unused;
	callbacks++;
	if (defer)
		amdgpio_intr_mask(gsc, gh);
	return 1;
}
static void setup(struct amdgpio_softc *sc, struct amdgpio_config *cfg,
                  int mode, uint32_t expected)
{
	uint32_t x;
	*sc = (struct amdgpio_softc){0};
	*cfg = (struct amdgpio_config){184};
	sc->sc_config = cfg;
	LIST_INIT(&sc->sc_intrs);
	regs[AMDGPIO_PIN_REG(6) / 4] = AMDGPIO_CONF_LEVEL | AMDGPIO_CONF_MASK |
	                               AMDGPIO_CONF_GPIOTXSTATE_EN;
	gh = amdgpio_intr_establish(sc, 6, IPL_VM, mode, callback, NULL);
	CHECK(gh != NULL);
	gsc = sc;
	x = regs[AMDGPIO_PIN_REG(6) / 4];
	CHECK((x & (AMDGPIO_CONF_LEVEL | AMDGPIO_CONF_MASK)) == expected);
	CHECK(!(x & AMDGPIO_CONF_GPIOTXSTATE_EN));
	CHECK((x & (AMDGPIO_CONF_INTR_EN | AMDGPIO_CONF_INTR_MASK_EN)) ==
	      (AMDGPIO_CONF_INTR_EN | AMDGPIO_CONF_INTR_MASK_EN));
}
static void fire(struct amdgpio_softc *sc)
{
	regs[AMDGPIO_INTR_STATUS(0) / 4] = 1u << (6 / 4);
	regs[AMDGPIO_PIN_REG(6) / 4] |= AMDGPIO_CONF_INTR_STATUS;
	CHECK(amdgpio_intr(sc) == 1);
	CHECK(!(regs[AMDGPIO_PIN_REG(6) / 4] & AMDGPIO_CONF_INTR_STATUS));
	CHECK(regs[AMDGPIO_PIN_REG(6) / 4] & AMDGPIO_CONF_INTR_EN);
}
int main(void)
{
	struct amdgpio_softc sc;
	struct amdgpio_config cfg;
	int modes[] = {1, 2, 4, 8, 16};
	uint32_t vals[] = {0, AMDGPIO_CONF_ACTLO, AMDGPIO_CONF_ACTBOTH,
	                   AMDGPIO_CONF_LEVEL,
	                   AMDGPIO_CONF_LEVEL | AMDGPIO_CONF_ACTLO};
	for (int i = 0; i < 5; i++) {
		setup(&sc, &cfg, modes[i], vals[i]);
		free(gh);
	}
	setup(&sc, &cfg, GPIO_INTR_LOW_LEVEL,
	      AMDGPIO_CONF_LEVEL | AMDGPIO_CONF_ACTLO);
	defer = 1;
	fire(&sc);
	CHECK(!(regs[AMDGPIO_PIN_REG(6) / 4] & AMDGPIO_CONF_INTR_MASK_EN));
	amdgpio_intr_unmask(&sc, gh);
	CHECK(regs[AMDGPIO_PIN_REG(6) / 4] & AMDGPIO_CONF_INTR_MASK_EN);
	defer = 0;
	fire(&sc);
	CHECK(callbacks == 2);
	free(gh);
	setup(&sc, &cfg, GPIO_INTR_POS_EDGE, 0);
	fire(&sc);
	fire(&sc);
	CHECK(callbacks == 4);
	free(gh);
	puts("PASS modes, deferred mask, repeated level and edge");
	return 0;
}
'''
Path(sys.argv[2]).write_text(pre + '\n\n'.join(parts) + post)
