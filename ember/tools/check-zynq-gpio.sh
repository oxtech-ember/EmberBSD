#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD source-derived Zynq-7000 GPIO bank mapping regression.
# Exercise the driver's actual mapping, read, write, direction and FDT acquire
# code against an in-memory MMIO model. This does not test physical GPIOs.
set -eu
root=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
source=$root/sys/arch/arm/xilinx/zynq_gpio.c
testdir=$(mktemp -d "${TMPDIR:-/tmp}/zynq-gpio-check.XXXXXXXX")
trap 'rm -rf "$testdir"' EXIT HUP INT TERM
cat > "$testdir/check.c" <<'EOF'
#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define __BIT(n) (UINT32_C(1) << (n))
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define __SHIFTOUT(v, mask) (((v) & (mask)) >> __builtin_ctz(mask))
#define KASSERT(x) assert(x)
#define GPIO_PIN_INPUT 1
#define GPIO_PIN_OUTPUT 2
#define KM_SLEEP 0
#define device_t void *
#define device_private(d) (d)
#define kmem_zalloc(n, flags) calloc(1, (n))
static uint32_t from_be32(uint32_t value)
{
	const uint8_t *p = (const uint8_t *)&value;
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | p[3];
}
#undef be32toh
#define be32toh(v) from_be32(v)
static uint32_t to_be32(uint32_t value)
{
	uint8_t bytes[4] = { value >> 24, value >> 16, value >> 8, value };
	uint32_t result;
	memcpy(&result, bytes, sizeof(result));
	return result;
}
static void mutex_enter(int *lock) { assert(!*lock); *lock = 1; }
static void mutex_exit(int *lock) { assert(*lock); *lock = 0; }
#define mutex_owned(lock) (*(lock))
EOF
sed -n '/^#define[[:space:]]*ZYNQ_GPIO_NPINS/,/^#define[[:space:]]*OEN_BIT/p' "$source" >> "$testdir/check.c"
cat >> "$testdir/check.c" <<'EOF'
struct zynq_gpio_softc {
	int sc_lock;
	int sc_pins[ZYNQ_GPIO_NPINS];
	uint32_t regs[256];
};
struct zynq_gpio_pin {
	struct zynq_gpio_softc *pin_sc;
	u_int pin_nr;
	int pin_flags;
	bool pin_actlo;
};
static uint32_t last_reg, last_value;
static void write_reg(struct zynq_gpio_softc *sc, unsigned reg, uint32_t value)
{
	assert(reg % 4 == 0 && reg / 4 < __arraycount(sc->regs));
	sc->regs[reg / 4] = value;
	last_reg = reg;
	last_value = value;
}
#define RD4(sc, reg) ((sc)->regs[(reg) / 4])
#define WR4(sc, reg, value) write_reg((sc), (reg), (value))
EOF
for function in zynq_gpio_ctl zynq_gpio_acquire zynq_gpio_pin_read zynq_gpio_pin_write zynq_gpio_pin_ctl; do
	case "$function" in
	zynq_gpio_acquire) echo 'static void *' ;;
	zynq_gpio_pin_write|zynq_gpio_pin_ctl) echo 'static void' ;;
	*) echo 'static int' ;;
	esac >> "$testdir/check.c"
	sed -n "/^$function(/,/^}/p" "$source" >> "$testdir/check.c"
