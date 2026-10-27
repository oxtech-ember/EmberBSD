/* Origin: EmberBSD; AI-assisted production GPU identity consumer checks. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <time.h>
#include <sys/types.h>
/* Keep provider calls and checks active in release builds. */
#define assert(c) do { \
	if (!(c)) { \
		fprintf(stderr, "Assertion failed: %s at %s:%d\n", #c, __FILE__, __LINE__); \
		abort(); \
	} \
} while (0)
#ifndef EAUTH
#define EAUTH 80
#endif
#include "powervr/pvr_rogue_fwif.h"
#include "sun60i_a733_gpu_fwinfo.h"

typedef unsigned int u_int;
typedef int kmutex_t;
typedef struct device *device_t;
typedef void *cfdata_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef uintptr_t bus_addr_t;
typedef size_t bus_size_t;
typedef int kmutex_t;
typedef void *bus_dma_tag_t;
typedef void *bus_dmamap_t;
typedef struct { bus_addr_t ds_addr; bus_size_t ds_len; } bus_dma_segment_t;
#include "sun60i_a733_gpu_fwimage.h"
#include "sun60i_a733_gpu_fwinit.h"
#include "sun60i_a733_gpu_fwmem.h"
#include "sun60i_a733_gpu_fwstart.h"
#include "sun60i_a733_gpu_fwboot.h"
#include "sun60i_a733_gpu_fwprobe.h"
#include "sun60i_a733_gpu_fwcompute.h"
#include "sun60i_a733_gpu_fwcompute_file.h"
#include "sun60i_a733_gpu_fwheap.h"
#include "sun60i_a733_gpu_fwservice.h"
#include "pvr-backend.h-body.h"
#include "sun60i_a733_gpu_fwbridge.h"
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define device_private(d) ((d)->dv_private)
#define aprint_naive(...) ((void)0)
static void aprint_normal(const char *fmt, ...) {}
static void aprint_debug(const char *fmt, ...) {}
#define printf_nolog(...) ((void)0)
#define KASSERT(c) assert(c)
#define KERNEL_LOCKED_P() true
#define KERNEL_LOCK(n, l) ((void)0)
#define KERNEL_UNLOCK_ONE(l) ((void)0)
#define KM_SLEEP 0
#define AB_QUIET 1
#define AB_SILENT 2
#define AB_VERBOSE 4
#define mstohz(n) (n)
#define device_xname(d) "fixture"
#ifndef __printflike
#define __printflike(a, b)
#endif
struct device {
	device_t dv_parent;
	void *dv_private;
	unsigned int dv_pending;
	TAILQ_ENTRY(device) dv_pending_list;
};
#include "autoconf-structs.h"
TAILQ_HEAD(deferred_config_head, deferred_config);
static struct deferred_config_head deferred_config_queue =
    TAILQ_HEAD_INITIALIZER(deferred_config_queue);
static struct deferred_config_head mountroot_config_queue =
    TAILQ_HEAD_INITIALIZER(mountroot_config_queue);
static bool root_is_mounted;
static unsigned mountroot_exits;
static void kthread_exit(int error) { assert(error == 0); mountroot_exits++; }
static TAILQ_HEAD(, finalize_hook) config_finalize_list =
    TAILQ_HEAD_INITIALIZER(config_finalize_list);
static TAILQ_HEAD(, device) config_pending =
    TAILQ_HEAD_INITIALIZER(config_pending);
static int config_finalize_done, config_misc_lock, config_misc_cv;
static int config_do_twiddle, boothowto;
static const unsigned int hz = 100;
struct pdevinit { void (*pdev_attach)(int); int pdev_count; };
struct pdevinit pdevinit[] = { { NULL, 0 } };
static struct device root_device, soc_device, gpu_device;

static unsigned getticks(void) { return 0; }
static void getnanouptime(struct timespec *t) { t->tv_sec = 0; t->tv_nsec = 0; }
static void delay(unsigned int us) { (void)us; }
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
static unsigned trace_sync_calls;
static bus_addr_t trace_sync_offset;
static bus_size_t trace_sync_size;
static int trace_sync_op;
static struct rogue_fwif_hwrinfobuf *hwr_on_sync;
static uint32_t *scratch_on_sync;
static bus_dmamap_t scratch_sync_map;
static unsigned scratch_sync_calls;
static void bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t m, bus_addr_t o,
    bus_size_t s, int op)
{
	(void)t;
	(void)m;
	trace_sync_offset = o;
	trace_sync_size = s;
	trace_sync_op = op;
	trace_sync_calls++;
	if (hwr_on_sync != NULL)
		hwr_on_sync->hwr_counter = UINT32_C(0x87654321);
	if (m == scratch_sync_map && scratch_sync_map != NULL) {
		scratch_sync_calls++;
		if (scratch_on_sync != NULL)
			*scratch_on_sync = UINT32_C(0x11223344);
	}
}
#ifndef timespecsub
#define timespecsub(tsp, usp, vsp) do { 	(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec; 	(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec; 	if ((vsp)->tv_nsec < 0) { (vsp)->tv_sec--; (vsp)->tv_nsec += 1000000000L; } } while (0)
#endif
static int aprint_get_error_count(void) { return 0; }
/* Stage-18 digest only runs on hardware; the fixture compiles, never digests. */
#define SHA256_DIGEST_LENGTH 32
typedef struct { unsigned unused; } SHA256_CTX;
static int SHA256_Init(SHA256_CTX *c) { (void)c; return 0; }
static int SHA256_Update(SHA256_CTX *c, const uint8_t *data, size_t len)
{ (void)c; (void)data; (void)len; return 0; }
static int SHA256_Final(uint8_t digest[32], SHA256_CTX *c)
{ (void)c; memset(digest, 0, 32); return 0; }
static unsigned kmem_alloc_calls, kmem_free_calls;
static size_t kmem_last_size;
static void *
kmem_alloc(size_t size, int flags)
{
	void *ptr = calloc(1, size);

	assert(ptr != NULL);
	kmem_alloc_calls++;
	kmem_last_size = size;
	return ptr;
}
static void
kmem_free(void *ptr, size_t size)
{
	kmem_free_calls++;
	free(ptr);
}
static void panic(const char *msg) { assert(!msg); }
static void mutex_enter(int *lock) { assert(!*lock); *lock = 1; }
static void mutex_exit(int *lock) { assert(*lock); *lock = 0; }
static int
cv_timedwait(int *cv, int *lock, int ticks)
{
	/* Turn a stranded pending device into a bounded, causal failure. */
	fprintf(stderr, "configuration would wait for an orphaned callback\n");
	abort();
	return 0;
}
static void
config_pending_incr(device_t dev)
{
	if (dev->dv_pending++ == 0)
		TAILQ_INSERT_TAIL(&config_pending, dev, dv_pending_list);
}
static void
config_pending_decr(device_t dev)
{
	assert(dev->dv_pending > 0);
	if (--dev->dv_pending == 0)
		TAILQ_REMOVE(&config_pending, dev, dv_pending_list);
}
struct fdt_attach_args {
	int faa_phandle;
	bus_space_tag_t faa_bst;
	bus_dma_tag_t faa_dmat;
};
static int dma_tag_cookie;
struct device_compatible_entry { const char *compat; };
struct fdtbus_regulator { int acquired; };
struct clk { int acquired; };
#include "sun60i_a733_ccu.h"
#include "sun60i_a733_pck600.h"

static uint32_t pd[2], clocks[2], power_cells, clock_cells;
static int prepare_len = -1, request_len = -1;
static int pd_len, clock_len, managed_len, supply_len, observe_len;
static bool bad_power_compat, bad_pmic, bad_supply;
static bool missing_supply, missing_clock;
static int reg_error, state_error, voltage_error, power_error, ready_error;
static int map_error, peek_error, match_result;
static unsigned inspect_calls;
static int terminal_error, initial_error;
static bool preparation_timed_out;
static bus_addr_t binding_addr;
static bus_size_t binding_size;
static bool supply_on, power_on;
static unsigned supply_calls, fail_supply_call;
static u_int voltage, core_hz, bus_hz;
static uint64_t pbvnc;
static uint32_t core_id;
static int coreid_peek_error;
static struct fdtbus_regulator regulator;
static struct clk clock_fixture;
static struct sun60i_a733_gpu_state clock_observation;
static struct sun60i_a733_gpu_state terminal_observation;
static struct sun60i_a733_gpu_state initial_observation;
static unsigned int maps, peeks, unmaps, releases, clock_puts, checks;
static char events[512], normal_output[4096], error_output[256];
static size_t nevents;
static int full_map_error;
static unsigned full_maps, full_unmaps;
static bool full_mapping, fixture_device_owned;
static bool later_publication;
static const char *later_memory_state, *later_interface_state, *later_boot_state;
static char later_memory_before[256], later_interface_before[256];
static char later_boot_before[512], later_probe_before[1024];
static const char *later_probe_state, *later_cache_state;
static char later_cache_before[1024];
static int *consumer_lock;
static bool native_fixture, native_identify_error;
static int native_render_error, native_prepare_error, native_publish_error;
static unsigned native_renders, native_prepares, native_publishes;
static char native_render_cookie;

static void
later_not_published(void)
{
	if (!later_publication)
		return;
	assert(strcmp(later_memory_state, later_memory_before) == 0);
	assert(strcmp(later_interface_state, later_interface_before) == 0);
	assert(strcmp(later_boot_state, later_boot_before) == 0);
	assert(strcmp(later_probe_state, later_probe_before) == 0);
	assert(strcmp(later_cache_state, later_cache_before) == 0);
}

static void
record(char event)
{
	assert(nevents + 1 < sizeof(events));
	events[nevents++] = event;
	events[nevents] = '\0';
}

static uint32_t
swap32(uint32_t value)
{
	const uint32_t one = 1;

	return *(const unsigned char *)&one == 1 ? __builtin_bswap32(value) : value;
}
#ifndef be32toh
#define be32toh(v) swap32(v)
#endif

static int
OF_getproplen(int phandle, const char *name)
{
	if (strcmp(name, "netbsd,experimental-clock-prepare") == 0)
		return prepare_len;
	if (strcmp(name, "netbsd,experimental-domain-request") == 0)
		return request_len;
	if (strcmp(name, "netbsd,observe-only") == 0)
		return observe_len;
	if (strcmp(name, "netbsd,consumer-managed-power") == 0)
		return managed_len;
	assert(strcmp(name, "gpu-supply") == 0);
	return supply_len;
}

static int
fdtbus_get_reg(int phandle, u_int index, bus_addr_t *addr, bus_size_t *size)
{
	assert(index == 0);
	*addr = binding_addr;
	*size = binding_size;
	return reg_error;
}

static const void *
fdtbus_get_prop(int phandle, const char *name, int *len)
{
	if (strcmp(name, "power-domains") == 0) {
		*len = pd_len;
		return pd_len < 0 ? NULL : pd;
	}
	assert(strcmp(name, "clocks") == 0);
	*len = clock_len;
	return clock_len < 0 ? NULL : clocks;
}

static int fdtbus_get_phandle_from_native(int phandle) { return phandle; }
static int
of_getprop_uint32(int phandle, const char *name, uint32_t *value)
{
	if (strcmp(name, "#power-domain-cells") == 0) {
		*value = power_cells;
		return phandle == 2 ? 0 : -1;
	}
	assert(strcmp(name, "#clock-cells") == 0);
	*value = clock_cells;
	return phandle == 3 ? 0 : -1;
}

static int
of_compatible_match(int phandle, const struct device_compatible_entry *compat)
{
	if (strcmp(compat[0].compat, "allwinner,sun60i-a733-gpu") == 0)
		return match_result;
	if (strcmp(compat[0].compat, "allwinner,sun60i-a733-pck-600") == 0)
		return phandle == 2 && !bad_power_compat;
	assert(strcmp(compat[0].compat, "x-powers,axp8191") == 0);
	return phandle == 6 && !bad_pmic;
}

static int
fdtbus_get_phandle(int phandle, const char *name)
{
	assert(strcmp(name, "gpu-supply") == 0);
	return 4;
}
static const char *
fdtbus_get_string(int phandle, const char *name)
{
	assert(phandle == 4 && strcmp(name, "name") == 0);
	return bad_supply ? "dcdc2" : "dcdc4";
}
static int OF_parent(int phandle) { return phandle + 1; }

static struct fdtbus_regulator *
fdtbus_regulator_acquire(int phandle, const char *name)
{
	record('A');
	assert(!regulator.acquired);
	if (missing_supply)
		return NULL;
	regulator.acquired = 1;
	return &regulator;
}
static int
fdtbus_regulator_is_enabled(struct fdtbus_regulator *reg, bool *enabled)
{
	assert(reg->acquired);
	record('S');
	if (++supply_calls == fail_supply_call)
		return EIO;
	*enabled = supply_on;
	return state_error;
}
static int
fdtbus_regulator_get_voltage(struct fdtbus_regulator *reg, u_int *uvol)
{
	assert(reg->acquired);
	record('V');
	*uvol = voltage;
	return voltage_error;
}
static int
fdtbus_powerdomain_is_enabled_index(int phandle, int index, bool *enabled)
{
	assert(index == 0);
	record('P');
	*enabled = power_on;
	return power_error;
}
static struct clk *
fdtbus_clock_get_index(int phandle, u_int index)
{
	assert(index == 0 && !clock_fixture.acquired);
	record('C');
	if (missing_clock)
		return NULL;
	clock_fixture.acquired = 1;
	return &clock_fixture;
}
int
sun60i_a733_ccu_gpu_inspect(struct clk *clock,
    struct sun60i_a733_gpu_state *state)
{
	assert(clock->acquired);
	record('Q');
	inspect_calls++;
	if (prepare_len == 0 && inspect_calls == 1) {
		if (initial_error != 0)
			return initial_error;
		*state = initial_observation;
		return 0;
	}
	if (prepare_len == 0 &&
	    (inspect_calls > 2 || preparation_timed_out)) {
		if (terminal_error != 0)
			return terminal_error;
		*state = terminal_observation;
		return 0;
	}
	if (ready_error == 0) {
		*state = clock_observation;
		state->core_hz = core_hz;
		state->bus_hz = bus_hz;
	}
	return ready_error;
}
static int
bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *handle)
{
	assert(addr == 0x01800000 && flags == 0);
	if (size == A733_GPU_FWSTART_REG_SIZE) {
		assert(!full_mapping);
		full_maps++;
		record('X');
		if (full_map_error != 0)
			return full_map_error;
		full_mapping = true;
		*handle = 0x5678;
		return 0;
	}
	assert(size == 0x28);
	record('M');
	maps++;
	*handle = 0x1234;
	return map_error;
}
static int
bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint32_t *value)
{
	assert(handle == 0x1234 && offset == 0x18);
	record('i');
	*value = core_id;
	return coreid_peek_error;
}
static int
bus_space_peek_8(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint64_t *value)
{
	assert(handle == 0x1234 && offset == 0x20);
	record('I');
	peeks++;
	if (native_identify_error && peeks == 2)
		return EFAULT;
	*value = pbvnc;
	return peek_error;
}
static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t size)
{
	if (size == A733_GPU_FWSTART_REG_SIZE) {
		assert(handle == 0x5678 && full_mapping);
		assert(consumer_lock != NULL && *consumer_lock);
		later_not_published();
		full_mapping = false;
		full_unmaps++;
		record('Z');
		return;
	}
	assert(handle == 0x1234 && size == 0x28);
	record('U');
	unmaps++;
}
static void
clk_put(struct clk *clock)
{
	assert(clock->acquired);
	clock->acquired = 0;
	clock_puts++;
	record('c');
}
static void
fdtbus_regulator_release(struct fdtbus_regulator *reg)
{
	assert(reg->acquired);
	reg->acquired = 0;
	releases++;
	record('s');
}
static void
aprint_normal_dev(device_t dev, const char *fmt, ...)
{
	va_list ap;
	size_t len = strlen(normal_output);
	int written;

	va_start(ap, fmt);
	written = vsnprintf(normal_output + len, sizeof(normal_output) - len,
	    fmt, ap);
	va_end(ap);
	assert(written >= 0 && (size_t)written < sizeof(normal_output) - len);
}
static void
aprint_error_dev(device_t dev, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(error_output, sizeof(error_output), fmt, ap);
	va_end(ap);
}
/* Runtime staging stubs: the sysctl tree itself is not under test. */
struct sysctllog;
struct sysctlnode { void *sysctl_data; };
#define SYSCTLFN_ARGS struct sysctllog *log, void *sname, void *nam, \
	struct sysctlnode *rnode, const void *newp, size_t newlen
#define SYSCTLFN_CALL(rn) log, sname, nam, (rn), newp, newlen
static int
sysctl_lookup(SYSCTLFN_ARGS)
{
	if (newp != NULL) {
		assert(newlen == sizeof(unsigned));
		memcpy(rnode->sysctl_data, newp, newlen);
	}
	return 0;
}
static int sysctl_createv_stub(void *first, ...) { (void)first; return 0; }
#define sysctl_createv(...) sysctl_createv_stub(__VA_ARGS__)

typedef int32_t __s32;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
#include "pvr-drm.h-body.h"
#include "pvr-query.h-body.h"

/* The separate render-attach contract owns these new handler paths. */
struct drm_device;
static int
pvr_render_attach(device_t dev, bus_dma_tag_t dmat, uint64_t bvnc,
    const struct pvr_query_input *input, struct drm_device **owner)
{

	assert(native_fixture && consumer_lock != NULL && !*consumer_lock);
	assert(bvnc == UINT64_C(0x00240038006800b7) && input->bvnc == bvnc);
	assert(native_prepares == 0 && native_publishes == 0);
	native_renders++;
	if (native_render_error == 0)
		*owner = (struct drm_device *)&native_render_cookie;
	return native_render_error;
}
static void sysctl_unlock(void) { abort(); }
static void sysctl_relock(void) { abort(); }
#define SYSCTL_DESCR(s) (s)
#define CTLTYPE_NODE 1
#define CTLTYPE_INT 2
#define CTLTYPE_QUAD 3
#define CTLTYPE_STRING 5
#define CTLFLAG_READWRITE 4
#define CTLFLAG_READONLY 8
#define CTL_HW 16
#define CTL_CREATE 32
#define CTL_EOL -1
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
static void mutex_init(kmutex_t *m, int type, int ipl) { *m = 0; }
#define mutex_destroy(m) ((void)0)
#include "autoconf.h"
static int prepare_error, reserve_error, wait_error, retain_error;
static int request_error;
static unsigned pll_prepares;
static int power_reserve_error, second_wait_error, final_ready_error;static unsigned prepares, reserves, waits, retains, unreserves, requests;
int sun60i_a733_ccu_gpu_reserve(struct clk *c, const void *owner)
{ reserves++; record('C'); return reserve_error; }
int sun60i_a733_pck_gpu_reserve(int node, const void *owner)
{ reserves++; record('P'); return power_reserve_error; }
int sun60i_a733_ccu_gpu_release(struct clk *c, const void *owner)
{ unreserves++; record('c'); return 0; }
int sun60i_a733_pck_gpu_release(int node, const void *owner)
{ unreserves++; record('p'); return 0; }
int sun60i_a733_pck_gpu_retain(int node, const void *owner)
{ retains++; record('H'); return retain_error; }
int sun60i_a733_pck_gpu_request_on(int node, const void *owner, bool quiet)
{ requests++; record('R'); return request_error; }
int sun60i_a733_ccu_gpu_prepare(struct clk *c, const void *owner, bool *held)
{
	prepares++; record('W'); *held = true;
	preparation_timed_out = prepare_error == ETIMEDOUT;
	return prepare_error;
}
int sun60i_a733_ccu_gpu_prepare_pll(struct clk *c, const void *owner, bool *held)
{ pll_prepares++; record('L'); *held = true; return prepare_error; }
int sun60i_a733_pck_gpu_wait(int node, const void *owner, bool quiet)
{ waits++; record('K'); return waits == 2 ? second_wait_error : wait_error; }
int sun60i_a733_ccu_gpu_ready(struct clk *c, u_int *core, u_int *bus)
{ record('F'); *core = core_hz; *bus = bus_hz; return final_ready_error; }
/* Exercise firmware I/O failures with the real metadata parser. */
struct firmware_handle { bool open; };
typedef struct firmware_handle *firmware_handle_t;
static struct firmware_handle fw_handle;
static uint8_t *compute_file_data;
static size_t compute_file_size;
static struct sun60i_a733_gpu_dma *compute_scratch_dma;
static bool compute_scratch_unavailable;
static unsigned compute_completion_mode, compute_observe_calls;
static int fw_open_error, fw_read_error, fw_close_error;
static bool fw_alloc_fail, fw_bad_bvnc;
static off_t fw_file_size = 8192;
static void *fw_memory;
static size_t fw_memory_size;
static unsigned fw_opens, fw_sizes, fw_allocs, fw_reads, fw_parses;
static unsigned fw_closes, fw_frees;
static const char *fw_cached_state;
static bool fw_strict_publication;
static char fw_before_state[256];
static struct sun60i_a733_gpu_fwmem **fw_public_memory;
static struct sun60i_a733_gpu_fwmem *fw_expected_memory;
static struct sun60i_a733_gpu_fwmem *fwmem_live;
static int fwmem_prepare_error;
static unsigned fwmem_prepares, fwmem_destroys, fwmem_null_destroys;
static unsigned fw_interfaces, fw_boots;
static int fw_interface_error, fw_boot_error;
static uint64_t expected_interface_hz;
static uint32_t expected_clusters, expected_slc;
static struct sun60i_a733_gpu_fwboot_result boot_reply;
static struct sun60i_a733_gpu_fwprobe_result probe_reply;
static unsigned fw_probes;
static int fw_probe_error;
static struct sun60i_a733_gpu_fwprobe_result cache_reply;
static unsigned fw_caches;
static int fw_cache_error;

