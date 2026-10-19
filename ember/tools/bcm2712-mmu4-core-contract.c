/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm2712_iommuc_corevar.h"
#include "bcm2712_mmu4_tablevar.h"
#include "bcm2712_mmu4_corevar.h"

static unsigned long checks;
#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
		abort(); \
	} \
} while (0)

struct event {
	char kind;
	enum bcm2712_mmu4_register reg;
	uint32_t value;
};

/* Fresh fake worlds per case are not recovery of a retained native owner. */
static struct {
	struct bcm2712_mmu4_core core;
	struct bcm2712_iommuc_core cache;
	struct bcm2712_mmu4_config cfg;
	struct bcm2712_mmu4_io io;
	struct bcm2712_mmu4_pages plan;
	struct bcm2712_mmu4_image image;
	uint8_t backing[4][4096];
	struct event events[4096];
	unsigned int count, calls, fail_at;
	unsigned int local_writes, local_polls, cache_polls;
	unsigned int local_busy, cache_busy;
	uint32_t debug, initial_control, shoot, misc, poll_raw, final_raw;
	uint32_t cache_initial, top_word, default_word;
	int injected_error;
	bool prepared, clear_written, cache_written, enabled, failed;
	bool reentry, cache_setup;
} ctx;

static void
record(char kind, enum bcm2712_mmu4_register reg, uint32_t value)
{

	CHECK(ctx.count < 4096);
	ctx.events[ctx.count++] = (struct event){ kind, reg, value };
}

static int
enter(char kind, enum bcm2712_mmu4_register reg, uint32_t value)
{

	CHECK(!ctx.failed);
	record(kind, reg, value);
	if (++ctx.calls == ctx.fail_at) {
		ctx.failed = true;
		return ctx.injected_error;
	}
	return 0;
}

static void
lease(bool retained)
{
	struct bcm2712_iommuc_status s;

	CHECK(bcm2712_iommuc_core_status(&ctx.cache, &s) == 0);
	CHECK((s.reserved & 2U) != 0);
	CHECK(((s.retained & 2U) != 0) == retained);
}

static int
prepare(void *arg, const struct bcm2712_mmu4_config *cfg)
{
	int error;

	CHECK(arg == &ctx);
	lease(false);
	CHECK(ctx.count == 0);
	error = enter('P', BCM2712_MMU4_DEBUG, 0);
	if (error != 0)
		return error;
	CHECK(cfg->debug == ctx.plan.debug);
	CHECK(cfg->top_pa == ctx.plan.top_pa);
	CHECK(cfg->l2_pa[0] == ctx.plan.l2_pa[0]);
	CHECK(cfg->l2_pa[1] == ctx.plan.l2_pa[1]);
	CHECK(cfg->default_pa == ctx.plan.default_pa);
	if (ctx.reentry) {
		CHECK(bcm2712_mmu4_core_program(&ctx.core) == EBUSY);
		CHECK(bcm2712_mmu4_core_fini(&ctx.core) == EBUSY);
	}
	error = bcm2712_mmu4_encode(&ctx.plan, &ctx.image);
	if (error != 0)
		return error;
	/* Model copying/sync completion, not actual native DMA authority. */
	memcpy(ctx.backing, &ctx.image, sizeof(ctx.backing));
	ctx.prepared = true;
	return 0;
}

static int
local_read(void *arg, enum bcm2712_mmu4_register reg, uint32_t *out)
{
	uint32_t value;
	int error;

	CHECK(arg == &ctx && ctx.prepared);
	lease(ctx.clear_written);
	switch (reg) {
	case BCM2712_MMU4_DEBUG:
		value = ctx.debug;
		break;
	case BCM2712_MMU4_CONTROL:
		if (ctx.enabled)
			value = ctx.final_raw;
		else if (ctx.clear_written)
			value = ctx.poll_raw |
			    (++ctx.local_polls <= ctx.local_busy ? 0x80U : 0);
		else
			value = ctx.initial_control;
		break;
	case BCM2712_MMU4_SHOOT_DOWN:
		value = ctx.shoot;
		break;
	case BCM2712_MMU4_MISC:
		value = ctx.misc;
		break;
	default:
		CHECK(false);
		return EINVAL;
	}
	error = enter('R', reg, value);
	if (error == 0)
		*out = value;
	return error;
}

