/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Execute the actual module and flip bodies with controlled native services. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define __KERNEL_RCSID(a,b)
#define MODULE(a,b,c)
#define MODULE_CMD_INIT 1
#define MODULE_CMD_FINI 2
#define DRM_MINOR_RENDER 2
#define DRIVER_MODESET 2
#ifndef EPROGMISMATCH
#define EPROGMISMATCH 1000
#endif
#define ASSERT_SLEEPABLE() ((void)0)
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096U
#endif
#define ROUNDED_BYTES ((8294400U + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define ACPI_FAILURE(e) ((e) != 0)
typedef int modcmd_t;
typedef void *ACPI_HANDLE;
typedef uintptr_t bus_space_handle_t;
static const char buildinfo[] = "scanout-source-test\n";
static int hz = 100;
struct device { int identity; };
typedef struct device *device_t;
struct drm_driver { const char *name; unsigned int driver_features; };
struct drm_device;
struct drm_minor { int type; struct drm_device *dev; };
struct drm_device { device_t dev; bool registered; struct drm_minor *render;
 const struct drm_driver *driver; void *dev_private; };
struct acpi_devnode { device_t ad_device; };
struct bcm2712_display_contract { uint32_t version; };
struct props { const char *mode, *expected; };
typedef const struct props *prop_dictionary_t;
#include "flip-types.h"
#include "scanout-types.h"
#include "vblank-types.h"
#include "bo-stats.h"
/* Native service layout is controlled; target TU compilation checks real ABI. */
struct bcmv3d_mmu4_native { int identity; };
#define BCMV3D_MMU4_NATIVE_COMPLETE 7
struct bcmv3d_mmu4_native_status {
 int stage, fault, status_error;
 bool sealed, backing_valid, sequence_valid, shared_valid, icac_valid;
 struct { bool prepared, sealed, mapping_uncertain; int first_error; } backing;
 struct { int first_error; } range;
 struct { bool sequence_complete, retained; int fault; } sequence;
 struct { bool flush_complete; unsigned int retained; int fault; } shared;
};
struct bcmv3d_vblank_native { int identity; };
struct drm_gem_object { size_t size; unsigned int refs, id; };
struct bcmv3d_bo { struct { struct drm_gem_object base; void *vaddr;
 size_t dmasize; } cma; };
struct bcmv3d_device { struct drm_device *drm; device_t dev; ACPI_HANDLE handle;
 struct bcmv3d_mmu4_native *mmu4_native; struct bcmv3d_vblank_native *vblank_native;
 struct bcmv3d_scanout *scanout; };
struct bcmv3d_scanout_frame { struct drm_gem_object *obj; unsigned int slot, refs; };
struct bcmv3d_scanout { struct bcmv3d_flip flip; struct bcmv3d_scanout_frame frame[3];
 uint64_t prepared, latches, retired; };