/* Independent buffer storage makes accidental consumer writes observable. */
static void
fixture_dma(struct sun60i_a733_gpu_dma *dma, size_t size, int fill)
{
	dma->size = size;
	dma->cpu = malloc(size);
	assert(dma->cpu != NULL);
	memset(dma->cpu, fill, size);
}

static void
fw_not_published(void)
{
	if (fw_strict_publication) {
		assert(strcmp(fw_cached_state, fw_before_state) == 0);
		assert(*fw_public_memory == fw_expected_memory);
	}
	if (fw_cached_state != NULL)
		assert(strstr(fw_cached_state, "error=0") == NULL);
}

static void
fw_put32(uint8_t *p, uint32_t value)
{
	for (unsigned n = 0; n < 4; n++)
		p[n] = (uint8_t)(value >> (8 * n));
}

static void
fw_put64(uint8_t *p, uint64_t value)
{
	fw_put32(p, (uint32_t)value);
	fw_put32(p + 4, (uint32_t)(value >> 32));
}

static void
fw_valid_metadata(uint8_t *bytes, size_t size)
{
	uint8_t *info = bytes + size - 4096;
	uint8_t *device = info - 128;
	uint8_t *layout = info + 48;

	memset(bytes, 0, size);
	fw_put32(info, 3);
	fw_put32(info + 4, 48);
	fw_put32(info + 8, 1);
	fw_put32(info + 12, 24);
	fw_put64(info + 16, UINT64_C(0x00240038006800b7) ^ fw_bad_bvnc);
	fw_put32(info + 24, 4096);
	fw_put32(info + 28, 1);
	info[32] = info[34] = 1;
	fw_put32(info + 36, 1);
	fw_put32(info + 40, 128);
	fw_put32(layout, 4);
	fw_put32(layout + 4, 1);
	fw_put32(layout + 8, UINT32_C(0xc0000000));
	fw_put32(layout + 12, 4096);
	fw_put32(layout + 16, 4096);
	fw_put64(device + 16, 1);
	fw_put64(device + 32, UINT64_C(1) << 27);
}

static int
firmware_open(const char *driver, const char *name, firmware_handle_t *fh)
{
	assert(strcmp(driver, "powervr") == 0);
	assert(!native_fixture || root_is_mounted);
	if (compute_file_data != NULL) {
		assert(strcmp(name, "a733-compute.bin") == 0 && !fw_handle.open);
		fw_handle.open = true;
		*fh = &fw_handle;
		return 0;
	}
	assert(strcmp(name, "rogue_36.56.104.183_v1.fw") == 0);
	assert(!fw_handle.open);
	fw_not_published();
	fw_opens++;
	record('o');
	if (fw_open_error != 0)
		return fw_open_error;
	fw_handle.open = true;
	*fh = &fw_handle;
	return 0;
}

static off_t
firmware_get_size(firmware_handle_t fh)
{
	assert(fh == &fw_handle && fh->open);
	if (compute_file_data != NULL)
		return (off_t)compute_file_size;
	fw_sizes++;
	record('z');
	return fw_file_size;
}

static void *
firmware_malloc(size_t size)
{
	assert(fw_handle.open && fw_memory == NULL);
	assert(size >= 4096 && size <= 1024 * 1024);
	fw_allocs++;
	record('a');
	if (fw_alloc_fail)
		return NULL;
	fw_memory = malloc(size);
	assert(fw_memory != NULL);
	fw_memory_size = size;
	return fw_memory;
}

static int
firmware_read(firmware_handle_t fh, off_t offset, void *bytes, size_t size)
{
	assert(fh == &fw_handle && fh->open && offset == 0);
	if (compute_file_data != NULL) {
		assert(size == compute_file_size);
		memcpy(bytes, compute_file_data, size);
		return 0;
	}
	assert(bytes == fw_memory && size == fw_memory_size);
	fw_not_published();
	fw_reads++;
	record('r');
	if (fw_read_error != 0)
		return fw_read_error;
	if (size >= 8192)
		fw_valid_metadata(bytes, size);
	else
		memset(bytes, 0, size);
	return 0;
}

static int
fw_parse(const void *bytes, size_t size, uint64_t expected,
    struct sun60i_a733_gpu_fwinfo *info)
{
	assert(fw_handle.open && bytes == fw_memory && size == fw_memory_size);
	assert(fw_reads == 1 && fw_closes == 0);
	fw_not_published();
	fw_parses++;
	record('v');
	return sun60i_a733_gpu_fwinfo_parse(bytes, size, expected, info);
}

static int
firmware_close(firmware_handle_t fh)
{
	assert(fh == &fw_handle && fh->open);
	if (compute_file_data != NULL) {
		fh->open = false;
		return 0;
	}
	fw_not_published();
	fw_closes++;
	record('c');
	fh->open = false;
	return fw_close_error;
}

static void
firmware_free(void *bytes, size_t size)
{
	assert(!fw_handle.open && bytes == fw_memory && size == fw_memory_size);
	fw_not_published();
	fw_frees++;
	record('f');
	free(bytes);
	fw_memory = NULL;
	fw_memory_size = 0;
}

/*
 * Explicit consumer boundary. The real fwmem implementation has a separate
 * production-source contract. These stubs test calls, ownership, and ordering;
 * they do not claim to validate ELF loading, bus DMA, or page-table contents.
 */
int
sun60i_a733_gpu_fwmem_prepare(bus_dma_tag_t tag, const void *bytes, size_t size,
    uint64_t expected, struct sun60i_a733_gpu_fwmem **out)
{
	assert(tag == &dma_tag_cookie);
	assert(bytes == fw_memory && size == fw_memory_size);
	assert(expected == UINT64_C(0x00240038006800b7));
	assert(fw_handle.open);
	assert(fw_reads == (native_fixture ? 2U : 1U));
	assert(fw_closes == (native_fixture ? 1U : 0U));
	assert(out != NULL && *out == NULL && fwmem_live == NULL);
	fw_not_published();
	fwmem_prepares++;
	record('m');
	if (fwmem_prepare_error != 0)
		return fwmem_prepare_error;
	fwmem_live = calloc(1, sizeof(*fwmem_live));
	assert(fwmem_live != NULL);
	fwmem_live->dmat = tag;
	fwmem_live->code.size = 102400;
	fwmem_live->data.size = 20480;
	fwmem_live->table.size = 16384;
	fixture_dma(&fwmem_live->code, 102400, 0x31);
	fixture_dma(&fwmem_live->data, 20480, 0x42);
	fixture_dma(&fwmem_live->table, 16384, 0x53);
	fwmem_live->image.info.bvnc = expected;
	fwmem_live->image.info.feature_mask[0] =
	    (UINT64_C(1) << A733_GPU_FW_FEATURE_NUM_CLUSTERS) |
	    (UINT64_C(1) << A733_GPU_FW_FEATURE_SLC_SIZE_IN_KILOBYTES);
	fwmem_live->image.info.feature_value[A733_GPU_FW_FEATURE_NUM_CLUSTERS] = 2;
	fwmem_live->image.info.feature_value[
	    A733_GPU_FW_FEATURE_SLC_SIZE_IN_KILOBYTES] = 16;
	if (native_fixture) {
		/* Validated feature metadata for the real query-input converter. */
		fwmem_live->image.info.feature_mask[0] = UINT64_MAX;
		fwmem_live->image.info.feature_mask[1] = UINT64_MAX;
	}
	fwmem_live->image.load_count = 5;
	fwmem_live->image.entry = UINT32_C(0xbfc00001);
	fwmem_live->boot_code = UINT32_C(0x20000000);
	fwmem_live->stack = UINT32_C(0x30004000);
	*out = fwmem_live;
	return 0;
}

void
sun60i_a733_gpu_fwmem_destroy(struct sun60i_a733_gpu_fwmem *mem)
{
	fw_not_published();
	/* Both firmware resources must be released before error cleanup. */
	assert(!fw_handle.open && fw_memory == NULL);
	if (mem == NULL) {
		fwmem_null_destroys++;
		return;
	}
	assert(mem == fwmem_live && !fixture_device_owned);
	free(mem->code.cpu);
	free(mem->data.cpu);
	free(mem->table.cpu);
	free(mem->work.cpu);
	free(mem->config.cpu);
	fwmem_destroys++;
	record('d');
	free(mem);
	fwmem_live = NULL;
}

/* Real helper contracts run separately. These are explicit consumer boundaries. */
int
sun60i_a733_gpu_fwmem_interface(struct sun60i_a733_gpu_fwmem *mem,
    uint64_t clock_rate, uint32_t clusters, uint32_t slc_kib)
{
	assert(mem == fwmem_live && !fixture_device_owned);
	assert(consumer_lock != NULL && *consumer_lock);
	assert(clock_rate == expected_interface_hz);
	assert(clusters == expected_clusters && slc_kib == expected_slc);
	assert(!fw_handle.open && fw_memory == NULL);
	later_not_published();
	fw_interfaces++;
	record('J');
	if (fw_interface_error != 0)
		return fw_interface_error;
	assert(!mem->interface_prepared);
	fixture_dma(&mem->work, 139264, 0x64);
	fixture_dma(&mem->config, 12288, 0x75);
	mem->interface_layout.total_work_size = 139264;
	mem->interface_prepared = true;
	return 0;
}

int
sun60i_a733_gpu_fwboot(bus_space_tag_t tag, bus_space_handle_t handle,
    struct sun60i_a733_gpu_fwmem *mem,
    struct sun60i_a733_gpu_fwboot_result *out)
{
	assert(handle == 0x5678 && full_mapping);
	assert(mem == fwmem_live && mem->interface_prepared);
	assert(consumer_lock != NULL && *consumer_lock);
	assert(!out->attempted && !out->device_owned && !fixture_device_owned);
	assert(!fw_handle.open && fw_memory == NULL);
	later_not_published();
	fw_boots++;
	record('B');
	*out = boot_reply;
	fixture_device_owned = out->device_owned;
	return fw_boot_error;
}

int
sun60i_a733_gpu_fwprobe(bus_space_tag_t tag, bus_space_handle_t handle,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	assert(handle == 0x5678 && full_mapping);
	assert(mem == fwmem_live && mem->interface_prepared && fixture_device_owned);
	assert(consumer_lock != NULL && *consumer_lock);
	assert(boot->attempted && boot->device_owned && boot->error == 0);
	assert(boot->start.touched && boot->start.released);
	assert(boot->snapshot_valid && boot->firmware_started == 1);
	assert(!out->attempted && !out->published && !out->kick_attempted && !out->kicked);
	assert(!fw_handle.open && fw_memory == NULL);
	later_not_published();
	fw_probes++;
	record('L');
	*out = probe_reply;
	return fw_probe_error;
}

/* Stages 19-21 publish caller-built KCCB commands on real hardware only. */
static unsigned fwprobe_cmd_calls;
int
sun60i_a733_gpu_fwprobe_cmd(bus_space_tag_t tag, bus_space_handle_t handle,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *previous,
    const struct rogue_fwif_kccb_cmd *cmd, unsigned int slot,
    uint32_t expected_log_type,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	fwprobe_cmd_calls++;
	assert(cmd != NULL && slot >= 2 && previous != NULL);
	if (compute_file_data != NULL) {
		assert(slot == 3 || slot == 4);
		if (slot == 3)
			return 0;
		assert(compute_scratch_dma != NULL);
		scratch_on_sync = compute_scratch_dma->cpu;
		if (compute_scratch_unavailable)
			compute_scratch_dma->loaded = false;
		if (compute_completion_mode != 0) {
			out->attempted = out->published = out->kick_attempted = true;
			out->kicked = true;
			out->slot = 4;
			out->after.rtn = 1;
			return 0;
		}
		return EIO;
	}
	return ENOSYS;
}

int
sun60i_a733_gpu_fwprobe_observe(struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot, unsigned int slot,
    uint32_t command, uint32_t log_type,
    struct sun60i_a733_gpu_fwprobe_observation *out)
{
	assert(compute_completion_mode != 0 && slot == 4 && log_type == 0x3f);
	assert(command == ROGUE_FWIF_KCCB_CMD_KICK);
	compute_observe_calls++;
	memset(out, 0, sizeof(*out));
	out->valid = true;
	out->state.rtn = 1;
	if (compute_completion_mode == 2 ||
	    (compute_completion_mode == 3 && compute_observe_calls == 2)) {
		if (compute_completion_mode == 3)
			assert(scratch_sync_calls == 2);
		out->state.fw_write_offset = 1;
		out->state.fw_command = 0x2abc0069;
		out->error = EIO;
	}
	return out->error;
}

/* The submission provider allocates device memory; never in this fixture. */
static unsigned fwheap_calls, fwvm_calls;
int
sun60i_a733_gpu_fwheap_prepare(bus_dma_tag_t tag,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwcompute_image *image,
    struct sun60i_a733_gpu_fwheap **out)
{
	fwheap_calls++;
	return ENOSYS;
}

int
sun60i_a733_gpu_fwheap_map(struct sun60i_a733_gpu_fwheap *heap,
    struct sun60i_a733_gpu_fwmem *mem)
{
	return ENOSYS;
}

int
sun60i_a733_gpu_fwheap_set_vm_context(struct sun60i_a733_gpu_fwheap *heap,
    const uint8_t context[32])
{
	return ENOSYS;
}

void
sun60i_a733_gpu_fwheap_observe(const struct sun60i_a733_gpu_fwheap *heap,
    uint32_t *read_offset, uint32_t *write_offset, uint32_t *timeline)
{
	bool completed = compute_completion_mode != 0 && compute_completion_mode != 4;
	uint32_t offset = completed ? A733_GPU_FWCOMPUTE_USED_BYTES : 0;

	if (read_offset != NULL)
		*read_offset = offset;
	if (write_offset != NULL)
		*write_offset = offset;
	if (timeline != NULL)
		*timeline = completed;
}

void
sun60i_a733_gpu_fwheap_destroy(struct sun60i_a733_gpu_fwheap **out)
{
}

int
sun60i_a733_gpu_fwvm_prepare(bus_dma_tag_t tag,
    const struct a733_gpu_fwcompute_file *file, const uint8_t *bytes,
    struct sun60i_a733_gpu_fwheap *heap,
    struct sun60i_a733_gpu_fwvm **out)
{
	fwvm_calls++;
	return ENOSYS;
}

void
sun60i_a733_gpu_fwvm_destroy(struct sun60i_a733_gpu_fwvm **out)
{
}

int
sun60i_a733_gpu_fwprobe_mmucache(bus_space_tag_t tag, bus_space_handle_t handle,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *probe,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	assert(handle == 0x5678 && full_mapping);
	assert(mem == fwmem_live && mem->interface_prepared && fixture_device_owned);
	assert(consumer_lock != NULL && *consumer_lock);
	assert(boot->attempted && boot->device_owned && boot->error == 0);
	assert(boot->start.touched && boot->start.released);
	assert(boot->snapshot_valid && boot->firmware_started == 1);
	assert(probe != out && memcmp(probe, &probe_reply, sizeof(*probe)) == 0);
	assert(probe->attempted && probe->published && probe->kicked);
	assert(probe->command == A733_GPU_FWPROBE_LOGTYPE && probe->error == 0);
	assert(probe->slot == 0 && probe->after.rtn == 1);
	assert(probe->after.read_offset == 1 && probe->after.write_offset == 1);
	assert(!out->attempted && !out->published && !out->kick_attempted && !out->kicked);
	assert(!fw_handle.open && fw_memory == NULL);
	later_not_published();
	fw_caches++;
	record('T');
	*out = cache_reply;
	return fw_cache_error;
}

#define sun60i_a733_gpu_fwinfo_parse fw_parse
/* The native-startup fixture checks this provider boundary and call order.
 * The separate bridge contract executes the real adoption implementation. */
int
sun60i_a733_gpu_fwbridge_prepare(struct sun60i_a733_gpu_fwbridge *bridge,
    kmutex_t *lock, bus_space_tag_t tag, bus_space_handle_t handle,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *probe)
{
	assert(native_fixture && consumer_lock == lock && *lock);
	assert(native_renders == 1 && native_prepares == 0 && native_publishes == 0);
	assert(full_mapping && handle == 0x5678 && mem == fwmem_live);
	assert(fixture_device_owned && mem->interface_prepared);
	assert(boot->firmware_started == 1 && probe->command == A733_GPU_FWPROBE_MMUCACHE);
	native_prepares++;
	bridge->attempted = true;
	return native_prepare_error;
}

int
sun60i_a733_gpu_fwbridge_publish(struct sun60i_a733_gpu_fwbridge *bridge,
    struct drm_device *device)
{
	assert(native_fixture && consumer_lock != NULL && !*consumer_lock);
	assert(native_renders == 1 && native_prepares == 1 && native_publishes == 0);
	assert(bridge->attempted && device == (struct drm_device *)&native_render_cookie);
	native_publishes++;
	bridge->registered = native_publish_error == 0;
	return native_publish_error;
}

#include "driver.h"
#undef sun60i_a733_gpu_fwinfo_parse
#define CHECK(c) do { assert(c); checks++; } while (0)
static struct sun60i_gpu_softc sc;

static void
reset(void)
{
	assert(!regulator.acquired && !clock_fixture.acquired);
	assert(!fw_handle.open && fw_memory == NULL);
	assert(fwmem_live == NULL);
	assert(TAILQ_EMPTY(&config_pending));
	assert(TAILQ_EMPTY(&deferred_config_queue));
	assert(TAILQ_EMPTY(&config_finalize_list));
	assert(TAILQ_EMPTY(&mountroot_config_queue));
	root_is_mounted = false;
	mountroot_exits = 0;
	native_fixture = native_identify_error = false;
	native_render_error = native_prepare_error = native_publish_error = 0;
	native_renders = native_prepares = native_publishes = 0;
	memset(&sc, 0, sizeof(sc));
	root_device = (struct device){ 0 };
	soc_device = (struct device){ .dv_parent = &root_device };
	gpu_device = (struct device){ .dv_parent = &soc_device, .dv_private = &sc };
	config_finalize_done = 0;
	sc.sc_dev = &gpu_device;
	sc.sc_phandle = 1;
	sc.sc_dmat = &dma_tag_cookie;
	sc.sc_run_pass = true;
	sc.sc_diagnostic = true;
	fw_open_error = fw_read_error = fw_close_error = 0;
	fw_alloc_fail = fw_bad_bvnc = false;
	fw_file_size = 8192;
	fw_opens = fw_sizes = fw_allocs = fw_reads = fw_parses = 0;
	fw_closes = fw_frees = 0;
	fw_cached_state = sc.sc_fw_state;
	fw_strict_publication = false;
	fw_public_memory = &sc.sc_fw_memory;
	fw_expected_memory = NULL;
	fwmem_prepare_error = 0;
	fwmem_prepares = fwmem_destroys = fwmem_null_destroys = 0;
	fw_interfaces = fw_boots = fw_probes = fw_caches = 0;
	fw_interface_error = fw_boot_error = full_map_error = fw_probe_error = 0;
	fw_cache_error = 0;
	full_maps = full_unmaps = 0;
	assert(!full_mapping && !fixture_device_owned);
	later_publication = false;
	consumer_lock = &sc.sc_run_lock;
	expected_interface_hz = 400000000;
	expected_clusters = 2;
	expected_slc = 16;
	memset(&boot_reply, 0, sizeof(boot_reply));
	memset(&probe_reply, 0, sizeof(probe_reply));
	memset(&cache_reply, 0, sizeof(cache_reply));
	pd[0] = swap32(2);
	pd[1] = swap32(5);
	clocks[0] = swap32(3);
	clocks[1] = swap32(A733_CLK_GPU0);
	power_cells = clock_cells = 1;
	pd_len = clock_len = 8;
	managed_len = 0;
	observe_len = prepare_len = request_len = -1;
	prepare_error = reserve_error = wait_error = retain_error = 0;
	request_error = 0;
	pll_prepares = 0;
	power_reserve_error = second_wait_error = final_ready_error = 0;
	inspect_calls = 0; terminal_error = initial_error = 0;
	preparation_timed_out = false;
	prepares = reserves = waits = retains = unreserves = requests = 0;
	supply_calls = fail_supply_call = 0;
	supply_len = 4;
	bad_power_compat = bad_pmic = bad_supply = false;
	missing_supply = missing_clock = false;
	reg_error = state_error = voltage_error = power_error = ready_error = 0;
	map_error = peek_error = 0;
	match_result = 1;
	binding_addr = 0x01800000;
	binding_size = 0x8ffff;
	supply_on = power_on = true;
	voltage = 800000;
	core_hz = 400000000;
	bus_hz = 200000000;
	memset(&clock_observation, 0, sizeof(clock_observation));
	memset(&terminal_observation, 0, sizeof(terminal_observation));
	memset(&initial_observation, 0, sizeof(initial_observation));
	clock_observation.hosc_hz[0] = clock_observation.hosc_hz[1] = 24000000;
	pbvnc = UINT64_C(0x00240038006800b7);
	core_id = 0;
	coreid_peek_error = 0;
	maps = peeks = unmaps = releases = clock_puts = 0;
	nevents = 0;
	events[0] = normal_output[0] = error_output[0] = '\0';
}

