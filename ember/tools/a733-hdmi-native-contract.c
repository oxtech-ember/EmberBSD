/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted actual-source native output adapter checks. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>

typedef unsigned u_int;
typedef uintptr_t bus_addr_t;
typedef size_t bus_size_t;
typedef unsigned bus_space_handle_t;
typedef void *bus_space_tag_t;
typedef void *device_t;
typedef void *cfdata_t;
struct clk { int value; };
struct fdtbus_regulator { unsigned rail; };
struct syscon { unsigned locked; };
struct device_compatible_entry { const char *compat; };
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define CTASSERT(c) _Static_assert(c, #c)
#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define BUS_SPACE_BARRIER_WRITE 2
#define I2C_OP_READ_WITH_STOP 1
#define DDC_ADDR 0x50
#ifndef be32toh
#define be32toh(x) __builtin_bswap32(x)
#endif
#define CFATTACH_DECL_NEW(n,s,m,a,d,r)
#define DRM_MODE_FLAG_PHSYNC 1
#define DRM_MODE_FLAG_NHSYNC 2
#define DRM_MODE_FLAG_PVSYNC 4
#define DRM_MODE_FLAG_NVSYNC 8

static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
 fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)
#include "sun60i_a733_output.h"
#include "sun60i_a733_hdmi_phy.h"
#include "sun60i_a733_tv0.h"
#include "sun60i_a733_ccu.h"
#include "sunxi_rtcvar.h"
#include "videomode.h"
#include "edidvar.h"
#include "sun60i_a733_output.c"
#include "sun60i_a733_hdmi_plan.c"

