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
		for (unsigned mask = 0; mask < 4; mask++) {
			memset(&s,0,sizeof(s)); qb_initialize(gen,mask);
			CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == 0);
			CHECK(s.complete && s.snapshot_valid && s.attempted);
			CHECK(qb.writes == (mask == 3 ? 10U : mask == 0 ? 0U : 5U));
			CHECK(qb.microseconds == (mask == 0 ? 0U : 20000U) && qb.long_delays == (mask == 0 ? 0U : 1U));
			CHECK(qb.regs[0][8] == 0x81012340 && qb.regs[0][(qb.ctrl+2*qb.stride)/4] == 0x1234);
			for (unsigned n = 0; n < 2; n++) {
				CHECK(s.pipe[n].complete && s.pipe[n].pv_disabled && s.pipe[n].hvs_disabled && s.pipe[n].error == 0);
				CHECK(s.pipe[n].mutation_attempted == ((mask & (1U << n)) != 0));
				CHECK((s.pipe[n].control & 0xc0000000U) == 0x40000000U && (s.pipe[n].status & 0x6000) == 0);
				CHECK((s.pipe[n].pv_control & 1) == 0 && (s.pipe[n].pv_video & 1) == 0);
			}
			operations = qb.operations; repeat(&s,EBUSY);
			for (unsigned fault = 1; fault <= operations; fault++) {
				memset(&s,0,sizeof(s)); qb_initialize(gen,mask); qb.fault_at = fault;
				CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == EIO);
				CHECK(!s.complete && s.error == EIO && qb.operations == fault && qb.faulted);
				if (s.pipe[0].error) CHECK(s.pipe[0].error == EIO && !s.pipe[0].complete && s.pipe[1].error == 0);
				if (s.pipe[1].error) CHECK(s.pipe[1].error == EIO && !s.pipe[1].complete);
				repeat(&s,EIO);
			}
		}
		for (unsigned gate = 0; gate < 4; gate++) {
			memset(&s,0,sizeof(s)); qb_initialize(gen,3);
			if (gate == 0) qb.regs[1][0] |= 4;
			if (gate == 1) qb.regs[2][0] |= 8;
			if (gate == 2) qb.regs[0][(qb.ctrl+2*qb.stride)/4] |= 0x80000000U;
			if (gate == 3) qb.regs[0][(qb.stat+2*qb.stride)/4] |= 0x2000;
			CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == EBUSY);
			CHECK(qb.writes == 0 && qb.delays == 0 && !s.mutation_attempted); repeat(&s,EBUSY);
		}
		for (unsigned port = 0; port < 2; port++) {
			for (unsigned stuck = 0; stuck < 3; stuck++) {
				memset(&s,0,sizeof(s)); qb_initialize(gen,3);
				qb.vid_stuck[port] = stuck == 0; qb.pv_stuck[port] = stuck == 1; qb.hvs_stuck[port] = stuck == 2;
				e = stuck == 1 ? EIO : ETIMEDOUT;
				CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == e && !s.complete);
				CHECK(s.pipe[port].error == e && !s.pipe[port].complete);
				CHECK(qb.writes == (stuck == 0 ? port+1 : stuck == 1 ? 3+2*port : 8+2*port));
				CHECK(qb.microseconds == (stuck == 0 ? 1000U : stuck == 1 ? 20000U : 21000U));
				if (stuck == 2 && port == 1) CHECK(s.pipe[0].complete && s.pipe[0].error == 0);
				repeat(&s,e);
			}
		}
	}
	memset(&s,0,sizeof(s)); qb_initialize(0x55,3);
	CHECK(bcm2712_display_quiesce(&ops,&qb,&s) == EOPNOTSUPP);
	CHECK(qb.operations == 1 && qb.writes == 0); repeat(&s,EOPNOTSUPP);
	printf("display quiesce: %u actual-source checks PASS\n",checks); return 0;
}