static unsigned legacy_callbacks, extra_passes;
static void legacy_probe(device_t dev) { legacy_callbacks++; }
static int request_extra_pass(device_t dev) { return extra_passes++ == 0; }

static void
unavailable(int error)
{
	CHECK(sun60i_gpu_identify(&sc) == error);
	CHECK(maps == 0 && peeks == 0 && unmaps == 0);
	CHECK(!sc.sc_have_id);
	CHECK(!regulator.acquired && !clock_fixture.acquired);
}

static void
test_clock_observation(void)
{
	static const struct {
		enum sun60i_a733_gpu_reason reason;
		int error;
		const char *text;
	} failures[] = {
		{ A733_GPU_SNAPSHOT_CHANGED, EBUSY, "CCU snapshot changed" },
		{ A733_GPU_HOSC_CHANGED, EBUSY, "oscillator rate changed" },
		{ A733_GPU_DCXO_CHANGED, EBUSY, "DCXO status changed" },
		{ A733_GPU_MODULE_GATED, EBUSY, "GPU module gated" },
		{ A733_GPU_UPDATE_PENDING, EBUSY, "GPU update pending" },
		{ A733_GPU_BUS_GATED, EBUSY, "GPU bus gated" },
		{ A733_GPU_RESET_ASSERTED, EBUSY, "GPU reset asserted" },
		{ A733_GPU_MASTER_GATED, EBUSY, "GPU AHB master gated" },
		{ A733_GPU_REF_FLAGS, EBUSY, "PLL_REF flags" },
		{ A733_GPU_REF_RATE, EOPNOTSUPP, "PLL_REF rate" },
		{ A733_GPU_CORE_PARENT, EOPNOTSUPP, "GPU parent unsupported" },
		{ A733_GPU_CORE_PLL, EBUSY, "GPU parent PLL" },
		{ A733_GPU_CORE_DIVIDER, EOPNOTSUPP, "GPU divider" },
		{ A733_GPU_AHB_PARENT, EOPNOTSUPP, "AHB parent unsupported" },
		{ A733_GPU_AHB_PLL, EBUSY, "AHB parent PLL" },
		{ A733_GPU_AHB_DIVIDER, EOPNOTSUPP, "AHB divider" },
	};
	static const char * const registers[] = {
		"PLL_REF[0x000]", "PLL_PERIPH0[0x0a0]",
		"PLL_PERIPH0_PAT0[0x0a8]", "PLL_PERIPH0_PAT1[0x0ac]",
		"PLL_GPU0[0x0e0]", "PLL_GPU0_PAT0[0x0e8]",
		"PLL_GPU0_PAT1[0x0ec]", "GPU_CLK[0xb20]", "GPU_BGR[0xb24]",
		"AHB[0x500]", "AHB_MASTER[0x5c0]",
		"PERI0_GATE_EN[0x1908]", "PERI0_GATE_STAT[0x1988]",
	};
	char expected[128];

	for (u_int i = 0; i < __arraycount(failures); i++) {
		reset();
		/* Readiness fails before the OPP check; report the actual query. */
		voltage = 735000;
		sc.sc_core_hz = 111;
		sc.sc_bus_hz = 222;
		clock_observation.reason = failures[i].reason;
		clock_observation.readiness_error = failures[i].error;
		for (u_int reg = 0; reg < A733_GPU_NREGS; reg++) {
			clock_observation.sample[0][reg] = 0x10000000 + reg;
			clock_observation.sample[1][reg] = 0x10000000 + reg;
		}
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
		CHECK(maps == 0 && peeks == 0 && unmaps == 0 && !sc.sc_have_id);
		CHECK(releases == 1 && clock_puts == 1);
		CHECK(!regulator.acquired && !clock_fixture.acquired);
		CHECK(strcmp(events, "ASVPCQcs") == 0);
		CHECK(sc.sc_core_hz == 111 && sc.sc_bus_hz == 222);
		snprintf(expected, sizeof(expected), "CCU observation: %s (error %d)",
		    failures[i].text, failures[i].error);
		CHECK(strstr(normal_output, expected) != NULL);
		CHECK(strstr(normal_output, "DCDC4 735000 uV, changed 0x000") != NULL);
		CHECK(strstr(normal_output, "hosc 24000000 / 24000000 Hz") != NULL);
		for (u_int reg = 0; reg < A733_GPU_NREGS; reg++) {
			snprintf(expected, sizeof(expected), "%s 0x%08x\n",
			    registers[reg], 0x10000000 + reg);
			CHECK(strstr(normal_output, expected) != NULL);
		}
		CHECK(strstr(normal_output, " -> ") == NULL);
		snprintf(expected, sizeof(expected), "clock/reset state: %d;",
		    failures[i].error);
		CHECK(strstr(error_output, expected) != NULL);
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
		CHECK(strcmp(events, "ASVPCQcs") == 0);
	}
	reset();
	clock_observation.reason = A733_GPU_SNAPSHOT_CHANGED;
	clock_observation.readiness_error = EBUSY;
	clock_observation.changed = (1U << A733_GPU_NREGS) - 1;
	clock_observation.hosc_hz[1] = 19200000;
	for (u_int reg = 0; reg < A733_GPU_NREGS; reg++) {
		clock_observation.sample[0][reg] = 0xa5000000 + reg;
		clock_observation.sample[1][reg] = 0x5a000000 + reg;
	}
	unavailable(EBUSY);
	CHECK(strstr(normal_output, "changed 0x1fff") != NULL);
	CHECK(strstr(normal_output, "hosc 24000000 / 19200000 Hz") != NULL);
	for (u_int reg = 0; reg < A733_GPU_NREGS; reg++) {
		snprintf(expected, sizeof(expected), "%s 0x%08x -> 0x%08x\n",
		    registers[reg], 0xa5000000 + reg, 0x5a000000 + reg);
		CHECK(strstr(normal_output, expected) != NULL);
	}
	/* An acquisition error must not expose a nonexistent snapshot. */
	reset();
	ready_error = ENXIO;
	unavailable(ENXIO);
	CHECK(normal_output[0] == '\0' && strcmp(events, "ASVPCQcs") == 0);
	reset();
	clock_observation.reason = A733_GPU_CORE_PLL;
	clock_observation.readiness_error = ERANGE;
	unavailable(ERANGE);
}

static void
test_experimental_prepare(void)
{
	unsigned start = checks;
	const int errors[] = { EIO, EBUSY, EFAULT, ENXIO, ETIMEDOUT };

	reset(); prepare_len = 0; observe_len = 0;
	unavailable(EINVAL); CHECK(nevents == 0 && prepares == 0);
	for (int len = 1; len <= 4; len++) {
		reset(); prepare_len = len;
		unavailable(EINVAL); CHECK(nevents == 0);
	}
	reset(); prepare_len = 0;
	CHECK(sun60i_gpu_identify(&sc) == 0);
	CHECK(prepares == 1 && reserves == 2 && retains == 1 && waits == 2);
	CHECK(sc.sc_retained && peeks == 1 && unreserves == 0);
	CHECK(strcmp(events, "ASVPCQPCSVHWQKSVKFMIUcs") == 0);
	CHECK(strstr(normal_output, "initial CCU observation before preparation") != NULL);
	reset(); prepare_len = 0; initial_error = ENXIO;
	unavailable(ENXIO);
	CHECK(prepares == 0 && reserves == 0 && retains == 0 && waits == 0);
	CHECK(inspect_calls == 1 && normal_output[0] == '\0');
	for (u_int i = 0; i < __arraycount(errors); i++) {
		for (u_int stage = 0; stage < 5; stage++) {
			reset(); prepare_len = 0;
			switch (stage) {
			case 0: power_reserve_error = errors[i]; break;
			case 1: reserve_error = errors[i]; break;
			case 2: retain_error = errors[i]; break;
			case 3: prepare_error = errors[i]; break;
			case 4: wait_error = errors[i]; break;
			}
			unavailable(errors[i]);
			CHECK(sc.sc_retained == (stage >= 3));
			CHECK(prepares == (stage >= 3 ? 1 : 0));
			CHECK(unreserves == (stage == 1 ? 1 : stage == 2 ? 2 : 0));
		}
	}
	for (unsigned call = 2; call <= 3; call++) {
		reset(); prepare_len = 0; fail_supply_call = call;
		unavailable(EIO);
		CHECK(prepares == (call == 3 ? 1 : 0));
		CHECK(sc.sc_retained == (call == 3));
		CHECK(unreserves == (call == 2 ? 2 : 0));
	}
	reset(); prepare_len = 0; final_ready_error = EBUSY;
	unavailable(EBUSY); CHECK(sc.sc_retained && unreserves == 0);
	reset(); prepare_len = 0; second_wait_error = EIO;
	unavailable(EIO); CHECK(sc.sc_retained && unreserves == 0);
	/* Complete clock readiness must precede the first CORE observation. */
	reset(); prepare_len = 0;
	clock_observation.reason = A733_GPU_UPDATE_PENDING;
	clock_observation.readiness_error = EBUSY;
	unavailable(EBUSY);
	CHECK(waits == 0 && inspect_calls == 2 && sc.sc_retained && unreserves == 0);
	reset(); prepare_len = 0; ready_error = ENXIO;
	unavailable(ENXIO);
	CHECK(waits == 0 && sc.sc_retained && unreserves == 0);
	for (unsigned rate = 0; rate < 2; rate++) {
		reset(); prepare_len = 0;
		if (rate == 0) core_hz = 600000000; else bus_hz = 100000000;
		unavailable(EBUSY);
		CHECK(waits == 0 && sc.sc_retained && unreserves == 0);
	}
	/* A terminal snapshot is fresh, read-only, and preserves the timeout. */
	for (unsigned stage = 0; stage < 3; stage++) {
		reset(); prepare_len = 0;
		if (stage == 0) prepare_error = ETIMEDOUT;
		if (stage == 1) wait_error = ETIMEDOUT;
		if (stage == 2) second_wait_error = ETIMEDOUT;
		clock_observation.sample[0][A733_GPU_MODULE] = 0x83000000;
		clock_observation.sample[1][A733_GPU_MODULE] = 0x83000000;
		terminal_observation.sample[0][A733_GPU_MODULE] = 0x8b000000;
		terminal_observation.sample[1][A733_GPU_MODULE] = 0x8b000001;
		terminal_observation.changed = 1U << A733_GPU_MODULE;
		terminal_observation.reason = A733_GPU_SNAPSHOT_CHANGED;
		terminal_observation.readiness_error = EBUSY;
		initial_observation.sample[0][A733_GPU_PERIPH_GATE_EN] = 0x80020002;
		initial_observation.sample[1][A733_GPU_PERIPH_GATE_EN] = 0x80020002;
		initial_observation.periph_gates[0].configured = 2;
		initial_observation.periph_gates[0].no_auto = 2;
		initial_observation.periph_gates[1] = initial_observation.periph_gates[0];
		terminal_observation.sample[0][A733_GPU_PERIPH_GATE_STAT] = 0x00060000;
		terminal_observation.sample[1][A733_GPU_PERIPH_GATE_STAT] = 0x00020000;
		terminal_observation.periph_gates[0].effective = 6;
		terminal_observation.periph_gates[1].effective = 2;
		if (stage == 0) clock_observation = terminal_observation;
		unavailable(ETIMEDOUT);
		CHECK(waits == stage && inspect_calls == (stage == 0 ? 2 : 3));
		CHECK(sc.sc_retained && unreserves == 0 && prepares == 1);
		CHECK(strstr(normal_output, "terminal CCU observation after ") != NULL);
		CHECK(strstr(normal_output, "GPU_CLK[0xb20] 0x8b000000 -> 0x8b000001") != NULL);
		CHECK(strstr(normal_output, "PERI0_GATE_EN[0x1908] 0x80020002") != NULL);
		CHECK(strstr(normal_output, "PERI0_GATE_STAT[0x1988] 0x00060000 -> 0x00020000") != NULL);
		CHECK(strstr(normal_output, "configured 0x002, no-auto 0x002, effective 0x000") != NULL);
		CHECK(strstr(normal_output, "configured 0x000, no-auto 0x000, effective 0x006") != NULL);
		CHECK(strstr(normal_output, "diagnostic only") != NULL);
		CHECK(strcmp(sc.sc_stage, stage == 0 ? "experimental clock UPDATE completion" :
		    stage == 1 ? "experimental CORE ON/Q acceptance" :
		    "experimental final CORE check") == 0);
	}
	reset(); prepare_len = 0; wait_error = ETIMEDOUT; terminal_error = EIO;
	unavailable(ETIMEDOUT);
	CHECK(inspect_calls == 3 && waits == 1 && sc.sc_retained && unreserves == 0);
	CHECK(strstr(normal_output, "terminal CCU observation unavailable:") != NULL);
	CHECK(strstr(strstr(normal_output, "terminal CCU observation unavailable:"), "GPU_CLK[") == NULL);
	CHECK(strcmp(sc.sc_stage, "experimental CORE ON/Q acceptance") == 0);
	reset(); prepare_len = 0; map_error = ENOMEM;
	CHECK(sun60i_gpu_identify(&sc) == ENOMEM && sc.sc_retained && unreserves == 0);
	reset(); prepare_len = 0; peek_error = 1;
	CHECK(sun60i_gpu_identify(&sc) == EFAULT && sc.sc_retained && unreserves == 0);
	reset(); prepare_len = 0; pbvnc = 0;
	CHECK(sun60i_gpu_identify(&sc) == ENODEV && sc.sc_retained && unreserves == 0);
	reset(); prepare_len = 0; prepare_error = EIO;
	CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
	CHECK(strstr(error_output, "retained until reboot") != NULL);
	CHECK(strstr(error_output, "firmware state left unchanged") == NULL);
	CHECK(sun60i_gpu_finalize(&gpu_device) == 0 && prepares == 1);
	reset(); observe_len = 0;
	CHECK(sun60i_gpu_identify(&sc) == 0);
	CHECK(prepares == 0 && reserves == 0 && retains == 0 && waits == 0 && peeks == 0);
	printf("PASS: %u experimental GPU consumer checks\n", checks - start);
}

static void
test_domain_request(void)
{
	unsigned start = checks;
	const int errors[] = { EIO, EBUSY, EFAULT, ENXIO, ETIMEDOUT };

	/* The fresh request requires the prepared-clock opt-in. */
	reset(); request_len = 0;
	unavailable(EINVAL); CHECK(nevents == 0 && requests == 0);
	reset(); request_len = 0; observe_len = 0;
	unavailable(EINVAL); CHECK(nevents == 0 && requests == 0);
	for (int len = 1; len <= 4; len++) {
		reset(); request_len = len; prepare_len = 0;
		unavailable(EINVAL); CHECK(nevents == 0);
	}

	/* Full success keeps the request between clock preparation and wait. */
	reset(); prepare_len = request_len = 0;
	CHECK(sun60i_gpu_identify(&sc) == 0);
	CHECK(requests == 1 && prepares == 1 && waits == 2 && retains == 1);
	CHECK(strcmp(events, "ASVPCQPCSVHWQRKSVKFMIUcs") == 0);
	CHECK(strstr(normal_output, "TOP-only") == NULL);

	/* Preserve each failure, retained ownership, and safe CCU diagnostics. */
	for (u_int i = 0; i < __arraycount(errors); i++) {
		for (u_int stage = 0; stage < 4; stage++) {
			reset(); prepare_len = request_len = 0;
			switch (stage) {
			case 0: prepare_error = errors[i]; break;
			case 1: request_error = errors[i]; break;
			case 2: wait_error = errors[i]; break;
			case 3: second_wait_error = errors[i]; break;
			}
			unavailable(errors[i]);
			CHECK(sc.sc_retained && unreserves == 0 && prepares == 1);
			CHECK(requests == (stage == 0 ? 0U : 1U));
			CHECK(waits == (stage < 2 ? 0U : stage - 1));
			CHECK(strstr(normal_output, "TOP-only") == NULL);
			if (errors[i] == ETIMEDOUT)
				CHECK(strstr(normal_output,
				    "terminal CCU observation") != NULL);
		}
	}
	reset(); prepare_len = request_len = 0; fail_supply_call = 3;
	unavailable(EIO);
	CHECK(waits == 1 && sc.sc_retained && unreserves == 0);
	reset(); prepare_len = request_len = 0; final_ready_error = EBUSY;
	unavailable(EBUSY);
	CHECK(waits == 2 && sc.sc_retained && unreserves == 0);
	printf("PASS: %u experimental CORE request consumer checks\n",
	    checks - start);
}

/* Execute the real consumer with only power/clock/bus provider boundaries. */
static void
test_core_mmio_guard(const char *selected)
{
	unsigned start = checks;

	if (selected == NULL || strcmp(selected, "runtime") == 0) {
		/* Only REQUEST timeout permits the bounded, read-only WAIT. */
		for (unsigned dry_run = 0; dry_run < 2; dry_run++) {
			reset();
			for (u_int stage = 1; stage < A733_GPU_STAGE_REQUEST; stage++)
				sun60i_gpu_run_stage(&sc, stage);
			request_error = ETIMEDOUT;
			sun60i_gpu_run_stage(&sc, A733_GPU_STAGE_REQUEST);
			CHECK(sc.sc_run_result == ETIMEDOUT && sc.sc_run_pass);
			wait_error = ETIMEDOUT;
			sun60i_gpu_run_stage(&sc, A733_GPU_STAGE_WAIT);
			CHECK(sc.sc_run_result == ETIMEDOUT && !sc.sc_run_core_on);
			CHECK(!sc.sc_run_pass && waits == 1);
			if (dry_run)
				sun60i_gpu_run_stage(&sc, 0);
			nevents = 0;
			sun60i_gpu_run_stage(&sc, A733_GPU_STAGE_TOP_ID);
			CHECK(sc.sc_run_result == EINVAL && nevents == 0);
			CHECK(maps == 0 && peeks == 0 && unmaps == 0);
			CHECK(!sc.sc_have_id && !sc.sc_exec_ready && unreserves == 0);
		}
		/* A request timeout can still converge to confirmed CORE ON. */
		reset();
		for (u_int stage = 1; stage < A733_GPU_STAGE_REQUEST; stage++)
			sun60i_gpu_run_stage(&sc, stage);
		request_error = ETIMEDOUT;
		for (u_int stage = A733_GPU_STAGE_REQUEST;
		    stage <= A733_GPU_STAGE_IDENTIFY; stage++)
			sun60i_gpu_run_stage(&sc, stage);
		CHECK(sc.sc_run_result == 0 && sc.sc_run_core_on && sc.sc_have_id);
		CHECK(maps == 2 && peeks == 2 && unmaps == 2 && unreserves == 0);
		/* An unrelated timeout never authorizes the next stage. */
		reset();
		for (u_int stage = 1; stage < A733_GPU_STAGE_PREPARE; stage++)
			sun60i_gpu_run_stage(&sc, stage);
		prepare_error = ETIMEDOUT;
		sun60i_gpu_run_stage(&sc, A733_GPU_STAGE_PREPARE);
		CHECK(sc.sc_run_result == ETIMEDOUT && !sc.sc_run_pass);
		nevents = 0;
		sun60i_gpu_run_stage(&sc, A733_GPU_STAGE_INSPECT);
		CHECK(sc.sc_run_result == EINVAL && nevents == 0 && maps == 0);
	}
	if (selected == NULL || strcmp(selected, "boot") == 0) {
		reset(); prepare_len = request_len = 0; wait_error = ETIMEDOUT;
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
		CHECK(maps == 0 && peeks == 0 && unmaps == 0 && !sc.sc_have_id);
		CHECK(waits == 1 && sc.sc_retained && unreserves == 0);
		CHECK(strstr(normal_output, "terminal CCU observation") != NULL);
		CHECK(strstr(error_output, "experimental CORE ON/Q acceptance") != NULL);
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0 && waits == 1);
	}
	if (selected == NULL || strcmp(selected, "direct") == 0) {
		reset();
		CHECK(sun60i_gpu_stage_top_id(&sc) == EBUSY);
		CHECK(nevents == 0 && maps == 0 && peeks == 0);
		CHECK(sun60i_gpu_stage_identify(&sc) == EBUSY);
		CHECK(nevents == 0 && maps == 0 && peeks == 0);
		/* A stale cached identity must not bypass the helper's own gate. */
		sc.sc_have_id = true;
		sun60i_gpu_top_identify(&sc, A733_GPU_BASE, &regulator,
		    &clock_fixture, true);
		CHECK(nevents == 0 && maps == 0 && peeks == 0);
		reset(); sc.sc_run_core_on = true;
		CHECK(sun60i_gpu_stage_top_id(&sc) == 0);
		CHECK(maps == 1 && peeks == 1 && unmaps == 1 && sc.sc_have_id);
		CHECK(!regulator.acquired && !clock_fixture.acquired);
		/* Mapping and read failures remain exercised after CORE readiness. */
		for (unsigned fault = 0; fault < 3; fault++) {
			reset(); sc.sc_run_core_on = true;
			if (fault == 0) map_error = ENOMEM;
			if (fault == 1) coreid_peek_error = EFAULT;
			if (fault == 2) peek_error = EFAULT;
			CHECK(sun60i_gpu_stage_top_id(&sc) == EFAULT);
			CHECK(!sc.sc_have_id && maps == 1);
			CHECK(peeks == (fault == 2 ? 1U : 0U));
			CHECK(unmaps == (fault == 0 ? 0U : 1U));
			CHECK(!regulator.acquired && !clock_fixture.acquired);
		}
	}
	if (selected == NULL || strcmp(selected, "nonprepare") == 0) {
		reset();
		unavailable(EBUSY);
		CHECK(prepares == 0 && requests == 0 && waits == 0 && reserves == 0);
		CHECK(strcmp(sc.sc_stage, "CORE ON/Q not confirmed") == 0);
	}
	printf("PASS: %u unconfirmed CORE MMIO exclusion checks\n", checks - start);
}