static int
local_write(void *arg, enum bcm2712_mmu4_register reg, uint32_t value)
{
	const enum bcm2712_mmu4_register regs[8] = {
	    BCM2712_MMU4_CONTROL, BCM2712_MMU4_MISC, BCM2712_MMU4_CAP,
	    BCM2712_MMU4_BYPASS_START, BCM2712_MMU4_BYPASS_END,
	    BCM2712_MMU4_PT_BASE, BCM2712_MMU4_ILLEGAL_ADR,
	    BCM2712_MMU4_CONTROL };
	const uint32_t words[8] = { 0x0810100c, ctx.misc & 0x7fffffff,
	    0x80000000, 0, 0, ctx.top_word,
	    0x80000000U | ctx.default_word, 0x04090803 };
	struct bcm2712_iommuc_status s;
	unsigned int n;
	int error;

	CHECK(arg == &ctx && ctx.prepared);
	lease(true);
	n = ctx.local_writes++;
	CHECK(n < 8 && reg == regs[n] && value == words[n]);
	CHECK(ctx.core.status.mutation_attempted);
	CHECK(ctx.core.status.writes_attempted == n + 1);
	if (n == 7) {
		CHECK(bcm2712_iommuc_core_status(&ctx.cache, &s) == 0);
		CHECK(s.flush_complete && s.fault == 0);
	}
	error = enter('W', reg, value);
	if (error == 0) {
		ctx.clear_written = true;
		ctx.enabled = n == 7;
	}
	return error;
}

static void
local_barrier(void *arg)
{

	CHECK(arg == &ctx && !ctx.failed && ctx.count != 0);
	CHECK(ctx.events[ctx.count - 1].kind == 'W');
	record('B', BCM2712_MMU4_CONTROL, 0);
}

static int
cache_read(void *arg, uint32_t *out)
{
	uint32_t value;
	int error;

	CHECK(arg == &ctx);
	if (!ctx.cache_setup) {
		CHECK(ctx.prepared && ctx.local_writes == 7);
		lease(true);
	}
	value = ctx.cache_written ?
	    (++ctx.cache_polls <= ctx.cache_busy ? 5U : 1U) : ctx.cache_initial;
	error = enter('r', BCM2712_MMU4_CONTROL, value);
	if (error == 0)
		*out = value;
	return error;
}

static int
cache_write(void *arg, uint32_t value)
{
	int error;

	CHECK(arg == &ctx && value == 3);
	CHECK(ctx.prepared && ctx.local_writes == 7);
	lease(true);
	error = enter('w', BCM2712_MMU4_CONTROL, value);
	if (error == 0)
		ctx.cache_written = true;
	return error;
}

static void
cache_barrier(void *arg)
{

	CHECK(arg == &ctx && !ctx.failed && ctx.count != 0);
	CHECK(ctx.events[ctx.count - 1].kind == 'w');
	record('b', BCM2712_MMU4_CONTROL, 0);
}

static void
setup(bool initialize)
{
	struct bcm2712_iommuc_io io;
	unsigned int i;

	memset(&ctx, 0, sizeof(ctx));
	ctx.plan.debug = ctx.debug = 0x20804774;
	ctx.plan.top_pa = 0x01000000;
	ctx.plan.l2_pa[0] = 0x01001000;
	ctx.plan.l2_pa[1] = 0x01002000;
	ctx.plan.default_pa = 0x01003000;
	for (i = 0; i < 2025; i++)
		ctx.plan.frame_pa[i] = UINT64_C(0x02000000) + i * 4096U;
	ctx.cfg = (struct bcm2712_mmu4_config){ ctx.plan.debug,
	    ctx.plan.top_pa, { ctx.plan.l2_pa[0], ctx.plan.l2_pa[1] },
	    ctx.plan.default_pa };
	ctx.io = (struct bcm2712_mmu4_io){ prepare, local_read, local_write,
	    local_barrier, &ctx };
	io = (struct bcm2712_iommuc_io){ cache_read, cache_write,
	    cache_barrier, &ctx };
	CHECK(bcm2712_iommuc_core_init(&ctx.cache, &io) == 0);
	ctx.misc = 0xd5a53c69;
	ctx.poll_raw = 12;
	ctx.final_raw = 0x04090803;
	ctx.top_word = 0x1000;
	ctx.default_word = 0x1003;
	ctx.injected_error = EIO;
	memset(&ctx.image, 0xa5, sizeof(ctx.image));
	if (initialize)
		CHECK(bcm2712_mmu4_core_init(&ctx.core, &ctx.cfg,
		    &ctx.io, &ctx.cache) == 0);
}

