/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD native state/default EDCA regression, AI-assisted. */
/* Complete native state consumer plus real core; kernel scheduling is modeled. */
#include <stdarg.h>
#include <stdbool.h>
/* BSD sys/types.h alias used by the unchanged shared channel functions. */
typedef unsigned int u_int;
#include <dev/ic/rtw88_net_policy.h>
#include "native-state-types.inc"
#ifndef __UNCONST
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
#endif
const struct ieee80211_channel ieee80211_channel_anyc={0,0};
#define setbit(p,n) ((p)[(n)/8] |= (uint8_t)(1U<<((n)%8)))
#define clrbit(p,n) ((p)[(n)/8] &= (uint8_t)~(1U<<((n)%8)))
#define isset(p,n) ((p)[(n)/8] & (1U<<((n)%8)))
#define isclr(p,n) (!isset(p,n))
#define IFF_UP 1
#define IFF_RUNNING 2
#define IFF_OACTIVE 4
struct ifnet { void *if_softc; int if_flags, if_timer; unsigned if_mtu; };
typedef int (*ifm_change_cb_t)(struct ifnet *);
typedef void (*ifm_stat_cb_t)(struct ifnet *, void *);
struct ifmedia_entry { unsigned ifm_media; };
struct ifmedia {
	unsigned ifm_media, ifm_mask, ifm_legacy;
	struct ifmedia_entry *ifm_cur, *selected;
	int ifm_list;
	ifm_change_cb_t ifm_change;
	ifm_stat_cb_t ifm_status;
	kmutex_t *ifm_lock;
};
struct mbuf { struct mbuf *next; void *ctx; };
struct ifqueue { struct mbuf *head; };
struct ieee80211_node {
	struct ieee80211_ht20_node ni_ht20;
	struct ieee80211_rxba_state ni_rxba;
	struct { unsigned rsn_ucastcipher; } ni_rsn;
	unsigned references;
	struct ieee80211com *ni_ic;
	unsigned ni_wme_attempt;
	struct ieee80211_rateset ni_rates;
	struct ieee80211_channel *ni_wme_applied_chan, *ni_chan;
	unsigned ni_flags, ni_associd, ni_wme_applied_slot, ni_fails;
	uint8_t ni_bssid[6],ni_macaddr[6];
};
struct ieee80211com {
	struct { void (*ia_detach)(struct ieee80211com *); } *ic_auth;
	const struct ieee80211_rxba_ops *ic_rxba_ops;
	uint64_t ic_rxba_serial;
	int (*ic_ht20_admit)(struct ieee80211com *, struct ieee80211_ht20_profile *);
	int (*ic_vht_admit)(struct ieee80211com *, struct ieee80211_vht_cap *);
	struct ifqueue ic_mgtq;
	uint8_t ic_chan_active[32], ic_chan_scan[32];
	struct { unsigned nt_scangen,nt_scan_epoch; } ic_scan,ic_sta;
	struct ifnet *ic_ifp;
	struct ieee80211_node *ic_bss;
	struct ieee80211_channel *ic_curchan,*ic_ibss_chan,*ic_des_chan;
	struct ieee80211_channel ic_channels[256];
	struct ieee80211_wme_state ic_wme;
	struct ifmedia ic_media;
	unsigned ic_caps, ic_flags, ic_flags_ext, ic_opmode, ic_modecaps;
	unsigned ic_rtsthreshold, ic_fragthreshold;
	unsigned ic_nonerpsta,ic_longslotsta,ic_mgt_timer;
	void (*ic_updateslot)(struct ifnet *);
	unsigned ic_roaming;
	struct { unsigned is_scan_active,is_scan_passive; } ic_stats;
	enum ieee80211_phymode ic_curmode;
	int ic_fixed_rate, ic_state;
	struct ieee80211_rateset ic_sup_rates[IEEE80211_MODE_MAX];
};
struct work { int unused; };
struct workqueue { int queued; };
typedef int callout_t;
struct rtw88_net {
	struct { struct ifnet ec_if; } ec;
	struct ieee80211com ic;
	struct rtw88_softc *core;
	device_t dev;
	kmutex_t *lock;
	struct workqueue *wq;
	struct work state_work;
	void *softint;
	callout_t scan;
	struct rtw88_net_policy policy;
	int work_pending, reset_required;
	int tx_quiet, quiet_error;
	unsigned tx_timer, tx_completed;
	enum ieee80211_state requested;
	unsigned channel, configured_media;
	struct rtw88_channel_tuple pending_channel;
	int state_arg;
	int (*parent_state)(struct ieee80211com *, enum ieee80211_state, int);
};
static unsigned state_mbuf_frees, state_mbuf_free_while_master;
#define IF_DEQUEUE(q,m) do { (m)=(q)->head; if ((m)!=NULL) (q)->head=(m)->next; } while (0)
#define M_GETCTX(m,type) ((type)(m)->ctx)
#define M_SETCTX(m,value) ((m)->ctx=(value))
static void ieee80211_free_node(struct ieee80211_node *ni)
{ assert(ni!=NULL && ni->references>0); ni->references--; }
static void m_freem(struct mbuf *m)
{ assert(m->ctx==NULL); state_mbuf_frees++;
  state_mbuf_free_while_master+=f.master!=0; free(m); }
#include "native-drain.inc"
static kmutex_t state_lock;
static unsigned state_parent_tx, state_parent_init, state_expected_cw,
    state_expected_slot, state_expected_channel, state_fail_io,
    state_after_effect, state_io_count, state_io_armed;
