/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* Exercise the actual net key fence with the actual copied async queue. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <dev/ic/rtw88_async.h>
#include <dev/ic/rtw88_net_policy.h>
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH, IEEE80211_S_ASSOC, IEEE80211_S_RUN };
#define IEEE80211_F_SHSLOT 1U
#define IEEE80211_F_USEPROT 2U
#define IEEE80211_M_STA 1
#define RTW88_RA_LEGACY 0
#define IEEE80211_FIXED_RATE_NONE -1
#define RTW88_BAND_2G 1U
#define RTW88_NET_TX_BUDGET 16U
#define MAX(a,b) ((a)>(b)?(a):(b))
#define RTW88_EXEC_CONTROL 1U
#define RTW88_EXEC_TX 2U
#define RTW88_EXEC_STOP 8U
struct rtw88_net;
struct rtw88_softc;
struct rtw88_chip_ops { int (*prepare_station)(struct rtw88_softc *,int (*)(void *),void *); };
static const struct rtw88_chip_ops no_prepare = {0};
struct rtw88_softc { const struct rtw88_chip_ops *sc_chip; void *sc_rx_frame,*sc_tx_complete,*sc_net_cookie; struct rtw88_net *sc_net; void *sc_executor; uint32_t sc_generation; unsigned sc_channel; int sc_resources_ready; int sc_stop_requested,sc_hci_error,sc_dma_error,sc_power_error,sc_rf_enabled,sc_bootstrapping; int sc_started,sc_running,sc_powered,sc_power_dirty,sc_dma_master; };
struct ieee80211_node { unsigned *ni_chan; uint8_t ni_bssid[6]; unsigned ni_associd; struct { unsigned rs_nrates; } ni_rates; struct { int valid; } ni_ht20; };
struct ifnet { void *if_softc; unsigned if_flags,if_timer; };
#define mstohz(x) (x)
#define IFF_UP 4U
#define IFF_RUNNING 1U
#define IFF_OACTIVE 2U
#define aprint_error_dev(dev,fmt,...) ((void)(dev))
struct ieee80211com { uint64_t ic_sae_serial; struct ifnet *ic_ifp; struct ieee80211_node *ic_bss; unsigned ic_flags; int ic_fixed_rate,ic_state,ic_mgt_timer,ic_mgtq,ic_opmode; unsigned *ic_curchan; };
#define ieee80211_sae_serial(ic) ((ic)->ic_sae_serial)
typedef int kmutex_t;
struct rtw88_net_hw_snapshot { uint32_t generation; int started,running,stop_requested,linked,power_ready; struct {int active,configured,phy;} ra; struct {int channel_valid; struct rtw88_channel_tuple channel;} phy; };
struct rtw88_net {
 kmutex_t *lock; struct rtw88_softc *core; void *dev; struct { struct ifnet ec_if; } ec; uint64_t key_epoch, state_sae_serial; unsigned key_depth;
 struct ieee80211com ic; int requested,state_arg,work_pending,run_pending,scan,tx_retry,state_again,state_running,state_enqueued,csa_pending; unsigned tx_timer,tx_producers; struct rtw88_channel_tuple pending_channel; unsigned configured_media,channel; struct rtw88_net_neg neg;
 struct { void *m; } rx[RTW88_NET_RX_LIMIT]; void *wq; int state_work,stack_detached;
 int init_failed,parent_init_failed; int (*parent_state)(struct ieee80211com *,enum ieee80211_state,int);
 int quiet_delivered,quiet_pending; int key_error,tx_quiet,io_active,prep_granted,prep_requested,stop_pending,stop_hold;
 struct rtw88_async async; struct rtw88_net_policy policy; void *softint;
 struct rtw88_net_hw_snapshot hw; struct { unsigned bands; } caps;
 struct rtw88_control_snapshot control_pending,control_owner;
 struct rtw88_control_result control_result;
 struct rtw88_net_state_diag control_pending_diag,control_owner_diag,control_result_diag;
 struct rtw88_async_epoch control_last;
 int control_last_valid,control_pending_valid,control_active,control_ready;
 int control_pending_wme,control_owner_wme,control_result_wme,wme_pending;
 uint64_t wme_epoch,async_failed,stop_epoch; int init_requested,async_fault,stop_error,stop_pause,quiet_error,reset_required;
 struct { int init_pending,init_error; uint32_t init_generation; } stats;
};
#define KASSERT(x) assert(x)
static void kernel_unlock_all(void *lwp,int *nlocks){assert(lwp==NULL);*nlocks=0;}
static void kernel_lock(int nlocks,void *lwp){assert(nlocks==0 && lwp==NULL);}
#define KERNEL_UNLOCK_ALL kernel_unlock_all
#define KERNEL_LOCK kernel_lock
#define mutex_owned(m) (*(m))
static unsigned wakes,scheduled;
static void rtw88_net_state_schedule_locked(struct rtw88_net *n){(void)n;scheduled++;}
static void rtw88_net_state_idle_locked(struct rtw88_net *n){n->state_running=0;}
static void rtw88_net_hw_get_locked(struct rtw88_net *n,struct rtw88_net_hw_snapshot *h){*h=n->hw;}
static int ieee80211_new_state(struct ieee80211com *i,int s,int a){(void)i;(void)s;(void)a;return 0;}
static void ieee80211_drain_ifq(int *q){(void)q;}
static void mutex_enter(kmutex_t *m){assert(*m==0);*m=1;}
static void mutex_exit(kmutex_t *m){assert(*m==1);*m=0;}
static void softint_schedule(void *p){(void)p;wakes++;}
static void rtw88_net_tx_pending_discard(struct rtw88_net *n){(void)n;}
static int drain_error=EIO; static unsigned destroyed,freed;
static int rtw88_executor_drain(struct rtw88_softc *s){(void)s;return drain_error;}
static int rtw88_executor_destroy(struct rtw88_softc *s){(void)s;destroyed++;return 0;}
void rtw88_net_close(struct rtw88_net_policy *p){p->dying=1;p->accepting=0;p->epoch++;}
static int kpause(const char *s,bool b,int n,void *p){(void)s;(void)b;(void)n;(void)p;return 0;}
static void workqueue_wait(void *q,void *w){(void)q;(void)w;}
static void ieee80211_ifdetach(struct ieee80211com *i){(void)i;}
static void if_detach(struct ifnet *i){(void)i;}
static void softint_disestablish(void *p){(void)p;}
static void workqueue_destroy(void *p){(void)p;}
static void m_freem(void *p){(void)p;}
static void rtw88_net_rxba_detach(struct rtw88_net *n){(void)n;}
static void callout_destroy(int *c){(void)c;}
static void kmem_free(void *p,size_t n){(void)p;(void)n;freed++;}

