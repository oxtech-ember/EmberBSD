#!/usr/bin/env python3
"""Exercise the ESP FullMAC join generation and event boundary."""
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
bridge_source = (root / 'espressif/wifi/fullmac_wpa2.c').read_text()
glue_source = (root / 'espressif/wifi/glue.c').read_text()

def function(name, result):
    match = re.search(r'\n(?:static )?' + result + r'\n' + name +
                      r'\(.*?\)\n\{.*?\n\}', source, re.S)
    assert match, name
    return match.group()

def bridge_function(name, result):
    match = re.search(r'\n(?:static )?' + result + r'\n' + name +
                      r'\(.*?\)\n\{.*?\n\}', bridge_source, re.S)
    assert match, name
    return match.group()

def glue_function(name, result):
    match = re.search(r'\n(?:static )?' + result + r'\n' + name +
                      r'\(.*?\)\n\{.*?\n\}', glue_source, re.S)
    assert match, name
    return match.group()

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
typedef unsigned int u_int;
typedef uint8_t u8;
typedef uint64_t u64;
#define ESPWIFI_NET80211 1
#define ESPWIFI_NET80211_WPA2 1
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_NWID_LEN 32
#define IEEE80211_MAX_OPT_IE 256
#define IEEE80211_ELEMID_RSN 48
#define IEEE80211_F_WPA 1
#define IEEE80211_F_SCAN 2
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
 IEEE80211_S_ASSOC, IEEE80211_S_RUN };
#define IEEE80211_FC0_SUBTYPE_ASSOC_RESP 0x10
#define IFF_RUNNING 1
#define LINK_STATE_UP 1
#define LINK_STATE_DOWN 0
#define ETHER_ADDR_LEN 6
#define ESPWIFI_WAIT_FOREVER 0
#define WLAN_FC_STYPE_ASSOC_RESP 1
#define WLAN_FC_STYPE_REASSOC_RESP 3
#define WLAN_FC_STYPE_AUTH 11
#define WLAN_FC_STYPE_ACTION 13
typedef int esp_err_t;
#define ESP_OK 0
struct ieee80211_node { uint8_t ni_bssid[6],ni_essid[32]; unsigned ni_esslen; };
struct ifnet { void *if_softc; int if_flags; };
struct ieee80211com { struct ifnet *ic_ifp; struct ieee80211_node *ic_bss;
 const uint8_t *ic_opt_ie; unsigned ic_opt_ie_len; unsigned ic_flags;
 enum ieee80211_state ic_state; };
struct espwifi_softc { struct { struct ifnet ec_if; } sc_ec; struct ieee80211com sc_ic;
 int sc_scan_lock; void *sc_scan_ctl_sem; unsigned sc_port_authorized;
 bool sc_ptk_installed,sc_link,sc_radio_ready,sc_join_pending,sc_joined,
 sc_join_disconnect_pending,sc_join_rescan_pending,sc_scan_enabled,sc_attached,
 sc_scan_pending,sc_scan_busy,sc_join_barrier_busy,sc_sta_started,sc_join_stopped;
 uint8_t sc_key_slots,sc_group_key_slots;uint64_t sc_key_generation,sc_scan_generation,sc_scan_active;
 uint64_t sc_join_generation,sc_join_active; uint8_t sc_join_bssid[6],sc_join_ssid[32],sc_join_rsn[256];
 unsigned sc_join_ssid_len,sc_join_rsn_len;
 int (*sc_newstate)(struct ieee80211com *,enum ieee80211_state,int); };
static struct espwifi_softc espwifi_sc;
static int locked,signals,link_changes,disconnects,connects,fail_disconnect,fail_connect;
static uint32_t stop_events,start_events;
static bool pending_stop,pending_start,defer_stop,defer_start,stale_signal;
static bool drop_start;
static unsigned noise_wakes,delay_stop,delay_start;
static uint64_t fake_time_us;
static int stop_calls,start_calls,sem_waits;
static void *link_change=(void *)1;
static unsigned lock_count,barrier_on_lock;
static bool invalidate_link_generation;
static bool stop_during_newstate;
int espwifi_if_assoc_response(unsigned int,const void *,unsigned,
 const unsigned char *);
static int splnet(void) { return 0; } static void splx(int s) { (void)s; }
static void mutex_spin_enter(int *p) { (void)p;assert(!locked);locked=1;
 if (++lock_count==barrier_on_lock) espwifi_sc.sc_join_barrier_busy=true; }
