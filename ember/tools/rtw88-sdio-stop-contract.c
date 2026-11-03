/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* Actual frontend stop, deterministic model; no kernel concurrency claim. */
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
enum { RTWSDIO_ATTACHED, RTWSDIO_LOADING, RTWSDIO_NET_READY,
 RTWSDIO_BOOT_READY, RTWSDIO_FAILED, RTWSDIO_STOPPING, RTWSDIO_DETACHED };
struct rtw88_softc { void *sc_executor, *sc_net; };
struct rtwsdio_softc { struct rtw88_softc sc_core; int sc_lock, sc_state;
 bool sc_stopping, sc_retry_armed, sc_retry_due, sc_core_attached;
 void *sc_dev, *sc_retry; };
#define RTW88_EXEC_STOP 8U
static int seq, drain_order, net_order, core_order, destroy_order;
static int stop_error;
static int rtw88_executor_owned(struct rtw88_softc *s) { (void)s; return 0; }
static void mutex_enter(int *p) { assert(!*p); *p=1; }
static void mutex_exit(int *p) { assert(*p); *p=0; }
static int config_mountroot_cancel(void *p) { (void)p; return 0; }
static void callout_halt(void *p, void *q) { (void)p; (void)q; }
static int rtw88_executor_drain(struct rtw88_softc *s) { (void)s; drain_order=++seq; return 0; }
static int rtw88_net_detach(void *p) { struct rtwsdio_softc *s=p; net_order=++seq; if (!stop_error) s->sc_core.sc_executor=NULL; return stop_error; }
static void rtw88_executor_schedule(struct rtw88_softc *s, unsigned r) { (void)s; (void)r; }
static int rtw88_core_detach(struct rtw88_softc *s) { (void)s; core_order=++seq; return 0; }
static int rtw88_executor_destroy(struct rtw88_softc *s) { destroy_order=++seq; s->sc_executor=NULL; return 0; }
#include "stop.inc"
int main(void) {
 struct rtwsdio_softc s = { .sc_state=RTWSDIO_LOADING, .sc_retry_armed=true };
 assert(rtwsdio_stop(&s)==EBUSY);
 assert(!s.sc_stopping && s.sc_retry_armed && s.sc_state==RTWSDIO_LOADING && seq==0);
 s.sc_state=RTWSDIO_NET_READY; s.sc_core.sc_executor=&s; s.sc_core.sc_net=&s; s.sc_core_attached=true;
 stop_error=EIO; assert(rtwsdio_stop(&s)==EIO);
 assert(net_order==1 && drain_order==0 && core_order==0 && destroy_order==0);
 assert(s.sc_core.sc_executor && s.sc_core_attached);
 seq=net_order=drain_order=core_order=destroy_order=0;stop_error=0;
 assert(rtwsdio_stop(&s)==0);
 assert(net_order==1 && core_order==2 && drain_order==0 && destroy_order==0);
 s=(struct rtwsdio_softc){ .sc_state=RTWSDIO_BOOT_READY, .sc_core_attached=true };
 s.sc_core.sc_executor=&s;seq=net_order=drain_order=core_order=destroy_order=0;
 assert(rtwsdio_stop(&s)==0);
 assert(drain_order==1 && core_order==2 && destroy_order==3);
 return 0;
}
