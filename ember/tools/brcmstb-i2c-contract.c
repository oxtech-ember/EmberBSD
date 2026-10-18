/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dev/i2c/brcmstb_i2creg.h>
#include <dev/i2c/brcmstb_i2cvar.h>

static unsigned int checks;
#define CHECK(x) do { \
	checks++; \
	if (!(x)) { \
		fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); \
		exit(1); \
	} \
} while (0)

struct command {
	uint32_t address, count, ctl, condition, input;
};
struct fixture {
	struct brcmstb_i2c core;
	uint32_t reg[22];
	struct command cmd[5];
	unsigned int calls, fail_at, writes, guards, barriers;
	unsigned int commands, read_bytes, first_write_call, delays, time_calls;
	unsigned int nack, settle, remaining;
	unsigned int lose_mask_at, guard_advance_at, time_mode;
	uint64_t now;
	uint8_t offset;
	bool masked, pending, active, never_complete, clear_stuck, initial_advance;
	bool count_stuck, ctl_stuck, hi_stuck, failed, dirty_write;
};

static uint8_t
pattern(unsigned int i)
{

	return (uint8_t)((i * 37U) ^ 0x80U);
}

static int
event(struct fixture *f)
{

	CHECK(!f->failed);
	f->calls++;
	if (f->calls == f->fail_at) {
		f->failed = true;
		return EACCES;
	}
	return 0;
}

static void
complete(struct fixture *f)
{
	const struct command *c = &f->cmd[f->commands - 1];
	unsigned int word, byte;

	f->active = false;
	f->pending = true;
	f->reg[BRCMSTB_I2C_ENABLE / 4] = c->condition | BRCMSTB_I2C_INTRP;
	if (f->commands == f->nack) {
		f->reg[BRCMSTB_I2C_ENABLE / 4] |= BRCMSTB_I2C_NOACK;
		return;
	}
	if ((c->ctl & BRCMSTB_I2C_DTF) == 0) {
		f->offset = (uint8_t)c->input;
		return;
	}
	for (word = 0; word < 8; word++) {
		uint32_t value = 0;

		for (byte = 0; byte < 4; byte++) {
			unsigned int i =
			    f->offset + f->read_bytes + word * 4 + byte;

			value |= (uint32_t)pattern(i) << (8 * byte);
		}
		f->reg[BRCMSTB_I2C_DATA_OUT(word) / 4] = value;
	}
	f->read_bytes += c->count;
}

static int
read_reg(void *arg, unsigned int reg, uint32_t *out)
{
	struct fixture *f = arg;
	int error = event(f);

	if (error != 0)
		return error;
	CHECK(reg <= BRCMSTB_I2C_CTLHI && (reg & 3) == 0);
	if (reg == BRCMSTB_I2C_ENABLE && f->active && !f->never_complete) {
		if (f->remaining != 0)
			f->remaining--;
		else
			complete(f);
	}
	*out = f->reg[reg / 4];
	return 0;
}

static int
write_reg(void *arg, unsigned int reg, uint32_t value)
{
	struct fixture *f = arg;
	int error;

	CHECK(f->masked);
	CHECK(f->guards == f->writes + 1);
	CHECK(f->barriers == f->writes);
	f->writes++;
	if (f->first_write_call == 0)
		f->first_write_call = f->calls + 1;
	error = event(f);
	if (error != 0) {
		/* A callback can fail after hardware accepted the value. */
		f->dirty_write = true;
		f->reg[reg / 4] = value;
		return error;
	}
	CHECK(reg <= BRCMSTB_I2C_CTLHI && (reg & 3) == 0);
	if (reg == BRCMSTB_I2C_ENABLE && value != 0) {
		struct command *c;

		CHECK(!f->active && f->commands < 5);
		CHECK((f->reg[BRCMSTB_I2C_CTL / 4] & BRCMSTB_I2C_INT_EN) != 0);
		c = &f->cmd[f->commands++];
		c->address = f->reg[BRCMSTB_I2C_ADDRESS / 4];
		c->count = f->reg[BRCMSTB_I2C_COUNT / 4];
		c->ctl = f->reg[BRCMSTB_I2C_CTL / 4];
		c->input = f->reg[BRCMSTB_I2C_DATA_IN(0) / 4];
		c->condition = value & ~BRCMSTB_I2C_START;
		CHECK(c->count >= 1 && c->count <= 32);
		f->active = true;
		f->remaining = f->settle;
	}
	if (reg == BRCMSTB_I2C_ENABLE && value == 0 && f->clear_stuck)
		return 0;
	if (reg == BRCMSTB_I2C_COUNT && value == 0 && f->count_stuck)
		return 0;
	if (reg == BRCMSTB_I2C_CTL && (value & BRCMSTB_I2C_INT_EN) == 0 &&
	    f->ctl_stuck)
		return 0;
	if (reg == BRCMSTB_I2C_CTLHI && f->hi_stuck)
		return 0;
	f->reg[reg / 4] = value;
	if (reg == BRCMSTB_I2C_CTL && (value & BRCMSTB_I2C_INT_EN) == 0)
		f->pending = false;
	return 0;
}

