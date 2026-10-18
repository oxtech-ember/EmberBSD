/* Origin: EmberBSD; AI-assisted actual-source NPU preflight control. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned int u_int;
struct clk {
	u_int id;
};
struct sun60i_npu_softc {
	int sc_phandle, sc_dev;
};
#ifndef __BITS
#define __BITS(h, l) (((UINT32_MAX >> (31 - (h))) >> (l)) << (l))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define CHECK(c) do { assert(c); checks++; } while (0)
static unsigned checks, console_calls, domain_calls, get_calls, rates, put_calls;
static int domain_error, missing;
static uint32_t domain_policy;
static struct clk clocks[5];
static const char * const clock_names[] = {
	"clk_npu", "clk_parent", "clk_bus", "clk_mbus_gate", "clk_ahb_gate",
};

static void
aprint_normal_dev(int dev, const char *format, ...)
{
	CHECK(dev == 7 && format != NULL);
	console_calls++;
}

static int
sun60i_a733_pck_domain_state(int node, unsigned domain, uint32_t *pwpr,
    uint32_t *pmer, uint32_t *pwsr)
{
	CHECK(node == 17 && domain == 4);
	domain_calls++;
	if (domain_error != 0)
		return domain_error;
	*pwpr = domain_policy;
	*pmer = 0;
	*pwsr = 8;
	return 0;
}

static struct clk *
fdtbus_clock_get(int node, const char *name)
{
	unsigned id;

	CHECK(node == 5 && get_calls < __arraycount(clocks));
	CHECK(strcmp(name, clock_names[get_calls]) == 0);
	id = get_calls++;
	return (int)id == missing ? NULL : &clocks[id];
}

static u_int
clk_get_rate(struct clk *clk)
{
	CHECK(rates < __arraycount(clocks) && clk == &clocks[rates]);
	rates++;
	return 0;
}

static void
clk_put(struct clk *clk)
{
	CHECK(put_calls < __arraycount(clocks) && clk == &clocks[put_calls]);
	CHECK(rates == put_calls + 1);
	put_calls++;
}

#include "npu-preflight.h"

int
main(void)
{
	const struct sun60i_npu_softc initial = { 5, 7 };
	struct sun60i_npu_softc sc = initial;
	const uint32_t policies[] = { 8, 0x108, 0, 7, 9 };
	(void)aprint_normal_dev;
	for (unsigned i = 0; i < __arraycount(policies); i++) {
		domain_policy = policies[i];
		CHECK(sun60i_npu_stage_domain(&sc, 17) ==
		    ((policies[i] & 15) == 8 ? 0 : EBUSY));
		CHECK(domain_calls == i + 1 && console_calls == 0);
	}
	domain_error = EFAULT;
	CHECK(sun60i_npu_stage_domain(&sc, 17) == EFAULT);
	CHECK(domain_calls == __arraycount(policies) + 1 && console_calls == 0);
	for (missing = -1; missing < 5; missing++) {
		get_calls = rates = put_calls = 0;
		CHECK(sun60i_npu_stage_clocks(&sc) == (missing < 0 ? 0 : ENXIO));
		CHECK(get_calls == (missing < 0 ? 5U : (unsigned)missing + 1));
		CHECK(rates == (missing < 0 ? 5U : (unsigned)missing));
		CHECK(put_calls == rates && console_calls == 0);
	}
	CHECK(memcmp(&sc, &initial, sizeof(sc)) == 0);
	printf("PASS: %u actual-source NPU silent preflight checks\n", checks);
	return 0;
}
