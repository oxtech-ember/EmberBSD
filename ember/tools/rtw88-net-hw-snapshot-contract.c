/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* Numeric snapshots must preserve PCI DMA and SDIO no-DMA readiness gates. */
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include "net-hw-layout.inc"
struct rtw88_net { struct rtw88_softc *core; kmutex_t *lock; struct rtw88_net_hw_snapshot hw; };
static int rtw88_executor_owned(struct rtw88_softc *s){(void)s;return 1;}
void mutex_enter(kmutex_t *m){assert(!m->held);m->held=1;}
void mutex_exit(kmutex_t *m){assert(m->held);m->held=0;}
#include "net-hw-capture.inc"
#include "net-hw-update.inc"
int main(void)
{
 struct rtw88_softc sc={0}; struct rtw88_hci_ops hci={0};
 kmutex_t lock={0}; struct rtw88_net net={.core=&sc,.lock=&lock}; struct rtw88_net_hw_snapshot hw;
 sc.sc_hci=&hci; sc.sc_resources_ready=sc.sc_powered=1; sc.sc_generation=3;
 rtw88_net_hw_capture(&net,&hw);
 assert(!hw.power_ready && hw.generation==3);
 sc.sc_dma_master=1; rtw88_net_hw_capture(&net,&hw); assert(hw.power_ready);
 sc.sc_dma_master=0; hci.no_host_dma=true;
 rtw88_net_hw_capture(&net,&hw); assert(hw.power_ready);
 sc.sc_power_error=EIO; rtw88_net_hw_capture(&net,&hw); assert(!hw.power_ready);
 sc.sc_power_error=0; rtw88_net_hw_update(&net); assert(!lock.held && net.hw.power_ready);
 sc.sc_powered=0; assert(net.hw.power_ready);
 return 0;
}
