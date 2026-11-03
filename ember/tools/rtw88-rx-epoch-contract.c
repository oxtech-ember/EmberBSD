/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* Host RX identity survives classification; no device or network is used. */
#include <dev/ic/rtw88_async.h>
#define main rtw88_start_contract_main
#include "rtw88-start-contract.c"
#undef main
static int owned=1;
static unsigned deliveries;
static struct rtw88_rx_info received;
int rtw88_executor_owned(struct rtw88_softc *sc){(void)sc;return owned;}
static void capture(void *cookie,const uint8_t *data,size_t len,const struct rtw88_rx_info *info)
{(void)cookie;(void)data;(void)len;received=*info;deliveries++;}
int main(void)
{
 struct rtw88_softc sc;
 struct rtw88_async_epoch id;
 uint8_t frame[28]={0x08};
 if (!start_ok(&sc)) return EXIT_FAILURE;
 sc.sc_executor=(void *)&sc; sc.sc_rx_frame=capture;
 id=(struct rtw88_async_epoch){.generation=sc.sc_generation,.epoch=7,.key_epoch=9};
 rx_put(&sc,0,sizeof(frame),0,frame,0);
 CHECK(rtw88_rx_deliver_epoch(&sc,sc.sc_rxbuf[0].vaddr,52,1,&id)==0);
 CHECK(deliveries==1 && received.host_epoch_valid && received.generation==id.generation && received.epoch==7 && received.key_epoch==9);
 id.generation++;
 CHECK(rtw88_rx_deliver_epoch(&sc,sc.sc_rxbuf[0].vaddr,52,2,&id)==ESTALE);
 CHECK(deliveries==1);
 id.generation--;
 owned=0;
 CHECK(rtw88_rx_deliver_epoch(&sc,sc.sc_rxbuf[0].vaddr,52,2,&id)==EPERM);
 CHECK(deliveries==1);
 owned=1;
 rtw88_rx_deliver(&sc,sc.sc_rxbuf[0].vaddr,52,3);
 CHECK(deliveries==2 && !received.host_epoch_valid);
 sc.sc_executor=NULL; sc.sc_rx_frame=NULL;
 CHECK(rtw88_stop(&sc)==0); CHECK(rtw88_core_detach(&sc)==0);
 return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
