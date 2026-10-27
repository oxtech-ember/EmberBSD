/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual shared association producer/consumer; kernel services from WME fixture. */
#define main ht20_prior_wme_main
#include "net80211-wme-contract.c"
#undef main
#include "native-ht20-policy.h"
#include <stdarg.h>

/* Actual native callback and complete worker use explicit kernel/core services.
 * RF programming and full core I/O are not claimed by this exchange. */
struct rtw88_softc { int sc_started, sc_running, sc_stop_requested; unsigned sc_channel, sc_generation, sc_h2c_box, modeled_tx_pending; int sc_linked; struct { int active, configured; } sc_ra; struct rtw88_phy_state sc_phy; };
struct rtw88_net {
	struct { struct ifnet ec_if; } ec;
	struct ieee80211com ic;
	struct rtw88_net_neg neg;
	int run_pending;
	volatile uint64_t ht20_snapshot;
	volatile uint32_t ht20_generation;
	void *dev, *softint;
	unsigned configured_media;
	int (*parent_state)(struct ieee80211com *, enum ieee80211_state, int);
	struct rtw88_softc *core;
	struct rtw88_net_policy policy;
	int *lock;
	int scan, work_pending, reset_required, state_arg, state_work;
	int tx_quiet, quiet_error;
	unsigned channel;
	struct rtw88_channel_tuple pending_channel;
	enum ieee80211_state requested;
	void *wq;
};
static unsigned native_queues, native_scans;
static int native_lock;
static void callout_stop(int *c) { (void)c; native_scans++; }
static void mutex_enter(int *p) { assert(!*p); *p=1; }
static void mutex_exit(int *p) { assert(*p); *p=0; }
static void workqueue_enqueue(void *q, int *w, void *arg)
{ (void)q; (void)w; (void)arg; assert(!native_lock); native_queues++; }
#include "native-ht20-callback.inc"
/* Complete worker, with bounded core/clock/restart services modeled here.
 * Real core/restart I/O remains independently executed by RA/reconnect suites. */
#ifndef IFF_UP
#define IFF_UP 1
#define IFF_RUNNING 2
#define IFF_OACTIVE 4
#endif
struct work { int unused; };
#define RTW88_WME_NAC 4
struct rtw88_edca_params { int unused; };
struct rtw88_ra_config { unsigned use_protection; };
static unsigned worker_hw_calls, worker_parent_run, worker_parent_init, worker_restarts;
static int worker_parent_error, worker_parent_newer, worker_init_error;
static uint64_t worker_newer_epoch;
static int rtw88_net_restart_peer(struct rtw88_net *net)
{
 assert(!native_lock);
 if (net->reset_required) { worker_restarts++; net->reset_required=0; net->core->sc_linked=0; }
 return 0;
}
static unsigned rtw88_net_basic_rates(struct ieee80211com *ic) { (void)ic; return 15; }
static int rtw88_net_edca_prepare(struct ieee80211com *ic,unsigned media,unsigned state,unsigned ch,unsigned slot,struct rtw88_edca_params *p)
{ (void)ic;(void)media;(void)state;(void)ch;(void)slot;(void)p;assert(!native_lock);return 0; }
static int rtw88_net_ra_prepare(struct ieee80211com *ic,unsigned ch,unsigned basic,uint64_t epoch,struct rtw88_ra_config *p)
{ (void)ic;(void)ch;(void)basic;(void)epoch;p->use_protection=0;assert(!native_lock);return 0; }
static int rtw88_channel(struct rtw88_softc *sc,unsigned ch)
{ assert(native_lock);worker_hw_calls++;sc->sc_channel=ch;return 0; }
static int
rtw88_channel_apply(struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *tuple)
{
	assert(tuple != NULL && tuple->width_mhz == 20 &&
	    tuple->primary == tuple->center);
	return rtw88_channel(sc, tuple->primary);
}
static int rtw88_station_config(struct rtw88_softc *sc,const uint8_t *bssid,unsigned aid,unsigned basic,unsigned slot,int linked)
{ (void)bssid;(void)aid;(void)basic;(void)slot;assert(native_lock);worker_hw_calls++;sc->sc_linked=linked;return 0; }
static int rtw88_edca_apply(struct rtw88_softc *sc,struct rtw88_edca_params *p,unsigned slot)
{ (void)sc;(void)p;(void)slot;assert(native_lock);worker_hw_calls++;return 0; }
static int rtw88_ra_configure(struct rtw88_softc *sc,struct rtw88_ra_config *p)
{ (void)p;assert(native_lock);worker_hw_calls++;sc->sc_ra.active=1;return 0; }
static int rtw88_stop(struct rtw88_softc *sc)
{ assert(!native_lock);sc->sc_started=sc->sc_running=sc->sc_linked=0;return 0; }
int rtw88_net_state_format(char *buf,size_t len,const struct rtw88_net_state_diag *diag)
{ return snprintf(buf,len,"stage%u",diag->stage); }
static void aprint_error_dev(void *dev,const char *fmt,...)
{ (void)dev;(void)fmt;assert(!native_lock); }
static void callout_schedule(int *c,unsigned when) { (void)c;(void)when; }
static void softint_schedule(void *cookie) { (void)cookie; }
#define mstohz(ms) (ms)
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
static unsigned getticks(void) { return 1; }
/* This HT exchange owns profile validation, not busy-mailbox scheduling.
 * The enable/RA suites execute the real transport. Busy is a loud boundary. */
/* This exchange model has no posted DMA. Reject that undeclared domain;
 * the owning enable/core/IRQ fixture proves actual drain completion. */
static int rtw88_tx_drain_snapshot(struct rtw88_softc *sc,unsigned *pending)
{ assert(sc!=NULL && native_lock);if(sc->modeled_tx_pending)return EOPNOTSUPP;*pending=0;return 0; }
static int rtw88_fw_mailbox_begin(struct rtw88_softc *sc,unsigned count,int *ready)
{ assert(sc!=NULL && native_lock && count<=3);*ready=1;return 0; }
static int rtw88_fw_mailbox_end(struct rtw88_softc *sc,int error)
{ assert(sc!=NULL && native_lock);return error; }
static inline int kpause(const char *name,bool intr,int ticks,int *lock)
{ (void)name;(void)intr;(void)ticks;(void)lock;
  assert(!"HT exchange does not model busy mailbox transport");return EWOULDBLOCK; }
/* This association/worker fixture has no radio or power readiness model.
 * Keep the newly attached profile disabled; the owning RTL8822CE readiness
 * contract compiles and exercises the production helper. */
