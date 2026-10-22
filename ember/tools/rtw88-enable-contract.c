/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: native attach -> WME offer -> strict RUN apply -> AC consumer. */
#ifdef NDEBUG
#error The kernel service model requires assertion preconditions.
#endif
#include "enable-routing.inc"
/* Default hardware-progress service for old owning-exchange cases. Device
 * indices advance at a sleep boundary; the actual IRQ decoder retires them. */
static int enable_drain_complete(unsigned ticks)
{
    (void)ticks;assert(f.sc && f.sc->sc_net_cookie);
    struct rtw88_net *net=f.sc->sc_net_cookie;
    CHECK(!net->lock->held);
    for(unsigned q=0;q<RTW88_NTXQ;q++)f.tx_hw_rp[q]=f.sc->sc_txq[q].wp;
    f.mmio[0xb4]=0x7c;
    mutex_enter(net->lock);CHECK(rtw88_intr(f.sc)==1);mutex_exit(net->lock);
    return EWOULDBLOCK;
}
static struct chanAccParams enable_ap_params;
static unsigned enable_run_callbacks,enable_first_producer;
static int enable_reenter_run;
static int enable_parent_state(struct ieee80211com *ic,enum ieee80211_state state,int arg)
{
    (void)arg;ic->ic_state=state;
    if(state==IEEE80211_S_RUN) {
        struct rtw88_net *net=ic->ic_ifp->if_softc;
        CHECK(net->policy.accepting && net->core->sc_running);
        CHECK(mmio32(0x500)!=0 && mmio32(0x504)!=0);
        if(ic->ic_bss->ni_flags&IEEE80211_NODE_QOS) {
            static const uint32_t regs[4]={0x508,0x50c,0x504,0x500};
            CHECK(ic->ic_bss->ni_wme_applied_chan==ic->ic_curchan);
            CHECK(ic->ic_bss->ni_wme_applied_slot==9);
            CHECK(memcmp(&ic->ic_wme.wme_wmeChanParams,&enable_ap_params,
                sizeof(enable_ap_params))==0);
            for(unsigned ac=0;ac<4;ac++) {
                const struct wmeParams *p=&enable_ap_params.cap_wmeParams[ac];
                uint32_t expected=(uint32_t)p->wmep_txopLimit<<16 |
                    (uint32_t)p->wmep_logcwmax<<12 | (uint32_t)p->wmep_logcwmin<<8 |
                    (16U+9U*p->wmep_aifsn);
                CHECK(mmio32(regs[ac])==expected);
            }
        }
        CHECK(net->work_pending);
        if(ic->ic_fixed_rate==IEEE80211_FIXED_RATE_NONE) {
            CHECK(net->core->sc_ra.configured && net->core->sc_ra.active);
            CHECK(net->core->sc_ra.epoch==net->policy.epoch);
            CHECK(net->core->sc_ra.generation==net->core->sc_generation);
        } else CHECK(!net->core->sc_ra.configured && !net->core->sc_ra.active);
        enable_run_callbacks++;
        if(enable_reenter_run) {
            uint64_t epoch=net->policy.epoch;
            CHECK(rtw88_net_newstate(ic,IEEE80211_S_SCAN,0)==0);
            CHECK(!net->policy.accepting && net->policy.epoch==epoch+1);
            CHECK(!net->core->sc_ra.configured && net->reset_required);
        }
        if(ic->ic_ifp->if_snd.m!=NULL) {
            struct mbuf *head=ic->ic_ifp->if_snd.m;
            unsigned errors=route_errors,posted=net->core->sc_txq[RTW88_TXQ_VO].queued;
            enable_first_producer++;
            rtw88_net_start(ic->ic_ifp);
            if(enable_reenter_run) {
                CHECK(ic->ic_ifp->if_snd.m==head && route_errors==errors);
                CHECK(net->core->sc_txq[RTW88_TXQ_VO].queued==posted);
            }
        }
    }
    return 0;
}
static struct mbuf *enable_response(struct ieee80211com *ic,int include_wme,int malformed)
{
    struct mbuf *m=newbuf(sizeof(struct ieee80211_frame)+6+3+(include_wme?26:0));
    assert(m);struct ieee80211_frame *wh=(void *)m->m_data;
    wh->i_fc[0]=IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_ASSOC_RESP;
    memcpy(wh->i_addr1,ic->ic_myaddr,6);
    memcpy(wh->i_addr2,ic->ic_bss->ni_macaddr,6);
    memcpy(wh->i_addr3,ic->ic_bss->ni_bssid,6);
    uint8_t *p=(uint8_t *)(wh+1);
    p[0]=IEEE80211_CAPINFO_ESS;p[4]=1;p+=6;
    *p++=IEEE80211_ELEMID_RATES;*p++=1;*p++=12;
    if(include_wme) {
        struct ieee80211_wme_state ap=ic->ic_wme;
        ap.wme_bssChanParams.cap_info=3;
        for(unsigned ac=0;ac<4;ac++) {
            struct wmeParams *param=&ap.wme_bssChanParams.cap_wmeParams[ac];
            param->wmep_aifsn=3+ac;param->wmep_logcwmin=2+ac;
            param->wmep_logcwmax=6+ac;param->wmep_txopLimit=11*(ac+1);
            param->wmep_acm=param->wmep_noackPolicy=0;
        }
        enable_ap_params=ap.wme_bssChanParams;
        uint8_t *end=ieee80211_add_wme_param(p,&ap);
        CHECK(end==m->m_data+m->m_len);
        if(malformed)p[7]=2;
    }
    return m;
}
static int enable_wire_offer(struct rtw88_net *net)
{
    struct rtw88_txq *q=&net->core->sc_txq[RTW88_TXQ_MGMT];
    if(q->queued==0)return 0;
    unsigned at=(q->wp+q->nbuf-1)%q->nbuf;
    const uint8_t *p=q->buf[at].vaddr;
    unsigned len=(unsigned)p[0]|(unsigned)p[1]<<8;
    p+=RTW8822C_TX_PACKET_DESC_SIZE;
    const uint8_t *end=p+len;
    p+=sizeof(struct ieee80211_frame)+4;
    while(end-p>=2) {
        if(end-p<(int)p[1]+2)return 0;
        if(p[0]==IEEE80211_ELEMID_VENDOR && p[1]==7 &&
            p[2]==0x00 && p[3]==0x50 && p[4]==0xf2 && p[5]==WME_OUI_TYPE &&
            p[6]==WME_INFO_OUI_SUBTYPE && p[7]==WME_VERSION)return 1;
        p+=p[1]+2;
    }
    return 0;
}
static struct rtw88_net *enable_attach(struct rtw88_softc *sc,kmutex_t *lock)
{
    struct rtw88_net *net=NULL;
    const uint8_t mac[6]={2,1,2,3,4,5};
    reset_bus(sc);
    int attached=attach_ok(sc);CHECK(attached);
    if(!attached)return NULL;
    unsigned writes=f.writes;
    int rc=rtw88_net_attach(sc,sc,lock,mac,&net);
    CHECK(rc==0 && net!=NULL);
    if(!net)return NULL;
    CHECK(f.writes==writes);
    CHECK(net->ic.ic_wme.wme_sta_apply==rtw88_net_wme_apply);
    CHECK(net->ic.ic_rxba_ops==&rtw88_net_rxba_ops && net->ic.ic_ht20_admit==NULL);
    CHECK(net->ic.ic_caps==(IEEE80211_C_WPA2|IEEE80211_C_SHSLOT|IEEE80211_C_WME));
    CHECK(net->ic.ic_flags&IEEE80211_F_WME);
    for(unsigned ac=0;ac<WME_NUM_AC;ac++) {
        CHECK(net->ic.ic_wme.wme_chanParams.cap_wmeParams[ac].wmep_noackPolicy==0);
        CHECK(net->ic.ic_wme.wme_chanParams.cap_wmeParams[ac].wmep_logcwmin>0);
    }
    CHECK(net->configured_media==IFM_MAKEWORD(IFM_IEEE80211,IFM_AUTO,0,0));
    CHECK(rtw88_start(sc)==0);
    CHECK(ieee80211_setmode(&net->ic,IEEE80211_MODE_11A)==0);
    net->ic.ic_curchan=&net->ic.ic_channels[36];
    struct ieee80211_node *ni=net->ic.ic_bss;
    ni->ni_chan=net->ic.ic_curchan;ni->ni_rates=ieee80211_std_rateset_11a;
    for(unsigned i=0;i<ni->ni_rates.rs_nrates;i++) {
        unsigned rate=ni->ni_rates.rs_rates[i];
        if(rate==12 || rate==24 || rate==48)ni->ni_rates.rs_rates[i]|=IEEE80211_RATE_BASIC;
    }
    memset(ni->ni_macaddr,0x22,6);memset(ni->ni_bssid,0x22,6);
    ni->ni_intval=100;ni->ni_wme_ie=&observed_wme;
    net->ec.ec_if.if_flags|=IFF_RUNNING|IFF_UP;
    CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_ASSOC,0)==0);
    rtw88_net_state_work(&net->state_work,net);
    CHECK(!net->work_pending && net->policy.accepting);
    CHECK(net->ic.ic_state==IEEE80211_S_ASSOC);
    return net;
}
static void enable_destroy(struct rtw88_net *net)
{
    if(!net)return;
    CHECK(net->ec.ec_if.if_snd.m==NULL);
    CHECK(net->ic.ic_mgtq.m==NULL);
    CHECK(node_refs==1);
    CHECK(rtw88_stop(net->core)==0);
    net->core->sc_net_cookie=NULL;net->core->sc_tx_complete=NULL;
    net->core->sc_rx_frame=NULL;
    CHECK(rtw88_core_detach(net->core)==0);
    ieee80211_rxba_reset(&net->ic,net->ic.ic_bss);rtw88_net_rxba_detach(net);
    free(net->ic.ic_bss);workqueue_destroy(net->wq);kmem_free(net,sizeof(*net));
}
static unsigned enable_fail_after_reg;
static int enable_write_after(void *cookie,uint32_t reg,uint32_t value)
{
    int rc=write_4(cookie,reg,value);
    return rc==0 && reg==enable_fail_after_reg?EIO:rc;
}
static void enable_negatives(void)
{
    for(unsigned kind=0;kind<9;kind++) {
        struct rtw88_softc sc;kmutex_t lock={.initialized=1};
        struct rtw88_net *net=enable_attach(&sc,&lock);
        if(!net)return;
        struct ieee80211com *ic=&net->ic;
        struct ieee80211_node *ni=ic->ic_bss;
        active_ic=ic;
        if(kind!=0) {
            CHECK(ieee80211_send_mgmt(ic,ni,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,0)==0);
            CHECK(enable_wire_offer(net));
        }
        struct mbuf *m=enable_response(ic,kind!=8,kind==1);
        struct ieee80211_wme_state before=ic->ic_wme;
        unsigned runs=enable_run_callbacks,writes=f.writes;
        struct rtw88_bus_ops after_ops=ops;
        uint32_t prior=mmio32(0x504);
        if(kind==2)f.fail_edca_reg=0x504;
        if(kind==3) {
            enable_fail_after_reg=0x504;after_ops.write_4=enable_write_after;
            sc.sc_ops=&after_ops;
        }
        if(kind==4)CHECK(rtw88_net_newstate(ic,IEEE80211_S_ASSOC,0)==0);
        if(kind==5)rtw88_net_close(&net->policy);
        if(kind==6)CHECK(rtw88_channel(&sc,40)==0);
        if(kind==7)((struct ieee80211_frame *)(void *)m->m_data)->i_addr3[0]^=2;
        unsigned pre_response_writes=f.writes;
        ieee80211_recv_mgmt_assoc_resp(ic,m,ni,IEEE80211_FC0_SUBTYPE_ASSOC_RESP,0,0);
        m_freem(m);
        CHECK((ni->ni_flags&(IEEE80211_NODE_QOS|IEEE80211_NODE_WME_VALID))==0);
        CHECK(enable_run_callbacks==runs);
        if(kind==0 || kind==8) {
            CHECK(net->requested==IEEE80211_S_RUN);
            rtw88_net_state_work(&net->state_work,net);
            CHECK(ic->ic_state==IEEE80211_S_RUN && net->policy.accepting);
            queue_packet(&net->ec.ec_if.if_snd,ether_packet(0x30,0,0));
            rtw88_net_start(&net->ec.ec_if);
            CHECK(sc.sc_txq[RTW88_TXQ_BE].queued==1);
            CHECK(sc.sc_txq[RTW88_TXQ_VO].queued==0);
        } else if(kind==7) {
            CHECK(ic->ic_state==IEEE80211_S_ASSOC && !net->work_pending);
            CHECK(f.writes==pre_response_writes);
        } else {
            CHECK(ni->ni_associd==0);
            CHECK(!net->policy.accepting);
            if(kind!=5)CHECK(net->work_pending && net->requested==IEEE80211_S_SCAN);
            CHECK(memcmp(&before.wme_wmeChanParams,&ic->ic_wme.wme_wmeChanParams,
                sizeof(before.wme_wmeChanParams))==0);
            CHECK(memcmp(&before.wme_chanParams,&ic->ic_wme.wme_chanParams,
                sizeof(before.wme_chanParams))==0);
            if(kind==2 || kind==3) {
                CHECK(sc.sc_edca_error==EIO && rtw88_tx_available(&sc,RTW88_TXQ_BE)==0);
                CHECK((mmio32(0x504)!=prior)==(kind==3));
            } else CHECK(f.writes==pre_response_writes);
        }
        f.fail_edca_reg=enable_fail_after_reg=0;sc.sc_ops=&ops;
        if(kind==2 || kind==3) {
            CHECK(rtw88_stop(&sc)==0 && sc.sc_edca_error==EIO);
            CHECK(rtw88_start(&sc)==0 && sc.sc_edca_error==0);
        }
        (void)writes;
        enable_destroy(net);
    }
    for(unsigned fail=0;fail<2;fail++) {
        struct rtw88_softc sc;kmutex_t lock={.initialized=1};
        reset_bus(&sc);int attached=attach_ok(&sc);CHECK(attached);
        if(!attached)return;
        const uint8_t mac[6]={2,1,2,3,4,5};struct rtw88_net *net=NULL;
        unsigned registrations=enable_registrations;
        enable_fail_workqueue=!fail;enable_fail_softint=fail;
        CHECK(rtw88_net_attach(&sc,&sc,&lock,mac,&net)==ENOMEM);
        CHECK(net==NULL && enable_registrations==registrations);
        CHECK(enable_allocations==enable_releases);
        enable_fail_workqueue=enable_fail_softint=0;
        CHECK(rtw88_core_detach(&sc)==0);
    }
}
/* C1 actual posted-MGMT -> request -> drain -> first AUTH consumer.
 * Only hardware RP is advanced by the sleep service; the actual IRQ decoder
 * must retire descriptors and notify the native owner. */
