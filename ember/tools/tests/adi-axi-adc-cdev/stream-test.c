/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual ADC read path with deterministic DMA and uio services. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef ERESTART
#define ERESTART 85
#endif
#define KASSERT assert
#define IO_NDELAY 1
#define ADI_AXI_ADC_STREAM_ABI_VERSION 2
#define ADI_AXI_ADC_STREAM_MAX_BYTES 4194304
#define ADI_AXI_ADC_CAPTURE_MAX_BYTES 1048576
#define ADI_AXI_ADC_CAPTURE_ALIGNMENT 8
#define KM_SLEEP 0
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
#define ADI_AXI_ADC_STREAM_MAX_BLOCKS 16
#define ADI_AXI_ADC_STREAM_RUNNING 1
#define NOTE_SUBMIT 0
#define POLLERR 1
#define POLLHUP 2
#define POLLIN 4
#define POLLRDNORM 8
struct lwp { int unused; };
#define ADC_DMA_STATUS 0x88
#define ADI_AXI_ADC_STREAM_LOSS_UNKNOWN 2
#define uimin(a,b) ((size_t)(a)<(size_t)(b)?(size_t)(a):(size_t)(b))
struct adi_axi_dmac_buffer { int unused; };
struct converter { int c_lock; bool c_poisoned; };
struct capture_ops { int (*quiesce)(void *, unsigned); };
struct adi_axi_adc_lease {
 void *l_capture_guard;
 struct converter *l_converter;
 const struct capture_ops *l_ops;
 void *l_cookie;
 bool l_poisoned,l_running,l_quiet;
};
struct adi_axi_dmac_stream_status {
 uint64_t sequence,completed_blocks,completed_bytes,discontinuities;
 unsigned queued,ready;
};
struct adi_axi_rx_status { uint32_t sample_rate_hz; };
struct adi_axi_adc_stream_config { unsigned abi_version,struct_size,block_bytes,block_count,reserved[4]; };
struct status {
 uint64_t generation;
 unsigned block_count,sample_rate_hz;
 uint64_t sequence,completed_blocks,completed_bytes,discontinuities;
 unsigned flags,block_bytes,block_offset,ready_blocks;
 int last_error;
};
struct adi_axi_adc_softc {
 struct adi_axi_adc_lease *sc_lease;
 int sc_session_lock,sc_session_cv,sc_stream_sel,sc_stream_sel_lock;
 bool sc_session_open,sc_session_busy,sc_stream_active,sc_configured,sc_capture_pending;
 struct adi_axi_dmac_buffer *sc_stream_buffers[16];
 bool sc_stream_cancel;
 struct status sc_stream_status;
 struct adi_axi_dmac_buffer *sc_stream_current;
 unsigned char *sc_stream_data;
 size_t sc_stream_offset,sc_stream_valid;
 void *sc_bst,*sc_bsh;
};
struct uio { size_t uio_resid; unsigned char *data; };
static int next_error;
static unsigned submitted,waits,overflow,loss,partial_fault,freed,notified,recorded;
static int stop_error,alloc_error,wait_result;
static bool enforce_sel_lock,registered,cluster_ready,inject_irq;
static unsigned allocated;
static unsigned char allocation[64];
static struct adi_axi_adc_softc *fixture;
static struct adi_axi_dmac_buffer block;
static void adiaxiadc_stream_notify(void *);
static int adiaxiadc_dma_drain(void *,unsigned);
static void mutex_enter(int *p) { assert(*p==0); *p=1; }
static void mutex_exit(int *p) {
 assert(*p==1); *p=0;
 if (fixture && p==&fixture->sc_stream_sel_lock && inject_irq) {
  inject_irq=false;
  adiaxiadc_stream_notify(fixture);
 }
}
static bool mutex_owned(const int *p) { return *p==1; }
static void cv_broadcast(int *p) { assert(p==&fixture->sc_session_cv); }

