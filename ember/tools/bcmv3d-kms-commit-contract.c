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
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int64_t __s64;
#include "kms-event-uapi.h"
typedef int64_t ktime_t;
struct timespec64 { int64_t tv_sec; long tv_nsec; };
struct drm_pending_event { unsigned identity; };
struct drm_pending_vblank_event {
	struct drm_pending_event base;
	unsigned pipe, terminal;
	int out_status;
	union {
		struct drm_event base;
		struct drm_event_vblank vbl;
		struct drm_event_crtc_sequence seq;
	} event;
};
struct drm_crtc_state { bool active; struct drm_pending_vblank_event *event; };
struct drm_plane_state { struct dma_fence *fence; };
struct drm_device { spinlock_t event_lock; void *dev_private; bool irq_enabled; };
struct drm_crtc { struct drm_device *dev; };
struct drm_plane { unsigned unused; };
struct drm_connector { unsigned unused; };
struct drm_encoder { unsigned unused; };
struct drm_atomic_state { struct drm_device *dev; struct drm_crtc_state *crtc;
    struct drm_plane_state *plane, *old_plane; };
struct bcmv3d_scanout_frame { unsigned refs; bool ready, exposed, retained, retired; };
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
static struct bcmv3d_kms_plane_state plane, old_plane;
static struct bcmv3d_scanout owner;
static struct bcmv3d_vblank_native pv;
static struct bcmv3d_scanout_frame frame, old_frame;
static struct bcmv3d_scanout_status cached;
static struct bcmv3d_vblank_snapshot irq;
static struct drm_pending_vblank_event events[2];
static struct dma_fence explicit_fence;
static void (*native_notify)(void *, uint64_t, bool, int);
static void *native_arg;
static unsigned mode, submissions, writes, sends, failures, hardware_done, cleanup;
static unsigned old_refs, finalize_calls, legacy_calls, vblank_refs, on_calls;
static unsigned off_calls, handle_calls, bind_calls, client_attempts, poll_calls;
static unsigned config_cleanups, completion_destroys, frees, retire_waits, captures;
static unsigned blank_waits, snapshot_calls;
enum test_blank_state { TEST_BLANK_READY, TEST_BLANK_CURRENT, TEST_BLANK_RETIRING };
static enum test_blank_state blank_state;
static int blank_error;
static bool commit_snapshot, commit_vblank_on, commit_vblank_get, commit_submitted;
static bool blank_wait_saw_snapshot;
static uint64_t snapshot_sequence, blank_stale_sequence;
static bool completion_live;
static uint64_t last_counter, drm_frames, captured_sequence;
static ktime_t cooked_time, captured_time;
static bool in_irq, core_on, core_enabled, explicit_checked, bind_fault;
static bool simulate_first_client, initial_off, no_event;
static const int hz=100;
static int kms_available(struct bcmv3d_kms *);
static void kms_native_event(void *, uint64_t, bool, int);
static void kms_fault(struct bcmv3d_kms *, int);
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
static struct drm_plane_state *drm_atomic_get_old_plane_state(
    struct drm_atomic_state *s, struct drm_plane *p)
{ CHECK(s==&atomic&&p==&kms.plane); return s->old_plane; }
static unsigned drm_crtc_index(struct drm_crtc *c)
{ CHECK(c==&kms.crtc);return 0; }
static struct timespec64 ktime_to_timespec64(ktime_t time)
{ return (struct timespec64){.tv_sec=time/1000000000,
    .tv_nsec=(long)(time%1000000000)}; }
static int64_t ktime_to_ns(ktime_t time)
{ return time; }
static ktime_t ktime_get(void)
{ return cooked_time; }
static void drm_atomic_helper_update_legacy_modeset_state(
    struct drm_device *d, struct drm_atomic_state *s)
{ CHECK(d==&dev&&s==&atomic&&!dev.event_lock.held);
  CHECK(hardware_done==cleanup);legacy_calls++;
  commit_snapshot=commit_vblank_on=commit_vblank_get=commit_submitted=false;
  blank_wait_saw_snapshot=false; }