/* The heap boundary must preserve the production optional-output contract. */
static void
test_fwheap_optional_outputs(void)
{
	unsigned int start = checks;

	for (unsigned int mode = 0; mode < 5; mode++) {
		for (unsigned int mask = 0; mask < 8; mask++) {
			uint32_t rd = UINT32_MAX, wr = UINT32_MAX, timeline = UINT32_MAX;
			bool completed = mode != 0 && mode != 4;
			uint32_t offset = completed ? A733_GPU_FWCOMPUTE_USED_BYTES : 0;

			compute_completion_mode = mode;
			sun60i_a733_gpu_fwheap_observe(NULL, mask & 1 ? &rd : NULL,
			    mask & 2 ? &wr : NULL, mask & 4 ? &timeline : NULL);
			CHECK(rd == (mask & 1 ? offset : UINT32_MAX));
			CHECK(wr == (mask & 2 ? offset : UINT32_MAX));
			CHECK(timeline == (mask & 4 ? (uint32_t)completed : UINT32_MAX));
		}
	}
	compute_completion_mode = 0;
	printf("PASS: %u firmware heap fixture optional-output checks\n",
	    checks - start);
}

/* An adopted bridge must make every diagnostic stage unreachable. */
static void
test_fwbridge_diagnostic_gate(void)
{
	unsigned int start = checks;

	for (unsigned int failed = 0; failed < 2; failed++) {
		for (u_int stage = 0; stage <= A733_GPU_STAGE_FWKICK + 1; stage++) {
			reset();
			sc.sc_fwbridge.attempted = true;
			sc.sc_fwbridge.service.queue.failed = failed;
			sun60i_gpu_run_stage(&sc, stage);
			CHECK(sc.sc_run_result == EBUSY && sc.sc_run_stage == 0);
			CHECK(sc.sc_run_pass && !sc.sc_run_lock);
			CHECK(nevents == 0 && maps == 0 && full_maps == 0);
			CHECK(sun60i_gpu_exec_attach(&sc) == (failed ? EIO : EBUSY));
			CHECK(!sc.sc_run_lock && nevents == 0 && full_maps == 0);
		}
	}
	reset();
	printf("PASS: %u firmware bridge diagnostic exclusion checks\n",
	    checks - start);
}

static void
test_runtime_stages(void)
{
	unsigned start = checks;
	const char * const seq[] = {
	    NULL,
	    "ASVs",		/* 1 supply */
	    "P",		/* 2 GPU_TOP */
	    "P",		/* 3 PCK reserve */
	    "CCc",		/* 4 CCU reserve */
	    "H",		/* 5 retain */
	    "CWc",		/* 6 prepare */
	    "CQc",		/* 7 inspect */
	    "ASVsPR",		/* 8 request */
	    "K",		/* 9 wait */
	    "ASVsPACSVPFMiIUcs",	/* 10 TOP-only identify */
	    "ASVsPMIU",		/* 11 strict identify */
	};
	u_int stage;

	/* The dry run passes without touching a single provider. */
	reset();
	sun60i_gpu_run_stage(&sc, 0);
	CHECK(sc.sc_run_result == 0 && sc.sc_run_stage == 0);
	sun60i_gpu_run_stage(&sc, 2);
	CHECK(sc.sc_run_result == EINVAL && sc.sc_run_stage == 0);
	CHECK(nevents == 0 && prepares == 0 && requests == 0);

	/* Ordered single-call stages with the exact provider sequences. */
	reset();
	sun60i_gpu_run_stage(&sc, 1);
	CHECK(sc.sc_run_result == 0);
	CHECK(strcmp(events, seq[1]) == 0);
	for (stage = 2; stage <= 8; stage++) {
		nevents = 0;
		sun60i_gpu_run_stage(&sc, stage);
		CHECK(sc.sc_run_stage == stage);
		if (stage == 9 - 1)
			continue;
		CHECK(sc.sc_run_result == 0);
		CHECK(strcmp(events, seq[stage]) == 0);
	}
	CHECK(prepares == 1 && reserves == 2 && retains == 1 && requests == 1);
	nevents = 0;
	sun60i_gpu_run_stage(&sc, 9);
	CHECK(sc.sc_run_result == 0 && waits == 1 && sc.sc_run_core_on);
	CHECK(strcmp(events, seq[9]) == 0);
	nevents = 0;
	sun60i_gpu_run_stage(&sc, 10);
	CHECK(sc.sc_run_result == 0 && sc.sc_have_id && sc.sc_bvnc == pbvnc);
	CHECK(strcmp(events, seq[10]) == 0);
	nevents = 0;
	sun60i_gpu_run_stage(&sc, 11);
	CHECK(sc.sc_run_result == 0);
	CHECK(strcmp(events, seq[11]) == 0);
	nevents = 0;
	sun60i_gpu_run_stage(&sc, 13);
	CHECK(sc.sc_run_result == EINVAL && nevents == 0);

	/* A supply failure at stage 1 blocks the rest until reboot. */
	reset(); supply_on = false;
	sun60i_gpu_run_stage(&sc, 1);
	CHECK(sc.sc_run_result == EBUSY && sc.sc_run_stage == 1);
	sun60i_gpu_run_stage(&sc, 2);
	CHECK(sc.sc_run_result == EINVAL);
	/* Clock selection is explicit and cannot change an acquired sequence. */
	reset();
	CHECK(sc.sc_clock_source == 0);
	CHECK(sun60i_gpu_clock_source(&sc, 2) == EINVAL);
	CHECK(sc.sc_clock_source == 0 && nevents == 0);
	CHECK(sun60i_gpu_clock_source(&sc, 1) == 0);
	CHECK(sc.sc_clock_source == 1 && nevents == 0);
	for (stage = 1; stage <= 5; stage++)
		sun60i_gpu_run_stage(&sc, stage);
	CHECK(sun60i_gpu_clock_source(&sc, 0) == EBUSY);
	sun60i_gpu_run_stage(&sc, 0);
	CHECK(sun60i_gpu_clock_source(&sc, 0) == EBUSY);
	sun60i_gpu_run_stage(&sc, 6);
	CHECK(sc.sc_run_result == 0 && pll_prepares == 1 && prepares == 0);
	clock_observation.core_hz = core_hz = 600000000;
	clock_observation.bus_hz = bus_hz = 200000000;
	clock_observation.sample[0][A733_GPU_PLL] = 0xf9204a00;
	sun60i_gpu_run_stage(&sc, 7);
	CHECK(sc.sc_run_result == 0 && sc.sc_core_hz == 600000000);
	CHECK(strstr(sc.sc_clock_state, "pll=0xf9204a00") != NULL);
	CHECK(strstr(sc.sc_clock_state, "core_hz=600000000") != NULL);
	for (stage = 8; stage <= 11; stage++)
		sun60i_gpu_run_stage(&sc, stage);
	CHECK(sc.sc_run_result == 0 && sc.sc_have_id && sc.sc_bvnc == pbvnc);
	/* The PLL experiment rechecks voltage immediately before its writes. */
	reset();
	CHECK(sun60i_gpu_clock_source(&sc, 1) == 0);
	for (stage = 1; stage <= 5; stage++)
		sun60i_gpu_run_stage(&sc, stage);
	voltage = 810000;
	sun60i_gpu_run_stage(&sc, 6);
	CHECK(sc.sc_run_result == EOPNOTSUPP && pll_prepares == 0);
	CHECK(!sc.sc_run_pass && maps == 0);
	reset(); sc.sc_retained = true;
	CHECK(sun60i_gpu_clock_source(&sc, 1) == EBUSY);
	printf("PASS: %u runtime GPU stage checks\n", checks - start);
}

static void
fw_ready(void)
{
	reset();
	for (u_int stage = 1; stage <= 11; stage++) {
		sun60i_gpu_run_stage(&sc, stage);
		CHECK(sc.sc_run_result == 0);
	}
	CHECK(sc.sc_have_id && sc.sc_bvnc == A733_GPU_EXPECTED);
	nevents = 0;
	events[0] = '\0';
}

static void
fw_finished(int error, const char *order)
{
	CHECK(sc.sc_run_result == (u_int)error && sc.sc_run_stage == 12);
	CHECK(strcmp(events, order) == 0);
	CHECK(!fw_handle.open && fw_memory == NULL);
	CHECK(maps == 2 && peeks == 2 && unmaps == 2);
	CHECK(strstr(sc.sc_fw_state, "metadata only") != NULL);
	if (error != 0) {
		char expected[64];

		snprintf(expected, sizeof(expected), "metadata only error=%d", error);
		CHECK(strcmp(sc.sc_fw_state, expected) == 0);
	}
}

static void
test_fwinfo_stage(void)
{
	const off_t invalid_sizes[] = { -1, 0, 4095, 1048577, INT64_MAX };
	unsigned start = checks;

	/* Normal boot and a premature stage cannot open any firmware. */
	reset(); observe_len = 0;
	CHECK(sun60i_gpu_identify(&sc) == 0);
	CHECK(fw_opens == 0 && maps == 0);
	reset();
	sun60i_gpu_run_stage(&sc, 12);
	CHECK(sc.sc_run_result == EINVAL && fw_opens == 0);
	for (unsigned n = 0; n < 5; n++) {
		fw_ready();
		switch (n) {
		case 0: sc.sc_run_stage = 10; break;
		case 1: sc.sc_run_result = EFAULT; break;
		case 2: sc.sc_run_pass = false; break;
		case 3: sc.sc_have_id = false; break;
		case 4: sc.sc_bvnc ^= 1; break;
		}
		CHECK(sun60i_gpu_stage_fwinfo(&sc) == EBUSY);
		CHECK(fw_opens == 0 && nevents == 0);
	}
	/* Stage 0 cannot turn a failed strict identity into permission. */
	reset();
	for (u_int stage = 1; stage <= 10; stage++)
		sun60i_gpu_run_stage(&sc, stage);
	peek_error = 1;
	sun60i_gpu_run_stage(&sc, 11);
	CHECK(sc.sc_run_result == EFAULT && !sc.sc_run_pass);
	sun60i_gpu_run_stage(&sc, 0);
	sun60i_gpu_run_stage(&sc, 12);
	CHECK(sc.sc_run_result == EINVAL && fw_opens == 0);

	fw_ready(); fw_open_error = ENOENT;
	sun60i_gpu_run_stage(&sc, 12);
	fw_finished(ENOENT, "o");
	CHECK(fw_sizes == 0 && fw_allocs == 0 && fw_closes == 0);
	for (unsigned n = 0; n < __arraycount(invalid_sizes); n++) {
		fw_ready(); fw_file_size = invalid_sizes[n];
		sun60i_gpu_run_stage(&sc, 12);
		fw_finished(EINVAL, "ozc");
		CHECK(fw_allocs == 0 && fw_reads == 0 && fw_closes == 1);
	}
	fw_ready(); fw_alloc_fail = true;
	sun60i_gpu_run_stage(&sc, 12);
	fw_finished(ENOMEM, "ozac");
	CHECK(fw_reads == 0 && fw_frees == 0 && fw_closes == 1);
	fw_ready(); fw_read_error = EIO; fw_close_error = EACCES;
	sun60i_gpu_run_stage(&sc, 12);
	fw_finished(EIO, "ozarcf");
	CHECK(fw_parses == 0 && fw_frees == 1);
	fw_ready(); fw_bad_bvnc = true; fw_close_error = EIO;
	sun60i_gpu_run_stage(&sc, 12);
	fw_finished(ENODEV, "ozarvcf");
	CHECK(fw_parses == 1 && fw_frees == 1);
	fw_ready(); fw_file_size = 4096;
	sun60i_gpu_run_stage(&sc, 12);
	fw_finished(EOPNOTSUPP, "ozarvcf");
	fw_ready(); fw_close_error = EIO;
	sun60i_gpu_run_stage(&sc, 12);
	fw_finished(EIO, "ozarvcf");
	for (unsigned n = 0; n < 2; n++) {
		fw_ready(); fw_file_size = n == 0 ? 8192 : 1048576;
		sun60i_gpu_run_stage(&sc, 12);
		fw_finished(0, "ozarvcf");
		CHECK(fw_parses == 1 && fw_closes == 1 && fw_frees == 1);
		CHECK(strstr(sc.sc_fw_state, "version=1.1.1") != NULL);
		CHECK(strstr(sc.sc_fw_state, "bvnc=0x00240038006800b7") != NULL);
		CHECK(strstr(sc.sc_fw_state, "layouts=1 code=4096 data=0") != NULL);
		unsigned calls = fw_opens;
		sun60i_gpu_run_stage(&sc, 0);
		sun60i_gpu_run_stage(&sc, 12);
		CHECK(sc.sc_run_result == EINVAL && fw_opens == calls);
	}
	reset();
	printf("PASS: %u firmware metadata consumer checks\n", checks - start);
}

static void
fwmem_ready(void)
{
	fw_ready();
	sun60i_gpu_run_stage(&sc, 12);
	fw_finished(0, "ozarvcf");
	CHECK(fwmem_prepares == 0 && fwmem_live == NULL);
	fw_opens = fw_sizes = fw_allocs = fw_reads = fw_parses = 0;
	fw_closes = fw_frees = 0;
	nevents = 0;
	events[0] = '\0';
	fw_cached_state = sc.sc_memory_state;
	snprintf(sc.sc_memory_state, sizeof(sc.sc_memory_state),
	    "unchanged before stage 13");
	memcpy(fw_before_state, sc.sc_memory_state, sizeof(fw_before_state));
	fw_strict_publication = true;
}

static void
fwmem_finished(int error, const char *order)
{
	CHECK(sc.sc_run_result == (u_int)error && sc.sc_run_stage == 13);
	CHECK(strcmp(events, order) == 0);
	CHECK(!fw_handle.open && fw_memory == NULL);
	CHECK(maps == 2 && peeks == 2 && unmaps == 2);
	CHECK(fw_parses == 0);
	CHECK(strstr(sc.sc_fw_state, "metadata only error=0") != NULL);
	if (error != 0) {
		char expected[64];

		snprintf(expected, sizeof(expected), "CPU-owned memory only error=%d",
		    error);
		CHECK(strcmp(sc.sc_memory_state, expected) == 0);
		CHECK(sc.sc_fw_memory == NULL && fwmem_live == NULL);
	} else {
		CHECK(sc.sc_fw_memory != NULL && sc.sc_fw_memory == fwmem_live);
		CHECK(strcmp(sc.sc_memory_state,
		    "CPU-owned memory only error=0 code=102400 data=20480 table=16384 "
		    "loads=5 entry=0xbfc00001 boot=0x20000000 stack=0x30004000") == 0);
	}
}

static void
fwmem_no_replay(void)
{
	unsigned calls = fw_opens;
	unsigned prep = fwmem_prepares, destroyed = fwmem_destroys;
	const struct sun60i_a733_gpu_fwmem *saved = sc.sc_fw_memory;
	char cached[sizeof(sc.sc_memory_state)];

	memcpy(cached, sc.sc_memory_state, sizeof(cached));
	for (unsigned n = 0; n < 2; n++) {
		if (n != 0)
			sun60i_gpu_run_stage(&sc, 0);
		sun60i_gpu_run_stage(&sc, 13);
		CHECK(sc.sc_run_result == EINVAL && fw_opens == calls);
		CHECK(fwmem_prepares == prep && fwmem_destroys == destroyed);
		CHECK(sc.sc_fw_memory == saved);
		CHECK(memcmp(cached, sc.sc_memory_state, sizeof(cached)) == 0);
	}
}

static void
fwmem_teardown(void)
{
	/* Only the test owns this teardown. No real GPU has these addresses. */
	fw_strict_publication = false;
	fw_cached_state = NULL;
	sun60i_a733_gpu_fwmem_destroy(sc.sc_fw_memory);
	sc.sc_fw_memory = NULL;
	CHECK(fwmem_live == NULL);
}

static void
test_fwmem_stage(void)
{
	const off_t invalid_sizes[] = { -1, 0, 4095, 1048577, INT64_MAX };
	const int preparation_errors[] = {
		EINVAL, ENODEV, EOPNOTSUPP, ENOMEM, EFBIG, ENOSPC, EIO
	};
	struct sun60i_a733_gpu_fwmem existing;
	unsigned start = checks;

	reset(); observe_len = 0;
	CHECK(sun60i_gpu_identify(&sc) == 0);
	CHECK(fw_opens == 0 && fwmem_prepares == 0 && maps == 0);
	reset();
	sun60i_gpu_run_stage(&sc, 13);
	CHECK(sc.sc_run_result == EINVAL && fw_opens == 0);
	fw_ready();
	sun60i_gpu_run_stage(&sc, 13);
	CHECK(sc.sc_run_result == EINVAL && fwmem_prepares == 0);
	/* Every direct helper precondition also blocks file and DMA preparation. */
	for (unsigned n = 0; n < 6; n++) {
		fwmem_ready();
		switch (n) {
		case 0: sc.sc_run_stage = 11; break;
		case 1: sc.sc_run_result = EFAULT; break;
		case 2: sc.sc_run_pass = false; break;
		case 3: sc.sc_have_id = false; break;
		case 4: sc.sc_bvnc ^= 1; break;
		case 5:
			sc.sc_fw_memory = &existing;
			fw_expected_memory = &existing;
			break;
		}
		CHECK(sun60i_gpu_stage_fwmem(&sc) == EBUSY);
		CHECK(fw_opens == 0 && fwmem_prepares == 0 && nevents == 0);
		CHECK(sc.sc_fw_memory == fw_expected_memory);
		CHECK(fwmem_destroys == 0 && fwmem_null_destroys == 1);
		sc.sc_fw_memory = NULL;
	}
	/* A failed metadata stage cannot be bypassed through dry-run stage 0. */
	fw_ready(); fw_read_error = EIO;
	sun60i_gpu_run_stage(&sc, 12);
	CHECK(sc.sc_run_result == EIO && !sc.sc_run_pass);
	sun60i_gpu_run_stage(&sc, 0);
	sun60i_gpu_run_stage(&sc, 13);
	CHECK(sc.sc_run_result == EINVAL && fw_opens == 1);
	CHECK(fwmem_prepares == 0);

	fwmem_ready(); fw_open_error = ENOENT;
	sun60i_gpu_run_stage(&sc, 13);
	fwmem_finished(ENOENT, "o");
	CHECK(fw_sizes == 0 && fw_allocs == 0 && fw_closes == 0);
	CHECK(fwmem_prepares == 0);
	fwmem_no_replay();
	for (unsigned n = 0; n < __arraycount(invalid_sizes); n++) {
		fwmem_ready(); fw_file_size = invalid_sizes[n]; fw_close_error = EIO;
		sun60i_gpu_run_stage(&sc, 13);
		fwmem_finished(EINVAL, "ozc");
		CHECK(fw_allocs == 0 && fw_reads == 0 && fw_closes == 1);
		CHECK(fwmem_prepares == 0);
	}
	fwmem_ready(); fw_alloc_fail = true; fw_close_error = EIO;
	sun60i_gpu_run_stage(&sc, 13);
	fwmem_finished(ENOMEM, "ozac");
	CHECK(fw_reads == 0 && fw_frees == 0 && fw_closes == 1);
	CHECK(fwmem_prepares == 0);
	fwmem_ready(); fw_read_error = EIO; fw_close_error = EACCES;
	sun60i_gpu_run_stage(&sc, 13);
	fwmem_finished(EIO, "ozarcf");
	CHECK(fwmem_prepares == 0 && fw_frees == 1);
	for (unsigned n = 0; n < __arraycount(preparation_errors); n++) {
		fwmem_ready(); fwmem_prepare_error = preparation_errors[n];
		fw_close_error = EACCES;
		sun60i_gpu_run_stage(&sc, 13);
		fwmem_finished(preparation_errors[n], "ozarmcf");
		CHECK(fwmem_prepares == 1 && fw_frees == 1 && fw_closes == 1);
		CHECK(fwmem_destroys == 0 && fwmem_null_destroys == 1);
		fwmem_no_replay();
	}
	/* The lower bound is accepted for preparation, which can reject content. */
	fwmem_ready(); fw_file_size = 4096; fwmem_prepare_error = EOPNOTSUPP;
	sun60i_gpu_run_stage(&sc, 13);
	fwmem_finished(EOPNOTSUPP, "ozarmcf");
	CHECK(fwmem_prepares == 1);
	/* Close failure after success must free the prepared CPU-only allocation. */
	fwmem_ready(); fw_close_error = EIO;
	sun60i_gpu_run_stage(&sc, 13);
	fwmem_finished(EIO, "ozarmcfd");
	CHECK(fwmem_prepares == 1 && fwmem_destroys == 1);
	CHECK(fwmem_null_destroys == 0 && fw_frees == 1);
	fwmem_no_replay();
	for (unsigned n = 0; n < 2; n++) {
		fwmem_ready(); fw_file_size = n == 0 ? 8192 : 1048576;
		sun60i_gpu_run_stage(&sc, 13);
		fwmem_finished(0, "ozarmcf");
		CHECK(fwmem_prepares == 1 && fwmem_destroys == 0);
		CHECK(fwmem_null_destroys == 0 && fw_frees == 1);
		CHECK(sc.sc_fw_memory->dmat == &dma_tag_cookie);
		fwmem_no_replay();
		fwmem_teardown();
	}
	reset();
	printf("PASS: %u CPU-owned firmware memory consumer checks\n", checks - start);
}

