/* Origin: EmberBSD actual-source DRM wait locking contract, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define DRM_UNLOCKED 1
#define DRM_RENDER_ALLOW 2
#define DRIVER_MODESET 4
#define ISSET(v,b) ((v)&(b))
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
struct drm_device { unsigned features; };
struct drm_file { int dummy; };
struct drm_ioctl_desc { unsigned flags; int (*func)(struct drm_device *,void *,struct drm_file *); };
static int drm_global_mutex, entered;
static int drm_core_check_feature(struct drm_device *d,unsigned f) { return d->features&f; }
static void drm_ioctl_enter(struct drm_device *d) { CHECK(!entered); entered=1; }
static void drm_ioctl_exit(struct drm_device *d) { CHECK(entered); entered=0; }
static void mutex_lock(int *m) { CHECK(!*m); *m=1; }
static void mutex_unlock(int *m) { CHECK(*m); *m=0; }
static int drm_syncobj_wait_ioctl(struct drm_device *d,void *a,struct drm_file *f)
{
    /* SIGNAL/create/close must be able to enter while this ioctl sleeps. */
    CHECK(entered && !drm_global_mutex);
    return 0;
}
static int drm_syncobj_timeline_wait_ioctl(struct drm_device *d,void *a,struct drm_file *f)
{ return drm_syncobj_wait_ioctl(d,a,f); }
static int drm_syncobj_transfer_ioctl(struct drm_device *d,void *a,struct drm_file *f)
{ return drm_syncobj_wait_ioctl(d,a,f); }
static int legacy_ioctl(struct drm_device *d,void *a,struct drm_file *f)
{ CHECK(entered && drm_global_mutex); return 0; }
#define DRM_IOCTL_DEF(cmd,fn,flags) {flags,fn}
static const struct drm_ioctl_desc waits[]={
#include "wait-descriptors.h"
};
static void dispatch(struct drm_device *dev,const struct drm_ioctl_desc *ioctl)
{
    const bool is_driver_ioctl=false;
    struct drm_file *file=NULL;
    void *data0=NULL;
    int error=1;
#include "dispatch-body.h"
    CHECK(!error && !entered && !drm_global_mutex);
}
int main(void)
{
    CHECK(sizeof(waits)/sizeof(waits[0])==3);
    struct drm_device dev={0};
    for(unsigned mode=0;mode<2;mode++) {
        dev.features=mode?DRIVER_MODESET:0;
        for(unsigned i=0;i<3;i++) {
            CHECK(waits[i].flags&DRM_RENDER_ALLOW);
            dispatch(&dev,&waits[i]);
        }
        const struct drm_ioctl_desc legacy={0,legacy_ioctl};
        dispatch(&dev,&legacy);
    }
    puts("PASS: native binary/timeline waits and transfer release the legacy DRM mutex; other core ioctls retain it");
    return 0;
}