static int state_media_error, state_real_media;
static int state_shared_parent(struct ieee80211com *,enum ieee80211_state,int);
static int state_shared_ieee80211_media_change(struct ifnet *);
static int state_shared_ieee80211_setmode(struct ieee80211com *,enum ieee80211_phymode);
static unsigned state_epoch_change;
static unsigned state_retune_failure;
static struct rtw88_net *state_current_net;
static unsigned state_channel_calls, state_station_calls, state_negotiated;
static uint32_t state_expected_words[4];
static enum ieee80211_phymode state_media_mode;
static char state_diag[512];
void
mutex_enter(kmutex_t *lock)
{
	assert(lock->initialized && !lock->held);
	lock->held=1;
	if (state_epoch_change && --state_epoch_change == 0) {
		/* A newer request wins between worker preparation and commit. */
		struct rtw88_net *net = state_current_net;
		assert(net && net->lock == lock);
		(void)rtw88_net_request(&net->policy);
		net->channel = 36;
			net->pending_channel =
			    (struct rtw88_channel_tuple){36, 36, 20};
		net->requested = IEEE80211_S_SCAN;
		net->ic.ic_curchan = &net->ic.ic_channels[36];
		net->ic.ic_curchan->ic_flags = IEEE80211_CHAN_A;
	}
}
void
mutex_exit(kmutex_t *lock)
{
	assert(lock->initialized && lock->held);
	lock->held=0;
}
static unsigned state_beacon_lock;
#define IEEE80211_BEACON_LOCK(ic) do { (void)(ic); assert(!state_lock.held && !state_beacon_lock); state_beacon_lock=1; } while (0)
#define IEEE80211_BEACON_UNLOCK(ic) do { (void)(ic); assert(state_beacon_lock); state_beacon_lock=0; } while (0)
unsigned ieee80211_chan2ieee(struct ieee80211com *,struct ieee80211_channel *);
#define mstohz(ms) (ms)
static unsigned state_ticks=1;
static unsigned getticks(void) { return state_ticks; }
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
static inline int kpause(const char *name,bool intr,int ticks,kmutex_t *lock)
{
    (void)name;(void)intr;assert(lock==NULL && !state_lock.held && ticks>0);
    state_ticks+=(unsigned)ticks;
    if(strcmp(name,"rtwdrn")==0) {
        assert(state_current_net && state_current_net->core==f.sc);
        for(unsigned q=0;q<RTW88_NTXQ;q++)f.tx_hw_rp[q]=f.sc->sc_txq[q].wp;
        f.mmio[0xb4]=0x7c;
        mutex_enter(&state_lock);assert(rtw88_intr(f.sc)==1);mutex_exit(&state_lock);
    }
    return EWOULDBLOCK;
}
static void callout_stop(callout_t *c) { (void)c; }
static void callout_schedule(callout_t *c, unsigned n) { (void)c; (void)n; }
static void softint_schedule(void *cookie) { (void)cookie; }
static void workqueue_enqueue(struct workqueue *q, struct work *w, void *arg)
{
	(void)w; (void)arg; assert(!state_lock.held); q->queued++;
}
void
aprint_error_dev(device_t dev, const char *format, ...)
{
	va_list ap;
	(void)dev;
	va_start(ap,format);
	vsnprintf(state_diag,sizeof(state_diag),format,ap);
	va_end(ap);
}
static int
state_read1(void *cookie, uint32_t reg, uint8_t *value)
{
	if (state_io_armed) {
		assert(state_lock.held);
		state_io_count++;
		if (state_io_count==state_fail_io && !state_after_effect) return EIO;
	}
	int e=read_1(cookie,reg,value);
	return e==0 && state_io_armed && state_io_count==state_fail_io ? EIO:e;
}
static int
state_write1(void *cookie, uint32_t reg, uint8_t value)
{
	if (state_io_armed) {
		assert(state_lock.held);
		state_io_count++;
		if (state_io_count==state_fail_io && !state_after_effect) return EIO;
	}
	int e=write_1(cookie,reg,value);
	return e==0 && state_io_armed && state_io_count==state_fail_io ? EIO:e;
}
static int
state_write4(void *cookie, uint32_t reg, uint32_t value)
{
	if (state_io_armed) {
		assert(state_lock.held);
		state_io_count++;
		if (state_io_count==state_fail_io && !state_after_effect) return EIO;
	}
	int e=write_4(cookie,reg,value);
    if(state_retune_failure && reg==0x18a0)return EIO;
	return e==0 && state_io_armed && state_io_count==state_fail_io ? EIO:e;
}
/* Arm only bus fault injection, then execute the actual core operation. */
int
state_edca_apply(struct rtw88_softc *sc, const struct rtw88_edca_params *p,
    unsigned slot)
{
	assert(state_lock.held);
	assert(state_current_net && !state_current_net->policy.accepting);
	state_io_armed=1;
	state_io_count=0;
	int e=rtw88_edca_apply(sc,p,slot);
	state_io_armed=0;
	return e;
}
static int rtw88_net_newstate(struct ieee80211com *, enum ieee80211_state, int);
static int
ieee80211_new_state(struct ieee80211com *ic, enum ieee80211_state state, int arg)
{
	return rtw88_net_newstate(ic,state,arg);
}
static int
ieee80211_media_change(struct ifnet *ifp)
{
	struct rtw88_net *net=ifp->if_softc;
	if (state_real_media) return state_shared_ieee80211_media_change(ifp);
	if (state_media_error==0 || state_media_error==ENETRESET)
		net->ic.ic_curmode=state_media_mode;
	return state_media_error;
}
/* Shared media/mode policy is modeled here; the media contract executes it. */
static int
ieee80211_setmode(struct ieee80211com *ic, enum ieee80211_phymode mode)
{
	assert(!state_lock.held);
	if (state_real_media) return state_shared_ieee80211_setmode(ic,mode);
	ic->ic_curmode=mode;
	return 0;
}
static void
rtw88_net_ht20_publish(struct rtw88_net *net,
    const struct ieee80211_ht20_profile *profile)
{
	/* State tests model only fail-closed withdrawals; publication is tested
	 * against the actual atomic policy in the dedicated readiness contract. */
	CHECK(net != NULL);
	CHECK(profile == NULL);
}

