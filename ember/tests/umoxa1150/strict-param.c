/* Origin: EmberBSD actual ucom parameter-section regression, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include "fake.h"
#define UCOMHIST_FUNC() ((void)0)
#define UCOMHIST_CALLED() ((void)0)
#define DPRINTF(...) ((void)0)
#define ISSET(a,b) ((a)&(b))
#define SET(a,b) ((a)|=(b))
#define CLR(a,b) ((a)&=~(b))
#define UCOMUNIT(d) (d)
#define TIOCFLAG_SOFTCAR 1
#define TIOCFLAG_CLOCAL 2
#define TIOCFLAG_CRTSCTS 4
#define TIOCFLAG_MDMBUF 8
#define HUPCL 0x4000
#define TTYDEF_SPEED 9600
#define TTYDEF_CFLAG CS8
#define UCOM_OPENING 1
#define UMSR_DCD 0x80
struct linesw { int (*l_modem)(void *,int); };
struct tty { int t_dev;uint32_t t_ispeed,t_ospeed,t_cflag;struct linesw *t_linesw; };
struct ucom_softc { int sc_swflags,sc_state,sc_dying;void *sc_parent,*sc_transport;int sc_portno;uint8_t sc_msr,sc_lcr;struct ucom_methods *sc_methods;kmutex_t sc_tctl,sc_lock; };
static struct ucom_softc softc;
static unsigned calls;
static int parent_error;
static int ucom_cd;
static void *device_lookup_private(void *cd,int unit) { (void)cd;(void)unit;return &softc; }
static int ucom_transport_error(void *sc) { (void)sc;return 0; }
static int mutex_owned(kmutex_t *m) { return *m; }
static void ttylock(void *tp) { (void)tp; }
static void ttyunlock(void *tp) { (void)tp; }
static int modem(void *tp,int carrier) { (void)tp;(void)carrier;return 0; }
static int param(void *parent,int port,struct termios *t) { (void)parent;(void)port;(void)t;calls++;return parent_error; }
/* The runner inserts unmodified ucomparam and initial parameter block here. */
#include "actual-param.inc"
static int initial(struct tty *tp) {
 struct ucom_softc *sc=&softc;int error=0;
#include "actual-initial.inc"
 return 0;
bad: return error;
}
int main(void) {
 struct ucom_methods methods={.ucom_param=param,.ucom_param_strict=true};
 struct linesw lines={modem};struct tty tp={.t_ospeed=9600,.t_cflag=CS8,.t_linesw=&lines};
 struct termios t={.c_ospeed=115200,.c_cflag=CS8};
 softc.sc_methods=&methods;parent_error=EIO;
 assert(ucomparam(&tp,&t)==EIO);assert(tp.t_ospeed==9600);
 assert(ucomparam(&tp,&t)==EIO && calls==2);
 parent_error=0;assert(ucomparam(&tp,&t)==0 && tp.t_ospeed==115200 && calls==3);
 parent_error=EIO;softc.sc_state=UCOM_OPENING;
 assert(initial(&tp)==EIO);
 puts("PASS actual ucomparam rejection/retry/acceptance and first-open errno");
 return 0;
}