static int
rtw88_net_ht20_profile_prepare(struct rtw88_net *net,
    struct ieee80211_ht20_profile *profile)
{ (void)net; memset(profile,0,sizeof(*profile)); return EOPNOTSUPP; }
#include "native-ht20-worker.inc"
static int worker_parent(struct ieee80211com *ic,enum ieee80211_state state,int arg)
{
 (void)arg;assert(!native_lock);
 if (state==IEEE80211_S_RUN) {
  worker_parent_run++;
  if (worker_parent_newer) { worker_parent_newer=0;rtw88_net_newstate(ic,IEEE80211_S_SCAN,0);worker_newer_epoch=((struct rtw88_net *)ic->ic_ifp->if_softc)->policy.epoch; }
  if (worker_parent_error) return worker_parent_error;
 }
 if (state==IEEE80211_S_INIT) { worker_parent_init++; if (worker_init_error) return worker_init_error; }
 int e=ieee80211_ht20_state_request(ic,state);
 if (!e) ic->ic_state=state;
 return e;
}
static struct ieee80211_ht20_profile admitted;
static struct rtw88_softc native_core;
static struct rtw88_net native_net;
static int change_before_run;
static int provider_error;
static int
provider(struct ieee80211com *ic, struct ieee80211_ht20_profile *p)
{ (void)ic; assert(!native_lock); *p=admitted; return provider_error; }
static int
native_dispatch(struct ieee80211com *ic, int state, int arg)
{
	if (state==IEEE80211_S_RUN && change_before_run) {
		admitted.generation++; change_before_run=0;
	}
	return rtw88_net_newstate(ic,(enum ieee80211_state)state,arg);
}
/* A synthetic scheduler satisfies negotiation readiness only. BA Action/data
 * is not admitted in this older fixture and is executed by the full B suite. */