static struct rtw88_net *drain_net;
static unsigned drain_pauses,drain_auth,drain_runs,drain_action,drain_stall,drain_reads,drain_fail_reg,drain_after_pause_reads;
static struct ieee80211_ht20_profile drain_profile;
static int drain_provider(struct ieee80211com *ic,struct ieee80211_ht20_profile *p)
{ (void)ic;CHECK(!drain_net->lock->held);*p=drain_profile;return 0; }
static int drain_read4(void *cookie,uint32_t reg,uint32_t *value)
{
    if(reg==0x3b0 || reg==0x3a8 || reg==0x3ac || reg==0x3a4 || reg==0x3a0)drain_reads++;
    if(drain_fail_reg && reg==drain_fail_reg)return EIO;
    return read_4(cookie,reg,value);
}
static unsigned drain_pending(void)
{
    unsigned n=0;
    for(unsigned q=0;q<RTW88_NTXQ;q++)n+=drain_net->core->sc_txq[q].queued;
    return n;
}
static int drain_write1(void *cookie,uint32_t reg,uint8_t value)
{
    if(reg==0x522)CHECK(drain_pending()==0);
    return write_1(cookie,reg,value);
}
static int drain_pause(unsigned ticks)
{
    (void)ticks;CHECK(!drain_net->lock->held);drain_pauses++;
    CHECK(!drain_net->core->sc_mailbox.active && !drain_net->policy.accepting);
    struct mbuf *head=drain_net->ec.ec_if.if_snd.m,*mgmt=drain_net->ic.ic_mgtq.m;
    unsigned refs=node_refs,crypto=route_crypto;
    rtw88_net_start(&drain_net->ec.ec_if);
    CHECK(drain_net->ec.ec_if.if_snd.m==head && drain_net->ic.ic_mgtq.m==mgmt && node_refs==refs && route_crypto==crypto);
    CHECK(drain_pending()!=0);
    if(drain_action && drain_pauses==1) {
        unsigned action=drain_action;drain_action=0;
        if(action==1) {
            drain_net->ic.ic_curchan=&drain_net->ic.ic_channels[40];
            CHECK(rtw88_net_newstate(&drain_net->ic,IEEE80211_S_SCAN,0)==0);
        } else if(action==2)rtw88_net_close(&drain_net->policy);
        else if(action==3)drain_net->core->sc_generation++;
        else if(action==4)route_ticks+=1001;
        else if(action==5)CHECK(rtw88_net_newstate(&drain_net->ic,IEEE80211_S_INIT,0)==0);
        else if(action==6)drain_profile.ampdu=32;
        if(action!=4)return EWOULDBLOCK;
    }
    if(drain_stall)return EWOULDBLOCK;
    for(unsigned q=0;q<RTW88_NTXQ;q++)f.tx_hw_rp[q]=drain_net->core->sc_txq[q].wp;
    f.mmio[0xb4]=0x7c; /* Source TX completion cause bits, no RX cause. */
    mutex_enter(drain_net->lock);
    CHECK(rtw88_intr(drain_net->core)==1);
    CHECK(drain_pending()==0);
    mutex_exit(drain_net->lock);
    drain_after_pause_reads=drain_reads;
    return EWOULDBLOCK;
}
static int drain_parent(struct ieee80211com *ic,enum ieee80211_state state,int arg)
{
    (void)arg;ic->ic_state=state;CHECK(!drain_net->lock->held);
    if(state==IEEE80211_S_RUN)drain_runs++;
    if(state==IEEE80211_S_AUTH) {
        CHECK(drain_pending()==0);drain_auth++;
        CHECK(ieee80211_send_mgmt(ic,ic->ic_bss,IEEE80211_FC0_SUBTYPE_AUTH,1)==0);
    }
    return 0;
}
static void enable_drain_case(void)
{
    struct rtw88_softc sc;kmutex_t lock={.initialized=1};
    struct rtw88_net *net=enable_attach(&sc,&lock);if(!net)return;
    active_ic=&net->ic;
    CHECK(ieee80211_send_mgmt(&net->ic,net->ic.ic_bss,IEEE80211_FC0_SUBTYPE_AUTH,1)==0);
    CHECK(sc.sc_txq[RTW88_TXQ_MGMT].queued==1);
    queue_packet(&net->ec.ec_if.if_snd,ether_packet(0x30,0,0));
    struct mbuf *head=net->ec.ec_if.if_snd.m;
    struct rtw88_bus_ops bus=ops;bus.write_1=drain_write1;sc.sc_ops=&bus;
    drain_net=net;drain_pauses=drain_auth=drain_runs=drain_action=drain_stall=drain_reads=drain_fail_reg=0;net->parent_state=drain_parent;
    enable_drain_pause_hook=drain_pause;
    CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_AUTH,0)==0);
    CHECK(ieee80211_send_mgmt(&net->ic,net->ic.ic_bss,IEEE80211_FC0_SUBTYPE_AUTH,1)==0);
    CHECK(net->ic.ic_mgtq.m!=NULL && sc.sc_txq[RTW88_TXQ_MGMT].queued==1);
    uint64_t completed=sc.sc_stats.tx_done;
    rtw88_net_state_work(&net->state_work,net);
    CHECK(drain_pauses==1 && drain_auth==1);
    rtw88_net_softintr(net); /* Deliver the actual IRQ's scheduled continuation. */
    CHECK(net->ic.ic_mgtq.m==NULL);
    CHECK(sc.sc_stats.tx_done==completed+1 && sc.sc_txq[RTW88_TXQ_MGMT].queued==2);
    CHECK(net->ec.ec_if.if_snd.m==head && node_refs==1);
    m_freem(head);net->ec.ec_if.if_snd.m=NULL;
    net->ec.ec_if.if_snd.ifq_tail=NULL;net->ec.ec_if.if_snd.ifq_len=0;
    enable_drain_pause_hook=NULL;drain_net=NULL;active_ic=NULL;sc.sc_ops=&ops;
    enable_destroy(net);
}

