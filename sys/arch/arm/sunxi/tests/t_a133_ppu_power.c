/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD; native A133 power-domain behavior checks. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arm/sunxi/sun50i_a133_ppu_power.h>

struct hardware {
	uint32_t reg[16];
	unsigned writes, fail_write, domain_a_writes;
	unsigned reads, fail_read, acks;
	bool fail_readback, drop_state_write;
	uint32_t powered_a, powered_b;
};

static int
read_reg(void *cookie, unsigned reg, uint32_t *value)
{
	struct hardware *hw = cookie;

	assert(reg < sizeof(hw->reg) && reg % 4 == 0);
	if (++hw->reads == hw->fail_read ||
	    (hw->fail_readback && reg == A133_PPU_STATE && hw->writes > 5))
		return EIO;
	*value = hw->reg[reg / 4];
	return 0;
}

static int
write_reg(void *cookie, unsigned reg, uint32_t value)
{
	struct hardware *hw = cookie;

	assert(reg < sizeof(hw->reg) && reg % 4 == 0);
	if (++hw->writes == hw->fail_write)
		return EIO;
	if (reg == A133_PPU_PSWON) {
		hw->powered_a |= value & 0xff;
		hw->powered_b |= (value >> 16) & 0xff;
		if ((value & 0xff) != 0)
			hw->domain_a_writes++;
	} else if (reg == A133_PPU_PSWOFF) {
		/* Isolate a domain before removing its power. */
		if ((value & 0xff) != 0) {
			assert((hw->reg[A133_PPU_ISOLATION / 4] & 1) != 0);
			hw->domain_a_writes++;
		}
		if ((value & 0xff0000) != 0)
			assert((hw->reg[A133_PPU_ISOLATION / 4] & 2) != 0);
		hw->powered_a &= ~(value & 0xff);
		hw->powered_b &= ~((value >> 16) & 0xff);
	} else if (reg == A133_PPU_ISOLATION) {
		if ((value & 1) == 0)
			assert(hw->powered_a == 0xff);
		if ((value & 2) == 0)
			assert(hw->powered_b == 0xff);
	} else if (reg == A133_PPU_RSTN && (value & 1) != 0) {
		assert(hw->powered_a == 0xff && hw->powered_b == 0xff);
		assert((hw->reg[A133_PPU_ISOLATION / 4] & 3) == 0);
		assert((hw->reg[A133_PPU_CLOCK / 4] & 1) != 0);
	} else if (reg == A133_PPU_REQUEST) {
		hw->acks++;
		hw->reg[reg / 4] &= ~value;
		return 0;
	}
	if (reg == A133_PPU_STATE && hw->drop_state_write)
		return 0;
	hw->reg[reg / 4] = value;
	return 0;
}

static void
pause_us(void *cookie, unsigned us)
{

	(void)cookie;
	assert(us > 0 && us <= 10);
}

static struct a133_ppu_power
controller(struct hardware *hw, unsigned revision)
{
	struct a133_ppu_power power = {
		.io = { hw, read_reg, write_reg, pause_us },
		.revision = revision,
	};

	return power;
}

static void
off(struct hardware *hw)
{

	memset(hw, 0, sizeof(*hw));
	hw->reg[A133_PPU_ISOLATION / 4] = 3;
	hw->reg[A133_PPU_STATE / 4] = A133_PPU_OFF;
	hw->reg[A133_PPU_CONTROL / 4] = 1;
}

