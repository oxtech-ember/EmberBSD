/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD A133 compute workflow contract, AI-assisted. */
#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/sha.h>
#include "../../sys/dev/powervr/pvr_fwcompute.h"
#include "../../sys/dev/powervr/pvr_fwcompute_file.h"
#define EAUTH 80
#define KM_SLEEP 0
#define PWAIT 0
#define BUS_DMASYNC_POSTREAD 2
#define PVR_FWPROBE_MMUCACHE 0x2abc0066
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } checks++; } while (0)
typedef FILE *firmware_handle_t;
struct pvr_fwmem { struct { struct pvr_fwinfo info; } image; };
struct pvr_fwboot_result { int error; };
struct pvr_query_input { uint64_t bvnc; };
struct drm_device { int valid; };
struct pvr_dma { void *cpu, *map; bool device_exposed; };
struct pvr_fwheap { struct pvr_dma heap; };
struct pvr_fwvm { bool device_exposed; struct pvr_dma resource[PVR_FWCOMPUTE_RESOURCES]; };
struct pvr_fwprobe_result { unsigned polls; struct { int read_offset, write_offset, rtn, fw_faults, hwr_state; } after; };
struct pvr_fwprobe_observation { bool valid; struct { int fw_faults, hwr_state; } state; };
struct a133_gpu_diagnostics { int stage; };
#define PVR_GE8300_DRM 1
#define A133_GPU_STAGE_COMPLETE 35
struct a133_gpu_softc {
 struct a133_gpu_diagnostics diag; void *dev,*dmat,*bst; uintptr_t bsh;
 struct pvr_fwmem *memory; struct pvr_fwboot_result boot_result;
 bool drm_pending; struct { bool attempted, registered; } bridge;
 struct drm_device *render_device; int lock;
 struct pvr_fwprobe_result queue_cache,compute_sync,compute_kick;
 struct pvr_fwheap *compute_heap; struct pvr_fwvm *compute_vm;
 uint64_t effective_hz; int irq_lock,irq_fault;
 int queue_stage,queue_error,queue_attempted,compute_stage,compute_error,compute_attempted,compute_polls;
 struct { bool retain_resources; } owner;
 int compute_sync_read,compute_sync_write,compute_sync_return;
 int compute_kick_read,compute_kick_write,compute_kick_return;
 int compute_ccb_read,compute_ccb_write,compute_ufo,compute_canary,compute_faults,compute_hwr;
};
static const char *input_path;
static struct a133_gpu_softc *active_sc;
static unsigned checks, calls, probes, sync_calls, kick_calls, vm_calls, postreads;
static int fault, ticks;
enum { BAD_FILE=1, BAD_MAP, BAD_SYNC, BAD_KICK, BAD_CANARY, BAD_FINAL, NO_COMPLETE, BAD_IRQ };
static void mutex_enter(int *x){ (void)x; }
static void mutex_exit(int *x){ (void)x; }
static int a133_gpu_live(struct a133_gpu_softc *s,uint64_t *r){ s->diag.stage=99; *r=s->effective_hz; return 0; }
static int firmware_open(const char *a,const char *b,firmware_handle_t *f){
 (void)a; (void)b; *f=fopen(input_path,"rb"); return *f ? 0 : ENOENT; }
static off_t firmware_get_size(firmware_handle_t f){ fseek(f,0,SEEK_END); off_t n=ftell(f); rewind(f); return n; }
static int firmware_read(firmware_handle_t f,off_t o,void *p,size_t n){
 fseek(f,o,SEEK_SET); if(fread(p,1,n,f)!=n) return EIO;
 if(fault==BAD_FILE) ((uint8_t *)p)[n-1]^=1; return 0; }
static int firmware_close(firmware_handle_t f){return fclose(f);}
static void *kmem_alloc(size_t n,int flags){(void)flags; return malloc(n);}
static void kmem_free(void *p,size_t n){(void)n; free(p);}
static int pvr_fwprobe_observe(struct pvr_fwmem *m,const struct pvr_fwboot_result *b,
 unsigned slot,uint32_t cmd,uint32_t log,struct pvr_fwprobe_observation *out){
 (void)m;(void)b;(void)slot;(void)cmd;(void)log; probes++; out->valid=true;
 out->state.hwr_state=1; return fault==BAD_FINAL && probes>=3 ? EIO : 0; }
static int pvr_fwheap_prepare(void *d,struct pvr_fwmem *m,const struct pvr_fwcompute_image *i,
 struct pvr_fwheap **out){(void)d;(void)m;CHECK(i->used_bytes==PVR_FWCOMPUTE_USED_BYTES);
 *out=calloc(1,sizeof(**out)); calls++;return *out ? 0 : ENOMEM;}
static int pvr_fwheap_map(struct pvr_fwheap *h,struct pvr_fwmem *m){
 (void)m;h->heap.device_exposed=true;return fault==BAD_MAP ? EIO : 0;}
