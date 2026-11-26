/* SPDX-License-Identifier: BSD-2-Clause */
/* Host model of the OS hooks used by the real IRQ state machine. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __KERNEL_RCSID(a,b)
#define KASSERT assert
#define MUTEX_DEFAULT 0
#define IPL_VM 1
#define PRI_NONE 0
#define WQ_MPSAFE 1
#define EVCNT_TYPE_INTR 1
#define EVCNT_TYPE_MISC 0
#define MIN(a,b) ((a)<(b)?(a):(b))

typedef int kmutex_t, kcondvar_t, bus_space_tag_t, bus_space_handle_t;
typedef void *device_t;
struct evcnt { uint64_t ev_count; };
struct work { int pending; };
struct workqueue { void (*fn)(struct work *, void *); void *arg; };
static int masks, unmasks, released, reads, writes, polls, logcount;
static int resets, enabled, clear_budget;
static bool pending, unbound;
static int (*handler)(void *);
static void *handler_arg;
static void mutex_init(kmutex_t *m,int t,int p) { (void)t;(void)p;*m=0; }
static void mutex_enter(kmutex_t *m) { assert(!*m);*m=1; }
static void mutex_exit(kmutex_t *m) { assert(*m);*m=0; }
static bool mutex_owned(kmutex_t *m) { return *m; }
static void mutex_destroy(kmutex_t *m) { assert(!*m); }
static void cv_init(kcondvar_t *c,const char *n) { (void)n;*c=0; }
static void cv_destroy(kcondvar_t *c) { (void)c; }
static void cv_broadcast(kcondvar_t *c) { ++*c; }
static int cv_timedwait(kcondvar_t *c,kmutex_t *m,int t)
{ (void)c; assert(*m && t>0); return EWOULDBLOCK; }
static const char *device_xname(device_t d) { assert(d);return "gpu0"; }
static void evcnt_attach_dynamic(struct evcnt *e,int t,void *p,const char *g,const char *n)
{ (void)t;(void)p;assert(g && n);e->ev_count=0; }
static void evcnt_detach(struct evcnt *e) { (void)e; }
#define aprint_error_dev(dev, ...) do { assert(dev); ++logcount; } while (0)
static int workqueue_create(struct workqueue **q,const char *name,
    void (*fn)(struct work *,void *),void *arg,int pri,int ipl,int flags)
{ (void)name;(void)pri;(void)ipl;(void)flags;*q=calloc(1,sizeof(**q));(*q)->fn=fn;(*q)->arg=arg;return 0; }
static void workqueue_enqueue(struct workqueue *q,struct work *w,void *cpu)
{ assert(q && !w->pending);(void)cpu;w->pending=1; }
static void workqueue_wait(struct workqueue *q,struct work *w)
{ if (w->pending) { w->pending=0;q->fn(w,q->arg); } }
static void workqueue_destroy(struct workqueue *q) { free(q); }
static int kpause(const char *n,bool intr,int t,void *lk)
{ assert(n && !intr && t>0 && !lk); ++polls;return 0; }
static int pvr_mips_irq_pending(bus_space_tag_t t,bus_space_handle_t h,bool *v)
{ assert(t==1 && h==2);++reads;*v=pending;return 0; }
static int pvr_mips_irq_clear(bus_space_tag_t t,bus_space_handle_t h)
{ assert(t==1 && h==2);++writes;if (++resets>clear_budget) pending=false;return 0; }
static int pvr_mips_irq_enable(bus_space_tag_t t,bus_space_handle_t h,bool enable)
{ assert(t==1 && h==2);enabled=enable;return 0; }

#include "job-irq-actual.h"

static void *route_setup(void *arg,int (*fn)(void *),void *cookie)
{ assert(arg && fn && cookie);handler=fn;handler_arg=cookie;return unbound ? NULL : arg; }
static void route_mask(void *arg,void *cookie) { assert(arg==cookie);++masks; }
static void route_unmask(void *arg,void *cookie) { assert(arg==cookie);++unmasks; }
static void route_release(void *arg,void *cookie) { assert(arg==cookie&&masks);++released; }

int main(void)
{
	const struct pvr_job_irq_route route = { route_setup,route_mask,route_unmask,route_release };
	struct pvr_job_irq irq = {0}, ge = {0};
	unsigned mark, total, window;
	int softc;
	pvr_job_irq_init(&irq,&softc);
	unbound=true;
	assert(pvr_job_irq_start(&irq,1,2,&route,&softc,true)==ENXIO);
	assert(!pvr_job_irq_active(&irq)&&reads==0&&writes==0&&unmasks==0);
	unbound=false;
	assert(pvr_job_irq_start(&irq,1,2,&route,&softc,true)==0);
	assert(pvr_job_irq_active(&irq)&&enabled&&unmasks==1);
	mark=pvr_job_irq_mark(&irq);
	pending=true;
	assert(handler(handler_arg)==1);
	pvr_job_irq_wait(&irq,mark,1);
	assert(irq.count[PVR_IRQ_RACED].ev_count==1);
	pvr_job_irq_wait(&irq,pvr_job_irq_mark(&irq),1);
	assert(irq.count[PVR_IRQ_TIMEOUTS].ev_count==1);
	clear_budget=resets+9;
	pending=true;
	assert(handler(handler_arg)==1);
	assert(!pvr_job_irq_active(&irq)&&!enabled&&masks==1);
	assert(irq.count[PVR_IRQ_REASSERT].ev_count==4);
	assert(irq.count[PVR_IRQ_FIRED].ev_count==1);
	assert(handler(handler_arg)==0);
	pvr_job_irq_wait(&irq,mark,1);
	assert(polls==1);
	pvr_job_irq_stop(&irq);
	pvr_job_irq_stop(&irq);
	assert(released==1&&logcount==1&&irq.count[PVR_IRQ_RETIRED].ev_count==1);
	pvr_job_irq_fini(&irq);
	pvr_job_irq_init(&ge,&softc);
	pending=false;
	assert(pvr_job_irq_start(&ge,1,2,&route,&softc,false)==0&&!enabled);
	assert(pvr_job_irq_probe_arm(&ge)==0);
	pending=true;
	assert(handler(handler_arg)==1);
	assert(pvr_job_irq_probe(&ge,&total,&window,true)==0&&total==1&&window==1);
	pvr_job_irq_stop(&ge);
	pvr_job_irq_fini(&ge);
	assert(released==2&&logcount==1);
	puts("PASS IRQ probe, raced timeout, storm fallback, serial retirement and legacy GE W1C");
}