int
main(void)
{
	struct hardware hw;
	struct a133_ppu_power power;
	unsigned writes, fault, protected[] = { 0, 3, 4, 5 };
	bool enabled, handled, change;

	off(&hw);
	power = controller(&hw, 6);
	{
		unsigned state = 99, reads = hw.reads;

		assert(a133_ppu_power_observe(&power, &state) == 0);
		assert(state == A133_PPU_OFF && hw.reads == reads + 5);
		hw.reg[A133_PPU_RSTN / 4] = 1;
		hw.reg[A133_PPU_CLOCK / 4] = 1;
		hw.reg[A133_PPU_ISOLATION / 4] = 2;
		hw.reg[A133_PPU_STATE / 4] = A133_PPU_HOST;
		assert(a133_ppu_power_observe(&power, &state) == 0);
		assert(state == A133_PPU_HOST);
		hw.reg[A133_PPU_ISOLATION / 4] = 0;
		hw.reg[A133_PPU_STATE / 4] = A133_PPU_3D;
		assert(a133_ppu_power_observe(&power, &state) == 0);
		assert(state == A133_PPU_3D);
		hw.reg[A133_PPU_REQUEST / 4] = 1;
		state = 99;
		assert(a133_ppu_power_observe(&power, &state) == EBUSY);
		assert(state == 99 && hw.writes == 0);
	}
	off(&hw);
	power = controller(&hw, 6);
	assert(a133_ppu_power_get(&power, &enabled) == 0 && !enabled);
	assert(a133_ppu_power_set(&power, true) == 0);
	assert(power.owned && !power.uncertain);
	assert(a133_ppu_power_get(&power, &enabled) == 0 && enabled);
	{
		struct a133_ppu_power firmware = controller(&hw, 6);

		writes = hw.writes;
		change = true;
		assert(a133_ppu_power_prepare(&firmware, true, &change) == 0);
		assert(!change && hw.writes == writes);
		change = true;
		assert(a133_ppu_power_prepare(&firmware, false, &change) == EOPNOTSUPP);
		assert(change && hw.writes == writes);
	}
	writes = hw.writes;
	handled = true;
	assert(a133_ppu_service_request(&power, &handled) == 0 && !handled);
	assert(hw.writes == writes);
	assert(a133_ppu_power_set(&power, true) == 0 && hw.writes == writes);
	assert(a133_ppu_power_set(&power, false) == 0);
	assert(!power.owned && !power.uncertain);
	assert(a133_ppu_power_get(&power, &enabled) == 0 && !enabled);
	assert(hw.powered_a == 0 && hw.powered_b == 0);

	/* Firmware-owned domain A must survive all protected revisions. */
	for (unsigned i = 0; i < sizeof(protected) / sizeof(protected[0]); i++) {
		off(&hw);
		power = controller(&hw, protected[i]);
		assert(a133_ppu_power_set(&power, true) == EOPNOTSUPP);
		assert(hw.writes == 0);
		power = controller(&hw, 6);
		assert(a133_ppu_power_set(&power, true) == 0);
		power = controller(&hw, protected[i]);
		hw.writes = hw.domain_a_writes = 0;
		assert(a133_ppu_power_set(&power, true) == 0 && hw.writes == 0);
		assert(a133_ppu_power_set(&power, false) == EOPNOTSUPP);
		assert(hw.writes == 0 && !power.owned);
		power.owned = true;
		assert(a133_ppu_power_set(&power, false) == EOPNOTSUPP);
		assert(hw.writes == 0);
		power.owned = false;
		hw.reg[A133_PPU_REQUEST / 4] = 1 | (2U << 16);
		assert(a133_ppu_service_request(&power, &handled) == 0 && handled);
		assert(a133_ppu_power_get(&power, &enabled) == 0 && !enabled);
		hw.reg[A133_PPU_REQUEST / 4] = 1 | (4U << 16);
		assert(a133_ppu_service_request(&power, &handled) == 0 && handled);
		assert(a133_ppu_power_get(&power, &enabled) == 0 && enabled);
		assert(hw.domain_a_writes == 0);
		assert((hw.reg[A133_PPU_CONTROL / 4] & 0x10) != 0);
	}

	/* Pending firmware work fences otherwise stable GPU access. */
	hw.reg[A133_PPU_REQUEST / 4] = 1 | (2U << 16);
	writes = hw.writes;
	enabled = false;
	assert(a133_ppu_power_get(&power, &enabled) == EBUSY && !enabled);
	assert(a133_ppu_power_set(&power, true) == EBUSY && hw.writes == writes);
	change = true;
	assert(a133_ppu_power_prepare(&power, false, &change) == EBUSY);
	assert(change && hw.writes == writes);

	/* Future silicon cannot acquire domain-B request control. */
	for (unsigned revision = 1; revision <= 7; revision++) {
		if (revision != 1 && revision != 2 && revision != 7)
			continue;
		off(&hw);
		power = controller(&hw, 6);
		assert(a133_ppu_power_set(&power, true) == 0);
		power = controller(&hw, revision);
		hw.reg[A133_PPU_REQUEST / 4] = 1 | (2U << 16);
		writes = hw.writes;
		assert(a133_ppu_service_request(&power, &handled) == EOPNOTSUPP);
		assert(hw.writes == writes);
	}

	/* A reserved state cannot authorize writes or publish an output. */
	off(&hw);
	power = controller(&hw, 6);
	hw.reg[A133_PPU_STATE / 4] = 3;
	enabled = true;
	assert(a133_ppu_power_get(&power, &enabled) == EIO && enabled);
	assert(a133_ppu_power_set(&power, true) == EIO && hw.writes == 0);

	/* An unknown request is masked without ACK or a success signal. */
	off(&hw);
	power = controller(&hw, 6);
	assert(a133_ppu_power_set(&power, true) == 0);
	hw.reg[A133_PPU_CONTROL / 4] = 1;
	hw.reg[A133_PPU_IRQ_MASK / 4] = 1;
	hw.reg[A133_PPU_REQUEST / 4] = 1 | (3U << 16);
	assert(a133_ppu_service_request(&power, &handled) == EPROTO);
	assert(power.uncertain && hw.reg[A133_PPU_IRQ_MASK / 4] == 0);
	assert(hw.reg[A133_PPU_REQUEST / 4] == (1 | (3U << 16)));
	assert(hw.reg[A133_PPU_CONTROL / 4] == 1);

	/* Inject a fault at every power-on write. Never retry or roll back. */
	off(&hw);
	power = controller(&hw, 6);
	assert(a133_ppu_power_set(&power, true) == 0);
	writes = hw.writes;
	for (fault = 1; fault <= writes; fault++) {
		off(&hw);
		power = controller(&hw, 6);
		hw.fail_write = fault;
		assert(a133_ppu_power_set(&power, true) == EIO);
		assert(power.uncertain && hw.writes == fault);
		assert(a133_ppu_power_set(&power, false) == EIO);
		assert(hw.writes == fault);
	}

	/* A failed observation never publishes readiness or unmasks the IRQ. */
	for (unsigned mismatch = 0; mismatch < 2; mismatch++) {
		off(&hw);
		power = controller(&hw, 6);
		hw.fail_readback = mismatch == 0;
		hw.drop_state_write = mismatch != 0;
		assert(a133_ppu_power_set(&power, true) == EIO);
		assert(power.uncertain && hw.acks == 0);
		assert(hw.reg[A133_PPU_IRQ_MASK / 4] == 0);
	}
	off(&hw);
	power = controller(&hw, 6);
	hw.fail_read = 1;
	enabled = true;
	assert(a133_ppu_power_get(&power, &enabled) == EIO && enabled);
	assert(hw.writes == 0);

	/* Reverse ownership has the same no-retry rule at every write. */
	off(&hw);
	power = controller(&hw, 6);
	assert(a133_ppu_power_set(&power, true) == 0);
	hw.writes = 0;
	assert(a133_ppu_power_set(&power, false) == 0);
	writes = hw.writes;
	for (fault = 1; fault <= writes; fault++) {
		off(&hw);
		power = controller(&hw, 6);
		assert(a133_ppu_power_set(&power, true) == 0);
		hw.writes = 0;
		hw.fail_write = fault;
		assert(a133_ppu_power_set(&power, false) == EIO);
		assert(power.uncertain && hw.writes == fault);
		assert(a133_ppu_power_set(&power, true) == EIO && hw.writes == fault);
	}

	/* Service faults include losing completion after a successful ACK. */
	off(&hw);
	power = controller(&hw, 6);
	assert(a133_ppu_power_set(&power, true) == 0);
	hw.writes = 0;
	hw.reg[A133_PPU_REQUEST / 4] = 1 | (2U << 16);
	assert(a133_ppu_service_request(&power, &handled) == 0 && handled);
	writes = hw.writes;
	for (fault = 1; fault <= writes; fault++) {
		off(&hw);
		power = controller(&hw, 6);
		assert(a133_ppu_power_set(&power, true) == 0);
		hw.writes = 0;
		hw.fail_write = fault;
		hw.reg[A133_PPU_REQUEST / 4] = 1 | (2U << 16);
		assert(a133_ppu_service_request(&power, &handled) == EIO);
		assert(power.uncertain && hw.writes == fault);
		assert((hw.reg[A133_PPU_CONTROL / 4] & 0x10) == 0);
		assert(a133_ppu_power_set(&power, false) == EIO && hw.writes == fault);
	}
	puts("PASS: A133 power ownership, domains, request fencing and faults");
	return 0;
}