uint64_t rtw88_net_request(struct rtw88_net_policy *p){p->accepting=0;return ++p->epoch;}
static void rtw88_net_ht20_publish(struct rtw88_net *n,void *p){(void)n;(void)p;}
static void ieee80211_rxba_reset(struct ieee80211com *i,struct ieee80211_node *n){(void)i;(void)n;}
static void callout_stop(int *c){(void)c;}
static void callout_halt(int *c,void *m){(void)c;(void)m;}
static void rtw88_net_rxba_quiesce(struct rtw88_net *n){(void)n;}
static void callout_schedule(int *c,int t){(void)c;(void)t;}
static int getticks(void){return 1;}
int rtw88_net_commit(struct rtw88_net_policy *p,uint64_t e,int x,int a){if(p->epoch!=e)return ECANCELED;p->accepting=a;return x;}
int rtw88_net_beacon_start(struct rtw88_net_policy *p,uint64_t e,uint32_t t){(void)p;(void)e;(void)t;return 0;}
int rtw88_net_state_format(char *p,size_t n,const struct rtw88_net_state_diag *d){(void)p;(void)n;(void)d;return 0;}
static unsigned ieee80211_chan2ieee(struct ieee80211com *i,unsigned *c){(void)i;return *c;}

static uint32_t executor_reasons;
static void rtw88_executor_schedule(struct rtw88_softc *s,uint32_t r){(void)s;executor_reasons|=r;}
static int fake_owner=1;
static int rtw88_executor_owned(struct rtw88_softc *s){(void)s;return fake_owner;}
static unsigned rtw88_net_tx_queued(const struct rtw88_net *n){return n->async.total;}
static void rtw88_net_quiet_locked(struct rtw88_net *n){n->tx_quiet=1;}
static void rtw88_net_fault(struct rtw88_net *n,int e){n->key_error=e;}
static struct rtw88_net *parent_mutation;
static unsigned parent_calls;
static int parent_retires;
static int parent_state(struct ieee80211com *ic,enum ieee80211_state s,int a){(void)ic;(void)s;(void)a;parent_calls++;if(parent_retires){ic->ic_sae_serial++;return EACCES;}if(parent_mutation)parent_mutation->policy.epoch++;return 0;}
static struct rtw88_net *tx_test_net;
static int tx_error=EIO;
static enum rtw88_tx_progress tx_progress=RTW88_TX_MAY_HAVE_WRITTEN;
static int rtw88_tx_execute(struct rtw88_softc *s,const struct rtw88_tx_job *j,enum rtw88_tx_progress *p)
{(void)s;(void)j;assert(tx_test_net && !*tx_test_net->lock);*p=tx_progress;return tx_error;}
static uint32_t rtw88_net_now_ms(void){return 1;}
static unsigned stop_calls;
static int rtw88_tx_pause(struct rtw88_softc *s){(void)s;return EIO;}
static int rtw88_stop(struct rtw88_softc *s){stop_calls++;s->sc_running=0;return 0;}
static struct rtw88_net *invalidate_net;
static unsigned channel_calls,station_calls,mailbox_ends;
static int rtw88_net_control_drain(struct rtw88_net *n,const struct rtw88_control_snapshot *r){(void)n;(void)r;return 0;}
static int rtw88_net_control_mailbox(struct rtw88_net *n,const struct rtw88_control_snapshot *r,unsigned c){(void)n;(void)r;(void)c;return 0;}
static int rtw88_start_tuple(struct rtw88_softc *s,const struct rtw88_channel_tuple *c){(void)s;(void)c;return 0;}
static void rtw88_ra_invalidate(struct rtw88_softc *s){(void)s;}
static void rtw88_dfs_close(struct rtw88_softc *s){(void)s;}
static unsigned rtw88_station_commands(struct rtw88_softc *s,int linked){(void)s;(void)linked;return 1;}
static int rtw88_channel_apply(struct rtw88_softc *s,const struct rtw88_channel_tuple *c){(void)s;(void)c;channel_calls++;if(invalidate_net)invalidate_net->policy.epoch++;return 0;}
static int rtw88_station_config(struct rtw88_softc *s,const uint8_t *b,unsigned a,unsigned r,unsigned l,int u){(void)s;(void)b;(void)a;(void)r;(void)l;(void)u;station_calls++;return 0;}
static int rtw88_edca_apply(struct rtw88_softc *s,const struct rtw88_edca_params *p,unsigned l){(void)s;(void)p;(void)l;return 0;}
static int rtw88_ra_configure(struct rtw88_softc *s,const struct rtw88_ra_config *r){(void)s;(void)r;return 0;}
static int rtw88_fw_mailbox_end(struct rtw88_softc *s,int e){(void)s;mailbox_ends++;return e;}
static struct rtw88_net *prepare_target;
static unsigned prepare_calls, prepare_mutation;
static int fixture_prepare(struct rtw88_softc *sc,int (*current)(void *),void *cookie)
{
 struct rtw88_net *n=prepare_target;
 assert(n && sc==n->core && !*n->lock && current(cookie)==0);
 prepare_calls++;
 switch(prepare_mutation) {
 case 1: n->policy.epoch++; break;
 case 2: n->key_epoch++; break;
 case 3: n->core->sc_generation++; break;
 case 4: n->policy.dying=1; break;
 case 5: n->stop_pending=1; break;
 case 6: n->tx_quiet=1; break;
 case 7: n->ic.ic_sae_serial++; break;
 }
 if(prepare_mutation) assert(current(cookie)!=0);
 return 0;
}
static const struct rtw88_chip_ops with_prepare = { fixture_prepare };
static struct rtw88_net *snapshot_mutation;
static unsigned rtw88_net_basic_rates(struct ieee80211com *ic){(void)ic;return 0xfff;}
static int ieee80211_ht20_state_request(struct ieee80211com *ic,int state){(void)ic;(void)state;return 0;}
static int rtw88_net_edca_prepare(struct ieee80211com *ic,unsigned m,int state,unsigned c,unsigned slot,struct rtw88_edca_params *e){(void)ic;(void)m;(void)state;(void)c;(void)slot;(void)e;if(snapshot_mutation)snapshot_mutation->policy.epoch++;return 0;}
static int rtw88_net_ra_prepare(struct ieee80211com *ic,unsigned c,unsigned rates,uint64_t epoch,struct rtw88_ra_config *r){(void)ic;(void)c;(void)rates;(void)epoch;(void)r;return 0;}