static void
rtw88_net_stop(struct ifnet *ifp, int disable)
{
	struct rtw88_net *net=ifp->if_softc;
	(void)disable;
	ifp->if_flags &= ~(IFF_RUNNING|IFF_OACTIVE);
	(void)rtw88_net_request(&net->policy);
	if (state_real_media) (void)state_shared_parent(&net->ic,IEEE80211_S_INIT,-1);
	(void)rtw88_stop(net->core);
}
static int
state_channel_apply(struct rtw88_softc *sc,
	const struct rtw88_channel_tuple *channel)
{
	state_channel_calls++;
	return rtw88_channel_apply(sc,channel);
}
static int
state_station(struct rtw88_softc *sc, const uint8_t *bssid, unsigned aid,
    unsigned basic, unsigned slot, int linked)
{
	state_station_calls++;
	return rtw88_station_config(sc,bssid,aid,basic,slot,linked);
}
#define rtw88_channel_apply state_channel_apply
#define rtw88_station_config state_station
#define rtw88_edca_apply state_edca_apply
#ifdef RTW88_STATE_RA_OBSERVE
static int state_ra_configure(struct rtw88_softc *, const struct rtw88_ra_config *);
#define rtw88_ra_configure state_ra_configure
#endif
#define IEEE80211_CIPHER_AES_CCM 3
#define IEEE80211_ADDR_COPY(a,b) memcpy(a,b,6)
#define IEEE80211_ADDR_EQ(a,b) (memcmp(a,b,6)==0)
#include "native-rxba-lifecycle.inc"
#ifndef mutex_owned
#define mutex_owned(l) ((l)->held)
#endif
#include "native-state.inc"
#ifdef RTW88_STATE_RA_OBSERVE
#undef rtw88_ra_configure
#endif
#undef rtw88_edca_apply
#undef rtw88_channel_apply
#undef rtw88_station_config
/* Actual media initialization and default selection. List lookup is modeled. */
#define KM_SLEEP 1
#define TAILQ_INIT(head) ((void)(head))
static kmutex_t state_media_lock;
static kmutex_t *mutex_obj_alloc(int type, int ipl)
{
	(void)type;(void)ipl;state_media_lock=(kmutex_t){.initialized=1};
	return &state_media_lock;
}
static void ifmedia_lock(struct ifmedia *m) { mutex_enter(m->ifm_lock); }
static void ifmedia_unlock(struct ifmedia *m) { mutex_exit(m->ifm_lock); }
static struct ifmedia_entry *
ifmedia_match_locked(struct ifmedia *m, int word, unsigned mask)
{
	assert(m->ifm_lock->held);
	return m->selected && ((m->selected->ifm_media^(unsigned)word)&~mask)==0?
	    m->selected:NULL;
}
void *kmem_zalloc(size_t n, int flags) { (void)flags;return calloc(1,n); }
void kmem_free(void *p, size_t n) { (void)n;free(p); }
static void
ifmedia_add_entry(struct ifmedia *m, int word, int data, void *aux,
    struct ifmedia_entry *entry)
{
	(void)data;(void)aux;assert(entry);entry->ifm_media=word;m->selected=entry;
}
#define panic(text) do { (void)(text);abort(); } while (0)
#include "native-media.inc"
#undef panic
static struct ifmedia_entry state_default_entry;
static void
ieee80211_media_init(struct ieee80211com *ic, ifm_change_cb_t change,
    ifm_stat_cb_t status)
{
	ifmedia_init_with_lock(&ic->ic_media,0,change,status,NULL);
	CHECK(ic->ic_media.ifm_media==IFM_NONE && ic->ic_media.ifm_cur==NULL);
	state_default_entry.ifm_media=IFM_IEEE80211;
	ic->ic_media.selected=&state_default_entry;
	ifmedia_set(&ic->ic_media,IFM_IEEE80211);
	CHECK(ic->ic_media.ifm_media==IFM_NONE);
	CHECK(ic->ic_media.ifm_cur==&state_default_entry);
}
static void ieee80211_media_status(struct ifnet *ifp, void *request)
{ (void)ifp;(void)request; }
static void if_register(struct ifnet *ifp)
{
	struct rtw88_net *net=ifp->if_softc;
	CHECK(net->configured_media==IFM_IEEE80211);
}
/* WME initialization is an external service in this state/media model.
 * The enable contract executes its actual implementation and attach ordering.
 */