static struct bcm2712_mmu4_status
status(void)
{
	struct bcm2712_mmu4_status s;
	unsigned int count;

	count = ctx.count;
	memset(&s, 0x80, sizeof(s));
	CHECK(bcm2712_mmu4_core_status(&ctx.core, &s) == 0);
	CHECK(count == ctx.count);
	return s;
}

static void
reinit(void)
{
	struct bcm2712_mmu4_core saved;

	memcpy(&saved, &ctx.core, sizeof(saved));
	CHECK(bcm2712_mmu4_core_init(&ctx.core, &ctx.cfg, &ctx.io,
	    &ctx.cache) == EBUSY);
	CHECK(memcmp(&saved, &ctx.core, sizeof(saved)) == 0);
}

static void
retained(int error)
{
	struct bcm2712_mmu4_status s;
	unsigned int count;

	s = status();
	CHECK(s.first_error == error && s.fault == error);
	CHECK(s.retained && s.reservation_owned && !ctx.core.running);
	CHECK(s.sequence_complete == (error == 0));
	lease(true);
	count = ctx.count;
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == (error ? EIO : EBUSY));
	CHECK(bcm2712_mmu4_core_fini(&ctx.core) == EBUSY);
	reinit();
	CHECK(count == ctx.count);
}

static void
clean_refusal(int error)
{
	struct bcm2712_mmu4_status s;
	struct bcm2712_iommuc_status cache;
	unsigned int count;

	s = status();
	CHECK(s.first_error == error && s.fault == 0);
	CHECK(!s.retained && !s.mutation_attempted && !s.sequence_complete);
	CHECK(s.writes_attempted == 0 && !ctx.core.running);
	count = ctx.count;
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == EBUSY);
	reinit();
	CHECK(bcm2712_mmu4_core_fini(&ctx.core) == 0);
	s = status();
	CHECK(s.stage == BCM2712_MMU4_CLOSED && s.first_error == error);
	CHECK(s.close_error == 0 && !s.reservation_owned);
	CHECK(bcm2712_iommuc_core_status(&ctx.cache, &cache) == 0);
	CHECK(cache.reserved == 0 && cache.retained == 0);
	CHECK(count == ctx.count);
	reinit();
}

static void
success(void)
{
	const char order[] = { 'P', 'R', 'R', 'R', 'W', 'B', 'R', 'R',
	    'W', 'B', 'W', 'B', 'W', 'B', 'W', 'B', 'W', 'B', 'W', 'B',
	    'r', 'w', 'b', 'r', 'W', 'B', 'R' };
	struct bcm2712_mmu4_status s;
	unsigned int i;

	setup(true);
	ctx.reentry = true;
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == 0);
	s = status();
	CHECK(s.stage == BCM2712_MMU4_COMPLETE && s.step == 9 && s.poll == 1);
	CHECK(s.prepared && s.reads_attempted == 6 && s.reads_completed == 6);
	CHECK(s.writes_attempted == 8 && s.writes_completed == 8);
	CHECK(s.raw_valid && s.raw_register == BCM2712_MMU4_CONTROL);
	CHECK(s.raw == 0x04090803 && s.mutation_attempted);
	CHECK(ctx.count == sizeof(order) && ctx.calls == 18);
	for (i = 0; i < sizeof(order); i++)
		CHECK(ctx.events[i].kind == order[i]);
	CHECK(memcmp(ctx.backing, &ctx.image, sizeof(ctx.backing)) == 0);
	CHECK(ctx.backing[0][0] == 1 && ctx.backing[0][1] == 0x10);
	CHECK(ctx.backing[1][0] == 0 && ctx.backing[1][1] == 0x20);
	CHECK(ctx.backing[2][4000] == 0xe8 && ctx.backing[2][4001] == 0x27);
	for (i = 0; i < 4096; i++)
		CHECK(ctx.backing[3][i] == 0);
	retained(0);

	setup(true);
	ctx.local_busy = ctx.cache_busy = 1023;
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == 0);
	s = status();
	CHECK(s.poll == 1024 && s.reads_attempted == 1029);
	CHECK(ctx.cache.status.reads_attempted == 1025);
	CHECK(s.reads_attempted + ctx.cache.status.reads_attempted == 2054);
	retained(0);
	setup(true);
	ctx.local_busy = 1024;
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == ETIMEDOUT);
	s = status();
	CHECK(s.poll == 1024 && s.reads_attempted == 1027);
	CHECK(s.writes_attempted == 1 && ctx.cache.status.reads_attempted == 0);
	retained(ETIMEDOUT);
	setup(true);
	ctx.cache_busy = 1024;
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == ETIMEDOUT);
	CHECK(ctx.local_writes == 7 && !ctx.enabled);
	CHECK(ctx.cache.status.fault == ETIMEDOUT);
	retained(ETIMEDOUT);
}

