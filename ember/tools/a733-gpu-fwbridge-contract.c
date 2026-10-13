/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests. */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_gpu_fwimage.h"
#include "sun60i_a733_gpu_fwinit.h"
#include "powervr/pvr_rogue_fwif.h"

typedef unsigned int u_int;
typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef void *bus_dma_tag_t;
typedef void *bus_dmamap_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef struct { uint64_t ds_addr; size_t ds_len; } bus_dma_segment_t;
typedef struct { bool held; unsigned int enters; } kmutex_t;
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_POSTWRITE 8
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#include "mem.h"
#include "start.h"
#include "boot.h"
#include "probe.h"
#include "service.h"
#include "backend.h"
#include "bridge.h"
#include "platform-defines.h"

static unsigned int checks;
#define CHECK(e) do { checks++; if (!(e)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); \
} } while (0)
#define KASSERT(e) CHECK(e)
static void mutex_enter(kmutex_t *m) { CHECK(!m->held); m->held = true; m->enters++; }
static void mutex_exit(kmutex_t *m) { CHECK(m->held); m->held = false; }
static bool mutex_owned(kmutex_t *m) { return m->held; }
struct drm_device { unsigned int id; };
static struct drm_device device;
static struct sun60i_a733_gpu_fwobject object_pool[4];
static uint8_t bytes[4][256];
static unsigned int attach_calls, alloc_calls, free_calls, sync_calls;
static unsigned int command_calls, flush_calls, check_calls, register_calls;
static unsigned int map_calls, provider_calls;
static int service_error, register_error, operation_error, map_error;
static bool recursive_publish;
static uint32_t reply;
static const struct sun60i_a733_gpu_fwprobe_result *adopted;
#include "bridge-driver.h"

static struct sun60i_gpu_softc {
	kmutex_t sc_run_lock;
	struct sun60i_a733_gpu_fwbridge sc_fwbridge;
	struct drm_device *sc_render_device;
	bool sc_render_pending, sc_exec_ready;
	bool sc_run_pass, sc_run_core_on, sc_have_id, sc_kick_done;
	unsigned int sc_run_result, sc_run_stage, sc_clock_source;
	unsigned int sc_core_hz, sc_bus_hz;
	int sc_phandle;
	uint64_t sc_bvnc;
	bus_space_tag_t sc_bst;
	struct sun60i_a733_gpu_fwmem *sc_fw_memory;
	struct sun60i_a733_gpu_fwboot_result sc_boot_result;
	struct sun60i_a733_gpu_fwprobe_result sc_cache_result, sc_kick_result;
	char sc_clock_state[160];
} sc;
struct clk { int unused; };
struct sun60i_a733_gpu_state {
	int readiness_error;
	unsigned int reason, sample[2][4], core_hz, bus_hz;
};
enum { A733_GPU_PLL, A733_GPU_MODULE, A733_GPU_BUS };
static struct clk clock_object;
static struct sun60i_a733_gpu_fwmem memory;
static int stage_error;
static int sun60i_gpu_power_node(struct sun60i_gpu_softc *s)
{ CHECK(mutex_owned(&s->sc_run_lock)); provider_calls++; return 1; }
static int stage_effect(void) { provider_calls++; return stage_error; }
#define sun60i_gpu_runtime_supply(...) stage_effect()
#define sun60i_gpu_powerdomain_top(...) stage_effect()
#define sun60i_a733_pck_gpu_reserve(...) stage_effect()
#define sun60i_a733_pck_gpu_retain(...) stage_effect()
#define sun60i_a733_ccu_gpu_reserve(...) stage_effect()
#define sun60i_gpu_runtime_supply_top(...) stage_effect()
#define sun60i_a733_ccu_gpu_prepare_pll(g, s, r) (*(r) = true, stage_effect())
#define sun60i_a733_ccu_gpu_prepare(g, s, r) (*(r) = true, stage_effect())
#define sun60i_gpu_stage_request(s, p) ((void)(p), stage_effect())
#define sun60i_a733_pck_gpu_wait(...) stage_effect()
#define sun60i_gpu_stage_top_id(...) stage_effect()
#define sun60i_gpu_stage_identify(...) stage_effect()
#define sun60i_gpu_stage_fwinfo(...) stage_effect()
#define sun60i_gpu_stage_fwmem(...) stage_effect()
#define sun60i_gpu_stage_fwinit(...) stage_effect()
#define sun60i_gpu_stage_fwboot(...) stage_effect()
#define sun60i_gpu_stage_fwprobe(...) stage_effect()
#define sun60i_gpu_stage_fwcache(...) stage_effect()
#define sun60i_gpu_stage_fwcompute(...) stage_effect()
#define sun60i_gpu_stage_fwheap(...) stage_effect()
#define sun60i_gpu_stage_fwvm(...) stage_effect()
#define sun60i_gpu_stage_fwkick(s, p) ((s)->sc_kick_done = true, stage_effect())
static struct clk *fdtbus_clock_get_index(int p, int i)
{ CHECK(p == 1 && i == 0); return &clock_object; }
static void clk_put(struct clk *c) { CHECK(c == &clock_object); }
static int sun60i_a733_ccu_gpu_inspect(struct clk *c, struct sun60i_a733_gpu_state *s)
{ CHECK(c == &clock_object); memset(s, 0, sizeof(*s)); return stage_effect(); }
static int fdtbus_get_reg(int phandle, int index, bus_addr_t *addr, bus_size_t *size)
{ CHECK(phandle == 1 && index == 0); *addr = A733_GPU_BASE; *size = A733_GPU_FWSTART_REG_SIZE; return 0; }
static int bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *handle)
{
	CHECK(tag == &sc && addr == A733_GPU_BASE && size == A733_GPU_FWSTART_REG_SIZE && flags == 0);
	CHECK(mutex_owned(&sc.sc_run_lock)); map_calls++;
	*handle = 0x1234;
	return map_error;
}
#include "platform-driver.h"