static int pvr_fwprobe_ge_heap_sync(void *b,uintptr_t bh,struct pvr_fwmem *m,
 const struct pvr_fwboot_result *boot,const struct pvr_fwprobe_result *prev,
 struct pvr_fwheap *h,struct pvr_fwprobe_result *out){
 (void)b;(void)bh;(void)m;(void)boot;(void)prev;(void)out;CHECK(h->heap.device_exposed);
 out->polls=1;out->after.read_offset=3;out->after.write_offset=3;out->after.rtn=1;
 sync_calls++; if (fault==BAD_IRQ) active_sc->irq_fault=EIO;
 return fault==BAD_SYNC ? EIO : 0; }
static int pvr_fwvm_prepare(void *d,const struct pvr_fwcompute_file *file,const uint8_t *bytes,
 struct pvr_fwheap *h,struct pvr_fwvm **out){
 (void)d;(void)bytes; CHECK(h->heap.device_exposed);vm_calls++;*out=calloc(1,sizeof(**out));
 for(unsigned i=0;i<PVR_FWCOMPUTE_RESOURCES;i++) if(file->resource[i].role==PVR_FWCOMPUTE_SCRATCH){
 (*out)->resource[i].cpu=calloc(1,4096);(*out)->resource[i].map=(*out)->resource[i].cpu; }
 (*out)->device_exposed=true;return 0; }
static int pvr_fwprobe_ge_kick(void *b,uintptr_t bh,struct pvr_fwmem *m,
 const struct pvr_fwboot_result *boot,const struct pvr_fwprobe_result *prev,
 struct pvr_fwheap *h,struct pvr_fwvm *v,struct pvr_fwprobe_result *out){
 (void)b;(void)bh;(void)m;(void)boot;(void)prev;(void)out;CHECK(h->heap.device_exposed && v->device_exposed);
 kick_calls++;out->polls=1;out->after.read_offset=4;out->after.write_offset=4; if(fault==BAD_KICK)return EIO;
 for(unsigned i=0;i<PVR_FWCOMPUTE_RESOURCES;i++) if(v->resource[i].cpu)
 *(uint32_t *)v->resource[i].cpu=fault==BAD_CANARY ? 0 : 0x13579bdf;
 return 0; }
static void pvr_fwheap_observe(struct pvr_fwheap *h,uint32_t *r,uint32_t *w,uint32_t *u){
 (void)h;*w=PVR_FWCOMPUTE_USED_BYTES;*r=fault==NO_COMPLETE ? 0 : *w;*u=fault==NO_COMPLETE ? 0 : 1; }
static void bus_dmamap_sync(void *d,void *m,size_t o,size_t n,int op){
 (void)d;CHECK(m!=NULL && o==0 && n==4 && op==BUS_DMASYNC_POSTREAD);postreads++; }
static void getnanouptime(struct timespec *t){t->tv_sec=ticks;t->tv_nsec=0;}
#define timespecsub(a,b,c) do { (c)->tv_sec=(a)->tv_sec-(b)->tv_sec; (c)->tv_nsec=0; } while(0)
static int tsleep(void *p,int pri,const char *n,int t){(void)p;(void)pri;(void)n;(void)t;ticks++;return 0;}
static int pvr_fwvm_release(struct pvr_fwvm **v){CHECK(!(*v)->device_exposed);return 0;}
static int pvr_fwheap_release(struct pvr_fwheap **h){CHECK(!(*h)->heap.device_exposed);return 0;}
#define aprint_error_dev(...) ((void)0)
#define PVR_FW_PROFILE_GE8300 1
#define PVR_GE8300_BVNC UINT64_C(0x0016006600360026)
static int pvr_query_input_from_fwinfo(const struct pvr_fwinfo *info,
 struct pvr_query_input *input) { input->bvnc=info->bvnc;return 0; }
static int a133_gpu_compute_run(struct a133_gpu_softc *);
static struct drm_device fake_device;
static unsigned drm_register_calls,drm_publish_calls,drm_queue_calls;static bool check_drm_race;
static int drm_queue_error;
static int a133_gpu_queue_run(struct a133_gpu_softc *sc){
 if(sc->queue_attempted)return EBUSY;sc->queue_attempted=1;
 drm_queue_calls++;if(drm_queue_error)return drm_queue_error;sc->queue_stage=5;return 0;}
static int pvr_render_attach(void *dev,void *dmat,uint64_t bvnc,
 const struct pvr_query_input *input,struct drm_device **out){
 (void)dev;(void)dmat;CHECK(bvnc==PVR_GE8300_BVNC && input->bvnc==bvnc);
 if(check_drm_race)CHECK(a133_gpu_compute_run(active_sc)==EBUSY);
 drm_register_calls++;*out=&fake_device;return 0; }
