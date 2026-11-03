/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
#include "rtw88-sdio-rx-fault-fixture.h"
#include <assert.h>
static unsigned direct,extended;
static int command52(void *v,uint32_t a,uint32_t *r){(void)v;(void)a;direct++;*r=0;return 0;}
static int command53(void *v,uint32_t a,void *p,size_t n,uint32_t *r){(void)v;(void)a;(void)p;(void)n;extended++;*r=0;return 0;}
static int desc(void){return 0;}
int rtw88_executor_owned(struct rtw88_softc *s){(void)s;return 1;}
int rtw88_sdio_fifo_read(struct rtw88_sdio_bus *b,uint8_t *c,void *p,size_t n,uint8_t *s,size_t z){(void)b;(void)c;(void)p;(void)n;(void)s;(void)z;return 0;}
int rtw88_sdio_rx_validate(struct rtw88_softc *s,const uint8_t *p,size_t n,struct rtw88_sdio_rx_batch *b){(void)s;(void)p;(void)n;(void)b;return EBADMSG;}
int main(void)
{
 struct rtw88_sdio_ops ops={.command52=command52,.command53=command53};
 uint8_t packet[64],scratch[64],out=0, stage[64];
 struct rtw88_sdio_bus bus={.ops=&ops,.block_size=512,.max_request=512,.read_scratch=stage,.read_scratch_len=sizeof(stage),.function_claimed=true,.function_ready=true,.cmd53_enabled=true};
 struct rtw88_sdio_rx_batch batch={0};
 struct rtw88_sdio_rx_context rx={.bus=&bus,.packet=packet,.scratch=scratch,.packet_cap=sizeof(packet),.scratch_cap=sizeof(scratch),.batch=&batch};
 const struct rtw88_chip_ops chip={.caps={.rx_desc_size=24},.rx_desc=desc};
 struct rtw88_softc sc={.sc_chip=&chip,.sc_generation=1,.sc_resources_ready=true,.sc_powered=true};
 uint64_t frontier=99;
 assert(rtw88_sdio_rx_drain(&rx,&sc,24,1,&frontier)==EBADMSG);
 assert(bus.fifo_failed && !bus.cmd53_enabled && frontier==99);
 /* Even an inconsistent caller cannot route through CMD53 while latched. */
 bus.cmd53_enabled=true;
 assert(rtw88_sdio_transfer(&bus,RTW88_SDIO_LOCAL,0,&out,1,false)==0);
 assert(direct==1 && extended==0);
 assert(rtw88_sdio_set_cmd53(&bus,true)==EIO);
 return 0;
}
