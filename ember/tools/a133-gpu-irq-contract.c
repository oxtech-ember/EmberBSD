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
struct pvr_job_irq { unsigned total, window; int fault; bool armed, live; };
struct a133_gpu_softc {
 struct { struct pvr_job_irq irq; } job_irq;
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
static void pvr_job_irq_probe_wait(struct pvr_job_irq *irq,int t) { CHECK(irq->armed && t==1);clock_ns+=10000000; }
static void aprint_error_dev(void *d,const char *fmt,...) { (void)d;(void)fmt; }

static bool pvr_job_irq_active(struct pvr_job_irq *irq) { return irq->live; }
static int pvr_job_irq_probe_arm(void *arg) { struct pvr_job_irq *irq=arg;irq->window=0;irq->armed=true;return irq->fault; }
static int pvr_job_irq_probe(struct pvr_job_irq *irq,unsigned *total,unsigned *window,bool finish)
{ if(total)*total=irq->total; if(window)*window=irq->window; if(finish)irq->armed=false; return irq->fault; }
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
 if(irq_emit) { struct pvr_job_irq *irq=cookie;CHECK(irq->armed);irq->total++;irq->window++; }
 r->polls=1;r->after=(struct snapshot){2,2,1,0,0};return 0;
}
int main(void) {
 {
  struct a133_gpu_softc q={.job_irq={.irq={.live=true}},.memory=&q,
      .owner={true},.diag={A133_GPU_STAGE_COMPLETE}};
  irq_emit=1;clock_ns=0;
  CHECK(a133_gpu_queue_run(&q)==0 && q.queue_stage==5 && q.queue_attempted==1);
  CHECK(q.queue_log_read==1 && q.queue_log_write==1 && q.queue_log_return==1);
  CHECK(q.queue_cache_read==2 && q.queue_cache_write==2 && q.queue_cache_return==1);
  CHECK(q.queue_irq_window==1 && q.queue_fw_faults==0 && q.queue_hwr_flags==0);
  CHECK(a133_gpu_queue_run(&q)==EBUSY && log_calls==1 && mmu_calls==1 && observe_calls==1);
 }
 {
  struct a133_gpu_softc q={.job_irq={.irq={.live=true}},.memory=&q,
      .owner={true},.diag={A133_GPU_STAGE_COMPLETE}};
  irq_emit=0;clock_ns=0;q.job_irq.irq.total=7;
  CHECK(a133_gpu_queue_run(&q)==ETIMEDOUT && q.queue_stage==3 && q.queue_attempted==1);
  CHECK(q.queue_irq_total==7 && q.queue_irq_window==0 && a133_gpu_queue_run(&q)==EBUSY);
 }
 {
  struct a133_gpu_softc q={.job_irq={.irq={.live=true}},.memory=&q,
      .owner={true},.diag={A133_GPU_STAGE_COMPLETE}};
  irq_emit=1;clock_ns=0;observe_fault=1;
  CHECK(a133_gpu_queue_run(&q)==EIO && q.queue_stage==4 && q.queue_fw_faults==1);
  CHECK(q.queue_attempted==1 && q.owner.retain_resources && a133_gpu_queue_run(&q)==EBUSY);
 }
 puts("A133 MIPS IRQ drain, window and failure: PASS");return 0;
}
