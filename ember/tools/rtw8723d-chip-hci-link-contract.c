/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* Host link and explicit legacy capabilities; no bus, netif or RF claim. */
#include <assert.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8723d_chip.h>
#include <dev/sdmmc/rtw88_sdio_hci.h>
#include <dev/sdmmc/rtw88_sdio_io.h>
static int owner=1, release_error=EIO;
static unsigned acquisitions, releases;
static uint8_t tx_buffer[4096], rx_buffer[16384], rx_scratch[16384];
static struct rtw88_sdio_rx_batch rx_batch;
int rtw88_executor_owned(struct rtw88_softc *sc){(void)sc; return owner;}
static int acquire(void *p){struct rtw88_sdio_bus *b=p; acquisitions++; b->function_claimed=true; return EIO;}
static int release(void *p){struct rtw88_sdio_bus *b=p; releases++; if (release_error) return release_error; b->function_claimed=b->function_ready=false; return 0;}
static int okay(void *p){(void)p; return 0;}
static bool no(void *p){(void)p;return false;}
static bool yes(void *p){(void)p;return true;}
static int epoch(void *p,struct rtw88_async_epoch *id){(void)p;(void)id;return 0;}
static int current(void *p,const struct rtw88_async_epoch *id){(void)p;(void)id;return 0;}
static void reset(void *p){(void)p;}
static void again(void *p,bool v){(void)p;(void)v;}
static void fault(void *p,int e){(void)p;(void)e;}
static void defer(struct rtw88_softc *s){(void)s;}
static void
power_admission(void)
{
 struct rtw8723d_efuse efuse={0};
 struct rtw88_profile profile={0};
 struct rtw8723d_power_policy policy={.rf_paths=1};
 uint8_t power[RTW8723D_POWER_RATES], before[RTW8723D_POWER_RATES];
 memset(efuse.txpower,0xff,sizeof(efuse.txpower));
 memset(power,0xa5,sizeof(power)); memcpy(before,power,sizeof(power));
 profile.tx_admitted=true; profile.domain=RTW88_REG_WORLD_MIN; profile.channels_2g=1;
 assert(rtw8723d_power_compute(&efuse,&profile,&policy,1,power)==ENODATA);
 assert(memcmp(power,before,sizeof(power))==0);
 profile.tx_admitted=false;
 assert(rtw8723d_power_compute(&efuse,&profile,&policy,1,power)==EACCES);
 assert(memcmp(power,before,sizeof(power))==0);
}

static void lifecycle(void)
{
 struct rtw88_softc sc={.sc_defer_irq=defer};
 struct rtw88_sdio_bus bus={0};
 struct rtw88_sdio_tx_context tx={.bus=&bus,.packet=tx_buffer,.packet_cap=sizeof(tx_buffer)};
 struct rtw88_sdio_rx_context rx={.bus=&bus,.packet=rx_buffer,.packet_cap=sizeof(rx_buffer),.scratch=rx_scratch,.scratch_cap=sizeof(rx_scratch),.batch=&rx_batch};
 struct rtw88_sdio_hci hci={0};
 struct rtw88_sdio_hci_parent parent={.acquire=acquire,.release=release,.irq_start=okay,.status=okay,.revoked=no,.rx_epoch=epoch,.rx_current=current,.retry_ready=yes,.retry_reset=reset,.again=again,.fault=fault,.cookie=&bus};
 assert(rtw88_sdio_hci_bind(&hci,&sc,&bus,&tx,&rx,&parent)==0);
 assert(sc.sc_hci->attach(&sc)==0);
 owner=0; assert(sc.sc_hci->prepare(&sc)==EPERM && acquisitions==0);
 owner=1; assert(sc.sc_hci->prepare(&sc)==EIO);
 assert(hci.acquired && bus.function_claimed && !sc.sc_resources_ready);
 assert(sc.sc_hci->detach(&sc)==EBUSY);
 assert(sc.sc_hci->quiesce(&sc)==0);
 assert(sc.sc_hci->stop(&sc)==EIO && hci.acquired && releases==1);
 release_error=0; bus.fifo_failed=true;
 assert(sc.sc_hci->stop(&sc)==EIO && hci.acquired && releases==1);
 /* Model only: an external owner has explicitly resynchronised the bus. */
 bus.fifo_failed=false;
 assert(sc.sc_hci->stop(&sc)==0 && !hci.acquired && releases==2);
 assert(sc.sc_hci->detach(&sc)==0);
}

int main(void)
{
 struct rtw88_softc sc={0};
 struct rtw88_caps caps={0};
 sc.sc_chip=&rtw8723d_chip_ops; sc.sc_hci=&rtw88_sdio_hci_ops;
 assert(rtw88_get_caps(&sc,&caps)==0);
 assert(caps.bands==RTW88_BAND_2G && caps.widths==RTW88_WIDTH_20);
 assert(caps.ht_mcs==0 && (caps.features&RTW88_CAP_WPA2) &&
     !(caps.features&RTW88_CAP_WME));
 lifecycle();
 power_admission();
 assert(sc.sc_hci->no_host_dma && sc.sc_hci->write_window && sc.sc_hci->set_data_mode);
 return 0;
}