static uint64_t ht_clock(struct ieee80211com *ic) { (void)ic;return 1; }
static int ht_schedule(struct ieee80211com *ic,uint64_t deadline,uint64_t token)
{ (void)ic;(void)deadline;(void)token;abort(); }
static void ht_cancel(struct ieee80211com *ic) { (void)ic; }
static const struct ieee80211_rxba_ops ht_ops={ht_clock,ht_schedule,ht_cancel};
static void
ht_setup(void)
{
	setup();
	test_ic.ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;
	memset(&native_core,0,sizeof(native_core));
	memset(&native_net,0,sizeof(native_net));
	native_net.core=&native_core; native_net.lock=&native_lock;
	test_ifp.if_softc=&native_net;
	test_ic.ic_test_newstate=native_dispatch;
	test_ic.ic_ht20_admit=provider;
	test_ic.ic_rxba_ops=&ht_ops;
	test_ic.ic_wme.wme_sta_apply=strict_update;
	admitted=(struct ieee80211_ht20_profile){19,0xffff,0xffff,IEEE80211_HT20_READY_ALL,16};
	native_queues=native_scans=0; native_lock=change_before_run=provider_error=0;
}
static uint8_t *
ht_in_request(void)
{
	if (!captured) return NULL;
	unsigned type=mtod(captured,struct ieee80211_frame *)->i_fc[0]&IEEE80211_FC0_SUBTYPE_MASK;
	size_t fixed=sizeof(struct ieee80211_frame)+4+
	    (type==IEEE80211_FC0_SUBTYPE_REASSOC_REQ?6:0);
	if (captured->m_len<0 || (size_t)captured->m_len<fixed) return NULL;
	uint8_t *p=captured->m_data+fixed;
	size_t len=(size_t)captured->m_len-fixed;
	while (len>=2 && p[1]<=len-2) {
		if (p[0]==45) return p;
		size_t n=(size_t)p[1]+2; p+=n; len-=n;
	}
	return NULL;
}
static struct mbuf *
ht_response(void)
{
	struct mbuf *m=response(1,0,IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	uint8_t *p=m->m_data+m->m_len;
	ieee80211_ht20_write(p,&admitted); p+=28;
	memset(p,0,24); p[0]=61; p[1]=22; p[2]=1;
	p[8]=0xff; /* Basic MCS0..7. */
	m->m_len+=52; m->m_pkthdr.len=m->m_len;
	return m;
}

static unsigned callback_reset;

static int
reset_during_apply(struct ieee80211com *ic, const struct chanAccParams *params,
    unsigned slot)
{

	CHECK(strict_update(ic, params, slot) == 0);
	if (callback_reset == 1)
		ieee80211_ht20_reset(ic->ic_bss);
	if (callback_reset == 2)
		ieee80211_wme_reset(ic->ic_bss);
	return 0;
}

static void
test_callback_epoch_cancellation(void)
{
	for (unsigned reset = 0; reset < 3; reset++) {
		struct ieee80211_ht20_profile profile;
		uint32_t ht_attempt, wme_attempt;

		ht_setup();
		callback_reset = reset;
		test_ic.ic_wme.wme_sta_apply = reset_during_apply;
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		CHECK(test_node.ni_ht20.offered);
		profile = admitted;
		ht_attempt = test_node.ni_ht20.attempt;
		wme_attempt = test_node.ni_wme_attempt;
		release_capture();
		deliver(ht_response(), IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK(applies == 1 && !beacon_lock);
		CHECK(profile.generation == admitted.generation &&
		    profile.rx_mcs == admitted.rx_mcs &&
		    profile.tx_mcs == admitted.tx_mcs &&
		    profile.ready == admitted.ready && profile.ampdu == admitted.ampdu);
		if (reset == 0) {
			CHECK(test_node.ni_ht20.valid &&
			    !test_node.ni_ht20.offered);
			CHECK(test_node.ni_flags & IEEE80211_NODE_AMSDU_RX);
			CHECK(test_node.ni_associd != 0);
			CHECK(native_net.requested == IEEE80211_S_RUN);
			CHECK(test_node.ni_ht20.attempt == ht_attempt &&
			    test_node.ni_wme_attempt == wme_attempt);
			continue;
		}
		/* Input owns failure cleanup after the synchronous callback. */
		CHECK(!test_node.ni_ht20.valid && !test_node.ni_ht20.offered);
		CHECK(!(test_node.ni_flags & IEEE80211_NODE_AMSDU_RX));
		CHECK(test_node.ni_associd == 0);
		CHECK(native_net.requested == IEEE80211_S_SCAN);
		CHECK(test_ic.ic_state != IEEE80211_S_RUN &&
		    !native_net.policy.accepting && !native_core.sc_ra.active);
	}
}

static void
test_saved_offer_epochs(void)
{
	for (unsigned reset = 0; reset < 3; reset++) {
		struct ieee80211_ht20_node offer;
		struct mbuf *m;
		ht_setup();
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		offer = test_node.ni_ht20;
		release_capture();
		if (reset == 1)
			ieee80211_ht20_reset(&test_node);
		if (reset == 2)
			ieee80211_wme_reset(&test_node);
		/* Isolate the epochs with every other admission guard satisfied.
		 * Input consumes these offer bits before invoking the helper. */
		test_node.ni_ht20.offered = test_node.ni_ht20.valid = 0;
		test_node.ni_flags &= ~IEEE80211_NODE_WME_OFFER;
		test_node.ni_flags |= IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID;
		m = ht_response();
		CHECK(ieee80211_ht20_response(&test_ic, &test_node,
		    IEEE80211_FC0_SUBTYPE_ASSOC_RESP, &offer,
		    m->m_data + sizeof(struct ieee80211_frame) + 6,
		    m->m_len - sizeof(struct ieee80211_frame) - 6, 1) ==
		    (reset == 0 ? 0 : EINVAL));
		CHECK(test_node.ni_ht20.valid == (reset == 0));
		CHECK(!test_node.ni_ht20.offered);
		CHECK(!!(test_node.ni_flags & IEEE80211_NODE_AMSDU_RX) ==
		    (reset == 0));
		m_freem(m);
	}
}
static void
test_actual_exchange(void)
{
	ht_setup();
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);
	uint8_t *p=ht_in_request();
	CHECK(p!=NULL);
	CHECK(p && p[1]==26 && p[2]==12 && p[4]==16 && p[5]==255 && p[6]==255 && p[17]==1);
	CHECK(test_node.ni_ht20.offered && !test_node.ni_ht20.valid);
	release_capture();
	deliver(ht_response(),IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(applies==1 && test_node.ni_ht20.valid && !test_node.ni_ht20.offered);
	CHECK(test_node.ni_ht20.tx_mcs==0xffff && test_node.ni_ht20.basic_mcs==255);
	CHECK(native_net.requested==IEEE80211_S_RUN && native_queues==1);
	CHECK(test_node.ni_ht20.profile.generation==19 && !native_lock);
	/* The actual callback consumes queued matching state; parent/RF worker is
	 * intentionally not modeled as successful negotiation or radio operation. */
	CHECK(rtw88_net_newstate(&test_ic,IEEE80211_S_SCAN,0)==0);
	CHECK(!test_node.ni_ht20.valid && native_net.requested==IEEE80211_S_SCAN);
	test_ic.ic_state=IEEE80211_S_SCAN;
	deliver(ht_response(),IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(native_net.requested==IEEE80211_S_SCAN && !test_node.ni_ht20.valid);
}

/* One queued request owns one response while the native RUN worker waits. */
static void
test_one_association_response(void)
{
	for (unsigned kind = 0; kind < 3; kind++) {
		for (unsigned reassoc = 0; reassoc < 2; reassoc++) {
			int req = reassoc ? IEEE80211_FC0_SUBTYPE_REASSOC_REQ :
			    IEEE80211_FC0_SUBTYPE_ASSOC_REQ;
			int resp = req + 0x10;
			struct ieee80211_ht20_node ht;
			struct rtw88_net worker;
			struct ieee80211_wme_state wme;
			unsigned flags, queues, aid;

			ht_setup();
			if (kind != 0) test_ic.ic_ht20_admit = NULL;
			if (kind == 2) test_ic.ic_flags &= ~IEEE80211_F_WME;
			CHECK(request(req) == 0);
			release_capture();
			deliver(kind == 0 ? ht_response() : response(1, 0, resp), resp);
			CHECK(test_ic.ic_state == IEEE80211_S_ASSOC);
			CHECK(native_net.requested == IEEE80211_S_RUN);
			CHECK(!!test_node.ni_ht20.valid == (kind == 0));
			ht = test_node.ni_ht20; worker = native_net;
			wme = test_ic.ic_wme;
			flags = test_node.ni_flags; aid = test_node.ni_associd;
			queues = native_queues;
			/* A different AID makes silent legacy reacceptance observable. */
			struct mbuf *m = kind == 0 ? ht_response() : response(1, 0, resp);
			m->m_data[sizeof(struct ieee80211_frame) + 4] = 7;
			deliver(m, resp);
			CHECK(memcmp(&ht, &test_node.ni_ht20, sizeof(ht)) == 0);
			CHECK(memcmp(&worker, &native_net, sizeof(worker)) == 0);
			CHECK(memcmp(&wme, &test_ic.ic_wme, sizeof(wme)) == 0);
			CHECK(flags == test_node.ni_flags && aid == test_node.ni_associd);
			CHECK(native_queues == queues && test_ic.ic_state == IEEE80211_S_ASSOC);
		}
	}
	for (unsigned kind = 0; kind < 3; kind++) {
		for (unsigned bad = 0; bad < 3; bad++) {
			ht_setup();
			if (kind != 0) test_ic.ic_ht20_admit = NULL;
			if (kind == 2) test_ic.ic_flags &= ~IEEE80211_F_WME;
			/* Unsolicited input must not replace any state or queue work. */
			struct ieee80211_node node = test_node;
			deliver(ht_response(), IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
			CHECK(memcmp(&node, &test_node, sizeof(node)) == 0);
			CHECK(native_queues == 0);
			CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
			release_capture();
			struct mbuf *m = response(1, bad == 0 ? 1 : 0,
			    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
			if (bad == 1) m->m_len = sizeof(struct ieee80211_frame) + 5;
			if (bad == 2) m->m_data[sizeof(struct ieee80211_frame) + 7] = 255;
			deliver(m, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
			node = test_node;
			struct rtw88_net worker = native_net;
			test_ic.ic_state = IEEE80211_S_ASSOC;
			deliver(ht_response(), IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
			CHECK(memcmp(&node, &test_node, sizeof(node)) == 0);
			CHECK(memcmp(&worker, &native_net, sizeof(worker)) == 0);
			/* A real new queued request grants exactly one response again. */
			CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
			release_capture();
			deliver(kind == 0 ? ht_response() : response(1, 0,
			    IEEE80211_FC0_SUBTYPE_ASSOC_RESP), IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
			CHECK(native_net.requested == IEEE80211_S_RUN && test_node.ni_associd == 1);
		}
	}
}

static void
test_pending_identity_guards(void)
{
	for (unsigned kind = 0; kind < 3; kind++) {
		for (unsigned bad = 0; bad < 10; bad++) {
			struct ieee80211_channel other = {5180, 0};
			ht_setup();
			if (kind != 0) test_ic.ic_ht20_admit = NULL;
			if (kind == 2) test_ic.ic_flags &= ~IEEE80211_F_WME;
			CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
			release_capture();
			struct mbuf *m = ht_response();
			struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
			if (bad < 3) {
				uint8_t *a = bad == 0 ? wh->i_addr1 : bad == 1 ? wh->i_addr2 : wh->i_addr3;
				a[0] ^= 2;
			}
			if (bad == 3) test_ic.ic_curchan = &other;
			if (bad == 4) test_node.ni_chan = &other;
			if (bad == 6) test_node.ni_wme_attempt++;
			if (bad == 7) test_node.ni_wme_offer_bssid[0] ^= 2;
			if (bad == 8) test_ic.ic_opmode = IEEE80211_M_HOSTAP;
			if (bad == 9) test_ic.ic_state = IEEE80211_S_AUTH;
			struct ieee80211_node node = test_node;
			struct ieee80211_wme_state wme = test_ic.ic_wme;
			struct rtw88_net worker = native_net;
			deliver(m, bad == 5 ? IEEE80211_FC0_SUBTYPE_REASSOC_RESP : IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
			CHECK(memcmp(&node, &test_node, sizeof(node)) == 0);
			CHECK(memcmp(&wme, &test_ic.ic_wme, sizeof(wme)) == 0);
			CHECK(memcmp(&worker, &native_net, sizeof(worker)) == 0);
			test_ic.ic_curchan = test_node.ni_chan = &test_channel;
			if (bad == 6) test_node.ni_wme_attempt--;
			if (bad == 7) test_node.ni_wme_offer_bssid[0] ^= 2;
			test_ic.ic_opmode = IEEE80211_M_STA;
			test_ic.ic_state = IEEE80211_S_ASSOC;
			deliver(kind == 0 ? ht_response() : response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP), IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
			CHECK(native_net.requested == IEEE80211_S_RUN && test_node.ni_associd == 1);
		}
	}
}

static void
test_local_admission(void)
{
	for (unsigned variant=0;variant<13;variant++) {
		ht_setup();
		switch(variant) {
		case 0: test_ic.ic_ht20_admit=NULL; break;
		case 1: provider_error=EIO; break;
		case 2: admitted.generation=0; break;
		case 3: admitted.ready=0; break;
		case 4: admitted.tx_mcs=255; break; /* unequal: cannot encodeTXparams1 */
		case 5: admitted.rx_mcs=admitted.tx_mcs=1; break;
		case 6: test_ic.ic_flags&=~IEEE80211_F_WME; break;
		case 7: test_ic.ic_caps&=~IEEE80211_C_WME; break;
		case 8: test_node.ni_wme_ie=NULL; break;
		case 9: test_ic.ic_wme.wme_sta_apply=NULL; break;
		case 10: test_ic.ic_flags|=IEEE80211_F_PRIVACY;
			test_node.ni_rsn.rsn_ucastcipher=IEEE80211_CIPHER_WEP; break;
		case 11: test_ic.ic_state=IEEE80211_S_AUTH; break;
		case 12: admitted.ampdu=3; break; /* Larger RXaggregate not admitted. */
		}
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);
		CHECK(ht_in_request()==NULL && !test_node.ni_ht20.offered);
		release_capture();
	}
}
static void
corrupt_ht_request(struct mbuf *m)
{
	uint8_t *p = m->m_data + sizeof(struct ieee80211_frame) + 4;
	uint8_t *end = m->m_data + m->m_len;
	while (end - p >= 2 && p[1] <= end - p - 2) {
		if (p[0] == 45) { p[2] ^= 1; return; }
		p += p[1] + 2;
	}
	CHECK(0); /* A real HT request must reach the byte proof. */
}

static void
test_pending_producer_boundaries(void)
{
	ht_setup();
	during_prepend = corrupt_ht_request;
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == EINVAL);
	CHECK(!(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING));
	CHECK(!test_node.ni_ht20.offered && captured == NULL && node_refs == 1);
	for (unsigned bad = 0; bad < 4; bad++) {
		struct ieee80211_channel other = {5180, 0};
		ht_setup();
		if (bad == 0) test_ic.ic_opmode = IEEE80211_M_HOSTAP;
		if (bad == 1) test_ic.ic_state = IEEE80211_S_AUTH;
		if (bad == 2) test_node.ni_chan = &other;
		if (bad == 3) test_ic.ic_bss = NULL;
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		CHECK(!(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING));
		release_capture();
	}
}

static void
test_producer_failures(void)
{
	for (unsigned variant=0;variant<4;variant++) {
		ht_setup();
		if (variant==0) fail_allocation=1;
		if (variant==1) fail_prepend=1;
		if (variant==2) { test_ic.ic_mgtq.ifq_len=1; test_ic.ic_mgtq.ifq_maxlen=1; }
		if (variant==3) test_ic.ic_mgtq.ifq_len=IFQ_MAXLEN;
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==(variant<2?ENOMEM:ENOBUFS));
		CHECK(!test_node.ni_ht20.offered && !test_node.ni_ht20.valid);
		CHECK(!(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING));
		CHECK(captured==NULL && node_refs==1);
		if (variant>=2) CHECK(test_ic.ic_mgtq.ifq_drops==1);
	}
}
static void
test_response_rejects(void)
{
	for (unsigned variant=0;variant<17;variant++) {
		ht_setup();
		if (variant==9) admitted.rx_mcs=admitted.tx_mcs=255;
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);
		CHECK(test_node.ni_ht20.offered);
		release_capture();
		struct mbuf *m=ht_response();
		uint8_t *cap=m->m_data+m->m_len-52;
		CHECK(((uintptr_t)(cap+2) & 1) != 0);
		uint8_t *op=cap+28;
		switch(variant) {
		case 0: cap[1]=25; break;
		case 1: memcpy(m->m_data+m->m_len,cap,28); m->m_len+=28; break;
		case 2: memcpy(m->m_data+m->m_len,op,24); m->m_len+=24; break;
		case 3: memmove(cap,op,24); m->m_len-=28; break;
		case 4: m->m_len-=24; break;
		case 5: op[2]=2; break;
		case 6: cap[2]=8; break; /* reservedSMPS */
		case 7: cap[5]=0; cap[6]=0; break;
		case 8: op[10]=1; break; /* basicMCS16 */
		case 9: op[9]=1; break; /* local1SS cannot receive basicMCS8 */
		case 10: admitted.generation++; break;
		case 11: admitted.rx_mcs=admitted.tx_mcs=255; break;
		case 12: test_ic.ic_flags|=IEEE80211_F_PRIVACY;
			test_node.ni_rsn.rsn_ucastcipher=IEEE80211_CIPHER_AES_CCM; break;
		case 13: apply_error=EIO; break;
		case 14: change_before_run=1; break;
		case 15: native_net.policy.dying=1; break;
		case 16: admitted.ampdu=20; break; /* Valid changed spacing, same epoch. */
		}
		m->m_pkthdr.len=m->m_len;
		deliver(m,IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK(!test_node.ni_ht20.valid && !test_node.ni_ht20.offered);
		CHECK(test_node.ni_associd==0);
		CHECK(native_net.requested!=IEEE80211_S_RUN);
	}
}
static void
test_response_limits(void)
{
	ht_setup();
	struct mbuf *s=ht_response(); size_t size=(size_t)s->m_len; m_freem(s);
	for (size_t len=0;len<size;len++) {
		ht_setup(); CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0); release_capture();
		struct mbuf *m=ht_response(); m->m_len=m->m_pkthdr.len=(int)len;
		deliver(m,IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK(!test_node.ni_ht20.valid);
	}
	for (unsigned smps=0;smps<4;smps++) {
		if (smps==2) continue;
		ht_setup(); CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0); release_capture();
		struct mbuf *m=ht_response(); m->m_data[m->m_len-52+2]=(uint8_t)(smps<<2);
		deliver(m,IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK(test_node.ni_ht20.valid);
		CHECK(test_node.ni_ht20.tx_mcs==(smps==3?0xffff:0xff));
	}
	/* Matching legacy response consumes the HT offer without activating HT. */
	ht_setup(); CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0); release_capture();
	deliver(response(1,0,IEEE80211_FC0_SUBTYPE_ASSOC_RESP),IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(!test_node.ni_ht20.valid && !test_node.ni_ht20.offered);
	CHECK(native_net.requested==IEEE80211_S_RUN);
}

static void
test_direct_run_failure_ownership(void)
{
	for (unsigned variant=0;variant<2;variant++) {
	ht_setup(); CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0); release_capture();
	deliver(ht_response(),IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(test_node.ni_ht20.valid);
	/* Modeled outcome of an earlier accepted worker, not a PHY/ACK claim. */
	test_ic.ic_state=IEEE80211_S_RUN;
	native_net.policy.accepting=1; native_core.sc_ra.active=1;
	native_core.sc_linked=1;
	uint64_t old=native_net.policy.epoch;
	if (variant==0) admitted.generation++; else admitted.ampdu=20;
	CHECK(rtw88_net_newstate(&test_ic,IEEE80211_S_RUN,0)==EINVAL);
	CHECK(!native_net.policy.accepting && native_net.policy.epoch!=old);
	CHECK(!native_core.sc_ra.active && !test_node.ni_ht20.valid);
	CHECK(native_net.requested==IEEE80211_S_INIT && native_net.reset_required);
	}
	/* A legacy RUN request is an exact policy success with no HT admission. */
	ht_setup(); test_ic.ic_ht20_admit=NULL;
	uint32_t attempt=test_node.ni_ht20.attempt;
	CHECK(rtw88_net_newstate(&test_ic,IEEE80211_S_RUN,0)==0);
	CHECK(test_node.ni_ht20.attempt==attempt && !test_node.ni_ht20.valid);
}
static void
test_delayed_worker_profile(void)
{
 for (unsigned variant=0;variant<6;variant++) {
  ht_setup();CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);release_capture();
  deliver(ht_response(),IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
  CHECK(test_node.ni_ht20.valid && native_net.requested==IEEE80211_S_RUN);
  native_net.ic=test_ic;native_net.ec.ec_if=test_ifp;
  native_net.ic.ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;
  native_net.ic.ic_ifp=&native_net.ec.ec_if;
  native_net.parent_state=worker_parent;native_net.channel=1;
  native_net.ec.ec_if.if_flags=IFF_UP|IFF_RUNNING;
  native_core.sc_started=native_core.sc_running=1;
  worker_hw_calls=worker_parent_run=worker_parent_init=worker_restarts=0;
  worker_parent_error=worker_parent_newer=worker_init_error=0;worker_newer_epoch=0;
  if (variant==0) admitted.generation++;
  if (variant==1) admitted.ampdu=20;
  if (variant==2 || variant==3) worker_parent_error=EINVAL;
  if (variant==3) worker_parent_newer=1;
  if (variant==4) { worker_init_error=EINVAL;CHECK(rtw88_net_newstate(&native_net.ic,IEEE80211_S_INIT,-1)==0); }
  rtw88_net_state_work(NULL,&native_net);
  if (variant==5) {
   CHECK(native_net.policy.accepting && native_core.sc_ra.active);
   CHECK(test_node.ni_ht20.valid && native_net.ic.ic_state==IEEE80211_S_RUN);
   CHECK(native_net.policy.beacon_valid && worker_parent_run==1 && !native_net.work_pending);
   continue;
  }
  CHECK(!native_net.policy.beacon_valid);
  CHECK((variant==3 || !native_net.policy.accepting) && !native_core.sc_ra.active);
  CHECK(!test_node.ni_ht20.valid && !native_net.work_pending);
  CHECK(native_net.ic.ic_state!=IEEE80211_S_RUN);
  if (variant<2) CHECK(worker_hw_calls==0 && worker_parent_run==0);
  if (variant==2) CHECK(native_net.requested==IEEE80211_S_INIT);
  if (variant==3) CHECK(native_net.requested==IEEE80211_S_SCAN && native_net.ic.ic_state==IEEE80211_S_SCAN && native_net.policy.epoch==worker_newer_epoch);
  if (variant==4) CHECK(worker_parent_init==1 && !(native_net.ec.ec_if.if_flags&IFF_RUNNING) && !native_core.sc_started);
 }
}
/* The accepted response is the producer. These are full beacon/probe-response
 * consumers, including the legacy TLV early-return paths. No PHY admission is
 * registered by the real driver; only this host provider admits the exchange. */
static struct mbuf *
ht_beacon(void)
{
	struct mbuf *old = beacon(0);
	struct mbuf *m;
	uint8_t *p;

	assert(old != NULL);
	m = newbuf(old->m_len + 52);
	assert(m != NULL);
	memcpy(m->m_data, old->m_data, old->m_len);
	p = m->m_data + old->m_len;
	ieee80211_ht20_write(p, &admitted);
	p += 28;
	memset(p, 0, 24);
	p[0] = 61; p[1] = 22; p[2] = 1; p[8] = 0xff;
	m_freem(old);
	return m;
}

static void
test_current_peer_beacons(void)
{
	for (unsigned running = 0; running < 2; running++) {
		for (unsigned variant = 0; variant < 33; variant++) {
			/* Reset reason and clause for each rejected variant. */
			static const uint8_t want_reason[33] = {
			    [2] = 3, [3] = 3, [4] = 3, [5] = 2, [6] = 2, [7] = 2,
			    [8] = 2, [9] = 3, [10] = 3, [11] = 3, [12] = 1,
			    [13] = 1, [18] = 2, [20] = 1, [21] = 1, [22] = 3,
			    [24] = 2, [25] = 2, [26] = 1, [27] = 1, [28] = 3,
			    [29] = 3 };
			static const uint8_t want_clause[33] = {
			    [2] = IEEE80211_HT20_BEACON_SECONDARY,
			    [3] = IEEE80211_HT20_BEACON_NO_CAP,
			    [4] = IEEE80211_HT20_BEACON_NO_OP,
			    [9] = IEEE80211_HT20_BEACON_SMPS,
			    [10] = IEEE80211_HT20_BEACON_MCS,
			    [11] = IEEE80211_HT20_BEACON_BASIC_MCS,
			    [22] = IEEE80211_HT20_BEACON_PRIMARY,
			    [28] = IEEE80211_HT20_BEACON_PROTECTION,
			    [29] = IEEE80211_HT20_BEACON_PROTECTION };
			struct mbuf *m;
			struct ieee80211_frame *wh;
			uint8_t *cap, *op;
			uint8_t seen_cap[4], seen_op[6], peer_cap[4], peer_op[6];
			uint8_t peer_mcs0 = 0;
			uint32_t resets;
			uint64_t epoch;
			/* Beacons may change the protection mode within the
			 * supported set and HT capability bits the contract does
			 * not use; everything the TX contract depends on stays. */
			int reject = variant != 0 && variant != 1 && variant != 14 &&
			    variant != 15 && variant != 16 && variant != 17 &&
			    variant != 19 && variant != 23 && variant != 30 &&
			    variant != 31 && variant != 32;

			ht_setup();
			CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
			release_capture();
			if (variant != 16 && variant != 17)
				deliver(ht_response(), IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
			if (variant == 17) ieee80211_ht20_reset(&test_node);
			/* Parent RUN completion is modeled here. The delayed RUN case
			 * remains in ASSOC with the actual native request queued. */
			if (running) test_ic.ic_state = IEEE80211_S_RUN;
			native_net.policy.accepting = 1;
			native_core.sc_ra.active = 1;
			epoch = native_net.policy.epoch;
			m = ht_beacon();
			wh = mtod(m, struct ieee80211_frame *);
			cap = m->m_data + m->m_len - 52;
			op = cap + 28;
			switch (variant) {
			case 1: op[4] = 6; break; /* Mode 2 + non-GF: supported. */
			case 2: op[3] = 5; break; /* Width/secondary channel. */
			case 3: cap[0] = 46; break; /* Missing HT Capabilities. */
			case 4: op[0] = 62; break; /* Missing HT Operation. */
			case 5: op[0] = 45; break; /* Duplicate cap, wrong length. */
			case 6: cap[0] = 61; break; /* Duplicate operation. */
			case 7: m->m_len--; break; /* Truncated HT operation. */
			case 8: cap[1] = 255; break; /* TLV early return. */
			case 9: cap[2] ^= 0x10; break; /* Peer capability changed. */
			case 10: cap[5] ^= 1; break; /* Peer RX MCS changed. */
			case 11: op[8] ^= 1; break; /* Basic MCS changed. */
			case 12: admitted.generation++; break;
			case 13: test_ic.ic_ht20_admit = NULL; break;
			case 14: wh->i_addr2[0] ^= 2; op[3] = 5; break;
			case 15: wh->i_addr3[0] ^= 2; op[3] = 5; break;
			case 16: case 17: op[3] = 5; break;
			case 28: op[4] = 7; break; /* Mode 3 is 5 GHz only. */
			case 29: op[4] = 1; break; /* Non-member protection. */
			case 30: cap[6] ^= 0xff; break; /* MCS8-15 bits: unused. */
			case 31: cap[4] ^= 0x03; break; /* A-MPDU factor: unused. */
			case 32: op[4] = 4; break; /* Mode 0, non-GF present. */
			case 18: m->m_len = sizeof(*wh) + 11; break;
			case 19: wh->i_fc[0] = IEEE80211_FC0_SUBTYPE_PROBE_RESP;
				break;
			case 20: test_ic.ic_flags ^= IEEE80211_F_PRIVACY; break;
			case 21: test_node.ni_chan = &test_ic.ic_channels[2]; break;
			case 22: op[2] = 2; break; /* Primary channel changed. */
			case 24: case 25: {
				struct mbuf *copy = newbuf(m->m_len + (variant == 24 ? 28 : 24));
				assert(copy != NULL);
				memcpy(copy->m_data, m->m_data, m->m_len);
				memcpy(copy->m_data + m->m_len, variant == 24 ? cap : op,
				    variant == 24 ? 28 : 24);
				/* Keep cap/op on the delivered copy, not the freed mbuf. */
				cap = copy->m_data + (cap - m->m_data);
				op = cap + 28;
				m_freem(m); m = copy;
				break;
			}
			case 26: test_node.ni_ht20.attempt++; break;
			case 27: test_node.ni_bssid[0] ^= 2;
				memcpy(wh->i_addr3, test_node.ni_bssid, 6); break;
			case 23: /* A newer SCAN already owns the association. */
				CHECK(rtw88_net_newstate(&test_ic, IEEE80211_S_SCAN, 0) == 0);
				epoch = native_net.policy.epoch;
				op[3] = 5;
				break;
			}
			/* The committed and offered octets the diagnostics copy. */
			memcpy(peer_cap, test_node.ni_ht20.peer_htcap, 4);
			memcpy(peer_op, test_node.ni_ht20.peer_htop, 6);
			peer_mcs0 = test_node.ni_ht20.peer_htop[6];
			memcpy(seen_cap, cap + 2, 4);
			memcpy(seen_op, op + 2, 6);
			resets = test_ic.ic_ht20_diag.resets;
			ieee80211_recv_mgmt_beacon(&test_ic, m, &test_node,
			    variant == 19 ? IEEE80211_FC0_SUBTYPE_PROBE_RESP :
			    IEEE80211_FC0_SUBTYPE_BEACON, 0, 0);
			if (reject) {
				const struct ieee80211_ht20_diag *d =
				    &test_ic.ic_ht20_diag;

				CHECK(d->resets == resets + 1 &&
				    d->reset_reason == want_reason[variant]);
				if (want_reason[variant] == IEEE80211_HT20_RESET_STALE)
					CHECK(d->reset_clause != 0);
				else
					CHECK(d->reset_clause == want_clause[variant]);
				if (want_reason[variant] == IEEE80211_HT20_RESET_HT) {
					static const uint8_t zero[6];

					CHECK(memcmp(d->reset_peer_cap, peer_cap, 4) == 0);
					CHECK(memcmp(d->reset_peer_op, peer_op, 6) == 0);
					CHECK(memcmp(d->reset_cap, variant == 3 ? zero :
					    seen_cap, 4) == 0);
					CHECK(memcmp(d->reset_op, variant == 4 ? zero :
					    seen_op, 6) == 0);
					if (variant == 11)
						CHECK(d->reset_mcs[0] == 0 &&
						    d->reset_mcs[1] == op[8] &&
						    d->reset_mcs[2] == peer_mcs0);
					else
						CHECK(d->reset_mcs[0] == 0 &&
						    d->reset_mcs[1] == 0 &&
						    d->reset_mcs[2] == 0);
				}
			}
			m_freem(m);
			if (reject) {
				CHECK(!test_node.ni_ht20.valid && !test_node.ni_ht20.offered);
				CHECK(!(test_node.ni_flags & IEEE80211_NODE_AMSDU_RX));
				CHECK(native_net.requested == IEEE80211_S_INIT);
				CHECK(!native_net.policy.accepting && !native_core.sc_ra.active);
				CHECK(native_net.policy.epoch != epoch);
			} else if (variant == 23) {
				CHECK(native_net.requested == IEEE80211_S_SCAN);
				CHECK(native_net.policy.epoch == epoch);
			} else {
				CHECK(native_net.policy.epoch == epoch);
				if (variant != 16 && variant != 17)
					CHECK(test_node.ni_ht20.valid);
			}
			CHECK(node_refs == 1 && captured == NULL);
		}
	}
}

static void
test_protection_qualification(void)
{
 for(unsigned kind=0;kind<23;kind++) {
  ht_setup();CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);release_capture();
  struct mbuf *m=ht_response();uint8_t *cap=m->m_data+m->m_len-52,*op=cap+28;
  op[3]=5;op[4]=6;
  switch(kind) {
  case 1: op[3]=0;op[4]=0;break;
  case 2: op[3]=7;break;
  case 3: op[4]=1;break;
  case 4: op[4]=2;break;
  case 5: op[4]=3;break;
  case 6: op[4]=4;break; /* Mode 0 with non-GF present: qualifies. */
  case 7: op[4]=7;break;
  case 8: cap[2]|=0x10;break;
  case 9: cap[2]&=~0x0c;break;
  case 10: cap[2]=(cap[2]&~0x0c)|4;break;
  case 11: op[3]|=8;break;
  case 12: op[3]|=0x10;break;
  case 13: op[3]=2;break;
  case 14: op[3]=4;break;
  case 15: op[6]=1;break;
  case 16: op[7]=0x80;break;
  case 17: op[5]=1;break;
  case 21: op[4]=0x17;break; /* OBSS non-HT STAs present. */
  case 22: op[4]=5;break; /* Non-member protection. */
  }
  deliver(m,IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
  CHECK(test_node.ni_ht20.valid);
  if(kind==18)admitted.generation++;
  if(kind==19)test_ic.ic_ht20_admit=NULL;
  if(kind==20)test_ic.ic_flags|=IEEE80211_F_USEPROT;
  struct ieee80211_ht20_protection result;
  memset(&result,0xff,sizeof(result));
  int error=ieee80211_ht20_tx_protection(&test_ic,&test_node,&result);
  /* Non-HT mixed mode 3 (5 and 7) stays closed on this 2.4 GHz primary. */
  if(kind<3 || kind==6) {
   CHECK(error==0 && result.qualification==IEEE80211_HT20_PROT_MF20_NO_RTS);
   CHECK(result.generation==admitted.generation && result.peer_cap==0x0c);
   CHECK(result.operation==(kind==1?0:kind==6?4:6) && result.secondary==(kind==1?0:kind==2?7:5));
  } else CHECK(error!=0 && result.qualification==0 && result.generation==0);
 }
}

static void
test_golden_and_reassociation(void)
{
	const uint8_t golden[28]={45,26,12,0,16,255,255,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0};
	ht_setup(); CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);
	CHECK(ht_in_request()!=NULL && memcmp(ht_in_request(),golden,sizeof(golden))==0);
	/* A packet missing its generated IE cannot prove a queued offer. */
	CHECK(captured!=NULL && ieee80211_ht20_request_offer(captured,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,&admitted,0));
	uint8_t *p=ht_in_request(); if(p) p[0]=46;
	CHECK(captured==NULL || !ieee80211_ht20_request_offer(captured,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,&admitted,0));
	release_capture();
	ht_setup(); CHECK(request(IEEE80211_FC0_SUBTYPE_REASSOC_REQ)==0); release_capture();
	struct mbuf *m=ht_response();
	deliver(m,IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(test_node.ni_ht20.offered && !test_node.ni_ht20.valid); /* wrongsubtype ignored */
	m=ht_response(); mtod(m,struct ieee80211_frame *)->i_fc[0]=IEEE80211_FC0_SUBTYPE_REASSOC_RESP;
	deliver(m,IEEE80211_FC0_SUBTYPE_REASSOC_RESP);
	CHECK(test_node.ni_ht20.valid && native_net.requested==IEEE80211_S_RUN);
}
/* The actual setoptie with a copyin that can fault and counted storage. */
static int optie_copyin_fail;
static unsigned optie_live;
static int optie_copyin(const void *in, void *out, size_t n)
{ if (optie_copyin_fail) return EFAULT; memcpy(out, in, n); return 0; }
static void *optie_malloc(size_t n)
{ void *p = malloc(n != 0 ? n : 1); assert(p != NULL); optie_live++; return p; }
static void optie_free(void *p)
{ assert(optie_live > 0); optie_live--; free(p); }
#ifndef IEEE80211_MAX_OPT_IE
#define IEEE80211_MAX_OPT_IE 256
#endif
#ifndef M_WAITOK
#define M_WAITOK 0
#endif
#define copyin optie_copyin
#define malloc(n, t, f) optie_malloc(n)
#define free(p, t) optie_free(p)
#include "native-ht20-setoptie.inc"
#undef copyin
#undef malloc
#undef free
/* A faulting copyin keeps the installed element; nothing leaks. */
static void
test_setoptie_copyin_fault(void)
{
	uint8_t first[4] = { 221, 2, 1, 2 }, second[4] = { 48, 2, 1, 0 };
	struct ieee80211req req;
	uint8_t *saved;
	unsigned saved_len;

	ht_setup();
	saved = test_ic.ic_opt_ie;
	saved_len = test_ic.ic_opt_ie_len;
	test_ic.ic_opt_ie = NULL;
	test_ic.ic_opt_ie_len = 0;
	memset(&req, 0, sizeof(req));
	req.i_len = sizeof(first);
	req.i_data = first;
	CHECK(ieee80211_ioctl_setoptie(&test_ic, &req) == 0);
	CHECK(test_ic.ic_opt_ie_len == 4 && optie_live == 1 &&
	    test_ic.ic_opt_ie != NULL &&
	    memcmp(test_ic.ic_opt_ie, first, 4) == 0);
	optie_copyin_fail = 1;
	req.i_data = second;
	CHECK(ieee80211_ioctl_setoptie(&test_ic, &req) == EFAULT);
	CHECK(test_ic.ic_opt_ie_len == 4 && optie_live == 1 &&
	    test_ic.ic_opt_ie != NULL &&
	    memcmp(test_ic.ic_opt_ie, first, 4) == 0);
	optie_copyin_fail = 0;
	req.i_len = IEEE80211_MAX_OPT_IE + 1;
	CHECK(ieee80211_ioctl_setoptie(&test_ic, &req) == EINVAL);
	CHECK(optie_live == 1 && test_ic.ic_opt_ie != NULL &&
	    memcmp(test_ic.ic_opt_ie, first, 4) == 0);
	req.i_len = sizeof(second);
	CHECK(ieee80211_ioctl_setoptie(&test_ic, &req) == 0);
	CHECK(test_ic.ic_opt_ie_len == 4 && optie_live == 1 &&
	    test_ic.ic_opt_ie != NULL &&
	    memcmp(test_ic.ic_opt_ie, second, 4) == 0);
	optie_free(test_ic.ic_opt_ie);
	test_ic.ic_opt_ie = saved;
	test_ic.ic_opt_ie_len = saved_len;
	CHECK(optie_live == 0);
}

static void
test_probe_and_optional_compatibility(void)
{
	const uint8_t ht[]={45,0}, vht[]={191,0}, vendor[]={221,4,0,0x90,0x4c,0x33};
	const uint8_t rsn[]={48,2,1,0};
	const uint8_t empty_ssid[]={0};
	const uint8_t wme[]={221,7,0,0x50,0xf2,2,0,1,0};
	const uint8_t *bad[]={ht,vht,vendor};
	const size_t sizes[]={sizeof(ht),sizeof(vht),sizeof(vendor)};
	for (unsigned i=0;i<3;i++) {
		ht_setup(); test_ic.ic_ht20_admit=NULL;
		CHECK(ieee80211_send_probereq(&test_node,test_ic.ic_myaddr,test_node.ni_macaddr,
		    test_node.ni_bssid,empty_ssid,0,bad[i],sizes[i])==EINVAL);
		CHECK(captured==NULL && node_refs==1);
	}
	ht_setup(); test_ic.ic_ht20_admit=NULL;
	test_ic.ic_opt_ie=(uint8_t *)rsn; test_ic.ic_opt_ie_len=sizeof(rsn);
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0 && captured!=NULL); release_capture();
	CHECK(ieee80211_ht20_optional(wme,sizeof(wme))==0);
	CHECK(ieee80211_ht20_optional(wme,sizeof(wme)-1)==0); /* existing opaque malformedWME policy */
	ht_setup(); test_ic.ic_opt_ie=(uint8_t *)wme; test_ic.ic_opt_ie_len=sizeof(wme);
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==EINVAL); /* newHTrequiresactualuniqueWME */
	CHECK(!test_node.ni_ht20.offered && captured==NULL && node_refs==1);
}

/*
 * With privacy, the HT/VHT gate reads the pairwise suite of this station's
 * own RSN element in ic_opt_ie. wpa_supplicant installs that element but
 * never sets the kernel unicast cipher, so ni_rsn keeps its WEP default.
 * Exactly one RSN element with a single CCMP-128 pairwise suite qualifies.
 */
static void
test_privacy_rsn_gate(void)
{
	static const uint8_t ccmp[22]={48,20,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,
	    1,0,0,0x0f,0xac,2,0x80,0};
	static const uint8_t two[26]={48,24,1,0,0,0x0f,0xac,4,2,0,0,0x0f,0xac,4,
	    0,0x0f,0xac,2,1,0,0,0x0f,0xac,2,0,0};
	static const uint8_t wpa[24]={221,22,0,0x50,0xf2,1,1,0,0,0x50,0xf2,4,1,0,
	    0,0x50,0xf2,4,1,0,0,0x50,0xf2,2};
	uint8_t ie[64];
	for (unsigned variant=0;variant<15;variant++) {
		size_t len=sizeof(ccmp);
		memcpy(ie,ccmp,sizeof(ccmp));
		switch(variant) {
		case 1: ie[13]=2; break;			/* TKIP pairwise. */
		case 2: memcpy(ie,two,sizeof(two)); len=sizeof(two); break;
		case 3: memcpy(ie+len,ccmp,sizeof(ccmp)); len*=2; break; /* Two RSN. */
		case 4: ie[1]=40; break;			/* Overlong element. */
		case 5: len=0; break;				/* No RSN element. */
		case 6: memcpy(ie,wpa,sizeof(wpa)); len=sizeof(wpa); break;
		case 7: ie[2]=2; break;				/* RSN version 2. */
		case 8: ie[1]=11; len=13; break;		/* No pairwise suite. */
		case 9: ie[11]=0x0e; break;			/* Other OUI. */
		case 10: ie[3]=1; break;			/* Version 0x0101. */
		case 11: ie[9]=1; break;			/* Count 0x0101. */
		case 12: ie[13]=8; break;			/* GCMP-128. */
		case 13: ie[13]=10; break;			/* CCMP-256. */
		case 14: ie[10]=1; break;			/* OUI byte 0. */
		}
		ht_setup();
		test_ic.ic_flags|=IEEE80211_F_PRIVACY|IEEE80211_F_WPA2;
		/* The kernel cipher field no longer decides either way. */
		test_node.ni_rsn.rsn_ucastcipher=variant==0?
		    IEEE80211_CIPHER_WEP:IEEE80211_CIPHER_AES_CCM;
		test_ic.ic_opt_ie=len?ie:NULL; test_ic.ic_opt_ie_len=(uint16_t)len;
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);
		CHECK((ht_in_request()!=NULL)==(variant==0));
		CHECK(test_node.ni_ht20.offered==(variant==0));
		release_capture();
		if (variant==0) {
			deliver(ht_response(),IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
			CHECK(test_node.ni_ht20.valid);
		}
	}
	/* A stale CCMP RSN element without the WPA2 configuration (a killed
	 * supplicant, then a WEP join) offers no HT. */
	{
		static const uint8_t rsn[22]={48,20,1,0,0,0x0f,0xac,4,1,0,0,
		    0x0f,0xac,4,1,0,0,0x0f,0xac,2,0,0};

		ht_setup();
		test_ic.ic_flags|=IEEE80211_F_PRIVACY;
		test_ic.ic_opt_ie=(uint8_t *)rsn; test_ic.ic_opt_ie_len=sizeof(rsn);
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);
		CHECK(ht_in_request()==NULL && !test_node.ni_ht20.offered);
		release_capture();
		ht_setup();
		test_ic.ic_flags|=IEEE80211_F_PRIVACY|IEEE80211_F_WPA2;
		test_ic.ic_opt_ie=(uint8_t *)rsn; test_ic.ic_opt_ie_len=sizeof(rsn);
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);
		CHECK(ht_in_request()!=NULL && test_node.ni_ht20.offered);
		release_capture();
	}
}

static void
test_profile_numeric_bounds(void)
{
	for (unsigned ampdu=0;ampdu<256;ampdu++) {
		ht_setup(); admitted.ampdu=(uint8_t)ampdu;
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);
		uint8_t *p=ht_in_request();
		CHECK((p!=NULL)==((ampdu&0xe3)==0));
		if (p) CHECK(p[4]==ampdu && test_node.ni_ht20.offered);
		release_capture(); CHECK(node_refs==1);
	}
}

static void
test_maximum_producer_budget(void)
{
	for (unsigned reassoc=0;reassoc<2;reassoc++) {
		ht_setup();
		test_node.ni_esslen=sizeof(test_node.ni_essid);
		memset(test_node.ni_essid,'x',sizeof(test_node.ni_essid));
		test_node.ni_rates.rs_nrates=IEEE80211_RATE_MAXSIZE;
		for (unsigned i=0;i<IEEE80211_RATE_MAXSIZE;i++)
			test_node.ni_rates.rs_rates[i]=(uint8_t)(2+i);
		CHECK(request(reassoc?IEEE80211_FC0_SUBTYPE_REASSOC_REQ:
		    IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);
		CHECK(ht_in_request()!=NULL && test_node.ni_ht20.offered);
		release_capture(); CHECK(node_refs==1);
	}
}

static void
test_manual_ht_rejected(void)
{
	const uint8_t ids[] = {45,61,191,192};
	const uint8_t lengths[] = {26,22,12,5};
	for (unsigned i=0;i<sizeof(ids);i++) {
		uint8_t ie[28] = {0};
		setup(); ie[0]=ids[i]; ie[1]=lengths[i];
		test_ic.ic_opt_ie=ie; test_ic.ic_opt_ie_len=lengths[i]+2;
		CHECK(ieee80211_send_mgmt(&test_ic,&test_node,
		    IEEE80211_FC0_SUBTYPE_ASSOC_REQ,0)==EINVAL);
		CHECK(captured==NULL && node_refs==1);
		release_capture();
	}
}
int
main(void)
{
    /* Out-of-domain posted DMA must not be silently reported drained. */
    struct rtw88_softc boundary={.modeled_tx_pending=1};unsigned pending=99;
    mutex_enter(&native_lock);
    CHECK(rtw88_tx_drain_snapshot(&boundary,&pending)==EOPNOTSUPP && pending==99);
    mutex_exit(&native_lock);
	test_manual_ht_rejected();
	test_one_association_response();
	test_pending_identity_guards();
	test_actual_exchange();
	test_callback_epoch_cancellation();
	test_saved_offer_epochs();
	test_local_admission();
	test_pending_producer_boundaries();
	test_producer_failures();
	test_response_rejects();
	test_response_limits();
	test_direct_run_failure_ownership();
	test_delayed_worker_profile();
	test_current_peer_beacons();
	test_protection_qualification();
	test_golden_and_reassociation();
	test_probe_and_optional_compatibility();
	test_setoptie_copyin_fault();
	test_privacy_rsn_gate();
	test_profile_numeric_bounds();
	test_maximum_producer_budget();
	printf("Native HT20 association: %u checks, %u failures\n",checks,failures);
	return failures!=0;
}