static void mutex_spin_exit(int *p) { (void)p;assert(locked);locked=0;
 if (invalidate_link_generation) {
  espwifi_sc.sc_join_active=0;espwifi_sc.sc_joined=false;
  if (++espwifi_sc.sc_join_generation==0) espwifi_sc.sc_join_generation++;
  invalidate_link_generation=false;
 } }
static void espwifi_os_sem_give(void *p) { (void)p;assert(!locked);signals++; }
static void atomic_store_release(unsigned *p,unsigned v) { *p=v; }
static unsigned atomic_load_acquire(unsigned *p) { return *p; }
static void if_link_state_change(struct ifnet *p,int state) { (void)p;link_changes+=state?1:-1; }
void espwifi_if_radio_started(void);
void espwifi_if_radio_stopped(void);
static int espwifi_os_critical_enter(void) { return 0; }
static void espwifi_os_critical_exit(int s) { (void)s; }
static int espwifi_os_sem_take(void *p,unsigned ticks);
static int esp_wifi_stop(void) {
 stop_calls++;disconnects++;
 if (fail_disconnect) return fail_disconnect;
 if (defer_stop) pending_stop=true;
 else { espwifi_if_radio_stopped();stop_events++; }
 return 0;
}
static int esp_wifi_start(void) {
 start_calls++;
 assert(espwifi_sc.sc_join_stopped && !espwifi_sc.sc_join_active &&
     !espwifi_sc.sc_joined && !espwifi_sc.sc_radio_ready);
 if (fail_connect) return fail_connect;
	if (defer_start) {
		if (!drop_start) pending_start=true;
	} else { espwifi_if_radio_started();start_events++; }
 return 0;
}
static int espwifi_os_sem_take(void *p,unsigned ticks) {
 (void)p;(void)ticks;sem_waits++;fake_time_us+=100000;
 if (noise_wakes != 0) { noise_wakes--;return 1; }
 if (stale_signal) { stale_signal=false;return 1; }
	if (pending_stop) {
		if (delay_stop != 0) { delay_stop--;return 0; }
		pending_stop=false;espwifi_if_radio_stopped();stop_events++;return 1;
	}
	if (pending_start) {
		if (delay_start != 0) { delay_start--;return 0; }
		pending_start=false;espwifi_if_radio_started();start_events++;return 1;
	}
 return 0;
}
static long long espwifi_os_time_us(void) { return (long long)fake_time_us; }
static int espwifi_ext_connect(const unsigned char *ssid,unsigned slen,
 const unsigned char *bssid,const unsigned char *rsn,unsigned rlen) {
 assert(slen==4 && memcmp(ssid,"test",4)==0 && bssid[0]==2 && rlen==22 && rsn[0]==48);
 connects++;return fail_connect; }
static int espwifi_os_assoc_response(unsigned int subtype,const void *body,
 unsigned int len,const unsigned char *sender) {
 return espwifi_if_assoc_response(subtype,body,len,sender); }
static int parent_state(struct ieee80211com *ic,enum ieee80211_state state,int arg) {
 (void)arg;ic->ic_state=state;
 if (stop_during_newstate) { stop_during_newstate=false;espwifi_if_radio_stopped(); }
 return 0; }
static void ieee80211_sta_leave(struct ieee80211com *,struct ieee80211_node *);
static void ieee80211_begin_scan(struct ieee80211com *,int);
static void explicit_memset(void *,int,size_t);
static int printf(const char *,...);
static int ieee80211_new_state(struct ieee80211com *ic,enum ieee80211_state state,int arg);
''' + function('espwifi_if_radio_started','void') + function('espwifi_if_radio_stopped','void') + glue_function('espwifi_disconnect','int') + function('espwifi_if_newstate','int') + function('espwifi_if_join_control','void') + function('espwifi_if_link_event','void') + function('espwifi_if_link','void') + function('espwifi_if_assoc_response','int') + function('espwifi_if_wpa_in_4way','bool') + bridge_function('espwifi_wpa2_sta_rx_mgmt','int') + r'''
''' + function('espwifi_if_set_port','int') + r'''
static void ieee80211_begin_scan(struct ieee80211com *ic,int reset) { (void)reset;ic->ic_flags|=IEEE80211_F_SCAN; }
static void ieee80211_sta_leave(struct ieee80211com *ic,struct ieee80211_node *ni) { (void)ic;(void)ni; }
static int ieee80211_new_state(struct ieee80211com *ic,enum ieee80211_state state,int arg) {
 return espwifi_if_newstate(ic,state,arg); }
