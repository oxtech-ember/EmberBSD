#!/usr/bin/env python3
"""Exercise Ethernet multicast reset and real net80211 ioctl reset paths."""
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
source = (Path(__file__).resolve().parents[1] /
          'espressif/esp_wifi_if.c').read_text()


def function(name):
    match = re.search(r'\nstatic (?:int|void)\n' + name +
                      r'\(.*?\n\{.*?\n\}', source, re.S)
    assert match, name
    return match.group()


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define ESPWIFI_NET80211 1
#define IEEE80211_ROAMING_MANUAL 1
#define SIOCADDMULTI 0x1001
#define SIOCDELMULTI 0x1002
#define SIOCS80211 0x1003
#define IFF_UP 0x01
#define IFF_RUNNING 0x02
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN,
 IEEE80211_S_AUTH, IEEE80211_S_ASSOC, IEEE80211_S_RUN };
typedef unsigned long u_long;
struct ieee80211com { enum ieee80211_state ic_state; int ic_roaming; };
struct espwifi_softc { struct ieee80211com sc_ic; bool sc_tx_ready;
 int sc_scan_lock; uint64_t sc_join_generation,sc_join_active;
 bool sc_joined,sc_link; unsigned sc_port_authorized; };
struct ifnet { void *if_softc; unsigned if_flags; };
static struct espwifi_softc sc;
static int ioctl_error, ioctl_calls, state_calls, spl_depth;
static unsigned states[4], membership_logs, restart_logs, stop_logs;
static int splnet(void) { spl_depth++; return spl_depth; }
static void splx(int s) { assert(s==spl_depth); spl_depth--; }
static int ieee80211_ioctl(struct ieee80211com *ic,unsigned long cmd,void *data) {
 (void)ic;(void)data;ioctl_calls++;
 if (cmd==SIOCADDMULTI || cmd==SIOCDELMULTI) return ioctl_error;
 return ioctl_error;
}
static int ieee80211_new_state(struct ieee80211com *ic,
 enum ieee80211_state state,int arg) {
 (void)arg;state_calls++;if ((unsigned)state<4) states[state]++;
 ic->ic_state=state;return 0;
}
static void mutex_spin_enter(int *p) { (void)p; }
static void mutex_spin_exit(int *p) { (void)p; }
static void espwifi_os_log(const char *fmt,...) {
 char msg[256]; va_list ap;
 va_start(ap,fmt); (void)vsnprintf(msg,sizeof(msg),fmt,ap); va_end(ap);
 if (strstr(msg,"action=membership-update")!=NULL) membership_logs++;
 if (strstr(msg,"action=restart")!=NULL) restart_logs++;
 if (strstr(msg,"action=if-stop")!=NULL) stop_logs++;
}
''' + function('espwifi_if_init') + function('espwifi_if_stop') + \
    function('espwifi_if_ioctl') + r'''
static void reset_state(void) {
 memset(&sc,0,sizeof(sc));sc.sc_tx_ready=true;
 sc.sc_ic.ic_state=IEEE80211_S_RUN;
 sc.sc_join_generation=17;sc.sc_join_active=17;
 sc.sc_joined=true;sc.sc_link=true;sc.sc_port_authorized=1;
 ioctl_error=ENETRESET;ioctl_calls=state_calls=0;
 memset(states,0,sizeof(states));
}
int main(void) {
 struct ifnet ifp={.if_softc=&sc,.if_flags=IFF_UP|IFF_RUNNING};
 int error;

 reset_state();
 error=espwifi_if_ioctl(&ifp,SIOCADDMULTI,NULL);
 assert(error==0 && ioctl_calls==1 && state_calls==0);
 assert(sc.sc_ic.ic_state==IEEE80211_S_RUN && sc.sc_join_generation==17 &&
     sc.sc_join_active==17 && sc.sc_joined && sc.sc_link &&
     sc.sc_port_authorized==1);
 assert(membership_logs==1 && restart_logs==0);

 reset_state();
 error=espwifi_if_ioctl(&ifp,SIOCDELMULTI,NULL);
 assert(error==0 && state_calls==0 && sc.sc_join_active==17 &&
     sc.sc_joined && sc.sc_link && sc.sc_port_authorized==1 &&
     membership_logs==2 && restart_logs==0);

 reset_state();ioctl_error=0;
 error=espwifi_if_ioctl(&ifp,SIOCADDMULTI,NULL);
 assert(error==0 && state_calls==0 && membership_logs==2 &&
     restart_logs==0 && sc.sc_join_active==17 && sc.sc_joined && sc.sc_link);

 /* Model a BSD configuration ioctl whose handler requests a reset. */
 reset_state();
 error=espwifi_if_ioctl(&ifp,SIOCS80211,NULL);
 assert(error==0 && state_calls==2 &&
     states[IEEE80211_S_INIT]==1 && states[IEEE80211_S_SCAN]==1 &&
     restart_logs==1 && sc.sc_join_generation==17);

 reset_state();ioctl_error=EINVAL;
 error=espwifi_if_ioctl(&ifp,SIOCADDMULTI,NULL);
 assert(error==EINVAL && state_calls==0 && membership_logs==2 &&
     restart_logs==1 && sc.sc_join_active==17 && sc.sc_joined && sc.sc_link);

 reset_state();
 ifp.if_flags=IFF_UP|IFF_RUNNING;
 espwifi_if_stop(&ifp,1);
 assert(state_calls==1 && states[IEEE80211_S_INIT]==1 &&
     (ifp.if_flags&IFF_RUNNING)==0 && stop_logs==1);
 assert(spl_depth==0);
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='espwifi-ioctl-reset-') as td:
    td = Path(td)
    c = td / 'ioctl_reset.c'
    out = td / 'ioctl_reset'
    c.write_text(code)
    subprocess.run([a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function',
                    '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(c), '-o', str(out)],
                   check=True)
    subprocess.run([str(out)], check=True)
print('multicast ENETRESET preserves association; other resets reinitialize')