static void ieee80211_wme_initparams(struct ieee80211com *ic)
{ CHECK(ic != NULL); }
static void state_attach_media(struct rtw88_net *net)
{
	struct ieee80211com *ic=&net->ic;
	struct ifnet *ifp=&net->ec.ec_if;
#include "native-media-attach.inc"
}
#define IEEE80211_DPRINTF(...) ((void)0)
#define IASSERT(c,msg) assert(c)
static void if_printf(struct ifnet *ifp,const char *fmt,...)
{ (void)ifp;(void)fmt; }
static struct ieee80211com *ieee80211_find_instance(struct ifnet *ifp)
{ return &((struct rtw88_net *)ifp->if_softc)->ic; }
static void ieee80211_init_link_state(struct ieee80211com *ic) { (void)ic; }
static void ieee80211_wme_initparams(struct ieee80211com *);
void ieee80211_set_shortslottime(struct ieee80211com *,int);
int ieee80211_media2rate(int);
int ieee80211_next_scan(struct ieee80211com *);
static unsigned state_scan_end,state_scan_free;
static void ieee80211_free_allnodes(void *table) { (void)table;state_scan_free++; }
static void ieee80211_end_scan(struct ieee80211com *ic) { (void)ic;state_scan_end++; }
#include "native-band-shared.inc"
static void state_driver_channels(struct ieee80211com *ic)
{
#include "native-band-driver.inc"
}
static unsigned state_scan_probe,state_scan_cancel,state_bss_reset,state_leave;
static void ieee80211_probe_curchan(struct ieee80211com *ic,int force)
{ (void)ic;(void)force;state_scan_probe++; }
static void ieee80211_cancel_scan(struct ieee80211com *ic)
{ (void)ic;state_scan_cancel++; }
static void state_band_sta_leave(struct ieee80211com *ic,struct ieee80211_node *ni)
{ (void)ic;(void)ni;state_leave++; }
static void ieee80211_reset_bss(struct ieee80211com *ic)
{
 state_bss_reset++;ic->ic_bss->ni_rates.rs_nrates=0;
 ic->ic_bss->ni_associd=0;ic->ic_bss->ni_chan=IEEE80211_CHAN_ANYC;
}
static struct ieee80211_node *ieee80211_find_node(void *table,const uint8_t *mac)
{ (void)table;(void)mac;return NULL; }
static void ieee80211_unref_node(struct ieee80211_node **ni) { assert(*ni); }
static void ieee80211_create_ibss(struct ieee80211com *ic,struct ieee80211_channel *ch)
{ (void)ic;(void)ch;abort(); }
#define IEEE80211_SEND_MGMT(...) ((void)0)
#define IEEE80211_NO_HOSTAP 1
#define ieee80211_sta_leave state_band_sta_leave
static int state_shared_parent(struct ieee80211com *ic,enum ieee80211_state nstate,int arg)
{
 struct ieee80211_node *ni;
 enum ieee80211_state ostate;
#include "native-band-parent.inc"
}
#undef ieee80211_sta_leave
static int
state_parent(struct ieee80211com *ic, enum ieee80211_state state, int arg)
{
	struct rtw88_net *net=ic->ic_ifp->if_softc;
	(void)arg;
	CHECK(!state_lock.held && !state_beacon_lock);
	if (state==IEEE80211_S_INIT) {
		state_parent_init++;
		ieee80211_wme_reset(ic->ic_bss);
		return 0;
	}
	CHECK(net->policy.accepting);
	CHECK(net->core->sc_channel==state_expected_channel);
	unsigned sifs=state_expected_channel>14?16:10;
	uint32_t expected=(uint32_t)10<<12 | state_expected_cw<<8 |
	    (sifs+2*state_expected_slot);
	CHECK(mmio32(0x508)==(state_negotiated?state_expected_words[0]:expected));
	CHECK(mmio32(0x50c)==(state_negotiated?state_expected_words[1]:expected));
	CHECK(mmio32(0x504)==(state_negotiated?state_expected_words[2]:expected));
	CHECK(mmio32(0x500)==(state_negotiated?state_expected_words[3]:expected));
	CHECK(rtw88_tx_raw(net->core,RTW88_TXQ_MGMT,bcast_frame,
	    sizeof(bcast_frame))==0);
	state_parent_tx++;
	return 0;
}
/* A RUN request has a joined peer; SCAN tests retain their stale-mode inputs. */
static void
state_legacy_peer(struct rtw88_net *net, struct ieee80211_node *ni,
    unsigned channel)
{
	static const uint8_t legacy[] = { 2, 4, 11, 22, 12, 18, 24, 36,
	    48, 72, 96, 108 };
	struct ieee80211_rateset *rs;
	net->ic.ic_curmode=channel>14?IEEE80211_MODE_11A:IEEE80211_MODE_11G;
	rs=&net->ic.ic_sup_rates[net->ic.ic_curmode];
	rs->rs_nrates=channel>14?8:12;
	memcpy(rs->rs_rates,legacy+(channel>14?4:0),rs->rs_nrates);
	ni->ni_rates=*rs;
	for (unsigned i=0;i<ni->ni_rates.rs_nrates;i++)
		if (channel>14?(i==0||i==2||i==4):i<4)
			ni->ni_rates.rs_rates[i]|=IEEE80211_RATE_BASIC;
	ni->ni_chan=net->ic.ic_curchan;
}

