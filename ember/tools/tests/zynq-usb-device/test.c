/* SPDX-License-Identifier: BSD-2-Clause */
/* The hardware double supplies MMIO and cache services, not controller logic. */
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <stdint.h>
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
static unsigned delays, syncs;
static bool stuck_reset, stuck_flush, stuck_prime, tripwire_race;
static inline uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, unsigned r) { (void)t;(void)h; return mmio[r/4]; }
static inline void bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, unsigned r, uint32_t v) {
 (void)t;(void)h;
 if(r==0x144 || r==0x1ac || r==0x1bc) { mmio[r/4]&=~v; return; }
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
 memset(buffers,0,sizeof(buffers)); delays=syncs=0;stuck_reset=stuck_flush=stuck_prime=tripwire_race=false;
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
static pthread_mutex_t waiter_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t waiter_cv = PTHREAD_COND_INITIALIZER;
static unsigned waiters;
static void *device_lookup_private(void *cd, unsigned unit) { (void)cd;return unit==0?lookup:NULL; }
static void mutex_enter(kmutex_t *m) { assert(pthread_mutex_lock(m)==0); }
static void mutex_exit(kmutex_t *m) { assert(pthread_mutex_unlock(m)==0); }
static void cv_broadcast(kcondvar_t *c) { assert(pthread_cond_broadcast(c)==0); }
static void cv_wait(kcondvar_t *c,kmutex_t *m) { assert(pthread_cond_wait(c,m)==0); }
static int getticks(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int)(t.tv_sec*1000+t.tv_nsec/1000000); }
static int mstohz(unsigned ms) { return (int)ms; }
static int cv_timedwait_sig(kcondvar_t *c,kmutex_t *m,int timeout) {
 struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_sec+=timeout/1000;t.tv_nsec+=(timeout%1000)*1000000;
 if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}
 pthread_mutex_lock(&waiter_lock);waiters++;pthread_cond_broadcast(&waiter_cv);pthread_mutex_unlock(&waiter_lock);
 int result=pthread_cond_timedwait(c,m,&t);
 return result==ETIMEDOUT?EWOULDBLOCK:result;
}
static int copyin(const void *src,void *dst,size_t len) { if(src==NULL)return EFAULT;memcpy(dst,src,len);return 0; }
static int copyout(const void *src,void *dst,size_t len) { if(dst==NULL)return EFAULT;memcpy(dst,src,len);return 0; }
#include "cdev.inc"
static struct zynqudc_softc attachment;
struct transfer_thread { struct usbperiph_xfer x;int result; };
static void *xfer_thread(void *cookie) {
 struct transfer_thread *t=cookie;t->result=zynqudc_ioctl(0,USBPERIPH_IOC_XFER,&t->x,0,NULL);return NULL;
}
static void wait_for(unsigned n) {
 pthread_mutex_lock(&waiter_lock);while(waiters<n)pthread_cond_wait(&waiter_cv,&waiter_lock);pthread_mutex_unlock(&waiter_lock);
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
 fixture();ready();CHECK(mmio[ZUDC_MODE/4]==0xa);CHECK((mmio[ZUDC_PORT/4]&0x1000)==0);
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
 test_cdev();
 printf("zynqudc: %u checks passed\n",checks);return 0;
}
