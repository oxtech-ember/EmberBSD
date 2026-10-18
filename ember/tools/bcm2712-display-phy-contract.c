/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual fixed BSD PHY core. Register oracle comes from pinned facts. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "phyvar.h"
#include "phy.h"

static unsigned int checks;
#define CHECK(e) do { checks++; if (!(e)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); } } while (0)
struct fact { char op; enum bcm2712_phy_window window; unsigned int offset; uint32_t value; };
/* Hardware transcript oracle; no driver implementation is included here. */
static const struct fact program[] = {
	{ 'w', BCM2712_PHY_WINDOW, 0, 0 }, { 'w', BCM2712_PHY_WINDOW, 4, 0 },
	{ 'w', BCM2712_PHY_WINDOW, 0x28, 0x10 },
	{ 'w', BCM2712_PHY_WINDOW, 0x60, 0x810c6000 },
	{ 'w', BCM2712_PHY_WINDOW, 0x64, 0x00b8c451 },
	{ 'w', BCM2712_PHY_WINDOW, 0x68, 0x46402e31 },
	{ 'w', BCM2712_PHY_WINDOW, 0x6c, 0x00b8c005 },
	{ 'w', BCM2712_PHY_WINDOW, 0x70, 0x42410261 },
	{ 'w', BCM2712_PHY_WINDOW, 0x74, 0xcc021001 },
	{ 'w', BCM2712_PHY_WINDOW, 0x78, 0xc8301c80 },
	{ 'w', BCM2712_PHY_WINDOW, 0x7c, 0xb0804444 },
	{ 'w', BCM2712_PHY_WINDOW, 0x80, 0xf80f8000 },
	{ 'w', BCM2712_PHY_WINDOW, 0x1c, 0x2036 },
	{ 'w', BCM2712_PHY_WINDOW, 0, 0x7f },
	{ 'w', BCM2712_RM_WINDOW, 0x18, 0x98100000 },
	{ 'w', BCM2712_PHY_WINDOW, 0x2c, 0x407 },
	{ 'w', BCM2712_PHY_WINDOW, 0x44, 0 },
	{ 'w', BCM2712_PHY_WINDOW, 0x28, 9 },
	{ 'w', BCM2712_PHY_WINDOW, 8, 0x80828700 },
	{ 'w', BCM2712_PHY_WINDOW, 12, 0x80828700 },
	{ 'w', BCM2712_PHY_WINDOW, 16, 0x80828700 },
	{ 'w', BCM2712_PHY_WINDOW, 20, 0x80828700 },
	{ 'w', BCM2712_PHY_WINDOW, 0x54, 0 },
	{ 'w', BCM2712_PHY_WINDOW, 4, 0x1cf },
	{ 'w', BCM2712_PHY_WINDOW, 0x194, 1 },
	{ 'r', BCM2712_PHY_WINDOW, 0x190, 0 },
	{ 'w', BCM2712_PHY_WINDOW, 0x190, 0 },
	{ 'r', BCM2712_PHY_WINDOW, 0x190, 0 },
	{ 'w', BCM2712_PHY_WINDOW, 0x190, 0 }
};
struct fixture {
	struct bcm2712_phy *core;
	unsigned int calls, barriers, fail, base;
	uint32_t first, second;
	int error;
	bool disable, barrier_due;
};
static int
access(struct fixture *f, char op, enum bcm2712_phy_window window,
    unsigned int offset, uint32_t *value)
{
	const struct fact *fact;
	unsigned int step = f->calls - f->base;

	CHECK(!f->barrier_due);
	CHECK(window == BCM2712_PHY_WINDOW || window == BCM2712_RM_WINDOW);
	CHECK((offset & 3U) == 0);
	CHECK(offset < (window == BCM2712_PHY_WINDOW ? BCM2712_PHY_LENGTH : BCM2712_RM_LENGTH));
	CHECK(step < (f->disable ? 3U : __arraycount(program)));
	fact = &program[step];
	CHECK(fact->op == op && fact->window == window && fact->offset == offset);
	CHECK(f->core->status.step == step + 1);
	CHECK(f->core->status.stage == (f->disable ? BCM2712_PHY_DISABLE : BCM2712_PHY_PROGRAM));
	CHECK(!f->core->status.programming_complete && !f->core->status.disable_complete);
	if (op == 'w') {
		CHECK(f->core->status.mutation_attempted);
		CHECK(*value == (step == 26 ? f->first & ~1U : step == 28 ? f->second | 1U : fact->value));
		f->barrier_due = true;
	} else {
		*value = step == 25 ? f->first : f->second;
	}
	f->calls++;
	return f->calls == f->fail ? f->error : 0;
}
static int read_reg(void *arg, enum bcm2712_phy_window window,
    unsigned int offset, uint32_t *out)
{ return access(arg, 'r', window, offset, out); }
static int write_reg(void *arg, enum bcm2712_phy_window window,
    unsigned int offset, uint32_t value)
{ return access(arg, 'w', window, offset, &value); }
static void barrier(void *arg)
{
	struct fixture *f = arg;

	CHECK(f->barrier_due && f->calls != f->fail);
	f->barrier_due = false; f->barriers++;
}
static struct bcm2712_phy_io
io_new(struct fixture *f)
{
	return (struct bcm2712_phy_io){ read_reg, write_reg, barrier, f };
}
static void
reinit_refused(struct bcm2712_phy *core, struct fixture *f)
{
	struct bcm2712_phy saved = *core;
	unsigned int calls = f->calls, barriers = f->barriers;

	CHECK(bcm2712_phy_init(core, &core->io) == EBUSY);
	CHECK(memcmp(core, &saved, sizeof(saved)) == 0);
	CHECK(f->calls == calls && f->barriers == barriers);
}
static void
validation(void)
{
	struct bcm2712_phy core = { 0 }, saved;
	struct fixture f = { .core = &core };
	struct bcm2712_phy_io io = io_new(&f), invalid;
	struct bcm2712_phy_status status, untouched;
	unsigned int i;

	memset(&status, 0xa5, sizeof(status)); untouched = status;
	CHECK(bcm2712_phy_status(NULL, &status) == EINVAL);
	CHECK(bcm2712_phy_status(&core, NULL) == EINVAL);
	CHECK(bcm2712_phy_status(&core, &status) == ENXIO);
	CHECK(memcmp(&status, &untouched, sizeof(status)) == 0);
	CHECK(bcm2712_phy_program_148500(NULL) == EINVAL);
	CHECK(bcm2712_phy_disable(NULL) == EINVAL);
	CHECK(bcm2712_phy_program_148500(&core) == ENXIO);
	CHECK(bcm2712_phy_disable(&core) == ENXIO);
	CHECK(bcm2712_phy_init(NULL, &io) == EINVAL);
	CHECK(bcm2712_phy_init(&core, NULL) == EINVAL);
	for (i = 0; i < 3; i++) {
		invalid = io; saved = core;
		if (i == 0) invalid.read = NULL;
		if (i == 1) invalid.write = NULL;
		if (i == 2) invalid.barrier = NULL;
		CHECK(bcm2712_phy_init(&core, &invalid) == EINVAL);
		CHECK(memcmp(&core, &saved, sizeof(core)) == 0);
	}
	CHECK(f.calls == 0 && f.barriers == 0);
	core.io = io;
	CHECK(bcm2712_phy_init(&core, &core.io) == 0);
	CHECK(core.io.arg == &f && core.io.read == read_reg && core.io.write == write_reg);
	reinit_refused(&core, &f);
	CHECK(bcm2712_phy_disable(&core) == EBUSY && f.calls == 0);
}
static void
sequence(uint32_t first, uint32_t second)
{
	struct bcm2712_phy core = { 0 };
	struct fixture f = { .core = &core, .first = first, .second = second };
	struct bcm2712_phy_io io = io_new(&f);
	struct bcm2712_phy_status status;

	CHECK(bcm2712_phy_init(&core, &io) == 0);
	CHECK(bcm2712_phy_program_148500(&core) == 0);
	CHECK(f.calls == 29 && f.barriers == 27);
	CHECK(bcm2712_phy_status(&core, &status) == 0);
	CHECK(status.stage == BCM2712_PHY_PROGRAMMED && status.step == 29);
	CHECK(status.reads == 2 && status.writes == 27 && status.fault == 0);
	CHECK(status.programming_complete && !status.disable_complete);
	reinit_refused(&core, &f);
	CHECK(bcm2712_phy_program_148500(&core) == EBUSY && f.calls == 29);
	f.disable = true; f.base = f.calls;
	CHECK(bcm2712_phy_disable(&core) == 0);
	CHECK(f.calls == 32 && f.barriers == 30);
	CHECK(bcm2712_phy_status(&core, &status) == 0);
	CHECK(status.stage == BCM2712_PHY_DISABLED && status.step == 3);
	CHECK(status.reads == 2 && status.writes == 30 && status.fault == 0);
	CHECK(!status.programming_complete && status.disable_complete);
	reinit_refused(&core, &f);
	CHECK(bcm2712_phy_program_148500(&core) == EBUSY);
	CHECK(bcm2712_phy_disable(&core) == EBUSY);
	CHECK(f.calls == 32 && f.barriers == 30);
}
static void
failures(bool disable, int error)
{
	struct bcm2712_phy core;
	struct fixture f;
	struct bcm2712_phy_io io;
	struct bcm2712_phy_status status;
	unsigned int fail, prior, reads, writes, i, calls, barriers;

	for (fail = 1; fail <= (disable ? 3U : 29U); fail++) {
		memset(&core, 0, sizeof(core));
		f = (struct fixture){ .core = &core, .first = 0xa5a50003U, .second = 0x5a5afff0U, .error = error };
		io = io_new(&f);
		CHECK(bcm2712_phy_init(&core, &io) == 0);
		if (disable) {
			CHECK(bcm2712_phy_program_148500(&core) == 0);
			f.disable = true; f.base = f.calls;
		}
		f.fail = f.base + fail;
		CHECK((disable ? bcm2712_phy_disable(&core) : bcm2712_phy_program_148500(&core)) == error);
		prior = fail - 1; reads = disable ? 2 : 0; writes = disable ? 27 : 0;
		for (i = 0; i < prior; i++) {
			if (program[i].op == 'r') reads++; else writes++;
		}
		CHECK(bcm2712_phy_status(&core, &status) == 0);
		CHECK(status.fault == error && status.step == fail);
		CHECK(status.stage == (disable ? BCM2712_PHY_DISABLE : BCM2712_PHY_PROGRAM));
		CHECK(status.reads == reads && status.writes == writes);
		CHECK(status.mutation_attempted && !status.programming_complete && !status.disable_complete);
		calls = f.calls; barriers = f.barriers;
		CHECK(barriers == writes);
		reinit_refused(&core, &f);
		CHECK(bcm2712_phy_program_148500(&core) == error);
		CHECK(bcm2712_phy_disable(&core) == error);
		CHECK(bcm2712_phy_status(&core, &status) == 0);
		CHECK(f.calls == calls && f.barriers == barriers);
	}
}
int
main(void)
{
	static const uint32_t bits[] = { 0, UINT32_MAX, 0x80000000U, 0xa5a50003U, 0x5a5afff0U };
	unsigned int i;

	validation();
	for (i = 0; i < __arraycount(bits); i++) sequence(bits[i], bits[__arraycount(bits) - 1 - i]);
	failures(false, EIO); failures(true, EIO);
	failures(false, ETIMEDOUT); failures(true, ETIMEDOUT);
	CHECK(UINT64_C(148500000) * 10 * 7 == UINT64_C(10395000000));
	CHECK(UINT64_C(148500000) / 100 * 101 == 149985000);
	printf("VC6 PHY core: %u checks PASS; source only, no hardware claim\n", checks);
	return 0;
}