static void
test_native_state_defaults(void)
{
	static const struct {
		unsigned channel, slot, cw, media;
		enum ieee80211_phymode stale_mode;
		int fixed;
	} cases[] = {
		{1,20,4,IFM_IEEE80211,IEEE80211_MODE_11A,IEEE80211_FIXED_RATE_NONE},
		{1,9,4,IFM_IEEE80211,IEEE80211_MODE_11A,IEEE80211_FIXED_RATE_NONE},
		{36,9,4,IFM_IEEE80211,IEEE80211_MODE_11B,IEEE80211_FIXED_RATE_NONE},
		{1,20,5,IFM_IEEE80211|IFM_IEEE80211_11B,IEEE80211_MODE_11G,IEEE80211_FIXED_RATE_NONE},
		{1,20,5,IFM_IEEE80211,IEEE80211_MODE_11B,0},
	};
	for (unsigned i=0;i<__arraycount(cases);i++) {
		struct rtw88_softc sc;
		struct rtw88_net net={0};
		struct ieee80211_node ni={0};
		struct workqueue wq={0};
		struct rtw88_bus_ops bus=ops;
		reset_bus(&sc);
		if (!attach_ok(&sc)) continue;
		state_lock=(kmutex_t){.initialized=1};
		net.core=&sc;net.lock=&state_lock;net.wq=&wq;
		net.ec.ec_if.if_softc=&net;
		net.ec.ec_if.if_flags=IFF_UP;
		net.ic.ic_ifp=&net.ec.ec_if;net.ic.ic_bss=&ni;ni.ni_ic=&net.ic;
		net.ic.ic_opmode=IEEE80211_M_STA;
		net.ic.ic_curmode=cases[i].stale_mode;
		net.ic.ic_fixed_rate=cases[i].fixed;
		net.ic.ic_curchan=&net.ic.ic_channels[cases[i].channel];
		net.ic.ic_curchan->ic_freq=cases[i].channel>14?5000+5*cases[i].channel:2407+5*cases[i].channel;
		setbit(net.ic.ic_chan_active,cases[i].channel);
		net.ic.ic_curchan->ic_flags=cases[i].channel>14?IEEE80211_CHAN_A:IEEE80211_CHAN_B|IEEE80211_CHAN_G;
		net.configured_media=cases[i].media;
		net.parent_state=state_parent;
		state_expected_channel=cases[i].channel;
		state_expected_slot=cases[i].slot;
		state_expected_cw=cases[i].cw;
		state_parent_tx=state_parent_init=state_io_count=state_fail_io=state_negotiated=0;
		state_current_net=&net;
		CHECK(rtw88_net_init(&net.ec.ec_if)==0);
		CHECK(wq.queued==1 && net.work_pending && !net.policy.accepting);
		if (cases[i].slot==9 && cases[i].channel==1) {
			net.ic.ic_flags|=IEEE80211_F_SHSLOT;
			net.requested=IEEE80211_S_RUN;
			state_legacy_peer(&net,&ni,cases[i].channel);
			ni.ni_associd=1;ni.ni_bssid[0]=2;
		}
		bus.read_1=state_read1;bus.write_1=state_write1;bus.write_4=state_write4;
		sc.sc_ops=&bus;
		rtw88_net_state_work(&net.state_work,&net);
		CHECK(state_parent_tx==1 && state_parent_init==0);
		CHECK(state_io_count==7);
		CHECK(!net.work_pending && net.policy.accepting);
		CHECK(rtw88_stop(&sc)==0);
		CHECK(rtw88_core_detach(&sc)==0);
		CHECK(f.dma_allocs==0);
		state_current_net=NULL;
	}
	{
		struct rtw88_net net={0};
		net.lock=&state_lock;net.ec.ec_if.if_softc=&net;
		net.ic.ic_media.ifm_media=IFM_IEEE80211|IFM_IEEE80211_11B;
		state_media_error=ENETRESET;state_media_mode=IEEE80211_MODE_11B;
		CHECK(rtw88_net_media_change(&net.ec.ec_if)==0);
		CHECK(net.configured_media==net.ic.ic_media.ifm_media);
		state_media_error=EINVAL;
		net.ic.ic_media.ifm_media=IFM_IEEE80211|IFM_IEEE80211_11A;
		CHECK(rtw88_net_media_change(&net.ec.ec_if)==EINVAL);
		CHECK(net.configured_media==(IFM_IEEE80211|IFM_IEEE80211_11B));
		net.policy.dying=1;
		net.ec.ec_if.if_flags=IFF_UP;
		state_media_error=ENETRESET;
		CHECK(rtw88_net_media_change(&net.ec.ec_if)==EOPNOTSUPP);
		CHECK(net.configured_media==(IFM_IEEE80211|IFM_IEEE80211_11B));
	}
}
struct state_fixture {
	struct rtw88_softc sc;
	struct rtw88_net net;
	struct ieee80211_node ni;
	struct workqueue wq;
	struct rtw88_bus_ops bus;
};
static int
state_fixture_start(struct state_fixture *s, unsigned channel, unsigned slot)
{
	memset(s,0,sizeof(*s));
	reset_bus(&s->sc);
	if (!attach_ok(&s->sc)) return 0;
	state_lock=(kmutex_t){.initialized=1};
	s->net.core=&s->sc;s->net.lock=&state_lock;s->net.wq=&s->wq;
	s->net.ec.ec_if.if_softc=&s->net;s->net.ec.ec_if.if_flags=IFF_UP;
	s->net.ic.ic_ifp=&s->net.ec.ec_if;s->net.ic.ic_bss=&s->ni;
	s->ni.ni_ic=&s->net.ic;
	s->net.ic.ic_opmode=IEEE80211_M_STA;
	s->net.ic.ic_curmode=IEEE80211_MODE_11G;
	s->net.ic.ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;
	s->net.ic.ic_curchan=&s->net.ic.ic_channels[channel];
	s->net.ic.ic_curchan->ic_freq=channel>14?5000+5*channel:2407+5*channel;
	setbit(s->net.ic.ic_chan_active,channel);
	s->net.ic.ic_curchan->ic_flags=channel>14?IEEE80211_CHAN_A:
	    IEEE80211_CHAN_B|IEEE80211_CHAN_G;
	state_attach_media(&s->net);
	s->net.ic.ic_media.ifm_media=0xdead; /* Worker must use owned snapshot. */
	s->net.parent_state=state_parent;
	state_expected_channel=channel;state_expected_slot=slot;state_expected_cw=4;
	state_parent_tx=state_parent_init=state_io_count=state_fail_io=state_after_effect=0;
	state_channel_calls=state_station_calls=state_negotiated=0;
	state_current_net=&s->net;
	memset(state_diag,0,sizeof(state_diag));
	CHECK(rtw88_net_init(&s->net.ec.ec_if)==0);
	s->bus=ops;s->bus.read_1=state_read1;s->bus.write_1=state_write1;
	s->bus.write_4=state_write4;s->sc.sc_ops=&s->bus;
	return 1;
}
static void
state_fixture_finish(struct state_fixture *s)
{
	CHECK(rtw88_stop(&s->sc)==0);
	CHECK(rtw88_core_detach(&s->sc)==0);
	CHECK(f.dma_allocs==0);
	state_current_net=NULL;
}
static void
test_native_state_edca_failures(void)
{
	for (unsigned after=0;after<2;after++)
	for (unsigned fail=1;fail<=7;fail++) {
		struct state_fixture s;
		if (!state_fixture_start(&s,1,20)) continue;
		state_fail_io=fail;state_after_effect=after;
		rtw88_net_state_work(&s.net.state_work,&s.net);
		CHECK(state_parent_tx==0 && state_parent_init==1);
		CHECK(!s.net.policy.accepting && !s.net.work_pending);
		CHECK((s.net.ec.ec_if.if_flags&(IFF_RUNNING|IFF_OACTIVE))==0);
		CHECK(s.sc.sc_edca_error==EIO && !s.sc.sc_running);
		CHECK(strstr(state_diag,"stage 4 ")!=NULL);
		CHECK(state_channel_calls==1 && state_station_calls==1);
		uint8_t frame[24]={0x08,0x02};
		CHECK(rtw88_tx_raw(&s.sc,RTW88_TXQ_BE,frame,sizeof(frame))==EIO);
		if (after==1 && fail==7) {
			state_fail_io=0;
			CHECK(rtw88_net_init(&s.net.ec.ec_if)==0);
			CHECK(!s.net.policy.accepting && s.sc.sc_edca_error==0);
			rtw88_net_state_work(&s.net.state_work,&s.net);
			CHECK(state_parent_tx==1 && s.net.policy.accepting);
		}
		state_fixture_finish(&s);
	}
}
static int
state_wme_params_equal(const struct wmeParams *a, const struct wmeParams *b)
{
	return a->wmep_acm == b->wmep_acm &&
	    a->wmep_aifsn == b->wmep_aifsn &&
	    a->wmep_logcwmin == b->wmep_logcwmin &&
	    a->wmep_logcwmax == b->wmep_logcwmax &&
	    a->wmep_txopLimit == b->wmep_txopLimit &&
	    a->wmep_noackPolicy == b->wmep_noackPolicy;
}