#include "net-key-functions.inc"
int main(void)
{
 kmutex_t lock=0;
 struct rtw88_net n={.lock=&lock};
 struct rtw88_async_epoch id={.generation=1,.epoch=2,.key_epoch=0};
 rtw88_async_init(&n.async,&id); n.policy.epoch=2; n.policy.accepting=1;
 lock=1;
 assert(rtw88_net_prepare_locked(&n)==0 && n.prep_requested);
 assert(rtw88_net_io_begin(&n)==0 && n.prep_granted && scheduled==1);
 assert(rtw88_net_prepare_locked(&n)==1);
 rtw88_net_prepare_release_locked(&n);
 assert(!n.prep_granted && rtw88_net_io_begin(&n)==1 && n.io_active);
 struct rtw88_async_epoch snapshot={0};
 assert(rtw88_net_rx_epoch(&n,1,&snapshot)==0 && snapshot.epoch==2);
 n.key_depth=1; assert(rtw88_net_rx_epoch(&n,1,&snapshot)==EAGAIN); n.key_depth=0;
 n.prep_requested=1; assert(rtw88_net_io_end(&n)==0 && n.prep_granted && scheduled==2);
 n.prep_granted=0; lock=0;
 rtw88_net_key_enter(&n);
 assert(n.key_depth==1 && n.key_epoch==1 && n.async.id.key_epoch==1);
 rtw88_net_key_enter(&n);
 assert(n.key_depth==2 && n.key_epoch==1);
 rtw88_net_key_leave(&n); assert(n.key_depth==1 && wakes==0);
 rtw88_net_key_leave(&n); assert(n.key_depth==0 && wakes==1);
 n.key_epoch=UINT64_MAX-1; n.async.id.key_epoch=UINT64_MAX-1;
 rtw88_net_key_enter(&n);
 assert(n.async.closed && n.key_error==EOVERFLOW);
 rtw88_net_key_leave(&n); assert(wakes==1);
 struct rtw88_softc core={.sc_generation=3};
 struct rtw88_net c={.lock=&lock,.core=&core,.key_epoch=1};
 struct rtw88_async_epoch cid={.generation=3,.epoch=4,.key_epoch=1};
 struct rtw88_control_snapshot req={.kind=RTW88_CTL_STATE,.id=cid}, taken;
 rtw88_async_init(&c.async,&cid); c.policy.epoch=4; c.hw.generation=3;
 assert(rtw88_net_control_current(&c,&req)==0);
 core.sc_generation=4; assert(rtw88_net_control_current(&c,&req)==ESTALE);
 core.sc_generation=3; c.stop_pending=1;
 assert(rtw88_net_control_current(&c,&req)==ECANCELED); c.stop_pending=0;
 assert(rtw88_net_request_control(&c,&req)==0 && c.control_pending_valid);
 lock=1; c.io_active=1;
 assert(rtw88_net_control_take_locked(&c,&taken)==0 && c.control_active);
 lock=0; c.policy.epoch++;
 rtw88_net_control_done(&c,&cid,0);
 assert(!c.control_active && !c.control_ready && c.prep_requested);
 assert(rtw88_net_request_control(&c,&req)==ESTALE);
 core.sc_started=core.sc_running=1; c.stop_pending=1; c.tx_quiet=1;
 c.io_active=1; c.prep_granted=0;
 rtw88_net_stop_service(&c);
 assert(stop_calls==1 && c.stop_error==EIO && c.quiet_error==EIO);
 assert(c.prep_requested && !c.stop_pending);
 struct rtw88_softc sc2={.sc_chip=&no_prepare,.sc_generation=8,.sc_started=1};
 struct rtw88_net e={.lock=&lock,.core=&sc2,.key_epoch=2};
 struct rtw88_control_snapshot request={.kind=RTW88_CTL_STATE,.state=IEEE80211_S_RUN,.id={.generation=8,.epoch=9,.key_epoch=2}};
 e.caps.bands=RTW88_BAND_2G; e.policy.epoch=9; invalidate_net=&e;
 assert(rtw88_net_control_execute(&e,&request,0)==ECANCELED);
 assert(channel_calls==1 && station_calls==0 && mailbox_ends==1);
 e.policy.epoch=9; e.io_active=1; e.hw.generation=8;
 e.control_pending=request; e.control_pending_valid=1;
 channel_calls=station_calls=mailbox_ends=stop_calls=0;
 rtw88_net_control_service(&e);
 assert(channel_calls==1 && station_calls==0 && mailbox_ends==1);
 assert(stop_calls==1 && e.reset_required && !e.control_ready);
 /* Actual async AUTH executor, with and without the optional hook. */
 invalidate_net=NULL;
 for(unsigned mutation=0;mutation<=7;mutation++) {
  struct rtw88_softc corep={.sc_chip=&with_prepare,.sc_generation=8,.sc_started=1};
  struct rtw88_net netp={.lock=&lock,.core=&corep,.key_epoch=2};
  struct rtw88_control_snapshot auth={.kind=RTW88_CTL_STATE,.state=IEEE80211_S_AUTH,.id={.generation=8,.epoch=9,.key_epoch=2}};
  netp.caps.bands=RTW88_BAND_2G;netp.policy.epoch=9;prepare_target=&netp;prepare_mutation=mutation;
  channel_calls=station_calls=mailbox_ends=prepare_calls=0;
  int result=rtw88_net_control_execute(&netp,&auth,0);
  assert(result==(mutation?ECANCELED:0));
  assert(channel_calls==1 && prepare_calls==1 && station_calls==(mutation?0U:1U) && mailbox_ends==1);
 }
 /* Single executor retains hardware ownership through stale cleanup. */
 for(unsigned mutation=1;mutation<=7;mutation++) {
  struct rtw88_softc corep={.sc_chip=&with_prepare,.sc_generation=8,.sc_started=1,.sc_running=1};
  struct rtw88_net netp={.lock=&lock,.core=&corep,.key_epoch=2,.io_active=1};
  struct rtw88_control_snapshot auth={.kind=RTW88_CTL_STATE,.state=IEEE80211_S_AUTH,.id={.generation=8,.epoch=9,.key_epoch=2}};
  netp.caps.bands=RTW88_BAND_2G;netp.policy.epoch=9;netp.hw.generation=8;
  netp.control_pending=auth;netp.control_pending_valid=1;
  prepare_target=&netp;prepare_mutation=mutation;
  channel_calls=station_calls=mailbox_ends=prepare_calls=stop_calls=0;
  rtw88_net_control_service(&netp);
  assert(channel_calls==1 && prepare_calls==1 && station_calls==0 && mailbox_ends==1);
  assert(stop_calls==1 && netp.reset_required && !netp.policy.accepting);
  assert(!netp.control_ready || netp.control_result.error==ECANCELED);
 }
 {
  struct rtw88_softc corep={.sc_chip=&with_prepare,.sc_generation=8,.sc_started=1};
  struct rtw88_net netp={.lock=&lock,.core=&corep,.key_epoch=2};
  struct rtw88_control_snapshot scan={.kind=RTW88_CTL_STATE,.state=IEEE80211_S_SCAN,.id={.generation=8,.epoch=9,.key_epoch=2}};
  netp.caps.bands=RTW88_BAND_2G;netp.policy.epoch=9;
  channel_calls=station_calls=mailbox_ends=prepare_calls=0;
  assert(rtw88_net_control_execute(&netp,&scan,0)==0);
  assert(channel_calls==1 && prepare_calls==0 && station_calls==1 && mailbox_ends==1);
 }
 {
  struct rtw88_softc corep={.sc_chip=&no_prepare,.sc_generation=8,.sc_started=1};
  struct rtw88_net netp={.lock=&lock,.core=&corep,.key_epoch=2};
  struct rtw88_control_snapshot auth={.kind=RTW88_CTL_STATE,.state=IEEE80211_S_AUTH,.id={.generation=8,.epoch=9,.key_epoch=2}};
  netp.caps.bands=RTW88_BAND_2G;netp.policy.epoch=9;
  channel_calls=station_calls=mailbox_ends=prepare_calls=0;
  assert(rtw88_net_control_execute(&netp,&auth,0)==0);
  assert(channel_calls==1 && prepare_calls==0 && station_calls==1 && mailbox_ends==1);
 }
 struct ieee80211_node node={.ni_bssid={2,3,4,5,6,7},.ni_associd=7};
 struct rtw88_net g={.lock=&lock,.core=&sc2,.key_epoch=2,.prep_granted=1,.requested=IEEE80211_S_RUN};
 struct rtw88_control_snapshot snap; struct rtw88_net_state_diag diag;
 node.ni_rates.rs_nrates=1; g.ic.ic_bss=&node; g.ic.ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;
 g.policy.epoch=9; g.pending_channel.primary=1; snapshot_mutation=&g;
 assert(rtw88_net_state_snapshot(&g,&snap,&diag,0)==ECANCELED);
 snapshot_mutation=NULL; g.policy.epoch=9;
 assert(rtw88_net_state_snapshot(&g,&snap,&diag,0)==0);
 assert(snap.id.epoch==9 && snap.id.key_epoch==2 && snap.aid==7 && snap.bssid[0]==2);
 /* A configure-only replacement leaves policy/key epochs unchanged. The
  * original queued request must never be rebound to the successor owner. */
 g.state_sae_serial=1; g.ic.ic_sae_serial=2;
 assert(rtw88_net_state_snapshot(&g,&snap,&diag,0)==ECANCELED);
 g.state_sae_serial=g.ic.ic_sae_serial=0;
 assert(rtw88_net_state_snapshot(&g,&snap,&diag,0)==0);
 struct ifnet ifp={0}; unsigned ch=6;
 struct rtw88_net h={.lock=&lock,.core=&sc2,.key_epoch=2};
 struct rtw88_async_epoch hid={.generation=8,.epoch=9,.key_epoch=2};
 ifp.if_softc=&h; h.ic.ic_ifp=&ifp; h.ic.ic_curchan=&ch;
 h.ic.ic_sae_serial=11; h.hw.generation=8; h.policy.epoch=9; h.policy.accepting=1; rtw88_async_init(&h.async,&hid);
 assert(rtw88_net_newstate_async(&h.ic,IEEE80211_S_AUTH,3)==0);
 assert(h.policy.epoch==10 && h.async.id.epoch==10 && !h.policy.accepting);
 assert(h.requested==IEEE80211_S_AUTH && h.pending_channel.primary==6 && h.work_pending);
 assert(h.state_sae_serial==11);
 h.policy.epoch=UINT64_MAX-1; h.async.id.epoch=UINT64_MAX-1;
 assert(rtw88_net_newstate_async(&h.ic,IEEE80211_S_AUTH,3)==EOVERFLOW);
 assert(h.async.closed && !h.policy.accepting);
 lock=1; assert(rtw88_net_state_current_locked(&g,&snap.id));
 g.key_epoch++; assert(!rtw88_net_state_current_locked(&g,&snap.id)); lock=0;
 rtw88_net_state_fail(&g,&snap.id,snap.sae_serial,EIO,&diag); assert(!g.stop_pending);
 g.key_epoch--; g.ic.ic_state=IEEE80211_S_INIT; g.policy.accepting=1;
 rtw88_net_state_fail(&g,&snap.id,snap.sae_serial,EIO,&diag);
 assert(g.stop_pending && g.init_failed && !g.policy.accepting);
 g.stop_pending=0;g.init_failed=0;g.policy.epoch=9;g.key_epoch=2;g.parent_state=parent_state;g.policy.accepting=0;
 g.ic.ic_sae_serial=1; parent_calls=0;
 assert(rtw88_net_state_complete(&g,&snap,&diag,0)==ECANCELED);
 assert(!parent_calls && !g.policy.accepting && g.ic.ic_sae_serial==1);
 g.ic.ic_sae_serial=0;g.reset_required=0;parent_retires=1;
 assert(rtw88_net_state_complete(&g,&snap,&diag,0)==ECANCELED);
 assert(!g.policy.accepting && g.reset_required && g.ic.ic_sae_serial==1);
 parent_retires=0;g.ic.ic_sae_serial=0;
 parent_mutation=&g; assert(rtw88_net_state_complete(&g,&snap,&diag,0)==ECANCELED);
 assert(!g.policy.accepting);parent_mutation=NULL;
 g.policy.epoch=9; assert(rtw88_net_state_complete(&g,&snap,&diag,0)==0);
 assert(g.policy.accepting && g.neg.run_published==1);
 /* A stale snapshot cannot start hardware or publish completion. */
 { struct rtw88_net stale={.lock=&lock,.core=&sc2,.key_epoch=2,.io_active=1};
   stale.policy.epoch=9;stale.hw.generation=sc2.sc_generation;
   stale.caps.bands=RTW88_BAND_2G;stale.ic.ic_sae_serial=2;
   snap.sae_serial=1;snap.id.generation=sc2.sc_generation;
   channel_calls=station_calls=0;
   assert(rtw88_net_control_execute(&stale,&snap,0)==ECANCELED);
   assert(!channel_calls && !station_calls);
   assert(rtw88_net_request_control(&stale,&snap)==ESTALE);
   stale.control_pending=snap;stale.control_pending_valid=1;
   lock=1;assert(rtw88_net_control_take_locked(&stale,&snap)==ESTALE);lock=0;
   assert(!stale.control_active);
   stale.control_active=1;stale.control_owner=snap;
   rtw88_net_control_done(&stale,&snap.id,0);
   assert(!stale.control_ready && !stale.control_active);
   stale.state_sae_serial=1;stale.work_pending=stale.run_pending=1;
   rtw88_net_state_async(&stale);
   assert(!stale.work_pending && !stale.run_pending && stale.ic.ic_sae_serial==2);
 }
 struct rtw88_net j={.lock=&lock,.core=&sc2,.key_epoch=2};
 j.ec.ec_if.if_flags=IFF_UP; j.policy.epoch=9; j.hw.generation=8;
 rtw88_net_state_async(&j);
 assert(j.prep_requested && !j.prep_granted && !j.state_running);
 struct rtw88_net k={.lock=&lock,.core=&sc2,.key_epoch=2};
 struct rtw88_async_epoch kid={.generation=8,.epoch=1,.key_epoch=2};
 k.hw.generation=8;k.policy.epoch=1;k.ec.ec_if.if_flags=IFF_UP|IFF_RUNNING;
 rtw88_async_init(&k.async,&kid);stop_calls=0;
 assert(rtw88_net_init_async(&k,6)==0);
 assert(k.init_requested && k.stats.init_pending && !(k.ec.ec_if.if_flags&IFF_RUNNING));
 assert(k.pending_channel.primary==6 && !stop_calls);
 rtw88_net_state_async(&k); assert(k.prep_requested && !k.prep_granted);
 lock=1; assert(rtw88_net_io_begin(&k)==0 && k.prep_granted); lock=0;
 rtw88_net_state_async(&k);
 assert(k.control_pending_valid && k.control_pending.kind==RTW88_CTL_INIT);
 assert(!k.prep_granted && !stop_calls);
 struct rtw88_net x={.lock=&lock,.core=&sc2,.key_epoch=2};
 x.ec.ec_if.if_softc=&x;x.ec.ec_if.if_flags=IFF_UP|IFF_RUNNING;
 x.policy.epoch=1;rtw88_async_init(&x.async,&kid);stop_calls=0;executor_reasons=0;
 rtw88_net_stop_async(&x.ec.ec_if,1);
 assert(x.stop_pending && x.stop_hold && !(x.ec.ec_if.if_flags&IFF_RUNNING));
 assert(stop_calls==0 && (executor_reasons&RTW88_EXEC_STOP));
 struct rtw88_net q={.lock=&lock,.core=&sc2,.key_epoch=2};
 q.ec.ec_if.if_softc=&q;q.ic.ic_ifp=&q.ec.ec_if;
 q.policy.epoch=1;q.policy.accepting=1;rtw88_async_init(&q.async,&kid);
 stop_calls=0;executor_reasons=0;rtw88_net_sta_quiet_async(&q.ic);
 assert(q.tx_quiet && q.stop_pending && !q.policy.accepting);
 assert(!stop_calls && (executor_reasons&RTW88_EXEC_STOP));
 q.csa_pending=1;q.hw.generation=8;
 assert(rtw88_net_init_async(&q,6)==EBUSY && q.tx_quiet);
 q.stop_pending=0;q.io_active=1;q.hw.started=q.hw.running=0;q.stop_error=0;
 assert(rtw88_net_init_async(&q,6)==EBUSY && q.tx_quiet);
 q.io_active=0;q.stop_error=EIO;
 assert(rtw88_net_init_async(&q,6)==EIO && q.tx_quiet);
 q.stop_error=0;
 assert(rtw88_net_init_async(&q,6)==0 && !q.tx_quiet && !q.csa_pending);
 struct rtw88_softc txsc={.sc_generation=5};
 struct rtw88_net t={.lock=&lock,.core=&txsc,.key_epoch=2};
 struct rtw88_async_epoch tid={.generation=5,.epoch=6,.key_epoch=2};
 struct rtw88_async_ticket ticket; struct rtw88_tx_job *job;
 t.policy.epoch=6;t.policy.accepting=1;rtw88_async_init(&t.async,&tid);
 assert(rtw88_async_reserve(&t.async,&tid,0,false,&ticket,&job)==0);
 assert(rtw88_async_commit(&t.async,&ticket,24,0)==0);
 tx_test_net=&t;executor_reasons=0;rtw88_net_tx_service(&t);
 assert(t.async_fault==EIO && t.stop_pending && !t.async.total);
 assert(executor_reasons&RTW88_EXEC_STOP);
 struct rtw88_net retry={.lock=&lock,.core=&txsc,.key_epoch=2};
 txsc.sc_stop_requested=0;retry.policy.epoch=6;retry.policy.accepting=1;
 rtw88_async_init(&retry.async,&tid);
 assert(rtw88_async_reserve(&retry.async,&tid,0,false,&ticket,&job)==0);
 assert(rtw88_async_commit(&retry.async,&ticket,24,0)==0);
 tx_test_net=&retry;executor_reasons=0;tx_error=EBUSY;tx_progress=RTW88_TX_NOT_STARTED;
 rtw88_net_tx_service(&retry);
 assert(!retry.async_fault && !retry.stop_pending && retry.async.total==1);
 assert(!(executor_reasons&RTW88_EXEC_STOP));
 txsc.sc_net=&retry;retry.stop_pending=1;retry.io_active=1;stop_calls=0;
 rtw88_async_service(&txsc,RTW88_EXEC_STOP|RTW88_EXEC_TX);
 assert(stop_calls==1 && retry.async.total==1);
 retry.stop_error=EIO;assert(rtw88_net_shutdown_check(&retry)==EIO);
 retry.stop_error=0;assert(rtw88_net_shutdown_check(&retry)==EBUSY);
 retry.async.total=0;retry.io_active=0;txsc.sc_running=0;
 assert(rtw88_net_shutdown_check(&retry)==0);
 txsc.sc_resources_ready=1;assert(rtw88_net_shutdown_check(&retry)==EBUSY);
 txsc.sc_resources_ready=0;
 struct rtw88_net detach={.lock=&lock,.core=&txsc};
 txsc.sc_net=&detach;txsc.sc_executor=(void *)&txsc;fake_owner=0;
 rtw88_async_init(&detach.async,&tid);detach.key_epoch=2;detach.policy.epoch=6;
 assert(rtw88_net_detach_async(&detach)==EIO);
 assert(detach.policy.dying && !detach.policy.accepting);
 assert(txsc.sc_net==&detach && !detach.stack_detached && !destroyed && !freed);
 drain_error=0;assert(rtw88_net_detach_async(&detach)==0);
 assert(txsc.sc_net==NULL && detach.stack_detached && destroyed==1 && freed==1);
 struct rtw88_net policy_net={.lock=&lock};
 struct ieee80211_node peer={.ni_associd=1}; unsigned policy_channel=1;
 uint8_t data[24]={0x08,1}; struct rtw88_tx_job policy_job={0};
 policy_net.ic.ic_bss=&peer; policy_net.ic.ic_curchan=&policy_channel;
 peer.ni_chan=&policy_channel;policy_net.ic.ic_state=IEEE80211_S_RUN;
 policy_net.ic.ic_opmode=IEEE80211_M_STA;policy_net.ic.ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;
 policy_net.policy.accepting=1;policy_net.hw.ra.configured=policy_net.hw.ra.active=1;
 lock=1;
 assert(rtw88_net_tx_policy_async(&policy_net,&peer,RTW88_TXQ_BE,data,sizeof(data),&policy_job)==0);
 assert(policy_job.mode==RTW88_TX_MODE_AUTO);
 peer.ni_ht20.valid=1;
 assert(rtw88_net_tx_policy_async(&policy_net,&peer,RTW88_TXQ_BE,data,sizeof(data),&policy_job)==EOPNOTSUPP);
 lock=0;
 struct rtw88_net reserve={.lock=&lock,.core=&txsc,.key_epoch=2};
 reserve.policy.accepting=1;reserve.policy.epoch=6;reserve.hw.generation=5;
 rtw88_async_init(&reserve.async,&tid);lock=1;
 for(unsigned i=0;i<RTW88_ASYNC_PER_QUEUE-2;i++)
     assert(rtw88_net_tx_reserve_locked(&reserve,&tid,RTW88_TXQ_BE,false,&ticket,&job)==0);
 assert(rtw88_net_tx_reserve_locked(&reserve,&tid,RTW88_TXQ_BE,false,&ticket,&job)==ENOBUFS);
 assert(rtw88_net_tx_reserve_locked(&reserve,&tid,RTW88_TXQ_BE,true,&ticket,&job)==0);
 lock=0;




 return 0;
}