/* The all-five matrix is a core descriptor contract; the owning unlinked
 * path above/below naturally exercises MGMT, not fabricated unlinked DATA. */
static void enable_drain_core(void)
{
    struct rtw88_softc sc;kmutex_t lock={.initialized=1};
    struct rtw88_net *net=enable_attach(&sc,&lock);if(!net)return;
    static const uint8_t tids[5]={0,3,1,5,6};
    static const unsigned idx[5]={0x3b0,0x3a8,0x3ac,0x3a4,0x3a0};
    uint8_t frame[26]={0x88,0x02};unsigned pending;
    drain_net=net;drain_fail_reg=drain_reads=0;
    struct rtw88_bus_ops bus=ops;bus.read_4=drain_read4;sc.sc_ops=&bus;
    mutex_enter(&lock);
    CHECK(rtw88_tx_drain_snapshot(&sc,&pending)==0 && pending==0 && drain_reads==0);
    for(unsigned round=0;round<RTW88_TX_SLOTS+2;round++) {
        uint64_t done=sc.sc_stats.tx_done;
        for(unsigned q=0;q<RTW88_NTXQ;q++) {
            frame[0]=q==0?0xb0:0x88;frame[24]=tids[q];
            CHECK(rtw88_tx_raw(&sc,q,frame,sizeof(frame))==0);
        }
        CHECK(rtw88_tx_drain_snapshot(&sc,&pending)==0 && pending==5);
        for(unsigned q=0;q<RTW88_NTXQ;q++) {
            f.tx_hw_rp[q]=sc.sc_txq[q].wp;
            CHECK(rtw88_tx_drain_snapshot(&sc,&pending)==0 && pending==4-q);
            CHECK(sc.sc_stats.tx_done==done+q+1);
            f.mmio[0xb4]=0x7c;CHECK(rtw88_intr(&sc)==1);
            CHECK(sc.sc_stats.tx_done==done+q+1); /* Duplicate IRQ cannot double count. */
        }
    }
    for(unsigned q=0;q<RTW88_NTXQ;q++) {
        frame[0]=q==0?0xb0:0x88;frame[24]=tids[q];
        CHECK(rtw88_tx_raw(&sc,q,frame,sizeof(frame))==0);
        unsigned queued=sc.sc_txq[q].queued,rp=sc.sc_txq[q].rp;
        uint64_t done=sc.sc_stats.tx_done;
        drain_fail_reg=idx[q];CHECK(rtw88_tx_drain_snapshot(&sc,&pending)==EIO);
        CHECK(sc.sc_txq[q].queued==queued && sc.sc_txq[q].rp==rp && sc.sc_stats.tx_done==done);
        drain_fail_reg=0;
        f.tx_hw_rp[q]=(sc.sc_txq[q].wp+1)%RTW88_TX_SLOTS;
        CHECK(rtw88_tx_drain_snapshot(&sc,&pending)==EIO);
        CHECK(sc.sc_txq[q].queued==queued && sc.sc_stats.tx_done==done);
        f.tx_hw_rp[q]=RTW88_TX_SLOTS;
        CHECK(rtw88_tx_drain_snapshot(&sc,&pending)==EIO);
        CHECK(sc.sc_txq[q].queued==queued && sc.sc_stats.tx_done==done);
        f.tx_hw_rp[q]=sc.sc_txq[q].wp;
        CHECK(rtw88_tx_drain_snapshot(&sc,&pending)==0 && pending==0);
        /* A producer write may land before returning EIO: keep the entry
         * until a subsequent real device RP confirms its consumption. */
        uint64_t posted=sc.sc_stats.tx_posted[q],retired=sc.sc_stats.tx_done;
        posted_fail_idx=idx[q];posted_fail_after=1;bus.write_2=posted_write_2;
        CHECK(rtw88_tx_raw(&sc,q,frame,sizeof(frame))==EIO);
        bus.write_2=write_2;posted_fail_idx=0;
        CHECK(sc.sc_stats.tx_posted[q]==posted && sc.sc_txq[q].queued==1);
        CHECK(rtw88_tx_drain_snapshot(&sc,&pending)==0 && pending==1);
        CHECK(sc.sc_stats.tx_done==retired);
        f.tx_hw_rp[q]=sc.sc_txq[q].wp;
        CHECK(rtw88_tx_drain_snapshot(&sc,&pending)==0 && pending==0);
        CHECK(sc.sc_stats.tx_done==retired+1);
    }
    mutex_exit(&lock);sc.sc_ops=&ops;drain_net=NULL;
    enable_destroy(net);
}
static void enable_drain_edges(void)
{
    for(unsigned kind=0;kind<10;kind++) {
        struct rtw88_softc sc;kmutex_t lock={.initialized=1};
        struct rtw88_net *net=enable_attach(&sc,&lock);if(!net)return;
        active_ic=&net->ic;
        CHECK(ieee80211_send_mgmt(&net->ic,net->ic.ic_bss,IEEE80211_FC0_SUBTYPE_AUTH,1)==0);
        CHECK(sc.sc_txq[RTW88_TXQ_MGMT].queued==1);
        struct rtw88_bus_ops bus=ops;bus.write_1=drain_write1;bus.read_4=drain_read4;
        drain_net=net;drain_pauses=drain_auth=drain_runs=drain_action=drain_stall=drain_reads=drain_fail_reg=0;
        net->parent_state=drain_parent;enable_drain_pause_hook=drain_pause;
        if(kind==0)drain_fail_reg=0x3b0;
        if(kind==1)f.tx_hw_rp[0]=RTW88_TX_SLOTS;
        if(kind==2)f.tx_hw_rp[0]=(sc.sc_txq[0].wp+1)%RTW88_TX_SLOTS;
        if(kind==3)drain_stall=1;
        if(kind>=4)drain_action=kind-3;
        if(kind==9) {
            CHECK(ieee80211_send_mgmt(&net->ic,net->ic.ic_bss,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,0)==0);
            struct mbuf *m=enable_response(&net->ic,1,0);
            ieee80211_recv_mgmt_assoc_resp(&net->ic,m,net->ic.ic_bss,IEEE80211_FC0_SUBTYPE_ASSOC_RESP,0,0);m_freem(m);
            drain_profile=(struct ieee80211_ht20_profile){99,0xff,0xff,0x3f,16};
            struct ieee80211_ht20_node *h=&net->ic.ic_bss->ni_ht20;
            h->valid=1;h->profile=drain_profile;h->tx_mcs=0xff;h->attempt=h->offer_attempt=1;
            h->channel=net->ic.ic_curchan;memcpy(h->bssid,net->ic.ic_bss->ni_bssid,6);
            net->ic.ic_ht20_admit=drain_provider;
        } else CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_AUTH,0)==0);
        sc.sc_ops=&bus; /* Monitor the worker, after actual WME response setup. */
        uint64_t done=sc.sc_stats.tx_done;
        rtw88_net_state_work(&net->state_work,net);
        CHECK(!net->work_pending && drain_auth==0 && drain_runs==0 && !sc.sc_mailbox.active);
        if(kind<=3 || kind==6 || kind==7 || kind==9)CHECK(!net->policy.accepting && !sc.sc_started);
        if(kind==3)CHECK(drain_pauses==1000 && sc.sc_stats.tx_done==done);
        if(kind==4)CHECK(net->ic.ic_state==IEEE80211_S_SCAN && sc.sc_channel==40);
        if(kind==5)CHECK(net->policy.dying && !net->policy.accepting && sc.sc_txq[0].queued==1);
        if(kind==6)CHECK(drain_reads==1 && sc.sc_stats.tx_done==done);
        if(kind==7)CHECK(drain_reads==drain_after_pause_reads && sc.sc_stats.tx_done==done+1);
        if(kind==8)CHECK(net->ic.ic_state==IEEE80211_S_INIT && drain_pauses==2 && sc.sc_started);
        if(kind==9)CHECK(drain_reads==1 && sc.sc_stats.tx_done==done && !net->ic.ic_bss->ni_ht20.valid);
        drain_fail_reg=drain_stall=0;enable_drain_pause_hook=NULL;drain_net=NULL;active_ic=NULL;sc.sc_ops=&ops;
        enable_destroy(net);
    }
}