int cv_timedwait(int *cv,int *lock,int ticks) {
 (void)cv; assert(*lock==1 && ticks==1); waits++;
 if (wait_result) { assert(waits==1); return wait_result; }
 fixture->sc_stream_cancel=true;return 0;
}
int cv_timedwait_sig(int *cv,int *lock,int ticks) { return cv_timedwait(cv,lock,ticks); }

static int adi_axi_dmac_guard_stream_next(void *g,struct adi_axi_dmac_buffer **b,struct adi_axi_dmac_stream_status *s) {
 assert(g);memset(s,0,sizeof(*s));s->discontinuities=loss;s->completed_blocks=1;s->completed_bytes=64;
 if (!next_error && b) *b=&block;
 return next_error;
}
static int adi_axi_dmac_guard_buffer_read(void *g,struct adi_axi_dmac_buffer *b,void *data,size_t size) {
 assert(g && b==&block && size==64);for(size_t i=0;i<size;i++)((unsigned char *)data)[i]=(unsigned char)i;return 0;
}
static int adi_axi_dmac_guard_stream_submit(void *g,struct adi_axi_dmac_buffer *b,size_t size) {
 assert(g && b==&block && size==64);submitted++;return 0;
}
static unsigned bus_space_read_4(void *a,void *b,unsigned reg) { (void)a;(void)b;assert(reg==ADC_DMA_STATUS);return overflow; }
static int uiomove(void *data,size_t size,struct uio *uio) {
 if(partial_fault && size>partial_fault)size=partial_fault;
 memcpy(uio->data,data,size);uio->uio_resid-=size;return partial_fault?EFAULT:0;
}
static int adi_axi_dmac_guard_stream_stop(void *g,unsigned ms) {
 assert(g && ms==1000);
 return stop_error ? stop_error : adiaxiadc_dma_drain(g,ms);
}
int adi_axi_adc_stop(struct adi_axi_adc_lease *l,unsigned ms) {
 assert(l && ms==1000 && !mutex_owned(&l->l_converter->c_lock));return 0;
}
static int adc_disable_channels(struct adi_axi_adc_lease *l) { assert(mutex_owned(&l->l_converter->c_lock));return 0; }
static int check_dmas(struct adi_axi_adc_lease *l) { assert(mutex_owned(&l->l_converter->c_lock));return 0; }
static int fixture_quiesce(void *cookie,unsigned ms) { struct adi_axi_adc_lease *l=cookie;assert(ms==1000 && mutex_owned(&l->l_converter->c_lock));return 0; }

static int adi_axi_dmac_guard_buffer_free(void *g,struct adi_axi_dmac_buffer *b) { assert(g && b);freed++;return 0; }
static void kmem_free(void *data,size_t n) { assert(data && n==64);freed++; }
static void selnotify(int *p,int events,int hint) {
 (void)events;(void)hint;assert(p==&fixture->sc_stream_sel);
 if(enforce_sel_lock)assert(fixture->sc_stream_sel_lock==1);
 assert(!registered || cluster_ready);notified++;
}
static void selrecord(struct lwp *l,int *p) {
 (void)l;assert(p==&fixture->sc_stream_sel);
 if(enforce_sel_lock)assert(fixture->sc_stream_sel_lock==1);
 if(!registered) {
  registered=true;cluster_ready=false;
  /* A hard IRQ arrives between sel_lwp and sel_cluster publication. */
  if(enforce_sel_lock)inject_irq=true;
  cluster_ready=true;
 }
 recorded++;
}
static struct adi_axi_adc_softc *adiaxiadc_cdev_lookup(dev_t dev) { return dev?NULL:fixture; }

