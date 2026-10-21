/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: native attach -> WME offer -> strict RUN apply -> AC consumer. */
#ifdef NDEBUG
#error The kernel service model requires assertion preconditions.
#endif
#include "enable-routing.inc"
static struct chanAccParams enable_ap_params;
static unsigned enable_run_callbacks,enable_first_producer;
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
        enable_run_callbacks++;
        if(ic->ic_ifp->if_snd.m!=NULL) {
            enable_first_producer++;
            rtw88_net_start(ic->ic_ifp);
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
    net->ec.ec_if.if_flags|=IFF_RUNNING;
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
int main(void)
{
    int result=routing_base_main();
    if(result!=0)return result;
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
    CHECK(enable_allocations==enable_releases);
    printf("Native WME attach/offer/RUN: %u checks, %u failures\n",checks,failures);
    return failures!=0 || core_failures!=0;
}