static struct device device;
static struct acpi_devnode node;
static struct drm_driver driver;
static struct drm_device drm;
static struct drm_minor minor;
static struct bcmv3d_device sc;
static struct bcmv3d_scanout native;
static struct bcmv3d_vblank_native irq_owner;
static struct bcmv3d_mmu4_native mmu_owner;
static struct bcmv3d_bo pool[3];
static struct bcmv3d_bo_stats stats;
static struct bcmv3d_mmu4_native_status mmu_status;
static struct bcmv3d_vblank_snapshot irq_status;
static struct props props;
static unsigned int allocations, releases, minor_refs, pauses, events, matches;
static unsigned int prepares, submits, get_calls, put_calls, status_calls, cycle_pauses;
static int fail_alloc, fail_prepare, fail_submit, fail_get, fail_put, fail_status;
static bool missing_device, missing_minor, wrong_node, wrong_resources, interrupt;
static bool missing_property, stall_latch, stall_retire, wrong_accounting;
static bool fail_mmu, fail_irq, stop_pause, stale_latches, stale_retired;
static bool final_fault, final_counter;
static char output[8192];
static size_t output_used;
static int cpu_intr_p(void) { return interrupt; }
static int cpu_softintr_p(void) { return 0; }
static bool prop_dictionary_get_cstring_nocopy(prop_dictionary_t p,
 const char *name, const char **out)
{
 if (missing_property) return false;
 if (strcmp(name,"scanout-mode") == 0) *out=p->mode;
 else if (strcmp(name,"expected-buildinfo") == 0) *out=p->expected;
 else return false;
 return *out != NULL;
}
static device_t device_find_by_xname(const char *name)
{ CHECK(strcmp(name,"bcmv3d0") == 0); return missing_device ? NULL : &device; }
static bool device_is_a(device_t d, const char *name)
{ return d==&device && strcmp(name,"bcmv3d")==0; }
static struct drm_minor *drm_minor_acquire(unsigned int index)
{ CHECK(index==128); if (missing_minor) return ERR_PTR(-ENODEV); minor_refs++; return &minor; }
static void drm_minor_release(struct drm_minor *m)
{ CHECK(m==&minor && minor_refs==1); minor_refs--; }
static int AcpiGetHandle(void *parent, const char *path, ACPI_HANDLE *out)
{ CHECK(parent==NULL && strcmp(path,"\\_SB.GPU0")==0); *out=&device; return 0; }
static const struct acpi_devnode *acpi_match_node(ACPI_HANDLE h)
{ CHECK(h==&device); return wrong_node ? NULL : &node; }
static int bcm2712_display_acpi_resources(device_t d, ACPI_HANDLE h,
 struct bcm2712_display_contract *out)
{ CHECK(d==&device && h==&device); out->version=1; return wrong_resources ? EINVAL : 0; }
static void drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
 CHECK(obj->refs!=0 && obj->id<3);
 if (--obj->refs==0) { stats.reserved_bytes-=obj->size; stats.releases++;
  releases++; free(pool[obj->id].cma.vaddr); pool[obj->id].cma.vaddr=NULL; }
}
static struct bcmv3d_bo *bcmv3d_bo_alloc(struct bcmv3d_device *d, uint32_t size)
{
 struct bcmv3d_bo *bo;
 CHECK(d==&sc && size==8294400 && allocations<3);
 allocations++;
 if ((int)allocations==fail_alloc) return ERR_PTR(-ENOMEM);
 size=ROUNDED_BYTES; bo=&pool[allocations-1]; bo->cma.base=(struct drm_gem_object){size,1,allocations-1};
 bo->cma.dmasize=size; bo->cma.vaddr=malloc(size); CHECK(bo->cma.vaddr!=NULL);
 memset(bo->cma.vaddr,0xa5,size); stats.reserved_bytes+=size; stats.reservations++;
 return bo;
}
static void bcmv3d_bo_snapshot(struct bcmv3d_device *d, struct bcmv3d_bo_stats *out)
{
 CHECK(d==&sc); *out=stats;
 if (wrong_accounting && submits==3) out->releases++;
 if (native.retired==2) {
  if (final_fault) native.flip.fault=EIO;
  if (final_counter) native.latches++;
 }
}
int bcmv3d_mmu4_native_cached_status(struct bcmv3d_mmu4_native *n,
 struct bcmv3d_mmu4_native_status *out)
{ CHECK(n==&mmu_owner); *out=mmu_status; return fail_mmu ? EIO : 0; }
int bcmv3d_vblank_native_snapshot(struct bcmv3d_vblank_native *n,
 struct bcmv3d_vblank_snapshot *out)
{ CHECK(n==&irq_owner); *out=irq_status; return fail_irq ? EIO : 0; }
static void aprint_normal(const char *fmt, ...)
{
 va_list args; int used;
 va_start(args,fmt); used=vsnprintf(output+output_used,sizeof(output)-output_used,fmt,args);
 va_end(args); CHECK(used>=0 && (size_t)used<sizeof(output)-output_used); output_used+=(size_t)used;
}
static int kpause(const char *, bool, int, void *);
#include "flip-body.h"
#include "module-body.h"

