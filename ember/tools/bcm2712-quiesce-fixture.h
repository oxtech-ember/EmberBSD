/* SPDX-License-Identifier: BSD-2-Clause */
/* Controlled bus service shared by the pure and joined actual-source tests. */
struct quiesce_event { unsigned kind, region, offset; uint32_t value; };
struct quiesce_bus {
	uint32_t regs[3][256];
	struct quiesce_event event[1024];
	unsigned events, operations, writes, delays, microseconds, fault_at;
	uint32_t ctrl, stat;
	bool faulted, vid_stuck, pv_stuck, hvs_stuck, barrier_pending;
};
static struct quiesce_bus qb;
static void qb_event(unsigned k, unsigned r, unsigned o, uint32_t v)
{
	CHECK(qb.events < sizeof(qb.event) / sizeof(qb.event[0]));
	qb.event[qb.events++] = (struct quiesce_event){k,r,o,v};
}
static int qb_access(void)
{
	CHECK(!qb.faulted && !qb.barrier_pending);
	qb.operations++;
	if (qb.operations == qb.fault_at) { qb.faulted = true; return EIO; }
	return 0;
}
static int qb_read(unsigned r, unsigned o, uint32_t *v)
{
	int e = qb_access();
	if (e) return e;
	CHECK(r < 3 && o < 1024 && o % 4 == 0 && v != NULL);
	if (qb.operations == 1) CHECK(r == 0 && o == 0);
	*v = qb.regs[r][o / 4]; qb_event(0,r,o,*v); return 0;
}
static int qb_write(unsigned r, unsigned o, uint32_t v)
{
	int e = qb_access();
	if (e) return e;
	/* This whitelist cannot reach global HVS, other channels, HDMI or MMU4. */
	CHECK((r == 1 && (o == 0 || o == 4)) || (r == 0 && o == qb.ctrl));
	CHECK(qb.operations > 12);
	if (r == 1 && o == 4) {
		CHECK(qb.writes == 0 && (v & 1) == 0);
		if (qb.vid_stuck) v |= 1;
	}
	if (r == 1 && o == 0) {
		CHECK(qb.microseconds >= 20000 && (qb.regs[1][1] & 1) == 0);
		CHECK((v & 1) == 0);
		if (qb.pv_stuck) v |= 1;
	}
	if (r == 0) {
		CHECK((qb.regs[1][0] & 1) == 0 && qb.writes >= 3);
		if (qb.writes == 3) { CHECK((v & 0x40000000U) != 0); v |= 0x04000000U; }
		else {
			CHECK((v & 0x44000000U) == 0x44000000U && (v & 0x80000000U) == 0);
			if (!qb.hvs_stuck) qb.regs[0][qb.stat / 4] &= ~0x6000U;
			else v |= 0x80000000U;
		}
	}
	qb.regs[r][o / 4] = v; qb.writes++; qb_event(1,r,o,v); qb.barrier_pending = true;
	return 0;
}
static void qb_barrier(unsigned r)
{
	CHECK(!qb.faulted && qb.barrier_pending && qb.event[qb.events - 1].region == r);
	qb.barrier_pending = false; qb_event(2,r,0,0);
}
static void qb_delay(unsigned us)
{
	CHECK(!qb.faulted && !qb.barrier_pending && (us == 10 || us == 20000));
	qb.delays++; qb.microseconds += us; qb_event(3,0,0,us);
}
static void qb_initialize(uint32_t version, bool active)
{
	unsigned stride = version == 0x53 ? 0x20 : 0x40;
	memset(&qb,0,sizeof(qb)); qb.ctrl = version == 0x53 ? 0x30 : 0x100;
	qb.stat = version == 0x53 ? 0x44 : 0x118;
	qb.regs[0][0] = version; qb.regs[0][8] = 0x81012340;
	for (unsigned i = 0; i < 3; i++) {
		qb.regs[0][(qb.ctrl + i * stride)/4] = 0x1234;
		qb.regs[0][(qb.stat + i * stride)/4] = 0x00400011;
	}
	qb.regs[0][qb.ctrl / 4] |= active ? 0x80000000U : 0x40000000U;
	qb.regs[0][qb.stat / 4] |= active ? 0x4000 : 0;
	qb.regs[1][0] = 0x12340000U | active;
	qb.regs[1][1] = 0xabc00000U | active;
	qb.regs[2][0] = 0x45670000; qb.regs[2][1] = 0xdef00000;
}