static void
failures(void)
{
	struct bcm2712_mmu4_status s;
	unsigned int position, n, i, reads, writes;
	int error;

	for (n = 0; n < 2; n++) {
		error = n ? ETIMEDOUT : EIO;
		for (position = 1; position <= 18; position++) {
			setup(true);
			ctx.fail_at = position;
			ctx.injected_error = error;
			CHECK(bcm2712_mmu4_core_program(&ctx.core) == error);
			CHECK(ctx.calls == position && ctx.failed);
			s = status();
			reads = writes = 0;
			for (i = 0; i < ctx.count; i++) {
				reads += ctx.events[i].kind == 'R';
				writes += ctx.events[i].kind == 'W';
			}
			CHECK(s.reads_attempted == reads && s.writes_attempted == writes);
			CHECK(s.reads_completed == reads -
			    (ctx.events[ctx.count - 1].kind == 'R'));
			CHECK(s.writes_completed == writes -
			    (ctx.events[ctx.count - 1].kind == 'W'));
			if (ctx.events[ctx.count - 1].kind == 'R')
				CHECK(!s.raw_valid);
			if (position <= 4)
				clean_refusal(error);
			else
				retained(error);
		}
	}
}

static void
named_guards(void)
{
	unsigned int bit;
	int error;

	setup(true);
	ctx.debug ^= 1;
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == EAGAIN);
	CHECK(ctx.calls == 2);
	clean_refusal(EAGAIN);
	for (bit = 0; bit < 32; bit++) {
		setup(true);
		ctx.initial_control = 1U << bit;
		CHECK(bcm2712_mmu4_core_program(&ctx.core) == EBUSY);
		CHECK(ctx.calls == 3);
		clean_refusal(EBUSY);
	}
	setup(true);
	ctx.shoot = 0x80000000;
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == EBUSY);
	clean_refusal(EBUSY);
	setup(true);
	ctx.shoot = 0x40000000;
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == 0);
	retained(0);
	for (bit = 0; bit < 32; bit++) {
		setup(true);
		ctx.poll_raw = 1U << bit;
		error = (ctx.poll_raw & 0x08101101U) ? EIO :
		    bit == 7 ? ETIMEDOUT : 0;
		CHECK(bcm2712_mmu4_core_program(&ctx.core) == error);
		retained(error);
		setup(true);
		ctx.final_raw ^= 1U << bit;
		error = ((1U << bit) & (0x070f0f03U | 0x08101080U)) ? EIO : 0;
		CHECK(bcm2712_mmu4_core_program(&ctx.core) == error);
		retained(error);
		setup(true);
		ctx.misc = 1U << bit;
		CHECK(bcm2712_mmu4_core_program(&ctx.core) == 0);
		retained(0);
	}
}

