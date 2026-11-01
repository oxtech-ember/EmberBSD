#!/usr/bin/env python3
"""Exercise the actual full-MAC EAPOL-to-Ethernet ingress boundary."""
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
f=re.search(r'\nint\nespwifi_if_rx_eapol\(.*?\)\n\{.*?\n\}',s,re.S)
closed=re.search(r'\nstatic void\nespwifi_if_fail_closed\(.*?\)\n\{.*?\n\}',s,re.S)
assert f
assert closed
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ETHER_ADDR_LEN 6
#define ETHER_HDR_LEN 14
#define ETHERMTU 1500
#define ETHERTYPE_PAE 0x888e
#define IFF_RUNNING 1
#define __UNCONST(p) ((void *)(uintptr_t)(p))
#define ESPWIFI_NET80211_WPA2 1
struct mbuf { int x; };
struct ifnet { int if_flags; void *if_softc; };
struct espwifi_softc { struct { struct ifnet ec_if; } sc_ec; bool sc_attached;
 uint8_t sc_mac[6]; void *sc_ipq; int sc_scan_lock; uint64_t sc_join_active;
 bool sc_joined,sc_join_stopped,sc_link,sc_radio_ready,sc_ptk_installed;
 bool sc_join_pending,sc_join_disconnect_pending,sc_join_rescan_pending;
 uint8_t sc_key_slots,sc_group_key_slots;uint64_t sc_key_generation,sc_join_generation;
 void *sc_scan_ctl_sem;unsigned sc_port_authorized; };
static struct espwifi_softc espwifi_sc; static struct mbuf mb;
static unsigned char copied[1514];static int copied_len,fail,queued,wiped;
static struct mbuf *m_devget(void *p,int n,int o,struct ifnet *i) {
 (void)o;(void)i;if(fail)return NULL;copied_len=n;memcpy(copied,p,n);return &mb; }
static void explicit_memset(void *p,int v,size_t n) { memset(p,v,n);wiped++; }
static int splnet(void) { return 0; }static void splx(int s) { (void)s; }
static void if_percpuq_enqueue(void *q,struct mbuf *m) { (void)q;assert(m==&mb);queued++; }
static int locked;
static void mutex_spin_enter(int *p) { (void)p;assert(!locked);locked=1; }
static void mutex_spin_exit(int *p) { (void)p;assert(locked);locked=0; }
static unsigned atomic_load_acquire(unsigned *p) { return *p; }
static void atomic_store_release(unsigned *p,unsigned v) { *p=v; }
static void espwifi_os_sem_give(void *p) { (void)p; }
static void if_link_state_change(struct ifnet *p,int s) { (void)p;(void)s; }
#define LINK_STATE_DOWN 0
''' + closed.group() + f.group() + r'''
int main(void) {
 const uint8_t src[6]={1,2,3,4,5,6}, msg[4]={2,3,0,0};
 assert(espwifi_if_rx_eapol(src,msg,4)==-1 && !queued);
 espwifi_sc.sc_attached=true;espwifi_sc.sc_ec.ec_if.if_flags=IFF_RUNNING;
 espwifi_sc.sc_ec.ec_if.if_softc=&espwifi_sc;
 memset(espwifi_sc.sc_mac,0xaa,6);
 espwifi_sc.sc_join_active=1;
 assert(espwifi_if_rx_eapol(src,msg,4)==1 && !queued);
 espwifi_sc.sc_joined=true;
 assert(espwifi_if_rx_eapol(NULL,msg,4)==-1);
 assert(espwifi_if_rx_eapol(src,NULL,4)==-1);
 assert(espwifi_if_rx_eapol(src,msg,0)==-1);
 assert(espwifi_if_rx_eapol(src,msg,1501)==-1);
 uint8_t other[4]={2,0,0,0}, truncated[4]={2,3,0,1};
 assert(espwifi_if_rx_eapol(src,other,4)==1 && !queued);
 espwifi_sc.sc_radio_ready=true;espwifi_sc.sc_link=true;
 espwifi_sc.sc_port_authorized=1;
 assert(espwifi_if_rx_eapol(src,truncated,4)==-1 && !queued);
 assert(!espwifi_sc.sc_port_authorized && !espwifi_sc.sc_link &&
  espwifi_sc.sc_join_disconnect_pending);
 espwifi_sc.sc_join_active=2;espwifi_sc.sc_joined=true;
 espwifi_sc.sc_link=true;espwifi_sc.sc_port_authorized=1;
 fail=1;assert(espwifi_if_rx_eapol(src,msg,4)==-1 && wiped==1 && !queued);
 assert(!espwifi_sc.sc_port_authorized && !espwifi_sc.sc_link);
 espwifi_sc.sc_join_active=3;espwifi_sc.sc_joined=true;
 fail=0;assert(espwifi_if_rx_eapol(src,msg,4)==1);
 assert(queued==1 && wiped==2 && copied_len==18 && copied[0]==0xaa);
 assert(!memcmp(copied+6,src,6) && copied[12]==0x88 && copied[13]==0x8e);
 assert(!memcmp(copied+14,msg,4));return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'eapol.c';out=Path(td)/'eapol';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual EAPOL Ethernet ingress bounds and failure paths passed')