/* Consumer snapshots do not emulate FWIF initialization or DMA coherency. */
struct memory_snapshot {
	struct sun60i_a733_gpu_fwmem owner;
	void *copy[5];
};

static void
memory_snapshot_take(struct memory_snapshot *snap)
{
	const struct sun60i_a733_gpu_dma *areas[] = {
		&fwmem_live->code, &fwmem_live->data, &fwmem_live->table,
		&fwmem_live->work, &fwmem_live->config
	};

	memcpy(&snap->owner, fwmem_live, sizeof(snap->owner));
	for (size_t i = 0; i < __arraycount(areas); i++) {
		snap->copy[i] = NULL;
		if (areas[i]->cpu == NULL)
			continue;
		snap->copy[i] = malloc(areas[i]->size);
		CHECK(snap->copy[i] != NULL);
		memcpy(snap->copy[i], areas[i]->cpu, areas[i]->size);
		for (size_t j = 0; j < i; j++)
			CHECK(areas[i]->cpu != areas[j]->cpu);
	}
}

static bool
fixture_bytes(const void *cpu, size_t size, uint8_t fill)
{
	const uint8_t *p = cpu;

	for (size_t i = 0; i < size; i++) {
		if (p[i] != fill)
			return false;
	}
	return true;
}

static void
memory_snapshot_check(struct memory_snapshot *snap, bool added_interface)
{
	struct sun60i_a733_gpu_fwmem actual;
	const struct sun60i_a733_gpu_dma *areas[] = {
		&snap->owner.code, &snap->owner.data, &snap->owner.table,
		&snap->owner.work, &snap->owner.config
	};

	CHECK(fwmem_live != NULL && sc.sc_fw_memory == fwmem_live);
	memcpy(&actual, fwmem_live, sizeof(actual));
	if (added_interface) {
		CHECK(fixture_bytes(actual.work.cpu, actual.work.size, 0x64));
		CHECK(fixture_bytes(actual.config.cpu, actual.config.size, 0x75));
		actual.work = snap->owner.work;
		actual.config = snap->owner.config;
		actual.interface_layout = snap->owner.interface_layout;
		actual.interface_prepared = snap->owner.interface_prepared;
	}
	CHECK(memcmp(&actual, &snap->owner, sizeof(actual)) == 0);
	for (size_t i = 0; i < __arraycount(areas); i++) {
		if (snap->copy[i] == NULL)
			continue;
		CHECK(memcmp(areas[i]->cpu, snap->copy[i], areas[i]->size) == 0);
		free(snap->copy[i]);
	}
	CHECK(fwmem_destroys == 0 && fwmem_null_destroys == 0);
	CHECK(fw_opens == 1 && fw_frees == 1 && !fw_handle.open);
	CHECK(fw_memory == NULL && !regulator.acquired && !clock_fixture.acquired);
}

static void
later_begin(void)
{
	nevents = 0;
	events[0] = '\0';
	maps = peeks = unmaps = 0;
	waits = 0;
	later_memory_state = sc.sc_memory_state;
	later_interface_state = sc.sc_interface_state;
	later_boot_state = sc.sc_boot_state;
	later_probe_state = sc.sc_probe_state;
	later_cache_state = sc.sc_cache_state;
	memcpy(later_memory_before, sc.sc_memory_state, sizeof(later_memory_before));
	memcpy(later_interface_before, sc.sc_interface_state,
	    sizeof(later_interface_before));
	memcpy(later_boot_before, sc.sc_boot_state, sizeof(later_boot_before));
	memcpy(later_probe_before, sc.sc_probe_state, sizeof(later_probe_before));
	memcpy(later_cache_before, sc.sc_cache_state, sizeof(later_cache_before));
	later_publication = true;
}

static void
fwinit_ready(void)
{
	fwmem_ready();
	sun60i_gpu_run_stage(&sc, 13);
	fwmem_finished(0, "ozarmcf");
	fw_strict_publication = false;
	fw_cached_state = NULL;
	snprintf(sc.sc_interface_state, sizeof(sc.sc_interface_state),
	    "interface not yet published");
	snprintf(sc.sc_boot_state, sizeof(sc.sc_boot_state),
	    "bootstrap not yet published");
	later_begin();
}

static void
later_teardown(void)
{
	CHECK(!full_mapping);
	/* Fixture-only reset. No production path may release a device owner. */
	fixture_device_owned = false;
	later_publication = false;
	fwmem_teardown();
}

static void
later_no_replay(u_int stage)
{
	struct sun60i_a733_gpu_fwboot_result saved_result = sc.sc_boot_result;
	struct sun60i_a733_gpu_fwprobe_result saved_probe = sc.sc_probe_result;
	unsigned probe_calls = fw_probes, cache_calls = fw_caches;
	struct sun60i_a733_gpu_fwprobe_result saved_cache = sc.sc_cache_result;
	unsigned init_calls = fw_interfaces, boot_calls = fw_boots;
	unsigned mmap_calls = full_maps, destroy_calls = fwmem_destroys;
	char memory[256], interface[256], boot[512], probe[1024], cache[1024];
	size_t count = nevents;

	memcpy(memory, sc.sc_memory_state, sizeof(memory));
	memcpy(interface, sc.sc_interface_state, sizeof(interface));
	memcpy(boot, sc.sc_boot_state, sizeof(boot));
	memcpy(probe, sc.sc_probe_state, sizeof(probe));
	memcpy(cache, sc.sc_cache_state, sizeof(cache));
	for (unsigned i = 0; i < 2; i++) {
		if (i != 0)
			sun60i_gpu_run_stage(&sc, 0);
		sun60i_gpu_run_stage(&sc, stage);
		CHECK(sc.sc_run_result == EINVAL && sc.sc_run_stage == stage);
		CHECK(fw_interfaces == init_calls && fw_boots == boot_calls);
		CHECK(fw_probes == probe_calls && fw_caches == cache_calls);
		CHECK(full_maps == mmap_calls && fwmem_destroys == destroy_calls);
		CHECK(nevents == count && !full_mapping);
		CHECK(memcmp(memory, sc.sc_memory_state, sizeof(memory)) == 0);
		CHECK(memcmp(interface, sc.sc_interface_state, sizeof(interface)) == 0);
		CHECK(memcmp(boot, sc.sc_boot_state, sizeof(boot)) == 0);
		CHECK(memcmp(probe, sc.sc_probe_state, sizeof(probe)) == 0);
		CHECK(memcmp(cache, sc.sc_cache_state, sizeof(cache)) == 0);
		CHECK(memcmp(&saved_cache, &sc.sc_cache_result, sizeof(saved_cache)) == 0);
		CHECK(memcmp(&saved_probe, &sc.sc_probe_result, sizeof(saved_probe)) == 0);
		CHECK(memcmp(&saved_result, &sc.sc_boot_result, sizeof(saved_result)) == 0);
	}
}

static void
test_fwinit_stage(void)
{
	const int helper_errors[] = { EINVAL, EOPNOTSUPP, ENOMEM, EIO, EEXIST };
	const unsigned features[] = {
		A733_GPU_FW_FEATURE_NUM_CLUSTERS,
		A733_GPU_FW_FEATURE_SLC_SIZE_IN_KILOBYTES
	};
	struct memory_snapshot snap;
	unsigned start = checks;

	reset();
	sun60i_gpu_run_stage(&sc, 14);
	CHECK(sc.sc_run_result == EINVAL && nevents == 0 && fw_interfaces == 0);
	for (unsigned n = 0; n < 7; n++) {
		fwinit_ready();
		switch (n) {
		case 0: sc.sc_run_stage = 12; break;
		case 1: sc.sc_run_result = EIO; break;
		case 2: sc.sc_run_pass = false; break;
		case 3: sc.sc_have_id = false; break;
		case 4: sc.sc_bvnc ^= 1; break;
		case 5: sc.sc_fw_memory = NULL; break;
		case 6: fwmem_live->image.info.bvnc ^= 1; break;
		}
		memory_snapshot_take(&snap);
		CHECK(sun60i_gpu_stage_fwinit(&sc) == EBUSY);
		CHECK(fw_interfaces == 0 && nevents == 0 && full_maps == 0);
		sc.sc_fw_memory = fwmem_live;
		memory_snapshot_check(&snap, false);
		later_teardown();
	}
	/* Feature lookup uses the production decoder, including absent fields. */
	for (size_t f = 0; f < __arraycount(features); f++) {
		for (unsigned n = 0; n < 3; n++) {
			unsigned feature = features[f];
			int wanted = n == 0 ? ENOENT : EOPNOTSUPP;

			fwinit_ready();
			if (n == 0)
				fwmem_live->image.info.feature_mask[0] &=
				    ~(UINT64_C(1) << feature);
			else
				fwmem_live->image.info.feature_value[feature] =
				    n == 1 ? 0 : UINT64_C(1) << 32;
			memory_snapshot_take(&snap);
			sun60i_gpu_run_stage(&sc, 14);
			CHECK(sc.sc_run_result == (u_int)wanted);
			CHECK(fw_interfaces == 0 && nevents == 0 && full_maps == 0);
			memory_snapshot_check(&snap, false);
			later_no_replay(14);
			later_teardown();
		}
	}
	for (size_t n = 0; n < __arraycount(helper_errors); n++) {
		fwinit_ready(); fw_interface_error = helper_errors[n];
		memory_snapshot_take(&snap);
		sun60i_gpu_run_stage(&sc, 14);
		CHECK(sc.sc_run_result == (u_int)helper_errors[n]);
		CHECK(strcmp(events, "J") == 0 && fw_interfaces == 1);
		CHECK(!fwmem_live->interface_prepared && full_maps == 0);
		memory_snapshot_check(&snap, false);
		later_no_replay(14);
		sun60i_gpu_run_stage(&sc, 15);
		CHECK(sc.sc_run_result == EINVAL && fw_boots == 0);
		later_teardown();
	}
	for (unsigned n = 0; n < 2; n++) {
		fwinit_ready();
		if (n != 0) {
			sc.sc_core_hz = expected_interface_hz = 600000000;
			fwmem_live->image.info.feature_value[features[0]] =
			    expected_clusters = 4;
			fwmem_live->image.info.feature_value[features[1]] =
			    expected_slc = 128;
		}
		memory_snapshot_take(&snap);
		sun60i_gpu_run_stage(&sc, 14);
		CHECK(sc.sc_run_result == 0 && sc.sc_run_stage == 14);
		CHECK(fwmem_live->interface_prepared && fw_interfaces == 1);
		CHECK(strcmp(events, "J") == 0 && maps == 0 && full_maps == 0);
		CHECK(strstr(sc.sc_interface_state, "error=0 work=139264 config=12288") != NULL);
		CHECK(strstr(sc.sc_interface_state, n == 0 ?
		    "core_hz=400000000 clusters=2 slc_kib=16" :
		    "core_hz=600000000 clusters=4 slc_kib=128") != NULL);
		CHECK(strcmp(sc.sc_memory_state, later_memory_before) == 0);
		memory_snapshot_check(&snap, true);
		later_no_replay(14);
		later_teardown();
	}
	reset();
	printf("PASS: %u firmware interface consumer checks\n", checks - start);
}

static void
fwboot_ready(void)
{
	fwinit_ready();
	sun60i_gpu_run_stage(&sc, 14);
	CHECK(sc.sc_run_result == 0 && sc.sc_run_stage == 14);
	CHECK(fw_interfaces == 1 && fwmem_live->interface_prepared);
	later_begin();
}

static void
boot_stopped(int wanted, const char *order, unsigned mmap_calls,
    unsigned munmap_calls, unsigned boot_calls)
{
	CHECK(sc.sc_run_result == (u_int)wanted && sc.sc_run_stage == 15);
	CHECK(strcmp(events, order) == 0);
	{
		char prefix[128];
		const struct sun60i_a733_gpu_fwboot_result *r = &sc.sc_boot_result;

		snprintf(prefix, sizeof(prefix),
		    "error=%d attempted=%u device_owned=%u touched=%u released=%u ",
		    wanted, r->attempted, r->device_owned, r->start.touched,
		    r->start.released);
		CHECK(strncmp(sc.sc_boot_state, prefix, strlen(prefix)) == 0);
	}
	CHECK(full_maps == mmap_calls && full_unmaps == munmap_calls);
	CHECK(fw_boots == boot_calls && !full_mapping);
	CHECK(fw_interfaces == 1 && fwmem_prepares == 1 && fwmem_destroys == 0);
	CHECK(!regulator.acquired && !clock_fixture.acquired);
	if (!sc.sc_boot_result.device_owned) {
		CHECK(strcmp(sc.sc_memory_state, later_memory_before) == 0);
		CHECK(strcmp(sc.sc_interface_state, later_interface_before) == 0);
	} else {
		CHECK(strcmp(sc.sc_memory_state,
		    "device-owned; retained until hardware reset; see boot_state") == 0);
		CHECK(strcmp(sc.sc_interface_state, sc.sc_memory_state) == 0);
	}
}

static void
test_fwboot_stage(void)
{
	static const struct { int error; const char *order; } failure[] = {
		{ ENXIO, "A" }, { EIO, "ASs" }, { EBUSY, "ASs" },
		{ EIO, "ASVs" }, { EOPNOTSUPP, "ASVs" },
		{ EIO, "ASVsP" }, { EBUSY, "ASVsP" },
		{ ETIMEDOUT, "ASVsPK" }, { ENXIO, "ASVsPKC" },
		{ EIO, "ASVsPKCFc" }, { EBUSY, "ASVsPKCFc" },
		{ EBUSY, "ASVsPKCFc" }, { EIO, "ASVsPKCFcASs" },
		{ EINVAL, "ASVsPKCFcASVsP" },
		{ EINVAL, "ASVsPKCFcASVsP" },
		{ EINVAL, "ASVsPKCFcASVsP" },
		{ ENOMEM, "ASVsPKCFcASVsPM" },
		{ EFAULT, "ASVsPKCFcASVsPMIU" },
		{ ENODEV, "ASVsPKCFcASVsPMIU" },
		{ EINVAL, "ASVsPKCFcASVsPMIU" },
		{ ENOMEM, "ASVsPKCFcASVsPMIUX" }
	};
	static const struct { int error; bool attempted, touched, released, owned; } reply[] = {
		{ EINVAL, false, false, false, false },
		{ EFAULT, true, false, false, false },
		{ EFAULT, true, true, false, true },
		{ EFAULT, true, true, true, true },
		{ ETIMEDOUT, true, true, true, true },
		{ EPROTO, true, true, true, true },
		{ 0, true, true, true, true }
	};
	struct memory_snapshot snap;
	unsigned start = checks;

	reset();
	sun60i_gpu_run_stage(&sc, 15);
	CHECK(sc.sc_run_result == EINVAL && nevents == 0 && fw_boots == 0);
	fwinit_ready();
	sun60i_gpu_run_stage(&sc, 15);
	CHECK(sc.sc_run_result == EINVAL && full_maps == 0 && fw_interfaces == 0);
	later_teardown();
	for (unsigned n = 0; n < 11; n++) {
		fwboot_ready();
		switch (n) {
		case 0: sc.sc_run_stage = 13; break;
		case 1: sc.sc_run_result = EIO; break;
		case 2: sc.sc_run_pass = false; break;
		case 3: sc.sc_run_core_on = false; break;
		case 4: sc.sc_have_id = false; break;
		case 5: sc.sc_bvnc ^= 1; break;
		case 6: sc.sc_fw_memory = NULL; break;
		case 7: fwmem_live->image.info.bvnc ^= 1; break;
		case 8: fwmem_live->interface_prepared = false; break;
		case 9: sc.sc_boot_result.attempted = true; break;
		case 10: sc.sc_boot_result.device_owned = true; break;
		}
		memory_snapshot_take(&snap);
		CHECK(sun60i_gpu_stage_fwboot(&sc, 2) == EBUSY);
		CHECK(fw_boots == 0 && full_maps == 0 && nevents == 0);
		sc.sc_fw_memory = fwmem_live;
		memory_snapshot_check(&snap, false);
		later_teardown();
	}
	for (size_t n = 0; n < __arraycount(failure); n++) {
		fwboot_ready();
		switch (n) {
		case 0: missing_supply = true; break;
		case 1: state_error = EIO; break;
		case 2: supply_on = false; break;
		case 3: voltage_error = EIO; break;
		case 4: voltage = 900000; break;
		case 5: power_error = EIO; break;
		case 6: power_on = false; break;
		case 7: wait_error = ETIMEDOUT; break;
		case 8: missing_clock = true; break;
		case 9: final_ready_error = EIO; break;
		case 10: core_hz++; break;
		case 11: bus_hz++; break;
		case 12: fail_supply_call = supply_calls + 2; break;
		case 13: reg_error = EIO; break;
		case 14: binding_addr += 4096; break;
		case 15: binding_size = 0x27; break;
		case 16: map_error = ENOMEM; break;
		case 17: peek_error = EFAULT; break;
		case 18: pbvnc ^= 1; break;
		case 19: binding_size = A733_GPU_FWSTART_REG_SIZE - 1; break;
		case 20: full_map_error = ENOMEM; break;
		}
		memory_snapshot_take(&snap);
		sun60i_gpu_run_stage(&sc, 15);
		boot_stopped(failure[n].error, failure[n].order, n == 20, 0, 0);
		CHECK(!sc.sc_boot_result.attempted && !sc.sc_boot_result.device_owned);
		memory_snapshot_check(&snap, false);
		later_no_replay(15);
		later_teardown();
	}
	for (size_t n = 0; n < __arraycount(reply); n++) {
		fwboot_ready();
		fw_boot_error = boot_reply.error = reply[n].error;
		boot_reply.attempted = reply[n].attempted;
		boot_reply.device_owned = reply[n].owned;
		boot_reply.start.touched = reply[n].touched;
		boot_reply.start.released = reply[n].released;
		boot_reply.start.last_register = 0x100;
		boot_reply.start.writes = 17;
		boot_reply.polls = 29;
		boot_reply.snapshot_valid = true;
		boot_reply.firmware_started = n == 6;
		boot_reply.firmware_started_timestamp = 12345;
		boot_reply.fw_state = 2;
		boot_reply.os_state = 3;
		boot_reply.alive_fw_token = 45;
		boot_reply.alive_os_token = 67;
		boot_reply.compchecks_updated = 1;
		boot_reply.hw_bvnc = UINT64_C(0x00240038006800b7);
		boot_reply.fw_bvnc = UINT64_C(0x00240038006800b7);
		boot_reply.ddk_build = 6976702;
		memory_snapshot_take(&snap);
		sun60i_gpu_run_stage(&sc, 15);
		boot_stopped(reply[n].error, "ASVsPKCFcASVsPMIUXBZ", 1, 1, 1);
		CHECK(memcmp(&boot_reply, &sc.sc_boot_result, sizeof(boot_reply)) == 0);
		CHECK(maps == 1 && peeks == 1 && unmaps == 1);
		CHECK(strstr(sc.sc_boot_state, "register=0x100 writes=17 polls=29 snapshot=1") != NULL);
		CHECK(strstr(sc.sc_boot_state, "timestamp=12345 fw_state=2 os_state=3 alive_fw=45 alive_os=67") != NULL);
		CHECK(strstr(sc.sc_boot_state, "compchecks=1 hw_bvnc=0x00240038006800b7 fw_bvnc=0x00240038006800b7 ddk_build=6976702") != NULL);
		memory_snapshot_check(&snap, false);
		later_no_replay(15);
		later_teardown();
	}
	reset();
	printf("PASS: %u firmware bootstrap consumer checks\n", checks - start);
}

static void
fwprobe_ready(void)
{
	fwboot_ready();
	boot_reply.attempted = boot_reply.device_owned = true;
	boot_reply.start.touched = boot_reply.start.released = true;
	boot_reply.snapshot_valid = true;
	boot_reply.firmware_started = 1;
	sun60i_gpu_run_stage(&sc, 15);
	CHECK(sc.sc_run_result == 0 && sc.sc_run_stage == 15);
	CHECK(fw_boots == 1 && fixture_device_owned);
	snprintf(sc.sc_probe_state, sizeof(sc.sc_probe_state),
	    "command not yet published");
	full_maps = full_unmaps = 0;
	later_begin();
}