/* Causal managed-mailbox case: removing the sleeping checkpoint must expose
 * a busy delay under PCI and prevent unrelated service from progressing. */
static struct rtw88_net *mailbox_net;
static unsigned mailbox_busy,mailbox_locked_waits,mailbox_pauses,mailbox_auth;
static unsigned mailbox_reads,mailbox_fail_read,mailbox_busy_at,mailbox_cancel;
static unsigned mailbox_fail_write,mailbox_writes,mailbox_run;
static uint32_t mailbox_words[3],mailbox_extensions[3];
static struct ieee80211_ht20_profile mailbox_profile;
static int mailbox_provider(struct ieee80211com *ic,struct ieee80211_ht20_profile *p)
{ (void)ic;CHECK(!mailbox_net->lock->held);*p=mailbox_profile;return 0; }
static int mailbox_read1(void *cookie,uint32_t reg,uint8_t *value)
{
    if(reg==0x1cc) {
        mailbox_reads++;
        if(mailbox_reads==mailbox_fail_read)return EIO;
        if(mailbox_busy || mailbox_reads==mailbox_busy_at) {
            if(mailbox_busy)mailbox_busy--;
            *value=0x0f;return 0;
        }
    }
    return read_1(cookie,reg,value);
}
static int mailbox_write4(void *cookie,uint32_t reg,uint32_t value)
{
    int rc=write_4(cookie,reg,value);
    if(reg>=0x1d0 && reg<0x1e0) {
        if(mailbox_writes<3) {
            mailbox_words[mailbox_writes]=value;
            mailbox_extensions[mailbox_writes]=mmio32(0x1f0+(reg-0x1d0));
        }
        mailbox_writes++;
    }
    if(reg>=0x1d0 && reg<0x200 && mailbox_fail_write &&
        --mailbox_fail_write==0)return EIO; /* May already have landed. */
    return rc;
}
static void mailbox_delay(void *cookie,unsigned us)
{
    if(us==RTW88_POLL_STEP_US && mailbox_net && mailbox_net->lock->held)
        mailbox_locked_waits++;
    delay_us(cookie,us);
}
static int mailbox_pause(unsigned ticks)
{
    (void)ticks;assert(mailbox_net!=NULL);CHECK(!mailbox_net->lock->held);
    mailbox_pauses++;
    /* Actual producer/capacity must preserve DATA and MGMT owners. */
    struct mbuf *data=mailbox_net->ec.ec_if.if_snd.m;
    struct mbuf *mgmt=mailbox_net->ic.ic_mgtq.m;
    unsigned crypto=route_crypto,refs=node_refs;
    mailbox_net->ec.ec_if.if_flags&=~IFF_OACTIVE;
    rtw88_net_start(&mailbox_net->ec.ec_if);
    CHECK(mailbox_net->ec.ec_if.if_snd.m==data && mailbox_net->ic.ic_mgtq.m==mgmt);
    CHECK(route_crypto==crypto && node_refs==refs);
    mutex_enter(mailbox_net->lock);
    for(unsigned q=0;q<RTW88_NTXQ;q++)CHECK(!rtw88_net_tx_capacity(mailbox_net,q,0));
    mutex_exit(mailbox_net->lock);
    if(mailbox_cancel && mailbox_pauses==1) {
        unsigned action=mailbox_cancel;mailbox_cancel=0;mailbox_busy=0;
        if(action==1) {
            mailbox_net->ic.ic_curchan=&mailbox_net->ic.ic_channels[40];
            CHECK(rtw88_net_newstate(&mailbox_net->ic,IEEE80211_S_SCAN,0)==0);
        } else if(action==2)rtw88_net_close(&mailbox_net->policy);
        else if(action==3)mailbox_net->core->sc_generation++;
        else if(action==4)mailbox_net->core->sc_h2c_box^=1;
        else if(action==6)route_ticks+=1001;
        else if(action==5)mailbox_profile.ampdu=32; /* Same generation. */
    }
    return EWOULDBLOCK;
}
static int mailbox_parent(struct ieee80211com *ic,enum ieee80211_state state,int arg)
{
    int rc=enable_parent_state(ic,state,arg);
    if(state==IEEE80211_S_RUN)mailbox_run++;
    if(state==IEEE80211_S_AUTH) {
        mailbox_auth++;
        CHECK(ieee80211_send_mgmt(ic,ic->ic_bss,IEEE80211_FC0_SUBTYPE_AUTH,1)==0);
    }
    return rc;
}
static void enable_mailbox_case(void)
{
    struct rtw88_softc sc;kmutex_t lock={.initialized=1};
    struct rtw88_net *net=enable_attach(&sc,&lock);if(!net)return;
    struct rtw88_bus_ops bus=ops;bus.read_1=mailbox_read1;bus.delay_us=mailbox_delay;
    bus.write_4=mailbox_write4;
    sc.sc_ops=&bus;mailbox_net=net;net->parent_state=mailbox_parent;
    active_ic=&net->ic;enable_pause_hook=mailbox_pause;
    mailbox_busy=3;mailbox_locked_waits=mailbox_pauses=mailbox_auth=0;
    mailbox_reads=mailbox_writes=mailbox_run=0;
    unsigned box=sc.sc_h2c_box,posted=sc.sc_txq[RTW88_TXQ_MGMT].queued;
    CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_AUTH,0)==0);
    rtw88_net_state_work(&net->state_work,net);
    CHECK(mailbox_locked_waits==0);
    CHECK(mailbox_pauses==3);
    CHECK(mailbox_auth==1 && net->ic.ic_state==IEEE80211_S_AUTH);
    CHECK(!net->work_pending && net->policy.accepting);
    CHECK(sc.sc_h2c_box==((box+1)&3));
    CHECK(mmio32(0x1d0+4*box)==0x01); /* Existing media-status bytes. */
    CHECK(sc.sc_txq[RTW88_TXQ_MGMT].queued==posted+1);
    enable_pause_hook=NULL;mailbox_net=NULL;active_ic=NULL;
    enable_destroy(net);
}

