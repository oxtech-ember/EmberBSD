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
        net.ic.ic_opmode=IEEE80211_M_STA;ni.ni_associd=1;
        memcpy(ni.ni_macaddr,ni.ni_bssid,6);
        net.ic.ic_channels[1].ic_freq=2412;
        ni.ni_chan=net.ic.ic_curchan=&net.ic.ic_channels[1];
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

static void beacon_body_clock(void) {
    enum { VALID, NO_SSID, NO_RATES, TRUNCATED_RATES, INTERVAL_ZERO,
        INTERVAL_SHORT, INTERVAL_LONG, WRONG_TA, OFF_CHANNEL_DS,
        CURRENT_DS, FOREIGN_NODE_CHANNEL, INTERVAL_MIN, INTERVAL_MAX,
        PROTECTED, ORDERED, MORE_FRAGMENTS, FRAGMENT_ONE, WRONG_BSSID,
        UNICAST_DA, NON_STA, NON_RUN, UNASSOCIATED, CSA, ECSA, QUIET,
        NULL_CHANNEL, ANY_CHANNEL, CHAINED_MBUF, SHORT_FIRST_MBUF };
    static const struct { unsigned kind, interval, accepted; } cases[]={
        {VALID,100,1}, {NO_SSID,100,0}, {NO_RATES,100,0},
        {TRUNCATED_RATES,100,0}, {INTERVAL_ZERO,0,0},
        {INTERVAL_SHORT,24,0}, {INTERVAL_LONG,1001,0},
        {WRONG_TA,100,0}, {OFF_CHANNEL_DS,100,0},
        {CURRENT_DS,100,1}, {FOREIGN_NODE_CHANNEL,100,0},
        {INTERVAL_MIN,25,1}, {INTERVAL_MAX,1000,1},
        {PROTECTED,100,0}, {ORDERED,100,0}, {MORE_FRAGMENTS,100,0},
        {FRAGMENT_ONE,100,0}, {WRONG_BSSID,100,0}, {UNICAST_DA,100,0},
        {NON_STA,100,0}, {NON_RUN,100,0}, {UNASSOCIATED,100,0},
        {CSA,100,0}, {ECSA,100,0}, {QUIET,100,0},
        {NULL_CHANNEL,100,0}, {ANY_CHANNEL,100,0},
        {CHAINED_MBUF,100,0}, {SHORT_FIRST_MBUF,100,0}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        struct rtw88_softc sc={.sc_generation=7};kmutex_t lock={0};
        struct rtw88_net net={.core=&sc,.lock=&lock};
        struct ieee80211_node ni={0};uint8_t frame[51]={0x80};
        size_t len=41;
        memset(ni.ni_bssid,0x22,6);memcpy(ni.ni_macaddr,ni.ni_bssid,6);
        memcpy(frame+10,ni.ni_bssid,6);memcpy(frame+16,ni.ni_bssid,6);
        memset(frame+4,0xff,6);
        frame[32]=(uint8_t)cases[i].interval;
        frame[33]=(uint8_t)(cases[i].interval>>8);frame[34]=1;
        frame[36]=0;frame[37]=0;frame[38]=1;frame[39]=1;frame[40]=12;
        net.ic.ic_bss=&ni;net.ic.ic_ifp=&net.ec.ec_if;
        net.ic.ic_opmode=IEEE80211_M_STA;net.ic.ic_state=IEEE80211_S_RUN;
        net.ic.ic_channels[1].ic_freq=2412;
        net.ic.ic_channels[6].ic_freq=2437;
        ni.ni_chan=net.ic.ic_curchan=&net.ic.ic_channels[1];ni.ni_associd=1;
        net.ec.ec_if.if_softc=&net;
        switch(cases[i].kind) {
        case NO_SSID:frame[36]=255;break;
        case NO_RATES:frame[38]=255;break;
        case TRUNCATED_RATES:frame[39]=2;break;
        case WRONG_TA:frame[10]=0x24;break;
        case OFF_CHANNEL_DS:case CURRENT_DS:
            frame[41]=3;frame[42]=1;
            frame[43]=cases[i].kind==CURRENT_DS?1:64;len=44;break;
        case FOREIGN_NODE_CHANNEL:ni.ni_chan=&net.ic.ic_channels[6];break;
        case PROTECTED:frame[1]=0x40;break;
        case ORDERED:frame[1]=0x80;break;
        case MORE_FRAGMENTS:frame[1]=0x04;break;
        case FRAGMENT_ONE:frame[22]=1;break;
        case WRONG_BSSID:frame[16]=0x24;break;
        case UNICAST_DA:frame[4]=0x02;break;
        case NON_STA:net.ic.ic_opmode=IEEE80211_M_IBSS;break;
        case NON_RUN:net.ic.ic_state=IEEE80211_S_ASSOC;break;
        case UNASSOCIATED:ni.ni_associd=0;break;
        case CSA:
            frame[41]=37;frame[42]=3;frame[43]=1;
            frame[44]=64;frame[45]=1;len=46;break;
        case ECSA:
            frame[41]=60;frame[42]=4;frame[43]=1;
            frame[44]=115;frame[45]=64;frame[46]=1;len=47;break;
        case QUIET:
            frame[41]=40;frame[42]=6;frame[43]=1;
            frame[44]=1;frame[45]=1;len=49;break;
        case NULL_CHANNEL:net.ic.ic_curchan=NULL;break;
        case ANY_CHANNEL:net.ic.ic_curchan=IEEE80211_CHAN_ANYC;break;
        default:break;
        }
        struct rtw88_rx_info info={.packet_size=len};
        net.policy.epoch=4;net.policy.accepting=1;route_ticks=100;
        CHECK(rtw88_net_beacon_start(&net.policy,4,route_ticks)==0);
        route_ticks=101;
        mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,len,&info);
        mutex_exit(&lock);CHECK(net.policy.queued==1);
        /* Mutate only the queued mbuf, after the real IRQ callback owns it. */
        if(cases[i].kind==CHAINED_MBUF) {
            struct mbuf *first=net.rx[net.rx_head].m;
            first->m_next=newbuf(1);
            memcpy(first->m_next->m_data,frame+len-1,1);
            first->m_len--;
        } else if(cases[i].kind==SHORT_FIRST_MBUF) {
            net.rx[net.rx_head].m->m_len--;
        }
        route_ticks=102;rtw88_net_softintr(&net);
        CHECK(net.policy.queued==0 && node_refs==1);
        CHECK(net.policy.beacon_tick==(cases[i].accepted?101U:100U));
        /* At delivery, only the original clock has expired this deadline. */
        CHECK(rtw88_net_beacon_poll(&net.policy,4,route_ticks,2)==
            !cases[i].accepted);
    }
}
int main(void) {
    struct rtw88_softc sc={.sc_generation=7};kmutex_t lock={0};
    struct rtw88_net net={.core=&sc,.lock=&lock};struct ieee80211_node ni={0};
    struct rtw88_rx_info rx={0};
    uint8_t frame[24]={8,2};unsigned observed[2];
    uint8_t packet[24+8+24]={0};
    struct rtw88_phy_state phy={0};
    /* Serialized by the actual startup/retune fixture in this same runner.
     * This boundary models transport of producer state, not its publication. */
    const char *context=getenv("RTW88_SIGNAL_CONTEXT");
    FILE *file=context?fopen(context,"rb"):NULL;CHECK(file!=NULL);
    if(file==NULL)return 1;
    CHECK(fread(&phy,sizeof(phy),1,file)==1);CHECK(fclose(file)==0);
    CHECK(phy.channel_valid && phy.paths==3);
    unsigned generation=phy.generation;sc.sc_generation=generation;
    /* Eight-byte PHY storage; the real descriptor supplies the bounds. */
    packet[0]=24;packet[2]=1;packet[3]=4;packet[12]=0x2c;
    packet[24]=1;packet[25]=60;packet[26]=50;packet[29]=0;packet[30]=36;
    memcpy(packet+32,frame,24);
    net.ic.ic_bss=&ni;net.ic.ic_ifp=&net.ec.ec_if;net.ec.ec_if.if_softc=&net;
    net.policy.epoch=4;net.policy.accepting=1;node_refs=1;
    for(unsigned i=0;i<2;i++) {
        packet[25]=(uint8_t)(i?90:60);
        CHECK(rtw88_rx_decode(packet,sizeof(packet),&rx)==0);
        CHECK(rtw88_rx_signal_decode(packet+rx.driver_info_offset,
            rx.driver_info_size,&rx,&phy,generation,36,&rx.signal)==0);
        CHECK(rx.signal.valid && rx.signal.quality==(i?80:50));
        mutex_enter(&lock);rtw88_net_rx_frame(&net,packet+rx.payload_offset,
            rx.packet_size,&rx);mutex_exit(&lock);
        struct mbuf *queued=net.rx[net.rx_head].m;
        CHECK(queued && queued->m_len==24 && memcmp(queued->m_data,frame,24)==0);
        memset(packet+32,0xa5,24);
        memset(&rx,0,sizeof(rx)); /* Borrowed producer storage can change now. */
        CHECK(queued && memcmp(queued->m_data,frame,24)==0);
        rtw88_net_softintr(&net);observed[i]=route_last_rssi;
        memcpy(packet+32,frame,24);
    }
    CHECK(observed[0]==50 && observed[1]==80);
    CHECK(rtw88_signal_scan_check(observed[0],observed[1])==0);
    /* The actual callback must attach unknown metadata to this frame only. */
    rx.signal.valid=0;mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,24,&rx);mutex_exit(&lock);
    rtw88_net_softintr(&net);CHECK(route_last_rssi==0);
    /* Unknown optional signal still delivers DATA from its actual descriptor. */
    for(unsigned kind=0;kind<4;kind++) {
        packet[12]=(uint8_t)(kind==2?0x40:0x2c);
        packet[29]=(uint8_t)(kind==0?0x01:kind==1?0x91:0x19);
        if(kind==0)phy.channel_valid=0;else phy.channel_valid=1;
        CHECK(rtw88_rx_decode(packet,sizeof(packet),&rx)==0);
        CHECK(rtw88_rx_signal_decode(packet+rx.driver_info_offset,
            kind==3?7:rx.driver_info_size,&rx,&phy,generation,36,&rx.signal)==ENOENT);
        unsigned before=route_rx;
        mutex_enter(&lock);rtw88_net_rx_frame(&net,packet+rx.payload_offset,
            rx.packet_size,&rx);mutex_exit(&lock);
        memset(packet+32,0xa5,24);memset(&rx,0,sizeof(rx));
        rtw88_net_softintr(&net);CHECK(route_rx==before+1 && route_last_rssi==0);
        memcpy(packet+32,frame,24);
    }
    unsigned delivered=route_rx;
    mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,24,&rx);mutex_exit(&lock);
    sc.sc_generation++;rtw88_net_softintr(&net);CHECK(route_rx==delivered);
    CHECK(net.stats.fw_stale==1 && net.policy.queued==0);
    /* A stale signal observation does not invalidate its current DATA owner. */
    packet[12]=0x2c;packet[29]=0x19;packet[25]=90;
    CHECK(rtw88_rx_decode(packet,sizeof(packet),&rx)==0);
    CHECK(rtw88_rx_signal_decode(packet+rx.driver_info_offset,
        rx.driver_info_size,&rx,&phy,generation,36,&rx.signal)==0);
    mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,24,&rx);mutex_exit(&lock);
    rtw88_net_softintr(&net);CHECK(route_rx==delivered+1 && route_last_rssi==0);
    /* Epoch withdrawal still fences queued data independently of firmware age. */
    mutex_enter(&lock);rtw88_net_rx_frame(&net,frame,24,&rx);
    (void)rtw88_net_request(&net.policy);mutex_exit(&lock);
    delivered=route_rx;rtw88_net_softintr(&net);CHECK(route_rx==delivered && net.policy.queued==0);
    beacon_arrival_clock();
    beacon_body_clock();
    printf("rtw88 signal queue: %u checks, %u failures\n",checks,failures);return failures!=0;
}