static void
probe_stopped(int wanted, const char *order, unsigned mmap_calls,
    unsigned munmap_calls, unsigned probe_calls)
{
	char prefix[160];
	const struct sun60i_a733_gpu_fwprobe_result *r = &sc.sc_probe_result;

	CHECK(sc.sc_run_result == (u_int)wanted && sc.sc_run_stage == 16);
	CHECK(strcmp(events, order) == 0);
	snprintf(prefix, sizeof(prefix),
	    "error=%d attempted=%u published=%u kick_attempted=%u kicked=%u device_owned=1 ",
	    wanted, r->attempted, r->published, r->kick_attempted, r->kicked);
	CHECK(strncmp(sc.sc_probe_state, prefix, strlen(prefix)) == 0);
	CHECK(full_maps == mmap_calls && full_unmaps == munmap_calls);
	CHECK(fw_probes == probe_calls && !full_mapping);
	CHECK(fw_boots == 1 && fw_interfaces == 1 && fwmem_destroys == 0);
	CHECK(fixture_device_owned && sc.sc_boot_result.device_owned);
	CHECK(!regulator.acquired && !clock_fixture.acquired);
	CHECK(strcmp(sc.sc_memory_state, later_memory_before) == 0);
	CHECK(strcmp(sc.sc_interface_state, later_interface_before) == 0);
	CHECK(strcmp(sc.sc_boot_state, later_boot_before) == 0);
}

static void
test_fwprobe_stage(void)
{
	static const struct { int error; const char *order; } failure[] = {
		{ ENXIO, "A" }, { EIO, "ASs" }, { EBUSY, "ASs" },
		{ EIO, "ASVs" }, { EOPNOTSUPP, "ASVs" },
		{ EIO, "ASVsP" }, { EBUSY, "ASVsP" },
		{ ETIMEDOUT, "ASVsPK" }, { ENXIO, "ASVsPKC" },
		{ EIO, "ASVsPKCFc" }, { EBUSY, "ASVsPKCFc" },
		{ EBUSY, "ASVsPKCFc" }, { EIO, "ASVsPKCFcASs" },
		{ EINVAL, "ASVsPKCFcASVsP" },
		{ EINVAL, "ASVsPKCFcASVsP" },
		{ EINVAL, "ASVsPKCFcASVsP" },
		{ ENOMEM, "ASVsPKCFcASVsPM" },
		{ EFAULT, "ASVsPKCFcASVsPMIU" },
		{ ENODEV, "ASVsPKCFcASVsPMIU" },
		{ EINVAL, "ASVsPKCFcASVsPMIU" },
		{ ENOMEM, "ASVsPKCFcASVsPMIUX" }
	};
	static const struct {
		int error;
		bool attempted, published, kick_attempted, kicked;
	} reply[] = {
		{ EINVAL, false, false, false, false },
		{ EOPNOTSUPP, true, false, false, false },
		{ EBUSY, true, false, false, false },
		{ EPROTO, true, false, false, false },
		{ EFAULT, true, true, true, false },
		{ ETIMEDOUT, true, true, true, true },
		{ EPROTO, true, true, true, true },
		{ 0, true, true, true, true }
	};
	struct sun60i_a733_gpu_fwboot_result saved_boot;
	struct sun60i_a733_gpu_fwprobe_result saved_probe;
	struct memory_snapshot snap;
	unsigned start = checks;

	reset();
	sun60i_gpu_run_stage(&sc, 16);
	CHECK(sc.sc_run_result == EINVAL && nevents == 0 && fw_probes == 0);
	fwboot_ready();
	sun60i_gpu_run_stage(&sc, 16);
	CHECK(sc.sc_run_result == EINVAL && fw_boots == 0 && full_maps == 0);
	later_teardown();
	for (unsigned n = 0; n < 21; n++) {
		fwprobe_ready();
		switch (n) {
		case 0: sc.sc_run_stage = 14; break;
		case 1: sc.sc_run_result = EIO; break;
		case 2: sc.sc_run_pass = false; break;
		case 3: sc.sc_run_core_on = false; break;
		case 4: sc.sc_have_id = false; break;
		case 5: sc.sc_bvnc ^= 1; break;
		case 6: sc.sc_fw_memory = NULL; break;
		case 7: fwmem_live->image.info.bvnc ^= 1; break;
		case 8: fwmem_live->interface_prepared = false; break;
		case 9: sc.sc_boot_result.attempted = false; break;
		case 10: sc.sc_boot_result.device_owned = false; break;
		case 11: sc.sc_boot_result.error = EIO; break;
		case 12: sc.sc_boot_result.start.touched = false; break;
		case 13: sc.sc_boot_result.start.released = false; break;
		case 14: sc.sc_boot_result.snapshot_valid = false; break;
		case 15: sc.sc_boot_result.firmware_started = 0; break;
		case 16: sc.sc_boot_result.firmware_started = 2; break;
		case 17: sc.sc_probe_result.attempted = true; break;
		case 18: sc.sc_probe_result.published = true; break;
		case 19: sc.sc_probe_result.kick_attempted = true; break;
		case 20: sc.sc_probe_result.kicked = true; break;
		}
		saved_boot = sc.sc_boot_result;
		saved_probe = sc.sc_probe_result;
		memory_snapshot_take(&snap);
		CHECK(sun60i_gpu_stage_fwprobe(&sc, 2) == EBUSY);
		CHECK(fw_probes == 0 && full_maps == 0 && nevents == 0);
		CHECK(memcmp(&saved_boot, &sc.sc_boot_result, sizeof(saved_boot)) == 0);
		CHECK(memcmp(&saved_probe, &sc.sc_probe_result, sizeof(saved_probe)) == 0);
		sc.sc_fw_memory = fwmem_live;
		memory_snapshot_check(&snap, false);
		later_teardown();
	}
	for (size_t n = 0; n < __arraycount(failure); n++) {
		fwprobe_ready();
		switch (n) {
		case 0: missing_supply = true; break;
		case 1: state_error = EIO; break;
		case 2: supply_on = false; break;
		case 3: voltage_error = EIO; break;
		case 4: voltage = 900000; break;
		case 5: power_error = EIO; break;
		case 6: power_on = false; break;
		case 7: wait_error = ETIMEDOUT; break;
		case 8: missing_clock = true; break;
		case 9: final_ready_error = EIO; break;
		case 10: core_hz++; break;
		case 11: bus_hz++; break;
		case 12: fail_supply_call = supply_calls + 2; break;
		case 13: reg_error = EIO; break;
		case 14: binding_addr += 4096; break;
		case 15: binding_size = 0x27; break;
		case 16: map_error = ENOMEM; break;
		case 17: peek_error = EFAULT; break;
		case 18: pbvnc ^= 1; break;
		case 19: binding_size = A733_GPU_FWSTART_REG_SIZE - 1; break;
		case 20: full_map_error = ENOMEM; break;
		}
		saved_boot = sc.sc_boot_result;
		memory_snapshot_take(&snap);
		sun60i_gpu_run_stage(&sc, 16);
		probe_stopped(failure[n].error, failure[n].order, n == 20, 0, 0);
		CHECK(memcmp(&saved_boot, &sc.sc_boot_result, sizeof(saved_boot)) == 0);
		CHECK(!sc.sc_probe_result.attempted && !sc.sc_probe_result.published);
		memory_snapshot_check(&snap, false);
		later_no_replay(16);
		later_teardown();
	}
	for (size_t n = 0; n < __arraycount(reply); n++) {
		fwprobe_ready();
		fw_probe_error = probe_reply.error = reply[n].error;
		probe_reply.command = A733_GPU_FWPROBE_LOGTYPE;
		probe_reply.attempted = reply[n].attempted;
		probe_reply.published = reply[n].published;
		probe_reply.kick_attempted = reply[n].kick_attempted;
		probe_reply.kicked = reply[n].kicked;
		probe_reply.slot = 0;
		probe_reply.polls = 19;
		probe_reply.before.cmds_executed = 21;
		probe_reply.after = (struct sun60i_a733_gpu_fwprobe_snapshot){
			.read_offset = 1, .write_offset = 1, .rtn = 0x2,
			.fw_read_offset = 2, .fw_write_offset = 3, .fw_command = 0x1234,
			.cmds_executed = 22, .pow_state = 1, .hwr_state = 0x20,
			.fw_faults = 7, .log_type = 0, .firmware_started = 1,
			.fw_state = 2, .os_state = 3, .alive_fw_token = 45, .alive_os_token = 67,
			.compchecks_updated = 1, .hw_bvnc = UINT64_C(0x00240038006800b7),
			.fw_bvnc = UINT64_C(0x00240038006800b7), .ddk_build = 6976702
		};
		saved_boot = sc.sc_boot_result;
		memory_snapshot_take(&snap);
		sun60i_gpu_run_stage(&sc, 16);
		probe_stopped(reply[n].error, "ASVsPKCFcASVsPMIUXLZ", 1, 1, 1);
		CHECK(memcmp(&saved_boot, &sc.sc_boot_result, sizeof(saved_boot)) == 0);
		CHECK(memcmp(&probe_reply, &sc.sc_probe_result, sizeof(probe_reply)) == 0);
		CHECK(maps == 1 && peeks == 1 && unmaps == 1);
		CHECK(strstr(sc.sc_probe_state, "slot=0 polls=19 before_read=0 before_write=0 after_read=1 after_write=1 rtn=0x2") != NULL);
		CHECK(strstr(sc.sc_probe_state, "fw_read=2 fw_write=3 fw_command=0x1234 executed_before=21 executed_after=22") != NULL);
		CHECK(strstr(sc.sc_probe_state, "power=1 hwr=0x20 faults=7 log=0x0 started=1") != NULL);
		CHECK(strstr(sc.sc_probe_state, "fw_state=2 os_state=3 alive_fw=45 alive_os=67 compchecks=1") != NULL);
		CHECK(strstr(sc.sc_probe_state, "hw_bvnc=0x00240038006800b7 fw_bvnc=0x00240038006800b7 ddk_build=6976702") != NULL);
		memory_snapshot_check(&snap, false);
		later_no_replay(16);
		later_teardown();
	}
	reset();
	printf("PASS: %u firmware command consumer checks\n", checks - start);
}

static void
fwcache_ready(void)
{
	fwprobe_ready();
	probe_reply.attempted = probe_reply.published = true;
	probe_reply.kick_attempted = probe_reply.kicked = true;
	probe_reply.command = A733_GPU_FWPROBE_LOGTYPE;
	probe_reply.polls = 2;
	probe_reply.after.rtn = 1;
	probe_reply.after.read_offset = probe_reply.after.write_offset = 1;
	sun60i_gpu_run_stage(&sc, 16);
	CHECK(sc.sc_run_result == 0 && sc.sc_run_stage == 16);
	CHECK(fw_probes == 1 && fixture_device_owned);
	snprintf(sc.sc_cache_state, sizeof(sc.sc_cache_state),
	    "MMUCACHE not yet published");
	full_maps = full_unmaps = 0;
	later_begin();
}

static void
cache_stopped(int wanted, const char *order, unsigned mmap_calls,
    unsigned munmap_calls, unsigned cache_calls)
{
	char prefix[160];
	const struct sun60i_a733_gpu_fwprobe_result *r = &sc.sc_cache_result;

	CHECK(sc.sc_run_result == (u_int)wanted && sc.sc_run_stage == 17);
	CHECK(strcmp(events, order) == 0);
	snprintf(prefix, sizeof(prefix),
	    "error=%d attempted=%u published=%u kick_attempted=%u kicked=%u device_owned=1 ",
	    wanted, r->attempted, r->published, r->kick_attempted, r->kicked);
	CHECK(strncmp(sc.sc_cache_state, prefix, strlen(prefix)) == 0);
	CHECK(full_maps == mmap_calls && full_unmaps == munmap_calls);
	CHECK(fw_caches == cache_calls && !full_mapping && fw_probes == 1);
	CHECK(fw_boots == 1 && fw_interfaces == 1 && fwmem_destroys == 0);
	CHECK(fixture_device_owned && sc.sc_boot_result.device_owned);
	CHECK(!regulator.acquired && !clock_fixture.acquired);
	CHECK(strcmp(sc.sc_memory_state, later_memory_before) == 0);
	CHECK(strcmp(sc.sc_interface_state, later_interface_before) == 0);
	CHECK(strcmp(sc.sc_boot_state, later_boot_before) == 0);
	CHECK(strcmp(sc.sc_probe_state, later_probe_before) == 0);
	CHECK(memcmp(&sc.sc_probe_result, &probe_reply, sizeof(probe_reply)) == 0);
}

static void
test_fwcache_stage(void)
{
	static const struct { int error; const char *order; } failure[] = {
		{ ENXIO, "A" }, { EIO, "ASs" }, { EBUSY, "ASs" },
		{ EIO, "ASVs" }, { EOPNOTSUPP, "ASVs" },
		{ EIO, "ASVsP" }, { EBUSY, "ASVsP" },
		{ ETIMEDOUT, "ASVsPK" }, { ENXIO, "ASVsPKC" },
		{ EIO, "ASVsPKCFc" }, { EBUSY, "ASVsPKCFc" },
		{ EBUSY, "ASVsPKCFc" }, { EIO, "ASVsPKCFcASs" },
		{ EINVAL, "ASVsPKCFcASVsP" },
		{ EINVAL, "ASVsPKCFcASVsP" },
		{ EINVAL, "ASVsPKCFcASVsP" },
		{ ENOMEM, "ASVsPKCFcASVsPM" },
		{ EFAULT, "ASVsPKCFcASVsPMIU" },
		{ ENODEV, "ASVsPKCFcASVsPMIU" },
		{ EINVAL, "ASVsPKCFcASVsPMIU" },
		{ ENOMEM, "ASVsPKCFcASVsPMIUX" }
	};
	static const struct {
		int error;
		bool attempted, published, kick_attempted, kicked;
	} reply[] = {
		{ EINVAL, false, false, false, false },
		{ EOPNOTSUPP, true, false, false, false },
		{ EBUSY, true, false, false, false },
		{ EPROTO, true, false, false, false },
		{ EFAULT, true, true, true, false },
		{ ETIMEDOUT, true, true, true, true },
		{ EPROTO, true, true, true, true },
		{ 0, true, true, true, true }
	};
	struct sun60i_a733_gpu_fwboot_result saved_boot;
	struct sun60i_a733_gpu_fwprobe_result saved_probe, saved_logtype;
	struct memory_snapshot snap;
	unsigned start = checks;

	reset();
	sun60i_gpu_run_stage(&sc, 17);
	CHECK(sc.sc_run_result == EINVAL && nevents == 0 && fw_caches == 0);
	fwprobe_ready();
	sun60i_gpu_run_stage(&sc, 17);
	CHECK(sc.sc_run_result == EINVAL && fw_caches == 0 && full_maps == 0);
	later_teardown();
	for (unsigned n = 0; n < 38; n++) {
		fwcache_ready();
		switch (n) {
		case 0: sc.sc_run_stage = 15; break;
		case 1: sc.sc_run_result = EIO; break;
		case 2: sc.sc_run_pass = false; break;
		case 3: sc.sc_run_core_on = false; break;
		case 4: sc.sc_have_id = false; break;
		case 5: sc.sc_bvnc ^= 1; break;
		case 6: sc.sc_fw_memory = NULL; break;
		case 7: fwmem_live->image.info.bvnc ^= 1; break;
		case 8: fwmem_live->interface_prepared = false; break;
		case 9: sc.sc_boot_result.attempted = false; break;
		case 10: sc.sc_boot_result.device_owned = false; break;
		case 11: sc.sc_boot_result.error = EIO; break;
		case 12: sc.sc_boot_result.start.touched = false; break;
		case 13: sc.sc_boot_result.start.released = false; break;
		case 14: sc.sc_boot_result.snapshot_valid = false; break;
		case 15: sc.sc_boot_result.firmware_started = 0; break;
		case 16: sc.sc_boot_result.firmware_started = 2; break;
		case 17: sc.sc_cache_result.attempted = true; break;
		case 18: sc.sc_cache_result.published = true; break;
		case 19: sc.sc_cache_result.kick_attempted = true; break;
		case 20: sc.sc_cache_result.kicked = true; break;
		case 21: sc.sc_probe_result.attempted = false; break;
		case 22: sc.sc_probe_result.published = false; break;
		case 23: sc.sc_probe_result.kick_attempted = false; break;
		case 24: sc.sc_probe_result.kicked = false; break;
		case 25: sc.sc_probe_result.error = EIO; break;
		case 26: sc.sc_probe_result.command = A733_GPU_FWPROBE_MMUCACHE; break;
		case 27: sc.sc_probe_result.slot = 1; break;
		case 28: sc.sc_probe_result.polls = 1; break;
		case 29: sc.sc_probe_result.polls = A733_GPU_FWPROBE_MAX_POLLS + 1; break;
		case 30: sc.sc_probe_result.before.read_offset = 1; break;
		case 31: sc.sc_probe_result.before.write_offset = 1; break;
		case 32: sc.sc_probe_result.after.read_offset = 0; break;
		case 33: sc.sc_probe_result.after.write_offset = 0; break;
		case 34: sc.sc_probe_result.after.rtn = 2; break;
		case 35: sc.sc_probe_result.cache_flags = 1; break;
		case 36: sc.sc_probe_result.sync_fw_addr = 1; break;
		case 37: sc.sc_probe_result.sync_update_value = 1; break;
		}
		saved_boot = sc.sc_boot_result;
		saved_logtype = sc.sc_probe_result;
		saved_probe = sc.sc_cache_result;
		memory_snapshot_take(&snap);
		CHECK(sun60i_gpu_stage_fwcache(&sc, 2) == EBUSY);
		CHECK(fw_caches == 0 && full_maps == 0 && nevents == 0);
		CHECK(memcmp(&saved_boot, &sc.sc_boot_result, sizeof(saved_boot)) == 0);
		CHECK(memcmp(&saved_logtype, &sc.sc_probe_result, sizeof(saved_logtype)) == 0);
		CHECK(memcmp(&saved_probe, &sc.sc_cache_result, sizeof(saved_probe)) == 0);
		sc.sc_fw_memory = fwmem_live;
		memory_snapshot_check(&snap, false);
		later_teardown();
	}
	for (size_t n = 0; n < __arraycount(failure); n++) {
		fwcache_ready();
		switch (n) {
		case 0: missing_supply = true; break;
		case 1: state_error = EIO; break;
		case 2: supply_on = false; break;
		case 3: voltage_error = EIO; break;
		case 4: voltage = 900000; break;
		case 5: power_error = EIO; break;
		case 6: power_on = false; break;
		case 7: wait_error = ETIMEDOUT; break;
		case 8: missing_clock = true; break;
		case 9: final_ready_error = EIO; break;
		case 10: core_hz++; break;
		case 11: bus_hz++; break;
		case 12: fail_supply_call = supply_calls + 2; break;
		case 13: reg_error = EIO; break;
		case 14: binding_addr += 4096; break;
		case 15: binding_size = 0x27; break;
		case 16: map_error = ENOMEM; break;
		case 17: peek_error = EFAULT; break;
		case 18: pbvnc ^= 1; break;
		case 19: binding_size = A733_GPU_FWSTART_REG_SIZE - 1; break;
		case 20: full_map_error = ENOMEM; break;
		}
		saved_boot = sc.sc_boot_result;
		saved_logtype = sc.sc_probe_result;
		memory_snapshot_take(&snap);
		sun60i_gpu_run_stage(&sc, 17);
		cache_stopped(failure[n].error, failure[n].order, n == 20, 0, 0);
		CHECK(memcmp(&saved_boot, &sc.sc_boot_result, sizeof(saved_boot)) == 0);
		CHECK(memcmp(&saved_logtype, &sc.sc_probe_result, sizeof(saved_logtype)) == 0);
		CHECK(!sc.sc_cache_result.attempted && !sc.sc_cache_result.published);
		memory_snapshot_check(&snap, false);
		later_no_replay(17);
		later_teardown();
	}
	for (size_t n = 0; n < __arraycount(reply); n++) {
		fwcache_ready();
		fw_cache_error = cache_reply.error = reply[n].error;
		cache_reply.command = A733_GPU_FWPROBE_MMUCACHE;
		cache_reply.cache_flags = 0x0400001f;
		cache_reply.sync_fw_addr = 0xc0123450;
		cache_reply.sync_update_value = 0;
		cache_reply.attempted = reply[n].attempted;
		cache_reply.published = reply[n].published;
		cache_reply.kick_attempted = reply[n].kick_attempted;
		cache_reply.kicked = reply[n].kicked;
		cache_reply.slot = 1;
		cache_reply.polls = 19;
		cache_reply.before.cmds_executed = 21;
		cache_reply.after = (struct sun60i_a733_gpu_fwprobe_snapshot){
			.read_offset = 1, .write_offset = 1, .rtn = 0x2,
			.fw_read_offset = 2, .fw_write_offset = 3, .fw_command = 0x1234,
			.cmds_executed = 22, .pow_state = 1, .hwr_state = 0x20,
			.fw_faults = 7, .log_type = 0, .firmware_started = 1,
			.fw_state = 2, .os_state = 3, .alive_fw_token = 45, .alive_os_token = 67,
			.compchecks_updated = 1, .hw_bvnc = UINT64_C(0x00240038006800b7),
			.fw_bvnc = UINT64_C(0x00240038006800b7), .ddk_build = 6976702
		};
		saved_boot = sc.sc_boot_result;
		saved_logtype = sc.sc_probe_result;
		memory_snapshot_take(&snap);
		sun60i_gpu_run_stage(&sc, 17);
		cache_stopped(reply[n].error, "ASVsPKCFcASVsPMIUXTZ", 1, 1, 1);
		CHECK(memcmp(&saved_boot, &sc.sc_boot_result, sizeof(saved_boot)) == 0);
		CHECK(memcmp(&saved_logtype, &sc.sc_probe_result, sizeof(saved_logtype)) == 0);
		CHECK(memcmp(&cache_reply, &sc.sc_cache_result, sizeof(cache_reply)) == 0);
		CHECK(maps == 1 && peeks == 1 && unmaps == 1);
		CHECK(strstr(sc.sc_cache_state, "command=0x2abc0066 cache_flags=0x400001f sync_fw=0xc0123450 sync_value=0 mmucache_sync=0") != NULL);
		CHECK(strstr(sc.sc_cache_state, "slot=1 polls=19 before_read=0 before_write=0 after_read=1 after_write=1 rtn=0x2") != NULL);
		CHECK(strstr(sc.sc_cache_state, "fw_read=2 fw_write=3 fw_command=0x1234 executed_before=21 executed_after=22") != NULL);
		CHECK(strstr(sc.sc_cache_state, "power=1 hwr=0x20 faults=7 log=0x0 started=1") != NULL);
		CHECK(strstr(sc.sc_cache_state, "fw_state=2 os_state=3 alive_fw=45 alive_os=67 compchecks=1") != NULL);
		CHECK(strstr(sc.sc_cache_state, "hw_bvnc=0x00240038006800b7 fw_bvnc=0x00240038006800b7 ddk_build=6976702") != NULL);
		memory_snapshot_check(&snap, false);
		later_no_replay(17);
		later_teardown();
	}
	reset();
	printf("PASS: %u MMUCACHE consumer checks\n", checks - start);
}

