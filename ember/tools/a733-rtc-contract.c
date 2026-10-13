/* Origin: EmberBSD; production RTC attachment and DCXO query regression. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef unsigned int u_int;
typedef void *device_t;
typedef void *cfdata_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef uintptr_t bus_addr_t;
typedef size_t bus_size_t;
typedef struct { bool initialized, held; } kmutex_t;
#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#ifndef __BITS
#define __BITS(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#endif
#ifndef __SHIFTOUT
#define __SHIFTOUT(v, m) (((v) & (m)) >> __builtin_ctz(m))
#endif
#ifndef __SHIFTIN
#define __SHIFTIN(v, m) (((v) << __builtin_ctz(m)) & (m))
#endif
#ifndef __SHIFTOUT_MASK
#define __SHIFTOUT_MASK(m) __SHIFTOUT(m, m)
#endif
#define KASSERT(c) assert(c)
#define MUTEX_DEFAULT 0
#define IPL_HIGH 0
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define device_private(d) (d)
#define device_xname(d) "rtc-fixture"
#define aprint_error(...) ((void)0)
#define aprint_error_dev(...) ((void)0)
#define aprint_normal(...) ((void)0)
#define aprint_normal_dev(...) ((void)0)
#define aprint_naive(...) ((void)0)
#define SECS_PER_DAY 86400
#define SECS_PER_HOUR 3600
#define SECS_PER_MINUTE 60
struct clock_ymdhms { uint64_t dt_year; unsigned dt_mon, dt_day, dt_hour,
	dt_min, dt_sec, dt_wday; };
struct todr_chip_handle;
typedef struct todr_chip_handle *todr_chip_handle_t;
struct todr_chip_handle { void *cookie;
	int (*todr_gettime_ymdhms)(todr_chip_handle_t, struct clock_ymdhms *);
	int (*todr_settime_ymdhms)(todr_chip_handle_t, struct clock_ymdhms *); };
static time_t clock_ymdhms_to_secs(struct clock_ymdhms *dt) { return 0; }
static void clock_secs_to_ymdhms(time_t t, struct clock_ymdhms *dt) {}
struct clk;
struct clk_funcs {
	struct clk *(*get)(void *, const char *);
	u_int (*get_rate)(void *, struct clk *);
	int (*enable)(void *, struct clk *), (*disable)(void *, struct clk *);
	int (*set_parent)(void *, struct clk *, struct clk *);
	struct clk *(*get_parent)(void *, struct clk *);
};
struct clk_domain { const char *name; const struct clk_funcs *funcs; void *priv; };
struct clk { struct clk_domain *domain; const char *name; };
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
struct device_compatible_entry { const char *compat; const void *data; };
struct fdtbus_clock_controller_func {
	struct clk *(*decode)(device_t, int, const void *, size_t);
};
#include "sunxi_rtcvar.h"
static const char *binding_compat;
static bus_size_t binding_size;
static int reg_error, map_error;
static unsigned reads, writes, maps, tod_attaches, checks;
static uint32_t samples[2];
static kmutex_t *read_lock;
static void mutex_init(kmutex_t *m, int type, int ipl) { m->initialized = true; }
static void mutex_enter(kmutex_t *m) { assert(m->initialized && !m->held); m->held = true; }
static void mutex_exit(kmutex_t *m) { assert(m->held); m->held = false; }
static int of_compatible_match(int node, const struct device_compatible_entry *table)
{
	for (; table->compat != NULL; table++)
	    if (strcmp(table->compat, binding_compat) == 0) return 1;
	return 0;
}
static const struct device_compatible_entry *
of_compatible_lookup(int node, const struct device_compatible_entry *table)
{
	for (; table->compat != NULL; table++)
	    if (strcmp(table->compat, binding_compat) == 0) return table;
	assert(false); return NULL;
}
static int fdtbus_get_reg(int node, u_int index, bus_addr_t *addr, bus_size_t *size)
{ *addr = 0x7090000; *size = binding_size; return reg_error; }
static int bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
	int flags, bus_space_handle_t *handle)
{ if (!map_error) maps++; *handle = 0; return map_error; }
static uint32_t bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
	bus_size_t off)
{
	assert(read_lock != NULL && read_lock->held && off == 0x160);
	assert(reads < 2); return samples[reads++];
}
static void bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
	bus_size_t off, uint32_t value) { writes++; }
static struct clk *fdtbus_clock_get_index(int node, u_int index) { return NULL; }
static const char *fdtbus_get_string_index(int node, const char *name, int index)
{ return NULL; }
static void fdtbus_todr_attach(device_t dev, int node, struct todr_chip_handle *tod)
{ tod_attaches++; }
static void fdtbus_register_clock_controller(device_t dev, int node,
	const struct fdtbus_clock_controller_func *funcs) {}
static void clk_attach(struct clk *clk) {}
static u_int clk_get_rate(struct clk *clk) { return 32768; }
#ifndef __NetBSD__
static uint32_t be32dec(const void *ptr)
{ const uint8_t *b = ptr; return (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]; }
#endif
/* Existing RTC date-range code has the kernel's signedness exemption. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "rtc.h"
#pragma GCC diagnostic pop
#define CHECK(c) do { assert(c); checks++; } while (0)
static struct sunxi_rtc_softc sc, other;

static void reset(void)
{
	memset(&sc, 0, sizeof(sc)); memset(&other, 0, sizeof(other));
	sunxi_rtc_dcxo_providers = NULL;
	binding_compat = "allwinner,sun60i-a733-rtc"; binding_size = 0x400;
	reg_error = map_error = 0; reads = writes = maps = tod_attaches = 0;
	samples[0] = samples[1] = 2U << 14; read_lock = &sc.sc_clk_mutex;
}
static void attach(void)
{
	struct fdt_attach_args faa = { .faa_phandle = 7 };
	CHECK(sunxi_rtc_match(NULL, NULL, &faa) == 1);
	sunxi_rtc_attach(NULL, &sc, &faa);
}
int main(void)
{
	static const u_int rates[] = { 24000000, 19200000, 26000000, 24000000 };
	struct sunxi_rtc_dcxo_state state, saved;
	reset(); attach();
	CHECK(maps == 1 && tod_attaches == 1 && reads == 2 && writes == 0);
	for (u_int code = 0; code < 4; code++) {
	    reads = 0; samples[0] = samples[1] = (code << 14) | 3;
	    CHECK(sunxi_rtc_dcxo_query(7, &state) == 0);
	    CHECK(state.rate_hz == rates[code] && reads == 2 && writes == 0);
	    CHECK(state.sample[0] == samples[0] && state.sample[1] == samples[1]);
	}
	memset(&state, 0xa5, sizeof(state)); saved = state;
	for (u_int bit = 0; bit < 32; bit++) {
	    reads = 0; samples[1] = samples[0] ^ __BIT(bit);
	    CHECK(sunxi_rtc_dcxo_query(7, &state) == EBUSY);
	    CHECK(memcmp(&state, &saved, sizeof(state)) == 0 && reads == 2);
	    CHECK(writes == 0 && !sc.sc_clk_mutex.held);
	}
	reads = 0;
	CHECK(sunxi_rtc_dcxo_query(8, &state) == ENXIO);
	CHECK(sunxi_rtc_dcxo_query(0, &state) == EINVAL);
	CHECK(sunxi_rtc_dcxo_query(7, NULL) == EINVAL);
	CHECK(memcmp(&state, &saved, sizeof(state)) == 0 && reads == 0);
	other.sc_phandle = 7; other.sc_conf = &sun60i_a733_rtc_config;
	CHECK(sunxi_rtc_dcxo_register(&other, 0x400) == EEXIST);
	CHECK(sunxi_rtc_dcxo_providers == &sc && sc.sc_dcxo_next == NULL);
	other.sc_phandle = 8;
	CHECK(sunxi_rtc_dcxo_register(&other, 0x163) == EINVAL);
	other.sc_conf = &sun4i_rtc_config;
	CHECK(sunxi_rtc_dcxo_register(&other, 0x400) == EOPNOTSUPP);
	other.sc_conf = &sun60i_a733_rtc_config;
	binding_compat = "allwinner,sun4i-a10-rtc";
	CHECK(sunxi_rtc_dcxo_register(&other, 0x400) == EOPNOTSUPP);
	for (u_int failure = 0; failure < 4; failure++) {
	    reset();
	    if (failure == 0) reg_error = EIO;
	    if (failure == 1) map_error = EIO;
	    if (failure == 2) binding_size = 0x163;
	    if (failure == 3) binding_compat = "allwinner,sun4i-a10-rtc";
	    attach();
	    CHECK(sunxi_rtc_dcxo_providers == NULL && reads == 0 && writes == 0);
	    CHECK(sunxi_rtc_dcxo_query(7, &state) == ENXIO);
	}
	reset(); samples[1] ^= 1; attach();
	CHECK(sunxi_rtc_dcxo_providers == &sc && writes == 0);
	reads = 0; samples[1] = samples[0];
	CHECK(sunxi_rtc_dcxo_query(7, &state) == 0 && state.rate_hz == 26000000);
	printf("PASS: %u production RTC/DCXO checks\n", checks);
	return 0;
}
