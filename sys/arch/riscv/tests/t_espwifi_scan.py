#!/usr/bin/env python3
"""Exercise the actual vendor scan capture boundary with mocked radio calls."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if not __debug__:
    sys.exit('this test requires Python assertions enabled')

p = argparse.ArgumentParser(); p.add_argument('--cc', required=True); a = p.parse_args()
source = (Path(__file__).resolve().parents[1] / 'espressif/wifi/glue.c').read_text()
def function(name, result):
    m = re.search(r'\n(?:static )?' + result + r'\n' + name + r'\([^)]*\)\n\{.*?\n\}', source, re.S)
    assert m, name
    return m.group()
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
typedef int wifi_promiscuous_pkt_type_t;
typedef struct { unsigned sig_len,channel,timestamp; int rssi; } rxctrl;
typedef struct { rxctrl rx_ctrl; uint8_t payload[64]; } wifi_promiscuous_pkt_t;
typedef struct { unsigned filter_mask; } wifi_promiscuous_filter_t;
#define WIFI_PKT_MGMT 1
#define WIFI_PROMIS_FILTER_MASK_MGMT 1
#define ESP_OK 0
static int received, step, failure; static unsigned gotlen,gotchannel,gotstamp; static int gotrssi;
static void espwifi_os_scan_frame(const void *p,unsigned len,unsigned channel,int rssi,unsigned timestamp) {
 assert(p); received++;gotlen=len;gotchannel=channel;gotrssi=rssi;gotstamp=timestamp; }
static int esp_wifi_set_promiscuous_rx_cb(void (*f)(void *,int)) {
 assert(f);step++;return failure==step?9:0; }
static int esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *f) {
 assert(f->filter_mask==1);step++;return failure==step?9:0; }
static int esp_wifi_set_promiscuous(bool on) { (void)on;step++;return failure==step?9:0; }
''' + function('espwifi_scan_capture_rx', 'void') + function('espwifi_scan_capture', 'int') + r'''
int main(void) {
 wifi_promiscuous_pkt_t p={0};p.rx_ctrl.sig_len=40;p.rx_ctrl.channel=6;p.rx_ctrl.rssi=-42;p.rx_ctrl.timestamp=77;
 p.payload[0]=0x80;espwifi_scan_capture_rx(&p,WIFI_PKT_MGMT);
 assert(received==1 && gotlen==36 && gotchannel==6 && gotrssi==-42 && gotstamp==77);
 p.payload[0]=0x50;espwifi_scan_capture_rx(&p,WIFI_PKT_MGMT);assert(received==2);
 p.payload[0]=0xb0;espwifi_scan_capture_rx(&p,WIFI_PKT_MGMT);
 p.payload[0]=0x88;espwifi_scan_capture_rx(&p,WIFI_PKT_MGMT);
 p.payload[0]=0x80;p.rx_ctrl.sig_len=27;espwifi_scan_capture_rx(&p,WIFI_PKT_MGMT);
 espwifi_scan_capture_rx(NULL,WIFI_PKT_MGMT);espwifi_scan_capture_rx(&p,0);assert(received==2);
 for (int i=1;i<=3;i++) { step=0;failure=i;assert(espwifi_scan_capture(1)==9 && step==i); }
 step=failure=0;assert(espwifi_scan_capture(1)==0 && step==3);
 step=0;assert(espwifi_scan_capture(0)==0 && step==1);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'scan.c'; out=Path(td)/'scan'; c.write_text(code)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('actual scan capture callback filtering and radio errors passed')

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
struct ap { unsigned char ssid[33]; unsigned primary; int rssi,authmode; };
static struct ap aps[16];static int naps,start_error,records_error,clear_calls,too_many;
#define ESP_OK 0
static int esp_wifi_scan_start(void *p,bool block) { assert(!p && block);return start_error; }
static int esp_wifi_scan_get_ap_records(uint16_t *n,struct ap *p) {
 assert(*n==16 && p==aps);*n=too_many?17:0;return records_error; }
static int esp_wifi_clear_ap_list(void) { clear_calls++;return 0; }
static void espwifi_os_log(const char *fmt,...) { (void)fmt; }
static unsigned espwifi_os_free_heap(void) { return 100; }
''' + function('espwifi_scan', 'int') + r'''
int main(void) {
 start_error=3;assert(espwifi_scan()==-1 && clear_calls==1);
 start_error=0;records_error=4;assert(espwifi_scan()==-1 && clear_calls==2);
 records_error=0;too_many=1;assert(espwifi_scan()==-1);
 too_many=0;assert(espwifi_scan()==0);return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'scanstart.c'; out=Path(td)/'scanstart'; c.write_text(code)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('actual blocking scan errors and record bounds passed')

kernel = (Path(__file__).resolve().parents[1] / 'espressif/esp_wifi_if.c').read_text()
def kernel_function(name, result):
    m = re.search(r'\n(?:static )?' + result + r'\n' + name + r'\(.*?\)\n\{.*?\n\}', kernel, re.S)
    assert m, name
    return m.group()
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <setjmp.h>
#include <string.h>
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
 IEEE80211_S_ASSOC, IEEE80211_S_RUN };
struct ifnet { void *if_softc; int if_flags; };
struct espwifi_ec { struct ifnet ec_if; };
struct ieee80211_node { uint8_t ni_bssid[6],ni_essid[32]; unsigned ni_esslen; };
struct ieee80211com { struct ifnet *ic_ifp; int ic_flags; enum ieee80211_state ic_state;
 struct ieee80211_node *ic_bss; const uint8_t *ic_opt_ie; unsigned ic_opt_ie_len; };
struct espwifi_softc { struct ieee80211com sc_ic; struct espwifi_ec sc_ec; int sc_scan_lock;
	int sc_key_lock;bool sc_key_pending;
 void *sc_scan_ctl_sem; unsigned sc_scan_count; bool sc_scan_enabled,sc_scan_pending,
 sc_scan_busy,sc_radio_ready,sc_attached,sc_sta_started,sc_join_stopped;
 uint64_t sc_scan_generation,sc_scan_active;
 uint64_t sc_join_generation,sc_join_active,sc_key_generation;
 uint8_t sc_key_slots,sc_group_key_slots,sc_join_bssid[6],sc_join_ssid[32],sc_join_rsn[256];
 unsigned sc_join_ssid_len,sc_join_rsn_len;bool sc_join_pending,sc_joined,sc_join_disconnect_pending,sc_join_rescan_pending,sc_join_barrier_busy;
 unsigned sc_port_authorized;bool sc_ptk_installed,sc_link;
 int (*sc_newstate)(struct ieee80211com *,enum ieee80211_state,int); };
#define IEEE80211_F_SCAN 1
#define IEEE80211_F_WPA 2
#define ESPWIFI_NET80211_WPA2 1
#define IEEE80211_ELEMID_RSN 48
#define IEEE80211_NWID_LEN 32
#define IEEE80211_MAX_OPT_IE 256
#define ETHER_ADDR_LEN 6
#define IEEE80211_FC0_SUBTYPE_ASSOC_RESP 0x10
#define LINK_STATE_DOWN 0
#define ESPWIFI_WAIT_FOREVER 0
static struct espwifi_softc espwifi_sc; static int held, keyheld, key_controls, join_controls, signals, starts, stops, ends, cancels, fail, stale, waits, parentcalls, begins, autoretry;
static jmp_buf done;
static void mutex_spin_enter(int *p) { (void)p;assert(!held);held=1; }
static void mutex_spin_exit(int *p) { (void)p;assert(held);held=0; }
static void mutex_enter(int *p) { (void)p;assert(!keyheld);keyheld=1; }
static void mutex_exit(int *p) { (void)p;assert(keyheld);keyheld=0; }
static void espwifi_if_key_control(struct espwifi_softc *sc) {
	key_controls++;sc->sc_key_pending=false; }
static void espwifi_os_sem_give(void *p) { (void)p;assert(!held);signals++; }
static void atomic_store_release(unsigned *p,unsigned v) { *p=v; }
static void if_link_state_change(struct ifnet *p,int s) { (void)p;(void)s; }
static void ieee80211_sta_leave(struct ieee80211com *ic,struct ieee80211_node *ni) { (void)ic;(void)ni; }
static void espwifi_os_sem_take(void *p,int t) { (void)p;(void)t;if (++waits==2) longjmp(done,1); }
static void espwifi_os_task_delay(unsigned t) { (void)t;assert(0); }
static int espwifi_scan_capture(int on) { if(on){starts++;return fail?9:0;} stops++;return 0; }
static int espwifi_scan(void) { if(stale) espwifi_sc.sc_scan_generation++; return 1; }
static int splnet(void) { return 0; } static void splx(int s) { (void)s; }
static void ieee80211_begin_scan(struct ieee80211com *ic,int reset) { assert(reset==1);begins++;ic->ic_flags|=IEEE80211_F_SCAN; }
static void ieee80211_end_scan(struct ieee80211com *ic) { ends++;ic->ic_flags=autoretry ? IEEE80211_F_SCAN : 0; }
static void ieee80211_cancel_scan(struct ieee80211com *ic) { cancels++;ic->ic_flags=0; }
static void espwifi_if_join_control(struct espwifi_softc *sc) { join_controls++;
 sc->sc_join_pending=false;sc->sc_join_disconnect_pending=false; }
static int oldstate(struct ieee80211com *ic,enum ieee80211_state st,int arg) {
 parentcalls++;
 (void)arg;ic->ic_state=st;ic->ic_flags=(st==IEEE80211_S_SCAN)?IEEE80211_F_SCAN:0;return 0; }
''' + kernel_function('espwifi_if_newstate','int') + kernel_function('espwifi_if_radio_ready','void') + kernel_function('espwifi_if_scan_control','void') + r'''
static void control(void) { waits=0;if (setjmp(done)==0) espwifi_if_scan_control(&espwifi_sc); }
int main(void) {
	struct ifnet ifp={&espwifi_sc};espwifi_sc.sc_ic.ic_ifp=&ifp;espwifi_sc.sc_newstate=oldstate;espwifi_sc.sc_attached=true;espwifi_sc.sc_sta_started=true;
 assert(espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_AUTH,0)==EOPNOTSUPP);
 assert(espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_SCAN,0)==EOPNOTSUPP);
 espwifi_sc.sc_scan_enabled=true;
 assert(!espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_SCAN,0) && !signals);
 uint64_t generation=espwifi_sc.sc_scan_generation;
 assert(parentcalls==1);
 assert(!espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_SCAN,0));
 assert(parentcalls==1 && espwifi_sc.sc_scan_generation==generation && !signals);
 espwifi_if_radio_ready();assert(signals==1);
 control();assert(starts==1 && stops==1 && ends==1 && !cancels && !espwifi_sc.sc_scan_active);
 assert(!espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_SCAN,0));
 assert(begins==1 && signals==2);control();
 assert(starts==2 && stops==2 && ends==2);
 assert(!espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_INIT,0));
 assert(!espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_SCAN,0));fail=1;control();
 assert(cancels==1 && ends==2 && stops==3);
 fail=0;stale=1;assert(!espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_INIT,0));
 assert(!espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_SCAN,0));control();
 assert(ends==2 && cancels==1 && !espwifi_sc.sc_scan_active);
 stale=0;autoretry=1;assert(!espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_INIT,0));
 assert(!espwifi_if_newstate(&espwifi_sc.sc_ic,IEEE80211_S_SCAN,0));
 int previous=signals;control();assert(espwifi_sc.sc_scan_pending && signals==previous+1);
	struct espwifi_softc joinsc={.sc_radio_ready=true,.sc_sta_started=true,
	    .sc_scan_ctl_sem=(void *)1,
	    .sc_port_authorized=1,.sc_ptk_installed=true,.sc_newstate=oldstate};
	struct ifnet joinif={&joinsc};
	struct ieee80211_node bss={.ni_bssid={2,3,4,5,6,7},.ni_essid={'t','e','s','t'},.ni_esslen=4};
	uint8_t rsn[22]={48,20};
	struct ieee80211com joinic={.ic_ifp=&joinif,.ic_flags=IEEE80211_F_WPA,
	    .ic_state=IEEE80211_S_SCAN,.ic_bss=&bss,.ic_opt_ie=rsn,.ic_opt_ie_len=sizeof(rsn)};
	joinsc.sc_ic=joinic;previous=signals;
	assert(espwifi_if_newstate(&joinsc.sc_ic,IEEE80211_S_AUTH,0)==0);
	assert(joinsc.sc_join_active==1 && joinsc.sc_join_pending &&
	    joinsc.sc_join_ssid_len==4 && !memcmp(joinsc.sc_join_ssid,"test",4) &&
	    !memcmp(joinsc.sc_join_bssid,bss.ni_bssid,6) &&
	    joinsc.sc_join_rsn_len==sizeof(rsn) && !joinsc.sc_port_authorized &&
	    !joinsc.sc_ptk_installed && signals==previous+1);
	assert(espwifi_if_newstate(&joinsc.sc_ic,IEEE80211_S_AUTH,0)==EBUSY);
	assert(joinsc.sc_join_active==1 && joinsc.sc_join_pending);
	assert(espwifi_if_newstate(&joinsc.sc_ic,IEEE80211_S_INIT,0)==0);
	assert(joinsc.sc_join_active==0 && !joinsc.sc_join_pending &&
	    joinsc.sc_join_disconnect_pending && !joinsc.sc_port_authorized &&
	    !joinsc.sc_ptk_installed);
	espwifi_sc.sc_key_pending=true;
	espwifi_sc.sc_join_disconnect_pending=true;
	espwifi_sc.sc_key_pending=true;
	control();
	assert(key_controls==1 && join_controls==1 && !keyheld &&
	    !espwifi_sc.sc_key_pending && !espwifi_sc.sc_join_disconnect_pending);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'control.c'; out=Path(td)/'control'; c.write_text(code)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('actual scan state ordering, failure and stale generation passed')

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define ESPWIFI_SCAN_RING 8
#define ESPWIFI_SCAN_FRAME_MAX 2304
struct ieee80211_frame { char h[24]; };
struct espwifi_scan_frame { unsigned len, channel; uint32_t timestamp; uint64_t generation; int rssi; uint8_t data[ESPWIFI_SCAN_FRAME_MAX]; };
struct espwifi_softc { bool sc_attached,sc_scan_enabled; int sc_scan_lock; void *sc_scan_sem;
 unsigned sc_scan_count,sc_scan_head; uint64_t sc_scan_active;
 struct espwifi_scan_frame sc_scan_ring[ESPWIFI_SCAN_RING]; };
static struct espwifi_softc espwifi_sc; static int held,signals;
static void mutex_spin_enter(int *p) { (void)p;assert(!held);held=1; }
static void mutex_spin_exit(int *p) { (void)p;assert(held);held=0; }
static void espwifi_os_sem_give(void *p) { (void)p;assert(!held);signals++; }
''' + kernel_function('espwifi_if_scan_frame','void') + r'''
int main(void) {
 unsigned char data[32];memset(data,0x5a,sizeof(data));
 espwifi_if_scan_frame(data,sizeof(data),6,-40,7);assert(!signals);
 espwifi_sc.sc_attached=espwifi_sc.sc_scan_enabled=true;
 espwifi_if_scan_frame(data,sizeof(data),6,-40,7);assert(!signals);
 espwifi_sc.sc_scan_active=17;
 espwifi_if_scan_frame(data,23,6,-40,7);espwifi_if_scan_frame(data,2305,6,-40,7);
 espwifi_if_scan_frame(data,sizeof(data),15,-40,7);assert(!signals);
 for (int i=0;i<9;i++) espwifi_if_scan_frame(data,sizeof(data),6,-40,7);
 assert(signals==8 && espwifi_sc.sc_scan_count==8 && !held);
 assert(espwifi_sc.sc_scan_ring[0].generation==17 && espwifi_sc.sc_scan_ring[0].rssi==-40 && espwifi_sc.sc_scan_ring[0].timestamp==7);
 assert(!memcmp(data,espwifi_sc.sc_scan_ring[0].data,sizeof(data)));
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'queue.c'; out=Path(td)/'queue'; c.write_text(code)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('actual scan callback bounds, generation and queue capacity passed')

code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define __UNCONST(p) ((void *)(uintptr_t)(p))
#define ESPWIFI_WAIT_FOREVER 0
#define ESPWIFI_SCAN_RING 8
#define IEEE80211_F_SCAN 1
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
struct ifnet { int dummy; }; struct mbuf { unsigned char data[64]; };
struct ieee80211_channel { int dummy; };struct ieee80211_frame_min { int dummy; };
struct ieee80211_node { int ni_rssi_dbm; bool ni_rssi_dbm_valid; };
struct ieee80211com { int ic_flags;struct ieee80211_channel *ic_curchan,ic_channels[15]; int ic_scan; };
struct espwifi_scan_frame { unsigned len,channel; uint32_t timestamp; uint64_t generation; int rssi; uint8_t data[2304]; };
struct espwifi_softc { void *sc_scan_sem;int sc_scan_lock;bool sc_scan_shutdown,sc_scan_busy;
 unsigned sc_scan_head,sc_scan_count;uint64_t sc_scan_generation;
 struct espwifi_scan_frame sc_scan_ring[8];struct { struct ifnet ec_if; } sc_ec;
 struct ieee80211com sc_ic; };
static int takes,locked;
static void espwifi_os_sem_take(void *s,int n) { (void)s;(void)n;takes++; }
static void mutex_spin_enter(int *p) { (void)p;assert(!locked);locked=1; }
static void mutex_spin_exit(int *p) { (void)p;assert(locked);locked=0; }
static struct mbuf *m_devget(void *d,unsigned l,int o,struct ifnet *i) { (void)d;(void)l;(void)o;(void)i;return NULL; }
static int splnet(void) { return 0; } static void splx(int s) { (void)s; }
static void m_freem(struct mbuf *m) { (void)m; }
#define mtod(m,t) ((t)(m)->data)
static struct ieee80211_node *ieee80211_find_rxnode(struct ieee80211com *ic,const struct ieee80211_frame_min *f) { (void)ic;(void)f;return NULL; }
static void ieee80211_input(struct ieee80211com *ic,struct mbuf *m,struct ieee80211_node *n,int r,unsigned t) { (void)ic;(void)m;(void)n;(void)r;(void)t; }
static void ieee80211_free_node(struct ieee80211_node *n) { (void)n; }
static struct ieee80211_node *ieee80211_find_node(int *t,const void *p) { (void)t;(void)p;return NULL; }
''' + kernel_function('espwifi_if_scanthread','void') + r'''
int main(void) {
 struct espwifi_softc sc={0};sc.sc_scan_shutdown=true;espwifi_if_scanthread(&sc);
 assert(takes==1 && !locked && !sc.sc_scan_busy);return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'shutdown.c';out=Path(td)/'shutdown';c.write_text(code)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('actual scan worker exits after shutdown without touching queued frames')
