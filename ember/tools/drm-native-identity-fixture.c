/* Origin: EmberBSD; AI-assisted native DRM boundary fixtures. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "drm_native_identity.h"
#define __NetBSD__ 1
#define KASSERT assert
#define CTASSERT(x) _Static_assert(x, #x)
#undef __arraycount
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
#define KM_SLEEP 0
#define CTLFLAG_PERMANENT 1
#define CTLTYPE_NODE 1
#define CTLTYPE_STRUCT 2
#define CTL_HW 6
#define CTL_CREATE -3
#define CTL_EOL -1
#define SYSCTL_DESCR(s) (s)
#define NODEVMAJOR -1
typedef int devmajor_t;
struct sysctlnode { int unused; };
struct sysctllog { unsigned n; int minor[2]; };
struct drm_native_identity;
struct drm_minor { int index; };
struct drm_device;
struct drm_driver {
    int (*load)(struct drm_device *, unsigned long);
    void (*unload)(struct drm_device *);
    const char *name, *date; int major, minor, patchlevel;
};
struct drm_device {
    struct drm_native_identity *native_identity;
    struct drm_native_pci_record native_pci;
    struct drm_native_platform_record native_platform;
    struct drm_minor *primary, *render;
    struct drm_driver *driver;
    bool registered;
};
static const int drm_cdevsw;
static int cdevsw_lookup_major(const int *p) { assert(p==&drm_cdevsw); return 180; }
static int allocated, create_count, fail_at, load_error;
static bool active[192];
static struct drm_native_pci_record *leaves[192];
static struct drm_native_platform_record *platform_leaves[192];
static pthread_rwlock_t tree=PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready=PTHREAD_COND_INITIALIZER;
static bool teardown_waiting;
static void *kmem_zalloc(size_t n, int flags) { assert(!flags); allocated++; return calloc(1,n); }
static void kmem_free(void *p, size_t n) {
    (void)n;
    for (unsigned i=0; i<192; i++) assert(leaves[i]==NULL && platform_leaves[i]==NULL);
    allocated--; free(p);
}
static int sysctl_createv(struct sysctllog **log, int cf,
    const struct sysctlnode **parent, const struct sysctlnode **result,
    int flags, int type, const char *name, const char *desc,
    void *func, uint64_t value, void *data, size_t len, ...)
{
    static const struct sysctlnode root;
    int minor;
    (void)desc;
    assert(!cf && !func && !value);
    if (++create_count==fail_at) return ENOMEM;
    if (type==CTLTYPE_NODE) {
        assert(!log && !parent && flags==CTLFLAG_PERMANENT && !data && !len);
        *result=&root; return 0;
    }
    assert(type==CTLTYPE_STRUCT && parent && *parent==&root && !flags);
bool platform = strncmp(name,"platform",8)==0;
assert(len==(platform ? 160U : 68U));
assert(sscanf(name,platform ? "platform%d" : "identity%d",&minor)==1 && active[minor]);
    assert(pthread_rwlock_wrlock(&tree)==0);
    assert(!leaves[minor] && !platform_leaves[minor]);
    if (!*log) *log=calloc(1,sizeof(**log));
    (*log)->minor[(*log)->n++]=minor;
    if (platform) platform_leaves[minor]=data; else leaves[minor]=data;
    assert(pthread_rwlock_unlock(&tree)==0);
    return 0;
}
static void sysctl_teardown(struct sysctllog **log)
{
    assert(pthread_mutex_lock(&gate)==0);
    teardown_waiting=true;
    assert(pthread_cond_broadcast(&ready)==0);
    assert(pthread_mutex_unlock(&gate)==0);
    assert(pthread_rwlock_wrlock(&tree)==0);
    if (*log) {
        for (unsigned i=0; i<(*log)->n; i++) { leaves[(*log)->minor[i]]=NULL; platform_leaves[(*log)->minor[i]]=NULL; }
        free(*log); *log=NULL;
    }
    assert(pthread_rwlock_unlock(&tree)==0);
}
#define DRM_MINOR_PRIMARY 0
#define DRM_MINOR_RENDER 2
#define DRIVER_LEGACY 1
#define DRIVER_MODESET 2
#define DRM_INFO(...) ((void)driver)
static int drm_minor_register(struct drm_device *d, unsigned type) {
    struct drm_minor *m= type==DRM_MINOR_PRIMARY ? d->primary : d->render;
    if (m) active[m->index]=true;
    return 0;
}
static void drm_minor_unregister(struct drm_device *d, unsigned type) {
    struct drm_minor *m= type==DRM_MINOR_PRIMARY ? d->primary : d->render;
    if (m) { assert(!leaves[m->index] && !platform_leaves[m->index]); active[m->index]=false; }
}
static int create_compat_control_link(struct drm_device *d) { (void)d; return 0; }
static void remove_compat_control_link(struct drm_device *d) { (void)d; }
static bool drm_core_check_feature(struct drm_device *d, int f) { (void)d; return f==DRIVER_MODESET; }
static void drm_modeset_register_all(struct drm_device *d) { assert(d->registered); }
static void drm_modeset_unregister_all(struct drm_device *d) { assert(!d->native_identity); }
static void drm_client_dev_unregister(struct drm_device *d) { assert(!d->native_identity); }
static void drm_legacy_rmmaps(struct drm_device *d) { assert(!d->native_identity); }
static void drm_lastclose(struct drm_device *d) { (void)d; }
static int load(struct drm_device *d, unsigned long f) { assert(d->registered && !f); return load_error; }
static void unload(struct drm_device *d) { assert(!d->native_identity); }
void drm_dev_unregister(struct drm_device *);
static void *unregister_thread(void *p) { drm_dev_unregister(p); return NULL; }
#define DTYPE_MISC 3
struct fileops { int unused; };
static const struct fileops sync_file_ops, foreign_ops;
struct dma_fence { int refs; };
struct sync_file { struct dma_fence *sf_fence; };
struct file { int f_type; const struct fileops *f_ops; void *f_data; };
static struct file files[1];
static int held;
static struct file *fd_getfile(int fd) { if (fd!=0) return NULL; held++; return &files[0]; }
static void fd_putfile(int fd) { assert(fd==0 && held==1); held--; }
static struct dma_fence *dma_fence_get(struct dma_fence *f) {
    assert(held==1); if (f) f->refs++; return f;
}