static int
state_chan_params_equal(const struct chanAccParams *a,
    const struct chanAccParams *b)
{
	if (a->cap_info != b->cap_info)
		return 0;
	for (unsigned ac = 0; ac < WME_NUM_AC; ac++) {
		if (!state_wme_params_equal(&a->cap_wmeParams[ac],
		    &b->cap_wmeParams[ac]))
			return 0;
	}
	return 1;
}

static int
state_wme_state_equal(const struct ieee80211_wme_state *a,
    const struct ieee80211_wme_state *b)
{
	if (a->wme_flags != b->wme_flags ||
	    a->wme_hipri_traffic != b->wme_hipri_traffic ||
	    a->wme_hipri_switch_thresh != b->wme_hipri_switch_thresh ||
	    a->wme_hipri_switch_hysteresis != b->wme_hipri_switch_hysteresis ||
	    a->wme_update != b->wme_update ||
	    a->wme_sta_apply != b->wme_sta_apply ||
	    a->wme_sta_error != b->wme_sta_error)
		return 0;
	for (unsigned ac = 0; ac < sizeof(a->wme_params) /
	    sizeof(a->wme_params[0]); ac++) {
		if (!state_wme_params_equal(&a->wme_params[ac],
		    &b->wme_params[ac]))
			return 0;
	}
	return state_chan_params_equal(&a->wme_wmeChanParams,
	    &b->wme_wmeChanParams) &&
	    state_chan_params_equal(&a->wme_wmeBssChanParams,
	    &b->wme_wmeBssChanParams) &&
	    state_chan_params_equal(&a->wme_chanParams, &b->wme_chanParams) &&
	    state_chan_params_equal(&a->wme_bssChanParams,
	    &b->wme_bssChanParams);
}

static void
test_native_state_negotiated(void)
{
	for (unsigned mismatch=0;mismatch<4;mismatch++) {
		struct state_fixture s;
		if (!state_fixture_start(&s,1,9)) continue;
		s.net.ic.ic_flags=IEEE80211_F_SHSLOT|IEEE80211_F_WME;
		s.net.ic.ic_caps=IEEE80211_C_WME;
		s.ni.ni_associd=1;s.ni.ni_bssid[0]=2;
		state_legacy_peer(&s.net,&s.ni,1);
		s.ni.ni_flags=IEEE80211_NODE_QOS|IEEE80211_NODE_WME_VALID;
		s.ni.ni_wme_applied_chan=s.net.ic.ic_curchan;
		s.ni.ni_wme_applied_slot=9;
		for (unsigned ac=0;ac<4;ac++) {
			struct wmeParams p={0,2+ac,3,5,55+ac,0};
			s.net.ic.ic_wme.wme_chanParams.cap_wmeParams[ac]=p;
			state_expected_words[ac]=(uint32_t)(55+ac)<<16|0x5300|
			    (10+(2+ac)*9);
		}
		if (mismatch==1) s.ni.ni_wme_applied_slot=20;
		if (mismatch==2) s.ni.ni_wme_applied_chan=NULL;
		if (mismatch==3) s.ni.ni_flags=IEEE80211_NODE_WME_VALID;
		struct ieee80211_wme_state saved=s.net.ic.ic_wme;
		CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,0)==0);
		state_negotiated=1;
		rtw88_net_state_work(&s.net.state_work,&s.net);
		CHECK(state_wme_state_equal(&saved, &s.net.ic.ic_wme));
		if (mismatch==0) {
			CHECK(state_parent_tx==1 && state_parent_init==0);
			CHECK(state_io_count==7);
		} else {
			CHECK(state_parent_tx==0 && state_parent_init==1);
			CHECK(!s.net.policy.accepting && !s.sc.sc_running);
			CHECK(state_channel_calls==0 && state_station_calls==0);
			CHECK(state_io_count==0);
			CHECK(!(s.ni.ni_flags&(IEEE80211_NODE_QOS|IEEE80211_NODE_WME_VALID)));
		}
		state_fixture_finish(&s);
	}
}
static void
test_native_state_reject_context(void)
{
	for (unsigned variant=0;variant<10;variant++) {
		struct state_fixture s;
		if (!state_fixture_start(&s,variant==7 || variant==8?36:1,variant==7 || variant==8?9:20)) continue;
		switch (variant) {
		case 0: s.net.configured_media|=IFM_IEEE80211_11A;break;
		case 1: s.net.configured_media|=IFM_IEEE80211_FH;break;
		case 2: s.net.configured_media|=IFM_IEEE80211_TURBO;break;
		case 3: s.net.configured_media|=IFM_IEEE80211_11NG;break;
		case 4: s.net.ic.ic_curchan->ic_flags|=IEEE80211_CHAN_TURBO;break;
		case 5: s.net.ic.ic_curchan->ic_flags=IEEE80211_CHAN_A;break;
		case 6: s.net.ic.ic_fixed_rate=0;s.net.ic.ic_curmode=IEEE80211_MODE_TURBO_G;break;
		case 7: s.net.configured_media|=IFM_IEEE80211_11B;break;
		case 8: s.net.configured_media|=IFM_IEEE80211_11G;break;
		case 9: s.net.configured_media|=IFM_IEEE80211_11B;
		    s.net.ic.ic_flags=IEEE80211_F_SHSLOT;
		    s.net.requested=IEEE80211_S_RUN;break;
		}
		rtw88_net_state_work(&s.net.state_work,&s.net);
		CHECK(state_parent_tx==0 && state_parent_init==1);
		CHECK(state_channel_calls==0 && state_station_calls==0);
		CHECK(state_io_count==0 && !s.net.policy.accepting);
		state_fixture_finish(&s);
	}
}