static void explicit_memset(void *p,int c,size_t n) { memset(p,c,n); }
static int printf(const char *fmt,...) { (void)fmt;return 0; }
int main(void) {
 struct espwifi_softc *sc=&espwifi_sc;
 struct ieee80211_node bss={.ni_bssid={2,3,4,5,6,7},.ni_essid={'t','e','s','t'},.ni_esslen=4};
 uint8_t rsn[22]={48,20};
 uint8_t other[6]={2,8,8,8,8,8};
	sc->sc_ec.ec_if.if_softc=sc;sc->sc_ec.ec_if.if_flags=IFF_RUNNING;
	sc->sc_ic=(struct ieee80211com){.ic_ifp=&sc->sc_ec.ec_if,.ic_bss=&bss,.ic_opt_ie=rsn,
     .ic_opt_ie_len=sizeof(rsn),.ic_flags=IEEE80211_F_WPA,.ic_state=IEEE80211_S_SCAN};
 sc->sc_newstate=parent_state;sc->sc_scan_ctl_sem=(void *)1;sc->sc_radio_ready=true;
 sc->sc_scan_enabled=true;sc->sc_attached=true;
	assert(espwifi_if_newstate(&sc->sc_ic,IEEE80211_S_AUTH,0)==EOPNOTSUPP);
	espwifi_if_radio_started();

	/* A barrier can start between AUTH's optimistic check and commit. */
	lock_count=0;barrier_on_lock=2;
	assert(espwifi_if_newstate(&sc->sc_ic,IEEE80211_S_AUTH,0)==EBUSY);
	barrier_on_lock=0;sc->sc_join_barrier_busy=false;sc->sc_ic.ic_state=IEEE80211_S_SCAN;

 /* Association is queued with copied inputs and a nonzero operation id. */
 assert(espwifi_if_newstate(&sc->sc_ic,IEEE80211_S_AUTH,0)==0);
 uint64_t first=sc->sc_join_active;
 assert(first!=0 && sc->sc_join_pending && sc->sc_join_ssid_len==4 &&
     memcmp(sc->sc_join_ssid,"test",4)==0 && sc->sc_join_rsn_len==22);
 espwifi_if_join_control(sc);
 assert(connects==1 && sc->sc_join_active==first && !sc->sc_join_pending);

 /* Public fully-connected event follows EAPOL and is not the assoc boundary. */
 espwifi_if_link(1,bss.ni_bssid);
 assert(!sc->sc_joined && !sc->sc_link && sc->sc_ic.ic_state==IEEE80211_S_SCAN);
 uint8_t assoc[6]={0,0,0,0,1,0};
 uint8_t bad_status[6]={0,0,1,0,1,0};
 assert(espwifi_wpa2_sta_rx_mgmt(1,assoc,5,bss.ni_bssid,0,1,0)==-1);
 assert(espwifi_wpa2_sta_rx_mgmt(1,bad_status,sizeof(bad_status),bss.ni_bssid,0,1,0)==-1);
 assert(espwifi_wpa2_sta_rx_mgmt(2,assoc,sizeof(assoc),bss.ni_bssid,0,1,0)==-1);
 assert(espwifi_wpa2_sta_rx_mgmt(1,assoc,sizeof(assoc),other,0,1,0)==-1);
 assert(!sc->sc_joined && !sc->sc_link && !sc->sc_port_authorized);
 /* Matching success body is IDF's pre-EAPOL management callback boundary. */
 assert(espwifi_wpa2_sta_rx_mgmt(1,assoc,sizeof(assoc),bss.ni_bssid,0,1,0)==0);
 assert(sc->sc_joined && sc->sc_link && sc->sc_ic.ic_state==IEEE80211_S_RUN &&
     !sc->sc_port_authorized);
 assert(espwifi_if_wpa_in_4way());
 sc->sc_ptk_installed=true;
 sc->sc_key_generation=first;
 assert(espwifi_if_set_port(&sc->sc_ic,true)==0 && sc->sc_port_authorized);
 assert(!espwifi_if_wpa_in_4way());

 /* A mismatched AP event cannot change the selected association. */
 espwifi_if_link(0,other);
 assert(sc->sc_joined && sc->sc_port_authorized && sc->sc_link);

 /* Disconnect closes the port and fences joins once teardown starts. */
 espwifi_if_link(0,bss.ni_bssid);
 assert(!sc->sc_joined && !sc->sc_link && !sc->sc_ptk_installed &&
     !sc->sc_port_authorized && sc->sc_join_disconnect_pending &&
     sc->sc_join_rescan_pending && sc->sc_join_active==0);
 espwifi_if_link(1,bss.ni_bssid); /* stale same-BSSID event before STOP */
 assert(!sc->sc_joined && !sc->sc_link && sc->sc_join_disconnect_pending);
 assert(espwifi_if_newstate(&sc->sc_ic,IEEE80211_S_AUTH,0)==EBUSY);
	defer_stop=true;defer_start=true;stale_signal=true;
 espwifi_if_join_control(sc);
	assert(disconnects==1);
	assert(stop_calls==1 && start_calls==1 && sem_waits>=3);
	assert(!sc->sc_join_disconnect_pending);
	assert(sc->sc_ic.ic_state==IEEE80211_S_SCAN);
 assert(espwifi_wpa2_sta_rx_mgmt(1,assoc,sizeof(assoc),bss.ni_bssid,0,1,0)==-1);
 espwifi_if_link(1,bss.ni_bssid); /* late same-BSSID callback after STOP */
	assert(!sc->sc_joined && !sc->sc_link && !sc->sc_port_authorized);

 /* Only the new START event releases the fence; stale wakeups do not. */
 assert(!sc->sc_join_stopped && sc->sc_sta_started && sc->sc_radio_ready);
 assert(stop_events==1 && start_events==1);
 assert(espwifi_if_newstate(&sc->sc_ic,IEEE80211_S_AUTH,0)==0);
 uint64_t second=sc->sc_join_active;
 assert(second!=0 && second!=first);
 espwifi_if_join_control(sc);
 assert(connects==2 && sc->sc_join_active==second && !sc->sc_join_pending);
	/* A stop and start arriving after multiple wait timeouts still complete. */
	defer_stop=true;defer_start=true;delay_stop=12;delay_start=12;
	sem_waits=0;
	assert(espwifi_disconnect()==0 && stop_events==2 && start_events==2 &&
	    sem_waits>=24 && !sc->sc_join_stopped && sc->sc_radio_ready);

	/* An uncorrelated disconnect must fence the live operation too. */
	sc->sc_join_stopped=false;sc->sc_sta_started=true;sc->sc_radio_ready=true;
	sc->sc_join_active=99;sc->sc_joined=false;sc->sc_link=false;
	sc->sc_ic.ic_state=IEEE80211_S_SCAN;invalidate_link_generation=true;
	espwifi_if_link(1,bss.ni_bssid);
	assert(sc->sc_join_active==0 && !sc->sc_joined && !sc->sc_link &&
	    sc->sc_ic.ic_state==IEEE80211_S_SCAN);
	sc->sc_join_stopped=false;sc->sc_sta_started=true;sc->sc_radio_ready=true;
	sc->sc_join_active=99;sc->sc_joined=true;sc->sc_link=true;
	espwifi_if_link(0,NULL);
	assert(sc->sc_join_active==0 && sc->sc_join_disconnect_pending &&
	    !sc->sc_link && !sc->sc_port_authorized);

	/* STOP inside newstate must not be overwritten by the link commit tail. */
	sc->sc_join_stopped=false;sc->sc_sta_started=true;sc->sc_radio_ready=true;
	sc->sc_join_active=101;sc->sc_joined=false;sc->sc_link=false;
	sc->sc_join_disconnect_pending=false;sc->sc_ic.ic_state=IEEE80211_S_SCAN;
	memcpy(sc->sc_join_bssid,bss.ni_bssid,sizeof(sc->sc_join_bssid));
	stop_during_newstate=true;
	assert(espwifi_wpa2_sta_rx_mgmt(1,assoc,sizeof(assoc),bss.ni_bssid,0,1,0)==-1);
	assert(!stop_during_newstate && sc->sc_join_stopped &&
	    !sc->sc_join_active && !sc->sc_joined && !sc->sc_link &&
	    !sc->sc_port_authorized);
	/* Repeated unrelated semaphore wakes cannot extend either event deadline. */
	defer_stop=false;defer_start=true;drop_start=true;noise_wakes=30;
	sem_waits=0;
	assert(espwifi_disconnect()==-1 && start_calls==3 && sem_waits<=20);
 return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'join.c'
    out = Path(td) / 'join'
    c.write_text(code)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(c), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)