static void enable_mailbox_edges(void)
{
    for(unsigned kind=0;kind<13;kind++) {
        struct rtw88_softc sc;kmutex_t lock={.initialized=1};
        struct rtw88_net *net=enable_attach(&sc,&lock);if(!net)return;
        struct rtw88_bus_ops bus=ops;
        bus.read_1=mailbox_read1;bus.delay_us=mailbox_delay;bus.write_4=mailbox_write4;
        sc.sc_ops=&bus;mailbox_net=net;net->parent_state=mailbox_parent;
        active_ic=&net->ic;enable_pause_hook=mailbox_pause;
        mailbox_reads=mailbox_writes=mailbox_run=mailbox_auth=0;
        mailbox_pauses=mailbox_locked_waits=0;
        mailbox_fail_read=mailbox_busy_at=mailbox_cancel=mailbox_fail_write=0;
        mailbox_busy=kind==0?0:2;
        if(kind==0) { /* The actual zero-command INIT must not inspect HMETFR. */
            CHECK(rtw88_stop(&sc)==0);
            CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_INIT,0)==0);
        } else if(kind==11) {
            CHECK(ieee80211_send_mgmt(&net->ic,net->ic.ic_bss,
                IEEE80211_FC0_SUBTYPE_ASSOC_REQ,0)==0);
            struct mbuf *m=enable_response(&net->ic,1,0);
            ieee80211_recv_mgmt_assoc_resp(&net->ic,m,net->ic.ic_bss,
                IEEE80211_FC0_SUBTYPE_ASSOC_RESP,0,0);m_freem(m);
            mailbox_profile=(struct ieee80211_ht20_profile){99,0xff,0xff,0x3f,16};
            struct ieee80211_ht20_node *h=&net->ic.ic_bss->ni_ht20;
            h->valid=1;h->profile=mailbox_profile;h->tx_mcs=0xff;h->attempt=h->offer_attempt=1;
            h->channel=net->ic.ic_curchan;memcpy(h->bssid,net->ic.ic_bss->ni_bssid,6);
            net->ic.ic_ht20_admit=mailbox_provider;mailbox_cancel=5;
        } else {
            CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_AUTH,0)==0);
            if(kind<5)mailbox_cancel=kind;
            if(kind==12)mailbox_cancel=6;
            if(kind==5)mailbox_busy=100000; /* Deadline, not unbounded spinning. */
            if(kind==6 || kind==7) { mailbox_busy=0;mailbox_fail_read=kind-5; }
            if(kind==8) { mailbox_busy=0;mailbox_busy_at=2; }
            if(kind==9 || kind==10) { mailbox_busy=0;mailbox_fail_write=kind-8; }
        }
        unsigned auths=mailbox_auth,runs=mailbox_run,errors=enable_state_errors;
        rtw88_net_state_work(&net->state_work,net);
        CHECK(mailbox_locked_waits==0 && !sc.sc_mailbox.active);
        CHECK(mailbox_auth==auths && mailbox_run==runs);
        CHECK(!net->work_pending);
        if(kind==0)CHECK(mailbox_reads==0 && mailbox_pauses==0 &&
            mailbox_writes==0 && enable_state_errors==errors);
        if(kind==1)CHECK(net->ic.ic_state==IEEE80211_S_SCAN && sc.sc_channel==40);
        if(kind==2)CHECK(net->policy.dying && !net->policy.accepting &&
            mailbox_reads==1 && mailbox_writes==0);
        if(kind>=3)CHECK(!net->policy.accepting && !sc.sc_started);
        if(kind==5)CHECK(mailbox_pauses==1000);
        if(kind==8)CHECK(mailbox_pauses==0); /* Nowait commit must not fall back. */
        if(kind==3 || kind==4 || kind==11 || kind==12)
            CHECK(mailbox_reads==1 && mailbox_writes==0);
        if(kind==11)CHECK(!net->ic.ic_bss->ni_ht20.valid);
        enable_pause_hook=NULL;mailbox_net=NULL;active_ic=NULL;
        mailbox_fail_write=0;mailbox_fail_read=0;mailbox_busy=0;
        enable_destroy(net);
    }
}

