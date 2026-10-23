/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: native queued RX metadata and AP clock contract, AI-assisted. */
/* Actual callback and softintr; allocation/stack dispatch is a bounded service.
 * The independent scan contract executes the actual consumer of the quality. */
#include "signal-routing-model.inc"
#undef ifp
#define if_ierrors 3
static struct mbuf *rtw88_net_rx_mbuf(const uint8_t *frame,size_t len,struct ifnet *ifp) {
    (void)ifp;struct mbuf *m=newbuf(len);if(m)memcpy(m->m_data,frame,len);return m;
}
/* Match the kernel clock namespace at the actual RX callback boundary. */
extern int tick;
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wshadow"
#include "signal-native-rx.inc"
#pragma GCC diagnostic pop
int rtw88_signal_scan_check(unsigned,unsigned);

static void beacon_arrival_clock(void) {
    static const uint32_t start[]={100,100,UINT32_MAX-20,100};
    static const uint32_t delay[]={900,1,41,900};
    for(unsigned kind=0;kind<4;kind++) {
        struct rtw88_softc sc={.sc_generation=7};kmutex_t lock={0};
        struct rtw88_net net={.core=&sc,.lock=&lock};
        struct ieee80211_node ni={0};uint8_t frame[41]={0x80};
        struct rtw88_rx_info info={.packet_size=sizeof(frame)};
        memset(ni.ni_bssid,0x22,6);memcpy(frame+10,ni.ni_bssid,6);
        memcpy(frame+16,ni.ni_bssid,6);memset(frame+4,0xff,6);
        frame[32]=100;frame[34]=1;frame[36]=0;frame[37]=0;
        frame[38]=1;frame[39]=1;frame[40]=12;
        net.ic.ic_bss=&ni;net.ic.ic_ifp=&net.ec.ec_if;
        net.ec.ec_if.if_softc=&net;net.ic.ic_state=IEEE80211_S_RUN;
        net.policy.epoch=4;net.policy.accepting=1;route_ticks=start[kind];
        CHECK(rtw88_net_beacon_start(&net.policy,4,route_ticks)==0);
        mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,sizeof(frame),&info);
        mutex_exit(&lock);CHECK(net.policy.queued==1);
        route_ticks+=delay[kind];
        if(kind==3)CHECK(rtw88_net_beacon_poll(&net.policy,4,route_ticks,40)==1);
        rtw88_net_softintr(&net);
        CHECK(net.policy.queued==0 && node_refs==1);
        CHECK(net.policy.beacon_tick==start[kind]);
        if(kind==3)CHECK(net.policy.beacon_report_tick==route_ticks);
        CHECK(rtw88_net_beacon_poll(&net.policy,4,route_ticks,40)==(kind==0 || kind==2));
    }
}
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
    beacon_arrival_clock();
    printf("rtw88 signal queue: %u checks, %u failures\n",checks,failures);return failures!=0;
}