static void
ownership(void)
{
	struct bcm2712_iommuc_core saved;
	struct bcm2712_mmu4_status s;

	setup(true);
	CHECK(bcm2712_iommuc_core_reserve(&ctx.cache, BCM2712_IOMMUC_MMU4) == 0);
	memcpy(&saved, &ctx.cache, sizeof(saved));
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == EBUSY);
	CHECK(ctx.count == 0 && !status().reservation_owned);
	CHECK(bcm2712_mmu4_core_fini(&ctx.core) == 0);
	CHECK(memcmp(&saved, &ctx.cache, sizeof(saved)) == 0);

	setup(true);
	CHECK(bcm2712_iommuc_core_reserve(&ctx.cache, BCM2712_IOMMUC_MMU2) == 0);
	CHECK(bcm2712_iommuc_core_retain(&ctx.cache, BCM2712_IOMMUC_MMU2) == 0);
	ctx.cache_setup = true;
	ctx.cache_initial = 4;
	CHECK(bcm2712_iommuc_core_enable_flush(&ctx.cache,
	    BCM2712_IOMMUC_MMU2) == EBUSY);
	ctx.count = ctx.calls = 0;
	memcpy(&saved, &ctx.cache, sizeof(saved));
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == EIO);
	CHECK(ctx.count == 0 && !status().reservation_owned);
	CHECK(bcm2712_mmu4_core_fini(&ctx.core) == 0);
	CHECK(memcmp(&saved, &ctx.cache, sizeof(saved)) == 0);

	/* Deliberate external lease change exercises defensive close failure. */
	setup(true);
	ctx.debug ^= 1;
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == EAGAIN);
	CHECK(bcm2712_iommuc_core_retain(&ctx.cache, BCM2712_IOMMUC_MMU4) == 0);
	CHECK(bcm2712_mmu4_core_fini(&ctx.core) == EBUSY);
	s = status();
	CHECK(s.first_error == EAGAIN && s.close_error == EBUSY && s.fault == EBUSY);
	CHECK(s.reservation_owned && ctx.count == 2);
	CHECK(bcm2712_mmu4_core_fini(&ctx.core) == EBUSY);
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == EIO);
	CHECK(ctx.count == 2);

	/* Actual encoder rejects a late invalid frame PA before the first MMIO. */
	setup(true);
	ctx.plan.frame_pa[2024] = UINT64_C(0x1000000000);
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == ERANGE);
	CHECK(ctx.count == 1 && !ctx.prepared);
	clean_refusal(ERANGE);
}

static uint64_t *
config_pa(unsigned int i)
{

	switch (i) {
	case 0:
		return &ctx.cfg.top_pa;
	case 1:
		return &ctx.cfg.l2_pa[0];
	case 2:
		return &ctx.cfg.l2_pa[1];
	default:
		return &ctx.cfg.default_pa;
	}
}

static void
invalid_config(int error)
{
	struct bcm2712_mmu4_core saved;

	memcpy(&saved, &ctx.core, sizeof(saved));
	CHECK(bcm2712_mmu4_core_init(&ctx.core, &ctx.cfg,
	    &ctx.io, &ctx.cache) == error);
	CHECK(memcmp(&saved, &ctx.core, sizeof(saved)) == 0);
	CHECK(ctx.count == 0 && ctx.cache.status.reserved == 0);
}

static void
configurations(void)
{
	unsigned int i, j, bit, shift;

	for (i = 0; i < 4; i++) {
		setup(false);
		*config_pa(i) = 0;
		invalid_config(EINVAL);
		for (bit = 0; bit < 12; bit++) {
			setup(false);
			*config_pa(i) |= UINT64_C(1) << bit;
			invalid_config(EINVAL);
		}
		setup(false);
		*config_pa(i) = UINT64_C(0x1000000000);
		invalid_config(ERANGE);
		setup(false);
		*config_pa(i) = UINT64_C(0xfffffffffffff000);
		invalid_config(ERANGE);
		for (j = i + 1; j < 4; j++) {
			setup(false);
			*config_pa(j) = *config_pa(i);
			invalid_config(EINVAL);
		}
	}
	for (i = 0; i < 16; i++) {
		if (i == 4)
			continue;
		setup(false);
		ctx.cfg.debug = (ctx.cfg.debug & ~15U) | i;
		invalid_config(EOPNOTSUPP);
	}
	for (shift = 4; shift <= 8; shift += 4) {
		for (i = 0; i < 6; i++) {
			setup(false);
			ctx.cfg.debug = (ctx.cfg.debug & ~(15U << shift)) |
			    (i << shift);
			invalid_config(EOPNOTSUPP);
		}
	}
	setup(false);
	ctx.cfg.debug &= ~0x20000000U;
	invalid_config(EOPNOTSUPP);
	setup(false);
	ctx.cfg.top_pa = ctx.plan.top_pa = UINT64_C(0xffffff000);
	ctx.cfg.l2_pa[0] = ctx.plan.l2_pa[0] = UINT64_C(0xfffffe000);
	ctx.cfg.l2_pa[1] = ctx.plan.l2_pa[1] = UINT64_C(0xfffffd000);
	ctx.cfg.default_pa = ctx.plan.default_pa = UINT64_C(0xfffffc000);
	ctx.top_word = 0xffffff;
	ctx.default_word = 0xfffffc;
	CHECK(bcm2712_mmu4_core_init(&ctx.core, &ctx.cfg, &ctx.io,
	    &ctx.cache) == 0);
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == 0);
	retained(0);
}