static bool adiaxiadc_reserved_zero(const uint32_t *p,size_t n) { while(n--)if(*p++)return false;return true; }
static int adi_axi_adc_read_rx_status(struct adi_axi_adc_lease *l,struct adi_axi_rx_status *s) { assert(l);s->sample_rate_hz=2399999;return 0; }
static void *kmem_alloc(size_t n,int flags) { assert(n==64 && flags==KM_SLEEP);return allocation; }
static int adi_axi_dmac_guard_buffer_alloc(void *g,size_t n,struct adi_axi_dmac_buffer **b) { assert(g && n==64);if(alloc_error)return alloc_error;*b=&block;allocated++;return 0; }
static int adi_axi_adc_start(struct adi_axi_adc_lease *l) { assert(l);return 0; }
static int adi_axi_dmac_guard_stream_start(void *g) { assert(g);return 0; }
static int adi_axi_dmac_guard_stream_notify(void *g,void (*fn)(void *),void *cookie) { assert(g && fn && cookie);return 0; }

static void bus_space_write_4(void *a,void *b,unsigned reg,unsigned value) { (void)a;(void)b;assert(reg==ADC_DMA_STATUS && value==4);overflow=0; }
#include "stream.inc"
int main(int argc,char **argv) {
 assert(argc==2);
 (void)quiesce_locked;
 struct converter converter={0};
 const struct capture_ops ops={.quiesce=fixture_quiesce};
 struct adi_axi_adc_lease l={.l_capture_guard=&l,.l_converter=&converter,.l_ops=&ops,.l_cookie=&l};
 unsigned char copy[64],data[64];
 struct adi_axi_adc_softc sc={.sc_lease=&l,.sc_stream_data=copy,.sc_stream_status={.block_bytes=64}};
 struct uio u={.uio_resid=7,.data=data};fixture=&sc;
 if(strcmp(argv[1],"signal")==0) {
  next_error=EAGAIN;wait_result=EINTR;u.uio_resid=64;
  assert(adiaxiadc_stream_read(&sc,&u,0)==EINTR && u.uio_resid==64 && waits==1);
  waits=0;wait_result=ERESTART;
  assert(adiaxiadc_stream_read(&sc,&u,0)==ERESTART && u.uio_resid==64 && waits==1);
  puts("PASS actual blocking stream read propagates EINTR/ERESTART without consuming bytes");return 0;
 }
 if(strcmp(argv[1],"selection")==0) {
  enforce_sel_lock=true;sc.sc_session_open=true;sc.sc_stream_active=true;
  assert(adiaxiadc_cdev_poll(0,POLLIN,NULL)==POLLIN && notified>0);
  assert(!inject_irq && cluster_ready);
  unsigned quiet_notifications=notified;next_error=EAGAIN;
  assert(adiaxiadc_cdev_poll(0,POLLIN,NULL)==0);
  assert(notified==quiet_notifications); /* Empty poll must not wake itself. */
  sc.sc_stream_data=NULL;sc.sc_stream_cancel=true;
  assert(adiaxiadc_stream_stop(&sc)==0);
  registered=false;cluster_ready=false;adiaxiadc_stream_notify(&sc);
  puts("PASS first selector publication, deferred IRQ, stop and selector removal share one interlock");return 0;
 }
 if(strcmp(argv[1],"busy")==0) {
  sc.sc_session_open=true;sc.sc_session_busy=true;sc.sc_stream_active=true;
  sc.sc_stream_valid=64;sc.sc_stream_offset=7;
  assert(adiaxiadc_cdev_poll(0,POLLIN,NULL)==0);
  unsigned before=notified;adiaxiadc_session_release(&sc);
  assert(notified>before && !sc.sc_session_busy);
  assert(adiaxiadc_cdev_poll(0,POLLIN,NULL)==POLLIN);
  puts("PASS busy release wakes registered pollers for a partial block with no future IRQ");return 0;
 }
 if(strcmp(argv[1],"stop")==0) {
  sc.sc_stream_active=true;
  assert(adiaxiadc_stream_stop(&sc)==0 && !converter.c_lock && l.l_quiet);
  puts("PASS stream stop calls the actual bound DMA drain under converter ownership");return 0;
 }

 assert(adiaxiadc_stream_read(&sc,&u,0)==0 && u.uio_resid==0 && submitted==0);
 assert(sc.sc_stream_offset==7 && memcmp(data,copy,7)==0);
 u.uio_resid=57;
 assert(adiaxiadc_stream_read(&sc,&u,0)==0 && submitted==1 && sc.sc_stream_valid==0);
 assert(data[0]==7 && data[56]==63);
 next_error=EAGAIN;u.uio_resid=64;
 assert(adiaxiadc_stream_read(&sc,&u,IO_NDELAY)==EAGAIN && waits==0);
 assert(adiaxiadc_stream_read(&sc,&u,0)==ECANCELED && waits==1);
 sc.sc_stream_cancel=false;next_error=0;loss=1;
 assert(adiaxiadc_stream_read(&sc,&u,0)==EOVERFLOW);
 loss=0;
 assert(adiaxiadc_stream_read(&sc,&u,0)==EOVERFLOW && submitted==1);
 sc.sc_stream_status.last_error=0;overflow=4;
 assert(adiaxiadc_stream_read(&sc,&u,0)==EOVERFLOW);
 overflow=0;sc.sc_stream_status.last_error=0;partial_fault=5;
 assert(adiaxiadc_stream_read(&sc,&u,0)==EFAULT && sc.sc_stream_offset==5);
 partial_fault=0;u.uio_resid=59;
 assert(adiaxiadc_stream_read(&sc,&u,0)==0 && submitted==2);
 sc.sc_session_open=true;sc.sc_stream_active=true;sc.sc_stream_valid=0;
 assert(adiaxiadc_cdev_poll(0,POLLIN,NULL)==POLLIN && recorded==1);
 next_error=EAGAIN;
 assert(adiaxiadc_cdev_poll(0,POLLIN,NULL)==0 && recorded==2);
 sc.sc_stream_cancel=true;
 assert(adiaxiadc_cdev_poll(0,POLLIN,NULL)==POLLHUP);
 sc.sc_stream_cancel=false;sc.sc_stream_status.last_error=EOVERFLOW;
 assert(adiaxiadc_cdev_poll(0,POLLIN,NULL)==POLLERR);
 sc.sc_stream_buffers[0]=&block;stop_error=ETIMEDOUT;
 assert(adiaxiadc_stream_stop(&sc)==ETIMEDOUT && freed==0 && sc.sc_stream_active);
 assert(sc.sc_stream_buffers[0]==&block && sc.sc_stream_data==copy);
 stop_error=0;
 assert(adiaxiadc_stream_stop(&sc)==0 && freed==2 && !sc.sc_stream_active);
 assert(sc.sc_stream_buffers[0]==NULL && sc.sc_stream_data==NULL && notified>0);
 sc.sc_configured=true;
 struct adi_axi_adc_stream_config config={.abi_version=2,.struct_size=sizeof(config),.block_bytes=64,.block_count=2};
 assert(adiaxiadc_stream_start(&sc,&config)==EINVAL);
 config.block_count=3;config.reserved[0]=1;
 assert(adiaxiadc_stream_start(&sc,&config)==EINVAL);
 config.reserved[0]=0;
 assert(adiaxiadc_stream_start(&sc,&config)==0 && sc.sc_stream_active && allocated==3);
 assert(sc.sc_stream_status.generation==1 && sc.sc_stream_status.sample_rate_hz==2399999);
 assert(adiaxiadc_stream_start(&sc,&config)==EBUSY);
 assert(adiaxiadc_stream_stop(&sc)==0);
 alloc_error=ENOMEM;
 assert(adiaxiadc_stream_start(&sc,&config)==ENOMEM && !sc.sc_stream_active && sc.sc_stream_data==NULL);
 assert(sc.sc_stream_status.generation==2);
 puts("PASS stream bounded allocation, actual clock, restart generation and allocation unwind");
 puts("PASS stream poll readiness/cancel/error and failed-drain retained ownership");
 puts("PASS stream partial reads, EAGAIN, bounded cancel, sticky loss and partial uio fault");
}
