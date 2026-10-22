/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual shared association producer/consumer; kernel services from WME fixture. */
#define main ht20_prior_wme_main
#include "net80211-wme-contract.c"
#undef main
#include "native-ht20-policy.h"
#include <stdarg.h>

/* Actual native callback and complete worker use explicit kernel/core services.
 * RF programming and full core I/O are not claimed by this exchange. */
struct rtw88_softc { int sc_started, sc_running, sc_stop_requested; unsigned sc_channel, sc_generation, sc_h2c_box, modeled_tx_pending; int sc_linked; struct { int active, configured; } sc_ra; };
struct rtw88_net {
	struct { struct ifnet ec_if; } ec;
	struct ieee80211com ic;
	void *dev, *softint;
	unsigned configured_media;
	int (*parent_state)(struct ieee80211com *, enum ieee80211_state, int);
	struct rtw88_softc *core;
	struct rtw88_net_policy policy;
	int *lock;
	int scan, work_pending, reset_required, state_arg, state_work;
	unsigned channel;
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
static unsigned worker_diag_stage;
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
static int rtw88_station_config(struct rtw88_softc *sc,const uint8_t *bssid,unsigned aid,unsigned basic,unsigned slot,int linked)
{ (void)bssid;(void)aid;(void)basic;(void)slot;assert(native_lock);worker_hw_calls++;sc->sc_linked=linked;return 0; }
static int rtw88_edca_apply(struct rtw88_softc *sc,struct rtw88_edca_params *p,unsigned slot)
{ (void)sc;(void)p;(void)slot;assert(native_lock);worker_hw_calls++;return 0; }
static int rtw88_ra_configure(struct rtw88_softc *sc,struct rtw88_ra_config *p)
{ (void)p;assert(native_lock);worker_hw_calls++;sc->sc_ra.active=1;return 0; }
static int rtw88_stop(struct rtw88_softc *sc)
{ assert(!native_lock);sc->sc_started=sc->sc_running=sc->sc_linked=0;return 0; }
int rtw88_net_state_format(char *buf,size_t len,const struct rtw88_net_state_diag *diag)
{ worker_diag_stage=diag->stage;return snprintf(buf,len,"stage%u",diag->stage); }
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
  worker_parent_error=worker_parent_newer=worker_init_error=0;worker_diag_stage=0;worker_newer_epoch=0;
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
		for (unsigned variant = 0; variant < 28; variant++) {
			struct mbuf *m;
			struct ieee80211_frame *wh;
			uint8_t *cap, *op;
			uint64_t epoch;
			int reject = variant != 0 && variant != 14 &&
			    variant != 15 && variant != 16 && variant != 17 &&
			    variant != 19 && variant != 23;

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
			case 1: op[4] = 6; break; /* Changed operation. */
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
			case 14: wh->i_addr2[0] ^= 2; op[4] = 6; break;
			case 15: wh->i_addr3[0] ^= 2; op[4] = 6; break;
			case 16: case 17: op[4] = 6; break;
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
				m_freem(m); m = copy;
				break;
			}
			case 26: test_node.ni_ht20.attempt++; break;
			case 27: test_node.ni_bssid[0] ^= 2;
				memcpy(wh->i_addr3, test_node.ni_bssid, 6); break;
			case 23: /* A newer SCAN already owns the association. */
				CHECK(rtw88_net_newstate(&test_ic, IEEE80211_S_SCAN, 0) == 0);
				epoch = native_net.policy.epoch;
				op[4] = 6;
				break;
			}
			ieee80211_recv_mgmt_beacon(&test_ic, m, &test_node,
			    variant == 19 ? IEEE80211_FC0_SUBTYPE_PROBE_RESP :
			    IEEE80211_FC0_SUBTYPE_BEACON, 0, 0);
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
test_golden_and_reassociation(void)
{
	const uint8_t golden[28]={45,26,12,0,16,255,255,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0};
	ht_setup(); CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ)==0);
	CHECK(ht_in_request()!=NULL && memcmp(ht_in_request(),golden,sizeof(golden))==0);
	/* A packet missing its generated IE cannot prove a queued offer. */
	CHECK(captured!=NULL && ieee80211_ht20_request_offer(captured,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,&admitted));
	uint8_t *p=ht_in_request(); if(p) p[0]=46;
	CHECK(captured==NULL || !ieee80211_ht20_request_offer(captured,IEEE80211_FC0_SUBTYPE_ASSOC_REQ,&admitted));
	release_capture();
	ht_setup(); CHECK(request(IEEE80211_FC0_SUBTYPE_REASSOC_REQ)==0); release_capture();
	struct mbuf *m=ht_response();
	deliver(m,IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(test_node.ni_ht20.offered && !test_node.ni_ht20.valid); /* wrongsubtype ignored */
	m=ht_response(); mtod(m,struct ieee80211_frame *)->i_fc[0]=IEEE80211_FC0_SUBTYPE_REASSOC_RESP;
	deliver(m,IEEE80211_FC0_SUBTYPE_REASSOC_RESP);
	CHECK(test_node.ni_ht20.valid && native_net.requested==IEEE80211_S_RUN);
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
	test_actual_exchange();
	test_local_admission();
	test_producer_failures();
	test_response_rejects();
	test_response_limits();
	test_direct_run_failure_ownership();
	test_delayed_worker_profile();
	test_current_peer_beacons();
	test_golden_and_reassociation();
	test_probe_and_optional_compatibility();
	test_profile_numeric_bounds();
	test_maximum_producer_budget();
	printf("Native HT20 association: %u checks, %u failures\n",checks,failures);
	return failures!=0;
}