static void
test_probe_report_capacity(void)
{
	struct sun60i_a733_gpu_fwboot_result boot = { .device_owned = true };
	struct sun60i_a733_gpu_fwprobe_result r = {
		.attempted = true, .published = true, .kick_attempted = true,
		.kicked = true, .slot = UINT32_MAX, .polls = UINT32_MAX,
		.command = UINT32_MAX, .cache_flags = UINT32_MAX,
		.sync_fw_addr = UINT32_MAX, .sync_update_value = UINT32_MAX
	};
	struct {
		uint64_t before;
		char state[1024];
		uint64_t after;
	} output = { .before = 0x12345678, .after = 0x87654321 };
	unsigned start = checks;

	/* These snapshots contain only unsigned integers, never C bool fields. */
	memset(&r.before, 0xff, sizeof(r.before));
	memset(&r.after, 0xff, sizeof(r.after));
	sun60i_gpu_probe_report(output.state, sizeof(output.state), &boot, &r,
	    INT32_MIN);
	CHECK(output.before == 0x12345678 && output.after == 0x87654321);
	CHECK(strlen(output.state) < sizeof(output.state) - 1);
	CHECK(strncmp(output.state, "error=-2147483648 attempted=1", 28) == 0);
	CHECK(strstr(output.state, "hw_bvnc=0xffffffffffffffff fw_bvnc=0xffffffffffffffff ddk_build=4294967295") != NULL);
	CHECK(strstr(output.state, "command=0xffffffff cache_flags=0xffffffff sync_fw=0xffffffff sync_value=4294967295 mmucache_sync=4294967295") != NULL);
	printf("PASS: %u maximum report checks (%zu bytes)\n", checks - start,
	    strlen(output.state));
}

/* The real stage-21 formatter must retain the observed word 117 and bounds. */
static void
test_trace_report(void)
{
	static const unsigned lengths[] = { 1, 64, 118, 256, 257, 512 };
	struct sun60i_a733_gpu_fwmem mem = { 0 }, saved;
	struct sun60i_a733_gpu_fwinit_region *ctl, *data;
	struct rogue_fwif_tracebuf *tc;
	struct sun60i_gpu_softc *owner;
	struct sun60i_a733_gpu_fwprobe_result previous;
	uint8_t *arena, *copy;
	uint32_t *words;
	char expected[128], word[9];
	const char *payload;
	unsigned start = checks, captured;

	owner = calloc(1, sizeof(*owner));
	arena = calloc(1, 3072);
	copy = malloc(3072);
	assert(owner != NULL && arena != NULL && copy != NULL);
	mem.interface_prepared = true;
	mem.work.cpu = arena;
	mem.work.size = 3072;
	mem.work.map = &mem;
	mem.work.loaded = true;
	mem.interface_layout.object[A733_GPU_FWINIT_TRACE_CTL] =
	    (struct sun60i_a733_gpu_fwinit_region){
		.buffer = A733_GPU_FWINIT_WORK, .offset = 0, .size = sizeof(*tc)
	    };
	mem.interface_layout.object[A733_GPU_FWINIT_TRACE0] =
	    (struct sun60i_a733_gpu_fwinit_region){
		.buffer = A733_GPU_FWINIT_WORK, .offset = 1024, .size = 2048
	    };
	owner->sc_fw_memory = &mem;
	tc = (struct rogue_fwif_tracebuf *)arena;
	words = (uint32_t *)(arena + 1024);
	for (unsigned k = 0; k < 512; k++)
		words[k] = UINT32_C(0xa5000000) + k;
	tc->tracebuf[0].trace_pointer = 118;
	tc->log_type = 0x3f;
	for (unsigned n = 0; n < __arraycount(lengths); n++) {
		tc->tracebuf_size_in_dwords = lengths[n];
		memcpy(copy, arena, 3072);
		trace_sync_calls = 0;
		sun60i_gpu_trace_report(owner);
		payload = strchr(owner->sc_trace_state, ':');
		CHECK(payload != NULL);
		payload++;
		/* This assertion detects the old 64-word limit independently. */
		if (lengths[n] >= 118)
			CHECK(strstr(payload, "a5000075") != NULL);
		captured = lengths[n] < 256 ? lengths[n] : 256;
		snprintf(expected, sizeof(expected),
		    "capture_error=0 total=%u captured=%u truncated=%u:",
		    lengths[n], captured, lengths[n] > captured);
		CHECK(strstr(owner->sc_trace_state, expected) != NULL);
		CHECK(strlen(payload) == captured * 8);
		for (unsigned k = 0; k < captured; k++) {
			snprintf(word, sizeof(word), "%08x", UINT32_C(0xa5000000) + k);
			CHECK(strncmp(payload + k * 8, word, 8) == 0);
		}
		CHECK(trace_sync_calls == 1);
		CHECK(memcmp(arena, copy, 3072) == 0);
	}
	/* A wrapped or malformed firmware pointer never becomes a RAM offset. */
	tc->tracebuf[0].trace_pointer = UINT32_MAX;
	tc->tracebuf[1].trace_pointer = UINT32_MAX;
	tc->log_type = UINT32_MAX;
	owner->sc_trace_result.error = INT32_MIN;
	owner->sc_trace_result.polls = UINT32_MAX;
	memset(&owner->sc_trace_result.before, 0xff,
	    sizeof(owner->sc_trace_result.before));
	memset(&owner->sc_trace_result.after, 0xff,
	    sizeof(owner->sc_trace_result.after));
	previous = owner->sc_trace_result;
	sun60i_gpu_trace_report(owner);
	CHECK(strlen(owner->sc_trace_state) < sizeof(owner->sc_trace_state) - 1);
	CHECK(strstr(owner->sc_trace_state,
	    "ptr0=4294967295 ptr1=4294967295 trace_rtn=0xffffffff") != NULL);
	CHECK(strstr(owner->sc_trace_state, "a50000ff") != NULL);
	CHECK(memcmp(&previous, &owner->sc_trace_result, sizeof(previous)) == 0);
	saved = mem;
	ctl = &mem.interface_layout.object[A733_GPU_FWINIT_TRACE_CTL];
	data = &mem.interface_layout.object[A733_GPU_FWINIT_TRACE0];
	for (unsigned n = 0; n < 20; n++) {
		mem = saved;
		owner->sc_fw_memory = &mem;
		tc->tracebuf_size_in_dwords = 512;
		switch (n) {
		case 0: owner->sc_fw_memory = NULL; break;
		case 1: mem.interface_prepared = false; break;
		case 2: mem.work.cpu = NULL; break;
		case 3: mem.work.map = NULL; break;
		case 4: mem.work.loaded = false; break;
		case 5: mem.work.cpu = arena + 1; break;
		case 6: ctl->buffer = A733_GPU_FWINIT_CONFIG; break;
		case 7: data->buffer = A733_GPU_FWINIT_CONFIG; break;
		case 8: ctl->offset = 1; break;
		case 9: data->offset = 1025; break;
		case 10: ctl->size = sizeof(*tc) - 1; break;
		case 11: ctl->offset = UINT32_MAX - 7; break;
		case 12: ctl->size = UINT32_MAX; break;
		case 13: data->offset = UINT32_MAX - 3; break;
		case 14: data->size = UINT32_MAX - 3; break;
		case 15: data->size = 0; break;
		case 16: data->size = 2047; break;
		case 17: tc->tracebuf_size_in_dwords = 513; break;
		case 18: tc->tracebuf_size_in_dwords = 0; break;
		case 19: mem.work.size = 3071; break;
		}
		trace_sync_calls = 0;
		memcpy(copy, arena, 3072);
		sun60i_gpu_trace_report(owner);
		snprintf(expected, sizeof(expected), "capture_error=%d ",
		    n == 17 || n == 18 ? EPROTO : EINVAL);
		CHECK(strstr(owner->sc_trace_state, expected) != NULL);
		CHECK(strstr(owner->sc_trace_state, "captured=0 ") != NULL);
		payload = strchr(owner->sc_trace_state, ':');
		CHECK(payload != NULL && payload[1] == '\0');
		CHECK(trace_sync_calls == (n == 17 || n == 18 ? 1U : 0U));
		CHECK(memcmp(copy, arena, 3072) == 0);
	}
	free(copy);
	free(arena);
	free(owner);
	printf("PASS: %u bounded firmware trace capture checks\n", checks - start);
}

/* Exercise the real cached HWR capture, not a separate record decoder. */
static void
test_hwr_report(void)
{
	struct sun60i_a733_gpu_fwmem mem = { 0 }, saved;
	struct sun60i_a733_gpu_fwinit_region *region;
	struct sun60i_gpu_softc *owner, *previous;
	struct rogue_fwif_hwrinfobuf *hwr;
	uint8_t *arena, *copy;
	char expected[160];
	const char *last;
	size_t events_before;
	unsigned commands_before, allocs_before, frees_before, start = checks;

	owner = calloc(1, sizeof(*owner));
	previous = malloc(sizeof(*previous));
	arena = calloc(1, 2432);
	copy = malloc(2432);
	assert(owner != NULL && previous != NULL && arena != NULL && copy != NULL);
	mem.interface_prepared = true;
	mem.work.cpu = arena;
	mem.work.size = 2432;
	mem.work.loaded = true;
	mem.work.map = &mem;
	region = &mem.interface_layout.object[A733_GPU_FWINIT_HWR];
	*region = (struct sun60i_a733_gpu_fwinit_region){
		.buffer = A733_GPU_FWINIT_WORK, .offset = 64, .size = 2336
	};
	owner->sc_fw_memory = &mem;
	hwr = (struct rogue_fwif_hwrinfobuf *)(arena + 64);
	memcpy(copy, arena, 2432);
	trace_sync_calls = 0;
	allocs_before = kmem_alloc_calls;
	frees_before = kmem_free_calls;
	sun60i_gpu_hwr_report(owner);
	CHECK(kmem_alloc_calls == allocs_before + 1);
	CHECK(kmem_free_calls == frees_before + 1);
	CHECK(kmem_last_size == sizeof(*hwr));
	CHECK(strstr(owner->sc_hwr_state,
	    "capture_error=0 captured=16 counter=0 write_index=0") != NULL);
	CHECK(strstr(owner->sc_hwr_state, "slot=15 number=0 type=0") != NULL);
	CHECK(memcmp(copy, arena, 2432) == 0);
	CHECK(trace_sync_calls == 1);
	CHECK(trace_sync_offset == 64 && trace_sync_size == 2336 &&
	    trace_sync_op == BUS_DMASYNC_POSTREAD);

	/* Fill every field to its maximum: counters must not become bounds. */
	memset(hwr, 0xff, sizeof(*hwr));
	hwr->hwr_info[0].hwr_type = ROGUE_HWRTYPE_BIF0FAULT;
	hwr->hwr_info[0].hwr_data.bif_info.bif_req_status =
	    UINT64_C(0x123456789abcdef0);
	hwr->hwr_info[0].hwr_data.bif_info.bif_mmu_status = UINT64_C(0x15);
	hwr->hwr_info[0].hwr_data.bif_info.pc_address = UINT64_C(0x40165000);
	hwr->hwr_info[1].hwr_type = ROGUE_HWRTYPE_MMUFAULT;
	hwr->hwr_info[1].hwr_data.mmu_info.mmu_status[0] =
	    UINT64_C(0xabcdef0012345678);
	hwr->hwr_info[1].hwr_data.mmu_info.mmu_status[1] = UINT64_C(0x13579);
	hwr->hwr_info[1].hwr_data.mmu_info.pc_address = UINT64_C(0x40165000);
	hwr->hwr_info[2].hwr_type = ROGUE_HWRTYPE_POLLFAILURE;
	hwr->hwr_counter = 0;
	memcpy(copy, arena, 2432);
	((struct rogue_fwif_hwrinfobuf *)(copy + 64))->hwr_counter =
	    UINT32_C(0x87654321);
	*previous = *owner;
	commands_before = fwprobe_cmd_calls;
	events_before = nevents;
	hwr_on_sync = hwr;
	trace_sync_calls = 0;
	allocs_before = kmem_alloc_calls;
	frees_before = kmem_free_calls;
	sun60i_gpu_hwr_report(owner);
	CHECK(kmem_alloc_calls == allocs_before + 1);
	CHECK(kmem_free_calls == frees_before + 1);
	CHECK(kmem_last_size == sizeof(*hwr));
	hwr_on_sync = NULL;
	CHECK(trace_sync_calls == 1);
	CHECK(strstr(owner->sc_hwr_state,
	    "counter=2271560481 write_index=4294967295") != NULL);
	CHECK(strstr(owner->sc_hwr_state,
	    "req=0x123456789abcdef0 mmu=0x15 pc=0x40165000;") != NULL);
	CHECK(strstr(owner->sc_hwr_state,
	    "mmu0=0xabcdef0012345678 mmu1=0x13579 pc=0x40165000;") != NULL);
	CHECK(strstr(owner->sc_hwr_state,
	    "raw0=0xffffffffffffffff raw1=0xffffffffffffffff "
	    "raw2=0xffffffffffffffff;") != NULL);
	CHECK(strstr(owner->sc_hwr_state, "dm=4294967295 core=4294967295 "
	    "pid=4294967295 frame=4294967295 hwrt=0xffffffff") != NULL);
	last = strstr(owner->sc_hwr_state, "slot=15 number=4294967295");
	CHECK(last != NULL && strstr(last, "raw2=0xffffffffffffffff;") != NULL);
	CHECK(strlen(owner->sc_hwr_state) < sizeof(owner->sc_hwr_state) - 1);
	CHECK(owner->sc_hwr_state[strlen(owner->sc_hwr_state) - 1] == ';');
	CHECK(memcmp(copy, arena, 2432) == 0);
	CHECK(commands_before == fwprobe_cmd_calls && events_before == nevents);
	memcpy(previous->sc_hwr_state, owner->sc_hwr_state,
	    sizeof(previous->sc_hwr_state));
	CHECK(memcmp(previous, owner, sizeof(*owner)) == 0);

	saved = mem;
	for (unsigned n = 0; n < 14; n++) {
		mem = saved;
		owner->sc_fw_memory = &mem;
		switch (n) {
		case 0: owner->sc_fw_memory = NULL; break;
		case 1: mem.interface_prepared = false; break;
		case 2: mem.work.cpu = NULL; break;
		case 3: mem.work.map = NULL; break;
		case 4: mem.work.loaded = false; break;
		case 5: mem.work.cpu = arena + 1; break;
		case 6: region->buffer = A733_GPU_FWINIT_CONFIG; break;
		case 7: region->offset = 1; break;
		case 8: region->size = 2335; break;
		case 9: region->offset = UINT32_MAX - 7; break;
		case 10: region->size = UINT32_MAX; break;
		case 11: region->size = 0; break;
		case 12: mem.work.size = 2399; break;
		case 13: region->offset = 2432; break;
		}
		trace_sync_calls = 0;
		memcpy(copy, arena, 2432);
		allocs_before = kmem_alloc_calls;
		frees_before = kmem_free_calls;
		sun60i_gpu_hwr_report(owner);
		CHECK(kmem_alloc_calls == allocs_before);
		CHECK(kmem_free_calls == frees_before);
		snprintf(expected, sizeof(expected), "capture_error=%d captured=0:", EINVAL);
		CHECK(strcmp(owner->sc_hwr_state, expected) == 0);
		CHECK(trace_sync_calls == 0);
		CHECK(memcmp(copy, arena, 2432) == 0);
		CHECK(commands_before == fwprobe_cmd_calls && events_before == nevents);
	}

	/* The stage-21 error exit refreshes the cache without another command. */
	owner->sc_fw_memory = NULL;
	owner->sc_run_stage = 0;
	memset(owner->sc_hwr_state, 0, sizeof(owner->sc_hwr_state));
	CHECK(sun60i_gpu_stage_fwkick(owner, 0) == EBUSY);
	snprintf(expected, sizeof(expected), "capture_error=%d captured=0:", EINVAL);
	CHECK(strcmp(owner->sc_hwr_state, expected) == 0);
	CHECK(commands_before == fwprobe_cmd_calls && events_before == nevents);
	free(copy);
	free(arena);
	free(previous);
	free(owner);
	printf("PASS: %u bounded firmware HWR capture checks\n", checks - start);
}

static void
test_scratch_error_snapshot(void)
{
	struct a733_gpu_fwcompute_file_header h = { 0 };
	struct a733_gpu_fwcompute_file_entry entry[A733_GPU_FWCOMPUTE_RESOURCES] = { 0 }, swap;
	struct sun60i_a733_gpu_fwvm vm = { 0 };
	uint32_t scratch, decoy;
	unsigned start = checks, calls;

	/* Minimal valid wire image; the existing digest stub returns zero. */
	h.magic = A733_GPU_FWCOMPUTE_FILE_MAGIC;
	h.version = A733_GPU_FWCOMPUTE_FILE_VERSION;
	h.bvnc = UINT64_C(0x00240038006800b7);
	h.header_bytes = A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY;
	h.payload_bytes = 9 * 512 + A733_GPU_FWCOMPUTE_BORDER_BYTES + 56 + 48;
	h.resources = A733_GPU_FWCOMPUTE_RESOURCES;
	h.static_offset = 9 * 512 + A733_GPU_FWCOMPUTE_BORDER_BYTES;
	h.command_offset = h.static_offset + 56;
	h.command_bytes = A733_GPU_FWCOMPUTE_COMMAND_BYTES;
	h.static_bytes = 56;
	h.context_state_va = UINT64_C(0x8000001000);
	for (unsigned i = 0; i < 9; i++) {
		entry[i].role = i;
		entry[i].offset = i * 512;
		entry[i].bytes = i == A733_GPU_FWCOMPUTE_RESUME ? 512 : 4;
		entry[i].flags = A733_GPU_FWCOMPUTE_FLAG_BYPASS;
		entry[i].va = UINT64_C(0x8000000000) + i * 4096;
	}
	entry[9].role = A733_GPU_FWCOMPUTE_BORDER_TABLE;
	entry[9].offset = 9 * 512;
	entry[9].bytes = A733_GPU_FWCOMPUTE_BORDER_BYTES;
	entry[9].flags = A733_GPU_FWCOMPUTE_FLAG_RO;
	entry[9].va = UINT64_C(0x8000010000);
	/* SCRATCH deliberately follows all programs in the manifest. */
	swap = entry[0];
	entry[0] = entry[8];
	entry[8] = swap;
	for (unsigned scenario = 0; scenario < 6; scenario++) {
		const bool unavailable = scenario == 1;

		fwcache_ready();
		compute_completion_mode = scenario < 2 ? 0 : scenario - 1;
		compute_observe_calls = 0;
		compute_file_size = h.header_bytes + h.payload_bytes;
		compute_file_data = calloc(1, compute_file_size);
		assert(compute_file_data != NULL);
		memcpy(compute_file_data, &h, sizeof(h));
		memcpy(compute_file_data + sizeof(h), entry, sizeof(entry));
		{
			const uint32_t command[12] = { 44, 0, 0x4000, 0x20,
			    0x4000, 0x80, 0x1000, 0x80,
			    (4U << 21) | (1U << 20) | (1U << 11) | (4U << 1),
			    0, 0, 0 };

			memcpy(compute_file_data + h.header_bytes + h.command_offset,
			    command, sizeof(command));
		}
		memset(compute_file_data + h.header_bytes + h.static_offset,
		    0xa5, h.static_bytes);
		sc.sc_run_stage = A733_GPU_STAGE_FWVM;
		sc.sc_cache_result = (struct sun60i_a733_gpu_fwprobe_result){
		    .attempted = true, .published = true, .kick_attempted = true,
		    .kicked = true, .command = A733_GPU_FWPROBE_MMUCACHE,
		    .slot = 1, .after.rtn = 1
		};
		sc.sc_heap_result.attempted = true;
		sc.sc_heap_result.kicked = true;
		sc.sc_fw_memory->image.info.feature_mask[0] = UINT64_C(0x92d9ff57fdbf7cdb);
		sc.sc_fw_memory->image.info.feature_mask[1] = UINT64_C(0x1f);
		sc.sc_fw_memory->image.info.brn_mask = UINT64_C(0x201);
		{
			struct a733_gpu_fwcompute_file parsed;
			struct sun60i_a733_gpu_fwcompute_image *image;
			struct rogue_fwif_cmd_compute command;
			uint8_t *bytes;

			CHECK(sun60i_gpu_compute_load(&sc, &parsed, &bytes, &image) == 0);
			memcpy(&command, image->cccb +
			    sizeof(struct rogue_fwif_ccb_cmd_header), sizeof(command));
			CHECK(command.regs.tpu_border_colour_table ==
			    (UINT64_C(0x8000010000) >> 2));
			CHECK(command.regs.cdm_resume_pds1 ==
			    ((4U << 21) | (1U << 20) | (1U << 11) | (4U << 1)));
			CHECK(command.regs.cdm_context_state_base_addr == h.context_state_va);
			kmem_free(image, sizeof(*image));
			kmem_free(bytes, compute_file_size);
		}
		vm.prepared = true;
		scratch = UINT32_C(0x12345678);
		decoy = UINT32_C(0xaabbccdd);
		vm.resource[0].cpu = &decoy;
		vm.resource[8] = (struct sun60i_a733_gpu_dma){
		    .cpu = &scratch, .size = 4, .map = &scratch, .loaded = true
		};
		sc.sc_fwvm = &vm;
		compute_scratch_dma = &vm.resource[8];
		compute_scratch_unavailable = unavailable;
		scratch_sync_map = &scratch;
		scratch_on_sync = NULL;
		scratch_sync_calls = 0;
		calls = fwprobe_cmd_calls;
		sun60i_gpu_run_stage(&sc, A733_GPU_STAGE_FWKICK);
		CHECK(sc.sc_run_result == (scenario == 2 ? 0 :
		    scenario == 5 ? ETIMEDOUT : EIO));
		CHECK(fwprobe_cmd_calls == calls + 2);
		if (scenario == 0 || scenario == 1 || scenario == 3 || scenario == 4)
			CHECK(strstr(sc.sc_kick_state, "kick error=5 ") != NULL);
		if (scenario >= 2) {
			CHECK(sc.sc_kick_result.error == 0 && sc.sc_kick_result.after.rtn == 1);
			CHECK(sc.sc_kick_result.after.fw_command == 0);
			CHECK(strstr(sc.sc_kick_state, "fresh_valid=1 ") != NULL);
			if (scenario == 3 || scenario == 4) {
				CHECK(strstr(sc.sc_kick_state, "timeline=1 ") != NULL);
				CHECK(strstr(sc.sc_kick_state, "fresh_fw_cmd=0x2abc0069 ") != NULL);
				CHECK(strstr(sc.sc_kick_state, "fresh_error=5 ") != NULL);
			}
			CHECK(compute_observe_calls == (scenario == 3 ? 1U :
			    scenario == 5 ? 40001U : 2U));
		}
		if (unavailable) {
			CHECK(strstr(sc.sc_kick_state, "scratch_valid=1/0 ") != NULL);
			CHECK(scratch_sync_calls == 1 && scratch == UINT32_C(0x12345678));
		} else {
			CHECK(strstr(sc.sc_kick_state,
			    "scratch=0x12345678->0x11223344 scratch_valid=1/1 ") != NULL);
			CHECK(scratch_sync_calls == 2 && scratch == UINT32_C(0x11223344));
		}
		CHECK(decoy == UINT32_C(0xaabbccdd));
		scratch_on_sync = NULL;
		scratch_sync_map = NULL;
		compute_scratch_dma = NULL;
		compute_completion_mode = 0;
		sc.sc_fwvm = NULL;
		free(compute_file_data);
		compute_file_data = NULL;
		later_teardown();
	}
	reset();
	printf("PASS: %u scratch error-path snapshot checks\n", checks - start);
}


