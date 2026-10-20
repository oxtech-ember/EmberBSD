/* Origin: EmberBSD actual-source V3D binary syncobj contract, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define BCMV3D_SYNC_CONTRACT
#include "bcm2712-v3d-csd-contract.c"

static struct drm_file *active_file;
static struct bcmv3d_job *active_job;
static struct dma_fence *old_fence;
static bool close_on_wait;
static unsigned signals;

static void inspect_wait(void)
{
    CHECK(active_job->lock==0); /* A producer must be able to enter. */
    CHECK(active_file->syncs[1]->fence==old_fence);
    if(close_on_wait) {
        drm_syncobj_put(active_file->syncs[1]); active_file->syncs[1]=NULL;
    }
}
static void inspect_signal(struct dma_fence *f)
{
    struct bcmv3d_job *j=active_job;
    signals++;
    if(j->poisoned) { CHECK(f->status<0); return; }
    CHECK(revoked && !src_owned && !dst_owned && !csd_owned);
    for(unsigned i=0;i<j->nbo;i++)CHECK(!j->bos[i]->busy);
    if(j->src)CHECK(!j->src->busy && !j->dst->busy);
}
static void setup_sync(struct bcmv3d_device *c,struct drm_file *f)
{
    static const struct dma_fence_ops ops={.use_64bit_seqno=true};
    active_file=f; active_job=c->job;
    CHECK(!fence_count && !sync_count);
    struct drm_syncobj *s=calloc(1,sizeof(*s)); CHECK(s != NULL); s->refs=1;
    s->fence=calloc(1,sizeof(*s->fence)); CHECK(s->fence != NULL);
    dma_fence_init(s->fence,&ops,NULL,999,1); s->fence->status=1;
    f->syncs[1]=s; sync_count++; old_fence=s->fence;
    on_wait=inspect_wait; on_signal=inspect_signal;
    signals=0; close_on_wait=false; input_wait_result=1;
}
static void cleanup_sync(struct drm_file *f)
{
    if(f->syncs[1]) { drm_syncobj_put(f->syncs[1]); f->syncs[1]=NULL; }
    CHECK(!sync_count); on_wait=NULL; on_signal=NULL;
}
#ifndef BCMV3D_CL_CONTRACT
int main(void)
{
    struct bcmv3d_device c; struct drm_device d; struct drm_file f;
    uint32_t handles[2]; struct drm_v3d_submit_csd a;
    for(unsigned mode=0;mode<8;mode++) {
        a=csd_setup(&c,&d,&f,handles); setup_sync(&c,&f);
        a.in_sync=a.out_sync=1;
        if(mode==1) { old_fence->status=0; input_wait_result=0; }
        if(mode==2)old_fence->status=-EIO;
        if(mode==3)fail_fence_alloc=true;
        if(mode==4)close_on_wait=true;
        if(mode==5)model_case=1;
        if(mode==6) { old_fence->status=0; input_wait_result=-EINTR; }
        if(mode==7)a.out_sync=3;
        int error=bcmv3d_submit_csd(&d,&a,&f);
        int expected[]={0,-ETIME,-EIO,-ENOMEM,0,-EIO,-EINTR,-ENOENT};
        CHECK(error==expected[mode]);
        if(mode==0)CHECK(f.syncs[1]->fence!=old_fence && f.syncs[1]->fence->status==1);
        if(mode==0 || mode==4 || mode==5)CHECK(signals==1);
        else CHECK(signals==0 && csd_starts==0 && f.syncs[1]->fence==old_fence);
        fail_fence_alloc=false; cleanup_sync(&f); csd_cleanup(&c,&f,mode==5);
    }
    struct drm_v3d_submit_tfu t=setup(&c,&d,&f); setup_sync(&c,&f);
    t.in_sync=t.out_sync=1;
    CHECK(bcmv3d_submit_tfu(&d,&t,&f)==0 && signals==1);
    CHECK(f.syncs[1]->fence->status==1);
    cleanup_sync(&f); cleanup(&c,&f,false);
    printf("PASS: %u binary syncobj ownership checks\n",checks);
    return 0;
}
#endif