struct drm_display_mode {
 int clock, crtc_clock, hdisplay, crtc_hdisplay, hsync_start, crtc_hsync_start;
 int hsync_end, crtc_hsync_end, htotal, crtc_htotal, vdisplay, crtc_vdisplay;
 int vsync_start, crtc_vsync_start, vsync_end, crtc_vsync_end, vtotal, crtc_vtotal;
 unsigned flags;
};
struct dwhdmi_softc {
 device_t sc_dev;
 bus_space_tag_t sc_bst;
 bus_space_handle_t sc_bsh;
 u_int sc_reg_width, sc_scl_hcnt, sc_scl_lcnt;
 int sc_io_error;
 void *sc_ic;
 int (*sc_read)(struct dwhdmi_softc *, bus_size_t, uint8_t *);
 int (*sc_write)(struct dwhdmi_softc *, bus_size_t, uint8_t);
};
static const int owner_cookie;
static struct clk clock_de0;
static struct syscon syscfg;
static struct fdtbus_regulator rails[2] = {{0}, {1}};
static struct dwhdmi_softc *ddc_dw;
static unsigned calls, fail, reads, writes, maps, unmaps, clock_puts, faults, releases;
static unsigned map_failure, wrong_node, bad_range, missing_property, reject_syscfg;
static unsigned syscfg_writes, rail_voltage[2], rail_enabled[2], bad_name;
static bool retained, ccu_hdmi, ccu_tv0, dw_started, tv_started, registered;
static bool dcxo_drift, hpd_loss;
static uint32_t sfr_hz, sysreg[2], core8[0x8000], top[17], tvregs[0x341/4+1], vo1[9];
static uint32_t power_cells[2];
static unsigned event[512], events;
static int step(unsigned id) { CHECK(events < 512);event[events++]=id;return ++calls==fail?EIO:0; }
static void *device_private(device_t dev) { return dev; }
static void aprint_naive(const char *s) { (void)s; }
static void aprint_normal(const char *s) { (void)s; }
static void aprint_error_dev(device_t d,const char *s,...) { (void)d;(void)s; }
static void delay(unsigned n) { CHECK(n>0); }
static void clk_put(struct clk *c) { CHECK(c==&clock_de0);clock_puts++; }
static int of_compatible_match(int node,const struct device_compatible_entry *t)
{
 const char *value=node==1?"allwinner,sun60i-a733-hdmi":node==2?"allwinner,sun60i-a733-pck-600":node==3?"allwinner,sun60i-a733-rtc":node==4?"syscon":node==8?"x-powers,axp8191":"invalid";
 return wrong_node==(unsigned)node?0:strcmp(value,t[0].compat)==0;
}
static bool of_hasprop(int node,const char *name)
{ CHECK(node==1);return missing_property != (strcmp(name,"netbsd,consumer-managed-power")==0?1u:2u); }
static int fdtbus_get_reg(int node,unsigned index,bus_addr_t *address,bus_size_t *size)
{ CHECK(index==0);*address=node==2?0x07060000:node==3?0x07090000:0x03000000;*size=node==2?0xb000:node==3?0x400:0x1000;if(bad_range==(unsigned)node)(*address)++;return 0; }
static int fdtbus_get_reg_byname(int node,const char *name,bus_addr_t *address,bus_size_t *size)
{ CHECK(node==1);*address=strcmp(name,"hdmi")==0?0x05520000:strcmp(name,"vo1")==0?0x05510000:0x05730000;*size=strcmp(name,"hdmi")==0?0x100000:0x1000;if(bad_range==1)(*size)--;return 0; }
static int fdtbus_get_phandle(int node,const char *name)
{ CHECK(node==1);return strcmp(name,"netbsd,dcxo-source")==0?3:strcmp(name,"syscon")==0?4:strcmp(name,"vdd08-supply")==0?5:6; }
static int fdtbus_get_phandle_from_native(uint32_t v) { return (int)v; }
static const void *fdtbus_get_prop(int node,const char *name,int *len)
{ CHECK(node==1&&strcmp(name,"power-domains")==0);*len=8;return power_cells; }
static const char *fdtbus_get_string(int node,const char *name)
{ CHECK(strcmp(name,"regulator-name")==0);return bad_name==(unsigned)node?"wrong":node==5?"dcdc2":"cldo2"; }
static int OF_parent(int node) { return node==5||node==6?7:node==7?8:0; }
static struct clk *fdtbus_clock_get(int node,const char *name)
{ CHECK(node==1&&strcmp(name,"de0")==0);return &clock_de0; }
static struct syscon *fdtbus_syscon_acquire(int node,const char *name)
{ CHECK(node==1&&strcmp(name,"syscon")==0);return &syscfg; }
static struct fdtbus_regulator *fdtbus_regulator_acquire(int node,const char *name)
{ CHECK(node==1&&retained);if(step(3))return NULL;return &rails[strcmp(name,"vdd08-supply")==0?0:1]; }
static int fdtbus_regulator_is_enabled(struct fdtbus_regulator *r,bool *out)
{ int e=step(4);if(!e)*out=rail_enabled[r->rail]!=0;return e; }
static int fdtbus_regulator_get_voltage(struct fdtbus_regulator *r,u_int *out)
{ int e=step(5);if(!e)*out=rail_voltage[r->rail];return e; }
int sunxi_rtc_dcxo_query(int node,struct sunxi_rtc_dcxo_state *out)
{ CHECK(node==3);int e=step(2);if(!e)*out=(struct sunxi_rtc_dcxo_state){{0x183fb0f7,0x183fb0f7},dcxo_drift?24000000:26000000};return e; }
int sun60i_a733_ccu_display_ready(struct clk *c,const void *owner)
{ CHECK(c==&clock_de0&&owner==&owner_cookie&&retained);return step(1); }
int sun60i_a733_ccu_display_fault(struct clk *c,const void *owner,int error)
{ CHECK(c==&clock_de0&&owner==&owner_cookie&&retained&&error);faults++;return 0; }
int sun60i_a733_ccu_display_begin_hdmi(struct clk *c,const void *owner)
{ CHECK(c==&clock_de0&&owner==&owner_cookie&&retained&&!ccu_hdmi);int e=step(6);if(!e)ccu_hdmi=true;return e; }
int sun60i_a733_ccu_display_begin_tv0(struct clk *c,const void *owner)
{ CHECK(c==&clock_de0&&owner==&owner_cookie&&retained&&ccu_hdmi&&!ccu_tv0);int e=step(7);if(!e)ccu_tv0=true;return e; }
int sun60i_a733_ccu_display_hdmi_clock_state(struct clk *c,const void *owner,struct sun60i_a733_display_hdmi_clock *out)
{ CHECK(c==&clock_de0&&owner==&owner_cookie&&retained&&ccu_hdmi);int e=step(8);if(!e)*out=(struct sun60i_a733_display_hdmi_clock){0,1,sfr_hz};return e; }
static int bus_space_map(bus_space_tag_t t,bus_addr_t a,bus_size_t size,int flags,bus_space_handle_t *out)
{ (void)t;(void)flags;CHECK(size==(a==0x05520000?0x100000u:0x1000u));maps++;if(maps==map_failure)return ENOMEM;*out=a==0x05520000?1:a==0x05510000?2:3;return 0; }
static void bus_space_unmap(bus_space_tag_t t,bus_space_handle_t h,bus_size_t size)
{ (void)t;CHECK(h>=1&&h<=3&&size==(h==1?0x100000u:0x1000u));unmaps++; }
static int bus_space_peek_1(bus_space_tag_t t,bus_space_handle_t h,bus_size_t off,uint8_t *out)
{ (void)t;CHECK(retained&&ccu_hdmi&&h==1&&off<0x8000);reads++;int e=step(9);if(!e)*out=off==0x3004?(hpd_loss?0:3):core8[off];return e; }
static int bus_space_poke_1(bus_space_tag_t t,bus_space_handle_t h,bus_size_t off,uint8_t value)
{ (void)t;CHECK(retained&&ccu_hdmi&&h==1&&off<0x8000);writes++;int e=step(10);core8[off]=value;return e; }
static int bus_space_peek_4(bus_space_tag_t t,bus_space_handle_t h,bus_size_t off,uint32_t *out)
{ (void)t;CHECK(retained&&ccu_hdmi&&off%4==0);reads++;int e=step(11);if(!e){if(h==1){CHECK(off>=0xe0000&&off<=0xe0040);*out=top[(off-0xe0000)/4];}else if(h==2){CHECK(off==0||off==0x20);*out=vo1[off/4];}else{CHECK(h==3&&ccu_tv0&&off<=0x340);*out=tvregs[off/4];}}return e; }
static int bus_space_poke_4(bus_space_tag_t t,bus_space_handle_t h,bus_size_t off,uint32_t value)
{ (void)t;CHECK(retained&&ccu_hdmi&&off%4==0);writes++;int e=step(12);if(h==1){CHECK(off>=0xe0000&&off<=0xe0040);top[(off-0xe0000)/4]=value;}else if(h==2){CHECK(off==0||off==0x20);vo1[off/4]=value;}else{CHECK(h==3&&ccu_tv0&&off<=0x340);tvregs[off/4]=value;}return e; }
static void bus_space_barrier(bus_space_tag_t t,bus_space_handle_t h,bus_size_t off,bus_size_t size,int flags)
{ (void)t;(void)off;CHECK(h>=1&&h<=3&&(size==1||size==4)&&flags==BUS_SPACE_BARRIER_WRITE); }
static void syscon_lock(struct syscon *s) { CHECK(s==&syscfg&&!s->locked);s->locked=1; }
static void syscon_unlock(struct syscon *s) { CHECK(s==&syscfg&&s->locked);s->locked=0; }
static uint32_t syscon_read_4(struct syscon *s,bus_size_t off)
{ CHECK(s->locked&&retained&&(off==0x160||off==0x164));reads++;return sysreg[(off-0x160)/4]; }
static void syscon_write_4(struct syscon *s,bus_size_t off,uint32_t value)
{ CHECK(s->locked&&retained&&(off==0x160||off==0x164));writes++;syscfg_writes++;if(syscfg_writes!=reject_syscfg)sysreg[(off-0x160)/4]=value; }
int sun60i_a733_hdmi_hpd_prepare(struct sun60i_a733_hdmi_phy *p)
{ int e=p->io->ready(p->io->arg,p->owner);if(!e)e=step(13);if(!e)e=p->io->write(p->io->arg,A733_HDMI_CORE8,0x3000,0x16);if(!e)p->hpd_prepared=true;return e; }
int sun60i_a733_hdmi_phy_prepare(struct sun60i_a733_hdmi_phy *p,uint32_t dcxo,uint32_t pixel)
{ CHECK(p->hpd_prepared&&dcxo==26000000&&pixel==108000000);int e=p->io->ready(p->io->arg,p->owner);if(!e)e=step(14);if(!e)e=p->io->write(p->io->arg,A733_HDMI_TOP32,0x20,0xe80c3500);if(!e)p->phy_prepared=true;return e; }
int sun60i_a733_hdmi_phy_observe(struct sun60i_a733_hdmi_phy *p)
{ CHECK(p->phy_prepared);int e=p->io->ready(p->io->arg,p->owner);return e?e:step(15); }
int sun60i_a733_tv0_route(struct sun60i_a733_tv0 *p)
{ int e=p->io->ready(p->io->arg,p->owner);if(!e)e=step(16);if(!e)e=p->io->write(p->io->arg,A733_VO1_REGS,0x20,0x10100000);if(!e)p->routed=true;return e; }
int sun60i_a733_tv0_prepare(struct sun60i_a733_tv0 *p,const struct sun60i_a733_output_mode *mode,enum sun60i_a733_tv0_profile profile)
{ CHECK(p->routed&&ccu_tv0&&mode->pixel_clock_hz==108000000&&profile==A733_TV0_EXPERIMENT_VENDOR_ZERO);int e=p->io->ready(p->io->arg,p->owner);if(!e)e=step(17);if(!e)e=p->io->write(p->io->arg,A733_TV0_REGS,0x98,mode->vtotal*2);if(!e)p->prepared=true;return e; }
int sun60i_a733_tv0_start(struct sun60i_a733_tv0 *p)
{ CHECK(p->prepared&&dw_started);int e=p->io->ready(p->io->arg,p->owner);if(!e)e=step(18);if(!e)e=p->io->write(p->io->arg,A733_TV0_REGS,0x90,0x80000000);if(!e)tv_started=p->started=true;return e; }
int sun60i_a733_tv0_observe(struct sun60i_a733_tv0 *p,uint64_t *out)
{ CHECK(p->started&&tv_started);int e=p->io->ready(p->io->arg,p->owner);if(!e)e=step(19);if(!e)*out=++p->sequence;return e; }
static int dwhdmi_native_init(struct dwhdmi_softc *dw)
{ CHECK(ccu_hdmi);int e=step(20);if(!e){dw->sc_ic=dw;ddc_dw=dw;}return e; }
static int dwhdmi_native_prepare(struct dwhdmi_softc *dw,const struct drm_display_mode *m)
{ (void)dw;CHECK(m->clock==108000&&m->clock==m->crtc_clock&&m->hdisplay==1280&&m->htotal==1688&&m->vdisplay==1024&&m->vtotal==1066&&m->flags==5);return step(21); }
static int dwhdmi_native_start(struct dwhdmi_softc *dw)
{ (void)dw;int e=step(22);if(!e)dw_started=true;return e; }
static int iic_acquire_bus(void *tag,int flags) { (void)flags;CHECK(tag==ddc_dw);return step(23); }
static void iic_release_bus(void *tag,int flags) { (void)flags;CHECK(tag==ddc_dw);releases++; }
static int iic_exec(void *tag,int op,int address,const void *cmd,size_t cmdlen,void *out,size_t len,int flags)
{ (void)flags;CHECK(tag==ddc_dw&&op==I2C_OP_READ_WITH_STOP&&address==0x50&&cmdlen==1&&*(const uint8_t *)cmd==0&&len==128);CHECK(ddc_dw->sc_scl_hcnt==221&&ddc_dw->sc_scl_lcnt==245);int e=step(24);if(!e)memset(out,0,len);return e; }
static const struct videomode fixture_mode = {108000,1280,1328,1440,1688,1024,1025,1028,1066,5,"test"};
int edid_parse_quiet(uint8_t *data,struct edid_info *out)
{ (void)data;CHECK(out->edid_nmodes==0);if(step(25))return -1;out->edid_modes[0]=fixture_mode;out->edid_modes[0].dot_clock=74250;out->edid_modes[1]=fixture_mode;out->edid_nmodes=2;out->edid_preferred_mode=&out->edid_modes[0];return 0; }
int sun60i_a733_output_register(int node,const struct sun60i_a733_output_ops *ops,void *arg)
{ CHECK(node==1&&ops&&arg);int e=step(26);if(!e)registered=true;return e; }
#include "sun60i_a733_hdmi_fdt.c"