static int pvr_fwbridge_prepare(void *b,int *l,void *bst,uintptr_t bsh,
 struct pvr_fwmem *m,struct pvr_fwboot_result *boot,struct pvr_fwprobe_result *prev,
 int profile,int (*health)(void *),void *cookie) {
 (void)b;(void)l;(void)bst;(void)bsh;(void)m;(void)boot;(void)prev;
 CHECK(profile==PVR_FW_PROFILE_GE8300 && health(cookie)==0);
 active_sc->bridge.attempted=true;return 0; }
static int pvr_fwbridge_publish(void *b,struct drm_device *dev){
 (void)b;CHECK(dev==&fake_device);drm_publish_calls++;active_sc->bridge.registered=true;return 0; }
static int a133_gpu_drm_attach(struct a133_gpu_softc *);
#include "consumer.inc"
static void cleanup(struct a133_gpu_softc *s){
 if(s->compute_vm){for(unsigned i=0;i<PVR_FWCOMPUTE_RESOURCES;i++)free(s->compute_vm->resource[i].cpu);free(s->compute_vm);}
 free(s->compute_heap);
}
int main(int argc,char **argv){
 struct pvr_fwmem mem={0};(void)a133_gpu_queue_run;CHECK(argc==2);input_path=argv[1];
 {
  struct a133_gpu_softc sc={0};sc.memory=&mem;sc.queue_stage=5;
  sc.drm_pending=true;
  CHECK(a133_gpu_compute_run(&sc)==EBUSY && !sc.compute_attempted);
  sc.drm_pending=false;sc.bridge.attempted=true;
  CHECK(a133_gpu_compute_run(&sc)==EBUSY && !sc.compute_attempted);
 }
 {
  struct a133_gpu_softc sc={0};sc.memory=&mem;
  sc.owner.retain_resources=true;sc.diag.stage=35;sc.effective_hz=504000000;
  mem.image.info.bvnc=PVR_GE8300_BVNC;active_sc=&sc;
  sc.compute_attempted=1;CHECK(a133_gpu_drm_attach(&sc)==EBUSY);
  sc.compute_attempted=0;sc.irq_fault=EIO;
  CHECK(a133_gpu_drm_attach(&sc)==EIO && drm_register_calls==0);
  sc.irq_fault=0;drm_queue_error=ETIMEDOUT;
  CHECK(a133_gpu_drm_attach(&sc)==ETIMEDOUT && drm_register_calls==0);
  drm_queue_error=0;CHECK(a133_gpu_drm_attach(&sc)==EBUSY);
  /* A new fixture instance represents a fresh boot, not an in-place reset. */
  memset(&sc,0,sizeof(sc));sc.memory=&mem;sc.owner.retain_resources=true;
  sc.diag.stage=35;sc.effective_hz=504000000;active_sc=&sc;check_drm_race=true;
  CHECK(a133_gpu_drm_attach(&sc)==0);
  CHECK(drm_queue_calls==2 && drm_register_calls==1 &&
      drm_publish_calls==1 && sc.bridge.registered);
  CHECK(a133_gpu_compute_run(&sc)==EBUSY);
  CHECK(a133_gpu_drm_attach(&sc)==EBUSY);
  check_drm_race=false;
 }
 for(fault=0;fault<=BAD_IRQ;fault++){
  struct a133_gpu_softc sc={0}; sc.memory=&mem;sc.queue_stage=5;sc.effective_hz=504000000;sc.diag.stage=35;active_sc=&sc;
  calls=probes=sync_calls=kick_calls=vm_calls=postreads=0;ticks=0;
  int error=a133_gpu_compute_run(&sc);
  CHECK(sc.compute_attempted==1 && sc.diag.stage==35);
  CHECK(a133_gpu_compute_run(&sc)==EBUSY);
  if(fault==0){ CHECK(error==0 && sc.compute_stage==9);CHECK(sc.compute_sync_read==3 && sc.compute_sync_write==3 && sc.compute_sync_return==1 && sc.compute_kick_read==4 && sc.compute_kick_write==4);CHECK(sc.compute_ccb_read==184 && sc.compute_ccb_write==184 && sc.compute_ufo==1 && sc.compute_canary==0x13579bdf && postreads==1); }
  if(fault==BAD_FILE)CHECK(error==EAUTH && calls==0 && kick_calls==0);
  if(fault==BAD_MAP)CHECK(error==EIO && sc.compute_heap && !vm_calls && !kick_calls);
  if(fault==BAD_SYNC)CHECK(error==EIO && sc.compute_heap && !vm_calls && !kick_calls);
  if(fault==BAD_KICK)CHECK(error==EIO && sc.compute_vm && !postreads);
  if(fault==BAD_CANARY)CHECK(error==EIO && sc.compute_stage==8 && postreads==1);
  if(fault==BAD_FINAL)CHECK(error==EIO && sc.compute_vm);
  if(fault==NO_COMPLETE)CHECK(error==ETIMEDOUT && !postreads);
  if(fault==BAD_IRQ)CHECK(error==EIO && sc.compute_heap && !vm_calls && !kick_calls);
  cleanup(&sc);
 }
 printf("PASS: %u A133 compute workflow contract checks\n",checks);return 0;
}