static void enable_mailbox_run(void)
{
    for(unsigned fault=0;fault<=6;fault++) {
        struct rtw88_softc sc;kmutex_t lock={.initialized=1};
        struct rtw88_net *net=enable_attach(&sc,&lock);if(!net)return;
        active_ic=&net->ic;
        CHECK(ieee80211_send_mgmt(&net->ic,net->ic.ic_bss,
            IEEE80211_FC0_SUBTYPE_ASSOC_REQ,0)==0);
        struct mbuf *m=enable_response(&net->ic,1,0);
        ieee80211_recv_mgmt_assoc_resp(&net->ic,m,net->ic.ic_bss,
            IEEE80211_FC0_SUBTYPE_ASSOC_RESP,0,0);m_freem(m);
        struct rtw88_bus_ops bus=ops;bus.read_1=mailbox_read1;
        bus.delay_us=mailbox_delay;bus.write_4=mailbox_write4;sc.sc_ops=&bus;
        mailbox_net=net;net->parent_state=mailbox_parent;enable_pause_hook=mailbox_pause;
        mailbox_reads=mailbox_writes=mailbox_run=mailbox_auth=0;
        mailbox_pauses=mailbox_locked_waits=0;mailbox_busy=fault?0:2;
        mailbox_fail_read=mailbox_busy_at=mailbox_cancel=0;mailbox_fail_write=fault;
        unsigned box=sc.sc_h2c_box;
        rtw88_net_state_work(&net->state_work,net);
        CHECK(!sc.sc_mailbox.active && mailbox_locked_waits==0);
        if(fault==0) {
            CHECK(mailbox_run==1 && sc.sc_ra.active && net->policy.accepting);
            CHECK(mailbox_writes==3 && sc.sc_h2c_box==((box+3)&3));
            CHECK(mailbox_words[0]==0x101 && mailbox_extensions[0]==0);
            CHECK(mailbox_words[1]==0x2c && mailbox_extensions[1]==0);
            CHECK(mailbox_words[2]==0x40270040 && mailbox_extensions[2]==0xff0);
        } else CHECK(mailbox_run==0 && !net->policy.accepting && !sc.sc_started);
        enable_pause_hook=NULL;mailbox_net=NULL;active_ic=NULL;mailbox_fail_write=0;
        enable_destroy(net);
    }
}
static void enable_mailbox_queues(void)
{
    struct rtw88_softc sc;kmutex_t lock={.initialized=1};
    struct rtw88_net *net=enable_attach(&sc,&lock);if(!net)return;
    active_ic=&net->ic;
    CHECK(rtw88_net_newstate(&net->ic,IEEE80211_S_AUTH,0)==0);
    queue_packet(&net->ec.ec_if.if_snd,ether_packet(0x30,0,0));
    struct mbuf *data=net->ec.ec_if.if_snd.m;
    CHECK(ieee80211_send_mgmt(&net->ic,net->ic.ic_bss,IEEE80211_FC0_SUBTYPE_AUTH,1)==0);
    CHECK(net->ic.ic_mgtq.m!=NULL && sc.sc_txq[RTW88_TXQ_MGMT].queued==0);
    struct rtw88_bus_ops bus=ops;bus.read_1=mailbox_read1;
    bus.delay_us=mailbox_delay;bus.write_4=mailbox_write4;sc.sc_ops=&bus;
    mailbox_net=net;net->parent_state=mailbox_parent;enable_pause_hook=mailbox_pause;
    mailbox_reads=mailbox_writes=mailbox_run=mailbox_auth=0;
    mailbox_pauses=mailbox_locked_waits=0;mailbox_busy=2;
    mailbox_fail_read=mailbox_busy_at=mailbox_cancel=mailbox_fail_write=0;
    rtw88_net_state_work(&net->state_work,net);
    CHECK(mailbox_pauses==2 && mailbox_locked_waits==0);
    CHECK(net->ec.ec_if.if_snd.m==data && net->ic.ic_mgtq.m!=NULL);
    rtw88_net_softintr(net); /* Real scheduled continuation releases OACTIVE. */
    CHECK(net->ic.ic_mgtq.m==NULL && sc.sc_txq[RTW88_TXQ_MGMT].queued==2);
    CHECK(net->ec.ec_if.if_snd.m==data && node_refs==1);
    m_freem(data);net->ec.ec_if.if_snd.m=NULL;
    net->ec.ec_if.if_snd.ifq_tail=NULL;net->ec.ec_if.if_snd.ifq_len=0;
    enable_pause_hook=NULL;mailbox_net=NULL;active_ic=NULL;
    enable_destroy(net);
}

