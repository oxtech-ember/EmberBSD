/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Always-active native lifetime controls. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)
#define KASSERT(x) CHECK(x)
#define CTASSERT(x) _Static_assert(x, #x)
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define GFP_KERNEL 0
#define DRM_FORMAT_XRGB8888 UINT32_C(0x34325258)
#define DRM_FORMAT_MOD_LINEAR UINT64_C(0)
typedef unsigned int u_int;
typedef void *bus_dma_tag_t;
struct segment { uint64_t ds_addr, ds_len; };
struct dmamap { int dm_nsegs; size_t dm_mapsize; struct segment dm_segs[2]; };
typedef struct dmamap *bus_dmamap_t;
struct spinlock { bool locked, live; };
typedef struct spinlock spinlock_t;
struct dma_fence;
struct dma_fence_ops {
    bool use_64bit_seqno;
    const char *(*get_driver_name)(struct dma_fence *);
    const char *(*get_timeline_name)(struct dma_fence *);
    bool (*enable_signaling)(struct dma_fence *);
    void (*release)(struct dma_fence *);
};
struct dma_fence {
    unsigned refs;
    int status;
    uint64_t context, seqno;
    spinlock_t *lock;
    const struct dma_fence_ops *ops;
};
struct dma_resv {
    bool locked;
    struct dma_fence *writer;
    struct dma_fence *readers[128];
    unsigned nreaders;
};
struct drm_gem_object { size_t size; struct dma_resv *resv; unsigned refs; };
struct drm_gem_cma_object {
    struct drm_gem_object base;
    bus_dma_tag_t dmat;
    bus_dmamap_t dmamap;
    size_t dmasize;
};
#include "sun60i_a733_output.h"
#include "sun60i_a733_de352_scanout.h"
#include "sun60i_a733_drm_frame.h"
struct sun60i_a733_de352_client { bool quarantine; };
static struct dma_resv resv;
static struct drm_gem_cma_object object;
static struct sun60i_a733_de352_frame active, uncertain;
static struct dma_fence writers[40];
static int tag, foreign_tag;
static int lock_error, reserve_error, provider_mode, wait_mode;
static bool fail_alloc;
static unsigned allocations, frees, provider_calls, waits, locks, signals;
static void *kzalloc(size_t n, int flags) {
    (void)flags;
    if (fail_alloc) return NULL;
    void *p=calloc(1,n); CHECK(p); allocations++; return p;
}
static void spin_lock_init(spinlock_t *p) { p->locked=false; p->live=true; }
static void spin_lock(spinlock_t *p) { CHECK(p->live && !p->locked); p->locked=true; }
static void spin_unlock(spinlock_t *p) { CHECK(p->live && p->locked); p->locked=false; }
static void spin_lock_destroy(spinlock_t *p) { CHECK(p->live && !p->locked); p->live=false; }
static uint64_t dma_fence_context_alloc(unsigned count) {
    static uint64_t next=UINT64_C(0x100000000); CHECK(count==1); return next++;
}
static void dma_fence_init(struct dma_fence *f, const struct dma_fence_ops *ops,
    spinlock_t *lock, uint64_t context, uint64_t seqno) {
    f->refs=1; f->ops=ops; f->lock=lock; f->context=context; f->seqno=seqno;
}
static struct dma_fence *dma_fence_get(struct dma_fence *f) {
    if (f) { CHECK(f->refs); f->refs++; } return f;
}
static void dma_fence_put(struct dma_fence *f) {
    if (f) { CHECK(f->refs); if (--f->refs==0 && f->ops) f->ops->release(f); }
}
static void dma_fence_free(struct dma_fence *f) {
    CHECK(!f->refs && !f->lock->live); frees++; free(f);
}
static int dma_fence_get_status(struct dma_fence *f) { CHECK(f->refs); return f->status; }
static int dma_fence_signal_locked(struct dma_fence *f) {
    CHECK(f->lock->locked && !f->status); f->status=1; signals++; return 0;
}
static void drm_gem_object_get(struct drm_gem_object *o) { CHECK(o->refs); o->refs++; }
static void drm_gem_object_put_unlocked(struct drm_gem_object *o) {
    CHECK(!resv.locked && o->refs>1); o->refs--;
}
static int dma_resv_lock_interruptible(struct dma_resv *r, void *ctx) {
    (void)ctx; CHECK(!r->locked); locks++; if (lock_error) return -lock_error;
    r->locked=true; return 0;
}
static void dma_resv_unlock(struct dma_resv *r) { CHECK(r->locked); r->locked=false; }
static struct dma_fence *dma_resv_get_excl(struct dma_resv *r) {
    CHECK(r->locked); return r->writer;
}
static int dma_resv_reserve_shared(struct dma_resv *r, unsigned count) {
    CHECK(r->locked && count==1); return -reserve_error;
}
static void dma_resv_add_shared_fence(struct dma_resv *r, struct dma_fence *f) {
    CHECK(r->locked && (!r->writer || r->writer->status>0));
    CHECK(r->nreaders<128 && f->status==0);
    r->readers[r->nreaders++]=dma_fence_get(f);
}
static long msecs_to_jiffies(unsigned ms) { CHECK(ms==1000); return 100; }
static long dma_fence_wait_timeout(struct dma_fence *f, bool intr, long timeout) {
    CHECK(!resv.locked && intr && timeout==100 && f->status==0);
    waits++;
    if (wait_mode==1) return -EINTR;
    if (wait_mode==2) return 0;
    f->status=wait_mode==5 ? -EIO : 1;
    if ((wait_mode==3 && waits==1) || wait_mode==4) {
        CHECK(waits<40);
        resv.writer=&writers[waits];
    }
    return 99;
}
#include "sun60i_a733_drm_frame.c"
int sun60i_a733_de352_submit(struct sun60i_a733_de352_client *client,
    const struct sun60i_a733_de352_frame *frame,
    struct sun60i_a733_de352_completion *done) {
    CHECK(!resv.locked && !client->quarantine); provider_calls++;
    struct a733_scanout *s=frame->cookie;
    CHECK(!s->fence.status && s->gem==&object.base && object.base.refs>=2);
    CHECK(resv.nreaders && resv.readers[resv.nreaders-1]==&s->fence);
    CHECK(frame->dmat==&tag && frame->map==object.dmamap);
    if (provider_mode==1) return EIO;
    if (provider_mode==5) { *done=(struct sun60i_a733_de352_completion){7,100}; return 0; }
    CHECK(frame->retain(frame->cookie)==0);
    if (provider_mode==2) { frame->release(frame->cookie); return EIO; }
    if (provider_mode==3) { uncertain=*frame; client->quarantine=true; return EIO; }
    if (active.cookie) active.release(active.cookie);
    active=*frame;
    *done=(struct sun60i_a733_de352_completion){7,100}; return 0;
}
static void clear_readers(void) {
    for (unsigned i=0;i<resv.nreaders;i++) {
        CHECK(resv.readers[i]->status>0); dma_fence_put(resv.readers[i]);
    }
    resv.nreaders=0;
}
static void retire_active(void) {
    CHECK(active.cookie); active.release(active.cookie); memset(&active,0,sizeof(active));
}
static struct dmamap map;
static struct sun60i_a733_de352_state state;
static struct sun60i_a733_drm_frame desc;
static struct sun60i_a733_de352_client client;
static void reset(void) {
    CHECK(!active.cookie && !uncertain.cookie && !resv.nreaders && !resv.locked);
    memset(&resv,0,sizeof(resv)); memset(&client,0,sizeof(client));
    map=(struct dmamap){1,640*480*4,{{0x100000,640*480*4},{0,0}}};
    object=(struct drm_gem_cma_object){{640*480*4,&resv,1},&tag,&map,640*480*4};
    state=(struct sun60i_a733_de352_state){.mode={.hdisplay=640,.vdisplay=480},
        .dmat=&tag,.max_dma_address=UINT32_MAX};
    desc=(struct sun60i_a733_drm_frame){DRM_FORMAT_XRGB8888,640,480,2560,0,0};
    lock_error=reserve_error=provider_mode=wait_mode=0; fail_alloc=false;
    provider_calls=waits=locks=0;
    for (unsigned i=0;i<40;i++) writers[i]=(struct dma_fence){.refs=1};
}
static void expect_failure(int expected) {
    struct sun60i_a733_de352_completion done={123,456}, before=done;
    CHECK(sun60i_a733_drm_frame_submit(&client,&state,&object,&desc,&done)==expected);
    CHECK(memcmp(&done,&before,sizeof(done))==0);
    CHECK(object.base.refs==1 && !active.cookie && !uncertain.cookie);
    clear_readers(); CHECK(allocations==frees);
}
int main(void) {
    reset();
    for (unsigned invalid=0;invalid<18;invalid++) {
        reset(); unsigned calls=allocations;
        switch (invalid) {
        case 0: desc.format=0; break;
        case 1: desc.modifier=1; break;
        case 2: desc.offset=64; break;
        case 3: desc.width=0; break;
        case 4: desc.height=1081; break;
        case 5: desc.width=639; break;
        case 6: desc.pitch=2559; break;
        case 7: desc.pitch=2564; break;
        case 8: object.dmat=&foreign_tag; break;
        case 9: object.dmamap=NULL; break;
        case 10: map.dm_nsegs=2; break;
        case 11: map.dm_mapsize--; break;
        case 12: object.base.size--; break;
        case 13: object.dmasize--; break;
        case 14: map.dm_segs[0].ds_len--; break;
        case 15: map.dm_segs[0].ds_addr=UINT64_C(0x100000000); break;
        case 16: map.dm_segs[0].ds_addr=UINT32_MAX-63; break;
        default: map.dm_segs[0].ds_addr++; break;
        }
        struct sun60i_a733_de352_completion done={123,456}, before=done;
        CHECK(sun60i_a733_drm_frame_submit(&client,&state,&object,&desc,&done)!=0);
        CHECK(memcmp(&done,&before,sizeof(done))==0);
        CHECK(allocations==calls && !provider_calls && !locks && object.base.refs==1);
    }
    reset(); fail_alloc=true; expect_failure(ENOMEM);
    reset(); lock_error=EINTR; expect_failure(EINTR);
    reset(); reserve_error=ENOMEM; expect_failure(ENOMEM);
    for (unsigned mode=1;mode<=5;mode++) {
        if (mode==3) continue;
        reset(); wait_mode=(int)mode; resv.writer=&writers[0];
        expect_failure(mode==1 ? EINTR : mode==2 ? ETIMEDOUT : mode==4 ? EAGAIN : EIO);
        CHECK(!provider_calls && (mode!=4 || waits==32));
    }
    reset(); resv.writer=&writers[0]; writers[0].status=-EIO;
    expect_failure(EIO); CHECK(!waits && !provider_calls);
    reset(); wait_mode=3; resv.writer=&writers[0];
    struct sun60i_a733_de352_completion done={0};
    CHECK(sun60i_a733_drm_frame_submit(&client,&state,&object,&desc,&done)==0);
    CHECK(waits==2 && done.observed_sequence==7 && object.base.refs==2);
    CHECK(resv.readers[0]->status==0 && resv.readers[0]->refs==2);
    retire_active(); CHECK(object.base.refs==1 && resv.readers[0]->status==1);
    /* The retired reservation reference does not pin its own GEM object. */
    CHECK(resv.readers[0]->refs==1); clear_readers(); CHECK(allocations==frees);
    for (unsigned mode=1;mode<=2;mode++) { reset(); provider_mode=(int)mode; expect_failure(EIO); }
    reset();
    CHECK(sun60i_a733_drm_frame_submit(&client,&state,&object,&desc,&done)==0);
    struct dma_fence *old=resv.readers[0];
    CHECK(sun60i_a733_drm_frame_submit(&client,&state,&object,&desc,&done)==0);
    CHECK(old->status==1 && resv.readers[1]->status==0 && object.base.refs==2);
    provider_mode=3;
    struct sun60i_a733_de352_completion before=done;
    CHECK(sun60i_a733_drm_frame_submit(&client,&state,&object,&desc,&done)==EIO);
    CHECK(memcmp(&done,&before,sizeof(done))==0 && client.quarantine);
    CHECK(object.base.refs==3 && resv.readers[1]->status==0 && resv.readers[2]->status==0);
    /* Fixture-only isolated reboot cleanup. Production has no such release. */
    retire_active(); uncertain.release(uncertain.cookie); memset(&uncertain,0,sizeof(uncertain));
    clear_readers(); CHECK(object.base.refs==1 && allocations==frees);
    reset(); provider_mode=5; expect_failure(EPROTO);
    printf("A733 DRM frame: %u checks PASS\n",checks);
    return 0;
}