/* Legacy cfgset binds a mode for fixed rate, then clears only the fixed index.
 * Execute both transitions through the actual worker; no peer mode cache. */
static void
test_native_state_fixed_clear(void)
{
	struct state_fixture s;
	if (!state_fixture_start(&s,1,20)) return;
	s.net.ic.ic_curmode=IEEE80211_MODE_11B;
	s.net.ic.ic_fixed_rate=0;
	state_expected_cw=5;
	rtw88_net_state_work(&s.net.state_work,&s.net);
	CHECK(state_parent_tx==1 && s.net.policy.accepting);
	s.net.ic.ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;
	state_expected_cw=4;
	CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_SCAN,0)==0);
	rtw88_net_state_work(&s.net.state_work,&s.net);
	CHECK(state_parent_tx==2 && s.net.policy.accepting);
	state_io_count=0;
	CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_INIT,0)==0);
	rtw88_net_state_work(&s.net.state_work,&s.net);
	CHECK(state_parent_init==1 && state_io_count==0);
	state_fixture_finish(&s);
}

static void state_retune_barrier(void *cookie)
{
    CHECK(!state_lock.held);
    intr_barrier(cookie);
}
static void test_native_state_retune_failure(void)
{
    struct state_fixture s;
    if(!state_fixture_start(&s,36,9))return;
    s.bus.intr_barrier=state_retune_barrier;state_retune_failure=1;
    unsigned barriers=f.barriers;
    rtw88_net_state_work(&s.net.state_work,&s.net);
    CHECK(state_parent_tx==0 && state_parent_init==1);
    CHECK(!s.net.policy.accepting && !s.net.work_pending);
    CHECK(!s.sc.sc_running && s.sc.sc_stop_requested && !s.sc.sc_phy.channel_valid);
    CHECK(f.barriers==barriers+1 && state_channel_calls==1 && state_station_calls==0);
    state_retune_failure=0;state_fixture_finish(&s);
}
static void
test_native_state_width_restart(void)
{
	struct state_fixture s;
	unsigned barriers, generation;

	if (!state_fixture_start(&s, 36, 9))
		return;
	rtw88_net_state_work(&s.net.state_work, &s.net);
	CHECK(state_parent_tx == 1 && s.net.policy.accepting);
	generation = s.sc.sc_generation;
	CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_SCAN, 0) == 0);
	CHECK(!s.net.reset_required);
	rtw88_net_state_work(&s.net.state_work, &s.net);
	CHECK(state_parent_tx == 2 && s.sc.sc_generation == generation);
	/* Model a committed VHT80 tuple before leaving the association path. */
	s.sc.sc_phy.channel = (struct rtw88_channel_tuple){36, 42, 80};
	s.sc.sc_phy.channel_valid = 1;
	s.sc.sc_linked = 0;
	s.bus.intr_barrier = state_retune_barrier;
	barriers = f.barriers;
	generation = s.sc.sc_generation;
	CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_SCAN, 0) == 0);
	CHECK(s.net.reset_required);
	rtw88_net_state_work(&s.net.state_work, &s.net);
	CHECK(state_parent_tx == 3 && state_parent_init == 0);
	CHECK(f.barriers == barriers + 1);
	CHECK(s.sc.sc_generation > generation);
	CHECK(s.sc.sc_phy.channel_valid);
	CHECK(s.sc.sc_phy.channel.primary == 36);
	CHECK(s.sc.sc_phy.channel.center == 36);
	CHECK(s.sc.sc_phy.channel.width_mhz == 20);
	CHECK(!s.net.reset_required && s.net.policy.accepting);
	state_fixture_finish(&s);
}
static void
test_native_state_epoch(void)
{
	struct state_fixture s;
	if (!state_fixture_start(&s,1,20)) return;
	state_expected_channel=36;state_expected_slot=9;
	state_epoch_change=2;
	rtw88_net_state_work(&s.net.state_work,&s.net);
	CHECK(state_epoch_change==0 && state_parent_tx==1);
	CHECK(state_channel_calls==1 && state_station_calls==1);
	CHECK(state_io_count==7 && s.net.policy.accepting);
    CHECK(s.sc.sc_running && !s.sc.sc_stop_requested && !s.sc.sc_power_error);
	state_fixture_finish(&s);
}

/* Actual media/setmode -> init/worker -> shared INIT/SCAN/next-scan bridge.
 * Prior association state and allocation/notification services are modeled. */