static void
barrier(void *arg)
{
	struct fixture *f = arg;

	CHECK(!f->failed);
	f->barriers++;
}

static uint64_t
time_us(void *arg)
{
	struct fixture *f = arg;

	f->time_calls++;
	if (f->time_mode == 1 && f->time_calls > 8)
		return f->now - 1;
	if (f->time_mode == 2 && f->time_calls > 8)
		return f->now + 100000;
	return f->now;
}

static void
delay_us(void *arg, unsigned int us)
{
	struct fixture *f = arg;

	CHECK(us == 10 && !f->failed);
	f->delays++;
	if (f->time_mode != 3)
		f->now += us;
}

static int
guard(void *arg, bool idle)
{
	struct fixture *f = arg;
	int error = event(f);

	if (error != 0)
		return error;
	if (idle && f->initial_advance)
		f->now += 100000;
	if (!idle) {
		f->guards++;
		if (f->guards == f->lose_mask_at)
			f->masked = false;
		if (f->guards == f->guard_advance_at)
			f->now += 100000;
	}
	if (!f->masked || (idle && f->pending))
		return EBUSY;
	return 0;
}

static void
setup(struct fixture *f)
{
	struct brcmstb_i2c_io io = {
		.read = read_reg, .write = write_reg, .barrier = barrier,
		.time_us = time_us, .delay_us = delay_us, .guard = guard,
		.arg = f
	};

	memset(f, 0, sizeof(*f));
	f->masked = true;
	f->now = 100;
	f->reg[BRCMSTB_I2C_CTL / 4] = 0x1000;
	f->reg[BRCMSTB_I2C_CTLHI / 4] = 0x180;
	CHECK(brcmstb_i2c_init(&f->core, &io) == 0);
	CHECK(f->calls == 0);
}

static int
execute(struct fixture *f, size_t len, uint8_t *data)
{
	const uint8_t offset = 0x83;

	return brcmstb_i2c_exec(&f->core, I2C_OP_READ_WITH_STOP, 0x50,
	    &offset, 1, data, len, I2C_F_POLL);
}

static void
unchanged(const uint8_t *data)
{
	unsigned int i;

	for (i = 0; i < 128; i++)
		CHECK(data[i] == 0xa5);
}

static void
success(void)
{
	static const struct {
		size_t len;
		unsigned int n;
		uint32_t cond[5];
	} cases[] = {
		{ 1, 2, { 0x50, 0x00 } },
		{ 31, 2, { 0x50, 0x00 } },
		{ 32, 2, { 0x50, 0x00 } },
		{ 33, 3, { 0x50, 0x10, 0x20 } },
		{ 64, 3, { 0x50, 0x10, 0x20 } },
		{ 65, 4, { 0x50, 0x10, 0x30, 0x20 } },
		{ 128, 5, { 0x50, 0x10, 0x30, 0x30, 0x20 } }
	};
	struct fixture f;
	struct brcmstb_i2c_status status;
	uint8_t data[128];
	unsigned int i, j;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		setup(&f);
		f.settle = 2;
		memset(data, 0xa5, sizeof(data));
		CHECK(execute(&f, cases[i].len, data) == 0);
		CHECK(f.commands == cases[i].n);
		for (j = 0; j < f.commands; j++) {
			CHECK(f.cmd[j].condition == cases[i].cond[j]);
			CHECK(f.cmd[j].address == (j == 0 ? 0xa0U : 0xa1U));
			CHECK((f.cmd[j].ctl & BRCMSTB_I2C_SPEED) == 0x90);
			CHECK((f.cmd[j].ctl & 0x1000) != 0);
			CHECK((f.cmd[j].ctl & BRCMSTB_I2C_DTF) == (j == 0 ? 0U : 1U));
		}
		CHECK(f.cmd[0].count == 1 && f.cmd[0].input == 0x83);
		for (j = 0; j < cases[i].len; j++)
			CHECK(data[j] == pattern(0x83 + j));
		for (; j < sizeof(data); j++)
			CHECK(data[j] == 0xa5);
		CHECK(f.reg[BRCMSTB_I2C_CTLHI / 4] == 0x1c0);
		CHECK(f.reg[BRCMSTB_I2C_COUNT / 4] == 0);
		CHECK(f.reg[BRCMSTB_I2C_ENABLE / 4] == 0);
		CHECK((f.reg[BRCMSTB_I2C_CTL / 4] & BRCMSTB_I2C_INT_EN) == 0);
		CHECK(!f.active && !f.pending);
		CHECK(brcmstb_i2c_status(&f.core, &status) == 0);
		CHECK(status.fault == 0 && !status.active && status.mutation_attempted);
		CHECK(status.commands == f.commands && status.polls <= 10000);
		CHECK(brcmstb_i2c_fini(&f.core) == 0);
	}
}

