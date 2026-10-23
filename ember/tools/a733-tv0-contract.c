/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted actual-source TV0 controls. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#include "sun60i_a733_output.c"
#include "sun60i_a733_tv0.c"
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
	abort(); } } while (0)
struct event { unsigned space, reg; uint32_t value; };
struct model {
	uint32_t reg[2][256];
	unsigned ops, reads, writes, fail, reject;
	bool retained, late, late_lostwarn;
	struct event events[64];
};
static const int cookie;
static struct model m;
static struct sun60i_a733_tv0 s;
static const struct sun60i_a733_output_mode mode = {
	108000000, 1280, 1328, 1440, 1688, 1024, 1025, 1028, 1066, true, true
};
static int
ready(void *arg, const void *owner)
{
	struct model *p = arg;
	CHECK(p == &m && owner == &cookie && p->retained);
	return ++p->ops == p->fail ? EFAULT : 0;
}
static int
read_reg(void *arg, enum sun60i_a733_tv0_space space, uint32_t reg,
    uint32_t *value)
{
	struct model *p = arg;

	CHECK(space <= A733_VO1_REGS && reg < 0x400 && reg % 4 == 0);
	p->reads++;
	*value = p->reg[space][reg / 4];
	return ++p->ops == p->fail ? EFAULT : 0;
}
static int
write_reg(void *arg, enum sun60i_a733_tv0_space space, uint32_t reg,
    uint32_t value)
{
	struct model *p = arg;

	CHECK(space <= A733_VO1_REGS && reg < 0x400 && reg % 4 == 0);
	CHECK(p->retained && s.written);
	CHECK(p->writes < 64);
	p->events[p->writes++] = (struct event){space, reg, value};
	uint32_t *v = &p->reg[space][reg / 4];
	if (p->writes != p->reject) {
		if (space == A733_TV0_REGS && reg == 4) {
			if (p->late)
				*v |= UINT32_C(0x1000);
			*v = (value & UINT32_C(0x5f000000)) |
			    (*v & value & UINT32_C(0x5f00));
		} else if (space == A733_TV0_REGS && reg == 0xfc) {
			if (p->late_lostwarn)
				*v |= UINT32_C(0x20000000);
			*v &= value | ~UINT32_C(0x60000000);
		} else
			*v = value;
	}
	return ++p->ops == p->fail ? EFAULT : 0;
}
static const struct sun60i_a733_tv0_io io = {&m, ready, read_reg, write_reg};
static void
fixture(void)
{
	memset(&m, 0, sizeof(m));
	for (unsigned space = 0; space < 2; space++)
		for (unsigned reg = 0; reg < 256; reg++)
			m.reg[space][reg] = UINT32_C(0xa5a5a5a5);
	m.reg[A733_VO1_REGS][0] |= 0xa;
	m.reg[A733_TV0_REGS][0x90 / 4] &= ~UINT32_C(0x80000000);
	/* Bit 28 is field polarity. Bits 30 and 29 are W0C errors. */
	m.reg[A733_TV0_REGS][0xfc / 4] = UINT32_C(0x70000000);
	m.retained = true;
	s = (struct sun60i_a733_tv0){.io = &io, .owner = &cookie};
}
static int
pipeline(uint64_t *seq)
{
	int error = sun60i_a733_tv0_route(&s);
	if (error == 0)
		error = sun60i_a733_tv0_prepare(&s, &mode,
		    A733_TV0_EXPERIMENT_VENDOR_ZERO);
	if (error == 0)
		error = sun60i_a733_tv0_start(&s);
	if (error == 0) {
		m.reg[A733_TV0_REGS][1] |= UINT32_C(0x4100);
		error = sun60i_a733_tv0_observe(&s, seq);
	}
	return error;
}
static void
stop_restart(void)
{
	uint64_t seq;
	unsigned first, count;

	fixture();
	CHECK(sun60i_a733_tv0_stop(&s) == EINVAL && !s.fault);
	CHECK(pipeline(&seq) == 0);
	first = m.ops;
	CHECK(sun60i_a733_tv0_stop(&s) == 0);
	count = m.ops - first;
	CHECK(!s.started && s.prepared && s.sequence == 1);
	CHECK((m.reg[0][0x90 / 4] & UINT32_C(0x80000000)) == 0);
	CHECK((m.reg[0][1] & UINT32_C(0x5f005f00)) == 0);
	CHECK(sun60i_a733_tv0_stop(&s) == EALREADY && !s.fault);
	m.reg[0][0xfc / 4] |= UINT32_C(0x60000000);
	CHECK(sun60i_a733_tv0_start(&s) == 0 && s.started);
	CHECK((m.reg[0][0xfc / 4] & UINT32_C(0x70000000)) ==
	    UINT32_C(0x10000000));
	m.reg[0][1] |= UINT32_C(0x4000);
	CHECK(sun60i_a733_tv0_observe(&s, &seq) == 0 && seq == 2);
	fixture(); CHECK(pipeline(&seq) == 0);
	CHECK(sun60i_a733_tv0_stop(&s) == 0);
	m.reg[0][0xfc / 4] |= UINT32_C(0x20000000);
	m.reject = m.writes + 1;
	CHECK(sun60i_a733_tv0_start(&s) == EIO && !s.started);
	CHECK((m.reg[0][0xfc / 4] & UINT32_C(0x20000000)) != 0);
	for (unsigned fail = 1; fail <= count; fail++) {
		fixture(); CHECK(pipeline(&seq) == 0);
		m.fail = m.ops + fail;
		CHECK(sun60i_a733_tv0_stop(&s) == EFAULT);
		CHECK(s.started && s.prepared && s.sequence == 1);
		first = m.ops;
		CHECK(sun60i_a733_tv0_start(&s) == EFAULT && m.ops == first);
		CHECK(sun60i_a733_tv0_stop(&s) == EFAULT && m.ops == first);
	}
	for (unsigned reject = 1; reject <= 2; reject++) {
		fixture(); CHECK(pipeline(&seq) == 0);
		m.reject = m.writes + reject;
		CHECK(sun60i_a733_tv0_stop(&s) == EIO);
		CHECK(s.started && s.prepared);
	}
}