/* C4a host-only admitted profile. No native provider is registered by attach. */
static struct rtw88_net *ht20_net;
static struct ieee80211_ht20_profile ht20_profile;
static int ht20_provider(struct ieee80211com *ic,struct ieee80211_ht20_profile *p)
{
    CHECK(ic==&ht20_net->ic && !ht20_net->lock->held);
    *p=ht20_profile;return 0;
}
static struct rtw88_net *enable_ht20_negotiate(struct rtw88_softc *sc,
    kmutex_t *lock,int first)
{
    struct rtw88_net *net=enable_attach(sc,lock);
    if(!net)return NULL;
    struct ieee80211com *ic=&net->ic;struct ieee80211_node *ni=ic->ic_bss;
    ht20_net=net;ht20_profile=(struct ieee80211_ht20_profile){777,0xff,0xff,0x3f,16};
    ic->ic_ht20_admit=ht20_provider;active_ic=ic;
    static struct cipher ht20_cipher={.ic_header=8};
    route_key.wk_cipher=&ht20_cipher;
    ic->ic_flags|=IEEE80211_F_PRIVACY;ni->ni_rsn.rsn_ucastcipher=IEEE80211_CIPHER_AES_CCM;route_have_key=1;
    CHECK(ieee80211_send_mgmt(ic,ni,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,0)==0);
    CHECK(ni->ni_ht20.offered && ni->ni_ht20.profile.generation==777);
    struct mbuf *m=enable_response(ic,1,0);
    uint8_t *rates=m->m_data+sizeof(struct ieee80211_frame)+6;
    memmove(rates+10,rates+3,(size_t)m->m_len-sizeof(struct ieee80211_frame)-9);
    rates[1]=8;memcpy(rates+2,ni->ni_rates.rs_rates,8);
    m->m_len+=7;
    uint8_t *p=m->m_data+m->m_len;
    ieee80211_ht20_write(p,&ht20_profile);p+=28;
    p[0]=61;p[1]=22;p[2]=36;p[8]=1;p+=24;
    m->m_len=m->m_pkthdr.len=m->model_capacity=(int)(p-m->m_data);
    ieee80211_recv_mgmt_assoc_resp(ic,m,ni,IEEE80211_FC0_SUBTYPE_ASSOC_RESP,0,0);
    m_freem(m);
    CHECK(ni->ni_ht20.valid && ni->ni_ht20.tx_mcs==0xff && ni->ni_ht20.operation==0);
    if(first<0)return net; /* Parser has queued actual RUN; worker is deferred. */
    if(first)queue_packet(&net->ec.ec_if.if_snd,ether_packet(0,0,0));
    rtw88_net_state_work(&net->state_work,net);
    CHECK(ic->ic_state==IEEE80211_S_RUN && net->policy.accepting);
    return net;
}
static void enable_ht20_ra_chain(void)
{
    struct rtw88_softc sc;kmutex_t lock={.initialized=1};
    struct rtw88_net *net=enable_ht20_negotiate(&sc,&lock,1);
    if(!net)return;
    unsigned box=(sc.sc_h2c_box-1)&3;
    CHECK(mmio32(0x1d0+4*box)==UINT32_C(0x40250040));
    CHECK(mmio32(0x1f0+4*box)==UINT32_C(0x000ff030));
    CHECK(sc.sc_ra.rate_id==5 && sc.sc_ra.mask==0xff030);
    struct rtw88_txq *q=&sc.sc_txq[RTW88_TXQ_BE];
    CHECK(q->queued==1);
    if(q->queued) {
        const uint8_t *desc=q->buf[(q->wp+q->nbuf-1)%q->nbuf].vaddr;
        CHECK(desc[6]==5 && (desc[4]&0x7f)==0);
        CHECK((desc[13]&0x15)==0);
        CHECK((desc[9]&0x10)==0);
        CHECK((desc[20]&0xf0)==0 && (desc[21]&0x03)==0);
    }
    uint8_t report[9]={0x0c,0x44,0x13,0,0,0,0,0,0};
    uint64_t observed=sc.sc_stats.ra_reports;
    rx_put(&sc,0,sizeof(report),1,report,0);f.rx_hw_idx=1;set_hisr0(1);
    CHECK(rtw88_intr(&sc)==1);
    CHECK(sc.sc_stats.ra_reports==observed+1 && sc.sc_stats.ra_report_rate==0x13);
    CHECK(sc.sc_stats.ra_report_bw==0 && sc.sc_stats.ra_observed_generation==sc.sc_generation);
    enable_destroy(net);ht20_net=NULL;route_have_key=0;
}
static void enable_ht20_precrypto_fences(void)
{
    for(unsigned kind=0;kind<19;kind++) {
        struct rtw88_softc sc;kmutex_t lock={.initialized=1};
        struct rtw88_net *net=enable_ht20_negotiate(&sc,&lock,0);
        if(!net)return;
        struct ieee80211com *ic=&net->ic;struct ieee80211_node *ni=ic->ic_bss;
        queue_packet(&net->ec.ec_if.if_snd,ether_packet(0,0,0));
        struct mbuf *head=net->ec.ec_if.if_snd.m;
        unsigned crypto=route_crypto,posted=sc.sc_txq[RTW88_TXQ_BE].queued;
        uint64_t epoch=net->policy.epoch;
        switch(kind) {
        case 0: ic->ic_ht20_admit=NULL;break;
        case 1: ht20_profile.generation++;break;
        case 2: ht20_profile.rx_mcs=ht20_profile.tx_mcs=0xffff;break;
        case 3: ht20_profile.ampdu=20;break;
        case 4: ni->ni_ht20.valid=0;break;
        case 5: ni->ni_ht20.operation=1;break;
        case 6: sc.sc_ra.profile_generation++;break;
        case 7: sc.sc_ra.ht_mcs^=1;break;
        case 8: sc.sc_generation++;break;
        case 9: ni->ni_bssid[0]^=2;break;
        case 10: ic->ic_flags&=~IEEE80211_F_PRIVACY;break;
        case 11: ni->ni_rsn.rsn_ucastcipher=0;break;
        case 12: sc.sc_ra.phy=RTW88_RA_LEGACY;break;
        case 13: ic->ic_curmode=IEEE80211_MODE_11G;break;
        case 14: ic->ic_flags|=IEEE80211_F_USEPROT;break;
        case 15: ic->ic_fixed_rate=0;break;
        case 16: CHECK(rtw88_net_newstate(ic,IEEE80211_S_SCAN,0)==0);epoch=net->policy.epoch;break;
        case 17: sc.sc_channel=sc.sc_ra.channel=149;break;
        case 18: ni->ni_associd=0;break;
        }
        rtw88_net_start(&net->ec.ec_if);
        CHECK(net->ec.ec_if.if_snd.m==head && route_crypto==crypto);
        CHECK(sc.sc_txq[RTW88_TXQ_BE].queued==posted);
        CHECK(!net->policy.accepting && !sc.sc_ra.active);
        if(kind==16)CHECK(net->requested==IEEE80211_S_SCAN && net->policy.epoch==epoch);
        else CHECK(net->requested==IEEE80211_S_INIT);
        while(net->ec.ec_if.if_snd.m) {
            struct mbuf *m=net->ec.ec_if.if_snd.m;net->ec.ec_if.if_snd.m=m->m_nextpkt;m_freem(m);
        }
        net->ec.ec_if.if_snd.ifq_tail=NULL;net->ec.ec_if.if_snd.ifq_len=0;
        enable_destroy(net);ht20_net=NULL;route_have_key=0;
    }
}
static unsigned ht20_command_fault;
static int ht20_command_write4(void *cookie,uint32_t reg,uint32_t value)
{
    int target=reg>=0x1f0 && reg<=0x1fc && value==0xff030;
    if(target && ht20_command_fault==1)return EIO;
    int error=write_4(cookie,reg,value);
    return error==0 && target && ht20_command_fault==2?EIO:error;
}
static void enable_ht20_worker_fences(void)
{
    for(unsigned kind=0;kind<8;kind++) {
        struct rtw88_softc sc;kmutex_t lock={.initialized=1};
        struct rtw88_net *net=enable_ht20_negotiate(&sc,&lock,-1);
        if(!net)return;
        struct ieee80211com *ic=&net->ic;
        unsigned commands=sc.sc_stats.ra_commands,callbacks=enable_run_callbacks;
        switch(kind) {
        case 0: ic->ic_ht20_admit=NULL;break;
        case 1: ht20_profile.generation++;break;
        case 2: ic->ic_bss->ni_ht20.operation=1;break;
        case 3: ic->ic_bss->ni_ht20.tx_mcs=0x100;break;
        case 4: ic->ic_fixed_rate=0;break;
        case 5: case 6: ht20_command_fault=kind-4;break;
        case 7: ic->ic_curmode=IEEE80211_MODE_11G;break;
        }
        struct rtw88_bus_ops bus=ops;bus.write_4=ht20_command_write4;sc.sc_ops=&bus;
        rtw88_net_state_work(&net->state_work,net);
        CHECK(sc.sc_stats.ra_commands==commands && enable_run_callbacks==callbacks);
        CHECK(!net->policy.accepting && !sc.sc_ra.active && !sc.sc_running);
        CHECK((net->ec.ec_if.if_flags&IFF_RUNNING)==0);
        if(kind==5 || kind==6)CHECK(sc.sc_ra.error==EIO);
        ht20_command_fault=0;enable_destroy(net);ht20_net=NULL;route_have_key=0;
    }
}

