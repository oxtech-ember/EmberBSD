/* SPDX-License-Identifier: BSD-2-Clause */
/* Trace fingerprints generated from pinned FreeBSD table data and parser. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include "rtw8821c_phy.h"
struct fake {
	uint64_t hash;
	unsigned writes, events, stop;
};
static void event(struct fake *f, uint64_t a, uint64_t b, uint64_t c)
{
	uint64_t values[3] = {a, b, c};
	unsigned i, j;
	for (i = 0; i < 3; i++)
		for (j = 0; j < 8; j++) {
			f->hash ^= (values[i] >> (j * 8)) & 255;
			f->hash *= UINT64_C(1099511628211);
		}
	f->events++;
}
static int w1(void *v, uint32_t a, uint8_t d)
{
	struct fake *f = v;
	event(f, 1, a, d);
	f->writes++;
	return 0;
}
static int w4(void *v, uint32_t a, uint32_t d)
{
	struct fake *f = v;
	event(f, 4, a, d);
	f->writes++;
	return 0;
}
static void delay(void *v, unsigned us)
{
	struct fake *f = v;
	event(f, 5, us, 0);
}
static int check(void *v)
{
	struct fake *f = v;
	return f->stop && f->writes >= f->stop ? EINTR : 0;
}
int main(void)
{
	static const struct {
		unsigned rfe, cut, pkg, events;
		uint64_t hash;
	} vectors[] = {{0, 0, 0, 3935, UINT64_C(0xac511522a2b5e1e1)},
		       {2, 0, 0, 4063, UINT64_C(0x169bf9fc7ccee0b0)},
		       {4, 1, 1, 4063, UINT64_C(0xe0ef4ba3c6fe1e40)},
		       {6, 2, 0, 3935, UINT64_C(0xac511522a2b5e1e1)}};
	struct fake f;
	struct rtw8821c_phy_io io = {&f, w1, w4, delay, check};
	struct rtw8821c_phy_profile p;
	unsigned i;
	int e;
	for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
		f = (struct fake){.hash = UINT64_C(14695981039346656037)};
		p = (struct rtw8821c_phy_profile){
		    vectors[i].cut, vectors[i].pkg, vectors[i].rfe};
		e = rtw8821c_load_tables_io(&io, &p);
		if (e || f.events != vectors[i].events ||
		    f.hash != vectors[i].hash)
			return 1;
	}
	f = (struct fake){0};
	p = (struct rtw8821c_phy_profile){0, 0, 3};
	e = rtw8821c_load_tables_io(&io, &p);
	if (e != EINVAL || f.writes)
		return 2;
	p.rfe = 0;
	f.stop = 5;
	e = rtw8821c_load_tables_io(&io, &p);
	if (e != EINTR || f.writes != 5)
		return 3;
	puts("four pinned donor table traces, invalid RFE and cancellation: "
	     "PASS");
	return 0;
}
