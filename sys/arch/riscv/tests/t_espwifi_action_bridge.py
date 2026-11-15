#!/usr/bin/env python3
"""Exercise ESP Action RX queueing and asynchronous TX ownership."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--analyze', action='store_true')
p.add_argument('--cppcheck')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / 'espressif/esp_wifi_if.c').read_text()
sta_header = (root.parent.parent / 'net80211/ieee80211_sta.h').read_text()

def function(text, name, result):
    m = re.search(r'\n(?:static )?' + result + r'\n' + name +
                  r'\(.*?\)\n\{.*?\n\}', text, re.S)
    assert m, name
    return m.group()

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

tx_helpers = '\n'.join(function(source, name, result) for name, result in (
    ('espwifi_if_action_tx_snapshot', 'void'),
    ('espwifi_if_action_tx_clear', 'void')))
tx = '\n'.join(function(source, name, result) for name, result in (
    ('espwifi_if_action_tx_report', 'void'),
    ('espwifi_if_action_tx_done', 'void'),
    ('espwifi_if_action_tx_request', 'int'),
    ('espwifi_if_action_tx_cancel', 'int'),
    ('espwifi_if_action_tx_invalidate', 'void'),
    ('espwifi_if_action_tx_control', 'void')))
rx = '\n'.join(function(source, name, result) for name, result in (
    ('espwifi_if_action_rx', 'int'),
    ('espwifi_if_action_rx_process', 'void')))
scanthread = function(source, 'espwifi_if_scanthread', 'void')
assert re.search(r'if \(frame\.action\)\s*\{\s*'
                  r'espwifi_if_action_rx_process\(sc, &frame\);', scanthread)
action_header = (root.parent.parent / 'net80211/ieee80211_sta_action.h').read_text()
def c_block(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == '{': depth += 1
        elif text[pos] == '}':
            depth -= 1
            if depth == 0: return text[start:pos + 1]
    raise AssertionError(marker)
action_state = c_block(action_header, 'struct ieee80211_sta_action_state {') + ';'
action_helpers = '\n'.join(c_block(action_header, 'static inline ' + ret +
    '\nieee80211_sta_action_' + name) for name, ret in (
    ('begin', 'int'), ('match', 'bool'), ('cancel', 'bool'),
    ('invalidate', 'void'), ('report', 'bool'), ('release', 'bool')))
syntax = '\n'.join(inline_function(sta_header, name) for name in (
    'ieee80211_sta_ies_valid', 'ieee80211_sta_action_ies_valid',
    'ieee80211_sta_action_valid'))

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define ETHER_ADDR_LEN 6
#define IEEE80211_ADDR_LEN 6
#define CHECK(e) ((e) ? (void)0 : abort())
#define ESPWIFI_SCAN_RING 4
#define ESPWIFI_SCAN_FRAME_MAX 2304
#define IEEE80211_STA_MAX_BODY 1024
#define IEEE80211_STA_VERSION 2
#define IEEE80211_STA_GET 0
#define IEEE80211_STA_TX 3
#define IEEE80211_STA_CANCEL_TX 4
#define IEEE80211_STA_TX_ACK 0x0004
#define IEEE80211_STA_CAP_BTM 0x0001
#define IEEE80211_STA_CAP_RRM 0x0008
#define IEEE80211_STA_ASSOCIATED 0x0002
#define IEEE80211_STA_ACTION_BTM 0x01U
#define IEEE80211_STA_ACTION_NR 0x02U
#define IEEE80211_STA_ACTION_RM_REPORT 0x04U
#define IEEE80211_STA_ACTION_RM_REQUEST 0x08U
#define IEEE80211_S_RUN 4
#define IEEE80211_M_STA 1
#define IEEE80211_CHAN_ANYC ((struct ieee80211_channel *)(uintptr_t)-1)
#define RTM_IEEE80211_STA_RX 112
#define RTM_IEEE80211_STA_TX 113
#define hz 100
#define KERNEL_LOCK(a,b) ((void)(a),(void)(b),kernel_depth++,test_kernel_lock())
#define KERNEL_UNLOCK_ONE(a) ((void)(a),assert(kernel_depth==1),kernel_depth--)
struct ieee80211req_sta_mgmt {
 uint16_t version,op;uint32_t cookie,generation,flags;uint16_t freq,len;
 uint8_t bssid[6];uint16_t reserved;uint8_t data[1024]; };
''' + action_state + r'''
struct ieee80211_sta_scan_state { bool active; };
struct ieee80211_channel { uint16_t ic_freq; unsigned number; };
struct ieee80211_node { struct ieee80211_channel *ni_chan;uint8_t ni_bssid[6]; };
struct ifnet { void *if_softc; };
struct ieee80211com { struct ifnet *ic_ifp;int ic_state,ic_opmode;
 struct ieee80211_node *ic_bss;struct ieee80211_sta_scan_state ic_sta_scan;
 struct ieee80211_sta_action_state ic_sta_action; };
struct ethercom { struct ifnet ec_if; };
struct espwifi_scan_frame { bool action;unsigned len,channel;uint32_t timestamp;
 uint64_t generation;int rssi;uint8_t sender[6];uint8_t data[2304]; };
struct espwifi_softc {
 struct ethercom sc_ec;struct ieee80211com sc_ic;struct ifnet sc_ifp;
 int sc_scan_lock,sc_action_tx_lock;void *sc_scan_sem,*sc_scan_ctl_sem;
 bool sc_attached,sc_scan_enabled,sc_scan_shutdown,sc_sta_enabled,sc_sta_scan;
 bool sc_scan_pending,sc_join_pending,sc_join_disconnect_pending,sc_join_barrier_busy;
 uint64_t sc_scan_active;
 bool sc_radio_ready,sc_sta_started,sc_join_stopped,sc_joined,sc_link;
 unsigned sc_port_authorized,sc_scan_head,sc_scan_count;uint64_t sc_join_active;
 uint8_t sc_join_bssid[6];struct espwifi_scan_frame sc_scan_ring[4];
 bool sc_action_tx_owned,sc_action_tx_pending,sc_action_tx_started;
 bool sc_action_tx_send_done,sc_action_tx_send_ok,sc_action_tx_cb_seen;
 uint16_t sc_action_tx_len;uint64_t sc_action_tx_serial;
 uint8_t sc_action_tx_data[1024];
 };
static struct espwifi_softc sc,*espwifi_global=&sc;
#define espwifi_sc (*espwifi_global)
static int kernel_depth,spin_depth,rx_events,tx_events,sem_rx,sem_ctl;
static bool report_race;static struct ieee80211req_sta_mgmt report_race_req;
static void test_kernel_lock(void);
static int vendor_calls,vendor_error,vendor_sync_callback;
static struct ieee80211req_sta_mgmt rx_event,tx_event;
static void espwifi_if_action_tx_report(struct espwifi_softc *);
static void espwifi_if_action_tx_done(uint32_t,uint32_t,const uint8_t *);
static void espwifi_if_action_tx_control(struct espwifi_softc *);
static int espwifi_if_action_tx_cancel(struct espwifi_softc *,
    const struct ieee80211req_sta_mgmt *);
static void espwifi_if_action_tx_invalidate(struct espwifi_softc *);
static void espwifi_if_action_rx_process(struct espwifi_softc *,
    const struct espwifi_scan_frame *);
static void mutex_spin_enter(int *p) { (void)p;assert(!spin_depth);spin_depth++; }
static void mutex_spin_exit(int *p) { (void)p;assert(spin_depth==1);spin_depth--; }
static void mutex_enter(int *p) { (void)p; }
static void mutex_exit(int *p) { (void)p; }
static unsigned atomic_load_acquire(unsigned *p) { return *p; }
static void atomic_store_release(unsigned *p,unsigned v) { *p=v; }
static int splnet(void) { return 0; }static void splx(int s) { (void)s; }
static void espwifi_os_sem_give(void *p) { assert(!spin_depth);
 if(p==sc.sc_scan_sem)sem_rx++;else sem_ctl++; }
static unsigned ieee80211_chan2ieee(struct ieee80211com *ic,
 struct ieee80211_channel *c) { (void)ic;return c->number; }
static int espwifi_ext_action_tx(const unsigned char *bssid,const void *body,
 unsigned int len,uint32_t cookie,uint32_t generation) {
 vendor_calls++;assert(len>=3 && cookie==77 && generation==9);
 assert(memcmp(bssid,sc.sc_join_bssid,6)==0);(void)body;
 if(vendor_sync_callback)espwifi_if_action_tx_done(cookie,generation,bssid);
 return vendor_error; }
static void rt_ieee80211msg(struct ifnet *ifp,int type,const void *data,size_t len) {
 (void)ifp;assert(!spin_depth);
 if(type==RTM_IEEE80211_STA_RX){memcpy(&rx_event,data,len);
  assert(len==offsetof(struct ieee80211req_sta_mgmt,data)+rx_event.len);rx_events++;}
 else {assert(type==RTM_IEEE80211_STA_TX);memcpy(&tx_event,data,len);tx_events++;} }
static void explicit_memset(void *p,int c,size_t n) { memset(p,c,n); }
''' + syntax + action_helpers + tx_helpers + tx + rx + r'''
static void setup(void) {
 memset(&sc,0,sizeof(sc));espwifi_global=&sc;
 sc.sc_ifp.if_softc=&sc;sc.sc_ic.ic_ifp=&sc.sc_ifp;
 sc.sc_scan_sem=&sem_rx;sc.sc_scan_ctl_sem=&sem_ctl;
 sc.sc_attached=sc.sc_scan_enabled=sc.sc_sta_enabled=true;
 sc.sc_radio_ready=sc.sc_sta_started=sc.sc_joined=sc.sc_link=true;
 sc.sc_port_authorized=1;sc.sc_join_active=9;
 sc.sc_ic.ic_state=IEEE80211_S_RUN;sc.sc_ic.ic_opmode=IEEE80211_M_STA;
 static struct ieee80211_channel channel;static struct ieee80211_node bss;
 channel=(struct ieee80211_channel){.ic_freq=2437,.number=6};
 bss=(struct ieee80211_node){.ni_chan=&channel,.ni_bssid={2,3,4,5,6,7}};
 sc.sc_ic.ic_bss=&bss;memcpy(sc.sc_join_bssid,bss.ni_bssid,6);
 vendor_calls=vendor_error=vendor_sync_callback=rx_events=tx_events=0;
 report_race=false;memset(&report_race_req,0,sizeof(report_race_req));
 sem_rx=sem_ctl=0;memset(&rx_event,0,sizeof(rx_event));
 memset(&tx_event,0,sizeof(tx_event)); }
static struct ieee80211req_sta_mgmt request(void) {
 struct ieee80211req_sta_mgmt r={0};r.version=IEEE80211_STA_VERSION;
 r.op=IEEE80211_STA_TX;r.cookie=77;r.generation=9;r.freq=2437;r.len=3;
 memcpy(r.bssid,sc.sc_join_bssid,6);r.data[0]=5;r.data[1]=4;r.data[2]=1;return r; }
static void test_kernel_lock(void) {
 if (!report_race) return;
 report_race=false;
 struct ieee80211req_sta_mgmt cancel=report_race_req;
 cancel.op=IEEE80211_STA_CANCEL_TX;cancel.freq=0;cancel.len=0;
 assert(ieee80211_sta_action_cancel(&sc.sc_ic.ic_sta_action,&cancel));
 assert(ieee80211_sta_action_release(&sc.sc_ic.ic_sta_action,&report_race_req));
 espwifi_if_action_tx_clear(&sc);
 assert(espwifi_if_action_tx_request(&sc,&report_race_req)==0);
}
int main(void) {
 struct ieee80211req_sta_mgmt r,cancel;struct espwifi_scan_frame f;
 uint8_t body[]={5,5,1};
 setup();
 /* Accepted Action RX copies borrowed bytes into the bounded shared ring. */
 assert(ieee80211_sta_action_valid(body,sizeof(body),false,
   IEEE80211_STA_ACTION_BTM|IEEE80211_STA_ACTION_NR,IEEE80211_STA_MAX_BODY));
 assert(sc.sc_attached && sc.sc_scan_enabled && sc.sc_sta_enabled &&
   sc.sc_join_active==9 && sc.sc_port_authorized==1 &&
   memcmp(sc.sc_join_bssid,(uint8_t[]){2,3,4,5,6,7},6)==0);
 int rxret=espwifi_if_action_rx(body,sizeof(body),sc.sc_join_bssid,6);
 assert(rxret==0);
 assert(sc.sc_scan_count==1 && sem_rx==1);
 body[1]=99;f=sc.sc_scan_ring[sc.sc_scan_head];
 espwifi_if_action_rx_process(&sc,&f);
 assert(rx_events==1 && rx_event.version==IEEE80211_STA_VERSION &&
  rx_event.op==IEEE80211_STA_GET && rx_event.cookie==0 && rx_event.flags==0 &&
  rx_event.generation==9 && rx_event.freq==2437 && rx_event.len==3 &&
  rx_event.data[1]==5 && memcmp(rx_event.bssid,sc.sc_join_bssid,6)==0);
 body[1]=5;
 /* Reject malformed, foreign-peer, and full-ring input before route delivery. */
 setup();uint8_t bad[]={5,5,1,52,1};
 assert(espwifi_if_action_rx(bad,sizeof(bad),sc.sc_join_bssid,6)==-1);
 uint8_t peer[6]={2,3,4,5,6,8};
 assert(espwifi_if_action_rx(body,sizeof(body),peer,6)==-1);
 sc.sc_scan_count=4;
 assert(espwifi_if_action_rx(body,sizeof(body),sc.sc_join_bssid,6)==-1);
 /* Stale epoch, wrong BSS/channel, disabled port, or revoked authorization
  * after enqueue cannot become a STA_RX event. */
 setup();assert(espwifi_if_action_rx(body,sizeof(body),sc.sc_join_bssid,6)==0);
 f=sc.sc_scan_ring[0];sc.sc_join_active++;
 espwifi_if_action_rx_process(&sc,&f);assert(rx_events==0);
 setup();assert(espwifi_if_action_rx(body,sizeof(body),sc.sc_join_bssid,6)==0);
 f=sc.sc_scan_ring[0];sc.sc_ic.ic_bss->ni_chan->number=1;
 espwifi_if_action_rx_process(&sc,&f);assert(rx_events==0);
 setup();assert(espwifi_if_action_rx(body,sizeof(body),sc.sc_join_bssid,6)==0);
 f=sc.sc_scan_ring[0];sc.sc_port_authorized=0;
 espwifi_if_action_rx_process(&sc,&f);assert(rx_events==0);
 /* TX ioctl admission returns immediately; it does not wait for the worker. */
 setup();r=request();assert(espwifi_if_action_tx_request(&sc,&r)==0);
 assert(sc.sc_action_tx_owned && sc.sc_action_tx_pending && vendor_calls==0 && sem_ctl==1);
 assert(espwifi_if_action_tx_request(&sc,&r)==EBUSY);
 espwifi_if_action_tx_control(&sc);assert(vendor_calls==1 && tx_events==0 &&
  sc.sc_action_tx_owned && !sc.sc_action_tx_pending);
 espwifi_if_action_tx_done(77,9,sc.sc_join_bssid);
 espwifi_if_action_tx_report(&sc);assert(tx_events==1 && !sc.sc_action_tx_owned &&
  tx_event.cookie==77 && tx_event.generation==9 && tx_event.flags==0);
 /* Queued cancellation avoids firmware; in-flight cancellation quarantines
  * ownership until callback drain and suppresses route completion. */
 setup();r=request();assert(espwifi_if_action_tx_request(&sc,&r)==0);
 cancel=r;cancel.op=IEEE80211_STA_CANCEL_TX;cancel.len=0;cancel.freq=0;
 assert(espwifi_if_action_tx_cancel(&sc,&cancel)==0 && !sc.sc_action_tx_owned);
 espwifi_if_action_tx_control(&sc);assert(vendor_calls==0 && tx_events==0);
 setup();r=request();assert(espwifi_if_action_tx_request(&sc,&r)==0);
 espwifi_if_action_tx_control(&sc);assert(vendor_calls==1);
 cancel=r;cancel.op=IEEE80211_STA_CANCEL_TX;cancel.len=0;cancel.freq=0;
 assert(espwifi_if_action_tx_cancel(&sc,&cancel)==0 && sc.sc_action_tx_owned);
 espwifi_if_action_tx_done(77,9,sc.sc_join_bssid);
 espwifi_if_action_tx_report(&sc);assert(tx_events==0 && !sc.sc_action_tx_owned);
 /* If the report snapshot crosses KERNEL_LOCK while the old tuple is
  * cancelled/released and an identical wire tuple is reused, it must not
  * invalidate or retire the replacement physical request. */
 setup();r=request();assert(espwifi_if_action_tx_request(&sc,&r)==0);
 espwifi_if_action_tx_control(&sc);espwifi_if_action_tx_done(77,9,sc.sc_join_bssid);
 report_race_req=r;report_race=true;uint64_t old_serial=sc.sc_action_tx_serial;
 espwifi_if_action_tx_report(&sc);
 assert(!report_race && sc.sc_action_tx_serial!=old_serial &&
  sc.sc_action_tx_owned && sc.sc_action_tx_pending &&
  sc.sc_ic.ic_sta_action.active && !sc.sc_ic.ic_sta_action.cancelled &&
  tx_events==0);

 /* A synchronous callback is paired with the later enqueue return once. */
 setup();vendor_sync_callback=1;r=request();
 assert(espwifi_if_action_tx_request(&sc,&r)==0);
 espwifi_if_action_tx_control(&sc);assert(tx_events==1 && !sc.sc_action_tx_owned);
 /* Submission failure is drained and reported with ACK clear. */
 setup();vendor_error=EIO;r=request();
 assert(espwifi_if_action_tx_request(&sc,&r)==0);
 espwifi_if_action_tx_control(&sc);
 assert(tx_events==1 && tx_event.flags==0 && !sc.sc_action_tx_owned);
 /* Disconnect invalidates queued work; started work remains owned to drain. */
 setup();r=request();assert(espwifi_if_action_tx_request(&sc,&r)==0);
 espwifi_if_action_tx_invalidate(&sc);assert(!sc.sc_action_tx_owned);
 espwifi_if_action_tx_control(&sc);assert(vendor_calls==0);
 return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'action-bridge.c'
    out = Path(td) / 'action-bridge'
    c.write_text(code.replace('assert(', 'CHECK('))
    if a.cppcheck:
        subprocess.run([a.cppcheck, '--enable=warning,performance,portability',
            '--suppress=missingIncludeSystem',
            '--error-exitcode=1',
            '--std=c11', str(c)], check=True)
    if a.analyze:
        subprocess.run([a.cc, '--analyze', '-std=c11', '-Wall', '-Wextra',
            '-Werror', str(c), '-o', str(Path(td) / 'analysis.plist')], check=True)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', str(c), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('actual ESP Action RX validation/queue and asynchronous TX ownership passed')