static void enable_ht20_to_legacy_fixed(void)
{
    struct rtw88_softc sc;kmutex_t lock={.initialized=1};
    struct rtw88_net *net=enable_ht20_negotiate(&sc,&lock,0);
    if(!net)return;
    struct ieee80211com *ic=&net->ic;
    uint32_t generation=sc.sc_generation;
    CHECK(rtw88_net_newstate(ic,IEEE80211_S_SCAN,0)==0);
    rtw88_net_state_work(&net->state_work,net);
    CHECK(sc.sc_generation>generation && !sc.sc_ra.configured);
    ic->ic_ht20_admit=NULL;ic->ic_fixed_rate=0;
    CHECK(rtw88_net_newstate(ic,IEEE80211_S_ASSOC,0)==0);
    rtw88_net_state_work(&net->state_work,net);
    CHECK(ieee80211_send_mgmt(ic,ic->ic_bss,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,0)==0);
    CHECK(!ic->ic_bss->ni_ht20.offered);
    struct mbuf *m=enable_response(ic,1,0);
    ieee80211_recv_mgmt_assoc_resp(ic,m,ic->ic_bss,IEEE80211_FC0_SUBTYPE_ASSOC_RESP,0,0);
    m_freem(m);
    CHECK(!ic->ic_bss->ni_ht20.valid && net->requested==IEEE80211_S_RUN);
    queue_packet(&net->ec.ec_if.if_snd,ether_packet(0,0,0));
    rtw88_net_state_work(&net->state_work,net);
    CHECK(net->policy.accepting && ic->ic_state==IEEE80211_S_RUN);
    CHECK(!sc.sc_ra.configured && !sc.sc_ra.active);
    CHECK(net->ec.ec_if.if_snd.m==NULL && sc.sc_txq[RTW88_TXQ_BE].queued==1);
    if(sc.sc_txq[RTW88_TXQ_BE].queued) {
        const uint8_t *desc=sc.sc_txq[RTW88_TXQ_BE].buf[0].vaddr;
        CHECK((desc[13]&5)==5 && (desc[16]&0x7f)==4);
    }
    while(net->ec.ec_if.if_snd.m) {
        struct mbuf *p=net->ec.ec_if.if_snd.m;net->ec.ec_if.if_snd.m=p->m_nextpkt;m_freem(p);
    }
    net->ec.ec_if.if_snd.ifq_tail=NULL;net->ec.ec_if.if_snd.ifq_len=0;
    enable_destroy(net);ht20_net=NULL;route_have_key=0;
}

#ifndef RTW88_ENABLE_ENTRY
#define RTW88_ENABLE_ENTRY main
#endif
int RTW88_ENABLE_ENTRY(void)
{
    enable_drain_case();
    enable_drain_core();
    enable_drain_edges();
    enable_mailbox_case();
    enable_mailbox_edges();
    enable_mailbox_run();
    enable_mailbox_queues();
    enable_run_callbacks=enable_first_producer=0;
    int result=routing_base_main();
    if(result!=0)return result;
    enable_ht20_ra_chain();
    enable_ht20_precrypto_fences();
    enable_ht20_worker_fences();
    enable_ht20_to_legacy_fixed();
    enable_run_callbacks=enable_first_producer=0;
    struct rtw88_softc sc;kmutex_t lock={.initialized=1};
    struct rtw88_net *net=enable_attach(&sc,&lock);
    if(!net)return 1;
    struct ieee80211com *ic=&net->ic;
    active_ic=ic;
    CHECK(ieee80211_send_mgmt(ic,ic->ic_bss,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,0)==0);
    CHECK(enable_wire_offer(net));
    struct mbuf *m=enable_response(ic,1,0);
    ieee80211_recv_mgmt_assoc_resp(ic,m,ic->ic_bss,IEEE80211_FC0_SUBTYPE_ASSOC_RESP,0,0);
    m_freem(m);
    CHECK(ic->ic_bss->ni_flags&IEEE80211_NODE_QOS);
    CHECK(ic->ic_bss->ni_flags&IEEE80211_NODE_WME_VALID);
    CHECK(net->work_pending && net->requested==IEEE80211_S_RUN);
    queue_packet(&net->ec.ec_if.if_snd,ether_packet(0x30,0,0));
    rtw88_net_state_work(&net->state_work,net);
    CHECK(ic->ic_state==IEEE80211_S_RUN && net->policy.accepting);
    CHECK(enable_run_callbacks==1 && enable_first_producer==1);
    if(net->ec.ec_if.if_snd.m!=NULL) {
        m_freem(net->ec.ec_if.if_snd.m);net->ec.ec_if.if_snd.m=NULL;
    }
    CHECK(sc.sc_txq[RTW88_TXQ_VO].queued==1);
    static const unsigned tos[3]={0,8,0x28};
    static const unsigned queues[3]={RTW88_TXQ_BE,RTW88_TXQ_BK,RTW88_TXQ_VI};
    for(unsigned ac=0;ac<3;ac++) {
        queue_packet(&net->ec.ec_if.if_snd,ether_packet(tos[ac],0,0));
        rtw88_net_start(&net->ec.ec_if);
        CHECK(sc.sc_txq[queues[ac]].queued==1);
    }
    queue_packet(&net->ec.ec_if.if_snd,ether_packet(0x30,0,1));
    rtw88_net_start(&net->ec.ec_if);CHECK(sc.sc_txq[RTW88_TXQ_BE].queued==2);
    struct ieee80211req req={0};
    req.i_type=IEEE80211_IOC_WME_ACKPOLICY;req.i_len=WME_AC_VO;req.i_val=0;
    struct ieee80211_wme_state before=ic->ic_wme;
    unsigned writes=f.writes;
    CHECK(ieee80211_ioctl_setwmeparam(ic,&req)==EOPNOTSUPP);
    CHECK(memcmp(&before,&ic->ic_wme,sizeof(before))==0 && f.writes==writes);
    enable_destroy(net);
    enable_negatives();
    /* A real request during parent RUN withdraws acceptance before first TX.
     * Worker ownership alone permits the current commit; a new epoch does not. */
    net=enable_attach(&sc,&lock);
    if(!net)return 1;
    ic=&net->ic;active_ic=ic;
    CHECK(ieee80211_send_mgmt(ic,ic->ic_bss,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,0)==0);
    CHECK(enable_wire_offer(net));
    m=enable_response(ic,1,0);
    ieee80211_recv_mgmt_assoc_resp(ic,m,ic->ic_bss,IEEE80211_FC0_SUBTYPE_ASSOC_RESP,0,0);
    m_freem(m);
    queue_packet(&net->ec.ec_if.if_snd,ether_packet(0x30,0,0));
    enable_reenter_run=1;
    rtw88_net_state_work(&net->state_work,net);
    enable_reenter_run=0;
    CHECK(ic->ic_state==IEEE80211_S_SCAN && !net->work_pending);
    CHECK(net->ec.ec_if.if_snd.m!=NULL && !sc.sc_ra.configured);
    m_freem(net->ec.ec_if.if_snd.m);
    net->ec.ec_if.if_snd.m=net->ec.ec_if.if_snd.ifq_tail=NULL;
    net->ec.ec_if.if_snd.ifq_len=0;
    enable_destroy(net);
    CHECK(enable_allocations==enable_releases);
    printf("Native WME attach/offer/RUN: %u checks, %u failures\n",checks,failures);
    return failures!=0 || core_failures!=0;
}
