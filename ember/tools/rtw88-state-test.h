/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD native state/default EDCA regression, AI-assisted. */
/* Complete native state consumer plus real core; kernel scheduling is modeled. */
#include <stdarg.h>
#include <dev/ic/rtw88_net_policy.h>
#include "native-state-types.inc"
#define IFF_UP 1
#define IFF_RUNNING 2
#define IFF_OACTIVE 4
struct ifnet { void *if_softc; int if_flags, if_timer; };
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
struct ieee80211_node {
	struct ieee80211com *ni_ic;
	unsigned ni_wme_attempt;
	struct ieee80211_rateset ni_rates;
	struct ieee80211_channel *ni_wme_applied_chan;
	unsigned ni_flags, ni_associd, ni_wme_applied_slot;
	uint8_t ni_bssid[6];
};
struct ieee80211com {
	struct ifnet *ic_ifp;
	struct ieee80211_node *ic_bss;
	struct ieee80211_channel *ic_curchan;
	struct ieee80211_channel ic_channels[256];
	struct ieee80211_wme_state ic_wme;
	struct ifmedia ic_media;
	unsigned ic_caps, ic_flags, ic_opmode;
	enum ieee80211_phymode ic_curmode;
	int ic_fixed_rate;
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
	int work_pending;
	enum ieee80211_state requested;
	unsigned channel, configured_media;
	int state_arg;
	int (*parent_state)(struct ieee80211com *, enum ieee80211_state, int);
};
static kmutex_t state_lock;
static unsigned state_parent_tx, state_parent_init, state_expected_cw,
    state_expected_slot, state_expected_channel, state_fail_io,
    state_after_effect, state_io_count, state_io_armed;
static int state_media_error;
static unsigned state_epoch_change;
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
#define ieee80211_chan2ieee(ic, ch) ((unsigned)((ch) - (ic)->ic_channels))
#define mstohz(ms) (ms)
static unsigned getticks(void) { return 1; }
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
	if (state_media_error==0 || state_media_error==ENETRESET)
		net->ic.ic_curmode=state_media_mode;
	return state_media_error;
}
/* Shared media/mode policy is modeled here; the media contract executes it. */
static int
ieee80211_setmode(struct ieee80211com *ic, enum ieee80211_phymode mode)
{
	assert(!state_lock.held);
	ic->ic_curmode=mode;
	return 0;
}
static void
rtw88_net_stop(struct ifnet *ifp, int disable)
{
	struct rtw88_net *net=ifp->if_softc;
	(void)disable;
	ifp->if_flags &= ~(IFF_RUNNING|IFF_OACTIVE);
	(void)rtw88_net_request(&net->policy);
	(void)rtw88_stop(net->core);
}
static int
state_channel(struct rtw88_softc *sc, unsigned channel)
{
	state_channel_calls++;
	return rtw88_channel(sc,channel);
}
static int
state_station(struct rtw88_softc *sc, const uint8_t *bssid, unsigned aid,
    unsigned basic, unsigned slot, int linked)
{
	state_station_calls++;
	return rtw88_station_config(sc,bssid,aid,basic,slot,linked);
}
#define rtw88_channel state_channel
#define rtw88_station_config state_station
#define rtw88_edca_apply state_edca_apply
#include "native-state.inc"
#undef rtw88_edca_apply
#undef rtw88_channel
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
static void
test_native_state_negotiated(void)
{
	for (unsigned mismatch=0;mismatch<4;mismatch++) {
		struct state_fixture s;
		if (!state_fixture_start(&s,1,9)) continue;
		s.net.ic.ic_flags=IEEE80211_F_SHSLOT|IEEE80211_F_WME;
		s.net.ic.ic_caps=IEEE80211_C_WME;
		s.ni.ni_associd=1;s.ni.ni_bssid[0]=2;
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
		CHECK(memcmp(&saved,&s.net.ic.ic_wme,sizeof(saved))==0);
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
	state_fixture_finish(&s);
}