print('actual FullMAC join generation, STOP fence and PTK port gate passed')

disabled = code.replace('#define ESPWIFI_NET80211_WPA2 1\n', '')
disabled = disabled[:disabled.index('int main(void) {')] + r'''
int main(void) {
 struct espwifi_softc *sc=&espwifi_sc;
 struct ieee80211_node bss={.ni_bssid={2,3,4,5,6,7},.ni_essid={'t','e','s','t'},.ni_esslen=4};
 uint8_t rsn[22]={48,20};
 sc->sc_ec.ec_if.if_softc=sc;
 sc->sc_ic=(struct ieee80211com){.ic_ifp=&sc->sc_ec.ec_if,.ic_bss=&bss,
  .ic_opt_ie=rsn,.ic_opt_ie_len=sizeof(rsn),.ic_flags=IEEE80211_F_WPA,
  .ic_state=IEEE80211_S_SCAN};
 sc->sc_radio_ready=true;sc->sc_sta_started=true;
 assert(espwifi_if_newstate(&sc->sc_ic,IEEE80211_S_AUTH,0)==EOPNOTSUPP);
 assert(sc->sc_join_active==0 && !sc->sc_join_pending);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'disabled.c';out=Path(td)/'disabled';c.write_text(disabled)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('WPA2 FullMAC association remains disabled without explicit experimental option')

failure = code[:code.index('int main(void) {')] + r'''
int main(void) {
 struct espwifi_softc *sc=&espwifi_sc;
 struct ieee80211_node bss={.ni_bssid={2,3,4,5,6,7},.ni_essid={'t','e','s','t'},.ni_esslen=4};
 uint8_t rsn[22]={48,20};
 sc->sc_ec.ec_if.if_softc=sc;sc->sc_ec.ec_if.if_flags=IFF_RUNNING;
 sc->sc_ic=(struct ieee80211com){.ic_ifp=&sc->sc_ec.ec_if,.ic_bss=&bss,
  .ic_opt_ie=rsn,.ic_opt_ie_len=sizeof(rsn),.ic_flags=IEEE80211_F_WPA,
  .ic_state=IEEE80211_S_SCAN};
 sc->sc_newstate=parent_state;sc->sc_scan_ctl_sem=(void *)1;
 sc->sc_radio_ready=true;sc->sc_sta_started=true;sc->sc_scan_enabled=true;
 sc->sc_attached=true;fail_connect=1;fail_disconnect=1;
 assert(espwifi_if_newstate(&sc->sc_ic,IEEE80211_S_AUTH,0)==0);
 espwifi_if_join_control(sc);
 assert(sc->sc_join_active==0 && sc->sc_join_disconnect_pending && connects==1);
 espwifi_if_join_control(sc);
 assert(disconnects==1 && !sc->sc_join_barrier_busy && !sc->sc_radio_ready &&
  sc->sc_join_stopped);
 espwifi_if_link(1,bss.ni_bssid); /* never accept a late connect after failed stop */
 assert(!sc->sc_joined && !sc->sc_link);
 assert(espwifi_if_newstate(&sc->sc_ic,IEEE80211_S_AUTH,0)==EOPNOTSUPP);
 assert(connects==1);return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'failure.c';out=Path(td)/'failure';c.write_text(failure)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('connect and STOP failures leave the WPA2 join path fenced closed')

# The non-net80211 Ethernet-only configuration retains its old link behavior.
off = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define LINK_STATE_UP 1
#define LINK_STATE_DOWN 0
struct ifnet { int unused; };
struct espwifi_softc { struct { struct ifnet ec_if; } sc_ec;
 bool sc_attached,sc_link; };
static struct espwifi_softc espwifi_sc;
static int changes,last_state;
static void if_link_state_change(struct ifnet *ifp,int state) {
 (void)ifp;changes++;last_state=state;
}
''' + function('espwifi_if_link_event','void') + function('espwifi_if_link','void') + r'''
int main(void) {
 uint8_t bssid[6]={2,3,4,5,6,7};
 espwifi_sc.sc_attached=true;
 espwifi_if_link(1,bssid);
 assert(espwifi_sc.sc_link && changes==1 && last_state==LINK_STATE_UP);
 espwifi_if_link(0,bssid);
 assert(!espwifi_sc.sc_link && changes==2 && last_state==LINK_STATE_DOWN);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'ethernet.c';out=Path(td)/'ethernet';c.write_text(off)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('Ethernet-only link event behavior passes without net80211 lock state')
