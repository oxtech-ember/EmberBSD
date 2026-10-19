/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Actual atomic admission and worker checks. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); } } while (0)
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define KM_SLEEP 0
#define DRM_FORMAT_XRGB8888 1
#define DRM_FORMAT_MOD_LINEAR 0
#define DRM_MODE_ROTATE_0 1
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
struct work { int unused; };
struct completion { bool initialized, done; };
struct dma_fence { long wait; int status; };
struct drm_format { uint32_t format; };
struct drm_framebuffer { const struct drm_format *format; uint64_t modifier; uint32_t width,height,pitches[4],offsets[4]; };
struct drm_plane { int unused; };
struct drm_crtc { int unused; };
struct drm_connector { int unused; };
struct drm_display_mode { unsigned valid; };
struct drm_plane_state { struct drm_framebuffer *fb; struct drm_crtc *crtc; struct dma_fence *fence; uint32_t src_x,src_y,src_w,src_h,crtc_x,crtc_y,crtc_w,crtc_h,rotation; };
struct drm_crtc_state { bool enable,active; struct drm_display_mode mode; void *event; };
struct drm_connector_state { struct drm_crtc *crtc; };
struct drm_atomic_state { bool async_update; unsigned refs; struct drm_plane_state plane; struct drm_crtc_state crtc; struct drm_connector_state connector; };
struct drm_device { void *dev_private; };
struct sun60i_a733_output_mode { uint32_t hdisplay,vdisplay; };
struct sun60i_a733_de352_state { struct sun60i_a733_output_mode mode; };
struct sun60i_a733_de352_completion { uint64_t observed_sequence,observed_time_ns; };
struct sun60i_a733_drm_frame { uint32_t format,width,height,pitch,offset; uint64_t modifier; };
struct drm_gem_cma_object { unsigned refs; };
struct a733_drm_fb { struct drm_framebuffer base; struct drm_gem_cma_object *object; };
struct a733_drm { struct drm_device *dev; struct drm_plane plane; struct drm_crtc crtc; struct drm_connector connector; struct sun60i_a733_de352_state native; bool lock,busy; int fault; void *client,*queue; };
struct a733_commit { struct work work; struct completion done; struct drm_atomic_state *state; bool nonblock; };
static struct a733_drm *current;
static struct work *pending;
static unsigned allocations,completions,setup_calls,prepare_calls,swap_calls,queued,submitted,waits,events,hw_done,cleaned,cleanup_done,state_puts,retained_reads;
static int setup_error,prepare_error,swap_error,submit_error,check_error,lookup_error,event_error;
static unsigned lookup_step;
static bool modeset_lock,fail_before_pointer;
static char order[32]; static size_t order_length;
static void step(char c) { CHECK(order_length+1<sizeof(order));order[order_length++]=c;order[order_length]=0; }
static void mutex_enter(bool *p) { CHECK(!*p);*p=true; }
static void mutex_exit(bool *p) { CHECK(*p);*p=false; }
static void *kmem_zalloc(size_t n,int flags) { CHECK(flags==KM_SLEEP);void *p=calloc(1,n);CHECK(p);allocations++;return p; }
static void kmem_free(void *p,size_t n) { CHECK(p&&n&&allocations);allocations--;free(p); }
static void init_completion(struct completion *c) { CHECK(!c->initialized);c->initialized=true;completions++; }
static void destroy_completion(struct completion *c) { CHECK(c->initialized&&completions);c->initialized=false;completions--; }
static void complete_all(struct completion *c) { CHECK(c->initialized&&!c->done);c->done=true; }
static unsigned msecs_to_jiffies(unsigned n) { CHECK(n==1000);return 100; }
static long dma_fence_wait_timeout(struct dma_fence *f,bool intr,unsigned ticks) { CHECK(!current->lock&&!modeset_lock&&!intr&&ticks==100);waits++;return f->wait; }
static int dma_fence_get_status(struct dma_fence *f) { return f->status; }
static struct drm_plane_state *drm_atomic_get_new_plane_state(struct drm_atomic_state *s,struct drm_plane *p) { CHECK(p==&current->plane);return &s->plane; }
static struct drm_crtc_state *drm_atomic_get_new_crtc_state(struct drm_atomic_state *s,struct drm_crtc *p) { CHECK(p==&current->crtc);return &s->crtc; }
static int sun60i_a733_drm_frame_submit(void *client,const struct sun60i_a733_de352_state *native,struct drm_gem_cma_object *object,const struct sun60i_a733_drm_frame *f,struct sun60i_a733_de352_completion *done) {
 CHECK(!current->lock&&!modeset_lock&&client==current->client&&native==&current->native);
 CHECK(object&&f->format==DRM_FORMAT_XRGB8888&&f->width==640&&f->height==480&&f->pitch==2560&&f->offset==0&&f->modifier==0);
 submitted++;step('S');if(!fail_before_pointer){retained_reads++;object->refs++;}
 if(!submit_error){done->observed_sequence=17;done->observed_time_ns=UINT64_C(123456789000);}
 return submit_error;
}
static void a733_commit_event(struct a733_drm *sc,struct drm_crtc_state *s,const struct sun60i_a733_de352_completion *done,int error) {
 CHECK(sc==current&&!sc->lock&&!modeset_lock);events++;event_error=error;s->event=NULL;step('E');
 if(!error)CHECK(done->observed_time_ns==UINT64_C(123456789000));
}
static int drm_atomic_helper_setup_commit(struct drm_atomic_state *s,bool nb) { (void)nb;CHECK(s->refs==1&&current->busy);setup_calls++;return setup_error; }
static int drm_atomic_helper_prepare_planes(struct drm_device *dev,struct drm_atomic_state *s) { CHECK(dev==current->dev&&s->refs==1);prepare_calls++;return prepare_error; }
static int drm_atomic_helper_swap_state(struct drm_atomic_state *s,bool wait) { CHECK(!wait&&s->refs==1&&current->busy);swap_calls++;return swap_error; }
static void drm_atomic_state_get(struct drm_atomic_state *s) { CHECK(s->refs==1);s->refs++; }
static void drm_atomic_helper_commit_hw_done(struct drm_atomic_state *s) { CHECK(current->busy&&s->crtc.event==NULL&&s->refs==2);hw_done++;step('H'); }
static void drm_atomic_helper_cleanup_planes(struct drm_device *dev,struct drm_atomic_state *s) { CHECK(dev==current->dev&&current->busy);(void)s;cleaned++;step('C'); }
static void drm_atomic_helper_commit_cleanup_done(struct drm_atomic_state *s) { CHECK(current->busy&&s->refs==2&&hw_done==1);cleanup_done++;step('D'); }
static void drm_atomic_state_put(struct drm_atomic_state *s) { CHECK(current->busy&&s->refs==2&&cleanup_done==1);s->refs--;state_puts++;step('P'); }
static void workqueue_enqueue(void *q,struct work *w,void *cpu) { CHECK(q==current->queue&&!cpu&&!pending&&current->busy);pending=w;queued++; }
void a733_drm_work(struct work *,void *);
static void run_worker(void) { CHECK(pending);struct work *w=pending;pending=NULL;a733_drm_work(w,current); }
static void wait_for_completion(struct completion *c) { CHECK(c->initialized&&!c->done&&pending);bool held=modeset_lock;modeset_lock=false;run_worker();modeset_lock=held;CHECK(c->done); }
static struct drm_crtc_state *drm_atomic_get_crtc_state(struct drm_atomic_state *s,struct drm_crtc *p) { CHECK(p==&current->crtc);lookup_step++;return lookup_error&&lookup_step==1?ERR_PTR(lookup_error):&s->crtc; }
static struct drm_plane_state *drm_atomic_get_plane_state(struct drm_atomic_state *s,struct drm_plane *p) { CHECK(p==&current->plane);lookup_step++;return lookup_error&&lookup_step==2?ERR_PTR(lookup_error):&s->plane; }
static struct drm_connector_state *drm_atomic_get_connector_state(struct drm_atomic_state *s,struct drm_connector *p) { CHECK(p==&current->connector);lookup_step++;return lookup_error&&lookup_step==3?ERR_PTR(lookup_error):&s->connector; }
static bool a733_drm_mode_matches(const struct sun60i_a733_output_mode *n,const struct drm_display_mode *m) { CHECK(n==&current->native.mode);return m->valid==1; }
static int drm_atomic_helper_check(struct drm_device *d,struct drm_atomic_state *s) { CHECK(d==current->dev&&s->refs==1);return check_error; }
#include "worker.c"
static void reset(struct a733_drm *sc,struct drm_device *dev,struct drm_atomic_state *state,struct a733_drm_fb *fb,struct drm_gem_cma_object *object) {
 CHECK(!allocations&&!completions&&!pending);memset(sc,0,sizeof(*sc));memset(state,0,sizeof(*state));
 *dev=(struct drm_device){sc};sc->dev=dev;sc->native.mode=(struct sun60i_a733_output_mode){640,480};sc->queue=sc;sc->client=dev;current=sc;
 static const struct drm_format format={DRM_FORMAT_XRGB8888};
 *fb=(struct a733_drm_fb){{&format,0,640,480,{2560},{0}},object};object->refs=1;
 state->refs=1;state->crtc=(struct drm_crtc_state){true,true,{1},state};
 state->plane=(struct drm_plane_state){.fb=&fb->base,.crtc=&sc->crtc,.src_w=640u<<16,.src_h=480u<<16,.crtc_w=640,.crtc_h=480,.rotation=1};state->connector.crtc=&sc->crtc;
 setup_calls=prepare_calls=swap_calls=queued=submitted=waits=events=hw_done=cleaned=cleanup_done=state_puts=retained_reads=0;
 setup_error=prepare_error=swap_error=submit_error=check_error=lookup_error=event_error=0;lookup_step=0;fail_before_pointer=false;modeset_lock=false;order_length=0;order[0]=0;
}
int main(void) {
 struct a733_drm sc;struct drm_device dev;struct drm_atomic_state state;struct a733_drm_fb fb;struct drm_gem_cma_object object;
 for(unsigned nb=0;nb<2;nb++)for(unsigned scenario=0;scenario<11;scenario++){
  reset(&sc,&dev,&state,&fb,&object);struct dma_fence fence={1,1};state.plane.fence=&fence;
  int expected=0;bool admission=false;
  switch(scenario){case 1:setup_error=-ENOMEM;expected=ENOMEM;admission=true;break;case 2:prepare_error=-ENOMEM;expected=ENOMEM;admission=true;break;case 3:swap_error=-EIO;expected=EIO;admission=true;break;case 4:fence.wait=0;expected=ETIMEDOUT;break;case 5:fence.wait=-EINTR;expected=EINTR;break;case 6:fence.status=-EIO;expected=EIO;break;case 7:fence.status=0;expected=EIO;break;case 8:submit_error=EIO;expected=EIO;break;case 9:submit_error=ENOMEM;expected=ENOMEM;fail_before_pointer=true;break;case 10:state.plane.fence=NULL;break;}
  modeset_lock=true;CHECK(a733_drm_atomic_commit(&dev,&state,nb)==(admission?-expected:0));modeset_lock=false;
  if(!admission&&nb){CHECK(sc.busy&&pending&&state.refs==2);CHECK(a733_drm_atomic_commit(&dev,&state,true)==-EBUSY);CHECK(queued==1);run_worker();}
  CHECK(!sc.busy&&!allocations&&!completions&&!pending&&state.refs==1&&!sc.lock);
  if(admission){CHECK(!queued&&!events&&!submitted&&!sc.fault&&!state_puts);CHECK(cleaned==(scenario==3));}
  else{CHECK(queued==1&&events==1&&event_error==expected&&sc.fault==expected);CHECK(hw_done==1&&cleaned==1&&cleanup_done==1&&state_puts==1);CHECK(strcmp(order,submitted?"SEHCDP":"EHCDP")==0);CHECK(retained_reads==((scenario==0||scenario==8||scenario==10)?1u:0u));CHECK(object.refs==1+retained_reads);if(expected)CHECK(a733_drm_atomic_commit(&dev,&state,true)==-expected);}
 }
 for(unsigned bad=0;bad<17;bad++){
  reset(&sc,&dev,&state,&fb,&object);int expected=-EINVAL;
  switch(bad){case 0:expected=0;break;case 1:state.async_update=true;expected=-EOPNOTSUPP;break;case 2:state.crtc.active=false;expected=-EOPNOTSUPP;break;case 3:state.crtc.enable=false;expected=-EOPNOTSUPP;break;case 4:state.plane.fb=NULL;expected=-EOPNOTSUPP;break;case 5:state.plane.crtc=NULL;expected=-EOPNOTSUPP;break;case 6:state.connector.crtc=NULL;expected=-EOPNOTSUPP;break;case 7:state.crtc.mode.valid=0;break;case 8:state.plane.src_x=1;break;case 9:state.plane.src_w--;break;case 10:state.plane.crtc_h++;break;case 11:state.plane.rotation=2;break;case 12:fb.base.modifier=4;break;case 13:fb.base.offsets[0]=4;break;case 14:check_error=-EDEADLK;expected=-EDEADLK;break;case 15:sc.fault=EIO;expected=-EIO;break;case 16:lookup_error=-ENOMEM;expected=-ENOMEM;break;}
  CHECK(a733_drm_atomic_check(&dev,&state)==expected);CHECK(!queued&&!submitted&&!sc.busy&&!allocations);
 }
 reset(&sc,&dev,&state,&fb,&object);state.async_update=true;CHECK(a733_drm_atomic_commit(&dev,&state,true)==-EOPNOTSUPP&&!allocations&&!sc.busy);
 printf("PASS %u bounded worker and atomic admission checks\n",checks);return 0;
}
