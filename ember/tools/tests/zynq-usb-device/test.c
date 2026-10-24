/* SPDX-License-Identifier: BSD-2-Clause */
/* The hardware double supplies MMIO and cache services, not controller logic. */
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#define _IOW(a,b,c) (b)
#define _IOR(a,b,c) (b)
#define _IOWR(a,b,c) (b)
#define _IO(a,b) (b)
#define htole32(x) ((uint32_t)(x))
#define le32toh(x) ((uint32_t)(x))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_POSTWRITE 8
#define BUS_SPACE_BARRIER_WRITE 1
#define KASSERT assert
typedef void *bus_space_tag_t;
typedef void *bus_space_handle_t;
typedef void *bus_dma_tag_t;
typedef struct { uint64_t ds_addr; } bus_dma_segment_t;
typedef struct dma_map { bus_dma_segment_t dm_segs[1]; } *bus_dmamap_t;
static uint32_t mmio[0x300/4];
static unsigned delays, syncs, primes, mmio_writes, phy_function_writes;
static bool endpoints_ready_before_run;
static bool final_context;
static uint8_t ulpi_regs[256];
static bool stuck_reset, stuck_flush, stuck_prime, tripwire_race;
static inline uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, unsigned r) { (void)t;(void)h; return mmio[r/4]; }
static inline void bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, unsigned r, uint32_t v) {
 (void)t;(void)h;mmio_writes++;
 if(r==0x170) {
  unsigned addr=(v>>16)&255;
  if(v&0x40000000U) {
   if(v&0x20000000U) {
    if(addr==4)phy_function_writes++;
    if(addr==0x0c)ulpi_regs[0x0a]&=~(uint8_t)v;
    else if(addr==0x0b)ulpi_regs[0x0a]|=(uint8_t)v;
    else ulpi_regs[addr]=(uint8_t)v;
   }
   v=0x08000000U|((uint32_t)ulpi_regs[addr]<<8);
  }else v=0x08000000U;
 }
 if(r==0x1b0)primes++;
 if(r==0x144 || r==0x1ac || r==0x1bc) { mmio[r/4]&=~v; return; }
 if(r==0x140 && (v&1) && !(mmio[r/4]&1))
  endpoints_ready_before_run=mmio[0x158/4]!=0 && mmio[0x148/4]==0x147 && mmio[0x1c0/4]==0x800080;
 if(r==0x140 && (v&2) && !stuck_reset) v&=~2U;
 if(r==0x140 && (v&0x2000) && tripwire_race) { v&=~0x2000U; tripwire_race=false; }
 if(r==0x1b4 && !stuck_flush) { mmio[0x1b8/4]&=~v; mmio[0x1b0/4]&=~v; v=0; }
 if(r==0x1b0 && !stuck_prime) { mmio[0x1b8/4]|=v; v=0; }
 mmio[r/4]=v;
}
static inline void bus_space_barrier(bus_space_tag_t t,bus_space_handle_t h,unsigned off,unsigned len,int op) { (void)t;(void)h;(void)off;(void)len;(void)op; }
static inline void bus_dmamap_sync(bus_dma_tag_t t,bus_dmamap_t map,size_t off,size_t len,int ops) { (void)t; (void)map;(void)off;(void)len;(void)ops; syncs++; }
static inline void delay(unsigned n) { delays+=n; }
#include "driver.inc"
#include "pattern.h"
_Static_assert(sizeof(struct usbperiph_endpoint_gate)==16,"endpoint gate ABI size");
_Static_assert(sizeof(struct usbperiph_snapshot)==144,"snapshot ABI size");
static struct zynqudc fixture_core;
static struct dma_map maps[ZUDC_SLOTS+1];
static struct { _Alignas(2048) uint8_t bytes[4096]; } desc;
static uint8_t buffers[ZUDC_SLOTS][USBPERIPH_MAX_XFER];
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %u line %u: %s\n", checks,(unsigned)__LINE__,#x); exit(1); } } while (0)
static struct usbperiph_config config(void) {
 struct usbperiph_config c={.version=1,.config_length=55,.strings_length=10};
 const uint8_t dev[18]={18,1,0,2,0,0,0,64,0,0,0,0,0,1,0,0,0,1};
 const uint8_t cfg[55]={9,2,55,0,1,1,0,0x80,50,9,4,0,0,4,0xff,0,0,1,7,5,0x81,2,0,2,0,7,5,1,2,0,2,0,7,5,0x82,2,0,2,0,7,5,2,2,0,2,0};
 /* The configuration's literal total is 46 bytes. */
 c.config_length=46; memcpy(c.device,dev,18); memcpy(c.config,cfg,46); c.config[2]=46;
 const uint8_t strings[10]={4,3,9,4,6,3,'I',0,'I',0}; memcpy(c.strings,strings,10);
 return c;
}
static void fixture(void) {
 memset(&fixture_core,0,sizeof(fixture_core));memset(mmio,0,sizeof(mmio));memset(&desc,0,sizeof(desc));
 memset(buffers,0,sizeof(buffers)); delays=syncs=primes=mmio_writes=phy_function_writes=0;endpoints_ready_before_run=false;memset(ulpi_regs,0,sizeof(ulpi_regs));stuck_reset=stuck_flush=stuck_prime=tripwire_race=false;
 fixture_core.nep=4;fixture_core.descriptors.kva=desc.bytes;fixture_core.descriptors.size=4096;fixture_core.descriptors.map=&maps[0];maps[0].dm_segs[0].ds_addr=0x100000;
 fixture_core.qh=(void *)desc.bytes;fixture_core.td=(void *)(desc.bytes+2048);
 for(unsigned i=0;i<ZUDC_SLOTS;i++) { fixture_core.req[i].data.kva=buffers[i];fixture_core.req[i].data.size=USBPERIPH_MAX_XFER;fixture_core.req[i].data.map=&maps[i+1];maps[i+1].dm_segs[0].ds_addr=0x200003+i*0x10000; }
}
static void ready(void) { struct usbperiph_config c=config();CHECK(zynqudc_init(&fixture_core)==0);CHECK(zynqudc_configure(&fixture_core,&c)==0);CHECK(zynqudc_connect(&fixture_core)==0); }
static void setup(uint8_t type,uint8_t req,uint16_t value,uint16_t index,uint16_t length) {
 uint8_t *p=fixture_core.qh[0].setup;p[0]=type;p[1]=req;p[2]=value;p[3]=value>>8;p[4]=index;p[5]=index>>8;p[6]=length;p[7]=length>>8;
 mmio[ZUDC_SETUP/4]=1;mmio[ZUDC_STS/4]=ZUDC_INT_UI;CHECK(zynqudc_intr(&fixture_core)==1);
}
static void done(unsigned slot,unsigned residual,unsigned errors) {
 unsigned ep=slot/2,bit=ep+(slot%2?16:0);fixture_core.td[slot].token=htole32((residual<<16)|errors);
 mmio[ZUDC_STAT/4]&=~(1U<<bit);mmio[ZUDC_COMPLETE/4]=1U<<bit;mmio[ZUDC_STS/4]=ZUDC_INT_UI;CHECK(zynqudc_intr(&fixture_core)==1);
}
/* Actual cdev methods execute with real pthread synchronization. */
typedef void *device_t;
typedef pthread_mutex_t kmutex_t;
typedef pthread_cond_t kcondvar_t;
typedef int callout_t;
typedef unsigned long u_long;
struct lwp;
#define FREAD 1
#define FWRITE 2
#define minor(d) ((unsigned)(d))
static void *lookup;
static int zynqudc_cd;
static const int hz = 1000;
static void softint_schedule(void *cookie) { (void)cookie; }
static void callout_schedule(callout_t *timer,int ticks) { (void)timer;(void)ticks; }
static inline void *device_private(device_t dev) { return dev; }
static const char *device_xname(device_t dev) { (void)dev;return "test-zynqudc"; }
static bool mutex_tryenter(kmutex_t *m) { return pthread_mutex_trylock(m)==0; }
static bool fail_hook, fail_pmf;
static int hook_cookie;
static void *shutdownhook_establish(void (*fn)(void *),void *cookie) { (void)fn;(void)cookie;return fail_hook?NULL:&hook_cookie; }
static bool pmf_device_register1(device_t dev,void *suspend,void *resume,bool (*shutdown)(device_t,int)) { (void)dev;(void)suspend;(void)resume;(void)shutdown;return !fail_pmf; }
static void callout_stop(callout_t *timer) { assert(!final_context);(void)timer; }
static void callout_halt(callout_t *timer,void *lock) { assert(!final_context);(void)timer;(void)lock; }
static int splhigh(void) { return 0; }
static jmp_buf halt_jump;
static _Noreturn void test_reset_halt(void) { longjmp(halt_jump,1); }
#undef __dead
#define __dead __attribute__((noreturn))
#define SPINLOCK_BACKOFF_HOOK test_reset_halt()
static void zynqudc_softirq(void *);
static pthread_mutex_t waiter_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t waiter_cv = PTHREAD_COND_INITIALIZER;
static unsigned waiters;
static void *device_lookup_private(void *cd, unsigned unit) { (void)cd;return unit==0?lookup:NULL; }
static void mutex_enter(kmutex_t *m) { assert(!final_context);assert(pthread_mutex_lock(m)==0); }
static void mutex_exit(kmutex_t *m) { assert(pthread_mutex_unlock(m)==0); }
static void cv_broadcast(kcondvar_t *c) { assert(pthread_cond_broadcast(c)==0); }
static void cv_wait(kcondvar_t *c,kmutex_t *m) { assert(!final_context);assert(pthread_cond_wait(c,m)==0); }
static bool event_spurious;
static int event_ticks, event_budgets[8], event_waits;
static int getticks(void) { assert(!final_context);if(event_spurious)return event_ticks;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int)(t.tv_sec*1000+t.tv_nsec/1000000); }
static int mstohz(unsigned ms) { return (int)ms; }
static int cv_timedwait_sig(kcondvar_t *c,kmutex_t *m,int timeout) {
 assert(!final_context);
 if(event_spurious) {event_budgets[event_waits++]=timeout;event_ticks+=MIN(timeout,400);return timeout<=400||event_waits==5?EWOULDBLOCK:0;}
 struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_sec+=timeout/1000;t.tv_nsec+=(timeout%1000)*1000000;
 if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}
 pthread_mutex_lock(&waiter_lock);waiters++;pthread_cond_broadcast(&waiter_cv);pthread_mutex_unlock(&waiter_lock);
 int result=pthread_cond_timedwait(c,m,&t);
 return result==ETIMEDOUT?EWOULDBLOCK:result;
}
static pthread_mutex_t copy_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t copy_cv=PTHREAD_COND_INITIALIZER;
static bool pause_copyin, pause_copyout, copy_paused, resume_copy;
static void copy_boundary(bool pause) {
 pthread_mutex_lock(&copy_lock);
 if(pause){copy_paused=true;pthread_cond_broadcast(&copy_cv);while(!resume_copy)pthread_cond_wait(&copy_cv,&copy_lock);}
 pthread_mutex_unlock(&copy_lock);
}
static int copyin(const void *src,void *dst,size_t len) { if(src==NULL)return EFAULT;memcpy(dst,src,len);copy_boundary(pause_copyin);return 0; }
static int copyout(const void *src,void *dst,size_t len) { if(dst==NULL)return EFAULT;memcpy(dst,src,len);copy_boundary(pause_copyout);return 0; }
#include "cdev.inc"
static struct zynqudc_softc attachment;
struct transfer_thread { struct usbperiph_xfer x;int result;bool pause_entry; };
static void *xfer_thread(void *cookie) {
 struct transfer_thread *t=cookie;if(t->pause_entry)copy_boundary(true);t->result=zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&t->x,0,NULL);return NULL;
}
static void wait_for(unsigned n) {
 pthread_mutex_lock(&waiter_lock);while(waiters<n)pthread_cond_wait(&waiter_cv,&waiter_lock);pthread_mutex_unlock(&waiter_lock);
}
static void test_device_phy(void) {
 struct zynqudc_softc phy={0};ulpi_regs[0x0a]=0x7f;ulpi_regs[4]=0x45;
 CHECK(zynqudc_phy(&phy)==0);CHECK(ulpi_regs[0x0a]==4);
 CHECK((ulpi_regs[4]&0x5f)==0x41);CHECK(phy.phy_initial_valid);
 CHECK(phy.phy_function_initial==0x41);CHECK(phy.phy_otg_initial==4);
}
static void test_stopped_irq(void) {
 fixture();ready();memset(&attachment,0,sizeof(attachment));attachment.core=fixture_core;
 attachment.ready=attachment.opened=true;lookup=&attachment;
 pthread_mutex_init(&attachment.lock,NULL);pthread_cond_init(&attachment.cv,NULL);
 const uint8_t request[8]={0x80,6,0,1,0,0,18,0};memcpy(attachment.core.qh[0].setup,request,8);
 mmio[ZUDC_SETUP/4]=1;mmio[ZUDC_STS/4]=1;
 CHECK(zynqudc_irq(&attachment)==1);unsigned before=primes;
 CHECK(zynqudc_close(0,0,0,NULL)==0);zynqudc_softirq(&attachment);zynqudc_timer(&attachment);
 CHECK(primes==before);
 CHECK(zynqudc_open(0,FREAD|FWRITE,0,NULL)==0);
 memcpy(attachment.core.qh[0].setup,request,8);mmio[ZUDC_SETUP/4]=1;mmio[ZUDC_STS/4]=1;
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_CONNECT,NULL,0,NULL)==0);
 zynqudc_softirq(&attachment);CHECK(primes==before);
 CHECK(zynqudc_close(0,0,0,NULL)==0);CHECK(!attachment.core.req[1].active);
 pthread_cond_destroy(&attachment.cv);pthread_mutex_destroy(&attachment.lock);
}
static void attachment_fixture(bool configure) {
 fixture();ready();
 if(configure){setup(0,9,1,0,0);done(1,0,0);}
 memset(&attachment,0,sizeof(attachment));attachment.core=fixture_core;
 attachment.dev=&attachment;attachment.ready=attachment.opened=true;lookup=&attachment;
 pthread_mutex_init(&attachment.lock,NULL);pthread_cond_init(&attachment.cv,NULL);
 fail_hook=fail_pmf=false;
}
static void attachment_finish(void) {
 pthread_cond_destroy(&attachment.cv);pthread_mutex_destroy(&attachment.lock);
}
static void expect_final(bool blocked) {
 volatile bool halted=false;final_context=true;
 if(setjmp(halt_jump)==0)zynqudc_shutdown_final(&attachment);else halted=true;
 final_context=false;
 CHECK(halted==blocked);
 /* Both the positive certificate and a failing guard can retain the lock. */
 int locked=pthread_mutex_trylock(&attachment.lock);CHECK(locked==0||locked==EBUSY);
 pthread_mutex_unlock(&attachment.lock);
}
static void test_shutdown(void) {
 attachment_fixture(true);CHECK(zynqudc_register_shutdown(&attachment));
 CHECK(zynqudc_submit(&attachment.core,0x81,512,0)==0);
 CHECK(zynqudc_shutdown(&attachment,0));CHECK(attachment.stopping);CHECK(attachment.reset_drained);
 unsigned before=primes;zynqudc_softirq(&attachment);zynqudc_timer(&attachment);CHECK(primes==before);
 expect_final(false);attachment_finish();
 /* PMF skipped by panic or cold reboot: even apparently idle state is unproved. */
 attachment_fixture(false);expect_final(true);attachment_finish();
 attachment_fixture(false);mutex_enter(&attachment.lock);CHECK(!zynqudc_shutdown(&attachment,0));expect_final(true);attachment_finish();
 attachment_fixture(true);CHECK(zynqudc_submit(&attachment.core,0x81,64,0)==0);
 stuck_flush=true;CHECK(!zynqudc_shutdown(&attachment,0));CHECK(attachment.core.req[3].retained);
 before=primes;zynqudc_softirq(&attachment);CHECK(primes==before);expect_final(true);attachment_finish();stuck_flush=false;
 attachment_fixture(false);CHECK(zynqudc_shutdown(&attachment,0));mmio[ZUDC_STAT/4]=1;
 expect_final(true);attachment_finish();
 attachment_fixture(false);CHECK(zynqudc_disconnect(&attachment.core)==0);fail_hook=true;CHECK(!zynqudc_register_shutdown(&attachment));
 CHECK(attachment.stopping);CHECK(zynqudc_connect(&attachment.core)==EIO);attachment_finish();
 attachment_fixture(false);CHECK(zynqudc_disconnect(&attachment.core)==0);fail_pmf=true;CHECK(!zynqudc_register_shutdown(&attachment));
 CHECK(attachment.stopping);CHECK(zynqudc_connect(&attachment.core)==EIO);expect_final(true);attachment_finish();
}
static void test_event_budget(void) {
 attachment_fixture(false);attachment.core.event_pending=false;
 event_spurious=true;event_ticks=event_waits=0;struct usbperiph_event event;
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_GET_EVENT,&event,0,NULL)==EWOULDBLOCK);
 CHECK(event_ticks<=1000);CHECK(event_waits==3);CHECK(event_budgets[0]==1000);
 CHECK(event_budgets[1]==600);CHECK(event_budgets[2]==200);
 event_spurious=false;attachment_finish();
}
static void test_fault_irq(void) {
 attachment_fixture(true);CHECK(zynqudc_submit(&attachment.core,0x81,32,0)==0);
 stuck_flush=true;CHECK(zynqudc_cancel(&attachment.core,0x81,ECANCELED)==ETIMEDOUT);stuck_flush=false;
 unsigned before=primes;const uint8_t request[8]={0x80,6,0,1,0,0,18,0};
 memcpy(attachment.core.qh[0].setup,request,8);mmio[ZUDC_SETUP/4]=1;mmio[ZUDC_STS/4]=1;
 zynqudc_softirq(&attachment);zynqudc_timer(&attachment);CHECK(primes==before);
 CHECK(attachment.core.req[3].retained);CHECK(attachment.core.req[3].active);
 struct usbperiph_reply reply={.generation=attachment.core.generation,.sequence=attachment.core.sequence};
 CHECK(zynqudc_reply(&attachment.core,&reply)==EIO);CHECK(primes==before);attachment_finish();
}
static void test_status_recipients(void) {
 fixture();CHECK(zynqudc_init(&fixture_core)==0);struct usbperiph_config c=config();
 memmove(c.config+41,c.config+32,14);const uint8_t second[9]={9,4,1,0,2,0xff,0,0,0};
 memcpy(c.config+32,second,9);c.config_length=55;c.config[2]=55;c.config[4]=2;c.config[13]=2;
 CHECK(zynqudc_configure(&fixture_core,&c)==0);CHECK(zynqudc_connect(&fixture_core)==0);
 setup(0x81,0,0,0,2);CHECK((mmio[ZUDC_EPCTRL(0)/4]&0x10001)==0x10001);
 setup(0,9,1,0,0);done(1,0,0);setup(0x81,0,0,1,2);
 CHECK((mmio[ZUDC_EPCTRL(0)/4]&0x10001)==0);CHECK(buffers[1][0]==0);done(1,0,0);done(0,0,0);
 setup(0x81,0,0,2,2);CHECK((mmio[ZUDC_EPCTRL(0)/4]&0x10001)==0x10001);
 setup(0x82,0,0,0x83,2);CHECK((mmio[ZUDC_EPCTRL(0)/4]&0x10001)==0x10001);
 setup(0x82,0,0,0x80,2);CHECK((mmio[ZUDC_EPCTRL(0)/4]&0x10001)==0);done(1,0,0);done(0,0,0);
}
static void wait_copy(void) {
 pthread_mutex_lock(&copy_lock);while(!copy_paused)pthread_cond_wait(&copy_cv,&copy_lock);pthread_mutex_unlock(&copy_lock);
}
static void release_copy(void) {
 pthread_mutex_lock(&copy_lock);resume_copy=true;pthread_cond_broadcast(&copy_cv);pthread_mutex_unlock(&copy_lock);
}
static void test_endpoint_gate(void) {
 attachment_fixture(true);struct usbperiph_endpoint_gate gate={.endpoint=0x81,.enable=0};
 const uint8_t data[32]={0};unsigned before=primes;
 struct usbperiph_xfer x={.buffer=(uintptr_t)data,.endpoint=0x81,.length=32,.timeout_ms=10};
 /* A thread paused before ioctl entry must see the gate closed by CLOSE_PIPE. */
 struct transfer_thread early={.x=x,.pause_entry=true};pthread_t early_id;
 copy_paused=resume_copy=false;CHECK(pthread_create(&early_id,NULL,xfer_thread,&early)==0);wait_copy();
 CHECK(attachment.operations==0);
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==0);
 release_copy();CHECK(pthread_join(early_id,NULL)==0);CHECK(early.result==EPIPE);
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&x,0,NULL)==EPIPE);CHECK(primes==before);
 gate.enable=1;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==0);
 pause_copyin=true;copy_paused=resume_copy=false;
 struct transfer_thread thread={.x=x};pthread_t id;CHECK(pthread_create(&id,NULL,xfer_thread,&thread)==0);wait_copy();
 gate.enable=0;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==0);CHECK(primes==before);
 release_copy();CHECK(pthread_join(id,NULL)==0);CHECK(thread.result==EPIPE);CHECK(primes==before);pause_copyin=false;
 /* Reopen cannot resurrect an old request paused in its copy boundary. */
 gate.enable=1;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==0);
 pause_copyin=true;copy_paused=resume_copy=false;CHECK(pthread_create(&id,NULL,xfer_thread,&thread)==0);wait_copy();
 gate.enable=0;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==0);
 gate.enable=1;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==0);
 release_copy();CHECK(pthread_join(id,NULL)==0);CHECK(thread.result==ESTALE);CHECK(primes==before);pause_copyin=false;
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&x,0,NULL)==ETIMEDOUT);CHECK(primes==before+1);
 /* An admitted request is canceled before the close barrier returns. */
 thread.x.timeout_ms=2000;unsigned old=waiters;CHECK(pthread_create(&id,NULL,xfer_thread,&thread)==0);wait_for(old+1);
 gate.enable=0;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==0);
 CHECK(pthread_join(id,NULL)==0);CHECK(thread.result==ECANCELED);CHECK(!attachment.core.req[3].active);
 gate.generation=attachment.core.generation+1;gate.enable=1;
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==ESTALE);
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&x,0,NULL)==EPIPE);
 gate.generation=attachment.core.generation;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==0);
 gate.endpoint=0x80;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==EINVAL);
 gate.endpoint=0;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==EINVAL);
 gate.endpoint=0x83;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==EINVAL);
 gate.endpoint=0x81;gate.enable=2;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==EINVAL);
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_DISCONNECT,NULL,0,NULL)==0);
 gate.generation=0;gate.enable=0;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==0);
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_CONNECT,NULL,0,NULL)==0);
 /* Gates survive a new connection generation until explicitly reopened. */
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&x,0,NULL)==EPIPE);
 CHECK(zynqudc_close(0,0,0,NULL)==0);CHECK(zynqudc_open(0,FREAD|FWRITE,0,NULL)==0);
 x.generation=0;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&x,0,NULL)==ENXIO);attachment_finish();
}
static void test_gate_copyout_and_fault(void) {
 attachment_fixture(true);uint8_t data[32]={0};struct transfer_thread thread={.x={
  .buffer=(uintptr_t)data,.endpoint=1,.length=32,.timeout_ms=2000}};
 struct usbperiph_endpoint_gate gate={.endpoint=1};pthread_t id;
 pause_copyout=true;copy_paused=resume_copy=false;unsigned old=waiters;
 CHECK(pthread_create(&id,NULL,xfer_thread,&thread)==0);wait_for(old+1);
 mutex_enter(&attachment.lock);memset(buffers[2],0xa5,32);attachment.core.td[2].token=0;
 mmio[ZUDC_STAT/4]&=~2U;mmio[ZUDC_COMPLETE/4]=2;mmio[ZUDC_STS/4]=1;
 CHECK(zynqudc_intr(&attachment.core)==1);cv_broadcast(&attachment.cv);mutex_exit(&attachment.lock);
 wait_copy();CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==0);
 release_copy();CHECK(pthread_join(id,NULL)==0);CHECK(thread.result==EPIPE);
 /* This direct cdev assertion checks kernel-local state, not syscall copyback. */
 CHECK(thread.x.actual==0);CHECK(data[0]==0xa5);
 pause_copyout=false;CHECK(zynqudc_close(0,0,0,NULL)==0);attachment_finish();
 memset(data,0,sizeof(data));attachment_fixture(true);thread.x.endpoint=0x81;thread.x.generation=0;old=waiters;
 CHECK(pthread_create(&id,NULL,xfer_thread,&thread)==0);wait_for(old+1);
 stuck_flush=true;gate.endpoint=0x81;
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==ETIMEDOUT);
 CHECK(pthread_join(id,NULL)==0);CHECK(thread.result==EIO);CHECK(attachment.core.req[3].active);
 CHECK(attachment.core.req[3].retained);gate.enable=1;
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_ENDPOINT_GATE,&gate,0,NULL)==EIO);
 unsigned before=primes;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&thread.x,0,NULL)==EIO);CHECK(primes==before);
 attachment_finish();stuck_flush=false;
}
static void test_snapshot(void) {
 attachment_fixture(true);CHECK(zynqudc_phy(&attachment)==0);
 CHECK(zynqudc_submit(&attachment.core,0x81,32,0)==0);
 mmio[ZUDC_STS/4]=0x145;mmio[ZUDC_SETUP/4]=1;mmio[ZUDC_COMPLETE/4]=0x10000;
 /* Cached PHY is visibly distinct from later live register values. */
 ulpi_regs[4]=0x99;ulpi_regs[0x0a]=0x88;
 struct usbperiph_snapshot snapshot;memset(&snapshot,0xa5,sizeof(snapshot));unsigned before=mmio_writes;
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_SNAPSHOT,&snapshot,0,NULL)==0);
 CHECK(mmio_writes==before);CHECK(snapshot.version==1);CHECK(snapshot.reserved0==0);CHECK(snapshot.reserved1==0);
 const uint8_t zero[6]={0};CHECK(memcmp(snapshot.reserved,zero,6)==0);
 CHECK(snapshot.flags==0x11);CHECK(snapshot.configuration==1);CHECK(snapshot.address==0);CHECK(snapshot.ep0_phase==0);
 CHECK(snapshot.irq_count==2);CHECK(snapshot.setup_count==1);CHECK(snapshot.reset_count==0);CHECK(snapshot.disconnect_count==0);
 CHECK(snapshot.last_irq_status==1);const uint8_t last_setup[8]={0,9,1,0,0,0,0,0};
 CHECK(memcmp(snapshot.last_setup,last_setup,8)==0);CHECK(snapshot.active_slots==8);CHECK(snapshot.retained_slots==0);
 CHECK(snapshot.command==1);CHECK(snapshot.status==0x145);CHECK(snapshot.interrupt_enable==0x147);
 CHECK(snapshot.mode==0xa);CHECK(snapshot.otg==8);CHECK(snapshot.port==0x80000000);
 CHECK(snapshot.setup_status==1);CHECK(snapshot.complete==0x10000);CHECK(snapshot.endpoint_status==0x20000);
 CHECK(mmio[ZUDC_STS/4]==0x145);CHECK(mmio[ZUDC_SETUP/4]==1);CHECK(mmio[ZUDC_COMPLETE/4]==0x10000);
 CHECK(snapshot.phy_function_initial==0x41);CHECK(snapshot.phy_otg_initial==4);
 zynqudc_softirq(&attachment);CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_SNAPSHOT,&snapshot,0,NULL)==0);
 CHECK(snapshot.irq_count==3);CHECK(snapshot.reset_count==1);CHECK(snapshot.active_slots==0);CHECK(snapshot.configuration==0);
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_DISCONNECT,NULL,0,NULL)==0);
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_SNAPSHOT,&snapshot,0,NULL)==0);CHECK(snapshot.disconnect_count==1);
 CHECK((snapshot.flags&USBPERIPH_SNAPSHOT_CONNECTED)==0);attachment_finish();
}
static void test_controller_owned_pullup(void) {
 fixture();CHECK(zynqudc_init(&fixture_core)==0);struct usbperiph_config c=config();
 CHECK(zynqudc_configure(&fixture_core,&c)==0);
 memset(&attachment,0,sizeof(attachment));attachment.core=fixture_core;attachment.ready=attachment.opened=true;lookup=&attachment;
 pthread_mutex_init(&attachment.lock,NULL);pthread_cond_init(&attachment.cv,NULL);
 ulpi_regs[4]=0x45;CHECK(zynqudc_phy(&attachment)==0);
 CHECK((ulpi_regs[4]&4)==0);CHECK((mmio[ZUDC_CMD/4]&1)==0);
 unsigned writes=phy_function_writes;
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_CONNECT,NULL,0,NULL)==0);
 CHECK((mmio[ZUDC_CMD/4]&1)==1);CHECK(endpoints_ready_before_run);
 /* RS is the controller-owned attach control; no manual TERM override follows. */
 CHECK(phy_function_writes==writes);
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_DISCONNECT,NULL,0,NULL)==0);
 CHECK((mmio[ZUDC_CMD/4]&1)==0);CHECK(phy_function_writes==writes);attachment_finish();
}
static void test_cdev(void) {
 fixture();ready();setup(0,9,1,0,0);done(1,0,0);
 memset(&attachment,0,sizeof(attachment));attachment.core=fixture_core;attachment.ready=true;lookup=&attachment;
 pthread_mutex_init(&attachment.lock,NULL);pthread_cond_init(&attachment.cv,NULL);waiters=0;
 CHECK(zynqudc_open(0,FREAD|FWRITE,0,NULL)==0);CHECK(zynqudc_open(0,FREAD|FWRITE,0,NULL)==EBUSY);
 uint8_t outbuf[512];
 memset(outbuf,0xa5,sizeof(outbuf));
 const uint8_t inbuf[512]={0};
 struct usbperiph_xfer badcopy={.buffer=0,.endpoint=0x81,.length=1,.timeout_ms=100};
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&badcopy,0,NULL)==EFAULT);CHECK(!attachment.core.req[3].active);
 struct transfer_thread a={.x={.buffer=(uintptr_t)outbuf,.endpoint=1,.length=512,.timeout_ms=2000}};
 struct transfer_thread b={.x={.buffer=(uintptr_t)inbuf,.endpoint=0x82,.length=512,.timeout_ms=2000}};
 pthread_t ta,tb;CHECK(pthread_create(&ta,NULL,xfer_thread,&a)==0);wait_for(1);
 CHECK(pthread_create(&tb,NULL,xfer_thread,&b)==0);wait_for(2);
 mutex_enter(&attachment.lock);
 CHECK(attachment.core.req[2].active);CHECK(attachment.core.req[5].active);
 attachment.core.td[5].token=0;mmio[ZUDC_STAT/4]&=~(1U<<18);mmio[ZUDC_COMPLETE/4]=1U<<18;mmio[ZUDC_STS/4]=1;
 CHECK(zynqudc_intr(&attachment.core)==1);cv_broadcast(&attachment.cv);mutex_exit(&attachment.lock);
 CHECK(pthread_join(tb,NULL)==0);CHECK(b.result==0);CHECK(b.x.actual==512);
 uint32_t ep=1;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_CANCEL,&ep,0,NULL)==0);
 CHECK(pthread_join(ta,NULL)==0);CHECK(a.result==ECANCELED);
 a.x.timeout_ms=5;CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&a.x,0,NULL)==ETIMEDOUT);CHECK(!attachment.core.req[2].active);
 a.x.generation=0;a.x.timeout_ms=2000;unsigned old=waiters;CHECK(pthread_create(&ta,NULL,xfer_thread,&a)==0);wait_for(old+1);
 CHECK(zynqudc_close(0,0,0,NULL)==0);CHECK(pthread_join(ta,NULL)==0);CHECK(a.result==ENXIO);
 CHECK(!attachment.opened);CHECK(attachment.operations==0);
 CHECK(zynqudc_open(0,FREAD|FWRITE,0,NULL)==0);
 attachment.core.faulted=true;attachment.core.req[3].active=true;attachment.core.req[3].retained=true;
 buffers[3][0]=0x55;uint8_t damage=0xaa;
 struct usbperiph_xfer retained={.buffer=(uintptr_t)&damage,.endpoint=0x81,.length=1,.timeout_ms=100};
 CHECK(zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&retained,0,NULL)==EIO);
 CHECK(buffers[3][0]==0x55);
 pthread_cond_destroy(&attachment.cv);pthread_mutex_destroy(&attachment.lock);
}
int main(void) {
 uint8_t pattern[9];usbperiph_counter(pattern,9,1021);
 const uint8_t expected[9]={0,0,0,0,1,0,0,1,1};
 CHECK(memcmp(pattern,expected,9)==0);
 /* A peripheral match must never claim missing, host, or dynamic OTG roles. */
 CHECK(zynqudc_role("peripheral")==1);CHECK(zynqudc_role("host")==0);CHECK(zynqudc_role(NULL)==0);CHECK(zynqudc_role("otg")==0);
 fixture();fixture_core.descriptors.map->dm_segs[0].ds_addr++;CHECK(zynqudc_init(&fixture_core)==EINVAL);
 fixture();fixture_core.descriptors.map->dm_segs[0].ds_addr=0x100000000ULL;CHECK(zynqudc_init(&fixture_core)==EFBIG);
 fixture();stuck_reset=true;CHECK(zynqudc_init(&fixture_core)==ETIMEDOUT);CHECK(delays<=ZUDC_POLL_US);
 fixture();ready();CHECK(mmio[ZUDC_MODE/4]==0xa);CHECK(mmio[ZUDC_OTG/4]==8);CHECK((mmio[ZUDC_PORT/4]&0x1000)==0);
 CHECK(zynqudc_submit(&fixture_core,0x81,128,0)==ENXIO);
 setup(0x80,6,0x100,0,18);CHECK(fixture_core.req[1].active);CHECK(memcmp(buffers[1],fixture_core.config.device,18)==0);done(1,0,0);CHECK(fixture_core.req[0].active);done(0,0,0);
 setup(0,5,17,0,0);CHECK(mmio[ZUDC_ADDR/4]==0);done(1,0,0);CHECK(mmio[ZUDC_ADDR/4]==17U<<25);
 setup(0,9,1,0,0);done(1,0,0);CHECK(fixture_core.configuration==1);CHECK((mmio[ZUDC_EPCTRL(1)/4]&0x800080)==0x800080);
 CHECK(zynqudc_submit(&fixture_core,0x81,16384,0)==0);CHECK((le32toh(fixture_core.td[3].next)&1)==1);CHECK(le32toh(fixture_core.td[3].page[0])==0x230003);CHECK(le32toh(fixture_core.td[3].page[4])==0x234000);
 /* Active dTD is still owned by hardware despite a completion interrupt. */
 mmio[ZUDC_COMPLETE/4]=1U<<17;mmio[ZUDC_STS/4]=1;CHECK(zynqudc_intr(&fixture_core)==1);CHECK(fixture_core.req[3].active);
 done(3,384,0);CHECK(!fixture_core.req[3].active);CHECK(fixture_core.req[3].actual==16000);CHECK(fixture_core.req[3].status==0);
 CHECK(zynqudc_submit(&fixture_core,0x02,1024,fixture_core.generation-1)==ESTALE);CHECK(zynqudc_submit(&fixture_core,0x02,16385,0)==EINVAL);
 CHECK(zynqudc_submit(&fixture_core,0x02,512,0)==0);done(4,400,0);CHECK(fixture_core.req[4].actual==112);
 setup(0x80,8,0,0,1);CHECK(buffers[1][0]==1);done(1,0,0);done(0,0,0);
 setup(0x02,3,0,0x81,0);done(1,0,0);CHECK(zynqudc_submit(&fixture_core,0x81,4,0)==EPIPE);
 setup(0x82,0,0,0x81,2);CHECK(buffers[1][0]==1);done(1,0,0);done(0,0,0);
 setup(0x02,1,0,0x81,0);done(1,0,0);CHECK(zynqudc_submit(&fixture_core,0x81,4,0)==0);CHECK(zynqudc_cancel(&fixture_core,0x81,ECANCELED)==0);CHECK(fixture_core.req[3].status==ECANCELED);
 setup(0,5,255,0,0);CHECK((mmio[ZUDC_EPCTRL(0)/4]&0x10001)==0x10001);
 tripwire_race=true;setup(0x80,6,0x100,0,8);CHECK(fixture_core.req[1].length==8);
 /* New setup drains the previous transfer and clears pending address. */
 setup(0,5,19,0,0);setup(0x80,6,0x100,0,8);done(1,0,0);CHECK(mmio[ZUDC_ADDR/4]==17U<<25);done(0,0,0);
 setup(0x41,1,0,0,0);CHECK(fixture_core.vendor_pending);CHECK(fixture_core.event.setup[1]==1);
 struct usbperiph_reply reply={.generation=fixture_core.generation,.sequence=fixture_core.sequence};CHECK(zynqudc_reply(&fixture_core,&reply)==0);done(1,0,0);
 setup(0x41,2,0,0,0);zynqudc_tick(&fixture_core);zynqudc_tick(&fixture_core);CHECK(!fixture_core.vendor_pending);CHECK((mmio[ZUDC_EPCTRL(0)/4]&0x10001)==0x10001);
 CHECK(zynqudc_submit(&fixture_core,0x81,128,0)==0);uint64_t gen=fixture_core.generation;
 mmio[ZUDC_STS/4]=ZUDC_INT_URI;CHECK(zynqudc_intr(&fixture_core)==1);CHECK(fixture_core.generation>gen);CHECK(!fixture_core.req[3].active);CHECK(fixture_core.configuration==0);CHECK(mmio[ZUDC_ADDR/4]==0);
 setup(0,9,1,0,0);done(1,0,0);CHECK(zynqudc_submit(&fixture_core,0x81,32,0)==0);
 mmio[ZUDC_STS/4]=ZUDC_INT_SLI;CHECK(zynqudc_intr(&fixture_core)==1);CHECK(fixture_core.suspended);CHECK(zynqudc_submit(&fixture_core,0x82,8,0)==EAGAIN);
 CHECK(zynqudc_disconnect(&fixture_core)==0);CHECK(!fixture_core.req[3].active);CHECK(!fixture_core.connected);
 fixture();ready();setup(0,9,1,0,0);done(1,0,0);CHECK(zynqudc_submit(&fixture_core,0x81,32,0)==0);stuck_flush=true;CHECK(zynqudc_cancel(&fixture_core,0x81,ECANCELED)==ETIMEDOUT);CHECK(fixture_core.req[3].retained);CHECK(fixture_core.faulted);CHECK(fixture_core.req[3].active);CHECK(zynqudc_connect(&fixture_core)==EIO);
 fixture();ready();struct usbperiph_config bad=config();bad.config[2]=45;CHECK(zynqudc_configure(&fixture_core,&bad)==EBUSY);CHECK(zynqudc_disconnect(&fixture_core)==0);CHECK(zynqudc_configure(&fixture_core,&bad)==EINVAL);
 fixture();fixture_core.descriptors.map->dm_segs[0].ds_addr+=32;CHECK(zynqudc_init(&fixture_core)==EINVAL);
 test_device_phy();
 test_stopped_irq();
 test_fault_irq();
 test_shutdown();
 test_event_budget();
 test_status_recipients();
 test_endpoint_gate();
 test_gate_copyout_and_fault();
 test_snapshot();
 test_controller_owned_pullup();
 test_cdev();
 printf("zynqudc: %u checks passed\n",checks);return 0;
}