static void
validation(void)
{
	struct fixture f;
	uint8_t data[128], cmd = 0;
	unsigned int i;
	static const i2c_op_t ops[] = { I2C_OP_READ, I2C_OP_WRITE,
		I2C_OP_WRITE_WITH_STOP, I2C_OP_READ_BLOCK, I2C_OP_WRITE_BLOCK,
		(i2c_op_t)9 };

	setup(&f);
	memset(data, 0xa5, sizeof(data));
	for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
		CHECK(brcmstb_i2c_exec(&f.core, ops[i], 0x50, &cmd, 1,
		    data, 128, 0) == EOPNOTSUPP);
	CHECK(brcmstb_i2c_exec(&f.core, I2C_OP_READ_WITH_STOP, 0x50, &cmd, 1,
	    data, 128, I2C_F_PEC) == EOPNOTSUPP);
	CHECK(brcmstb_i2c_exec(&f.core, I2C_OP_READ_WITH_STOP, 0x50, &cmd, 1,
	    data, 128, 1U << 28) == EOPNOTSUPP);
	CHECK(brcmstb_i2c_exec(&f.core, I2C_OP_READ_WITH_STOP, 0x80, &cmd, 1,
	    data, 128, 0) == EINVAL);
	CHECK(brcmstb_i2c_exec(&f.core, I2C_OP_READ_WITH_STOP, 0x50, NULL, 1,
	    data, 128, 0) == EINVAL);
	CHECK(brcmstb_i2c_exec(&f.core, I2C_OP_READ_WITH_STOP, 0x50, &cmd, 0,
	    data, 128, 0) == EINVAL);
	CHECK(brcmstb_i2c_exec(&f.core, I2C_OP_READ_WITH_STOP, 0x50, &cmd, 2,
	    data, 128, 0) == EINVAL);
	CHECK(brcmstb_i2c_exec(&f.core, I2C_OP_READ_WITH_STOP, 0x50, &cmd, 1,
	    NULL, 128, 0) == EINVAL);
	CHECK(execute(&f, 0, data) == EINVAL);
	CHECK(execute(&f, 129, data) == EINVAL);
	CHECK(f.calls == 0 && f.writes == 0 && f.time_calls == 0);
	unchanged(data);
	CHECK(brcmstb_i2c_init(&f.core, NULL) == EINVAL);
	CHECK(brcmstb_i2c_status(NULL, NULL) == EINVAL);
	CHECK(brcmstb_i2c_fini(NULL) == EINVAL);
	f.core.status.active = true;
	CHECK(execute(&f, 128, data) == EBUSY);
	CHECK(brcmstb_i2c_fini(&f.core) == EBUSY);
}

static void
review_regressions(void)
{
	struct fixture f = { 0 };
	uint8_t data[128];
	unsigned int i;

	f.masked = true;
	f.now = 100;
	f.core.io = (struct brcmstb_i2c_io) {
		.read = read_reg, .write = write_reg, .barrier = barrier,
		.time_us = time_us, .delay_us = delay_us, .guard = guard,
		.arg = &f
	};
	CHECK(brcmstb_i2c_init(&f.core, &f.core.io) == 0);
	CHECK(f.core.io.read == read_reg && f.core.io.arg == &f);
	CHECK(execute(&f, 128, data) == 0);
	setup(&f);
	f.initial_advance = true;
	memset(data, 0xa5, sizeof(data));
	CHECK(execute(&f, 128, data) == ETIMEDOUT);
	CHECK(f.calls == 1 && f.writes == 0);
	CHECK(!f.core.status.mutation_attempted && f.core.status.fault == 0);
	CHECK(brcmstb_i2c_fini(&f.core) == 0);
	unchanged(data);
	for (i = 1; i <= 2; i++) {
		setup(&f);
		f.guard_advance_at = i;
		memset(data, 0xa5, sizeof(data));
		CHECK(execute(&f, 128, data) == ETIMEDOUT);
		CHECK(f.writes == i - 1);
		CHECK(f.core.status.mutation_attempted == (i > 1));
		CHECK(brcmstb_i2c_fini(&f.core) == (i > 1 ? ETIMEDOUT : 0));
		unchanged(data);
	}
}

