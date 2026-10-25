/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual CONFIG must initialize clocks only after positive DMA admission. */
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned long u_long;
struct lwp { int unused; };
#define _IO(a,b) (b)
#define _IOW(a,b,c) (b)
#define _IOWR(a,b,c) (b)
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
#define KM_SLEEP 0
#include "abi.inc"
struct adi_axi_rx_config {
 uint64_t frequency_hz;
 uint32_t sample_rate_hz,bandwidth_hz,rf_port,rx_mask,channel_mask;
 int32_t gain_mdB[2]; unsigned gain_mode[2];
};
struct adi_axi_rx_status { uint32_t sample_rate_hz,valid_mask; int32_t rssi_mdB[2],gain_mdB[2]; };
struct adi_axi_adc_lease;
struct converter_ops { int (*bootstrap)(void *,const struct adi_axi_rx_config *); };
struct capture_ops { int (*cold_bootstrap)(void *); };
struct adi_axi_converter {
 const struct converter_ops *c_ops; void *c_cookie; int c_lock;
 bool c_bootstrap_used;
};
struct adi_axi_adc_lease {
 struct adi_axi_converter *l_converter;
 const struct capture_ops *l_ops; void *l_cookie;
 bool l_poisoned,l_running,l_quiet,l_configured;
 unsigned l_nguards; void *l_guards[8],*l_capture_guard;
};
struct adi_axi_adc_softc {
 int sc_session_lock,sc_session_cv;void *sc_bst,*sc_bsh;
 bool sc_session_busy,sc_session_open,sc_stream_cancel,sc_stream_active;
 void *sc_stream_data;
 struct adi_axi_adc_stream_status sc_stream_status;
 struct adi_axi_adc_lease *sc_lease;
 bool sc_capture_pending,sc_capture_valid,sc_configured;
 struct adi_axi_rx_config sc_config;
 size_t sc_capture_offset,sc_capture_bytes;
 void *sc_capture_buffer,*sc_capture_data;
 unsigned sc_capture_samples,sc_capture_timeout; int sc_capture_error;
};
static struct adi_axi_adc_softc adc;
static unsigned drains,boots,configures,retunes;
static int drain_error,measurement_error;
static bool initialized;
static void mutex_enter(int *p) { (void)p; }
static void mutex_exit(int *p) { (void)p; }
static bool rx_config_valid(const struct adi_axi_rx_config *c) { return c!=NULL; }
static int quiesce_locked(struct adi_axi_adc_lease *l,unsigned ms) {
 assert(ms==1000); drains++; if(drain_error)return drain_error; l->l_quiet=true; return 0;
}
static int check_dmas(struct adi_axi_adc_lease *l) { (void)l; return 0; }
static int adi_axi_dmac_guard_cold_check(void *g) { (void)g; return 0; }
static void poison_locked(struct adi_axi_adc_lease *l) { l->l_poisoned=true; }
static int native_bootstrap(void *cookie,const struct adi_axi_rx_config *c) {
 (void)cookie; assert(c->rx_mask==1); assert(drains==1); boots++; initialized=true; return 0;
}
static struct adi_axi_adc_softc *adiaxiadc_cdev_lookup(dev_t d) { assert(d==0); return &adc; }
static int adiaxiadc_session_claim(struct adi_axi_adc_softc *sc) { assert(sc==&adc);return 0; }
static void adiaxiadc_session_release(struct adi_axi_adc_softc *sc) { assert(sc==&adc);sc->sc_session_busy=false; }
static bool adiaxiadc_reserved_zero(const uint32_t *p,size_t n) { while(n--)if(*p++)return false;return true; }
static int adi_axi_dmac_guard_buffer_free(void *g,void *p) { (void)g;free(p);return 0; }
static int adi_axi_dmac_guard_buffer_alloc(void *g,size_t n,void **p) { (void)g;*p=malloc(n);assert(*p);return 0; }
static void *kmem_alloc(size_t n,int flags) { (void)flags;void *p=malloc(n);assert(p);return p; }
static void kmem_free(void *p,size_t n) { (void)n;free(p); }
static int adi_axi_adc_configure(struct adi_axi_adc_lease *l,const struct adi_axi_rx_config *c,unsigned ms) {
 (void)c;assert(ms==1000);if(!initialized)return ENXIO;l->l_quiet=true;configures++;return 0;
}
int adi_axi_adc_retune_rx(struct adi_axi_adc_lease *l,uint64_t frequency);
int adi_axi_adc_retune_rx(struct adi_axi_adc_lease *l,uint64_t frequency) {
 assert(l->l_quiet && initialized && frequency==918000000);retunes++;return 0;
}
static int adi_axi_adc_read_rx_status(struct adi_axi_adc_lease *l,struct adi_axi_rx_status *s) { (void)l;if(measurement_error)return measurement_error;s->sample_rate_hz=2399999;return 0; }
static void cv_wait(int *cv,int *lock) { (void)cv;(void)lock;assert(0); }
static void cv_broadcast(int *cv) { (void)cv; }
static int adiaxiadc_stream_start(struct adi_axi_adc_softc *sc,const void *data) { (void)sc;(void)data;return EOPNOTSUPP; }
static int adiaxiadc_stream_stop(struct adi_axi_adc_softc *sc) { (void)sc;return 0; }
#define ADC_DMA_STATUS 0x88
struct adi_axi_dmac_stream_status { uint64_t completed_blocks,completed_bytes,discontinuities;unsigned ready; };
static int adi_axi_dmac_guard_stream_next(void *g,void *b,struct adi_axi_dmac_stream_status *s) { (void)g;(void)b;memset(s,0,sizeof(*s));return EAGAIN; }
static unsigned bus_space_read_4(void *a,void *b,unsigned reg) { (void)a;(void)b;(void)reg;return 0; }
static int adiaxiadc_stream_loss(struct adi_axi_adc_softc *sc,const struct adi_axi_dmac_stream_status *s,uint32_t raw) { (void)sc;(void)s;(void)raw;assert(0 && "unexpected CONFIG test loss");return EOVERFLOW; }
#include "bootstrap.inc"
int main(void)
{
 const struct converter_ops ops={.bootstrap=native_bootstrap};
 const struct capture_ops capture={0};
 struct adi_axi_converter converter={.c_ops=&ops};
 struct adi_axi_adc_lease lease={.l_converter=&converter,.l_ops=&capture,.l_nguards=1};
 struct adi_axi_adc_config config={.abi_version=1,.struct_size=sizeof(config),
  .frequency_hz=915000000,.sample_rate_hz=2400000,.bandwidth_hz=8000000,
  .rx_mask=1,.channel_mask=3,.timeout_ms=1000};
 struct adi_axi_rx_config cfg={.rx_mask=1};
 adc.sc_lease=&lease;adc.sc_session_open=true;
 assert(adiaxiadc_cdev_ioctl(0,ADI_AXI_ADCIOC_CONFIG,&config,0,NULL)==0);
 assert(initialized && adc.sc_configured && boots==1 && drains==2 && configures==1);
 struct adi_axi_adc_stream_status measured={.abi_version=2,.struct_size=sizeof(measured)};
 assert(!adc.sc_stream_active);
 assert(adiaxiadc_cdev_ioctl(0,ADI_AXI_ADCIOC_STREAM_STATUS,&measured,0,NULL)==0);
 assert(measured.sample_rate_hz==2399999 && measured.sample_rate_hz!=config.sample_rate_hz);
 puts("PASS CONFIG exposes measured sampling_frequency before STREAM_START");
 config.frequency_hz=918000000;
 assert(adiaxiadc_cdev_ioctl(0,ADI_AXI_ADCIOC_CONFIG,&config,0,NULL)==0);
 assert(boots==1 && configures==1 && retunes==1);
 config.sample_rate_hz=12000000;
 assert(adiaxiadc_cdev_ioctl(0,ADI_AXI_ADCIOC_CONFIG,&config,0,NULL)==0);
 assert(boots==1 && configures==2 && retunes==1);
 /* A new lease reuses initialized provider clocks, not a new cold proof. */
 lease.l_quiet=false;adc.sc_configured=false;
 assert(adiaxiadc_cdev_ioctl(0,ADI_AXI_ADCIOC_CONFIG,&config,0,NULL)==0);
 assert(boots==1 && configures==3);
 measurement_error=EOPNOTSUPP;
 assert(adiaxiadc_cdev_ioctl(0,ADI_AXI_ADCIOC_CONFIG,&config,0,NULL)==0);
 assert(adiaxiadc_cdev_ioctl(0,ADI_AXI_ADCIOC_STREAM_STATUS,&measured,0,NULL)==0);
 assert(measured.sample_rate_hz==0);
 measurement_error=EIO;
 assert(adiaxiadc_cdev_ioctl(0,ADI_AXI_ADCIOC_CONFIG,&config,0,NULL)==EIO);
 assert(!adc.sc_configured && adc.sc_stream_status.sample_rate_hz==0);
 measurement_error=0;
 puts("PASS unavailable clock reports zero, failed clock readback refuses CONFIG success");
 converter.c_bootstrap_used=false;lease.l_quiet=false;lease.l_poisoned=false;
 drains=boots=0;initialized=false;drain_error=ETIMEDOUT;
 assert(adi_axi_adc_bootstrap(&lease,&cfg,1000)==ETIMEDOUT);
 assert(boots==0 && !initialized && lease.l_poisoned && converter.c_bootstrap_used);
 puts("PASS first CONFIG bootstraps after hardware drain, repeats reuse clocks, failed admission never initializes");
 return 0;
}
