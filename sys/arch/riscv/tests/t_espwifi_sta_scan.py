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
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#define ETHER_ADDR_LEN 6
#define IEEE80211_STA_MAX_BODY 1024
#define IEEE80211_STA_VERSION 2
#define IEEE80211_STA_GET 0
#define IEEE80211_STA_ENABLE 1
#define IEEE80211_STA_DISABLE 2
#define IEEE80211_STA_SCAN 5
#define IEEE80211_STA_CANCEL_SCAN 6
#define IEEE80211_STA_CAP_SCAN 0x0040
#define IEEE80211_STA_ASSOCIATED 0x0002
#define IEEE80211_S_RUN 4
#define IEEE80211_F_SCAN 1
#define ESPWIFI_WAIT_FOREVER 0
#define IEEE80211_CHAN_ANYC ((struct ieee80211_channel *)(uintptr_t)-1)
#define isset(p,n) ((((const uint8_t *)(p))[(n)/8] & (1U<<((n)%8))) != 0)
#define KERNEL_LOCK(a,b) ((void)(a),(void)(b),kernel_depth++)
#define KERNEL_UNLOCK_ONE(a) ((void)(a),assert(kernel_depth==1),kernel_depth--)
struct ieee80211req_sta_mgmt {
 uint16_t version,op;uint32_t cookie,generation,flags;uint16_t freq,len;
 uint8_t bssid[6];uint16_t reserved;uint8_t data[1024]; };
struct ieee80211_channel { uint16_t ic_freq; };
struct ieee80211_node { struct ieee80211_channel *ni_chan;uint8_t ni_bssid[6]; };
struct ifnet { void *if_softc; };
struct ieee80211com { struct ifnet *ic_ifp;int ic_state,ic_flags;
 struct ieee80211_node *ic_bss;uint8_t ic_myaddr[6];uint8_t ic_chan_active[2];
 struct ieee80211_channel ic_channels[15]; };
struct espwifi_softc { struct ieee80211com sc_ic;struct ifnet sc_ifp;int sc_scan_lock,sc_key_lock;
 void *sc_scan_ctl_sem;unsigned sc_scan_count,sc_port_authorized;
 bool sc_key_pending,sc_attached,sc_scan_enabled,sc_sta_enabled,sc_sta_scan,
 sc_scan_pending,sc_scan_busy,sc_radio_ready,sc_sta_started,sc_join_stopped,
 sc_joined,sc_link,sc_join_pending,sc_join_disconnect_pending,
 sc_join_barrier_busy,sc_scan_shutdown;
 uint16_t sc_sta_scan_channels;struct ieee80211req_sta_mgmt sc_sta_scan_req;
 uint64_t sc_scan_generation,sc_scan_active,sc_join_active; };
static int kernel_depth,spin_depth,sem_gives,begin_calls,report_calls,release_calls;
static int abort_reports,success_reports,scan_start_result,scan_result,scan_mask_seen;
static bool report_aborted,inject_abort,report_once;static int admission_mutation;
static jmp_buf worker_done;static int sem_takes;
static int drain_delays;
static struct espwifi_softc sc;
static void mutex_spin_enter(int *p) { (void)p;assert(!spin_depth);spin_depth++; }
static void mutex_spin_exit(int *p) { (void)p;assert(spin_depth==1);spin_depth--; }
static unsigned atomic_load_acquire(unsigned *p) { return *p; }
static int splnet(void) { return 0; }static void splx(int s) { (void)s; }
static int ieee80211_sta_request_validate(const struct ieee80211req_sta_mgmt *r) {
 if (!r || r->version!=IEEE80211_STA_VERSION || r->len>sizeof(r->data) ||
     r->reserved!=0 || (r->op!=IEEE80211_STA_GET && r->flags!=0)) return EINVAL;
 return 0; }
static int ieee80211_sta_scan_begin(struct ieee80211com *ic,
 const struct ieee80211req_sta_mgmt *r,uint32_t generation) {
 (void)ic;begin_calls++;
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
''' + abort + adapter + worker + r'''
static void clear_events(void) { report_once=false;report_calls=abort_reports=
 success_reports=release_calls=0;report_aborted=false; }
static void setup(void) {
 memset(&sc,0,sizeof(sc));sc.sc_ifp.if_softc=&sc;
 sc.sc_ic.ic_ifp=&sc.sc_ifp;sc.sc_attached=sc.sc_scan_enabled=sc.sc_sta_enabled=true;
 sc.sc_radio_ready=sc.sc_sta_started=sc.sc_joined=sc.sc_link=true;
 sc.sc_join_active=9;sc.sc_port_authorized=1;sc.sc_ic.ic_state=IEEE80211_S_RUN;
 sc.sc_ic.ic_chan_active[0]=0xff;sc.sc_ic.ic_chan_active[1]=0x7f;
 for(unsigned i=1;i<=14;i++)sc.sc_ic.ic_channels[i].ic_freq=2407+5*i;
 struct ieee80211_channel *ch=&sc.sc_ic.ic_channels[6];
 static struct ieee80211_node node;node=(struct ieee80211_node){.ni_chan=ch,
     .ni_bssid={2,3,4,5,6,7}};sc.sc_ic.ic_bss=&node;
 begin_calls=sem_gives=0;scan_start_result=scan_result=0;scan_mask_seen=-1;
 admission_mutation=0;drain_delays=0;
 inject_abort=false;sem_takes=0;clear_events(); }
static struct ieee80211req_sta_mgmt request(unsigned op) {
 struct ieee80211req_sta_mgmt r={0};r.version=IEEE80211_STA_VERSION;
 r.cookie=0x53544131;r.op=(uint16_t)op;return r; }
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
 assert(r.flags==IEEE80211_STA_CAP_SCAN && r.len==6 && r.freq==0);
 for(unsigned i=0;i<sizeof(r.bssid);i++)assert(r.bssid[i]==0);
 assert(r.generation==9 && r.freq==0);
 sc.sc_joined=sc.sc_link=true;
 r=request(IEEE80211_STA_GET);sc.sc_ic.ic_state=IEEE80211_S_RUN;
 assert(espwifi_if_sta_mgmt(&sc.sc_ic,&r)==0 &&
   r.flags==(IEEE80211_STA_CAP_SCAN|IEEE80211_STA_ASSOCIATED) && r.freq==2437);
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
 return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'sta-scan.c';out=Path(td)/'sta-scan';c.write_text(code)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('actual ESP station scan admission, channel routing, cancel/drain and failure paths passed')
