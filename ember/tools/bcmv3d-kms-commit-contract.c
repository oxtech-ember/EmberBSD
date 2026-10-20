/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual KMS commit/IRQ/bind functions. Controlled DRM and native services. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "target-errno.h"
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define READ_ONCE(p) (p)
static unsigned checks;
#define CHECK(v) do { checks++; if (!(v)) { \
	fprintf(stderr, "CHECK %d: %s\n", __LINE__, #v); exit(1); } } while (0)
#define KASSERT(v) do { checks++; if (!(v)) { \
	fprintf(stderr, "CHECK %d: %s\n", __LINE__, #v); exit(1); } } while (0)
typedef struct { bool held; } spinlock_t;
#define spin_lock_irqsave(lock, flags) do { (flags)=0; \
	CHECK(!(lock)->held); (lock)->held=true; } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(flags); \
	CHECK((lock)->held); (lock)->held=false; } while (0)
struct completion { unsigned done; };
struct dma_fence { int status; unsigned refs; };
struct drm_pending_vblank_event { unsigned terminal; int out_status; };
struct drm_crtc_state { bool active; struct drm_pending_vblank_event *event; };
struct drm_plane_state { struct dma_fence *fence; };
struct drm_device { spinlock_t event_lock; void *dev_private; bool irq_enabled; };
struct drm_crtc { struct drm_device *dev; };
struct drm_plane { unsigned unused; };
struct drm_connector { unsigned unused; };
struct drm_encoder { unsigned unused; };
struct drm_atomic_state { struct drm_device *dev; struct drm_crtc_state *crtc;
    struct drm_plane_state *plane; };
struct bcmv3d_scanout_frame { unsigned refs; bool ready, exposed, retained; };
struct bcmv3d_scanout { unsigned unused; };
struct bcmv3d_vblank_native { unsigned unused; };
struct bcmv3d_scanout_status { uint64_t sequence, frame_epoch, prepared;
    int current_slot, pending_slot, first_error; };
struct bcmv3d_vblank_snapshot { uint64_t sequence; unsigned prepared,
    arm_attempted, routed, active_list; int first_error; };
struct bcmv3d_kms;
struct bcmv3d_device { struct drm_device *drm; struct bcmv3d_kms *kms;
    struct bcmv3d_scanout *scanout; struct bcmv3d_vblank_native *vblank_native; };
#include "kms-interface.h"
#include "kms-types.h"
static struct drm_device dev;
static struct bcmv3d_device sc;
static struct bcmv3d_kms kms;
static struct drm_atomic_state atomic;
static struct drm_crtc_state crtc;
static struct bcmv3d_kms_plane_state plane;
static struct bcmv3d_scanout owner;
static struct bcmv3d_vblank_native pv;
static struct bcmv3d_scanout_frame frame;
static struct bcmv3d_scanout_status cached;
static struct bcmv3d_vblank_snapshot irq;
static struct drm_pending_vblank_event events[2];
static struct dma_fence explicit_fence;
static void (*native_notify)(void *, uint64_t, bool, int);
static void *native_arg;
static unsigned mode, submissions, writes, sends, failures, hardware_done, cleanup;
static unsigned old_refs, finalize_calls, legacy_calls, vblank_refs, on_calls;
static unsigned off_calls, handle_calls, bind_calls, client_attempts, poll_calls;
static unsigned config_cleanups, completion_destroys, frees;
static bool completion_live;
static uint64_t last_counter, drm_frames;
static bool in_irq, core_on, core_enabled, explicit_checked, bind_fault;
static bool simulate_first_client, initial_off, no_event;
static const int hz=100;
static int kms_available(struct bcmv3d_kms *);
static void kms_native_event(void *, uint64_t, bool, int);
static uint32_t kms_vblank_counter(struct drm_crtc *);
static int kms_vblank_enable(struct drm_crtc *);
static void kms_vblank_disable(struct drm_crtc *);
static void complete_all(struct completion *c)
{ CHECK(c==&kms.latch); c->done=UINT32_MAX; }
static void reinit_completion(struct completion *c)
{ CHECK(c==&kms.latch&&!dev.event_lock.held); c->done=0; }
static int dma_fence_get_status(struct dma_fence *f)
{ CHECK(f==&explicit_fence&&f->refs==1&&!dev.event_lock.held);
  explicit_checked=true; return f->status; }