static void
test_native_band_media(void)
{
 static const struct { unsigned old,mode,expected; int special,narrow; } cases[]={
  {8,IFM_IEEE80211_11A,40,0,0},
  {149,IFM_IEEE80211_11G,2,0,0},
  {149,IFM_IEEE80211_11B,2,0,0},
  {8,IFM_IEEE80211_11A,149,1,0},
  {8,IFM_IEEE80211_11A,40,2,0},
  {8,IFM_IEEE80211_11A,40,3,0},
  {149,IFM_IEEE80211_11A,153,0,1},
  {8,IFM_IEEE80211_11A,40,4,0}
 };
 for(unsigned c=0;c<__arraycount(cases);c++) {
  struct state_fixture s;
  struct ifmedia_entry selected;
  if(!state_fixture_start(&s,cases[c].old,cases[c].old>14?9:20))return;
  rtw88_net_state_work(&s.net.state_work,&s.net);
  struct ieee80211com *ic=&s.net.ic;
  state_driver_channels(ic);
  memset(ic->ic_chan_active,0,sizeof(ic->ic_chan_active));
  for(unsigned i=1;i<=IEEE80211_CHAN_MAX;i++)if(ic->ic_channels[i].ic_flags)setbit(ic->ic_chan_active,i);
  ic->ic_modecaps=(1U<<IEEE80211_MODE_AUTO)|(1U<<IEEE80211_MODE_11A)|(1U<<IEEE80211_MODE_11B)|(1U<<IEEE80211_MODE_11G);
  ic->ic_curmode=cases[c].old>14?IEEE80211_MODE_11A:IEEE80211_MODE_11G;
  ic->ic_ibss_chan=&ic->ic_channels[cases[c].old];
  ic->ic_des_chan=cases[c].special==1?&ic->ic_channels[149]:IEEE80211_CHAN_ANYC;
  if(cases[c].special==2)ic->ic_curchan=NULL;
  if(cases[c].special==3)ic->ic_curchan=IEEE80211_CHAN_ANYC;
  if(cases[c].narrow) {
   memset(ic->ic_chan_active,0,sizeof(ic->ic_chan_active));
   setbit(ic->ic_chan_active,149);setbit(ic->ic_chan_active,153);
   s.net.configured_media=IFM_IEEE80211|IFM_IEEE80211_11A;
  }
  selected.ifm_media=IFM_IEEE80211|cases[c].mode|(cases[c].narrow?IFM_IEEE80211_OFDM6:IFM_AUTO);
  ic->ic_media.ifm_cur=&selected;ic->ic_media.ifm_media=selected.ifm_media;
  ic->ic_state=IEEE80211_S_RUN;
  __typeof__(*ic->ic_auth) auth={0};ic->ic_auth=&auth;
  s.net.parent_state=state_shared_parent;
  state_real_media=1;state_scan_probe=state_scan_end=state_scan_free=state_bss_reset=0;
  memset(state_diag,0,sizeof(state_diag));
  CHECK(rtw88_net_media_change(&s.net.ec.ec_if)==0);
  CHECK(state_bss_reset==1 && s.ni.ni_rates.rs_nrates==0);
  unsigned bootstrap=ieee80211_chan2ieee(ic,ic->ic_curchan);
  CHECK(bootstrap>0 && bootstrap<=IEEE80211_CHAN_MAX && isset(ic->ic_chan_active,bootstrap));
  if(cases[c].narrow)CHECK(bootstrap==149);
  if(cases[c].special==4)state_epoch_change=2;
  rtw88_net_state_work(&s.net.state_work,&s.net);
  CHECK(state_diag[0]=='\0');
  CHECK(s.net.policy.accepting && (s.net.ec.ec_if.if_flags&IFF_RUNNING));
  CHECK(ic->ic_state==IEEE80211_S_SCAN && state_scan_probe==1 && state_scan_end==0);
  CHECK(s.sc.sc_channel==cases[c].expected && s.net.channel==cases[c].expected);
  CHECK(ieee80211_chan2ieee(ic,ic->ic_curchan)==cases[c].expected);
  CHECK(s.ni.ni_rates.rs_nrates==ic->ic_sup_rates[ic->ic_curmode].rs_nrates);
  CHECK(s.sc.sc_basic_rates==(cases[c].expected>14?0x150:0xf));
  CHECK(f.mmio[0x51b]==(cases[c].expected>14?9:20));
  if(cases[c].narrow)CHECK(isset(ic->ic_chan_active,149)&&isset(ic->ic_chan_active,153)&&isclr(ic->ic_chan_active,36));
  CHECK(state_epoch_change==0);
  unsigned generation=s.sc.sc_generation,current=s.sc.sc_channel;
  CHECK(rtw88_net_media_change(&s.net.ec.ec_if)==0);
  CHECK(s.sc.sc_generation==generation && s.sc.sc_channel==current && !s.net.work_pending);
  state_real_media=0;state_fixture_finish(&s);
 }
}

static void
test_native_band_invalid_and_fault(void)
{
 for(unsigned kind=0;kind<4;kind++) {
  struct state_fixture s;
  if(!state_fixture_start(&s,8,20))return;
  rtw88_net_state_work(&s.net.state_work,&s.net);
  struct ieee80211com *ic=&s.net.ic;
  state_driver_channels(ic);
  memset(ic->ic_chan_active,0,sizeof(ic->ic_chan_active));
  if(kind==1)setbit(ic->ic_chan_active,200); /* Undeclared active channel. */
  if(kind>=2)setbit(ic->ic_chan_active,36);
  ic->ic_curmode=IEEE80211_MODE_11A;
  ic->ic_des_chan=IEEE80211_CHAN_ANYC;
  ic->ic_state=IEEE80211_S_RUN;
  __typeof__(*ic->ic_auth) auth={0};ic->ic_auth=&auth;
  s.net.configured_media=IFM_IEEE80211|IFM_IEEE80211_11A;
  s.net.parent_state=state_shared_parent;
  state_real_media=1;
  unsigned loads=f.firmware_loads;
  if(kind==2)ic->ic_channels[36].ic_flags=IEEE80211_CHAN_B;
  if(kind==3)f.fail_rcr_final=1;
  int e=rtw88_net_init(&s.net.ec.ec_if);
  if(kind<2) {
   CHECK(e==EINVAL && f.firmware_loads==loads);
   CHECK(!s.sc.sc_started && !s.sc.sc_running && !s.net.policy.accepting);
   CHECK((s.net.ec.ec_if.if_flags&IFF_RUNNING)==0);
  } else if(kind==2) {
   CHECK(e==0);
   rtw88_net_state_work(&s.net.state_work,&s.net);
   CHECK(strstr(state_diag,"stage 4")!=NULL);
   CHECK(!s.sc.sc_started && !s.net.policy.accepting && (s.net.ec.ec_if.if_flags&IFF_RUNNING)==0);
  } else {
   CHECK(e==EIO && !s.sc.sc_started && !s.sc.sc_running);
   CHECK(!s.sc.sc_powered && !s.sc.sc_dma_master && !f.master && !s.net.policy.accepting);
   CHECK((s.net.ec.ec_if.if_flags&IFF_RUNNING)==0 && !s.net.work_pending);
   f.fail_rcr_final=0;
  }
  state_real_media=0;state_fixture_finish(&s);
 }
}
