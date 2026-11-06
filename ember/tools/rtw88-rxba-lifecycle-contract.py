#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the actual optional RX BA lifecycle with strict fake callouts."""
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
names = ('rtw88_net_rxba_cancel', 'rtw88_net_rxba_quiesce',
         'rtw88_net_rxba_attach', 'rtw88_net_rxba_detach')
functions = []
for name in names:
    match = re.search(r'^static void\n' + name + r'\(.*?^}', source, re.M | re.S)
    if not match:
        raise SystemExit('missing helper: ' + name)
    functions.append(match.group())
print(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define RTW88_CAP_RXBA 1
#define CALLOUT_MPSAFE 1
#define KASSERT(x) assert(x)
typedef struct { int initialized, stops, halts, destroys; } callout_t;
struct ifnet { void *if_softc; };
struct ieee80211com { struct ifnet *ic_ifp; const void *ic_rxba_ops; };
struct rtw88_net { struct ieee80211com ic; struct { unsigned features; } caps;
    void *lock; uint64_t rxba_token, rxba_due, rxba_deadline; callout_t rxba_callout; };
static int rtw88_net_rxba_ops;
static int mutex_owned(void *p) { (void)p; return 0; }
static void mutex_enter(void *p) { (void)p; }
static void mutex_exit(void *p) { (void)p; }
static void callout_init(callout_t *c,int f) { assert(f==1 && !c->initialized);c->initialized=1; }
static void callout_setfunc(callout_t *c,void (*f)(void *),void *p)
{ assert(c->initialized && f && p); }
static void callout_stop(callout_t *c) { assert(c->initialized);c->stops++; }
static void callout_halt(callout_t *c,const void *p) { assert(c->initialized && !p);c->halts++; }
static void callout_destroy(callout_t *c) { assert(c->initialized);c->initialized=0;c->destroys++; }
static void rtw88_net_rxba_timeout(void *p) { (void)p; }
''')
print('\n'.join(functions))
print(r'''
int main(void) {
    struct rtw88_net n={0}; struct ifnet ifp={.if_softc=&n}; n.ic.ic_ifp=&ifp;
    n.rxba_token=3;n.rxba_due=4;n.rxba_deadline=5;
    for(int i=0;i<2;i++) { rtw88_net_rxba_quiesce(&n);rtw88_net_rxba_detach(&n); }
    assert(n.rxba_token==3 && n.rxba_due==4 && n.rxba_deadline==5);
    assert(!n.rxba_callout.stops && !n.rxba_callout.halts && !n.rxba_callout.destroys);
    n.caps.features=RTW88_CAP_RXBA; rtw88_net_rxba_attach(&n);
    assert(n.ic.ic_rxba_ops==&rtw88_net_rxba_ops);
    rtw88_net_rxba_quiesce(&n);rtw88_net_rxba_quiesce(&n);
    assert(!n.rxba_token && !n.rxba_due && !n.rxba_deadline);
    assert(n.rxba_callout.stops==2 && n.rxba_callout.halts==2);
    rtw88_net_rxba_detach(&n);
    assert(!n.ic.ic_rxba_ops && n.rxba_callout.stops==3 &&
        n.rxba_callout.halts==3 && n.rxba_callout.destroys==1);
    return 0;
}
''')