done
cat >> "$testdir/check.c" <<'EOF'
int main(void)
{
	static const unsigned widths[] = { 32, 22, 32, 32 };
	static const uint32_t invalid[] = {
		118, 127, 128, 255, 256, 310, 0x1000064, UINT32_MAX
	};
	struct zynq_gpio_softc sc = { 0 };
	unsigned bank, bit, pin = 0, i;

	for (bank = 0; bank < __arraycount(widths); bank++) {
		for (bit = 0; bit < widths[bank]; bit++, pin++) {
			uint32_t spec[3] = { 0, to_be32(pin), to_be32(1) };
			uint32_t mask = UINT32_C(1) << bit;
			uint32_t value = (UINT32_C(0xffff) ^
			    (UINT32_C(1) << (bit % 16))) << 16;
			uint32_t mask_reg = bank * 8 + (bit / 16) * 4;
			struct zynq_gpio_pin *gpin;

			zynq_gpio_pin_write(&sc, pin, 0);
			if (last_reg != mask_reg || last_value != value) {
				fprintf(stderr, "GPIO %u: mask reg/value %x/%x, expected %x/%x\n",
				    pin, last_reg, last_value, mask_reg, value);
				return 1;
			}
			zynq_gpio_pin_write(&sc, pin, 1);
			assert(last_reg == mask_reg);
			assert(last_value == (value | (UINT32_C(1) << (bit % 16))));
			memset(sc.regs, 0, sizeof(sc.regs));
			sc.regs[(0x60 + bank * 4) / 4] = mask;
			assert(zynq_gpio_pin_read(&sc, pin) == 1);
			sc.regs[(0x60 + bank * 4) / 4] = ~mask;
			assert(zynq_gpio_pin_read(&sc, pin) == 0);

			for (i = 0; i < __arraycount(sc.regs); i++)
				sc.regs[i] = UINT32_C(0xa55aa55a);
			zynq_gpio_pin_ctl(&sc, pin, GPIO_PIN_OUTPUT);
			for (i = 0; i < __arraycount(sc.regs); i++)
				assert(sc.regs[i] ==
				    ((i * 4 == 0x204 + bank * 0x40 ||
				      i * 4 == 0x208 + bank * 0x40) ?
				     (UINT32_C(0xa55aa55a) | mask) : UINT32_C(0xa55aa55a)));
			zynq_gpio_pin_ctl(&sc, pin, GPIO_PIN_INPUT);
			assert(sc.regs[(0x204 + bank * 0x40) / 4] == (UINT32_C(0xa55aa55a) & ~mask));
			assert(sc.regs[(0x208 + bank * 0x40) / 4] == (UINT32_C(0xa55aa55a) & ~mask));
			gpin = zynq_gpio_acquire(&sc, spec, sizeof(spec), GPIO_PIN_OUTPUT);
			assert(gpin != NULL && gpin->pin_nr == pin && gpin->pin_actlo);
			assert(gpin->pin_sc == &sc && gpin->pin_flags == GPIO_PIN_OUTPUT);
			free(gpin);
			spec[2] = 0;
			gpin = zynq_gpio_acquire(&sc, spec, sizeof(spec), GPIO_PIN_OUTPUT);
			assert(gpin != NULL && !gpin->pin_actlo);
			free(gpin);
		}
	}
	assert(pin == 118 && ZYNQ_GPIO_NPINS == 118);
	for (i = 0; i < __arraycount(invalid); i++) {
		uint32_t spec[3] = { 0, to_be32(invalid[i]), 0 };
		uint32_t before[256];
		memcpy(before, sc.regs, sizeof(before));
		assert(zynq_gpio_acquire(&sc, spec, sizeof(spec), GPIO_PIN_OUTPUT) == NULL);
		assert(memcmp(before, sc.regs, sizeof(before)) == 0);
		assert(sc.sc_lock == 0);
	}
	for (i = 0; i <= 16; i++) {
		uint32_t spec[3] = { 0, 0, 0 };
		uint32_t before[256];
		if (i == sizeof(spec))
			continue;
		memcpy(before, sc.regs, sizeof(before));
		assert(zynq_gpio_acquire(&sc, spec, i, GPIO_PIN_OUTPUT) == NULL);
		assert(memcmp(before, sc.regs, sizeof(before)) == 0);
		assert(sc.sc_lock == 0);
	}
	puts("PASS: 118 GPIO mappings, masked writes, readback, direction, both FDT polarities; invalid full-width pins and lengths rejected");
	return 0;
}
EOF
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-sign-compare -O2 "$testdir/check.c" -o "$testdir/check"
"$testdir/check"