static int bcmv3d_scanout_finalize(struct bcmv3d_scanout_frame *f, long ticks)
{
	CHECK(f==&frame&&f->refs==(old_plane.frame==f?2U:1U)&&ticks==5*hz&&!dev.event_lock.held);
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
{
	CHECK(v==&pv&&out);snapshot_calls++;
	CHECK(crtc.active||blank_error==0);
	if(kms.bound)commit_snapshot=true;
	snapshot_sequence=irq.sequence;
	if(mode==7)return EIO;
	*out=irq;return 0;
}
static void drm_crtc_vblank_on(struct drm_crtc *c)
{ CHECK(c==&kms.crtc&&!dev.event_lock.held&&!core_on);
  CHECK(kms.bound&&kms.fault==0);core_on=true;on_calls++;commit_vblank_on=true; }
static int drm_crtc_vblank_get(struct drm_crtc *c)
{
	unsigned long flags;int error;
	CHECK(c==&kms.crtc&&core_on&&!dev.event_lock.held);commit_vblank_get=true;
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
	if(core_enabled) {
		drm_frames+=(uint32_t)(counter-last_counter);
		cooked_time+=16666667*(uint32_t)(counter-last_counter);
	}
	last_counter=counter;
	spin_unlock_irqrestore(&dev.event_lock,flags);
	return core_enabled;
}
/* Cooked values change at real callback progression, independently of delivery. */
static uint64_t drm_crtc_vblank_count_and_time(struct drm_crtc *c, ktime_t *time)
{
	CHECK(c==&kms.crtc&&time&&dev.event_lock.held);
	*time=cooked_time;
	if(in_irq) {
		CHECK(captures==hardware_done);
		captured_sequence=drm_frames;captured_time=cooked_time;captures++;
	}
	return drm_frames;
}
static void drm_send_event_locked(struct drm_device *d, struct drm_pending_event *base)
{
	struct drm_pending_vblank_event *e=container_of(base,struct drm_pending_vblank_event,base);
	struct timespec64 tv=ktime_to_timespec64(captured_time);
	CHECK(d==&dev&&dev.event_lock.held&&e->pipe==0);
	CHECK(!in_irq&&hardware_done==cleanup&&cleanup>0&&old_refs==0);
	CHECK(kms.fault==0&&e->terminal==0&&e->out_status==0);
	CHECK(!kms.waiting&&kms.event==NULL&&crtc.event==NULL);
	switch(e->event.base.type) {
	case DRM_EVENT_VBLANK:
	case DRM_EVENT_FLIP_COMPLETE:
		CHECK(e->event.vbl.sequence==(uint32_t)captured_sequence);
		CHECK(e->event.vbl.tv_sec==(uint32_t)tv.tv_sec);
		CHECK(e->event.vbl.tv_usec==(uint32_t)(tv.tv_nsec/1000));
		break;
	case DRM_EVENT_CRTC_SEQUENCE:
		CHECK(e->event.seq.sequence==(captured_sequence?captured_sequence:77));
		CHECK(e->event.seq.time_ns==captured_time);
		break;
	}
	e->terminal=1;e->out_status=1;sends++;
}
/* The old callback called this helper and let CPU producers run too early. */
static void drm_crtc_send_vblank_event(struct drm_crtc *c,
    struct drm_pending_vblank_event *e)
{
	CHECK(c==&kms.crtc&&dev.event_lock.held);
	drm_send_event_locked(c->dev,&e->base);
}
static void emit(uint64_t sequence, bool latched, int error, unsigned advance)
{
	CHECK(native_notify&&!dev.event_lock.held&&!in_irq);
	cached.frame_epoch+=advance;
	if(sequence>irq.sequence)cached.sequence=irq.sequence=sequence;
	in_irq=true;native_notify(native_arg,sequence,latched,error);in_irq=false;
}
/* The native API owns its slot state. This service only models its boundary. */
static int bcmv3d_scanout_wait_blank_ready(struct bcmv3d_scanout *n,long ticks)
{
	CHECK(n==&owner&&ticks==5*hz&&!dev.event_lock.held&&!in_irq);
	CHECK(!kms.waiting&&kms.event==NULL&&crtc.event==(no_event?NULL:&events[hardware_done]));
	CHECK(!crtc.active&&!commit_vblank_on&&!commit_vblank_get&&!commit_submitted&&kms.fault==0);
	blank_wait_saw_snapshot=commit_snapshot;blank_waits++;
	if(blank_error!=0)return blank_error;
	if(blank_state==TEST_BLANK_RETIRING) {
		CHECK(mode==24);
		blank_stale_sequence=irq.sequence+1;
		/* Worker progress can advance native sequence before the fresh floor. */
		emit(irq.sequence+3,false,0,3);
		CHECK(!kms.waiting&&events[hardware_done].terminal==0&&writes==0);
		blank_state=TEST_BLANK_READY;
	}
	CHECK(blank_state==TEST_BLANK_READY||blank_state==TEST_BLANK_CURRENT);
	return 0;
}
static int bcmv3d_scanout_submit(struct bcmv3d_scanout *n,
    struct bcmv3d_scanout_frame *f)
{
	CHECK(n==&owner&&!dev.event_lock.held&&vblank_refs==1);
	CHECK(kms.waiting&&kms.event==(no_event?NULL:&events[hardware_done])&&crtc.event==NULL);
	CHECK(kms.event_floor==snapshot_sequence&&kms.latch.done==0);
	if(f==NULL)CHECK(blank_state==TEST_BLANK_READY||blank_state==TEST_BLANK_CURRENT);
	commit_submitted=true;
	CHECK(f==(crtc.active?&frame:NULL));
	if(f)CHECK(f->ready);
	submissions++;writes++;
	if(f)f->exposed=f->retained=true;
	if(mode==10)return EIO;
	if(mode==12){emit(irq.sequence+1,false,EIO,0);return 0;}
	if((mode==0||mode==13||mode==15||mode==16||mode>=17)&&mode!=24)
		emit(irq.sequence+1,true,0,1);
	return 0;
}
static int bcmv3d_scanout_wait_retired(struct bcmv3d_scanout_frame *f,long ticks)
{
	CHECK(f==&old_frame&&ticks==5*hz&&!dev.event_lock.held&&!in_irq);
	CHECK(kms.latch.done!=0&&!kms.waiting&&old_refs==1);
	CHECK(f->refs==1&&f->exposed&&f->retained&&!f->retired);
	CHECK(hardware_done==cleanup&&events[hardware_done].terminal==0);
	retire_waits++;
	if(mode==17)return EIO;
	if(mode==18)return ETIMEDOUT;
	if(mode==21) {
		emit(irq.sequence+3,false,0,3);
		CHECK(drm_frames>captured_sequence&&cooked_time>captured_time);
		CHECK(events[hardware_done].terminal==0&&sends==0);
	}
	f->retired=true;
	return 0;
}
static unsigned long wait_for_completion_timeout(struct completion *c,long ticks)
{
	CHECK(c==&kms.latch&&ticks==5*hz&&!dev.event_lock.held);
	if(c->done)return 1;
	if(mode==11)return 0;
	if(mode==24) {
		emit(blank_stale_sequence,true,0,0);
		CHECK(!c->done&&kms.waiting&&events[0].terminal==0);
		emit(kms.event_floor+1,true,0,1);
	} else if(mode==2) {
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
	CHECK(!kms.waiting&&kms.event==NULL);
	CHECK(kms.fault!=0||old_plane.frame==NULL||old_plane.frame==plane.frame||old_plane.frame->retired);
	CHECK(no_event||events[hardware_done].terminal==(kms.fault!=0?2U:0U));
	CHECK(vblank_refs==0&&legacy_calls==hardware_done+1);
	hardware_done++;
}
static void drm_atomic_helper_cleanup_planes(struct drm_device *d,
    struct drm_atomic_state *s)
{
	CHECK(d==&dev&&s==&atomic&&!dev.event_lock.held);
	CHECK(hardware_done==cleanup+1&&old_refs==1);
	CHECK(plane.frame==NULL||plane.frame->refs==(old_plane.frame==plane.frame?2U:1U));
	if(old_plane.frame!=NULL) {
		CHECK(old_plane.frame->refs>0);
		old_plane.frame->refs--;
	}
	old_refs--;cleanup++;
	if(mode==22)kms_fault(&kms,EIO);
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
	memset(&plane,0,sizeof(plane));memset(&old_plane,0,sizeof(old_plane));
	memset(events,0,sizeof(events));
	events[0].event.base.type=events[1].event.base.type=DRM_EVENT_FLIP_COMPLETE;
	frame=(struct bcmv3d_scanout_frame){.refs=1};
	old_frame=(struct bcmv3d_scanout_frame){.refs=1,.exposed=true,.retained=true};
	explicit_fence=(struct dma_fence){.refs=1,.status=1};
	cached=(struct bcmv3d_scanout_status){.sequence=1000,.frame_epoch=400,
	    .current_slot=-1,.pending_slot=-1};
	irq=(struct bcmv3d_vblank_snapshot){.sequence=1000,.prepared=1,
	    .arm_attempted=1,.routed=1,.active_list=32};
	sc.drm=&dev;sc.kms=&kms;sc.scanout=&owner;sc.vblank_native=&pv;
	dev.dev_private=&sc;kms.sc=&sc;kms.crtc.dev=&dev;
	atomic.dev=&dev;atomic.crtc=&crtc;atomic.plane=&plane.base;
	atomic.old_plane=&old_plane.base;
	plane.frame=&frame;crtc.active=true;crtc.event=&events[0];
	native_notify=NULL;native_arg=NULL;mode=0;
	submissions=writes=sends=failures=hardware_done=cleanup=0;
	finalize_calls=legacy_calls=vblank_refs=on_calls=off_calls=0;
	handle_calls=bind_calls=client_attempts=poll_calls=0;
	last_counter=drm_frames=captured_sequence=0;old_refs=1;
	cooked_time=123456789012;captured_time=0;retire_waits=captures=0;
	config_cleanups=completion_destroys=frees=0;completion_live=true;
	blank_waits=snapshot_calls=0;blank_state=TEST_BLANK_READY;blank_error=0;
	commit_snapshot=commit_vblank_on=commit_vblank_get=commit_submitted=false;
	blank_wait_saw_snapshot=false;snapshot_sequence=blank_stale_sequence=0;
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
	if(which==13||which==16||which==20){crtc.active=false;plane.frame=NULL;}
	if(which==16){no_event=true;crtc.event=NULL;}
	if(which>=4&&which<=12)error=which==11?ETIMEDOUT:EIO;
	if(which==17||which==18||which==22)error=which==18?ETIMEDOUT:EIO;
	if(which>=17&&which<=22)old_plane.frame=&old_frame;
	if(which==19){old_plane.frame=&frame;frame.refs=2;}
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
	if(which==17||which==18)CHECK(retire_waits==1&&!old_frame.retired&&old_frame.refs==0);
	if(which==19)CHECK(retire_waits==0&&frame.refs==1);
	if(which==20||which==21||which==22)CHECK(retire_waits==1&&old_frame.retired&&old_frame.refs==0);
	CHECK(blank_waits==((which==13||which==16||which==20)?1U:0U));
	CHECK(!dev.event_lock.held&&!in_irq);
}
static void blank_readiness(void)
{
	/* Run the advancing wait first so a stale-snapshot control reaches its bug. */
	const unsigned modes[]={24,23,29,25,26,27,28,30};
	unsigned i,which;
	int error;
	for(i=0;i<sizeof(modes)/sizeof(modes[0]);i++) {
		setup();boot();which=mode=modes[i];crtc.active=false;plane.frame=NULL;
		blank_state=which==23?TEST_BLANK_CURRENT:TEST_BLANK_READY;
		error=0;
		if(which==24)blank_state=TEST_BLANK_RETIRING;
		if(which>=25&&which<=28) {
			blank_state=TEST_BLANK_RETIRING;
			error=which==25?ETIME:which==26?EIO:which==27?EINTR:EBUSY;
			blank_error=error;
		}
		if(which==30){error=EIO;kms.fault=EIO;}
		if(which==24||error!=0)old_plane.frame=&old_frame;
		kms_commit_tail(&atomic);
		CHECK(blank_waits==(which==30?0U:1U)&&finalize_calls==0);
		CHECK(!blank_wait_saw_snapshot&&snapshot_calls==(error?1U:2U));
		CHECK(hardware_done==1&&cleanup==1&&old_refs==0&&vblank_refs==0);
		CHECK(!kms.waiting&&kms.event==NULL&&crtc.event==NULL);
		CHECK(frame.refs==1&&explicit_fence.refs==1);
		if(error!=0) {
			CHECK(kms.fault==error&&failures==1&&sends==0);
			CHECK(events[0].terminal==2&&events[0].out_status==-error);
			CHECK(!commit_snapshot&&!commit_vblank_on&&!commit_vblank_get&&!commit_submitted);
			CHECK(submissions==0&&writes==0&&retire_waits==0&&captures==0);
			CHECK(old_frame.refs==0&&!old_frame.retired&&old_frame.retained);
		} else {
			CHECK(kms.fault==0&&failures==0&&sends==1&&submissions==1);
			CHECK(commit_snapshot&&commit_vblank_on&&commit_vblank_get&&commit_submitted);
			CHECK(events[0].terminal==1&&events[0].out_status==1);
			if(which==24)CHECK(kms.event_floor>blank_stale_sequence&&old_frame.retired);
		}
		CHECK(!dev.event_lock.held&&!in_irq);
	}
}
/* The helper uses the callback sample for both legacy and 64-bit event payloads. */
static void event_payloads(void)
{
	const unsigned types[]={DRM_EVENT_VBLANK,DRM_EVENT_FLIP_COMPLETE,DRM_EVENT_CRTC_SEQUENCE};
	unsigned i;
	for(i=0;i<sizeof(types)/sizeof(types[0]);i++) {
		setup();boot();mode=21;old_plane.frame=&old_frame;
		events[0].event.base.type=types[i];
		if(types[i]==DRM_EVENT_CRTC_SEQUENCE)events[0].event.seq.sequence=77;
		kms_commit_tail(&atomic);
		CHECK(sends==1&&failures==0&&retire_waits==1&&captures==1);
		CHECK(captured_time<cooked_time&&captured_sequence<drm_frames);
	}
	setup();boot();old_refs=0;hardware_done=cleanup=1;
	captured_sequence=0;captured_time=123456789012;
	events[0].event.base.type=DRM_EVENT_CRTC_SEQUENCE;events[0].event.seq.sequence=77;
	crtc.event=NULL;
	kms_complete_event(&kms,&events[0],captured_sequence,captured_time);
	CHECK(events[0].event.seq.sequence==77&&sends==1);
	kms_complete_event(&kms,NULL,0,0);CHECK(sends==1&&failures==0);
}
int main(int argc, char **argv)
{
	unsigned i;
	if(argc==2)invalid_finalization(argv[1]);
	CHECK(argc==1);(void)drm_crtc_send_vblank_event;(void)ktime_get;
	(void)drm_crtc_vblank_count_and_time;
	binding();fault_codes();finalization();blank_readiness();
	for(i=0;i<23;i++)if(i!=14)commit_case(i);
	event_payloads();
	printf("PASS actual KMS commit/IRQ/bind: %u CHECKs, bind admission, frame epoch,"
	    "blank readiness before a fresh floor, captured timestamps, retirement and terminal cleanup\n",checks);
	return 0;
}