static struct sun60i_a733_hdmi_softc sc;
static void fixture(void)
{
 memset(&sc,0,sizeof(sc));memset(core8,0,sizeof(core8));memset(top,0,sizeof(top));memset(tvregs,0,sizeof(tvregs));memset(vo1,0,sizeof(vo1));
 calls=fail=reads=writes=maps=unmaps=clock_puts=faults=releases=0;
 map_failure=wrong_node=bad_range=missing_property=reject_syscfg=syscfg_writes=bad_name=events=0;
 retained=true;ccu_hdmi=ccu_tv0=dw_started=tv_started=registered=dcxo_drift=hpd_loss=false;
 rail_voltage[0]=800000;rail_voltage[1]=1800000;rail_enabled[0]=rail_enabled[1]=1;
 syscfg.locked=0;sysreg[0]=0xa5a505a5;sysreg[1]=0xa5a5a5a5;sfr_hz=24000000;
 power_cells[0]=__builtin_bswap32(2);power_cells[1]=__builtin_bswap32(10);
}
static void attach(void)
{ struct fdt_attach_args fa={1,NULL};CHECK(sun60i_a733_hdmi_match(NULL,NULL,&fa)!=0);sun60i_a733_hdmi_attach(NULL,&sc,&fa); }
static int pipeline(struct sun60i_a733_output_mode *mode,uint64_t *sequence)
{ int e=native_get_mode(&sc,&owner_cookie,mode);if(!e)e=native_prepare(&sc,&owner_cookie,mode);if(!e)e=native_start(&sc,&owner_cookie);if(!e)e=native_observe(&sc,&owner_cookie,sequence);return e; }
static void adapter_access_checks(void)
{
 struct sun60i_a733_output_mode mode;
 uint64_t sequence;
 uint32_t value;
 uint8_t byte;
 unsigned before;
 int error;

 for (unsigned which=0;which<5;which++) {
  for (unsigned reject=0;reject<2;reject++) {
   fixture();attach();CHECK(pipeline(&mode,&sequence)==0);
   calls=events=0;fail=reject;value=0x12345678;byte=0x42;before=reads;
   sc.trace.version = sc.trace.valid = 1;
   sc.trace.read_value = 0x11223344;
   sc.trace.read_valid = 1;
   core8[2]=0xa5;top[16]=0x11223344;vo1[0]=0x55667788;tvregs[0x340/4]=0x99aabbcc;
   switch(which) {
   case 0: error=native_phy_read(&sc,A733_HDMI_CORE8,2,&value);break;
   case 1: error=native_phy_read(&sc,A733_HDMI_TOP32,0x40,&value);break;
   case 2: error=native_tv_read(&sc,A733_VO1_REGS,0,&value);break;
   case 3: error=native_tv_read(&sc,A733_TV0_REGS,0x340,&value);break;
   default: error=native_dw_read(&sc.dw,2,&byte);break;
   }
   CHECK(reads==before+1&&calls==1);
   CHECK(sc.trace.read_valid == (reject ? 0U : 1U));
   CHECK(sc.trace.read_space == (which == 1 ? A733_OUTPUT_IO_TOP32 :
       which == 2 ? A733_OUTPUT_IO_VO1 : which == 3 ? A733_OUTPUT_IO_TV0 :
       A733_OUTPUT_IO_CORE8));
   if (reject)
    CHECK(sc.trace.read_value == 0);
   if(reject) CHECK(error==EFAULT&&value==0x12345678&&byte==0x42&&sc.fault==EFAULT);
   else {
    const uint32_t expected[]={0xa5,0x11223344,0x55667788,0x99aabbcc};
    CHECK(error==0&&sc.fault==0);
    if(which<4)CHECK(value==expected[which]);else CHECK(byte==0xa5);
   }
  }
 }
 fixture();attach();CHECK(pipeline(&mode,&sequence)==0);
 CHECK(native_dw_write(&sc.dw,2,0x7a)==0&&core8[2]==0x7a);
 calls=events=0;fail=1;CHECK(native_dw_write(&sc.dw,3,0x7b)==EFAULT&&core8[3]==0x7b);
 before=writes;CHECK(native_dw_write(&sc.dw,4,0)==EFAULT&&writes==before);
 fixture();attach();fail=1;CHECK(native_phy_read(&sc,A733_HDMI_CORE8,0,&value)==EACCES&&reads==0);
 fixture();fail=1;attach();CHECK(!registered&&maps==3&&unmaps==3&&clock_puts==1&&reads==0&&writes==0);
}

