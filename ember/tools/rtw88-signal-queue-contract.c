/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual callback and softintr; allocation/stack dispatch is a bounded service.
 * The independent scan contract executes the actual consumer of the quality. */
#include "signal-routing-model.inc"
#undef ifp
#define if_ierrors 3
static struct mbuf *rtw88_net_rx_mbuf(const uint8_t *frame,size_t len,struct ifnet *ifp) {
    (void)ifp;struct mbuf *m=newbuf(len);if(m)memcpy(m->m_data,frame,len);return m;
}
#include "signal-native-rx.inc"
int rtw88_signal_scan_check(unsigned,unsigned);
int main(void) {
    struct rtw88_softc sc={.sc_generation=7};kmutex_t lock={0};
    struct rtw88_net net={.core=&sc,.lock=&lock};struct ieee80211_node ni={0};
    struct rtw88_rx_info rx={.signal={.generation=7,.valid=1,.quality=50}};
    uint8_t frame[24]={8,2};unsigned observed[2];
    net.ic.ic_bss=&ni;net.ic.ic_ifp=&net.ec.ec_if;net.ec.ec_if.if_softc=&net;
    net.policy.epoch=4;net.policy.accepting=1;node_refs=1;
    for(unsigned i=0;i<2;i++) {
        rx.signal.quality=(uint8_t)(i?80:50);
        mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,24,&rx);mutex_exit(&lock);
        memset(&rx,0,sizeof(rx)); /* Borrowed producer storage can change now. */
        rtw88_net_softintr(&net);observed[i]=route_last_rssi;
        rx.signal.generation=7;rx.signal.valid=1;
    }
    CHECK(observed[0]==50 && observed[1]==80);
    CHECK(rtw88_signal_scan_check(observed[0],observed[1])==0);
    /* The actual callback must attach unknown metadata to this frame only. */
    rx.signal.valid=0;mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,24,&rx);mutex_exit(&lock);
    rtw88_net_softintr(&net);CHECK(route_last_rssi==0);
    unsigned delivered=route_rx;
    mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,24,&rx);mutex_exit(&lock);
    sc.sc_generation++;rtw88_net_softintr(&net);CHECK(route_rx==delivered);
    CHECK(net.stats.fw_stale==1 && net.policy.queued==0);
    /* A stale signal observation does not invalidate its current DATA owner. */
    rx.signal.valid=1;rx.signal.generation=7;rx.signal.quality=90;
    mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,24,&rx);mutex_exit(&lock);
    rtw88_net_softintr(&net);CHECK(route_rx==delivered+1 && route_last_rssi==0);
    /* Epoch withdrawal still fences queued data independently of firmware age. */
    mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,24,&rx);
    (void)rtw88_net_request(&net.policy);mutex_exit(&lock);
    delivered=route_rx;rtw88_net_softintr(&net);CHECK(route_rx==delivered && net.policy.queued==0);
    printf("rtw88 signal queue: %u checks, %u failures\n",checks,failures);return failures!=0;
}
