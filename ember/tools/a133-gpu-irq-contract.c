/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; actual A133 hard IRQ and epoch checks. */
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
struct snapshot { uint32_t read_offset, write_offset, rtn, fw_faults, hwr_state; };
struct pvr_fwprobe_result { unsigned polls; struct snapshot after; };
struct pvr_fwprobe_observation { bool valid; struct snapshot state; };
struct a133_gpu_softc {
 void *bst, *irq_cookie, *irq_soft, *memory, *dev; uintptr_t bsh;
 int phandle, irq_lock, irq_fault; unsigned irq_total, irq_window; bool irq_armed;
 int queue_attempted, queue_stage, queue_error, queue_irq_total, queue_irq_window, queue_irq_fault;
 int queue_log_read, queue_log_write, queue_log_return, queue_cache_read, queue_cache_write, queue_cache_return;
 int queue_fw_faults, queue_hwr_flags;
 struct { bool retain_resources; } owner;
 struct { int error; } boot_result;
 struct { int stage; } diag;
 struct pvr_fwprobe_result queue_log, queue_cache;
 struct pvr_fwprobe_observation queue_fresh;
};
#define A133_GPU_STAGE_COMPLETE 35
#define PVR_FWPROBE_MMUCACHE 0x2abc0066
#define PWAIT 0
static int irq_emit, log_calls, mmu_calls, observe_calls, observe_fault;
static long clock_ns;
static void getnanouptime(struct timespec *t) { t->tv_sec=clock_ns/1000000000L;t->tv_nsec=clock_ns%1000000000L; }
static void time_sub(const struct timespec *a,const struct timespec *b,struct timespec *o) {
 o->tv_sec=a->tv_sec-b->tv_sec;o->tv_nsec=a->tv_nsec-b->tv_nsec;
 if(o->tv_nsec<0){o->tv_sec--;o->tv_nsec+=1000000000L;}
}
#define timespecsub(a,b,o) time_sub((a),(b),(o))
static int tsleep(void *p,int pr,const char *name,int t) { (void)p;(void)pr;(void)name;(void)t;clock_ns+=10000000;return 0; }
static void aprint_error_dev(void *d,const char *fmt,...) { (void)d;(void)fmt; }

static bool event;
static int read_error, clear_error, clear_count, wakes;
static void mutex_enter(int *m) { CHECK(*m==0);*m=1; }
static void mutex_exit(int *m) { CHECK(*m==1);*m=0; }
static int pvr_mips_irq_pending(void *t,uintptr_t h,bool *p) { (void)t;(void)h;if (read_error) return EIO;*p=event;return 0; }
static int pvr_mips_irq_clear(void *t,uintptr_t h) { (void)t;(void)h;clear_count++;if (clear_error) return EIO;event=false;return 0; }
static void softint_schedule(void *v) { CHECK(v!=NULL);wakes++; }
static int pvr_fwprobe(void *t,uintptr_t h,void *m,void *b,struct pvr_fwprobe_result *r) {
 (void)t;(void)h;(void)m;(void)b;log_calls++;r->polls=1;r->after=(struct snapshot){1,1,1,0,0};return 0;
}
static int pvr_fwprobe_mmucache_irq(void *t,uintptr_t h,void *m,void *b,void *prev,
    int (*arm)(void *),void *cookie,struct pvr_fwprobe_result *r);
static int pvr_fwprobe_observe(void *m,void *b,int slot,uint32_t cmd,int log,struct pvr_fwprobe_observation *o) {
 (void)m;(void)b;(void)log;CHECK(slot==1 && cmd==PVR_FWPROBE_MMUCACHE);observe_calls++;o->valid=true;o->state=(struct snapshot){2,2,1,observe_fault ? 1 : 0,0};return observe_fault ? EIO : 0;
}
#include "irq-driver.inc"
static int pvr_fwprobe_mmucache_irq(void *t,uintptr_t h,void *m,void *b,void *prev,
    int (*arm)(void *),void *cookie,struct pvr_fwprobe_result *r) {
 (void)t;(void)h;(void)m;(void)b;(void)prev;mmu_calls++;int error=arm(cookie);if(error) return error;
 if(irq_emit) { event=true;CHECK(a133_gpu_interrupt(cookie)==1); }
 r->polls=1;r->after=(struct snapshot){2,2,1,0,0};return 0;
}
int main(void) {
 struct a133_gpu_softc sc={.irq_cookie=&sc,.irq_soft=&sc};
 CHECK(a133_gpu_interrupt(&sc)==0 && sc.irq_total==0 && wakes==0);
 event=true;CHECK(a133_gpu_interrupt(&sc)==1 && sc.irq_total==1 && clear_count==1 && wakes==1);
 event=true;CHECK(a133_gpu_irq_arm(&sc)==0 && !event && sc.irq_window==0 && sc.irq_armed && clear_count==2);
 event=true;CHECK(a133_gpu_interrupt(&sc)==1 && sc.irq_total==2 && sc.irq_window==1 && wakes==2);
 read_error=1;CHECK(a133_gpu_interrupt(&sc)==1 && sc.irq_fault==EIO);
 CHECK(a133_gpu_irq_arm(&sc)==EIO && !sc.irq_lock);
 {
  struct a133_gpu_softc q={.irq_cookie=&q,.irq_soft=&q,.memory=&q,
      .owner={true},.diag={A133_GPU_STAGE_COMPLETE}};
  read_error=clear_error=0;event=false;irq_emit=1;clock_ns=0;
  CHECK(a133_gpu_queue_run(&q)==0 && q.queue_stage==5 && q.queue_attempted==1);
  CHECK(q.queue_log_read==1 && q.queue_log_write==1 && q.queue_log_return==1);
  CHECK(q.queue_cache_read==2 && q.queue_cache_write==2 && q.queue_cache_return==1);
  CHECK(q.queue_irq_window==1 && q.queue_fw_faults==0 && q.queue_hwr_flags==0);
  CHECK(a133_gpu_queue_run(&q)==EBUSY && log_calls==1 && mmu_calls==1 && observe_calls==1);
 }
 {
  struct a133_gpu_softc q={.irq_cookie=&q,.irq_soft=&q,.memory=&q,
      .owner={true},.diag={A133_GPU_STAGE_COMPLETE}};
  event=false;irq_emit=0;clock_ns=0;q.irq_total=7;
  CHECK(a133_gpu_queue_run(&q)==ETIMEDOUT && q.queue_stage==3 && q.queue_attempted==1);
  CHECK(q.queue_irq_total==7 && q.queue_irq_window==0 && a133_gpu_queue_run(&q)==EBUSY);
 }
 {
  struct a133_gpu_softc q={.irq_cookie=&q,.irq_soft=&q,.memory=&q,
      .owner={true},.diag={A133_GPU_STAGE_COMPLETE}};
  event=false;irq_emit=1;clock_ns=0;observe_fault=1;
  CHECK(a133_gpu_queue_run(&q)==EIO && q.queue_stage==4 && q.queue_fw_faults==1);
  CHECK(q.queue_attempted==1 && q.owner.retain_resources && a133_gpu_queue_run(&q)==EBUSY);
 }
 puts("A133 MIPS IRQ drain, window and failure: PASS");return 0;
}
