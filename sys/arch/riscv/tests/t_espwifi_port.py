#!/usr/bin/env python3
"""Exercise real controlled-port filtering at the Ethernet ingress."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if not __debug__:
    sys.exit('this test requires Python assertions enabled')

p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
s=(Path(__file__).resolve().parents[1]/'espressif/esp_wifi_if.c').read_text()
attach=s[s.index('espwifi_if_attach(void)'):]
assert attach.index('espwifi_os_task_create(espwifi_if_txthread') < attach.index('if_register(ifp)')
def fn(name,result):
 m=re.search(r'\n(?:static )?'+result+r'\n'+name+r'\(.*?\)\n\{.*?\n\}',s,re.S)
 assert m,name
 return m.group()
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define ESPWIFI_NET80211 1
#define ETHER_HDR_LEN 14
#define ETHERTYPE_PAE 0x888e
#define IFF_RUNNING 1
#define if_ierrors 0
#define atomic_load_acquire(p) (*(p))
struct mbuf { int dummy; };struct ifnet { int if_flags; };
struct espwifi_softc { struct { struct ifnet ec_if; } sc_ec; bool sc_attached;
 unsigned sc_port_authorized; void *sc_ipq; };
static struct espwifi_softc espwifi_sc;static int queued,errors,allocfail;static struct mbuf mb;
static struct mbuf *m_devget(void *p,unsigned n,int off,struct ifnet *i) {
 (void)p;(void)n;(void)off;(void)i;return allocfail?NULL:&mb; }
static void if_statinc(struct ifnet *p,int field) { (void)p;(void)field;errors++; }
static int splnet(void) { return 0; }static void splx(int s) { (void)s; }
static void if_percpuq_enqueue(void *q,struct mbuf *m) { (void)q;assert(m==&mb);queued++; }
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
''' + fn('espwifi_if_is_eapol','bool') + fn('espwifi_if_input','void') + r'''
int main(void) {
 uint8_t packet[14]={0};espwifi_sc.sc_attached=true;espwifi_sc.sc_ec.ec_if.if_flags=IFF_RUNNING;
 espwifi_if_input(packet,sizeof(packet));assert(!queued);
 packet[12]=0x88;packet[13]=0x8e;espwifi_if_input(packet,sizeof(packet));assert(queued==1);
 espwifi_if_input(packet,13);assert(queued==1);
 espwifi_sc.sc_port_authorized=1;packet[12]=0x08;packet[13]=0x00;
 espwifi_if_input(packet,sizeof(packet));assert(queued==2);
 allocfail=1;espwifi_if_input(packet,sizeof(packet));assert(errors==1 && queued==2);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'port.c';out=Path(td)/'port';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual controlled-port ingress permits only EAPOL before authorization')

code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
typedef unsigned int u_int;
#define ESPWIFI_NET80211 1
#define ETHER_HDR_LEN 14
#define ETHERTYPE_PAE 0x888e
#define ESPWIFI_MTU_FRAME 1600
#define ESPWIFI_WAIT_FOREVER 0
#define BPF_D_OUT 0
#define if_oerrors 0
#define if_opackets 1
#define atomic_load_acquire(p) (*(p))
struct mbuf { struct { u_int len; } m_pkthdr; uint8_t data[32]; };
struct ifnet { int if_snd; };
struct espwifi_softc { struct { struct ifnet ec_if; } sc_ec; void *sc_txsem;
 bool sc_link; unsigned sc_port_authorized; uint8_t *sc_txbuf; };
static struct mbuf packets[3]; static int qpos,qsize, sent, freed, semcalls;
static jmp_buf done;
static void espwifi_os_sem_take(void *s,int n) { (void)s;(void)n;if(semcalls++)longjmp(done,1); }
static struct mbuf *dequeue(void) { return qpos<qsize ? &packets[qpos++] : NULL; }
#define IFQ_DEQUEUE(q,m) ((void)(q),(m)=dequeue())
static int splnet(void) { return 0; } static void splx(int s) { (void)s; }
static void if_statinc(struct ifnet *ifp,int n) { (void)ifp;(void)n; }
static void m_freem(struct mbuf *m) { (void)m;freed++; }
static void m_copydata(struct mbuf *m,unsigned off,unsigned n,void *p) { assert(off+n<=m->m_pkthdr.len);memcpy(p,m->data+off,n); }
static void bpf_mtap(struct ifnet *ifp,struct mbuf *m,int dir) { (void)ifp;(void)m;(void)dir; }
static int espwifi_tx(void *p,unsigned n) { (void)p;assert(n==14);sent++;return 0; }
''' + fn('espwifi_if_is_eapol','bool') + fn('espwifi_if_txthread','void') + r'''
int main(void) {
 uint8_t buf[1600]; struct espwifi_softc sc={0};sc.sc_link=true;sc.sc_txbuf=buf;
 packets[0].m_pkthdr.len=14;packets[0].data[12]=0x08;
 packets[1].m_pkthdr.len=14;packets[1].data[12]=0x88;packets[1].data[13]=0x8e;
 packets[2].m_pkthdr.len=13;qsize=3;
 if (!setjmp(done)) espwifi_if_txthread(&sc);
 assert(sent==1 && freed==3);
 sc.sc_port_authorized=1;semcalls=0;qpos=0;qsize=1;
 if (!setjmp(done)) espwifi_if_txthread(&sc);
 assert(sent==2 && freed==4);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'tx.c';out=Path(td)/'tx';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual controlled-port transmit permits only EAPOL before authorization')

code=r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#define ESPWIFI_NET80211 1
#define IFF_RUNNING 1
#define IEEE80211_ROAMING_MANUAL 2
#define IEEE80211_S_SCAN 1
struct ieee80211com { int ic_roaming; };
struct espwifi_softc { bool sc_tx_ready; struct ieee80211com sc_ic; };
struct ifnet { void *if_softc; int if_flags; };
static int scans,fail;
static int ieee80211_new_state(struct ieee80211com *ic,int state,int arg) {
 (void)ic;assert(state==IEEE80211_S_SCAN && arg==-1);scans++;return fail?EIO:0;
}
''' + fn('espwifi_if_init','int') + r'''
int main(void) {
 struct espwifi_softc sc={0};struct ifnet ifp={&sc,0};
 assert(espwifi_if_init(&ifp)==ENXIO && !ifp.if_flags && !scans);
 sc.sc_tx_ready=true;fail=1;assert(espwifi_if_init(&ifp)==EIO && !ifp.if_flags && scans==1);
 fail=0;assert(!espwifi_if_init(&ifp) && (ifp.if_flags & IFF_RUNNING) && scans==2);
 sc.sc_ic.ic_roaming=IEEE80211_ROAMING_MANUAL;assert(!espwifi_if_init(&ifp) && scans==2);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'init.c';out=Path(td)/'init';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual interface init rejects a missing transmit worker')

m=re.search(r'\tif \(!espwifi_os_task_create\(espwifi_if_txthread, "tx".*?\n#endif',s,re.S)
assert m
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#define ESPWIFI_NET80211 1
struct espwifi_softc { bool sc_tx_ready,sc_scan_shutdown,sc_scan_enabled;
 int sc_scan_lock;void *sc_scan_sem; };
static int fail,unblocks,lockheld;
static void espwifi_if_txthread(void *v) { (void)v; }
static void espwifi_if_scanthread(void *v) { (void)v; }
static void espwifi_if_scan_control(void *v) { (void)v; }
static int espwifi_os_task_create(void (*f)(void *),const char *name,int n,void *sc,void **handle) {
 (void)f;(void)n;(void)sc;(void)handle;
 return !((fail==1 && !strcmp(name,"tx")) || (fail==2 && !strcmp(name,"scan")) ||
          (fail==3 && !strcmp(name,"scanctl")));
}
static void mutex_spin_enter(int *p) { (void)p;assert(!lockheld);lockheld=1; }
static void mutex_spin_exit(int *p) { (void)p;assert(lockheld);lockheld=0; }
static void espwifi_os_sem_give(void *p) { (void)p;assert(!lockheld);unblocks++; }
#define printf(...) ((void)0)
static void start(struct espwifi_softc *sc) { void *handle;
''' + m.group() + r'''
}
int main(void) {
 struct espwifi_softc sc={0};start(&sc);assert(sc.sc_tx_ready && sc.sc_scan_enabled && !unblocks);
 sc=(struct espwifi_softc){0};fail=1;start(&sc);assert(!sc.sc_tx_ready && sc.sc_scan_enabled);
 sc=(struct espwifi_softc){0};fail=2;start(&sc);assert(sc.sc_tx_ready && !sc.sc_scan_enabled && !unblocks);
 sc=(struct espwifi_softc){0};fail=3;start(&sc);assert(sc.sc_tx_ready && !sc.sc_scan_enabled && sc.sc_scan_shutdown && unblocks==1);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'workers.c';out=Path(td)/'workers';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual worker creation handles failures and wakes the partial scan worker')

code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
typedef unsigned int u_int;
#define ETHER_ADDR_LEN 6
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_KEYIX_NONE 65535
#define IEEE80211_WEP_NKID 4
#define IEEE80211_KEY_GROUP 4
#define IEEE80211_KEY_XMIT 1
#define IEEE80211_CIPHER_AES_CCM 3
#define NBBY 8
#define LINK_STATE_DOWN 0
struct cipher { unsigned ic_cipher; };
struct ieee80211_key { const struct cipher *wk_cipher; unsigned wk_keylen;
 unsigned wk_keyix,wk_flags; uint64_t wk_keyrsc; uint8_t wk_key[32]; };
struct ifnet { void *if_softc; int if_flags; };
struct espwifi_ec { struct ifnet ec_if; };
struct ieee80211com { struct ifnet *ic_ifp; };
struct espwifi_softc { struct ieee80211com sc_ic; struct espwifi_ec sc_ec; unsigned sc_port_authorized;
 int sc_scan_lock;void *sc_scan_ctl_sem;bool sc_ptk_installed,sc_link,sc_joined,
 sc_radio_ready,sc_sta_started,sc_join_stopped,sc_join_pending,
 sc_join_disconnect_pending,sc_join_rescan_pending,sc_attached;
 int sc_key_lock,sc_key_cv;bool sc_key_pending,sc_key_running,sc_key_done,
 sc_key_abandoned,sc_scan_enabled;
 int sc_key_error;uint64_t sc_key_request_generation;uint8_t sc_key_request_mac[6],
 sc_key_request_seq[6],sc_key_request_data[16];unsigned sc_key_request_index;
 int sc_key_request_group,sc_key_request_xmit;
 uint64_t sc_join_generation,sc_join_active,sc_key_generation;
 uint8_t sc_key_slots,sc_group_key_slots; };
static struct espwifi_softc sc,espwifi_sc;static int calls,result,held,signals;static unsigned seq_seen;
static bool mutate_generation;
static bool teardown_on_unlock;
static bool stale_before_worker, force_timeout, delay_success, timeout_completion;
static bool wait_only,reconnect_during_wait,reconnect_on_timeout;
static int key_held,worker_context;
static u_int tick_clock, wake_elapsed;
static void espwifi_if_key_control(struct espwifi_softc *);
static void mutex_enter(int *p) { (void)p;assert(!key_held);key_held=1; }
static void mutex_exit(int *p) { (void)p;assert(key_held);key_held=0; }
static void cv_broadcast(int *p) { (void)p; }
static u_int getticks(void) { return tick_clock++; }
static int mstohz(int ms) { return ms; }
#define MAX(a,b) ((a)>(b)?(a):(b))
static int cv_timedwait(int *cv,int *p,int ticks) { (void)cv;(void)p;
 assert(key_held);
 if (reconnect_on_timeout) {
  reconnect_on_timeout=false;sc.sc_key_done=false;
  sc.sc_join_active=22;sc.sc_join_generation=22;sc.sc_joined=true;
  sc.sc_link=true;sc.sc_radio_ready=true;sc.sc_sta_started=true;
  sc.sc_join_stopped=false;sc.sc_join_disconnect_pending=false;
  sc.sc_join_rescan_pending=false;return EWOULDBLOCK;
 }
 if (force_timeout && !timeout_completion) return EWOULDBLOCK;
 if (reconnect_during_wait) {
  reconnect_during_wait=false;sc.sc_key_done=false;
  sc.sc_join_active=20;sc.sc_join_generation=20;sc.sc_joined=true;
  sc.sc_link=true;sc.sc_radio_ready=true;sc.sc_sta_started=true;
  sc.sc_join_stopped=false;return 0;
 }
 if (wait_only) { tick_clock+=wake_elapsed;return 0; }
 key_held=0;worker_context=1;espwifi_if_key_control(&sc);worker_context=0;
 key_held=1;if (delay_success) tick_clock+=ticks;
 return timeout_completion ? EWOULDBLOCK : 0; }
static void atomic_store_release(unsigned *p,unsigned v) { *p=v; }
static void mutex_spin_enter(int *p) { (void)p;assert(!held);held=1; }
static void mutex_spin_exit(int *p) { (void)p;assert(held);held=0;
 if (teardown_on_unlock) { sc.sc_link=false;sc.sc_ptk_installed=false;
  atomic_store_release(&sc.sc_port_authorized,0);teardown_on_unlock=false; } }
static void espwifi_os_sem_give(void *p) { (void)p;assert(!held);signals++;
 if (stale_before_worker) {
  sc.sc_join_active++;sc.sc_join_generation++;
  sc.sc_joined=true;sc.sc_link=true;sc.sc_radio_ready=true;
  sc.sc_sta_started=true;sc.sc_join_stopped=false;
  sc.sc_join_disconnect_pending=false;sc.sc_join_rescan_pending=false;
  sc.sc_ptk_installed=false;sc.sc_key_slots=0;sc.sc_group_key_slots=0;
  sc.sc_key_generation=0;
  atomic_store_release(&sc.sc_port_authorized,0);
  stale_before_worker=false;
 } }
static void if_link_state_change(struct ifnet *p,int s) { (void)p;(void)s; }
static int espwifi_ext_set_key(const unsigned char *mac,unsigned index,int group,
 int tx,int modify,const unsigned char *seq,unsigned nseq,const unsigned char *key,unsigned nkey) {
	assert(worker_context && mac[0]==2 && index==0 && group<=1 && tx<=1 && modify==0);
	assert(nseq==6 && nkey==16 && key[0]==9);calls++;seq_seen=seq[0];
	if (mutate_generation) {
		sc.sc_join_active++;sc.sc_join_generation++;
		sc.sc_joined=true;sc.sc_link=true;sc.sc_radio_ready=true;
		sc.sc_sta_started=true;sc.sc_join_stopped=false;
  sc.sc_join_disconnect_pending=false;sc.sc_join_rescan_pending=false;
  sc.sc_ptk_installed=false;sc.sc_key_slots=0;sc.sc_group_key_slots=0;
  sc.sc_key_generation=0;
		atomic_store_release(&sc.sc_port_authorized,0);
	}
	return result; }
static void explicit_memset(void *p,int c,size_t n) { memset(p,c,n); }
''' + fn('espwifi_if_set_port','int') + fn('espwifi_if_fail_closed_generation','bool') + fn('espwifi_if_fail_closed','void') + fn('espwifi_if_key_request_clear','void') + fn('espwifi_if_key_wait','bool') + fn('espwifi_if_key_control','void') + fn('espwifi_if_key_set','int') + fn('espwifi_if_key_delete','int') + fn('espwifi_if_radio_stopped','void') + r'''
int main(void) {
 struct ifnet ifp={&sc};struct ieee80211com ic={&ifp};struct cipher c={3};
 struct ieee80211_key key={.wk_cipher=&c,.wk_keylen=16,.wk_keyix=0,.wk_flags=1,
  .wk_keyrsc=0x0807060504,.wk_key={9}};uint8_t mac[6]={2};sc.sc_ic=ic;
 u_int left,start;
 wait_only=true;tick_clock=100;left=10;start=tick_clock;wake_elapsed=10;
 mutex_enter(&sc.sc_key_lock);
 assert(!espwifi_if_key_wait(&sc,&left,&start) && left==0);
 mutex_exit(&sc.sc_key_lock);
 tick_clock=100;left=10;start=tick_clock;wake_elapsed=11;
 mutex_enter(&sc.sc_key_lock);
 assert(!espwifi_if_key_wait(&sc,&left,&start) && left==0);
 mutex_exit(&sc.sc_key_lock);
 tick_clock=UINT32_MAX-1;left=4;start=tick_clock;wake_elapsed=2;
 mutex_enter(&sc.sc_key_lock);
 assert(espwifi_if_key_wait(&sc,&left,&start) && left==2);
 mutex_exit(&sc.sc_key_lock);
 wait_only=false;tick_clock=0;wake_elapsed=0;
 assert(espwifi_if_set_port(&ic,true)==EACCES && !sc.sc_port_authorized);
	sc.sc_link=true;sc.sc_join_active=1;sc.sc_join_generation=1;
	sc.sc_joined=true;sc.sc_radio_ready=true;sc.sc_sta_started=true;
	sc.sc_scan_enabled=true;
	assert(espwifi_if_set_port(&ic,true)==EACCES);
 result=0;assert(espwifi_if_key_set(&ic,&key,mac)==1 && sc.sc_ptk_installed);
 assert(calls==1 && seq_seen==4 && !sc.sc_port_authorized);
	key.wk_flags=IEEE80211_KEY_GROUP;delay_success=true;
	assert(espwifi_if_key_set(&ic,&key,mac)==1);
	delay_success=false;
	assert(calls==2 && (sc.sc_group_key_slots & 1));
	timeout_completion=true;force_timeout=true;
	assert(espwifi_if_key_set(&ic,&key,mac)==1);
	timeout_completion=false;force_timeout=false;
	assert(calls==3 && !sc.sc_key_done && !sc.sc_key_request_data[0]);
	key.wk_flags=IEEE80211_KEY_XMIT;
	teardown_on_unlock=true;
	assert(espwifi_if_set_port(&ic,true)==0);
	assert(!sc.sc_link && !sc.sc_ptk_installed && !sc.sc_port_authorized);
	sc.sc_link=true;sc.sc_ptk_installed=true;
 assert(espwifi_if_set_port(&ic,true)==0 && sc.sc_port_authorized);
	sc.sc_ptk_installed=false;
	sc.sc_join_active=7;sc.sc_joined=true;sc.sc_join_generation=7;
	mutate_generation=true;
	assert(espwifi_if_key_set(&ic,&key,mac)==0);
	mutate_generation=false;
	assert(!sc.sc_ptk_installed && !sc.sc_port_authorized &&
	    sc.sc_join_active==8 && sc.sc_join_generation==8 && sc.sc_joined &&
	    sc.sc_link && !sc.sc_join_disconnect_pending);
	sc.sc_join_active=8;sc.sc_joined=true;sc.sc_link=true;
	key.wk_flags=IEEE80211_KEY_GROUP;assert(espwifi_if_key_set(&ic,&key,mac)==1);
	assert(!sc.sc_ptk_installed && !sc.sc_port_authorized);
	key.wk_flags=0;assert(espwifi_if_key_delete(&ic,&key)==1);
	assert(sc.sc_link && !sc.sc_ptk_installed);
	key.wk_flags=IEEE80211_KEY_GROUP;
	assert(espwifi_if_key_delete(&ic,&key)==0);
	assert(!sc.sc_link && !sc.sc_ptk_installed && !sc.sc_port_authorized &&
	    sc.sc_join_disconnect_pending);
	sc.sc_join_active=9;sc.sc_join_generation=9;sc.sc_joined=true;sc.sc_link=true;
	key.wk_flags=0;result=1;assert(espwifi_if_key_set(&ic,&key,mac)==0);
	assert(!sc.sc_ptk_installed && !sc.sc_port_authorized && !sc.sc_link &&
	    sc.sc_join_disconnect_pending);
	result=0;sc.sc_join_active=10;sc.sc_join_generation=10;sc.sc_joined=true;sc.sc_link=true;
	assert(espwifi_if_key_set(&ic,&key,mac)==1 && sc.sc_ptk_installed);
	assert(espwifi_if_set_port(&ic,true)==0 && sc.sc_port_authorized);
	key.wk_keyix=IEEE80211_KEYIX_NONE;
	assert(espwifi_if_key_delete(&ic,&key)==1);
	assert(sc.sc_link && sc.sc_ptk_installed && sc.sc_port_authorized);
	key.wk_keyix=2;
	assert(espwifi_if_key_delete(&ic,&key)==1);
	assert(sc.sc_link && sc.sc_ptk_installed && sc.sc_port_authorized);
	key.wk_keyix=0;
	assert(espwifi_if_key_delete(&ic,&key)==0);
 assert(!sc.sc_ptk_installed && !sc.sc_port_authorized &&
	  !sc.sc_link && sc.sc_join_disconnect_pending && sc.sc_join_rescan_pending);
	key.wk_keyix=4;assert(espwifi_if_key_delete(&ic,&key)==0);
	sc.sc_join_active=11;sc.sc_join_generation=11;sc.sc_joined=true;
	key.wk_keyix=0;
	unsigned previous_calls=(unsigned)calls;
	stale_before_worker=true;
	assert(espwifi_if_key_set(&ic,&key,mac)==0);
	assert((unsigned)calls==previous_calls && !sc.sc_ptk_installed &&
	    !sc.sc_port_authorized && sc.sc_join_active==12 && sc.sc_joined &&
	    sc.sc_link && !sc.sc_join_disconnect_pending);
	sc.sc_join_active=12;sc.sc_join_generation=12;sc.sc_joined=true;
	key.wk_keyix=0;force_timeout=true;previous_calls=(unsigned)calls;
	assert(espwifi_if_key_set(&ic,&key,mac)==0);
	force_timeout=false;
	assert((unsigned)calls==previous_calls && !sc.sc_key_pending &&
	    !sc.sc_key_request_data[0] && !sc.sc_port_authorized &&
	    sc.sc_join_disconnect_pending);
	/* A new join must survive completion of a waiter from an old epoch. */
	sc.sc_join_active=19;sc.sc_join_generation=19;sc.sc_joined=true;
	sc.sc_link=true;sc.sc_radio_ready=true;sc.sc_sta_started=true;
	sc.sc_join_stopped=false;sc.sc_key_done=true;
	reconnect_during_wait=true;previous_calls=(unsigned)calls;
	assert(espwifi_if_key_set(&ic,&key,mac)==0);
	assert((unsigned)calls==previous_calls && sc.sc_join_active==20 &&
	    sc.sc_joined && sc.sc_link && sc.sc_radio_ready &&
	    !sc.sc_key_pending && !sc.sc_key_request_data[0]);
	/* A timeout from an old key wait cannot close a replacement generation. */
	sc.sc_join_active=21;sc.sc_join_generation=21;sc.sc_joined=true;
	sc.sc_link=true;sc.sc_radio_ready=true;sc.sc_sta_started=true;
	sc.sc_join_stopped=false;sc.sc_key_done=true;
	force_timeout=true;reconnect_on_timeout=true;
	assert(espwifi_if_key_set(&ic,&key,mac)==0);
	force_timeout=false;
	assert(sc.sc_join_active==22 && sc.sc_join_generation==22 &&
	    sc.sc_joined && sc.sc_link && sc.sc_radio_ready &&
	    !sc.sc_join_disconnect_pending);
	/* STOP invalidates an already queued key; its worker drains without install. */
	sc.sc_attached=true;sc.sc_join_active=23;sc.sc_join_generation=23;
	sc.sc_joined=true;sc.sc_link=true;sc.sc_radio_ready=true;
	sc.sc_sta_started=true;sc.sc_join_stopped=false;
	sc.sc_key_request_generation=23;sc.sc_key_request_index=0;
	sc.sc_key_request_mac[0]=2;sc.sc_key_request_seq[0]=8;
	sc.sc_key_request_data[0]=9;sc.sc_key_pending=true;
	previous_calls=(unsigned)calls;
	espwifi_sc=sc;
	espwifi_if_radio_stopped();
	sc=espwifi_sc;
	assert(sc.sc_join_stopped && sc.sc_join_active==0 &&
	    !sc.sc_radio_ready && !sc.sc_key_slots && !sc.sc_ptk_installed);
	worker_context=1;espwifi_if_key_control(&sc);worker_context=0;
	assert((unsigned)calls==previous_calls && !sc.sc_key_pending &&
	    sc.sc_key_done && sc.sc_key_error==ECANCELED);
	espwifi_if_key_request_clear(&sc);sc.sc_key_done=false;
	assert(!sc.sc_key_request_data[0] && !sc.sc_key_request_seq[0] &&
	    !sc.sc_key_request_mac[0]);
	sc.sc_join_active=13;sc.sc_join_generation=13;sc.sc_joined=true;
	sc.sc_key_request_generation=13;sc.sc_key_request_index=0;
	sc.sc_key_request_mac[0]=2;sc.sc_key_request_seq[0]=8;
	sc.sc_key_request_data[0]=9;sc.sc_key_pending=true;
	sc.sc_key_abandoned=true;
	worker_context=1;
	espwifi_if_key_control(&sc);
	worker_context=0;
	assert(!sc.sc_key_running && !sc.sc_key_abandoned &&
	    !sc.sc_key_request_data[0] && !sc.sc_key_done);
	key.wk_keyix=4;key.wk_cipher=&c;key.wk_keylen=16;
	assert(espwifi_if_key_set(&ic,&key,mac)==0);
 key.wk_cipher=NULL;assert(espwifi_if_key_set(&ic,&key,mac)==0);
 key.wk_cipher=&c;key.wk_keylen=15;assert(espwifi_if_key_set(&ic,&key,mac)==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'keyport.c';out=Path(td)/'keyport';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
assert 'IEEE80211_C_AES_CCM | IEEE80211_C_WPA2' in s
assert 'ic_sta_set_port = espwifi_if_set_port' in s
print('actual WPA2 key install and controlled-port gate passed; key deletion fails closed')
