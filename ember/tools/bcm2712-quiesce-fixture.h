/* SPDX-License-Identifier: BSD-2-Clause */
/* Controlled bus service shared by the pure and joined actual-source tests. */
struct quiesce_event { unsigned kind, region, offset; uint32_t value; };
struct quiesce_bus {
	uint32_t regs[3][256];
	struct quiesce_event event[1024];
	unsigned events, operations, writes, delays, microseconds, fault_at;
	unsigned video_writes[2], control_writes[2], hvs_writes[2];
	unsigned last_video, settled, long_delays, stride;
	uint32_t ctrl, stat;
	bool faulted, vid_stuck[2], pv_stuck[2], hvs_stuck[2], barrier_pending;
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
	unsigned n;
	struct quiesce_event last;
	if (e) return e;
	CHECK((r >= 1 && r <= 2 && (o == 0 || o == 4)) ||
	    (r == 0 && (o == qb.ctrl || o == qb.ctrl + qb.stride)));
	CHECK(qb.operations > 12 && qb.events > 0);
	last = qb.event[qb.events-1];
	CHECK(last.kind == 0 && last.region == r && last.offset == o);
	if (r >= 1 && o == 4) {
		n = r-1;
		CHECK(qb.control_writes[0] == 0 && qb.control_writes[1] == 0);
		CHECK(qb.hvs_writes[0] == 0 && qb.hvs_writes[1] == 0);
		CHECK(qb.video_writes[n] == 0 && (v & 1) == 0);
		qb.video_writes[n]++;
		if (qb.vid_stuck[n]) v |= 1;
	}
	if (r >= 1 && o == 0) {
		n = r-1;
		CHECK((qb.regs[1][1] & 1) == 0 && (qb.regs[2][1] & 1) == 0);
		CHECK(qb.microseconds >= 20000 && qb.settled > qb.last_video);
		CHECK(qb.hvs_writes[0] == 0 && qb.hvs_writes[1] == 0);
		CHECK((v & 1) == 0);
		if (qb.control_writes[n] != 0) CHECK((v & 2) != 0);
		qb.control_writes[n]++;
		if (qb.pv_stuck[n]) v |= 1;
	}
	if (r == 0) {
		n = o == qb.ctrl ? 0 : 1;
		CHECK(((qb.regs[1][0] | qb.regs[1][1] | qb.regs[2][0] | qb.regs[2][1]) & 1) == 0);
		if (qb.hvs_writes[n] == 0) { CHECK((v & 0x40000000U) != 0); v |= 0x04000000U; }
		else {
			CHECK(qb.hvs_writes[n] == 1 && (v & 0x44000000U) == 0x44000000U && (v & 0x80000000U) == 0);
			if (!qb.hvs_stuck[n]) qb.regs[0][(qb.stat + n * qb.stride) / 4] &= ~0x6000U;
			else v |= 0x80000000U;
		}
		qb.hvs_writes[n]++;
	}
	qb.regs[r][o / 4] = v; qb.writes++; qb_event(1,r,o,v); qb.barrier_pending = true;
	if (r >= 1 && o == 4) qb.last_video = qb.events;
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
	if (us == 20000) { qb.long_delays++; qb.settled = qb.events; }
}
static void qb_initialize(uint32_t version, unsigned active)
{
	unsigned stride = version == 0x53 ? 0x20 : 0x40;
	memset(&qb,0,sizeof(qb)); qb.ctrl = version == 0x53 ? 0x30 : 0x100;
	qb.stat = version == 0x53 ? 0x44 : 0x118; qb.stride = stride;
	qb.regs[0][0] = version; qb.regs[0][8] = 0x81012340;
	for (unsigned i = 0; i < 3; i++) {
		qb.regs[0][(qb.ctrl + i * stride)/4] = 0x1234;
		qb.regs[0][(qb.stat + i * stride)/4] = 0x00400011;
	}
	for (unsigned n = 0; n < 2; n++) {
		bool on = (active & (1U << n)) != 0;
		qb.regs[0][(qb.ctrl + n * stride) / 4] |= on ? 0x80000000U : 0x40000000U;
		qb.regs[0][(qb.stat + n * stride) / 4] |= on ? 0x4000 : 0;
		qb.regs[n+1][0] = 0x12340000U | on;
		qb.regs[n+1][1] = 0xabc00000U | on;
	}
}
