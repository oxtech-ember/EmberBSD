/* SPDX-License-Identifier: BSD-2-Clause */
/* Controlled services. The owner, clock core/adapter and stop mechanism are real. */
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32 OSSwapHostToLittleInt32
#define le32toh OSSwapLittleToHostInt32
#elif defined(__linux__)
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#define BCMMBOX_CHANARM2VC 8
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef __CTASSERT
#define __CTASSERT(e) _Static_assert(e,#e)
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((__aligned__(n)))
#endif
#include "clockvar.h"
#include "properties.h"
#include "clock-core.h"
#include "clock-firmware.h"
#include "native-resources.h"
#include "bcm2712-quiesce-fixture.h"
static struct firmware display_firmware[4];
static unsigned display_clock_calls, display_resource_calls, display_map_calls;
static unsigned display_fail_clock, display_fail_resource, display_fail_map;
static unsigned display_gate;
static bool display_reenter, display_permitted = true, display_lock;
static int bcmv3d_bootfb_stop_display(struct bcmv3d_softc *);
static int bcmmbox_request(uint8_t, void *, size_t, uint32_t *);
typedef pthread_mutex_t kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
static void display_mutex_init(kmutex_t *m,int type,int ipl)
{ CHECK(type == 0 && ipl == 0 && giant == 0); CHECK(pthread_mutex_init(m,NULL) == 0); }
static void display_mutex_enter(kmutex_t *m)
{ CHECK(!display_lock && giant == 0); CHECK(pthread_mutex_trylock(m) == 0); display_lock = true; }
static void display_mutex_exit(kmutex_t *m)
{ CHECK(display_lock && giant == 0); display_lock = false; CHECK(pthread_mutex_unlock(m) == 0); }
struct display_once { bool done; int error; };
#define ONCE_DECL(n) struct display_once n
static int display_once(struct display_once *o,int (*fn)(void))
{ if (!o->done) { o->error = fn(); o->done = true; } return o->error; }
#define RUN_ONCE(o,f) display_once(o,f)
static void *display_zalloc(size_t n,int f)
{ CHECK(f == KM_SLEEP && display_lock && giant == 0); void *p = calloc(1,n); CHECK(p != NULL); return p; }
int bcm2712_display_acpi_resources(device_t d,ACPI_HANDLE h,struct bcm2712_display_contract *out)
{
	CHECK(display_permitted && giant == 0 && d == &gpu && h == &gpu && out != NULL);
	CHECK(gpu_sc.sc_bootfb.display.attempted && gpu_sc.sc_bootfb.firmware.complete);
	display_resource_calls++;
	if (display_resource_calls == display_fail_resource) return EINVAL;
	memset(out,0,sizeof(*out)); out->version = BCM2712_DISPLAY_VERSION;
	for (unsigned i = 0; i < BCM2712_DISPLAY_NMEM; i++) out->memory[i] = bcm2712_display_resources[i].memory;
	return 0;
}
#define mutex_init display_mutex_init
#define mutex_enter display_mutex_enter
#define mutex_exit display_mutex_exit
#define kmem_zalloc display_zalloc
#include "native-clock.h"
#undef mutex_init
#undef mutex_enter
#undef mutex_exit
#undef kmem_zalloc
static int display_mailbox(uint8_t channel,void *packet,size_t length,uint32_t *response)
{
	uint32_t *p = packet, id = le32toh(p[5]);
	unsigned i = id == 13 ? 0 : id == 14 ? 1 : id == 4 ? 2 : 3;
	CHECK(display_permitted && channel == 8 && (length == 32 || length == 36));
	CHECK(display_lock && giant == 0 && response != NULL && (id == 13 || id == 14 || id == 4 || id == 16));
	CHECK(gpu_sc.sc_bootfb.display.attempted && !gpu_sc.sc_bootfb.display.complete);
	CHECK(gpu_sc.sc_bootfb.firmware.complete && serial_fixture_held() && renderer_calls == 1);
	CHECK(display_map_calls == 0 && qb.operations == 0);
	display_clock_calls++;
	if (display_reenter) {
		CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == EBUSY);
		CHECK(!gpu_sc.sc_bootfb.display.complete && gpu_sc.sc_bootfb.display.fault == 0 && giant == 0);
	}
	/* Every create finishes its four GETs before any acquired SET. */
	if (le32toh(p[2]) == VCPROPTAG_SET_CLOCKSTATE || le32toh(p[2]) == VCPROPTAG_SET_CLOCKRATE) {
		for (unsigned j = 0; j < 4; j++) CHECK(display_firmware[j].calls >= 4);
		if (le32toh(p[2]) == VCPROPTAG_SET_CLOCKRATE) CHECK(le32toh(p[6]) >= display_firmware[i].rate);
	}
	if (display_clock_calls == display_fail_clock) return EIO;
	*response = 0x1000; return exchange(&display_firmware[i],p,length);
}
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
static int display_map(bus_space_tag_t t,uint64_t a,uint64_t n,int f,bus_space_handle_t *out)
{
	static const unsigned indices[] = {3,5,6};
	unsigned i = display_map_calls++;
	CHECK(display_permitted && giant == 0 && t == &tag && f == 0 && out != NULL);
	CHECK(i < 3 && a == bcm2712_display_resources[indices[i]].memory.address && n == bcm2712_display_resources[indices[i]].memory.length);
	CHECK(gpu_sc.sc_bootfb.display.attempted && renderer_calls == 1 && gpu_sc.sc_bootfb.render_attached);
	CHECK(gpu_sc.sc_bootfb.firmware.complete && serial_fixture_held());
	for (unsigned j = 0; j < 4; j++) CHECK(display_firmware[j].state == 1 && display_firmware[j].rate > 0);
	if (display_map_calls == display_fail_map) return ENOMEM;
	*out = (bus_space_handle_t)(uintptr_t)(i+1); return 0;
}
static unsigned display_region(bus_space_tag_t t,bus_space_handle_t h)
{ CHECK(giant == 0 && t == &tag && (uintptr_t)h >= 1 && (uintptr_t)h <= 3); return (unsigned)(uintptr_t)h-1; }
static int display_peek(bus_space_tag_t t,bus_space_handle_t h,uint32_t o,uint32_t *v)
{ return qb_read(display_region(t,h),o,v); }
static int display_poke(bus_space_tag_t t,bus_space_handle_t h,uint32_t o,uint32_t v)
{ return qb_write(display_region(t,h),o,v); }
static void display_barrier(bus_space_tag_t t,bus_space_handle_t h,unsigned o,uint64_t n,int f)
{ unsigned r = display_region(t,h); CHECK(o == 0 && n == (r == 0 ? 0x1a000U : 0x100U) && f == 3); qb_barrier(r); }
static void display_delay(unsigned us) { CHECK(giant == 0); qb_delay(us); }
static void display_initialize(void)
{
	/* Explicit simulated reboot. Production never releases these providers. */
	free(bcm2712_hsm_owner); free(bcm2712_bvb_owner); free(bcm2712_core_owner); free(bcm2712_disp_owner);
	bcm2712_hsm_owner = NULL; bcm2712_bvb_owner = NULL; bcm2712_core_owner = NULL; bcm2712_disp_owner = NULL;
	if (bcm2712_clock_once.done) CHECK(pthread_mutex_destroy(&bcm2712_clock_lock) == 0);
	memset(&bcm2712_clock_once,0,sizeof(bcm2712_clock_once));
	display_firmware[0] = firmware_new(BCM2712_CLOCK_HSM,1,240000000);
	display_firmware[1] = firmware_new(BCM2712_CLOCK_BVB,1,240000000);
	display_firmware[2] = firmware_new(BCM2712_CLOCK_CORE,1,240000000);
	display_firmware[3] = firmware_new(BCM2712_CLOCK_DISP,1,240000000); display_firmware[3].id = 16;
	display_clock_calls = display_resource_calls = display_map_calls = 0;
	display_fail_clock = display_fail_resource = display_fail_map = display_gate = 0;
	display_reenter = display_lock = false; display_permitted = true; qb_initialize(0x53,false);
}