int
main(void)
{
	uint64_t seq = 0;
	stop_restart();
	unsigned ops, writes;
	struct sun60i_a733_output_mode invalid;
	fixture();
	CHECK(pipeline(&seq) == 0 && seq == 1 && s.started && s.prepared && s.routed);
	ops = m.ops; writes = m.writes;
	CHECK(writes == 26);
	CHECK(m.reg[A733_VO1_REGS][0] == UINT32_C(0xa5a5a5a5));
	CHECK(m.reg[A733_VO1_REGS][8] == UINT32_C(0xb5b5a5a5));
	CHECK((m.reg[0][0x98/4] & UINT32_C(0x8001ffff)) == 0x854);
	CHECK((m.reg[0][0x9c/4] & UINT32_C(0x7fff7fff)) == 0x04ff03ff);
	CHECK((m.reg[0][0xa0/4] & UINT32_C(0xffff7fff)) == 0x06970167);
	CHECK((m.reg[0][0xa4/4] & UINT32_C(0x7fff)) == 0x28);
	CHECK((m.reg[0][0xa8/4] & UINT32_C(0x0fff0fff)) == 0x006f0002);
	CHECK((m.reg[0][0x90/4] & UINT32_C(0x800ffff0)) == 0x800001f0);
	CHECK((m.reg[0][0x88/4] & UINT32_C(0x0f000000)) == 0x03000000);
	CHECK((m.reg[0][0x340/4] & UINT32_C(0xc0ffff77)) == 0);
	CHECK((m.reg[0][0xfc/4] & UINT32_C(0x70000000)) ==
	    UINT32_C(0x10000000));
	CHECK(m.reg[0][0] == (UINT32_C(0xa5a5a5a5) & ~UINT32_C(0x31)));
	CHECK((m.reg[0][1] & 0x4100) == 0x100);
	for (unsigned i = 0; i < writes; i++)
		printf("WRITE %u %#x %#x\n", m.events[i].space, m.events[i].reg, m.events[i].value);
	CHECK(sun60i_a733_tv0_observe(&s, &seq) == ENOENT && s.fault == 0 && seq == 1);
	m.reg[0][1] |= 0x4000; m.late = true;
	CHECK(sun60i_a733_tv0_observe(&s, &seq) == 0 && seq == 2);
	CHECK((m.reg[0][1] & 0x1100) == 0x1100);
	/* Every callback failure retains the first error and prevents later I/O. */
	for (unsigned fail = 1; fail <= ops; fail++) {
		fixture(); m.fail = fail; seq = UINT64_C(0x1234);
		CHECK(pipeline(&seq) == EFAULT && s.fault == EFAULT);
		CHECK(m.ops == fail && seq == UINT64_C(0x1234));
		CHECK(s.written == (m.writes != 0));
		CHECK(sun60i_a733_tv0_route(&s) == EFAULT);
		CHECK(sun60i_a733_tv0_prepare(&s, &mode,
		    A733_TV0_EXPERIMENT_VENDOR_ZERO) == EFAULT);
		CHECK(sun60i_a733_tv0_start(&s) == EFAULT);
		CHECK(sun60i_a733_tv0_observe(&s, &seq) == EFAULT && m.ops == fail);
	}
	for (unsigned reject = 1; reject <= writes; reject++) {
		fixture(); m.reject = reject;
		CHECK(pipeline(&seq) == EIO && s.fault == EIO && m.writes == reject);
	}
	for (unsigned loss = 0; loss < 4; loss++) {
		fixture(); CHECK(pipeline(&seq) == 0);
		if (loss < 2) m.reg[0][0xfc/4] |= UINT32_C(0x40000000) >> loss;
		if (loss == 2) m.reg[0][0x90/4] &= ~UINT32_C(0x80000000);
		if (loss == 3) m.reg[0][1] &= ~UINT32_C(0x40000000);
		unsigned old_writes = m.writes;
		CHECK(sun60i_a733_tv0_observe(&s, &seq) == EIO && m.writes == old_writes);
		CHECK(s.observe_valid == (loss == 2 ? 0x20 :
		    loss == 3 ? 0x68 : 0x60));
		CHECK(s.observe_gctl == m.reg[0][0x90/4]);
		if (loss == 3)
			CHECK(s.observe_gint0 == m.reg[0][1]);
	}
	fixture(); CHECK(sun60i_a733_tv0_route(&s) == 0);
	unsigned old_writes = m.writes;
	CHECK(sun60i_a733_tv0_prepare(&s, &mode, 0) == EOPNOTSUPP && m.writes == old_writes);
	fixture(); CHECK(pipeline(&seq) == 0); s.sequence = UINT64_MAX; m.reg[0][1] |= 0x4000;
	CHECK(sun60i_a733_tv0_observe(&s, &seq) == EOVERFLOW);
	/* Interrupt mode: a read-only check, then only a later count is new. */
	fixture(); CHECK(pipeline(&seq) == 0 && seq == 1);
	old_writes = m.writes; ops = m.ops;
	m.reg[0][1] |= 0x4000;
	CHECK(sun60i_a733_tv0_observe_count(&s, 1, &seq) == ENOENT && seq == 1);
	CHECK(sun60i_a733_tv0_observe_count(&s, 5, &seq) == 0 && seq == 5);
	CHECK(s.sequence == 5 && m.writes == old_writes && m.ops > ops);
	CHECK((m.reg[0][1] & 0x4000) == 0x4000);
	CHECK(sun60i_a733_tv0_observe_count(&s, 5, &seq) == ENOENT && seq == 5);
	CHECK(sun60i_a733_tv0_observe_count(&s, 6, NULL) == EINVAL && s.fault == EINVAL);
	fixture(); CHECK(pipeline(&seq) == 0);
	CHECK(sun60i_a733_tv0_observe_count(&s, 0, &seq) == EIO && s.fault == EIO);
	CHECK(s.observe_valid == 0x68 && s.observe_gint0 == m.reg[0][1]);
	/* One TV0 underflow is cleared, then a later clean IRQ is required. */
	fixture(); CHECK(pipeline(&seq) == 0);
	m.reg[0][0xfc/4] |= UINT32_C(0x40000000);
	old_writes = m.writes;
	CHECK(sun60i_a733_tv0_observe_count(&s, 9, &seq) == ENOENT);
	CHECK(s.fault == 0 && s.sequence == 1 && s.underflow_pending &&
	    s.underflow_sequence == 9 && s.underflow_events == 1);
	CHECK(m.writes == old_writes + 1 &&
	    (m.reg[0][0xfc/4] & UINT32_C(0x70000000)) ==
	    UINT32_C(0x10000000));
	old_writes = m.writes;
	CHECK(sun60i_a733_tv0_observe_count(&s, 9, &seq) == ENOENT &&
	    m.writes == old_writes);
	CHECK(sun60i_a733_tv0_observe_count(&s, 10, &seq) == 0 &&
	    seq == 10 && !s.underflow_pending && s.fault == 0);
	fixture(); CHECK(pipeline(&seq) == 0);
	m.reg[0][0xfc/4] |= UINT32_C(0x40000000);
	CHECK(sun60i_a733_tv0_observe_count(&s, 9, &seq) == ENOENT);
	m.reg[0][0xfc/4] |= UINT32_C(0x40000000);
	old_writes = m.writes;
	CHECK(sun60i_a733_tv0_observe_count(&s, 10, &seq) == EIO &&
	    s.fault == EIO && m.writes == old_writes);
	for (unsigned loss = 1; loss < 4; loss++) {
		fixture(); CHECK(pipeline(&seq) == 0);
		if (loss == 1) m.reg[0][0xfc/4] |= UINT32_C(0x20000000);
		if (loss == 2) m.reg[0][0x90/4] &= ~UINT32_C(0x80000000);
		if (loss == 3) m.reg[0][1] &= ~UINT32_C(0x40000000);
		old_writes = m.writes;
		CHECK(sun60i_a733_tv0_observe_count(&s, 9, &seq) == EIO);
		CHECK(s.observe_valid == (loss == 1 ? 0x60 :
		    loss == 2 ? 0x20 : 0x68));
		CHECK(m.writes == old_writes && s.sequence == 1);
	}
	for (unsigned fail = 5; fail <= 6; fail++) {
		fixture(); CHECK(pipeline(&seq) == 0);
		m.reg[0][0xfc/4] |= UINT32_C(0x40000000);
		m.fail = m.ops + fail;
		CHECK(sun60i_a733_tv0_observe_count(&s, 9, &seq) == EFAULT &&
		    s.fault == EFAULT && s.sequence == 1);
	}
	fixture(); CHECK(pipeline(&seq) == 0);
	m.reg[0][0xfc/4] |= UINT32_C(0x40000000);
	m.reject = m.writes + 1;
	CHECK(sun60i_a733_tv0_observe_count(&s, 9, &seq) == EIO &&
	    s.fault == EIO && s.sequence == 1);
	fixture(); CHECK(pipeline(&seq) == 0);
	m.reg[0][0xfc/4] |= UINT32_C(0x40000000);
	m.late_lostwarn = true;
	CHECK(sun60i_a733_tv0_observe_count(&s, 9, &seq) == EIO &&
	    s.fault == EIO && s.sequence == 1 &&
	    (m.reg[0][0xfc/4] & UINT32_C(0x20000000)) != 0);
	fixture(); CHECK(sun60i_a733_tv0_route(&s) == 0);
	CHECK(sun60i_a733_tv0_observe_count(&s, 1, &seq) == EINVAL);
	for (unsigned fail = 1; fail <= 4; fail++) {
		fixture(); CHECK(pipeline(&seq) == 0);
		old_writes = m.writes; m.fail = m.ops + fail;
		CHECK(sun60i_a733_tv0_observe_count(&s, 2, &seq) == EFAULT);
		CHECK(s.fault == EFAULT && s.sequence == 1 && m.writes == old_writes);
	}
	/* Reserved GCTL bit31 stays clear when the hardware reports it clear. */
	fixture(); m.reg[0][0] = 0;
	CHECK(pipeline(&seq) == 0 && m.reg[0][0] == 0);
	/* Reject unsupported timing and phase before any timing write. */
	fixture(); CHECK(sun60i_a733_tv0_route(&s) == 0);
	invalid = mode; invalid.vtotal = invalid.vdisplay + 5;
	old_writes = m.writes;
	CHECK(sun60i_a733_tv0_prepare(&s, &invalid,
	    A733_TV0_EXPERIMENT_VENDOR_ZERO) == ERANGE);
	CHECK(m.writes == old_writes && !s.prepared);
	fixture(); CHECK(sun60i_a733_tv0_start(&s) == EINVAL);
	CHECK(m.writes == 0 && !s.started);
	fixture(); CHECK(sun60i_a733_tv0_route(&s) == 0);
	m.reg[0][0x90 / 4] |= UINT32_C(0x80000000);
	old_writes = m.writes;
	CHECK(sun60i_a733_tv0_prepare(&s, &mode,
	    A733_TV0_EXPERIMENT_VENDOR_ZERO) == EBUSY);
	CHECK(m.writes == old_writes && !s.prepared);
	CHECK(sun60i_a733_tv0_route(NULL) == EINVAL);
	printf("A733 TV0 actual-source contract: %u checks passed\n", checks);
	return 0;
}