static void
prepare_trace_checks(void)
{
	static const struct { unsigned count; uint32_t phase; } groups[] = {
		{ 6, A733_OUTPUT_PREPARE_READY },
		{ 8, A733_OUTPUT_PREPARE_PHY },
		{ 1, A733_OUTPUT_PREPARE_TV0_CLOCK },
		{ 8, A733_OUTPUT_PREPARE_TV0_TIMING },
		{ 1, A733_OUTPUT_PREPARE_DW },
		{ 6, A733_OUTPUT_PREPARE_FINAL_READY }
	};
	struct sun60i_a733_output_mode mode;
	struct sun60i_a733_output_trace trace, saved, zero = { 0 };
	struct sun60i_a733_hdmi_phy_trace phy, phy_zero = { 0 };
	uint32_t value;
	unsigned n = 0, old_calls, old_reads, old_writes;

	fixture();
	attach();
	memset(&trace, 0xa5, sizeof(trace));
	native_cached_trace(&sc, &trace);
	CHECK(memcmp(&trace, &zero, sizeof(trace)) == 0);
	memset(&phy, 0xa5, sizeof(phy));
	native_cached_phy_trace(&sc, &phy);
	CHECK(memcmp(&phy, &phy_zero, sizeof(phy)) == 0);
	sc.phy.trace = (struct sun60i_a733_hdmi_phy_trace){
	    1, 15, 4, 14, 0x8160, 0x160, 2
	};
	old_calls = calls;
	old_reads = reads;
	old_writes = writes;
	native_fault(&sc, &owner_cookie, EIO);
	native_cached_phy_trace(&sc, &phy);
	CHECK(memcmp(&phy, &sc.phy.trace, sizeof(phy)) == 0);
	CHECK(calls == old_calls && reads == old_reads && writes == old_writes);
	for (unsigned g = 0; g < __arraycount(groups); g++) {
		for (unsigned i = 0; i < groups[g].count; i++) {
			fixture();
			attach();
			CHECK(native_get_mode(&sc, &owner_cookie, &mode) == 0);
			calls = events = 0;
			fail = ++n;
			CHECK(native_prepare(&sc, &owner_cookie, &mode) != 0);
			CHECK(calls == n && sc.trace.version == 1);
			CHECK(sc.trace.valid == 3 && sc.trace.done == 0);
			CHECK(sc.trace.phase == groups[g].phase);
			CHECK(sc.trace.result == (uint32_t)sc.fault);
			saved = sc.trace;
			old_calls = calls;
			old_reads = reads;
			old_writes = writes;
			native_fault(&sc, &owner_cookie, EINVAL);
			for (unsigned j = 0; j < 3; j++) {
				native_cached_trace(&sc, &trace);
				CHECK(memcmp(&trace, &saved, sizeof(trace)) == 0);
			}
			CHECK(calls == old_calls && reads == old_reads);
			CHECK(writes == old_writes);
		}
	}
	fixture();
	attach();
	CHECK(native_get_mode(&sc, &owner_cookie, &mode) == 0);
	calls = events = 0;
	CHECK(native_prepare(&sc, &owner_cookie, &mode) == 0);
	CHECK(calls == n && sc.trace.phase == A733_OUTPUT_PREPARE_DONE);
	CHECK(sc.trace.valid == 3 && sc.trace.done == 1 && sc.trace.result == 0);
	saved = sc.trace;
	old_calls = calls;
	old_reads = reads;
	old_writes = writes;
	for (unsigned i = 0; i < 3; i++) {
		native_cached_trace(&sc, &trace);
		CHECK(memcmp(&trace, &saved, sizeof(trace)) == 0);
	}
	CHECK(calls == old_calls && reads == old_reads && writes == old_writes);
	native_fault(&sc, &owner_cookie, EIO);
	CHECK(sc.fault == EIO);
	native_cached_trace(&sc, &trace);
	CHECK(memcmp(&trace, &saved, sizeof(trace)) == 0);
	/* Faulting reads keep the caller's value and invalidate cached data. */
	fixture();
	attach();
	CHECK(native_get_mode(&sc, &owner_cookie, &mode) == 0);
	sc.trace.version = sc.trace.valid = 1;
	sc.trace.read_value = 0x12345678;
	sc.trace.read_valid = 1;
	calls = events = 0;
	fail = 1;
	value = 0xabcdef01;
	CHECK(native_phy_read(&sc, A733_HDMI_TOP32, 0x20, &value) == EFAULT);
	CHECK(value == 0xabcdef01 && sc.trace.read_valid == 0);
	CHECK(sc.trace.read_value == 0 && sc.trace.read_reg == 0x20);
	CHECK(sc.trace.read_space == A733_OUTPUT_IO_TOP32);
}