static void
initialization(void)
{
	struct bcm2712_mmu4_core saved;
	struct bcm2712_mmu4_status out, original;
	unsigned int i;

	setup(false);
	CHECK(bcm2712_mmu4_core_program(NULL) == EINVAL);
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == EINVAL);
	CHECK(bcm2712_mmu4_core_fini(NULL) == EINVAL);
	CHECK(bcm2712_mmu4_core_fini(&ctx.core) == EINVAL);
	memset(&out, 0xa5, sizeof(out));
	memcpy(&original, &out, sizeof(out));
	CHECK(bcm2712_mmu4_core_status(NULL, &out) == EINVAL);
	CHECK(bcm2712_mmu4_core_status(&ctx.core, &out) == EINVAL);
	CHECK(memcmp(&out, &original, sizeof(out)) == 0);
	CHECK(bcm2712_mmu4_core_init(NULL, &ctx.cfg, &ctx.io, &ctx.cache) == EINVAL);
	CHECK(bcm2712_mmu4_core_init(&ctx.core, NULL, &ctx.io, &ctx.cache) == EINVAL);
	CHECK(bcm2712_mmu4_core_init(&ctx.core, &ctx.cfg, NULL, &ctx.cache) == EINVAL);
	CHECK(bcm2712_mmu4_core_init(&ctx.core, &ctx.cfg, &ctx.io, NULL) == EINVAL);
	for (i = 0; i < 4; i++) {
		setup(false);
		switch (i) {
		case 0: ctx.io.prepare_pages = NULL; break;
		case 1: ctx.io.read = NULL; break;
		case 2: ctx.io.write = NULL; break;
		default: ctx.io.barrier = NULL; break;
		}
		memcpy(&saved, &ctx.core, sizeof(saved));
		CHECK(bcm2712_mmu4_core_init(&ctx.core, &ctx.cfg,
		    &ctx.io, &ctx.cache) == EINVAL);
		CHECK(memcmp(&saved, &ctx.core, sizeof(saved)) == 0 && ctx.count == 0);
	}
	setup(false);
	ctx.core.cfg = ctx.cfg;
	ctx.core.io = ctx.io;
	CHECK(bcm2712_mmu4_core_init(&ctx.core, &ctx.core.cfg,
	    &ctx.core.io, &ctx.cache) == 0);
	CHECK(ctx.core.cfg.top_pa == 0x01000000 && ctx.core.io.arg == &ctx);
	CHECK(ctx.core.io.prepare_pages == prepare && ctx.core.io.read == local_read);
	CHECK(ctx.core.io.write == local_write && ctx.core.io.barrier == local_barrier);
	CHECK(bcm2712_mmu4_core_status(&ctx.core, NULL) == EINVAL);
	reinit();
	CHECK(bcm2712_mmu4_core_fini(&ctx.core) == 0);
	CHECK(bcm2712_mmu4_core_program(&ctx.core) == EBUSY);
	reinit();
	CHECK(ctx.count == 0);
}

int
main(void)
{

	initialization();
	configurations();
	success();
	failures();
	named_guards();
	ownership();
	printf("MMU4 init contract: %lu checks passed\n", checks);
	return 0;
}