/* The separately tested real event-failure helper supplies this contract. */
static void bcmv3d_kms_event_fail(struct drm_device *d,
    struct drm_pending_vblank_event *e, int error)
{
	CHECK(d==&dev&&!dev.event_lock.held&&error>0&&error<=ELAST);
	if (e==NULL) return;
	CHECK(e->terminal==0&&e->out_status==0);
	e->terminal=2;e->out_status=-error;failures++;
}
static struct drm_crtc_state *drm_atomic_get_new_crtc_state(
    struct drm_atomic_state *s, struct drm_crtc *c)
{ CHECK(s==&atomic&&c==&kms.crtc); return s->crtc; }
static struct drm_plane_state *drm_atomic_get_new_plane_state(
    struct drm_atomic_state *s, struct drm_plane *p)
{ CHECK(s==&atomic&&p==&kms.plane); return s->plane; }
static void drm_atomic_helper_update_legacy_modeset_state(
    struct drm_device *d, struct drm_atomic_state *s)
{ CHECK(d==&dev&&s==&atomic&&!dev.event_lock.held);
  CHECK(hardware_done==cleanup);legacy_calls++; }
static int bcmv3d_scanout_finalize(struct bcmv3d_scanout_frame *f, long ticks)
{
	CHECK(f==&frame&&f->refs==1&&ticks==5*hz&&!dev.event_lock.held);
	CHECK(legacy_calls==hardware_done+1);
	CHECK(plane.explicit==NULL||explicit_checked);finalize_calls++;
	if (mode==6) return EIO;
	f->ready=true;return 0;
}
static int bcmv3d_scanout_cached_status(struct bcmv3d_scanout *n,
    struct bcmv3d_scanout_status *out)
{ CHECK(n==&owner&&out);*out=cached;return 0; }
static int bcmv3d_vblank_native_snapshot(struct bcmv3d_vblank_native *v,
    struct bcmv3d_vblank_snapshot *out)
{ CHECK(v==&pv&&out);if(mode==7)return EIO;*out=irq;return 0; }
static void drm_crtc_vblank_on(struct drm_crtc *c)
{ CHECK(c==&kms.crtc&&!dev.event_lock.held&&!core_on);
  CHECK(kms.bound&&kms.fault==0);core_on=true;on_calls++; }
static int drm_crtc_vblank_get(struct drm_crtc *c)
{
	unsigned long flags;int error;
	CHECK(c==&kms.crtc&&core_on&&!dev.event_lock.held);
	spin_lock_irqsave(&dev.event_lock,flags);
	error=kms_vblank_enable(c);
	spin_unlock_irqrestore(&dev.event_lock,flags);
	if(error)return error;
	if(mode==9)return -EIO;
	vblank_refs++;core_enabled=true;return 0;
}
static void drm_crtc_vblank_put(struct drm_crtc *c)
{ CHECK(c==&kms.crtc&&!dev.event_lock.held&&vblank_refs==1);
  CHECK(!kms.waiting&&kms.event==NULL);vblank_refs--; }