int main(void)
{
 struct sun60i_a733_output_mode mode, saved;
 struct videomode bad;
 uint64_t sequence;
 unsigned count, h, l, old_reads, old_writes, old_calls;
 int error;
 fixture();attach();CHECK(registered&&maps==3&&unmaps==0&&reads==0&&writes==0);calls=events=0;
 CHECK(pipeline(&mode,&sequence)==0&&sequence==1&&mode.pixel_clock_hz==108000000);
 CHECK(sc.written&&sc.phy.phy_prepared&&sc.tv.prepared&&tv_started&&faults==0);
 CHECK(sysreg[0]==0xa5a515a5&&sysreg[1]==0x00a5a5a5);count=calls;
 for(unsigned n=1;n<=count;n++) {
  fixture();attach();calls=events=0;fail=n;memset(&mode,0x5a,sizeof(mode));saved=mode;sequence=0x1234;
  error=pipeline(&mode,&sequence);
  if(!(error!=0&&sc.fault==error&&faults==1&&calls==n&&sequence==0x1234))
   fprintf(stderr,"failure %u: error=%d fault=%d faults=%u calls=%u sequence=%llu\n",n,error,sc.fault,faults,calls,(unsigned long long)sequence);
  CHECK(error!=0&&sc.fault==error&&faults==1&&calls==n&&sequence==0x1234);
  old_reads=reads;old_writes=writes;old_calls=calls;
  CHECK(native_get_mode(&sc,&owner_cookie,&mode)==error);
  CHECK(native_prepare(&sc,&owner_cookie,&mode)==error);
  CHECK(native_start(&sc,&owner_cookie)==error);
  CHECK(native_observe(&sc,&owner_cookie,&sequence)==error);
  native_fault(&sc,&owner_cookie,EFAULT);CHECK(sc.fault==error&&faults==1);
  CHECK(reads==old_reads&&writes==old_writes&&calls==old_calls);
  if(!sc.mode_valid)CHECK(memcmp(&mode,&saved,sizeof(mode))==0);
 }
 for(unsigned n=1;n<=3;n++){fixture();map_failure=n;attach();CHECK(!registered&&maps==n&&unmaps==n-1&&clock_puts==1&&reads==0&&writes==0);}
 for(unsigned n=2;n<=8;n++){if(n==5||n==6||n==7)continue;fixture();wrong_node=n;attach();CHECK(!registered&&maps==0&&reads==0&&writes==0);}
 for(unsigned n=1;n<=4;n++){fixture();bad_range=n;attach();CHECK(!registered&&maps==0&&reads==0&&writes==0);}
 for(unsigned n=1;n<=2;n++){fixture();missing_property=n;attach();CHECK(!registered&&maps==0&&reads==0&&writes==0);}
 for(unsigned n=5;n<=6;n++){fixture();bad_name=n;attach();CHECK(!registered&&maps==0&&reads==0&&writes==0);}
 for(unsigned n=1;n<=2;n++){fixture();attach();reject_syscfg=n;CHECK(native_get_mode(&sc,&owner_cookie,&mode)==EIO&&syscfg_writes==n&&!sc.syscfg_ready);}
 for(unsigned n=0;n<2;n++){fixture();attach();rail_enabled[n]=0;CHECK(native_get_mode(&sc,&owner_cookie,&mode)==EOPNOTSUPP&&writes==0);}
 for(unsigned n=0;n<2;n++){fixture();attach();rail_voltage[n]++;CHECK(native_get_mode(&sc,&owner_cookie,&mode)==EOPNOTSUPP&&writes==0);}
 fixture();attach();hpd_loss=true;CHECK(native_get_mode(&sc,&owner_cookie,&mode)==ENXIO&&sc.dw.sc_ic==NULL);
 fixture();attach();CHECK(native_get_mode(&sc,&owner_cookie,&mode)==0);dcxo_drift=true;old_writes=writes;CHECK(native_prepare(&sc,&owner_cookie,&mode)==EBUSY&&writes==old_writes);
 fixture();attach();CHECK(native_get_mode(&sc,&owner_cookie,&mode)==0);sysreg[1]|=0x01000000;old_writes=writes;CHECK(native_prepare(&sc,&owner_cookie,&mode)==EBUSY&&writes==old_writes);
 CHECK(native_ddc_counts(24000000,&h,&l)==0&&h==221&&l==245);
 CHECK(native_ddc_counts(25000000,&h,&l)==0&&h==230&&l==256);
 CHECK(native_ddc_counts(26000000,&h,&l)==0&&h==239&&l==266);
 h=42;l=43;CHECK(native_ddc_counts(0,&h,&l)==EINVAL&&h==42&&l==43);
 CHECK(native_ddc_counts(24000000,NULL,&l)==EINVAL&&l==43);
 for(unsigned bit=4;bit<31;bit++){bad=fixture_mode;bad.flags|=1u<<bit;CHECK(native_mode(26000000,&bad,&mode)==EINVAL);}
 bad=fixture_mode;bad.dot_clock=74250;CHECK(native_mode(26000000,&bad,&mode)==EOPNOTSUPP);CHECK(native_mode(24000000,&bad,&mode)==0);
 bad=fixture_mode;bad.vtotal=bad.vdisplay+5;CHECK(native_mode(26000000,&bad,&mode)==EINVAL);
 bad=fixture_mode;bad.hsync_start=-1;CHECK(native_mode(26000000,&bad,&mode)!=0);
 CHECK(native_mode(26000000,NULL,&mode)==EINVAL);
 fixture();attach();CHECK(native_get_mode(&sc,&owner_cookie,&mode)==0);old_reads=reads;old_writes=writes;CHECK(native_phy_write(&sc,A733_HDMI_CORE8,0x8000,0)==ERANGE&&reads==old_reads&&writes==old_writes);
 adapter_access_checks();
 prepare_trace_checks();
 printf("A733 native HDMI adapter: %u checks passed (%u fallible calls)\n",checks,count);
 return 0;
}