/* Exercise the complete consumer sequence, with only provider boundaries fake. */
static void
native_begin(void)
{
	reset();
	native_fixture = true;
	fw_cached_state = NULL;
	core_hz = expected_interface_hz = 600000000;
	boot_reply.attempted = boot_reply.device_owned = true;
	boot_reply.start.touched = boot_reply.start.released = true;
	boot_reply.snapshot_valid = true;
	boot_reply.firmware_started = 1;
	probe_reply.attempted = probe_reply.published = true;
	probe_reply.kick_attempted = probe_reply.kicked = true;
	probe_reply.command = A733_GPU_FWPROBE_LOGTYPE;
	probe_reply.polls = 2;
	probe_reply.after.rtn = 1;
	probe_reply.after.read_offset = probe_reply.after.write_offset = 1;
	cache_reply.attempted = cache_reply.published = true;
	cache_reply.kick_attempted = cache_reply.kicked = true;
	cache_reply.command = A733_GPU_FWPROBE_MMUCACHE;
	cache_reply.slot = 1;
	cache_reply.after.rtn = 1;
	cache_reply.after.read_offset = cache_reply.after.write_offset = 2;
}

static void
native_controls_readonly(void)
{
	struct sysctlnode node = { 0 };
	const unsigned value = 1;
	const unsigned result = sc.sc_run_result;
	const size_t before = nevents;

#define CONTROL(fn) do { \
	CHECK(fn(NULL, NULL, NULL, &node, NULL, 0) == 0); \
	CHECK(fn(NULL, NULL, NULL, &node, &value, sizeof(value)) == EBUSY); \
} while (0)
	CONTROL(sun60i_gpu_sysctl_clock_source);
	CONTROL(sun60i_gpu_sysctl_stage);
	CONTROL(sun60i_gpu_sysctl_render_attach);
	CONTROL(sun60i_gpu_sysctl_exec_attach);
#undef CONTROL
	CHECK(nevents == before && sc.sc_run_result == result);
}

static void
native_attach(void)
{
	const struct fdt_attach_args faa = {
		.faa_phandle = 1, .faa_dmat = &dma_tag_cookie
	};

	sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
	CHECK(!sc.sc_diagnostic && !sc.sc_start_attempted);
	native_controls_readonly();
	config_finalize();
	CHECK(!sc.sc_start_attempted && fw_opens == 0 && maps == 0);
	CHECK(nevents == 0 && !TAILQ_EMPTY(&mountroot_config_queue));
	native_controls_readonly();
}

static void
native_run(void)
{
	root_is_mounted = true;
	config_mountroot_thread(NULL);
	CHECK(mountroot_exits == 1 && TAILQ_EMPTY(&mountroot_config_queue));
	CHECK(sc.sc_start_attempted && sc.sc_clock_source == 1);
	CHECK(prepares == 0);
}

static void
native_no_replay(void)
{
	const size_t before = nevents;
	const unsigned result = sc.sc_run_result;

	native_controls_readonly();
	sun60i_gpu_start(&gpu_device);
	CHECK(config_finalize_register(&gpu_device, sun60i_gpu_finalize) == 0);
	CHECK(nevents == before && sc.sc_run_result == result);
	CHECK(TAILQ_EMPTY(&mountroot_config_queue));
	/* Fixture-only reboot. Production retains this mapping and DMA memory. */
	full_mapping = false;
	if (fwmem_live != NULL)
		later_teardown();
}

static void
test_native_startup(void)
{
	unsigned start = checks;

	for (unsigned failure = 0; failure <= 20; failure++) {
		native_begin();
		native_attach();
		switch (failure) {
		case 1: state_error = EIO; break;
		case 2: power_error = EIO; break;
		case 3: power_reserve_error = EIO; break;
		case 4: reserve_error = EIO; break;
		case 5: retain_error = EIO; break;
		case 6: prepare_error = EIO; break;
		case 7: ready_error = EIO; break;
		case 8: request_error = EIO; break;
		case 9: wait_error = EIO; break;
		case 10: coreid_peek_error = EIO; break;
		case 11: native_identify_error = true; break;
		case 12: fw_open_error = EIO; break;
		case 13: fwmem_prepare_error = EIO; break;
		case 14: fw_interface_error = EIO; break;
		case 15: fw_boot_error = boot_reply.error = EIO; break;
		case 16: fw_probe_error = probe_reply.error = EIO; break;
		case 17: fw_cache_error = cache_reply.error = EIO; break;
		case 18: native_render_error = EIO; break;
		case 19: native_prepare_error = EIO; break;
		case 20: native_publish_error = EIO; break;
		}
		native_run();
		if (failure == 0) {
			CHECK(sc.sc_run_result == 0 && sc.sc_run_pass);
			CHECK(sc.sc_run_stage == 17 && sc.sc_fwbridge.registered);
			CHECK(native_renders == 1 && native_prepares == 1 && native_publishes == 1);
			CHECK(fw_opens == 2 && fw_closes == 2 && fw_frees == 2);
			CHECK(fw_boots == 1 && fw_probes == 1 && fw_caches == 1);
		} else {
			CHECK(sc.sc_run_result != 0 && !sc.sc_run_pass);
			CHECK(sc.sc_run_stage == (failure <= 17 ? failure : 17));
			CHECK(!sc.sc_fwbridge.registered);
			CHECK(native_renders == (failure >= 18));
			CHECK(native_prepares == (failure >= 19));
			CHECK(native_publishes == (failure == 20));
		}
		CHECK(unreserves == 0 && fwmem_destroys == 0);
		CHECK(sc.sc_retained == (failure == 0 || failure > 5));
		native_no_replay();
	}
	/* Only a request timeout can continue to the read-only CORE waiter. */
	native_begin();
	native_attach();
	request_error = ETIMEDOUT;
	native_run();
	CHECK(sc.sc_run_result == 0 && sc.sc_fwbridge.registered);
	native_no_replay();

	/* A late attach uses the same mountroot callback synchronously. */
	native_begin();
	root_is_mounted = true;
	config_finalize();
	{
		const struct fdt_attach_args faa = {
			.faa_phandle = 1, .faa_dmat = &dma_tag_cookie
		};
		sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
	}
	CHECK(sc.sc_start_attempted && sc.sc_fwbridge.registered);
	CHECK(TAILQ_EMPTY(&mountroot_config_queue));
	native_no_replay();

	/* Invalid native bindings never admit provider or firmware access. */
	for (unsigned mode = 0; mode < 7; mode++) {
		const struct fdt_attach_args faa = {
			.faa_phandle = 1, .faa_dmat = &dma_tag_cookie
		};

		native_begin();
		switch (mode) {
		case 0: observe_len = 1; break;
		case 1: prepare_len = 1; break;
		case 2: request_len = 1; break;
		case 3: request_len = 0; break;
		case 4: managed_len = -1; break;
		case 5: binding_size = A733_GPU_FWSTART_REG_SIZE - 1; break;
		case 6: binding_size = A733_GPU_ID_SIZE; break;
		}
		sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
		config_finalize();
		CHECK(sc.sc_run_result == EINVAL && !sc.sc_start_attempted);
		CHECK(TAILQ_EMPTY(&mountroot_config_queue) && nevents == 0);
		native_controls_readonly();
	}

	/* Registration failure must not leave a second startup opportunity. */
	native_begin();
	CHECK(config_finalize_register(&gpu_device, sun60i_gpu_finalize) == 0);
	{
		const struct fdt_attach_args faa = {
			.faa_phandle = 1, .faa_dmat = &dma_tag_cookie
		};
		sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
	}
	CHECK(sc.sc_run_result == EEXIST && !sc.sc_run_pass);
	config_finalize();
	CHECK(TAILQ_EMPTY(&mountroot_config_queue) && !sc.sc_start_attempted);
	CHECK(nevents == 0 && fw_opens == 0);
	reset();
	printf("PASS: %u native mountroot startup checks\n", checks - start);
}


int
main(void)
{
	const struct fdt_attach_args faa = {
		.faa_phandle = 1, .faa_dmat = &dma_tag_cookie
	};
	const int failures[] = { ENXIO, EIO, ETIMEDOUT, EOPNOTSUPP, EINVAL };
	const int bad_lengths[] = { -1, 0, 3, 4, 7, 12 };
	const u_int bad_rates[] = { 0, 300000000, 401000000, 800000000 };
	const u_int bad_volts[] = { 0, 500000, 799999, 810000 };
	const uint64_t ids[] = { 0, UINT64_MAX, UINT64_C(0x00240037006800b7),
	    UINT64_C(0x00230038006800b7), UINT64_C(0x00240038006700b7),
	    UINT64_C(0x00240038006800b6) };

	const char *guard_case = getenv("GPU_ID_CORE_GUARD_CASE");

	if (guard_case != NULL && strcmp(guard_case, "runtime") != 0 &&
	    strcmp(guard_case, "boot") != 0 &&
	    strcmp(guard_case, "direct") != 0 &&
	    strcmp(guard_case, "nonprepare") != 0)
		return 1;
	test_core_mmio_guard(guard_case);
	if (guard_case != NULL)
		return 0;
	reset();
	/* /soc finished before a later root FDT pass attaches its GPU child. */
	config_process_deferred(&deferred_config_queue, &soc_device);
	config_defer(&gpu_device, legacy_probe);
	config_process_deferred(&deferred_config_queue, &gpu_device);
	config_process_deferred(&deferred_config_queue, &root_device);
	CHECK(legacy_callbacks == 0 && gpu_device.dv_pending == 1);
	CHECK(!TAILQ_EMPTY(&config_pending));
	/* An artificial second /soc drain is needed to clean the old queue. */
	config_process_deferred(&deferred_config_queue, &soc_device);
	CHECK(legacy_callbacks == 1 && TAILQ_EMPTY(&config_pending));
	reset();
	CHECK(sun60i_gpu_match(NULL, NULL, (void *)&faa) == 1);
	match_result = 0;
	CHECK(sun60i_gpu_match(NULL, NULL, (void *)&faa) == 0);
	CHECK(nevents == 0);
	missing_supply = true;
	prepare_len = 0;
	config_process_deferred(&deferred_config_queue, &soc_device);
	sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
	CHECK(sc.sc_dmat == faa.faa_dmat);
	config_process_deferred(&deferred_config_queue, &gpu_device);
	config_process_deferred(&deferred_config_queue, &root_device);
	CHECK(!TAILQ_EMPTY(&config_finalize_list) && nevents == 0);
	CHECK(TAILQ_EMPTY(&config_pending));
	/* Remaining FDT passes attach the providers before finalization. */
	missing_supply = false;
	prepare_len = 0;
	CHECK(config_finalize_register(&root_device, request_extra_pass) == 0);
	config_finalize();
	CHECK(config_finalize_done && extra_passes == 2);
	CHECK(TAILQ_EMPTY(&config_finalize_list) && TAILQ_EMPTY(&config_pending));
	CHECK(strcmp(events, "ASVPCQPCSVHWQKSVKFMIUcs") == 0);
	CHECK(maps == 1 && peeks == 1 && unmaps == 1 &&
	    clock_puts == 1 && releases == 1);
	CHECK(strstr(normal_output, "0x00240038006800b7: 36.56.104.183") != NULL);
	CHECK(strstr(normal_output, "expected A733 GPU") != NULL);
	CHECK(error_output[0] == '\0');
	/* Late registration invokes the real hook immediately, still only once. */
	CHECK(config_finalize_register(&gpu_device, sun60i_gpu_finalize) == 0);
	CHECK(peeks == 1);
	reset();
	missing_supply = true;
	observe_len = 0;
	sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
	config_finalize();
	CHECK(config_finalize_done && TAILQ_EMPTY(&config_pending));
	CHECK(TAILQ_EMPTY(&config_finalize_list));
	CHECK(maps == 0 && peeks == 0);
	CHECK(strstr(error_output, "supply provider") != NULL);
	reset();
	core_hz = 600000000;
	unavailable(EBUSY);

	for (u_int i = 0; i < __arraycount(bad_lengths); i++) {
		reset();
		pd_len = bad_lengths[i];
		unavailable(EINVAL);
		CHECK(nevents == 0);
		reset();
		clock_len = bad_lengths[i];
		unavailable(EINVAL);
		CHECK(nevents == 0);
	}
	for (int i = 0; i < 13; i++) {
		reset();
		switch (i) {
		case 0: managed_len = -1; break;
		case 1: managed_len = 4; break;
		case 2: binding_addr += 0x1000; break;
		case 3: binding_size = 0x27; break;
		case 4: pd[1] = swap32(6); break;
		case 5: power_cells = 0; break;
		case 6: bad_power_compat = true; break;
		case 7: clocks[1] = swap32(A733_CLK_NPU); break;
		case 8: clock_cells = 0; break;
		case 9: supply_len = 8; break;
		case 10: bad_supply = true; break;
		case 11: bad_pmic = true; break;
		case 12: clocks[0] = swap32(99); break;
		}
		unavailable(EINVAL);
		CHECK(nevents == 0);
	}
	reset(); reg_error = ENOENT; unavailable(ENOENT);
	reset(); missing_supply = true; unavailable(ENXIO);
	CHECK(releases == 0 && clock_puts == 0);
	reset(); missing_clock = true; unavailable(ENXIO);
	CHECK(releases == 1 && clock_puts == 0);
	reset(); supply_on = false; unavailable(EBUSY);
	CHECK(strcmp(events, "ASs") == 0);
	reset(); power_on = false; unavailable(EBUSY);
	CHECK(strcmp(events, "ASVPs") == 0);

	for (u_int i = 0; i < __arraycount(failures); i++) {
		for (u_int stage = 0; stage < 4; stage++) {
			reset();
			int *errors[] = { &state_error, &voltage_error,
			    &power_error, &ready_error };
			*errors[stage] = failures[i];
			unavailable(failures[i]);
			CHECK(releases == 1 && clock_puts == (stage == 3 ? 1U : 0U));
			*errors[stage] = 0;
			/* Restored providers do not prove CORE ON/Q readiness. */
			unavailable(EBUSY);
			CHECK(releases == 2);
		}
	}
	for (u_int i = 0; i < __arraycount(bad_rates); i++) {
		reset(); core_hz = bad_rates[i]; unavailable(EOPNOTSUPP);
		CHECK(clock_puts == 1 && releases == 1);
		reset(); voltage = bad_volts[i]; unavailable(EOPNOTSUPP);
		CHECK(clock_puts == 1 && releases == 1);
	}
	reset(); prepare_len = 0; map_error = ENOMEM;
	CHECK(sun60i_gpu_identify(&sc) == ENOMEM);
	CHECK(maps == 1 && peeks == 0 && unmaps == 0 &&
	    clock_puts == 1 && releases == 1);
	reset(); prepare_len = 0; peek_error = -1;
	CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
	CHECK(!sc.sc_have_id && strstr(normal_output, "PBVNC") == NULL);
	CHECK(maps == 1 && peeks == 1 && unmaps == 1 &&
	    clock_puts == 1 && releases == 1);
	CHECK(strstr(error_output, "PBVNC read") != NULL);
	for (u_int i = 0; i < __arraycount(ids); i++) {
		reset(); prepare_len = 0; pbvnc = ids[i];
		CHECK(sun60i_gpu_identify(&sc) == ENODEV);
		CHECK(sc.sc_have_id && sc.sc_bvnc == pbvnc);
		CHECK(maps == 1 && peeks == 1 && unmaps == 1 &&
		    clock_puts == 1 && releases == 1);
		reset(); prepare_len = 0; pbvnc = ids[i];
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
		CHECK(strstr(normal_output, "(unexpected)") != NULL);
		CHECK(strstr(normal_output, "(expected A733 GPU)") == NULL);
		CHECK(strstr(error_output, "PBVNC value") != NULL);
	}
	/* Diagnostic binding must block MMIO even with completely ready inputs. */
	for (u_int ready = 0; ready < 2; ready++) {
		reset(); observe_len = 0;
		clock_observation.dcxo_hz[0] = clock_observation.dcxo_hz[1] = 26000000;
		clock_observation.dcxo_sample[0][0] = clock_observation.dcxo_sample[0][1] = 0x8003;
		clock_observation.dcxo_sample[1][0] = clock_observation.dcxo_sample[1][1] = 0x8003;
		if (!ready) {
			clock_observation.reason = A733_GPU_MODULE_GATED;
			clock_observation.readiness_error = EBUSY;
		}
		sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
		config_finalize();
		CHECK(maps == 0 && peeks == 0 && unmaps == 0 && !sc.sc_have_id);
		CHECK(strcmp(events, "ASVPCQcs") == 0);
		CHECK(strstr(normal_output, "observation only; GPU registers not mapped or read") != NULL);
		CHECK(strstr(normal_output, "DCXO before CCU: 0x00008003 / 0x00008003, 26000000 Hz") != NULL);
		CHECK(strstr(normal_output, "DCXO after CCU: 0x00008003 / 0x00008003, 26000000 Hz") != NULL);
		CHECK(strstr(normal_output, "PBVNC") == NULL);
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0 && maps == 0);
	}
	for (int len = 1; len < 9; len++) {
		reset(); observe_len = len; unavailable(EINVAL); CHECK(nevents == 0);
	}
	test_clock_observation();

	test_experimental_prepare();
	test_domain_request();
	test_fwheap_optional_outputs();
	test_fwbridge_diagnostic_gate();
	test_native_startup();
	test_runtime_stages();
	test_fwinfo_stage();
	test_fwmem_stage();
	test_fwinit_stage();
	test_fwboot_stage();
	test_fwprobe_stage();
	test_fwcache_stage();
	test_probe_report_capacity();
	test_trace_report();
	test_hwr_report();
	test_scratch_error_snapshot();
	CHECK(fwheap_calls == 0 && fwvm_calls == 0);
	printf("PASS: %u A733 GPU identification checks\n", checks);
	return 0;
}