int
sun60i_a733_gpu_fwservice_attach(struct sun60i_a733_gpu_fwservice *s,
    bus_space_tag_t bst, bus_space_handle_t bsh, struct sun60i_a733_gpu_fwmem *m,
    const struct sun60i_a733_gpu_fwboot_result *b,
    const struct sun60i_a733_gpu_fwprobe_result *previous)
{
	CHECK(mutex_owned(&sc.sc_run_lock));
	CHECK(bst == &sc && bsh == 0x1234 && m == &memory && b == &sc.sc_boot_result);
	attach_calls++; adopted = previous;
	if (service_error != 0) return service_error;
	s->memory = m; s->boot = b; s->bst = bst; s->bsh = bsh; s->queue.attached = true;
	return 0;
}
int
sun60i_a733_gpu_fwservice_alloc(struct sun60i_a733_gpu_fwservice *s,
    size_t size, struct sun60i_a733_gpu_fwobject **out)
{
	struct sun60i_a733_gpu_fwobject *o;
	CHECK(mutex_owned(&sc.sc_run_lock)); alloc_calls++;
	if (operation_error != 0) return operation_error;
	CHECK(size > 0 && size <= 256 && alloc_calls < 4);
	o = &object_pool[alloc_calls]; o->dma.cpu = bytes[alloc_calls];
	o->size = size; o->fw_addr = 0xc0200000 + alloc_calls * 4096;
	o->mapped = true; o->next = s->objects; s->objects = o; *out = o;
	return 0;
}
int
sun60i_a733_gpu_fwservice_free(struct sun60i_a733_gpu_fwservice *s,
    struct sun60i_a733_gpu_fwobject **slot)
{
	struct sun60i_a733_gpu_fwobject **p;
	CHECK(mutex_owned(&sc.sc_run_lock)); free_calls++;
	if (operation_error != 0) { s->queue.failed = true; return operation_error; }
	for (p = &s->objects; *p != *slot; p = &(*p)->next) CHECK(*p != NULL);
	*p = (*slot)->next; (*slot)->mapped = false; *slot = NULL;
	return 0;
}
void
sun60i_a733_gpu_fwservice_sync(struct sun60i_a733_gpu_fwservice *s,
    struct sun60i_a733_gpu_fwobject *o, int ops)
{ CHECK(s == &sc.sc_fwbridge.service && o->mapped && ops != 0); sync_calls++; }
int
sun60i_a733_gpu_fwservice_command(struct sun60i_a733_gpu_fwservice *s,
    const struct rogue_fwif_kccb_cmd *cmd, struct sun60i_a733_gpu_fwprobe_result *r)
{
	CHECK(cmd != NULL && r->after.rtn == 0); command_calls++;
	r->after.rtn = reply;
	if (operation_error != 0 && operation_error != EBUSY && operation_error != EINVAL)
		s->queue.failed = true;
	return operation_error;
}
int sun60i_a733_gpu_fwservice_flush(struct sun60i_a733_gpu_fwservice *s)
{ flush_calls++; if (operation_error != 0) s->queue.failed = true; return operation_error; }
int sun60i_a733_gpu_fwservice_observe(struct sun60i_a733_gpu_fwservice *s,
    struct sun60i_a733_gpu_fwprobe_observation *r)
{ CHECK(s == &sc.sc_fwbridge.service && !r->valid); check_calls++; return operation_error; }
int
sun60i_a733_gpu_fwservice_set_events(struct sun60i_a733_gpu_fwservice *s,
    void *cookie, int (*prepare)(void *, uint32_t, struct rogue_fwif_freelist_gs_data *),
    int (*complete)(void *, uint32_t, int))
{
	CHECK(s == &sc.sc_fwbridge.service && mutex_owned(&sc.sc_run_lock));
	if (cookie == NULL || prepare == NULL || complete == NULL) return EINVAL;
	if (s->events_set) return EBUSY;
	s->events_set = true; s->event_cookie = cookie;
	s->grow_prepare = prepare; s->grow_complete = complete;
	return 0;
}
int
pvr_backend_attach(struct drm_device *d, const struct pvr_backend *b)
{
	CHECK(d == &device && !mutex_owned(&sc.sc_run_lock)); register_calls++;
	CHECK(sc.sc_fwbridge.publishing && sc.sc_fwbridge.attempted);
	if (recursive_publish)
		CHECK(sun60i_a733_gpu_fwbridge_publish(&sc.sc_fwbridge, d) == EBUSY);
	b->enter(b->cookie); CHECK(b->check(b->cookie) == 0); b->leave(b->cookie);
	return register_error;
}
static void
reset(void)
{
	/* Fixture reset represents a new system boot, never a production API. */
	memset(&sc, 0, sizeof(sc)); memset(object_pool, 0, sizeof(object_pool));
	memset(bytes, 0, sizeof(bytes));
	sc.sc_render_device = &device; sc.sc_bst = &sc; sc.sc_phandle = 1;
	sc.sc_fw_memory = &memory; sc.sc_have_id = true; sc.sc_bvnc = A733_GPU_EXPECTED;
	sc.sc_run_stage = A733_GPU_STAGE_FWCACHE; sc.sc_exec_ready = true;
	sc.sc_run_core_on = sc.sc_run_pass = true;
	attach_calls = alloc_calls = free_calls = sync_calls = command_calls = 0;
	flush_calls = check_calls = register_calls = map_calls = provider_calls = 0;
	service_error = register_error = operation_error = map_error = stage_error = 0;
	recursive_publish = false; reply = 1; adopted = NULL;
}
static const struct pvr_backend *
start(void)
{
	CHECK(sun60i_gpu_exec_attach(&sc) == 0);
	CHECK(sc.sc_fwbridge.registered && map_calls == 1 && attach_calls == 1);
	sc.sc_fwbridge.backend.enter(&sc.sc_fwbridge);
	return &sc.sc_fwbridge.backend;
}
static void
lifecycle(void)
{
	unsigned int before;
	for (unsigned int stage = 0; stage <= A733_GPU_STAGE_FWKICK; stage++) {
		reset(); sc.sc_run_stage = stage; sc.sc_kick_done = stage == 21;
		if (stage == 17 || stage == 21) {
			CHECK(sun60i_gpu_exec_attach(&sc) == 0);
			CHECK(adopted == (stage == 17 ? &sc.sc_cache_result : &sc.sc_kick_result));
		} else { CHECK(sun60i_gpu_exec_attach(&sc) == EBUSY); CHECK(map_calls == 0); }
	}
	reset(); recursive_publish = true; CHECK(sun60i_gpu_exec_attach(&sc) == 0);	mutex_enter(&sc.sc_run_lock);
	CHECK(sun60i_a733_gpu_fwbridge_prepare(&sc.sc_fwbridge, &sc.sc_run_lock,
	    &sc, 0x1234, &memory, &sc.sc_boot_result, &sc.sc_cache_result) == EBUSY);
	mutex_exit(&sc.sc_run_lock);

	before = provider_calls; sun60i_gpu_run_stage(&sc, 18);
	CHECK(sc.sc_run_result == EBUSY && provider_calls == before);
	CHECK(sun60i_gpu_exec_attach(&sc) == EBUSY && attach_calls == 1 && register_calls == 1);
	reset(); sc.sc_run_stage = 20; stage_error = ETIMEDOUT;
	sun60i_gpu_run_stage(&sc, 21);
	CHECK(!sc.sc_exec_ready && sc.sc_kick_done && sc.sc_run_result == ETIMEDOUT);
	sun60i_gpu_run_stage(&sc, 0); CHECK(sc.sc_run_result == 0);
	CHECK(sun60i_gpu_exec_attach(&sc) == EBUSY && map_calls == 0);
	reset(); sc.sc_run_stage = 16; sc.sc_exec_ready = false;
	sun60i_gpu_run_stage(&sc, 17); CHECK(sc.sc_exec_ready);
	sun60i_gpu_run_stage(&sc, 18); CHECK(!sc.sc_exec_ready);
	reset(); map_error = ENOMEM; CHECK(sun60i_gpu_exec_attach(&sc) == ENOMEM);
	CHECK(!sc.sc_fwbridge.attempted && attach_calls == 0);
	map_error = 0; CHECK(sun60i_gpu_exec_attach(&sc) == 0);
	reset(); service_error = EIO; CHECK(sun60i_gpu_exec_attach(&sc) == EIO);
	CHECK(sc.sc_fwbridge.attempted && sc.sc_fwbridge.service.queue.failed);
	CHECK(sun60i_gpu_exec_attach(&sc) == EIO && attach_calls == 1 && map_calls == 1);
	reset(); register_error = EALREADY;
	CHECK(sun60i_gpu_exec_attach(&sc) == EALREADY);
	CHECK(sc.sc_fwbridge.service.queue.failed && !sc.sc_fwbridge.registered);
	CHECK(sun60i_gpu_exec_attach(&sc) == EIO && register_calls == 1);
}
static void
objects(void)
{
	struct pvr_fw_object obj = { 0 }, saved, bad;
	const struct pvr_backend *b;
	reset(); b = start();
	operation_error = ENOMEM;
	CHECK(b->alloc(b->cookie, 128, &obj) == ENOMEM && obj.owner == NULL && obj.cpu == NULL);
	operation_error = 0; alloc_calls = 0;
	CHECK(b->alloc(b->cookie, 128, &obj) == 0);
	CHECK(obj.cpu == bytes[1] && obj.owner == &object_pool[1] && obj.size == 128);
	saved = obj;
	CHECK(b->alloc(b->cookie, 128, &obj) == EINVAL && alloc_calls == 1);
	for (unsigned int i = 0; i < 4; i++) {
		bad = obj;
		if (i == 0) bad.owner = (void *)(uintptr_t)1;
		if (i == 1) bad.cpu = (void *)(uintptr_t)1;
		if (i == 2) bad.size++;
		if (i == 3) bad.address++;
		CHECK(b->free(b->cookie, &bad) == EINVAL && free_calls == 0);
	}
	b->sync(b->cookie, &obj, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	b->sync(b->cookie, &obj, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	CHECK(sync_calls == 2);
	CHECK(b->free(b->cookie, &obj) == 0 && obj.owner == NULL && obj.cpu == NULL && obj.size == 0 && obj.address == 0);
	CHECK(b->free(b->cookie, &saved) == EINVAL && free_calls == 1);
	b->leave(b->cookie);
	reset(); b = start(); memset(&obj, 0, sizeof(obj));
	CHECK(b->alloc(b->cookie, 128, &obj) == 0); saved = obj;
	operation_error = ETIMEDOUT;
	CHECK(b->free(b->cookie, &obj) == ETIMEDOUT && memcmp(&obj, &saved, sizeof(obj)) == 0);
	CHECK(b->free(b->cookie, &obj) == EIO && free_calls == 1 && sc.sc_fwbridge.service.objects != NULL);
	b->leave(b->cookie);
	for (unsigned int i = 0; i < 4; i++) {
		reset(); b = start(); memset(&obj, 0, sizeof(obj));
		CHECK(b->alloc(b->cookie, 64, &obj) == 0);
		if (i == 0) obj.owner = (void *)(uintptr_t)1;
		b->sync(b->cookie, &obj, i == 1 ? 0 : i == 2 ? 16 : i == 3 ? 3 : 1);
		CHECK(sc.sc_fwbridge.service.queue.failed && sync_calls == 0);
		b->leave(b->cookie);
	}
}
static void
commands(void)
{
	const struct pvr_backend *b;
	struct rogue_fwif_kccb_cmd cmd = { 0 };
	struct pvr_fw_object obj = { 0 };
	uint32_t rtn;
	reset(); b = start(); reply = 3; operation_error = EBUSY;
	CHECK(b->command(b->cookie, &cmd, &rtn) == EBUSY && rtn == 3 && !sc.sc_fwbridge.service.queue.failed);
	operation_error = EINVAL; reply = 0; rtn = 99;
	CHECK(b->command(b->cookie, &cmd, &rtn) == EINVAL && rtn == 0 && !sc.sc_fwbridge.service.queue.failed);
	operation_error = 0; reply = 1;
	CHECK(b->command(b->cookie, &cmd, &rtn) == 0 && rtn == 1);
	CHECK(b->flush(b->cookie) == 0 && flush_calls == 1);
	CHECK(b->check(b->cookie) == 0 && check_calls == 2);
	operation_error = EIO;
	CHECK(b->check(b->cookie) == EIO && sc.sc_fwbridge.service.queue.failed);
	CHECK(b->alloc(b->cookie, 1, &obj) == EIO && alloc_calls == 0);
	rtn = 99; CHECK(b->command(b->cookie, &cmd, &rtn) == EIO && rtn == 0 && command_calls == 3);
	CHECK(b->flush(b->cookie) == EIO && flush_calls == 1);
	CHECK(b->check(b->cookie) == EIO && check_calls == 3);
	b->leave(b->cookie);
	reset(); b = start(); b->fail(b->cookie);
	CHECK(sc.sc_fwbridge.service.queue.failed); b->leave(b->cookie);
}
static int test_grow(void *c, uint32_t id, struct rogue_fwif_freelist_gs_data *r)
{ (void)c; (void)id; (void)r; return 0; }
static int test_complete(void *c, uint32_t id, int e)
{ (void)c; (void)id; return e; }
static void event_tests(void)
{
	const struct pvr_backend *b;
	struct pvr_fw_events events = {&device, test_grow, test_complete};
	reset(); b = start();
	CHECK(b->set_events != NULL);
	CHECK(b->set_events(b->cookie, NULL) == EINVAL);
	CHECK(b->set_events(b->cookie, &events) == 0);
	CHECK(sc.sc_fwbridge.service.event_cookie == &device);
	CHECK(sc.sc_fwbridge.service.grow_prepare == test_grow);
	CHECK(sc.sc_fwbridge.service.grow_complete == test_complete);
	memset(&events, 0, sizeof(events));
	CHECK(sc.sc_fwbridge.service.grow_prepare == test_grow);
	events = (struct pvr_fw_events){&device, test_grow, test_complete};
	CHECK(b->set_events(b->cookie, &events) == EBUSY);
	b->fail(b->cookie);
	CHECK(b->set_events(b->cookie, &events) == EIO);
	b->leave(b->cookie);
}
int main(void)
{
	lifecycle(); objects(); commands();
	event_tests();
	printf("a733-gpu-fwbridge contract: %u checks passed\n", checks);
	return 0;
}
