/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the actual independent mechanism. Only the bus is controlled. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "bcm2712_display_quiesce.h"
static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { fprintf(stderr,"quiesce CHECK %d: %s\n",__LINE__,#e); abort(); } } while (0)
#include "bcm2712-quiesce-fixture.h"
static int rd(void *a,unsigned r,uint32_t o,uint32_t *v) { CHECK(a == &qb); return qb_read(r,o,v); }
static int wr(void *a,unsigned r,uint32_t o,uint32_t v) { CHECK(a == &qb); return qb_write(r,o,v); }
static void br(void *a,unsigned r) { CHECK(a == &qb); qb_barrier(r); }
static void dl(void *a,unsigned us) { CHECK(a == &qb); qb_delay(us); }
static const struct bcm2712_quiesce_ops ops = {rd,wr,br,dl};
static void repeat(struct bcm2712_quiesce_status *s, int e)
{
	unsigned n = qb.events;
	CHECK(bcm2712_display_quiesce(&ops,&qb,s) == e && qb.events == n);
}
int main(void)
{
	struct bcm2712_quiesce_status s;
	unsigned operations;
	int e;
	CHECK(bcm2712_display_quiesce(NULL,NULL,NULL) == EINVAL);
	for (unsigned gen = 0x53; gen <= 0x54; gen++) {
		memset(&s,0,sizeof(s)); qb_initialize(gen,true);
		CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == 0);
		CHECK(s.complete && s.snapshot_valid && s.attempted && s.mutation_attempted);
		CHECK(qb.writes == 5 && qb.microseconds == 20000 && qb.regs[0][8] == 0x81012340);
		CHECK((s.control & 0xc4000000U) == 0x44000000U && (s.status & 0x6000) == 0);
		CHECK((s.pv_control & 1) == 0 && (s.pv_video & 1) == 0);
		operations = qb.operations; repeat(&s,EBUSY);
		for (unsigned fault = 1; fault <= operations; fault++) {
			memset(&s,0,sizeof(s)); qb_initialize(gen,true); qb.fault_at = fault;
			CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == EIO);
			CHECK(!s.complete && s.error == EIO && qb.operations == fault && qb.faulted);
			repeat(&s,EIO);
		}
		memset(&s,0,sizeof(s)); qb_initialize(gen,false);
		CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == 0 && s.complete);
		CHECK(!s.mutation_attempted && qb.writes == 0 && qb.delays == 0);
		CHECK((s.control & 0x40000000U) != 0); /* RESET need not clear. */
		for (unsigned gate = 0; gate < 7; gate++) {
			memset(&s,0,sizeof(s)); qb_initialize(gen,true);
			unsigned stride = gen == 0x53 ? 0x20 : 0x40;
			if (gate == 0) qb.regs[2][0] |= 1;
			if (gate == 1) qb.regs[2][1] |= 1;
			if (gate == 2) qb.regs[1][0] |= 4;
			if (gate == 3 || gate == 4) qb.regs[0][(qb.ctrl+(gate-2)*stride)/4] |= 0x80000000U;
			if (gate == 5 || gate == 6) qb.regs[0][(qb.stat+(gate-4)*stride)/4] |= 0x2000;
			CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == EBUSY);
			CHECK(qb.writes == 0 && qb.delays == 0 && !s.mutation_attempted);
			repeat(&s,EBUSY);
		}
		for (unsigned stuck = 0; stuck < 3; stuck++) {
			memset(&s,0,sizeof(s)); qb_initialize(gen,true);
			qb.vid_stuck = stuck == 0; qb.pv_stuck = stuck == 1; qb.hvs_stuck = stuck == 2;
			e = stuck == 1 ? EIO : ETIMEDOUT;
			CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == e && !s.complete);
			CHECK(qb.writes == (stuck == 0 ? 1U : stuck == 1 ? 2U : 5U));
			CHECK(qb.microseconds == (stuck == 0 ? 1000U : stuck == 1 ? 20000U : 21000U));
			repeat(&s,e);
		}
	}
	memset(&s,0,sizeof(s)); qb_initialize(0x55,true);
	CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == EOPNOTSUPP);
	CHECK(qb.operations == 1 && qb.writes == 0); repeat(&s,EOPNOTSUPP);
	printf("display quiesce: %u actual-source checks PASS\n",checks); return 0;
}