int bcmv3d_scanout_prepare(struct bcmv3d_scanout *n, struct drm_gem_object *obj,
 struct bcmv3d_scanout_frame **out)
{
 static const uint32_t colours[3]={0x00d02020,0x0020d020,0x002020d0};
 unsigned int i; uint32_t *pixel;
 CHECK(n==&native && obj->refs==1 && obj->id<3 && allocations==3);
 prepares++; if ((int)prepares==fail_prepare) return EIO;
 /* Check every supplied pixel independently, including X and row boundaries. */
 pixel=pool[obj->id].cma.vaddr;
 for (i=0;i<1920U*1080U;i++)
  CHECK(pixel[i]==((((i%1920)/120+(i/1920)/120)%2) ? colours[obj->id] : 0x00ffffff));
 for (i=8294400;i<ROUNDED_BYTES;i++) CHECK(((unsigned char *)pixel)[i]==0);
 for (i=0;i<3;i++) if (n->flip.slot[i].state==BCMV3D_FLIP_FREE) break;
 CHECK(i<3); CHECK(bcmv3d_flip_ready(&n->flip,i)==0);
 n->frame[i]=(struct bcmv3d_scanout_frame){obj,i,1}; obj->refs++;
 n->prepared++; *out=&n->frame[i]; return 0;
}
int bcmv3d_scanout_get(struct bcmv3d_scanout_frame *f)
{ get_calls++; if ((int)get_calls==fail_get) return EIO; CHECK(f->refs>0); f->refs++; return 0; }
int bcmv3d_scanout_put(struct bcmv3d_scanout_frame *f)
{ put_calls++; CHECK(f->refs>0); f->refs--; return (int)put_calls==fail_put ? EIO : 0; }
bool bcmv3d_scanout_matches(struct bcmv3d_scanout_frame *f, struct drm_gem_object *obj)
{ matches++; return f->obj==obj && f->refs!=0; }
int bcmv3d_scanout_submit(struct bcmv3d_scanout *n, struct bcmv3d_scanout_frame *f)
{
 CHECK(n==&native && f->refs==1 && f->obj->refs==2);
 submits++; if ((int)submits==fail_submit) return EIO;
 CHECK(bcmv3d_flip_submit_after(&n->flip,f->slot,n->flip.sequence)==0);
 cycle_pauses=0; return 0;
}
int bcmv3d_scanout_cached_status(struct bcmv3d_scanout *n,
 struct bcmv3d_scanout_status *out)
{
 unsigned int i;
 CHECK(n==&native); status_calls++; if ((int)status_calls==fail_status) return EIO;
 memset(out,0,sizeof(*out)); out->version=1; out->length=sizeof(*out);
 out->sequence=n->flip.sequence; out->frame_epoch=n->flip.frame_epoch;
 out->prepared=n->prepared; out->latches=stale_latches ? 0 : n->latches;
 out->retired=stale_retired ? 0 : n->retired;
 out->current_slot=n->flip.current_slot; out->pending_slot=n->flip.pending_slot;
 out->first_error=n->flip.fault;
 for (i=0;i<4;i++) out->state[i]=n->flip.slot[i].state;
 return 0;
}
static int kpause(const char *name, bool catch, int ticks, void *lock)
{
 struct bcmv3d_flip_event event;
 struct bcmv3d_flip_update update;
 unsigned int i;
 CHECK(strcmp(name,"v3dcycle")==0 && !catch && ticks>0 && lock==NULL);
 pauses++; cycle_pauses++;
 if (stop_pause) return EINTR;
 if (stall_latch || (stall_retire && submits>1 && cycle_pauses>1)) return EWOULDBLOCK;
 event=(struct bcmv3d_flip_event){native.flip.sequence+1,
  bcmv3d_flip_head((unsigned int)(native.flip.pending_slot>=0 ?
  native.flip.pending_slot : native.flip.current_slot)), (native.flip.frame_count+1)%64,0};
 CHECK(bcmv3d_flip_observe(&native.flip,&event,&update)==0); events++;
 if (update.latched) native.latches++;
 /* Controlled process worker runs only after the actual flip admits retirement. */
 for(i=0;i<3;i++) if(native.flip.slot[i].state==BCMV3D_FLIP_RETIRED) {
  CHECK(cycle_pauses>=2 && native.frame[i].refs==0);
  CHECK(bcmv3d_flip_claim_retired(&native.flip,i)==0);
  CHECK(bcmv3d_flip_reclaimed(&native.flip,i)==0); native.retired++;
  drm_gem_object_put_unlocked(native.frame[i].obj); native.frame[i].obj=NULL;
 }
 return EWOULDBLOCK;
}
static void reboot_fixture(void)
{
 struct bcmv3d_flip_event initial={1,32,0,0}; unsigned int i;
 for(i=0;i<3;i++) free(pool[i].cma.vaddr);
 memset(pool,0,sizeof(pool)); memset(&scanout_probe,0,sizeof(scanout_probe));
 memset(&native,0,sizeof(native)); CHECK(bcmv3d_flip_init(&native.flip,&initial)==0);
 memset(&stats,0,sizeof(stats)); memset(&mmu_status,0,sizeof(mmu_status));
 memset(&irq_status,0,sizeof(irq_status));
 mmu_status.stage=BCMV3D_MMU4_NATIVE_COMPLETE;
 mmu_status.sealed=mmu_status.backing_valid=mmu_status.backing.prepared=true;
 mmu_status.backing.sealed=mmu_status.sequence_valid=mmu_status.sequence.sequence_complete=true;
 mmu_status.sequence.retained=mmu_status.shared_valid=mmu_status.shared.flush_complete=true;
 mmu_status.shared.retained=2; mmu_status.icac_valid=true;
 irq_status.version=1; irq_status.length=sizeof(irq_status);
 irq_status.prepared=irq_status.arm_attempted=irq_status.routed=1; irq_status.active_list=32;
 driver=(struct drm_driver){"v3d",0}; drm=(struct drm_device){&device,true,&minor,&driver,&sc};
 minor=(struct drm_minor){DRM_MINOR_RENDER,&drm}; node.ad_device=&device;
 sc=(struct bcmv3d_device){&drm,&device,&device,&mmu_owner,&irq_owner,&native};
 props=(struct props){"cycle","scanout-source-test"};
 allocations=releases=minor_refs=pauses=events=matches=0;
 prepares=submits=get_calls=put_calls=status_calls=cycle_pauses=0;
 fail_alloc=fail_prepare=fail_submit=fail_get=fail_put=fail_status=0;
 missing_device=missing_minor=wrong_node=wrong_resources=interrupt=false;
 missing_property=stall_latch=stall_retire=wrong_accounting=false;
 fail_mmu=fail_irq=stop_pause=stale_latches=stale_retired=false;
 final_fault=final_counter=false; output_used=0; output[0]=0;
}
static void clean_refusal(int expected)
{
 CHECK(bcmv3d_scanout_probe_modcmd(MODULE_CMD_INIT,&props)==expected);
 CHECK(!scanout_probe.attempted && !scanout_probe.accepted && minor_refs==0);
 CHECK(prepares==0 && submits==0 && stats.reserved_bytes==0);
 CHECK(bcmv3d_scanout_probe_modcmd(MODULE_CMD_FINI,NULL)==0);
}
static void retained_failure(void)
{
 CHECK(bcmv3d_scanout_probe_modcmd(MODULE_CMD_INIT,&props)==0);
 CHECK(scanout_probe.attempted && !scanout_probe.accepted && minor_refs==1);
 CHECK(strstr(output,"NO SUCCESS; retained fault")!=NULL);
 CHECK(strstr(output,"cycle accepted")==NULL);
 CHECK(bcmv3d_scanout_probe_modcmd(MODULE_CMD_FINI,NULL)==EBUSY);
 CHECK(bcmv3d_scanout_probe_modcmd(MODULE_CMD_INIT,&props)==EBUSY);
}
int main(void)
{
 unsigned int i, cases=0;
 reboot_fixture();
 CHECK(bcmv3d_scanout_probe_modcmd(MODULE_CMD_INIT,&props)==0);
 CHECK(scanout_probe.accepted && minor_refs==1 && prepares==3 && submits==3);
 CHECK(get_calls==3 && put_calls==6 && matches==3 && events==5 && pauses==5);
 CHECK(native.latches==3 && native.retired==2 && stats.reserved_bytes==ROUNDED_BYTES);
 CHECK(stats.reservations==3 && stats.releases==2 && releases==2);
 CHECK(strstr(output,"cycle accepted")!=NULL && strstr(output,"NO SUCCESS")==NULL);
 CHECK(bcmv3d_scanout_probe_modcmd(MODULE_CMD_FINI,NULL)==EBUSY); cases++;
 reboot_fixture(); props.mode="status"; clean_refusal(EINVAL); cases++;
 reboot_fixture(); props.expected="wrong"; clean_refusal(EPROGMISMATCH); cases++;
 reboot_fixture(); props.expected="scanout-source-tesX"; clean_refusal(EPROGMISMATCH); cases++;
 reboot_fixture(); missing_property=true; clean_refusal(EINVAL); cases++;
 reboot_fixture(); interrupt=true; clean_refusal(EWOULDBLOCK); cases++;
 reboot_fixture(); missing_device=true; clean_refusal(ENODEV); cases++;
 reboot_fixture(); missing_minor=true; clean_refusal(ENODEV); cases++;
 reboot_fixture(); wrong_node=true; clean_refusal(ENODEV); cases++;
 reboot_fixture(); wrong_resources=true; clean_refusal(EINVAL); cases++;
 reboot_fixture(); drm.registered=false; clean_refusal(ENODEV); cases++;
 reboot_fixture(); driver.driver_features=DRIVER_MODESET; clean_refusal(ENODEV); cases++;
 reboot_fixture(); sc.handle=NULL; clean_refusal(ENODEV); cases++;
 reboot_fixture(); sc.scanout=NULL; clean_refusal(ENXIO); cases++;
 reboot_fixture(); mmu_status.sealed=false; clean_refusal(EIO); cases++;
 reboot_fixture(); mmu_status.backing.mapping_uncertain=true; clean_refusal(EIO); cases++;
 reboot_fixture(); mmu_status.sequence.retained=false; clean_refusal(EIO); cases++;
 reboot_fixture(); mmu_status.shared.fault=EIO; clean_refusal(EIO); cases++;
 reboot_fixture(); fail_mmu=true; clean_refusal(EIO); cases++;
 reboot_fixture(); irq_status.routed=0; clean_refusal(EIO); cases++;
 reboot_fixture(); irq_status.active_list=42; clean_refusal(EIO); cases++;
 reboot_fixture(); fail_irq=true; clean_refusal(EIO); cases++;
 reboot_fixture(); native.prepared=1; clean_refusal(EBUSY); cases++;
 reboot_fixture(); native.flip.fault=EIO; clean_refusal(EIO); cases++;
 for(i=1;i<=3;i++) { reboot_fixture(); fail_alloc=(int)i; clean_refusal(ENOMEM); cases++; }
 for(i=1;i<=3;i++) { reboot_fixture(); fail_prepare=(int)i; retained_failure();
  CHECK(prepares==i && submits==i-1); cases++; }
 for(i=1;i<=3;i++) { reboot_fixture(); fail_get=(int)i; retained_failure(); cases++; }
 for(i=1;i<=6;i++) { reboot_fixture(); fail_put=(int)i; retained_failure();
  CHECK(scanout_probe.refs_uncertain[(i-1)/2]); cases++; }
 for(i=1;i<=3;i++) { reboot_fixture(); fail_submit=(int)i; retained_failure(); cases++; }
 reboot_fixture(); fail_status=3; retained_failure(); CHECK(submits==1); cases++;
 reboot_fixture(); stall_latch=true; retained_failure(); CHECK(pauses==500 && native.retired==0); cases++;
 reboot_fixture(); stall_retire=true; retained_failure();
 CHECK(native.latches==2 && native.retired==0 && pauses==501 && releases==0); cases++;
 reboot_fixture(); stale_latches=true; retained_failure(); CHECK(pauses==500); cases++;
 reboot_fixture(); stale_retired=true; retained_failure(); CHECK(pauses==501); cases++;
 reboot_fixture(); stop_pause=true; retained_failure(); CHECK(pauses==1 && native.retired==0); cases++;
 reboot_fixture(); final_fault=true; retained_failure(); CHECK(native.retired==2); cases++;
 reboot_fixture(); final_counter=true; retained_failure(); CHECK(native.retired==2); cases++;
 reboot_fixture(); wrong_accounting=true; retained_failure(); CHECK(native.retired==2); cases++;
 reboot_fixture(); /* Test-owned reboot cleanup is not a native retirement API. */
 printf("PASS %u isolated module boots page%u; actual flip latch/extra-frame gates and retained faults\n",cases,PAGE_SIZE);
 return 0;
}
