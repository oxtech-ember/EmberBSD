/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual wrapper AC producer contract, AI-assisted. */
#define RTW88_START_CORE_ONLY 1
#define main core_contract_main
#define failures core_failures
#include "rtw88-start-contract.c"
#undef failures
#undef main
#undef RTW88_START_CORE_ONLY
#undef CHECK
#include "routing-constants.inc"
#define ieee80211_new_state model_shared_new_state
#define main shared_wme_main
#include "routing-model.inc"
#undef main
#undef ieee80211_new_state
#include <dev/ic/rtw88_net_policy.h>
#define IFF_RUNNING 1
#define IFF_OACTIVE 2
#define if_oerrors 1
#define BPF_D_OUT 1
#define M_GETCTX(m, type) ((type)(m)->m_pkthdr.ctx)
#define M_CLEARCTX(m) ((m)->m_pkthdr.ctx=NULL)
#undef IF_ENQUEUE
#include "routing-ifq.inc"
struct work { int unused; };
struct workqueue { unsigned queued; };
struct rtw88_net_rx {
    struct mbuf *m;
    uint64_t epoch;
    uint32_t tsf;
};
struct rtw88_net {
    struct { struct ifnet ec_if; } ec;
    struct rtw88_net_rx rx[RTW88_NET_RX_LIMIT];
    unsigned rx_head;
    struct {
        unsigned chained, first_short, beacon, chanmismatch, badchan;
        unsigned elem_missing, elem_toobig, elem_toosmall, nodealloc, mgtdiscard;
    } stats;
    struct ieee80211com ic;
    struct rtw88_softc *core;
    struct rtw88_net_policy policy;
    kmutex_t *lock;
    int work_pending;
    int scan, state_arg;
    enum ieee80211_state requested;
    unsigned channel;
    struct work state_work;
    struct workqueue *wq;
    unsigned tx_timer, tx_completed;
    void *softint;
    uint8_t txbuf[RTW88_TX_BUF_SIZE];
};
static unsigned route_errors, route_crypto, route_packets, route_rx;
static int route_no_node, route_crypto_fail, route_pullup_fail;
static unsigned scheduler_polls, scheduler_dequeues;
static int scheduler_null;
static struct mbuf *scheduler_selected;
#define if_opackets 2
#define ALTQ_IS_ENABLED(q) ((q)->altq_enabled)
#define TBR_IS_ENABLED(q) ((q)->tbr_enabled)
#define ALTDQ_REMOVE 1
#define ALTDQ_POLL 2
static struct mbuf *scheduler(struct queue *, int);
#define ALTQ_DEQUEUE(q,m) ((m)=scheduler((q),ALTDQ_REMOVE))
#define ALTQ_POLL(q,m) ((m)=scheduler((q),ALTDQ_POLL))
#define tbr_dequeue scheduler
#define mutex_owned(l) ((l)->held)
void mutex_enter(kmutex_t *l) { assert(!l->held);l->held=1; }
void mutex_exit(kmutex_t *l) { assert(l->held);l->held=0; }
static void if_statinc(struct ifnet *p, int counter) { (void)p;(void)counter;route_errors++; }
static void if_statadd(struct ifnet *p,int counter,unsigned n)
{ (void)p;(void)counter;route_packets+=n; }
static void bpf_mtap(void *p, struct mbuf *m, int d) { (void)p;(void)m;(void)d; }
#define bpf_mtap3 bpf_mtap
static void softint_schedule(void *p) { (void)p; }
static struct ieee80211_node *
ieee80211_find_txnode(struct ieee80211com *ic, const uint8_t *dest)
{ (void)dest;if(route_no_node)return NULL;ieee80211_ref_node(ic->ic_bss);return ic->ic_bss; }
static struct mbuf *m_pullup(struct mbuf *m, unsigned need)
{
    if (route_pullup_fail) { m_freem(m);return NULL; }
    if ((unsigned)m->m_len>=need) return m;
    if ((unsigned)m->m_pkthdr.len<need) { m_freem(m);return NULL; }
    struct mbuf *copy=newbuf((unsigned)m->m_pkthdr.len);
    assert(copy);
    copy->ac=m->ac;m_copydata(m,0,m->m_pkthdr.len,copy->m_data);m_freem(m);
    return copy;
}
static void *ieee80211_crypto_encap(struct ieee80211com *ic, struct ieee80211_node *ni, struct mbuf *m)
{ (void)ic;(void)ni;(void)m;route_crypto++;return route_crypto_fail?NULL:&route_crypto; }
#undef ieee80211_chan2ieee
#define ieee80211_chan2ieee(ic,ch) ((void)(ic),(void)(ch),1U)
static unsigned getticks(void) { return 1; }
static struct ieee80211_node *ieee80211_find_rxnode(struct ieee80211com *ic,const void *wh)
{ (void)wh;ieee80211_ref_node(ic->ic_bss);return ic->ic_bss; }
static void ieee80211_input(struct ieee80211com *ic,struct mbuf *m,
    struct ieee80211_node *ni,int rssi,uint32_t tsf)
{ (void)ic;(void)ni;(void)rssi;(void)tsf;route_rx++;m_freem(m); }
static void rtw88_net_start(struct ifnet *);
static int rtw88_net_newstate(struct ieee80211com *,enum ieee80211_state,int);
static void callout_stop(int *scan) { (void)scan; }
static void workqueue_enqueue(struct workqueue *q,struct work *w,void *cookie)
{ (void)w;(void)cookie;assert(q);q->queued++; }
static int route_new_state(struct ieee80211com *ic,int state,int arg)
{ return rtw88_net_newstate(ic,(enum ieee80211_state)state,arg); }
#define ieee80211_new_state route_new_state
#include "routing-native.inc"
#undef ieee80211_new_state
static struct mbuf *scheduler(struct queue *q,int op)
{
    assert(q->ifq_lock->held);
    if(op==ALTDQ_POLL) { scheduler_polls++;return q->m; }
    scheduler_dequeues++;
    if(scheduler_null)return NULL;
    struct mbuf *m=scheduler_selected?scheduler_selected:q->m;
    scheduler_selected=NULL;
    if(m==q->m) { struct mbuf *out;IF_DEQUEUE(q,out);assert(out==m); }
    else { assert(q->m && q->m->m_nextpkt==m);q->m->m_nextpkt=m->m_nextpkt;q->ifq_len--;m->m_nextpkt=NULL; }
    return m;
}
static struct mbuf *ether_packet(unsigned tos,int chained,int pae)
{
    struct mbuf *m=newbuf(sizeof(struct ether_header)+sizeof(struct ip));
    assert(m);
    struct ether_header *eh=(void *)m->m_data;
    eh->ether_dhost[0]=2;
    eh->ether_type=htons(pae?ETHERTYPE_PAE:ETHERTYPE_IP);
    m->m_data[sizeof(*eh)+1]=tos;
    if(chained) {
        struct mbuf *tail=newbuf(m->m_len-7);
        assert(tail);
        memcpy(tail->m_data,m->m_data+7,tail->m_len);
        m->m_len=7;m->m_next=tail;
    }
    return m;
}
static void queue_packet(struct queue *q,struct mbuf *m)
{ assert(q->m==NULL);q->m=q->ifq_tail=m;q->ifq_len=1; }
static void restart_core(struct rtw88_softc *sc)
{ CHECK(rtw88_stop(sc)==0);CHECK(rtw88_start(sc)==0); }
int main(void)
{
    int shared_result=shared_wme_main();
    if(shared_result!=0)return shared_result;
    (void)scheduler;
    (void)route_new_state;
    struct rtw88_softc sc;
    struct rtw88_net net={0};
    #define ifp net.ec.ec_if
    kmutex_t qlock={.initialized=1};
    ifp.if_snd.ifq_lock=&qlock;
    struct workqueue wq={0};
    struct ieee80211_node ni={0};
    struct ieee80211_channel ch={2412,0};
    kmutex_t lock={.initialized=1};
    reset_bus(&sc);if(!attach_ok(&sc))return 1;CHECK(rtw88_start(&sc)==0);
    net.core=&sc;net.lock=&lock;net.wq=&wq;net.policy.accepting=1;
    sc.sc_net_cookie=&net;sc.sc_tx_complete=rtw88_net_tx_complete;
    ifp.if_softc=&net;ifp.if_flags=IFF_RUNNING;
    net.ic.ic_ifp=&ifp;net.ic.ic_bss=&ni;net.ic.ic_curchan=&ch;
    net.ic.ic_state=IEEE80211_S_RUN;net.ic.ic_opmode=IEEE80211_M_STA;
    net.ic.ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;
    net.ic.ic_caps=IEEE80211_C_WME;net.ic.ic_flags=IEEE80211_F_WME;
    net.ic.ic_fragthreshold=4096;
    ni.ni_ic=&net.ic;ni.ni_chan=&ch;ni.ni_associd=1;
    ni.ni_flags=IEEE80211_NODE_QOS|IEEE80211_NODE_WME_VALID;
    ni.ni_wme_applied_chan=&ch;ni.ni_wme_applied_slot=20;ni.ni_bssid[0]=2;
    static const unsigned tos[4]={0,8,0x28,0x30};
    static const unsigned queues[4]={RTW88_TXQ_BE,RTW88_TXQ_BK,RTW88_TXQ_VI,RTW88_TXQ_VO};
    for(unsigned ac=0;ac<4;ac++) {
        struct mbuf *m=newbuf(sizeof(struct ether_header)+sizeof(struct ip));
        assert(m);struct ether_header *eh=(void *)m->m_data;
        eh->ether_type=htons(ETHERTYPE_IP);eh->ether_dhost[0]=2;
        m->m_data[sizeof(*eh)+1]=tos[ac];
        ifp.if_snd.m=m;ifp.if_snd.ifq_tail=m;ifp.if_snd.ifq_len=1;
        unsigned old=sc.sc_txq[queues[ac]].queued;
        rtw88_net_start(&ifp);
        CHECK(ifp.if_snd.m==NULL);
        CHECK(sc.sc_txq[queues[ac]].queued==old+1);
        CHECK(node_refs==1);
    }
    CHECK(route_errors==0);CHECK(route_crypto==0);
    for(unsigned ac=0;ac<4;ac++) {
        CHECK(rtw88_stop(&sc)==0);CHECK(rtw88_start(&sc)==0);
        uint8_t frame[26]={0x88,1};
        static const unsigned tid[4]={0,1,5,6};
        frame[24]=tid[ac];
        while(rtw88_tx_available(&sc,queues[ac]))
            CHECK(rtw88_tx_raw(&sc,queues[ac],frame,sizeof(frame))==0);
        struct mbuf *m=newbuf(sizeof(struct ether_header)+sizeof(struct ip));
        assert(m);struct ether_header *eh=(void *)m->m_data;
        eh->ether_type=htons(ETHERTYPE_IP);eh->ether_dhost[0]=2;
        m->m_data[sizeof(*eh)+1]=tos[ac];ifp.if_snd.m=m;ifp.if_snd.ifq_tail=m;ifp.if_snd.ifq_len=1;
        ifp.if_flags=IFF_RUNNING;
        uint16_t seq[17];memcpy(seq,ni.ni_txseqs,sizeof(seq));
        unsigned errors=route_errors,crypto=route_crypto;
        rtw88_net_start(&ifp);
        CHECK(ifp.if_snd.m==m);
        CHECK(memcmp(seq,ni.ni_txseqs,sizeof(seq))==0);
        CHECK(node_refs==1 && route_errors==errors && route_crypto==crypto);
        CHECK(ifp.if_flags&IFF_OACTIVE);
        if(ifp.if_snd.m) { m_freem(ifp.if_snd.m);ifp.if_snd.m=NULL; }
    }
    
    CHECK(rtw88_stop(&sc)==0);CHECK(rtw88_start(&sc)==0);
    for(unsigned pending=0;pending<2;pending++) {
        struct mbuf *m=newbuf(sizeof(struct ether_header)+sizeof(struct ip));
        assert(m);((struct ether_header *)(void *)m->m_data)->ether_type=htons(ETHERTYPE_IP);
        ifp.if_snd.m=m;ifp.if_snd.ifq_tail=m;ifp.if_snd.ifq_len=1;ifp.if_flags=IFF_RUNNING;
        net.policy.accepting=!pending;net.work_pending=pending;
        net.requested=IEEE80211_S_ASSOC;net.ic.ic_state=IEEE80211_S_RUN;
        ni.ni_wme_applied_slot=9;
        unsigned errors=route_errors,epoch=net.policy.epoch,queued=wq.queued;
        uint16_t seq[17];memcpy(seq,ni.ni_txseqs,sizeof(seq));
        rtw88_net_start(&ifp);
        CHECK(ifp.if_snd.m==m && node_refs==1);
        CHECK(memcmp(seq,ni.ni_txseqs,sizeof(seq))==0);
        CHECK(!net.policy.accepting && net.work_pending);
        CHECK(net.requested==(pending?IEEE80211_S_ASSOC:IEEE80211_S_INIT));
        CHECK(route_errors==errors+!pending);
        CHECK(net.policy.epoch==epoch+!pending);
        CHECK(wq.queued==queued+!pending);
        if(ifp.if_snd.m)
            m_freem(ifp.if_snd.m);
        ifp.if_snd.m=NULL;
    }

    /* Restore a valid, accepted peer after the deliberate quarantine cases. */
    net.policy.accepting=1;net.work_pending=0;net.policy.dying=0;
    ni.ni_wme_applied_slot=20;net.ic.ic_state=IEEE80211_S_RUN;
    ifp.if_flags=IFF_RUNNING;
    restart_core(&sc);
    /* Management keeps its own queue while data uses the selected AC. */
    struct mbuf *management=newbuf(26);
    assert(management);
    management->m_data[0]=0xb0;management->m_pkthdr.ctx=&ni;
    ieee80211_ref_node(&ni);net.ic.ic_mgtq.m=management;
    queue_packet(&ifp.if_snd,ether_packet(0x30,0,0));
    rtw88_net_start(&ifp);
    CHECK(net.ic.ic_mgtq.m==NULL && ifp.if_snd.m==NULL && node_refs==1);
    CHECK(sc.sc_txq[RTW88_TXQ_MGMT].queued==1 && sc.sc_txq[RTW88_TXQ_VO].queued==1);
    restart_core(&sc);
    for(unsigned ac=0;ac<4;ac++) {
        /* Whole wrapper, actual classifier and encap on a chained head. */
        struct mbuf *m=ether_packet(tos[ac],1,0);
        unsigned count=sc.sc_txq[queues[ac]].queued;
        queue_packet(&ifp.if_snd,m);rtw88_net_start(&ifp);
        CHECK(ifp.if_snd.m==NULL && node_refs==1);
        CHECK(sc.sc_txq[queues[ac]].queued==count+1);
    }
    restart_core(&sc);
    /* VLAN priority can elevate BE; STA ACM must downgrade the actual AC. */
    ni.ni_vlan=1|(6<<13);
    queue_packet(&ifp.if_snd,ether_packet(0,0,0));rtw88_net_start(&ifp);
    CHECK(sc.sc_txq[RTW88_TXQ_VO].queued==1);
    net.ic.ic_wme.wme_wmeChanParams.cap_wmeParams[WME_AC_VO].wmep_acm=1;
    queue_packet(&ifp.if_snd,ether_packet(0,0,0));rtw88_net_start(&ifp);
    CHECK(sc.sc_txq[RTW88_TXQ_VI].queued==1);
    net.ic.ic_wme.wme_wmeChanParams.cap_wmeParams[WME_AC_VO].wmep_acm=0;
    /* EAPOL remains non-QoS BE even when VLAN priority is VO. */
    unsigned be=sc.sc_txq[RTW88_TXQ_BE].queued;
    queue_packet(&ifp.if_snd,ether_packet(0x30,0,1));rtw88_net_start(&ifp);
    CHECK(sc.sc_txq[RTW88_TXQ_BE].queued==be+1);
    CHECK(node_refs==1);ni.ni_vlan=0;
    /* Capabilities OFF retains the legacy BE path and non-QoS sequence. */
    ni.ni_flags=0;net.ic.ic_caps=0;net.ic.ic_flags=0;
    be=sc.sc_txq[RTW88_TXQ_BE].queued;
    queue_packet(&ifp.if_snd,ether_packet(0x30,0,0));rtw88_net_start(&ifp);
    CHECK(sc.sc_txq[RTW88_TXQ_BE].queued==be+1);
    ni.ni_flags=IEEE80211_NODE_QOS|IEEE80211_NODE_WME_VALID;
    net.ic.ic_caps=IEEE80211_C_WME;net.ic.ic_flags=IEEE80211_F_WME;
    /* Malformed header, absent node and failed pullup/encap are counted once. */
    for(unsigned failure=0;failure<5;failure++) {
        restart_core(&sc);unsigned errors=route_errors;
        struct mbuf *m=ether_packet(0,0,0);
        if(failure==0)m->m_pkthdr.len=m->m_len=3;
        route_no_node=failure==1;
        if(failure==2) { m->m_pkthdr.len=m->m_len=sizeof(struct ether_header); }
        route_pullup_fail=failure==3;
        adjust_failure=failure==4;
        queue_packet(&ifp.if_snd,m);rtw88_net_start(&ifp);
        CHECK(ifp.if_snd.m==NULL && node_refs==1);
        CHECK(route_errors==errors+1);
        route_no_node=route_pullup_fail=adjust_failure=0;
    }
    /* Failed crypto adapter consumes one owned packet and balances its node. */
    restart_core(&sc);route_have_key=1;route_crypto_fail=1;
    struct cipher cipher={0};route_key.wk_cipher=&cipher;
    net.ic.ic_flags|=IEEE80211_F_PRIVACY;
    unsigned crypto=route_crypto,errors=route_errors;
    queue_packet(&ifp.if_snd,ether_packet(0,0,0));rtw88_net_start(&ifp);
    CHECK(route_crypto==crypto+1 && route_errors==errors+1 && node_refs==1);
    route_crypto_fail=0;
    /* Full VI queue: no encap sequence or crypto/PN consumer, then reclaim. */
    restart_core(&sc);
    uint8_t vi[26]={0x88,1};vi[24]=5;
    while(rtw88_tx_available(&sc,RTW88_TXQ_VI))
        CHECK(rtw88_tx_raw(&sc,RTW88_TXQ_VI,vi,sizeof(vi))==0);
    struct mbuf *blocked=ether_packet(0x28,0,0);
    queue_packet(&ifp.if_snd,blocked);
    uint16_t seq[17];memcpy(seq,ni.ni_txseqs,sizeof(seq));crypto=route_crypto;
    ifp.if_flags=IFF_RUNNING;rtw88_net_start(&ifp);
    CHECK(ifp.if_snd.m==blocked && (ifp.if_flags&IFF_OACTIVE));
    CHECK(route_crypto==crypto && memcmp(seq,ni.ni_txseqs,sizeof(seq))==0);
    CHECK(node_refs==1);
    /* Unrelated completion retries but must not consume the blocked VI head. */
    mutex_enter(&lock);rtw88_net_tx_complete(&net,1);mutex_exit(&lock);
    rtw88_net_softintr(&net);
    CHECK(ifp.if_snd.m==blocked && (ifp.if_flags&IFF_OACTIVE));
    CHECK(net.tx_timer==5 && route_crypto==crypto);
    f.tx_hw_rp[RTW88_TXQ_VI]=sc.sc_txq[RTW88_TXQ_VI].wp;
    set_hisr0(0x08);mutex_enter(&lock);CHECK(rtw88_intr(&sc)==1);mutex_exit(&lock);
    rtw88_net_softintr(&net);
    CHECK(ifp.if_snd.m==NULL && node_refs==1);
    CHECK(route_crypto==crypto+1 && sc.sc_txq[RTW88_TXQ_VI].queued==1);
    route_have_key=0;net.ic.ic_flags&=~IEEE80211_F_PRIVACY;
    /* Every outstanding AC retains the watchdog after another completion. */
    for(unsigned q=0;q<RTW88_NTXQ;q++) {
        restart_core(&sc);uint8_t frame[26]={0x88,1};
        static const unsigned tids[5]={0,0,1,5,6};
        if(q==RTW88_TXQ_MGMT)frame[0]=0xb0;
        frame[24]=tids[q];CHECK(rtw88_tx_raw(&sc,q,frame,sizeof(frame))==0);
        net.tx_timer=1;mutex_enter(&lock);rtw88_net_tx_complete(&net,1);mutex_exit(&lock);
        rtw88_net_softintr(&net);CHECK(net.tx_timer==5);
    }
#ifdef ALTQ
    for(unsigned shaped=0;shaped<2;shaped++) {
        restart_core(&sc);ifp.if_flags=IFF_RUNNING;
        ifp.if_snd.altq_enabled=!shaped;ifp.if_snd.tbr_enabled=shaped;
        struct mbuf *first=ether_packet(0x08,0,0),*selected=ether_packet(0x30,0,0);
        queue_packet(&ifp.if_snd,first);first->m_nextpkt=selected;
        ifp.if_snd.ifq_tail=selected;ifp.if_snd.ifq_len=2;
        scheduler_selected=selected;
        struct mbuf *polled;
        unsigned polls=scheduler_polls;
        IFQ_POLL(&ifp.if_snd,polled);
        CHECK(polled==first && scheduler_polls==polls+1);
        /* A full unrelated AC gates all scheduler dequeue/crypto operations. */
        uint8_t full[26]={0x88,1};full[24]=5;
        while(rtw88_tx_available(&sc,RTW88_TXQ_VI))
            CHECK(rtw88_tx_raw(&sc,RTW88_TXQ_VI,full,sizeof(full))==0);
        unsigned crypto_before=route_crypto,deq_before=scheduler_dequeues;
        rtw88_net_start(&ifp);
        CHECK(ifp.if_snd.m==first && first->m_nextpkt==selected);
        CHECK((ifp.if_flags&IFF_OACTIVE) && scheduler_dequeues==deq_before);
        CHECK(route_crypto==crypto_before && node_refs==1);
        restart_core(&sc);ifp.if_flags=IFF_RUNNING;
        /* Scheduler could return a different head than any earlier poll. */
        scheduler_null=1;unsigned deqs=scheduler_dequeues;
        rtw88_net_start(&ifp);CHECK(ifp.if_snd.m==first && first->m_nextpkt==selected);
        CHECK(scheduler_dequeues==deqs+1 && node_refs==1);
        scheduler_null=0;rtw88_net_start(&ifp);
        CHECK(sc.sc_txq[RTW88_TXQ_VO].queued==1 && node_refs==1);
        scheduler_selected=NULL;
        /* Remaining actual FIFO head is BK, not the consumed VO node/AC. */
        rtw88_net_start(&ifp);CHECK(ifp.if_snd.m==NULL);
        CHECK(sc.sc_txq[RTW88_TXQ_BK].queued==1);
    }
    ifp.if_snd.altq_enabled=ifp.if_snd.tbr_enabled=0;
#endif
    /* Stop/quarantine never consumes an Ethernet head or a node reference. */
    for(unsigned closed=0;closed<3;closed++) {
        restart_core(&sc);ifp.if_flags=IFF_RUNNING;
        struct mbuf *m=ether_packet(0,0,0);queue_packet(&ifp.if_snd,m);
        net.policy.accepting=closed==2;net.policy.dying=closed==1;
        if(closed==2)ifp.if_flags=0;
        unsigned errors=route_errors;
        rtw88_net_start(&ifp);
        CHECK(ifp.if_snd.m==m && node_refs==1 && route_errors==errors);
        struct mbuf *other=ether_packet(0,0,0);
#ifdef ROUTING_NATIVE_QUEUE_HELPERS
        CHECK(rtw88_net_tx_dequeue_same(&ifp,other)==NULL && ifp.if_snd.m==m);
#endif
        m_freem(other);m_freem(m);ifp.if_snd.m=NULL;
    }
    net.policy.accepting=1;net.policy.dying=0;ifp.if_flags=IFF_RUNNING;
    restart_core(&sc);net.tx_completed=0;
    mutex_enter(&lock);rtw88_net_tx_complete(&net,1);mutex_exit(&lock);
    CHECK(net.tx_completed==1);
    CHECK(rtw88_stop(&sc)==0);CHECK(rtw88_core_detach(&sc)==0);
    printf("Native four-AC wrapper: %u checks, %u failures\n",checks,failures);
    printf("Actual core: %u assertions, %u failures\n",assertions,core_failures);
    return failures!=0 || core_failures!=0;
}