static void drm_crtc_vblank_off(struct drm_crtc *c)
{
	unsigned long flags;
	CHECK(c==&kms.crtc&&!dev.event_lock.held);off_calls++;
	if(initial_off&&simulate_first_client) {
		client_attempts++;CHECK(kms_available(&kms)!=0);
	}
	spin_lock_irqsave(&dev.event_lock,flags);
	kms_vblank_disable(c);
	last_counter=kms_vblank_counter(c);
	spin_unlock_irqrestore(&dev.event_lock,flags);
	core_on=core_enabled=false;initial_off=false;
}
static bool drm_crtc_handle_vblank(struct drm_crtc *c)
{
	unsigned long flags;uint32_t counter;
	CHECK(c==&kms.crtc&&in_irq&&!dev.event_lock.held);handle_calls++;
	spin_lock_irqsave(&dev.event_lock,flags);
	counter=kms_vblank_counter(c);
	if(core_enabled)drm_frames+=(uint32_t)(counter-last_counter);
	last_counter=counter;
	spin_unlock_irqrestore(&dev.event_lock,flags);
	return core_enabled;
}
static void drm_crtc_send_vblank_event(struct drm_crtc *c,
    struct drm_pending_vblank_event *e)
{
	CHECK(c==&kms.crtc&&in_irq&&dev.event_lock.held);
	CHECK(kms.waiting&&e==kms.event&&e->terminal==0&&e->out_status==0);
	e->terminal=1;e->out_status=1;sends++;
}
static void emit(uint64_t sequence, bool latched, int error, unsigned advance)
{
	CHECK(native_notify&&!dev.event_lock.held&&!in_irq);
	cached.frame_epoch+=advance;
	if(sequence>irq.sequence)cached.sequence=irq.sequence=sequence;
	in_irq=true;native_notify(native_arg,sequence,latched,error);in_irq=false;
}
static int bcmv3d_scanout_submit(struct bcmv3d_scanout *n,
    struct bcmv3d_scanout_frame *f)
{
	CHECK(n==&owner&&!dev.event_lock.held&&vblank_refs==1);
	CHECK(kms.waiting&&kms.event==(no_event?NULL:&events[hardware_done])&&crtc.event==NULL);
	CHECK(kms.event_floor==irq.sequence&&kms.latch.done==0);
	CHECK(f==(crtc.active?&frame:NULL));
	if(f)CHECK(f->ready);
	submissions++;writes++;
	if(f)f->exposed=f->retained=true;
	if(mode==10)return EIO;
	if(mode==12){emit(irq.sequence+1,false,EIO,0);return 0;}
	if(mode==0||mode==13||mode==15||mode==16)emit(irq.sequence+1,true,0,1);
	return 0;
}
static unsigned long wait_for_completion_timeout(struct completion *c,long ticks)
{
	CHECK(c==&kms.latch&&ticks==5*hz&&!dev.event_lock.held);
	if(c->done)return 1;
	if(mode==11)return 0;
	if(mode==2) {
		emit(kms.event_floor+4,false,0,4);
		CHECK(!c->done&&events[0].terminal==0);
		emit(kms.event_floor+2,true,0,0);
	} else if(mode==3) {
		emit(kms.event_floor-1,true,0,0);
		CHECK(!c->done&&kms.waiting&&events[0].terminal==0);
		emit(kms.event_floor+1,true,0,1);
	} else {
		CHECK(mode==1);emit(kms.event_floor+1,true,0,1);
	}
	CHECK(c->done!=0);return 1;
}
static void drm_atomic_helper_commit_hw_done(struct drm_atomic_state *s)
{
	CHECK(s==&atomic&&!dev.event_lock.held&&crtc.event==NULL);
	CHECK(!kms.waiting&&kms.event==NULL&&(no_event||events[hardware_done].terminal!=0));
	CHECK(vblank_refs==0&&legacy_calls==hardware_done+1);
	hardware_done++;
}
static void drm_atomic_helper_cleanup_planes(struct drm_device *d,
    struct drm_atomic_state *s)
{
	CHECK(d==&dev&&s==&atomic&&!dev.event_lock.held);
	CHECK(hardware_done==cleanup+1&&old_refs==1);
	CHECK(plane.frame==NULL||plane.frame->refs==1);
	old_refs--;cleanup++;
}
static int bcmv3d_scanout_bind(struct bcmv3d_scanout *n,
    void (*notify)(void *,uint64_t,bool,int),void *arg)
{
	CHECK(n==&owner&&notify==kms_native_event&&arg==&kms);
	CHECK(!dev.event_lock.held&&!native_notify);bind_calls++;
	native_notify=notify;native_arg=arg;
	if(bind_fault)emit(irq.sequence+1,false,EIO,0);
	return 0;
}
static void drm_kms_helper_poll_init(struct drm_device *d)
{ CHECK(d==&dev&&kms.bound&&kms.fault==0&&!dev.event_lock.held);poll_calls++; }
static void drm_kms_helper_hotplug_event(struct drm_device *d)
{ CHECK(d==&dev&&kms.bound&&kms.polling&&poll_calls==1); }
static void drm_mode_config_cleanup(struct drm_device *d)
{
	CHECK(d==&dev&&sc.kms==&kms&&!dev.event_lock.held);
	CHECK(config_cleanups==0&&completion_live&&completion_destroys==0&&frees==0);
	config_cleanups++;
}
static void destroy_completion(struct completion *c)
{
	CHECK(c==&kms.latch&&sc.kms==&kms&&!dev.event_lock.held);
	CHECK(config_cleanups==1&&completion_live&&completion_destroys==0&&frees==0);
	completion_live=false;completion_destroys++;
}
static void kfree(void *p)
{
	CHECK(p==&kms&&sc.kms==NULL&&!dev.event_lock.held);
	CHECK(config_cleanups==1&&!completion_live&&completion_destroys==1&&frees==0);
	frees++;
}
#include "kms-body.h"
static int detect(void *arg,bool *connected)
{ CHECK(arg==&owner&&connected);*connected=true;return 0; }
static void setup(void)
{
	memset(&dev,0,sizeof(dev));memset(&sc,0,sizeof(sc));memset(&kms,0,sizeof(kms));
	memset(&atomic,0,sizeof(atomic));memset(&crtc,0,sizeof(crtc));
	memset(&plane,0,sizeof(plane));memset(events,0,sizeof(events));
	frame=(struct bcmv3d_scanout_frame){.refs=1};
	explicit_fence=(struct dma_fence){.refs=1,.status=1};
	cached=(struct bcmv3d_scanout_status){.sequence=1000,.frame_epoch=400,
	    .current_slot=-1,.pending_slot=-1};
	irq=(struct bcmv3d_vblank_snapshot){.sequence=1000,.prepared=1,
	    .arm_attempted=1,.routed=1,.active_list=32};
	sc.drm=&dev;sc.kms=&kms;sc.scanout=&owner;sc.vblank_native=&pv;
	dev.dev_private=&sc;kms.sc=&sc;kms.crtc.dev=&dev;
	atomic.dev=&dev;atomic.crtc=&crtc;atomic.plane=&plane.base;
	plane.frame=&frame;crtc.active=true;crtc.event=&events[0];
	native_notify=NULL;native_arg=NULL;mode=0;
	submissions=writes=sends=failures=hardware_done=cleanup=0;
	finalize_calls=legacy_calls=vblank_refs=on_calls=off_calls=0;
	handle_calls=bind_calls=client_attempts=poll_calls=0;
	last_counter=drm_frames=0;old_refs=1;
	config_cleanups=completion_destroys=frees=0;completion_live=true;
	in_irq=core_on=core_enabled=explicit_checked=bind_fault=false;
	simulate_first_client=initial_off=true;no_event=false;
}
static void boot(void)
{
	CHECK(bcmv3d_kms_bind(&sc,detect,&owner)==0);
	CHECK(kms.bound&&dev.irq_enabled&&kms.polling&&poll_calls==1);
	CHECK(client_attempts==1&&off_calls==1&&bind_calls==1&&!core_on);
}
static void binding(void)
{
	setup();bind_fault=true;
	CHECK(bcmv3d_kms_bind(&sc,detect,&owner)==EIO);
	CHECK(!kms.bound&&!dev.irq_enabled&&kms.fault==EIO&&poll_calls==0);
	CHECK(kms_available(&kms)==EIO&&writes==0);
	setup();cached.prepared=1;
	CHECK(bcmv3d_kms_bind(&sc,detect,&owner)==EPERM&&bind_calls==0);
	setup();boot();core_enabled=true;core_on=true;
	cached.frame_epoch=403;cached.sequence=irq.sequence=1001;
	CHECK(kms_vblank_counter(&kms.crtc)==403);
	emit(1001,false,0,0);
	CHECK(handle_calls==1&&drm_frames==3&&kms.vblank_sequence==1001);
	emit(1000,false,0,0);CHECK(handle_calls==1&&drm_frames==3);
}
static void fault_codes(void)
{
	const int errors[]={0,-EIO,ELAST+1,ERESTARTSYS,ELAST};
	unsigned i;
	int expected;
	CHECK(ERESTARTSYS==ELAST+1);
	for(i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
		setup();boot();expected=errors[i]==ELAST?ELAST:EIO;
		kms.event=crtc.event;crtc.event=NULL;kms.waiting=true;
		kms_fault(&kms,errors[i]);
		CHECK(kms.fault==expected&&kms.pending_error==expected);
		CHECK(!kms.waiting&&kms.event==NULL&&kms.latch.done==UINT32_MAX);
		CHECK(events[0].terminal==2&&events[0].out_status==-expected);
		CHECK(failures==1&&sends==0&&writes==0);
		/* A later error must not replace the first terminal cause. */
		kms_fault(&kms,EINVAL);
		CHECK(kms.fault==expected&&kms.pending_error==expected);
		CHECK(failures==1&&sends==0&&writes==0&&!dev.event_lock.held);
	}
}
static void finalization(void)
{
	setup();sc.kms=NULL;bcmv3d_kms_fini(&sc);
	CHECK(config_cleanups==0&&completion_destroys==0&&frees==0&&completion_live);
	setup();bcmv3d_kms_fini(&sc);
	CHECK(sc.kms==NULL&&config_cleanups==1&&completion_destroys==1&&frees==1);
	CHECK(!completion_live&&!dev.event_lock.held);
	bcmv3d_kms_fini(&sc);
	CHECK(config_cleanups==1&&completion_destroys==1&&frees==1);
}
static void invalid_finalization(const char *arg)
{
	setup();
	if(strcmp(arg,"bound")==0)kms.bound=true;
	else if(strcmp(arg,"waiting")==0)kms.waiting=true;
	else if(strcmp(arg,"event")==0)kms.event=&events[0];
	else if(strcmp(arg,"polling")==0)kms.polling=true;
	else CHECK(false);
	bcmv3d_kms_fini(&sc);
	CHECK(false);
}
static void commit_case(unsigned which)
{
	int error=0;unsigned before;
	setup();boot();mode=which;
	if(which==4||which==5||which==15) {
		plane.explicit=&explicit_fence;plane.base.fence=NULL;
		explicit_fence.status=which==4?-EIO:which==5?0:1;
	}
	if(which==8)irq.first_error=EIO;
	if(which==13||which==16){crtc.active=false;plane.frame=NULL;}
	if(which==16){no_event=true;crtc.event=NULL;}
	if((which>=4&&which<=12)&&which!=13)error=which==11?ETIMEDOUT:EIO;
	kms_commit_tail(&atomic);
	CHECK(hardware_done==1&&cleanup==1&&old_refs==0&&vblank_refs==0);
	CHECK(!kms.waiting&&kms.event==NULL&&crtc.event==NULL);
	CHECK(frame.refs==1&&explicit_fence.refs==1);
	if(error) {
		CHECK(kms.fault==error&&sends==0&&failures==1);
		CHECK(events[0].terminal==2&&events[0].out_status==-error);
		CHECK(submissions==(which>=10?1U:0U));
		if(which>=10)CHECK(frame.retained&&frame.exposed);
		if(which==11||which==12) {
			before=writes;emit(irq.sequence+1,true,0,1);
			CHECK(sends==0&&failures==1&&frame.retained);
			/* An already-checked commit can reach tail after a sticky fault. */
			crtc.event=&events[1];old_refs=1;kms_commit_tail(&atomic);
			CHECK(writes==before&&failures==2&&events[1].out_status==-error);
			CHECK(hardware_done==2&&cleanup==2&&old_refs==0);
		}
	} else {
		CHECK(kms.fault==0&&sends==(no_event?0U:1U)&&failures==0&&submissions==1);
		CHECK(no_event||(events[0].terminal==1&&events[0].out_status==1));
		if(which==0) {
			crtc.event=&events[1];old_refs=1;kms_commit_tail(&atomic);
			CHECK(sends==2&&hardware_done==2&&cleanup==2&&on_calls==1);
		}
		if(which==13||which==16)CHECK(!kms.vblank_on&&!core_on&&off_calls==2);
	}
	CHECK(!dev.event_lock.held&&!in_irq);
}
int main(int argc, char **argv)
{
	unsigned i;
	if(argc==2)invalid_finalization(argv[1]);
	CHECK(argc==1);binding();fault_codes();finalization();
	for(i=0;i<17;i++)if(i!=14)commit_case(i);
	printf("PASS actual KMS commit/IRQ/bind: %u CHECKs, bind admission, frame epoch,"
	    "callback order, fence errors, terminal events and old-state cleanup\n",checks);
	return 0;
}