static void
preflight(void)
{
	struct fixture f;
	uint8_t data[128];
	unsigned int i;

	for (i = 0; i < 7; i++) {
		setup(&f);
		memset(data, 0xa5, sizeof(data));
		switch (i) {
		case 0: f.masked = false; break;
		case 1: f.pending = true; break;
		case 2: f.reg[BRCMSTB_I2C_CTL / 4] |= BRCMSTB_I2C_INT_EN; break;
		case 3: f.reg[BRCMSTB_I2C_ENABLE / 4] = BRCMSTB_I2C_START; break;
		case 4: f.reg[BRCMSTB_I2C_ENABLE / 4] = BRCMSTB_I2C_INTRP; break;
		case 5: f.reg[BRCMSTB_I2C_ENABLE / 4] = BRCMSTB_I2C_NOACK; break;
		case 6: f.reg[BRCMSTB_I2C_COUNT / 4] = 1; break;
		}
		CHECK(execute(&f, 128, data) == EBUSY);
		CHECK(f.writes == 0 && f.core.status.fault == 0);
		unchanged(data);
		CHECK(brcmstb_i2c_fini(&f.core) == 0);
	}
}

static void
faults(void)
{
	struct fixture f;
	uint8_t data[128];
	unsigned int total, i;

	setup(&f);
	CHECK(execute(&f, 128, data) == 0);
	total = f.calls;
	for (i = 1; i <= total; i++) {
		unsigned int calls;
		bool fault;

		setup(&f);
		f.fail_at = i;
		memset(data, 0xa5, sizeof(data));
		CHECK(execute(&f, 128, data) == EACCES);
		CHECK(f.calls == i && f.failed);
		unchanged(data);
		fault = f.first_write_call != 0;
		CHECK(f.core.status.mutation_attempted == fault);
		CHECK(f.core.status.fault == (fault ? EACCES : 0));
		calls = f.calls;
		if (fault) {
			CHECK(brcmstb_i2c_fini(&f.core) == EACCES);
			CHECK(execute(&f, 128, data) == EACCES);
		} else {
			CHECK(brcmstb_i2c_fini(&f.core) == 0);
		}
		CHECK(f.calls == calls);
	}
	for (i = 1; i <= 5; i++) {
		setup(&f);
		f.nack = i;
		memset(data, 0xa5, sizeof(data));
		CHECK(execute(&f, 128, data) == EIO);
		CHECK(f.commands == i && f.core.status.fault == EIO);
		CHECK(f.reg[BRCMSTB_I2C_COUNT / 4] != 0);
		CHECK((f.reg[BRCMSTB_I2C_CTL / 4] & BRCMSTB_I2C_INT_EN) != 0);
		unchanged(data);
	}
	setup(&f);
	f.lose_mask_at = 12;
	memset(data, 0xa5, sizeof(data));
	CHECK(execute(&f, 128, data) == EBUSY);
	CHECK(f.writes == 11 && f.core.status.fault == EBUSY);
	unchanged(data);
	printf("BRCMSTB callback failure positions: %u\n", total);
}

static void
bounds(void)
{
	struct fixture f;
	uint8_t data[128];
	unsigned int i;

	for (i = 0; i < 8; i++) {
		setup(&f);
		memset(data, 0xa5, sizeof(data));
		switch (i) {
		case 0: f.never_complete = true; f.time_mode = 3; break;
		case 1: f.never_complete = true; break;
		case 2: f.time_mode = 1; break;
		case 3: f.time_mode = 2; break;
		case 4: f.never_complete = true; f.now = UINT64_MAX - 5; break;
		case 5: f.clear_stuck = true; break;
		case 6: f.count_stuck = true; break;
		case 7: f.ctl_stuck = true; break;
		}
		CHECK(execute(&f, 128, data) ==
		    (i == 2 || i == 4 || i == 6 || i == 7 ? EIO : ETIMEDOUT));
		CHECK(f.core.status.polls <= 10000);
		CHECK(f.core.status.fault != 0 && !f.core.status.active);
		unchanged(data);
	}
	setup(&f);
	f.hi_stuck = true;
	CHECK(execute(&f, 128, data) == EIO);
	CHECK(f.commands == 0 && f.core.status.fault == EIO);
	setup(&f);
	f.now = UINT64_MAX - 5;
	CHECK(execute(&f, 128, data) == 0);
}

int
main(void)
{

	success();
	validation();
	review_regressions();
	preflight();
	faults();
	bounds();
	printf("BRCMSTB actual-source contract: %u checks passed\n", checks);
	return 0;
}
