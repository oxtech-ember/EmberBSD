#!/usr/bin/env python3
"""Exercise the real ESP connected-station scan adapter and worker boundary."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if not __debug__:
    sys.exit('this test requires Python assertions enabled')

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--analyze', action='store_true')
p.add_argument('--cppcheck')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / 'espressif/esp_wifi_if.c').read_text()
glue = (root / 'espressif/wifi/glue.c').read_text()

def function(text, name, result):
    m = re.search(r'\n(?:static )?' + result + r'\n' + name +
                  r'\(.*?\)\n\{.*?\n\}', text, re.S)
    assert m, name
    return m.group()

adapter = function(source, 'espwifi_if_sta_mgmt', 'int')
abort = function(source, 'espwifi_if_sta_scan_abort', 'void')
worker = function(source, 'espwifi_if_scan_control', 'void')
tx_report = function(source, 'espwifi_if_action_tx_report', 'void')
tx_done = function(source, 'espwifi_if_action_tx_done', 'void')
tx_request = function(source, 'espwifi_if_action_tx_request', 'int')
tx_cancel = function(source, 'espwifi_if_action_tx_cancel', 'int')
tx_invalidate = function(source, 'espwifi_if_action_tx_invalidate', 'void')
tx_control = function(source, 'espwifi_if_action_tx_control', 'void')
sta_header = (root.parent.parent / 'net80211/ieee80211_sta.h').read_text()
action_header = (root.parent.parent / 'net80211/ieee80211_sta_action.h').read_text()

def inline_function(text, name):
    marker = name + '('
    end = text.index(marker) + len(marker)
    start = text.rfind('static inline', 0, end)
    brace = text.index('{', end)
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == '{':
            depth += 1
        elif text[pos] == '}':
            depth -= 1
            if depth == 0:
                return text[start:pos + 1]
    raise AssertionError(name)

sta_validation = '\n'.join(inline_function(sta_header, name) for name in
    ('ieee80211_sta_ies_valid', 'ieee80211_sta_action_ies_valid',
     'ieee80211_sta_action_valid'))

def c_block(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == '{':
            depth += 1
        elif text[pos] == '}':
            depth -= 1
            if depth == 0:
                return text[start:pos + 1]
    raise AssertionError(marker)

action_state = c_block(action_header, 'struct ieee80211_sta_action_state {') + ';'
action_helpers = '\n'.join(c_block(action_header, 'static inline ' + ret +
    '\nieee80211_sta_action_' + name) for name, ret in (
    ('begin', 'int'), ('match', 'bool'), ('cancel', 'bool'),
    ('invalidate', 'void'), ('report', 'bool'), ('release', 'bool')))
tx_snapshot = function(source, 'espwifi_if_action_tx_snapshot', 'void')
tx_clear = function(source, 'espwifi_if_action_tx_clear', 'void')
vendor = function(glue, 'espwifi_scan', 'int')
newstate = function(source, 'espwifi_if_newstate', 'int')
auth_commit = newstate.index('ic->ic_state = IEEE80211_S_AUTH;')
auth_abort = newstate.index('espwifi_if_sta_scan_abort(sc);', auth_commit)
auth_wake = newstate.index('espwifi_os_sem_give(sc->sc_scan_ctl_sem);', auth_abort)
assert auth_commit < auth_abort < auth_wake

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#define ETHER_ADDR_LEN 6
#define IEEE80211_ADDR_LEN 6
#define CHECK(e) ((e) ? (void)0 : abort())
#define IEEE80211_STA_MAX_BODY 1024
#define IEEE80211_STA_VERSION 2
#define IEEE80211_STA_GET 0
#define IEEE80211_STA_ENABLE 1
#define IEEE80211_STA_DISABLE 2
#define IEEE80211_STA_SCAN 5
#define IEEE80211_STA_CANCEL_SCAN 6
#define IEEE80211_STA_TX 3
#define IEEE80211_STA_CANCEL_TX 4
#define IEEE80211_STA_TX_ACK 0x0004
#define IEEE80211_STA_CAP_SCAN 0x0040
#define IEEE80211_STA_CAP_BTM 0x0001
#define IEEE80211_STA_CAP_RRM 0x0008
#define IEEE80211_STA_ASSOCIATED 0x0002
#define IEEE80211_STA_ACTION_BTM 0x01U
#define IEEE80211_STA_ACTION_NR 0x02U
#define IEEE80211_STA_ACTION_RM_REPORT 0x04U
#define IEEE80211_STA_ACTION_RM_REQUEST 0x08U
#define IEEE80211_S_RUN 4
#define IEEE80211_M_STA 1
#define IEEE80211_F_SCAN 1
#define ESPWIFI_WAIT_FOREVER 0
#define hz 100
#define RTM_IEEE80211_STA_TX 113
#define IEEE80211_CHAN_ANYC ((struct ieee80211_channel *)(uintptr_t)-1)
#define isset(p,n) ((((const uint8_t *)(p))[(n)/8] & (1U<<((n)%8))) != 0)
#define KERNEL_LOCK(a,b) ((void)(a),(void)(b),kernel_depth++)
#define KERNEL_UNLOCK_ONE(a) ((void)(a),assert(kernel_depth==1),kernel_depth--)
struct ieee80211req_sta_mgmt {
 uint16_t version,op;uint32_t cookie,generation,flags;uint16_t freq,len;
 uint8_t bssid[6];uint16_t reserved;uint8_t data[1024]; };
''' + action_state + r'''
struct ieee80211_sta_scan_state { bool active; };
struct ieee80211_channel { uint16_t ic_freq; };
struct ieee80211_node { struct ieee80211_channel *ni_chan;uint8_t ni_bssid[6]; };
struct ifnet { void *if_softc; };
struct ieee80211com { struct ifnet *ic_ifp;int ic_state,ic_flags;
 struct ieee80211_node *ic_bss;uint8_t ic_myaddr[6];uint8_t ic_chan_active[2];
 struct ieee80211_sta_scan_state ic_sta_scan;struct ieee80211_sta_action_state ic_sta_action;
 int ic_opmode;
 struct ieee80211_channel ic_channels[15]; };
struct ethercom { struct ifnet ec_if; };
struct espwifi_softc { struct ieee80211com sc_ic;struct ifnet sc_ifp;
 struct ethercom sc_ec;int sc_scan_lock,sc_key_lock;
 void *sc_scan_ctl_sem;unsigned sc_scan_count,sc_port_authorized;
 bool sc_key_pending,sc_attached,sc_scan_enabled,sc_sta_enabled,sc_sta_scan,
 sc_scan_pending,sc_scan_busy,sc_radio_ready,sc_sta_started,sc_join_stopped,
 sc_joined,sc_link,sc_join_pending,sc_join_disconnect_pending,
 sc_join_barrier_busy,sc_scan_shutdown;
 uint16_t sc_sta_scan_channels;struct ieee80211req_sta_mgmt sc_sta_scan_req;
 uint64_t sc_scan_generation,sc_scan_active,sc_join_active;
 bool sc_action_tx_owned,sc_action_tx_pending,sc_action_tx_started,
 sc_action_tx_send_done,sc_action_tx_send_ok,sc_action_tx_cb_seen;
 uint16_t sc_action_tx_len;uint64_t sc_action_tx_serial;
 uint8_t sc_action_tx_data[IEEE80211_STA_MAX_BODY];
 uint8_t sc_join_bssid[6]; };
static int kernel_depth,spin_depth,sem_gives,begin_calls,report_calls,release_calls;
static int abort_reports,success_reports,scan_start_result,scan_result,scan_mask_seen;
static bool report_aborted,inject_abort,report_once;static int admission_mutation;
static jmp_buf worker_done;static int sem_takes;
static int drain_delays;
static int action_enabled,action_ext_error,action_ext_calls,action_callbacks;
static int action_tx_events;
static int action_auto_callback;
static struct ieee80211req_sta_mgmt action_event;
static struct espwifi_softc sc;
static struct espwifi_softc *espwifi_global;
#define espwifi_sc (*espwifi_global)
static void espwifi_if_action_tx_done(uint32_t,uint32_t,const uint8_t *);
static void mutex_spin_enter(int *p) { (void)p;assert(!spin_depth);spin_depth++; }
static void mutex_spin_exit(int *p) { (void)p;assert(spin_depth==1);spin_depth--; }
static void mutex_enter(int *p) { (void)p; }
static void mutex_exit(int *p) { (void)p; }
static void espwifi_if_action_tx_control(struct espwifi_softc *);
static unsigned atomic_load_acquire(unsigned *p) { return *p; }
static int splnet(void) { return 0; }static void splx(int s) { (void)s; }
static int ieee80211_sta_request_validate(const struct ieee80211req_sta_mgmt *r) {
 if (!r || r->version!=IEEE80211_STA_VERSION || r->len>sizeof(r->data) ||
     r->reserved!=0 || (r->op!=IEEE80211_STA_GET && r->flags!=0)) return EINVAL;
 return 0; }
static int ieee80211_sta_scan_begin(struct ieee80211com *ic,
 const struct ieee80211req_sta_mgmt *r,uint32_t generation) {
 (void)ic;begin_calls++;
 if (ic->ic_sta_action.active) return EBUSY;
 if (r->op!=IEEE80211_STA_SCAN || r->generation!=generation) return ESTALE;
 if (r->len && (r->len%2 || r->len>sizeof(r->data))) return EINVAL;
 switch(admission_mutation) {
 case 1:sc.sc_radio_ready=false;break;case 2:sc.sc_link=false;break;
 case 3:sc.sc_port_authorized=0;break;case 4:sc.sc_sta_started=false;break;
 case 5:sc.sc_join_stopped=true;break;case 6:sc.sc_joined=false;break;
 case 7:sc.sc_join_active++;break;case 8:sc.sc_join_barrier_busy=true;break;
 default:break; }
 return 0; }
static bool ieee80211_sta_scan_match(struct ieee80211com *ic,
 const struct ieee80211req_sta_mgmt *r) {
 struct espwifi_softc *s=ic->ic_ifp->if_softc;
 return r->op==IEEE80211_STA_CANCEL_SCAN && s->sc_sta_scan &&
     r->generation==s->sc_sta_scan_req.generation &&
     r->cookie==s->sc_sta_scan_req.cookie; }
static bool ieee80211_sta_scan_report(struct ieee80211com *ic,
 const struct ieee80211req_sta_mgmt *r,bool aborted) {
 (void)ic;(void)r;report_calls++;
 if (report_once) return false;report_once=true;report_aborted=aborted;
 if(aborted)abort_reports++;else success_reports++;return true; }
static bool ieee80211_sta_scan_release(struct ieee80211com *ic,
 const struct ieee80211req_sta_mgmt *r) { (void)ic;(void)r;release_calls++;return true; }
static void espwifi_os_sem_give(void *p) { (void)p;assert(!spin_depth);sem_gives++; }
static int espwifi_ext_action_tx_enabled(void) { return action_enabled; }
static int espwifi_ext_action_tx(const unsigned char *bssid,const void *body,
 unsigned int len,uint32_t cookie,uint32_t generation) {
 action_ext_calls++;assert(len>=3 && cookie!=0 && generation==9);
 assert(memcmp(bssid,sc.sc_join_bssid,6)==0);
 if(action_auto_callback){action_callbacks++;
   espwifi_if_action_tx_done(cookie,generation,bssid);
   if(action_auto_callback>1){action_callbacks++;
     espwifi_if_action_tx_done(cookie,generation,bssid);}}
 (void)body;return action_ext_error; }
static void rt_ieee80211msg(struct ifnet *ifp,int type,const void *data,size_t len) {
 (void)ifp;assert(type==RTM_IEEE80211_STA_TX);
 assert(len==offsetof(struct ieee80211req_sta_mgmt,data));
 memcpy(&action_event,data,len);action_tx_events++; }
static void explicit_memset(void *p,int c,size_t n) { memset(p,c,n); }
static void espwifi_os_sem_take(void *p,int n) { (void)p;(void)n;
 if (++sem_takes==2) longjmp(worker_done,1); }
static void espwifi_os_task_delay(unsigned n) { (void)n;assert(release_calls==0);
 drain_delays++;sc.sc_scan_count=0;sc.sc_scan_busy=false; }
static void espwifi_if_join_control(struct espwifi_softc *s) { (void)s;assert(0); }
static void espwifi_if_key_control(struct espwifi_softc *s) { (void)s;assert(0); }
static int espwifi_scan_capture(int on) { return on?scan_start_result:0; }
static void espwifi_if_sta_scan_abort(struct espwifi_softc *);
static int espwifi_scan(unsigned channels) {
 scan_mask_seen=(int)channels;
 if (inject_abort) { inject_abort=false;espwifi_if_sta_scan_abort(&sc);
   assert(release_calls==0); }
 return scan_result; }
static void espwifi_os_log(const char *fmt,...) { (void)fmt; }
static int printf(const char *fmt,...) { (void)fmt;return 0; }
static void ieee80211_cancel_scan(struct ieee80211com *ic) { ic->ic_flags&=~IEEE80211_F_SCAN; }
static void ieee80211_end_scan(struct ieee80211com *ic) { ic->ic_flags&=~IEEE80211_F_SCAN; }
''' + sta_validation + action_helpers + abort + tx_snapshot + tx_clear + tx_report + tx_done + tx_request + tx_cancel + tx_invalidate + tx_control + adapter + worker + r'''
static void clear_events(void) { report_once=false;report_calls=abort_reports=
 success_reports=release_calls=0;report_aborted=false; }
static void setup(void) {
 memset(&sc,0,sizeof(sc));espwifi_global=&sc;sc.sc_ifp.if_softc=&sc;
 sc.sc_ic.ic_ifp=&sc.sc_ifp;sc.sc_attached=sc.sc_scan_enabled=sc.sc_sta_enabled=true;
 sc.sc_radio_ready=sc.sc_sta_started=sc.sc_joined=sc.sc_link=true;
 sc.sc_ic.ic_opmode=IEEE80211_M_STA;
 sc.sc_join_active=9;sc.sc_port_authorized=1;sc.sc_ic.ic_state=IEEE80211_S_RUN;
 sc.sc_ic.ic_chan_active[0]=0xff;sc.sc_ic.ic_chan_active[1]=0x7f;
 for(unsigned i=1;i<=14;i++)sc.sc_ic.ic_channels[i].ic_freq=2407+5*i;
 struct ieee80211_channel *ch=&sc.sc_ic.ic_channels[6];
 static struct ieee80211_node node;node=(struct ieee80211_node){.ni_chan=ch,
     .ni_bssid={2,3,4,5,6,7}};sc.sc_ic.ic_bss=&node;
 memcpy(sc.sc_join_bssid,node.ni_bssid,6);
 begin_calls=sem_gives=0;scan_start_result=scan_result=0;scan_mask_seen=-1;
 admission_mutation=0;drain_delays=0;
 action_enabled=1;action_ext_error=action_ext_calls=action_callbacks=0;
 action_tx_events=0;
 action_auto_callback=0;memset(&action_event,0,sizeof(action_event));
 inject_abort=false;sem_takes=0;clear_events(); }
static struct ieee80211req_sta_mgmt request(unsigned op) {
 struct ieee80211req_sta_mgmt r={0};r.version=IEEE80211_STA_VERSION;
 r.cookie=0x53544131;r.op=(uint16_t)op;return r; }
static struct ieee80211req_sta_mgmt action_request(void) {
 struct ieee80211req_sta_mgmt r=request(IEEE80211_STA_TX);
 r.cookie=17;r.generation=9;r.freq=2437;r.len=4;
 memcpy(r.bssid,sc.sc_join_bssid,6);
 r.data[0]=10;r.data[1]=6;r.data[2]=0;r.data[3]=1;
 return r; }
static void run_worker(void) { sem_takes=0;if(setjmp(worker_done)==0)
 espwifi_if_scan_control(&sc); }
int main(void) {
 setup();
 /* GET reports only scan capability while disconnected, and scan+association
  * details only for the live RUN BSS. */
 sc.sc_joined=sc.sc_link=false;
 struct ieee80211req_sta_mgmt r=request(IEEE80211_STA_GET);r.freq=2412;
 memset(r.bssid,0xa5,sizeof(r.bssid));
 int geterr=espwifi_if_sta_mgmt(&sc.sc_ic,&r);assert(geterr==0);
 assert(r.flags==(IEEE80211_STA_CAP_SCAN|IEEE80211_STA_CAP_BTM|IEEE80211_STA_CAP_RRM) && r.len==6 && r.freq==0);
 for(unsigned i=0;i<sizeof(r.bssid);i++)assert(r.bssid[i]==0);
 assert(r.generation==9 && r.freq==0);
 sc.sc_joined=sc.sc_link=true;
 r=request(IEEE80211_STA_GET);sc.sc_ic.ic_state=IEEE80211_S_RUN;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0 &&
   r.flags==(IEEE80211_STA_CAP_SCAN|IEEE80211_STA_CAP_BTM|IEEE80211_STA_CAP_RRM|IEEE80211_STA_ASSOCIATED) && r.freq==2437);
 sc.sc_ic.ic_state=IEEE80211_S_RUN;
 /* Unsupported/malformed/stale and busy requests do not enter the worker. */
 r=request(99);assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==EOPNOTSUPP);
 r=request(IEEE80211_STA_SCAN);r.reserved=1;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==EINVAL);
 r=request(IEEE80211_STA_SCAN);r.generation=8;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==ESTALE);
 r=request(IEEE80211_STA_SCAN);sc.sc_scan_active=22;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==EBUSY);sc.sc_scan_active=0;
 sc.sc_port_authorized=0;r=request(IEEE80211_STA_SCAN);
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==ENOTCONN);sc.sc_port_authorized=1;
 /* One requested 2437 MHz channel maps to bit 6 and survives worker dispatch. */
 r=request(IEEE80211_STA_SCAN);r.generation=9;r.len=2;uint16_t f=2437;
 memcpy(r.data,&f,2);int scanerr=espwifi_if_sta_mgmt(&sc.sc_ic,&r);assert(scanerr==0);
 assert(begin_calls==2);assert(sem_gives==1);assert(sc.sc_sta_scan_channels==(1U<<6));
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==EBUSY);
 run_worker();assert(scan_mask_seen==(1U<<6) && success_reports==1 &&
   !report_aborted && release_calls==1 && !sc.sc_sta_scan);
 /* Zero-length request is all active 2.4 GHz channels; unsupported empty mask
  * releases the common request without admitting work. */
 setup();r=request(IEEE80211_STA_SCAN);r.generation=9;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0 &&
   sc.sc_sta_scan_channels==0x7ffe);
 setup();sc.sc_ic.ic_chan_active[0]=sc.sc_ic.ic_chan_active[1]=0;
 r=request(IEEE80211_STA_SCAN);r.generation=9;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==EOPNOTSUPP && release_calls==1 && !sem_gives);
 /* Every submit-to-commit race is rolled back without a queued completion. */
 for(int mutation=1;mutation<=8;mutation++) {
   setup();admission_mutation=mutation;r=request(IEEE80211_STA_SCAN);r.generation=9;
   assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==ESTALE && release_calls==1 &&
       !sem_gives && !sc.sc_sta_scan && !sc.sc_scan_pending);
 }
 /* DISABLE aborts a queued scan exactly once and releases immediately. */
 setup();r=request(IEEE80211_STA_SCAN);r.generation=9;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);struct ieee80211req_sta_mgmt c=request(IEEE80211_STA_CANCEL_SCAN);
 c.generation=8;assert(espwifi_if_sta_mgmt(&sc.sc_ic,&c)==ESTALE);
 c.generation=9;assert(espwifi_if_sta_mgmt(&sc.sc_ic,&c)==0);
 assert(abort_reports==1 && report_aborted && release_calls==1 && !sc.sc_sta_scan);
 espwifi_if_sta_scan_abort(&sc);assert(abort_reports==1 && release_calls==1);
 /* A live vendor scan retains ownership until it drains after cancellation. */
 setup();r=request(IEEE80211_STA_SCAN);r.generation=9;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);inject_abort=true;run_worker();
 assert(scan_mask_seen==0x7ffe && abort_reports==1 && release_calls==1 &&
   !sc.sc_sta_scan && !sc.sc_scan_active);
 /* Radio capture start failure after admission completes aborted and releases. */
 setup();r=request(IEEE80211_STA_SCAN);r.generation=9;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);scan_start_result=EIO;run_worker();
 assert(abort_reports==1 && report_aborted && release_calls==1 && !sc.sc_sta_scan);
 /* Vendor scan failure after capture setup also reports an aborted event. */
 setup();r=request(IEEE80211_STA_SCAN);r.generation=9;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);scan_result=-1;run_worker();
 assert(scan_mask_seen==0x7ffe && abort_reports==1 && report_aborted &&
   release_calls==1 && !sc.sc_sta_scan);
 /* Queued frame drain holds the common request until the queue is empty. */
 setup();r=request(IEEE80211_STA_SCAN);r.generation=9;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);sc.sc_scan_count=1;sc.sc_scan_busy=true;
 run_worker();assert(drain_delays==1 && success_reports==1 && release_calls==1);
 /* Fresh AUTH's real retirement hook is exercised by this same abort path. */
 setup();r=request(IEEE80211_STA_SCAN);r.generation=9;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);uint64_t oldgen=sc.sc_scan_generation;
 espwifi_if_sta_scan_abort(&sc);assert(sc.sc_scan_generation!=oldgen &&
   !sc.sc_scan_pending && abort_reports==1 && release_calls==1);
	/* Diagnostic STA_TX rejects unsupported or stale requests before reserving
	 * its single asynchronous slot. */
	setup();action_enabled=0;r=action_request();
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==EOPNOTSUPP &&
	    !sc.sc_action_tx_owned && action_ext_calls==0);
	setup();r=action_request();r.len=2;
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==EINVAL && !sc.sc_action_tx_owned);
	setup();r=action_request();r.data[0]=99;
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==EINVAL && !sc.sc_action_tx_owned);
	setup();r=action_request();r.generation++;
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==ESTALE && !sc.sc_action_tx_owned);
	setup();r=action_request();sc.sc_port_authorized=0;
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==ENOTCONN && !sc.sc_action_tx_owned);
	setup();r=action_request();sc.sc_sta_enabled=false;
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==ENOTCONN && !sc.sc_action_tx_owned);
	setup();r=action_request();sc.sc_scan_pending=true;
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==EBUSY && !sc.sc_action_tx_owned);

	/* A request returns after queueing; only the existing worker enters the
	 * vendor API. The completion callback, not submission success, reports TX. */
	setup();r=action_request();
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0 && action_ext_calls==0 &&
	    sc.sc_action_tx_pending && sc.sc_action_tx_owned);
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==EBUSY);
	espwifi_if_action_tx_control(&sc);
	assert(action_ext_calls==1 && action_tx_events==0 &&
	    sc.sc_action_tx_owned && !sc.sc_action_tx_pending);
	espwifi_if_action_tx_done(18,9,sc.sc_join_bssid);
	espwifi_if_action_tx_report(&sc);assert(action_tx_events==0);
	espwifi_if_action_tx_done(17,10,sc.sc_join_bssid);
	espwifi_if_action_tx_report(&sc);assert(action_tx_events==0);
	uint8_t wrong_bssid[6];memcpy(wrong_bssid,sc.sc_join_bssid,6);wrong_bssid[5]^=1;
	espwifi_if_action_tx_done(17,9,wrong_bssid);
	espwifi_if_action_tx_report(&sc);assert(action_tx_events==0);
	espwifi_if_action_tx_done(17,9,sc.sc_join_bssid);
	espwifi_if_action_tx_report(&sc);assert(action_tx_events==1 && !sc.sc_action_tx_owned);
	assert(action_event.cookie==17 && action_event.generation==9 &&
	    action_event.flags==0 && action_event.len==0 &&
	    memcmp(action_event.bssid,sc.sc_join_bssid,6)==0);
	/* Duplicate and stale callbacks cannot generate a second route event. */
	espwifi_if_action_tx_done(17,9,sc.sc_join_bssid);
	espwifi_if_action_tx_report(&sc);assert(action_tx_events==1);

	/* Immediate callback is legal before vendor enqueue returns; it is still
	 * reported once after the worker records successful submission. */
	setup();action_auto_callback=1;r=action_request();
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0 && action_tx_events==0);
	espwifi_if_action_tx_control(&sc);
	assert(action_callbacks==1 && action_tx_events==1 && !sc.sc_action_tx_owned);
	/* If submission fails, no callback ownership exists and one negative
	 * completion is reported with ACK clear. */
	setup();action_ext_error=EIO;r=action_request();
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0 && action_tx_events==0);
	espwifi_if_action_tx_control(&sc);
	assert(action_ext_calls==1 && action_tx_events==1 && action_event.flags==0 &&
	    !sc.sc_action_tx_owned);

	/* Cancellation before worker claim removes queued work. After claim, the
	 * slot remains quarantined until the matching callback drains ownership. */
	setup();r=action_request();assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);
	struct ieee80211req_sta_mgmt cancel=request(IEEE80211_STA_CANCEL_TX);
	cancel.cookie=r.cookie;cancel.generation=r.generation;
	memcpy(cancel.bssid,r.bssid,sizeof(cancel.bssid));
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&cancel)==0 && !sc.sc_action_tx_owned);
	espwifi_if_action_tx_control(&sc);assert(action_ext_calls==0 && action_tx_events==0);
	setup();r=action_request();assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);
	espwifi_if_action_tx_control(&sc);assert(action_ext_calls==1);
	cancel=request(IEEE80211_STA_CANCEL_TX);cancel.cookie=r.cookie;
	cancel.generation=r.generation;memcpy(cancel.bssid,r.bssid,sizeof(cancel.bssid));
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&cancel)==0 && sc.sc_action_tx_owned);
	espwifi_if_action_tx_done(17,9,sc.sc_join_bssid);
	espwifi_if_action_tx_report(&sc);assert(action_tx_events==0 && !sc.sc_action_tx_owned);

	/* Invalidation follows the same queued-vs-in-flight ownership rule. */
	setup();r=action_request();assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);
	espwifi_if_action_tx_invalidate(&sc);assert(!sc.sc_action_tx_owned);
	espwifi_if_action_tx_control(&sc);assert(action_ext_calls==0);
	setup();r=action_request();assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);
	espwifi_if_action_tx_control(&sc);espwifi_if_action_tx_invalidate(&sc);
	assert(sc.sc_action_tx_owned && action_ext_calls==1);
	espwifi_if_action_tx_done(17,9,sc.sc_join_bssid);
	espwifi_if_action_tx_report(&sc);assert(action_tx_events==0 && !sc.sc_action_tx_owned);

	/* A synchronous callback followed by a failed submit is not a success. */
	setup();action_auto_callback=1;action_ext_error=EIO;r=action_request();
	assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0);
	espwifi_if_action_tx_control(&sc);
	assert(action_callbacks==1 && action_tx_events==1 && action_event.flags==0);

 return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'sta-scan.c';out=Path(td)/'sta-scan';c.write_text(code.replace('assert(', 'CHECK('))
    if a.cppcheck:
        subprocess.run([a.cppcheck, '--enable=warning,performance,portability',
            '--suppress=missingIncludeSystem',
            '--error-exitcode=1',
            '--std=c11', str(c)], check=True)
    if a.analyze:
        subprocess.run([a.cc, '--analyze', '-std=c11', '-Wall', '-Wextra',
            '-Werror', str(c), '-o', str(Path(td) / 'analysis.plist')], check=True)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all',
        str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('actual ESP station scan admission, channel routing, cancel/drain and failure paths passed')
