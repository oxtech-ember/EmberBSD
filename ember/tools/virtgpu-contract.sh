#!/bin/sh
# Origin: EmberBSD production VirtGPU queue and memory contracts, 2026-10-06.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/virtgpu-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
extract() {
    awk -v name="$1" -v prefix="$3" '
        $0 ~ "^((static )?(void|int) )?" name "\\(" {
            copying = 1; body = 0; text = "";
        }
        copying {
            text = text $0 "\n";
            if (!body && /;[[:space:]]*$/) { copying = 0; next }
            if (/^{/) body = 1;
            if (/^}/) {
                if (prefix != "") print prefix;
                printf "%s", text; found = 1; exit;
            }
        }
        END { if (!found) exit 1 }
    ' "$2"
}
vq="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_vq.c"
cat > "$work/queue.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include "virtgpu_limits.h"
#define HZ 100
#define GFP_ATOMIC 0
#define DRM_ERROR(...) ((void)0)
struct mutex { int held; };
struct dma_fence { int refs, error; bool signaled; };
struct virtio_gpu_fence { struct dma_fence f; };
struct virtio_gpu_ctrl_hdr { uint64_t id; };
struct virtio_gpu_object { int alive; };
struct virtio_gpu_object_array { bool locked; };
struct linux_virtio_sg { void *buffer; size_t length; struct linux_virtio_sg *next; };
struct netbsd_virtqueue { unsigned int num_free; };
struct virtio_gpu_vbuffer {
    char *buf, *resp_buf; int size, resp_size, error;
    void *data_buf; unsigned int data_size;
    struct virtio_gpu_fence *fence;
    struct virtio_gpu_object_array *objs;
    struct virtio_gpu_object *release;
};
struct virtio_gpu_device {
    struct { struct netbsd_virtqueue *vq; struct mutex qlock; int ack_queue; } ctrlq;
    struct mutex submit_lock; int submitters;
    bool vqs_ready, dma_stopped; int submit_error;
};
#define jiffies 0
/* These legacy queue cases contain no eligible BO. */
static void virtio_gpu_dma_stop(struct virtio_gpu_device *d) { d->dma_stopped = true; }
static int virtio_gpu_dma_prepare(struct virtio_gpu_vbuffer *b) { (void)b; return 0; }
static void virtio_gpu_dma_post(struct virtio_gpu_vbuffer *b) { (void)b; }
static bool virtio_gpu_submit_begin(struct virtio_gpu_device *d) {
    if (!d->vqs_ready)
        return false;
    d->submitters++;
    return true;
}
static void virtio_gpu_submit_done(struct virtio_gpu_device *d) {
    assert(d->submitters == 1);
    d->submitters--;
}
static bool virtio_gpu_fence_space(struct virtio_gpu_device *d) {
    (void)d;
    return true;
}
static int attempts, canceled, stops, wait_calls;
static int planned_error, first_enospc, progress;
static unsigned int captured_out, captured_in;
static size_t captured_lengths[3];
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = 1; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = 0; }
#define spin_lock mutex_lock
#define spin_unlock mutex_unlock
static void dma_fence_get(struct dma_fence *f) { f->refs++; }
static void dma_fence_put(struct dma_fence *f) { assert(f->refs > 1); f->refs--; }
static int virtio_gpu_fence_emit(struct virtio_gpu_device *v,
    struct virtio_gpu_ctrl_hdr *hdr, struct virtio_gpu_fence *f) {
    assert(v->submit_lock.held);
    hdr->id = 1;
    dma_fence_get(&f->f);
    return 0;
}
static void virtio_gpu_array_add_fence(struct virtio_gpu_object_array *a,
    struct dma_fence *f) { (void)f; assert(a->locked); }
static void virtio_gpu_array_unlock_resv(struct virtio_gpu_object_array *a) {
    assert(a->locked); a->locked = false;
}
static void virtio_gpu_fence_fail(struct virtio_gpu_fence *f, int error) {
    assert(!f->f.signaled); f->f.error = error; f->f.signaled = true;
    dma_fence_put(&f->f);
}
static void virtio_gpu_stop(struct virtio_gpu_device *v, int error) {
    stops++; v->vqs_ready = false; v->submit_error = error;
}
static void virtio_gpu_cancel_vbuf(struct virtio_gpu_vbuffer *b) {
    canceled++;
    if (b->fence) {
        if (!b->fence->f.signaled)
            virtio_gpu_fence_fail(b->fence, b->error);
        dma_fence_put(&b->fence->f);
    }
    if (b->release) { assert(stops); b->release->alive = 0; }
}
static int virtqueue_add_sgs(struct netbsd_virtqueue *vq,
    struct linux_virtio_sg **sgs, unsigned int out, unsigned int in,
    void *cookie, int gfp) {
    (void)vq; (void)cookie; (void)gfp;
    attempts++; captured_out = out; captured_in = in;
    for (unsigned int i = 0; i < out + in; i++) {
        assert(sgs[i] && sgs[i]->buffer && sgs[i]->length);
        assert(sgs[i]->next == NULL); captured_lengths[i] = sgs[i]->length;
    }
    if (first_enospc && attempts == 1) return -ENOSPC;
    return planned_error;
}
static int model_wait(struct netbsd_virtqueue *vq, unsigned int before) {
    wait_calls++; if (progress) { vq->num_free = before + 1; return 1; }
    return 0;
}
#define wait_event_timeout(q, cond, timeout) ((cond) ? (timeout) : model_wait(vq, vq->num_free))
C
extract virtio_gpu_queue_remaining "$vq" 'static long' >> "$work/queue.c"
extract virtio_gpu_queue_fenced_ctrl_buffer "$vq" 'static int' >> "$work/queue.c"
cat >> "$work/queue.c" <<'C'
static void queue_case(int error, bool ready, bool pressure, bool wake,
    bool resource) {
    char cmd[32], resp[24], data[16];
    struct netbsd_virtqueue q = { .num_free = 0 };
    struct virtio_gpu_device dev = { .ctrlq.vq = &q, .vqs_ready = ready };
    struct virtio_gpu_fence fence = { .f.refs = 1 };
    struct virtio_gpu_ctrl_hdr hdr = { 0 };
    struct virtio_gpu_object_array objs = { .locked = true };
    struct virtio_gpu_object bo = { .alive = 1 };
    struct virtio_gpu_vbuffer b = { .buf = cmd, .size = sizeof(cmd),
        .resp_buf = resp, .resp_size = sizeof(resp), .data_buf = data,
        .data_size = sizeof(data), .objs = &objs,
        .release = resource ? &bo : NULL };
    attempts = canceled = stops = wait_calls = 0;
    planned_error = error; first_enospc = pressure; progress = wake;
    int ret = virtio_gpu_queue_fenced_ctrl_buffer(&dev, &b, &hdr, &fence);
    int expected = !ready ? -ENODEV : (pressure && !wake ? -ETIMEDOUT : error);
    assert(ret == expected && !objs.locked && !dev.submit_lock.held);
    if (ready) {
        assert(captured_out == 2 && captured_in == 1);
        assert(captured_lengths[0] == 32 && captured_lengths[1] == 16 &&
            captured_lengths[2] == 24);
    } else assert(attempts == 0);
    if (ret) {
        assert(canceled == 1 && fence.f.refs == 1 && fence.f.error == (ready ? ret : 0));
        if (resource) assert(stops == 1 && bo.alive == 0);
    } else {
        assert(canceled == 0 && fence.f.refs == 3);
        if (resource) assert(bo.alive);
        virtio_gpu_fence_fail(&fence, -ECANCELED);
        if (resource) virtio_gpu_stop(&dev, -ECANCELED);
        virtio_gpu_cancel_vbuf(&b);
        assert(fence.f.refs == 1);
    }
    if (pressure && ready) assert(wait_calls == 1);
    if (pressure && wake && ready) assert(attempts == 2);
}
int main(void) {
    assert(virtgpu_2d_size_valid(1920, 1080, 1920ULL * 1080 * 4));
    assert(!virtgpu_2d_size_valid(0, 1080, 8192));
    assert(!virtgpu_2d_size_valid(UINT32_MAX, UINT32_MAX, UINT64_MAX));
    assert(!virtgpu_2d_size_valid(64, 64, 64 * 64 * 4 - 1));
    assert(!virtgpu_2d_size_valid(1, 1, VIRTGPU_MAX_OBJECT_SIZE + 1ULL));
    assert(virtgpu_transfer_valid(64, 64, 16384, 4, 8, 60, 56, 2064));
    assert(!virtgpu_transfer_valid(64, 64, 16384, 63, 0, 2, 1, 0));
    assert(!virtgpu_transfer_valid(64, 64, 16384, 0, 0, 1, 1, UINT64_MAX));
    assert(!virtgpu_transfer_valid(64, 64, 16384, 0, 0, 64, 64, 1));
    assert(!virtgpu_transfer_valid(64, 64, 16384, 0, 0, 0, 1, 0));
    const int errors[] = { -EMSGSIZE, -ENODEV, -ENOMEM, -EINVAL, -EIO };
    for (unsigned int i = 0; i < sizeof(errors) / sizeof(errors[0]); i++)
        queue_case(errors[i], true, false, false, false);
    queue_case(0, false, false, false, false);
    queue_case(0, true, false, false, false);
    queue_case(0, true, true, true, false);
    queue_case(0, true, true, false, false);
    queue_case(-ENOMEM, true, false, false, true);
    queue_case(0, true, false, false, true);
    puts("VirtGPU production queue, fence/error ownership and size contracts passed");
    return 0;
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    -I"$src/sys/external/bsd/drm2/virtio" "$work/queue.c" -o "$work/queue"
"$work/queue"
if ! grep -q '^virtio_gpu_complete_transfer(' "$vq"; then
    printf '#define CONTROLLED_2D_FOUNDATION 1\n' > "$work/lifetime.c"
else
    : > "$work/lifetime.c"
fi
cat >> "$work/lifetime.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define MAX_INLINE_RESP_SIZE 24
#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x102
#define VIRTIO_GPU_FLAG_FENCE 1
#define GFP_KERNEL 0
#define cpu_to_le32(x) (x)
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define gem_to_virtio_gpu_obj(p) container_of(p, struct virtio_gpu_object, base.base)
struct drm_device { void *dev_private; };
struct drm_gem_object { struct drm_device *dev; };
struct virtio_gpu_device { bool vqs_ready; void *vbufs; };
struct virtio_gpu_object {
    struct { struct drm_gem_object base; } base;
    bool created; unsigned int hw_res_handle; bool mapped, pinned;
    bool dma_required, dma_finalizing, release_pending, dma_resource_retained;
    unsigned int dma_members, dma_retire_refs, dma_lease;
};
struct dma_fence { int refs; };
struct virtio_gpu_fence { struct dma_fence f; };
struct virtio_gpu_ctrl_hdr { uint32_t type, flags; uint64_t fence_id; };
struct virtio_gpu_resource_unref { struct virtio_gpu_ctrl_hdr hdr; uint32_t resource_id; };
struct virtio_gpu_vbuffer {
    struct virtio_gpu_device *vgdev;
    struct virtio_gpu_object *release;
    struct virtio_gpu_fence *fence;
    void *objs, *data_buf, *resp_buf;
    int resp_size, error; char *buf; char payload[64];
    void (*resp_cb)(struct virtio_gpu_device *, struct virtio_gpu_vbuffer *);
    struct { struct virtio_gpu_object *bo; int kind; } dma_op;
};
static struct virtio_gpu_vbuffer *pending;
static bool host_access;
static int frees, map_frees, id_frees, cookie_frees, resets;
static int allocation_error, submission_error, fence_error, fence_frees;
#define KASSERT(c) assert(c)
#define VIRTGPU_DMA_UNREF 2
#define VIRTGPU_LEASE_NONE 0
#define VIRTGPU_LEASE_CLOSED 3
static void virtio_gpu_finalize_object(struct virtio_gpu_object *);
/* Legacy-only objects have no DMA lease; the backing contract tests arbitration. */
static void virtio_gpu_dma_release(struct virtio_gpu_object *bo) {
    assert(!bo->dma_required);
    bo->dma_finalizing = bo->release_pending = true;
    virtio_gpu_finalize_object(bo);
}
static void virtio_gpu_dma_finish(struct virtio_gpu_vbuffer *b, int error) {
    (void)error; assert(!b->dma_op.bo || !b->dma_op.bo->dma_required);
}
#ifndef CONTROLLED_2D_FOUNDATION
static void virtio_gpu_complete_transfer(struct virtio_gpu_device *d,
    struct virtio_gpu_vbuffer *b) { (void)d; (void)b; }
#endif
static void virtio_gpu_release_object(struct virtio_gpu_object *);
static void virtio_gpu_queue_unref(struct virtio_gpu_device *, struct virtio_gpu_object *);
static void virtio_gpu_cancel_vbuf(void *);
static void free_vbuf(struct virtio_gpu_device *, struct virtio_gpu_vbuffer *);
static void virtio_gpu_stop(struct virtio_gpu_device *v, int error) {
    assert(error < 0); resets++; host_access = false; v->vqs_ready = false;
}
static void virtio_gpu_object_detach(struct virtio_gpu_device *v,
    struct virtio_gpu_object *bo) {
    (void)v; assert(!host_access);
    if (bo->mapped) { map_frees++; bo->mapped = bo->pinned = false; }
}
static void virtio_gpu_resource_id_put(struct virtio_gpu_device *v, uint32_t id) {
    (void)v; assert(id == 7); id_frees++;
}
static void drm_gem_shmem_free_object(struct drm_gem_object *obj) {
    struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
    assert(!bo->mapped && !bo->pinned); frees++; free(bo);
}
static void *virtio_gpu_alloc_cmd(struct virtio_gpu_device *v,
    struct virtio_gpu_vbuffer **bp, int size) {
    assert(size <= 64);
    if (allocation_error) return (void *)(intptr_t)-ENOMEM;
    struct virtio_gpu_vbuffer *b = calloc(1, sizeof(*b)); assert(b);
    b->vgdev = v; b->buf = b->payload; b->resp_size = 24;
    b->resp_buf = b->payload + 32; *bp = b; return b->buf;
}
static struct virtio_gpu_fence *virtio_gpu_fence_alloc(struct virtio_gpu_device *v) {
    (void)v;
    if (fence_error) return NULL;
    struct virtio_gpu_fence *f = calloc(1, sizeof(*f)); assert(f);
    f->f.refs = 1; return f;
}
static int virtio_gpu_queue_fenced_ctrl_buffer(struct virtio_gpu_device *v,
    struct virtio_gpu_vbuffer *b, struct virtio_gpu_ctrl_hdr *hdr,
    struct virtio_gpu_fence *f) {
    assert(((struct virtio_gpu_resource_unref *)b->buf)->resource_id == 7);
    assert(hdr == (void *)b->buf && f);
    hdr->flags = VIRTIO_GPU_FLAG_FENCE; hdr->fence_id = 1;
    f->f.refs++; b->fence = f;
    if (submission_error || !v->vqs_ready) {
        int error = submission_error ? submission_error : -ENODEV;
        virtio_gpu_stop(v, error); virtio_gpu_cancel_vbuf(b);
        return error;
    }
    assert(!pending); pending = b; return 0;
}
static void virtio_gpu_fence_complete(struct virtio_gpu_fence *f, int error) {
    (void)f;
    (void)error;
}
static void virtio_gpu_wait_done(struct virtio_gpu_vbuffer *b, int error) {
    (void)b; assert(error == -ENODEV);
}
static void virtio_gpu_array_put_free_delayed(struct virtio_gpu_device *v, void *a) {
    (void)v; (void)a; assert(false);
}
static void dma_fence_put(struct dma_fence *f) {
    assert(f->refs > 0);
    if (--f->refs == 0) { fence_frees++; free(f); }
}
#define kfree free
#define kvfree free
static void kmem_cache_free(void *pool, struct virtio_gpu_vbuffer *b) {
    assert(pool); cookie_frees++; free(b);
}
C
obj="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_object.c"
extract virtio_gpu_finalize_object "$obj" '' >> "$work/lifetime.c"
extract virtio_gpu_release_object "$obj" '' >> "$work/lifetime.c"
extract virtio_gpu_free_object "$obj" '' >> "$work/lifetime.c"
extract free_vbuf "$vq" '' >> "$work/lifetime.c"
extract virtio_gpu_finish_vbuf "$vq" 'static void' >> "$work/lifetime.c"
extract virtio_gpu_cancel_vbuf "$vq" 'void' >> "$work/lifetime.c"
extract virtio_gpu_queue_unref "$vq" '' >> "$work/lifetime.c"
cat >> "$work/lifetime.c" <<'C'
static void lifetime_case(int how) {
    struct virtio_gpu_device dev = { .vqs_ready = true, .vbufs = &dev };
    struct drm_device drm = { .dev_private = &dev };
    struct virtio_gpu_object *bo = calloc(1, sizeof(*bo)); assert(bo);
    bo->base.base.dev = &drm; bo->hw_res_handle = 7;
    bo->created = true; bo->mapped = bo->pinned = true;
    frees = map_frees = id_frees = cookie_frees = resets = fence_frees = 0;
    allocation_error = how == 2; submission_error = how == 3 ? -ENOMEM : 0;
    fence_error = how == 4;
    /* Stop has disabled submissions, but reset has not yet stopped DMA. */
    if (how == 5) dev.vqs_ready = false;
    host_access = true; pending = NULL;
    virtio_gpu_free_object(&bo->base.base);
    if (how < 2) {
        assert(frees == 0 && map_frees == 0 && id_frees == 0 && pending);
        assert(bo->mapped && bo->pinned && host_access);
        struct virtio_gpu_vbuffer *b = pending; pending = NULL;
        assert(((struct virtio_gpu_ctrl_hdr *)b->buf)->flags & VIRTIO_GPU_FLAG_FENCE);
        if (how == 0) {
            host_access = false; dma_fence_put(&b->fence->f); free_vbuf(&dev, b);
        }
        else { virtio_gpu_stop(&dev, -ENODEV); virtio_gpu_cancel_vbuf(b); }
    } else assert(resets == 1);
    assert(!host_access && frees == 1 && map_frees == 1 && id_frees == 1);
    assert(cookie_frees == (how == 2 ? 0 : 1));
    assert(fence_frees == (how == 2 || how == 4 ? 0 : 1));
}
int main(void) {
    for (int i = 0; i < 6; i++) lifetime_case(i);
    puts("VirtGPU production unref/ACK/reset/cancel GEM lifetime contracts passed");
    return 0;
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror "$work/lifetime.c" -o "$work/lifetime"
"$work/lifetime"
cat > "$work/uvm.c" <<'C'
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#define PROT_READ 1
#define PROT_WRITE 2
#define KASSERT assert
#define to_shmem(obj) ((struct drm_gem_shmem_object *)(obj))
struct uvm_object { int unused; };
struct drm_gem_object { size_t size; struct uvm_object gemo_uvmobj; int refs; };
struct drm_gem_shmem_object {
    struct drm_gem_object base; unsigned int pin_count, vmap_count;
    void *pages; int lock;
};
static int unwired, arrays, released, offsets, locks, frees;
static void drm_gem_object_get(struct drm_gem_object *o) { o->refs++; }
static void drm_gem_put_pages(struct drm_gem_object *o, void *p, bool d, bool a) {
    (void)o; assert(p && d && a); unwired++;
}
static void kvfree(void *p) { assert(unwired == 1); arrays++; free(p); }
static void drm_gem_free_mmap_offset(struct drm_gem_object *o) { (void)o; offsets++; }
static void drm_gem_object_release(struct drm_gem_object *o) { (void)o; released++; }
static void linux_mutex_destroy(int *p) { (void)p; locks++; }
static void kfree(void *p) { assert(arrays == 1); frees++; free(p); }
C
helper="$src/sys/external/bsd/drm2/drm/drm_gem_shmem_helper.c"
extract drm_gem_shmem_prime_mmap "$helper" int >> "$work/uvm.c"
extract drm_gem_shmem_free_object "$helper" void >> "$work/uvm.c"
cat >> "$work/uvm.c" <<'C'
static void mmap_case(size_t bytes) {
    struct drm_gem_shmem_object *s = calloc(1, sizeof(*s)); assert(s);
    s->base.size = bytes; s->base.refs = 1; s->pages = malloc(32); assert(s->pages);
    const off_t offsets_in[] = { 0, 4096, -1, (off_t)bytes };
    for (unsigned int i = 0; i < 4; i++) {
        off_t off = offsets_in[i]; int flags = 0, advice = 0, maxprot = 0;
        struct uvm_object *uobj = NULL;
        int ret = drm_gem_shmem_prime_mmap(&s->base, &off, 4096, PROT_READ,
            &flags, &advice, &uobj, &maxprot);
        if (offsets_in[i] >= 0 && (uint64_t)offsets_in[i] + 4096 <= bytes) {
            assert(ret == 0 && off == offsets_in[i]);
            assert(uobj == &s->base.gemo_uvmobj && s->base.refs == 2);
            assert(maxprot == (PROT_READ | PROT_WRITE));
            s->base.refs--; /* native pager detach */
        } else assert(ret == -EINVAL && s->base.refs == 1 && uobj == NULL);
    }
    assert(s->base.refs == 1); s->base.refs--;
    unwired = arrays = released = offsets = locks = frees = 0;
    drm_gem_shmem_free_object(&s->base);
    assert(unwired == 1 && arrays == 1 && released == 1 && offsets == 1 &&
        locks == 1 && frees == 1);
}
int main(void) {
    for (unsigned int i = 0; i < 128; i++) {
        mmap_case(4096); mmap_case(8 * 1024 * 1024);
    }
    puts("VirtGPU production PRIME byte offset/reference and UVM page-array contracts passed");
    return 0;
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    "$work/uvm.c" -o "$work/uvm"
"$work/uvm"
cat > "$work/sync.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#define HZ 100
#define GFP_KERNEL 0
#define VIRTIO_GPU_FLAG_FENCE 1
#define le32_to_cpu(x) (x)
#define u32 uint32_t
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x100
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO 0x108
#define VIRTIO_GPU_CMD_GET_CAPSET 0x109
#define VIRTIO_GPU_CMD_GET_EDID 0x10a
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101
#define VIRTIO_GPU_RESP_OK_CAPSET_INFO 0x1102
#define VIRTIO_GPU_RESP_OK_CAPSET 0x1103
#define VIRTIO_GPU_RESP_OK_EDID 0x1104
struct mutex { int held; };
struct dma_fence { int refs; bool emitted; };
struct virtio_gpu_fence { struct dma_fence f; };
struct virtio_gpu_ctrl_hdr { uint32_t type, flags; uint64_t fence_id; };
struct virtio_gpu_wait { int refs; bool done; int error; };
struct virtio_gpu_object_array { bool locked; };
struct virtio_gpu_device { bool vqs_ready; struct { struct mutex lock; } resp_wq; };
struct virtio_gpu_vbuffer {
    struct virtio_gpu_device *vgdev;
    struct virtio_gpu_ctrl_hdr *buf, *resp_buf;
    int resp_size; unsigned int resp_received;
    struct virtio_gpu_wait *wait;
    struct virtio_gpu_fence *fence;
    struct virtio_gpu_object_array *objs;
};
static int wait_oom, fence_oom, lock_error, response_mode, submits, canceled, stops;
static int objects_released, wait_frees, fence_frees, pending_waits;
static struct virtio_gpu_vbuffer *pending;
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = 1; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = 0; }
#define atomic_set(p, v) (*(p) = (v))
static void *kzalloc(size_t size, int flags) {
    (void)flags; return wait_oom ? NULL : calloc(1, size);
}
#define kfree free
static struct virtio_gpu_fence *virtio_gpu_fence_alloc(struct virtio_gpu_device *v) {
    (void)v; if (fence_oom) return NULL;
    struct virtio_gpu_fence *f = calloc(1, sizeof(*f)); assert(f); f->f.refs = 1;
    return f;
}
static void dma_fence_put(struct dma_fence *f) {
    assert(f->refs > 0); if (--f->refs == 0) { fence_frees++; free(f); }
}
static int virtio_gpu_array_lock_resv(struct virtio_gpu_object_array *a) {
    assert(!a->locked); if (lock_error) return -EINTR;
    a->locked = true; return 0;
}
static void virtio_gpu_array_unlock_resv(struct virtio_gpu_object_array *a) {
    assert(a->locked); a->locked = false;
}
static void virtio_gpu_wait_put(struct virtio_gpu_wait *w) {
    assert(w->refs > 0); if (--w->refs == 0) { wait_frees++; free(w); }
}
static void virtio_gpu_cancel_vbuf(struct virtio_gpu_vbuffer *b) {
    canceled++;
    if (b->wait) { virtio_gpu_wait_put(b->wait); b->wait = NULL; }
    if (b->fence) { dma_fence_put(&b->fence->f); b->fence = NULL; }
    if (b->objs) { assert(!b->objs->locked); objects_released++; }
}
static void virtio_gpu_stop(struct virtio_gpu_device *v, int error) {
    assert(error < 0); v->vqs_ready = false; stops++;
}
static int virtio_gpu_response_error(struct virtio_gpu_vbuffer *);
static int virtio_gpu_queue_fenced_ctrl_buffer(struct virtio_gpu_device *v,
    struct virtio_gpu_vbuffer *b, struct virtio_gpu_ctrl_hdr *hdr,
    struct virtio_gpu_fence *f) {
    (void)v; assert(hdr == b->buf && f); submits++;
    hdr->flags = VIRTIO_GPU_FLAG_FENCE; hdr->fence_id = 21;
    f->f.emitted = true; f->f.refs++; b->fence = f;
    if (b->objs) virtio_gpu_array_unlock_resv(b->objs);
    assert(!pending); pending = b; return 0;
}
static int model_wait(struct virtio_gpu_device *v, struct virtio_gpu_wait *w) {
    if (w->done || !v->vqs_ready) return 1;
    assert(pending && w == pending->wait); pending_waits++;
    struct virtio_gpu_vbuffer *b = pending;
    if (response_mode == 2) { virtio_gpu_stop(v, -ENODEV); return 1; }
    if (response_mode == 3) return 0;
    *b->resp_buf = *b->buf; b->resp_buf->type = VIRTIO_GPU_RESP_OK_NODATA;
    if (response_mode == 1) b->resp_buf->flags = 0; /* early unfenced response */
    w->error = virtio_gpu_response_error(b); w->done = true;
    if (w->error) virtio_gpu_stop(v, w->error);
    b->wait = NULL; virtio_gpu_wait_put(w); dma_fence_put(&b->fence->f);
    b->fence = NULL; pending = NULL;
    return 1;
}
#define wait_event_timeout(q, condition, ticks) \
    ((void)(q), (void)(ticks), (condition) ? 1 : model_wait(vgdev, wait))
C
extract virtio_gpu_response_error "$vq" int >> "$work/sync.c"
extract virtio_gpu_queue_sync "$vq" int >> "$work/sync.c"
cat >> "$work/sync.c" <<'C'
static void sync_case(int how, bool supplied) {
    struct virtio_gpu_device dev = { .vqs_ready = true };
    struct virtio_gpu_ctrl_hdr cmd = { .type = 0x105 }, resp = { 0 };
    struct virtio_gpu_object_array objs = { .locked = supplied };
    struct virtio_gpu_vbuffer b = { .vgdev = &dev, .buf = &cmd, .resp_buf = &resp,
        .resp_size = sizeof(resp), .resp_received = sizeof(resp), .objs = &objs };
    wait_oom = how == 1; fence_oom = how == 2; lock_error = how == 3;
    response_mode = how >= 4 ? how - 3 : 0;
    submits = canceled = stops = objects_released = wait_frees = fence_frees = 0;
    pending = NULL;
    struct virtio_gpu_fence *f = supplied ? virtio_gpu_fence_alloc(&dev) : NULL;
    if (supplied && fence_oom) return;
    int ret = virtio_gpu_queue_sync(&dev, &b, supplied ? &cmd : NULL, f);
    if (how == 1 || (!supplied && how == 2)) {
        assert(ret == -ENOMEM && submits == 0 && canceled == 1);
        if (f) assert(!f->f.emitted);
    } else if (!supplied && how == 3) {
        assert(ret == -EINTR && submits == 0 && canceled == 1 && fence_frees == 1);
    } else {
        assert(submits == 1 && (cmd.flags & VIRTIO_GPU_FLAG_FENCE));
        assert(ret == (how == 4 ? -EIO : how == 5 ? -ENODEV :
            how == 6 ? -ETIMEDOUT : 0));
        if (how >= 4) assert(stops == 1);
        if (pending) { virtio_gpu_cancel_vbuf(pending); pending = NULL; }
        assert(wait_frees == 1);
    }
    if (f) { assert(f->f.refs == 1); dma_fence_put(&f->f); }
    assert(!objs.locked);
}
int main(void) {
    for (int i = 0; i < 7; i++) { sync_case(i, false); sync_case(i, true); }
    assert(pending_waits > 0);
    puts("VirtGPU production synchronous GPU fence, early response and reset-wait contracts passed");
    return 0;
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror "$work/sync.c" -o "$work/sync"
"$work/sync"
cat > "$work/cursor.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "virtgpu_limits.h"
#define GFP_KERNEL 0
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x105
#define VIRTIO_GPU_CMD_UPDATE_CURSOR 0x300
#define VIRTIO_GPU_CMD_MOVE_CURSOR 0x301
#define BUS_DMASYNC_PREWRITE 1
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define WARN_ON(x) (x)
#define DRM_DEBUG(...) ((void)0)
#define gem_to_virtio_gpu_obj(p) ((struct virtio_gpu_object *)(p))
#define to_virtio_gpu_framebuffer(p) ((struct virtio_gpu_framebuffer *)(p))
#define drm_crtc_to_virtio_gpu_output(p) ((struct virtio_gpu_output *)(p))
struct dma_fence { bool emitted; int refs; };
struct virtio_gpu_fence { struct dma_fence f; };
struct virtio_gpu_ctrl_hdr { uint32_t type; };
struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr; uint32_t resource_id;
    uint64_t offset; struct { uint32_t width, height, x, y; } r;
};
struct virtio_gpu_device { struct { void *dmat; } *vdev; int submit_error; };
struct drm_device { void *dev_private; };
struct drm_gem_object { size_t size; };
struct drm_framebuffer { struct drm_gem_object *obj[1]; unsigned int hot_x, hot_y; };
struct virtio_gpu_framebuffer { struct drm_framebuffer base; struct virtio_gpu_fence *fence; };
struct virtio_gpu_object {
    struct { struct drm_gem_object base; } base;
    unsigned int width, height, hw_res_handle; bool dumb;
    struct { struct { void *sg_dmamap; } *sgl; } *pages;
};
struct virtio_gpu_output {
    struct { struct virtio_gpu_ctrl_hdr hdr; unsigned int resource_id, hot_x, hot_y;
        struct { unsigned int x, y; } pos; } cursor;
};
struct drm_plane_state {
    void *crtc; struct drm_framebuffer *fb;
    unsigned int crtc_w, crtc_h, crtc_x, crtc_y;
};
struct drm_plane { struct drm_device *dev; struct drm_plane_state *state; };
struct virtio_gpu_object_array { struct drm_gem_object *objs[1]; bool locked; };
struct virtio_gpu_vbuffer { void *buf; struct virtio_gpu_object_array *objs; };
static int mode, arrays_freed, waited, pings, cmd_frees;
static struct virtio_gpu_fence fence;
static struct virtio_gpu_object_array *virtio_gpu_array_alloc(int n) {
    assert(n == 1); return calloc(1, sizeof(struct virtio_gpu_object_array));
}
static void virtio_gpu_array_add_obj(struct virtio_gpu_object_array *a,
    struct drm_gem_object *o) { a->objs[0] = o; }
static int virtio_gpu_array_lock_resv(struct virtio_gpu_object_array *a) {
    assert(!a->locked); a->locked = true; return 0;
}
static void virtio_gpu_array_unlock_resv(struct virtio_gpu_object_array *a) {
    assert(a->locked); a->locked = false;
}
static void virtio_gpu_array_put_free(struct virtio_gpu_object_array *a) {
    assert(!a->locked); arrays_freed++; free(a);
}
#define VIRTGPU_OPERATION_TO_HOST 2
static struct virtio_gpu_object_array *virtio_gpu_operation_array_alloc(
    struct virtio_gpu_device *v,int n,int kind) {
    assert(v && kind==VIRTGPU_OPERATION_TO_HOST); return virtio_gpu_array_alloc(n);
}
static int virtio_gpu_cmd_transfer_to_host_2d(struct virtio_gpu_device *v,
    uint64_t offset,uint32_t w,uint32_t h,uint32_t x,uint32_t y,
    struct virtio_gpu_object_array *a,struct virtio_gpu_fence *f) {
    assert(f==&fence && !offset && !x && !y && w==64 && h==64);
    (void)v; virtio_gpu_array_unlock_resv(a); virtio_gpu_array_put_free(a);
    cmd_frees++;
    if(mode) return mode==1?-ENOMEM:mode==2?-EIO:mode==3?-EINTR:-ETIMEDOUT;
    f->f.emitted=true;return 0;
}
static void dma_fence_put(struct dma_fence *f) { assert(f->refs == 1); f->refs--; }
static void virtio_gpu_cursor_ping(struct virtio_gpu_device *v,
    struct virtio_gpu_output *o, struct virtio_gpu_object *b) {
    (void)v; assert(o && b); pings++;
}
C
plane="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_plane.c"
extract virtio_gpu_cursor_plane_update "$plane" '' >> "$work/cursor.c"
cat >> "$work/cursor.c" <<'C'
static void cursor_case(int how) {
    struct virtio_gpu_device vgdev = { 0 };
    struct drm_device dev = { .dev_private = &vgdev };
    struct virtio_gpu_output output = { 0 };
    struct virtio_gpu_object bo = { .base.base.size = 16384,
        .width = 64, .height = 64, .dumb = true, .hw_res_handle = 8 };
    bo.pages = calloc(1, sizeof(*bo.pages)); assert(bo.pages);
    bo.pages->sgl = calloc(1, sizeof(*bo.pages->sgl)); assert(bo.pages->sgl);
    vgdev.vdev = calloc(1, sizeof(*vgdev.vdev)); assert(vgdev.vdev);
    fence.f.emitted = false; fence.f.refs = 1;
    struct virtio_gpu_framebuffer fb = { .base.obj = { &bo.base.base }, .fence = &fence };
    struct drm_plane_state old = { .crtc = &output };
    struct drm_plane_state state = { .crtc = &output, .fb = &fb.base,
        .crtc_w = 64, .crtc_h = 64 };
    struct drm_plane p = { .dev = &dev, .state = &state };
    mode = how; arrays_freed = waited = pings = cmd_frees = 0;
    virtio_gpu_cursor_plane_update(&p, &old);
    assert(arrays_freed == 1);
    if (how == 0) assert(waited == 0 && pings == 1 && fb.fence == NULL);
    else assert(waited == 0 && pings == 0 && !fence.f.emitted && fb.fence == &fence);
    assert(cmd_frees == 1);

    free(bo.pages->sgl); free(bo.pages); free(vgdev.vdev);
}
int main(void) {
    for (int i = 0; i < 5; i++) cursor_case(i);
    puts("VirtGPU production cursor exact upload errors prevent ping without a second wait");
    return 0;
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    -I"$src/sys/external/bsd/drm2/virtio" "$work/cursor.c" -o "$work/cursor"
"$work/cursor"
# Opcode completion no longer owns 2D POST; immutable direction does.
cat > "$work/dma.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define KASSERT assert
#define BUS_DMASYNC_PREWRITE 1
#define BUS_DMASYNC_POSTWRITE 2
#define BUS_DMASYNC_PREREAD 4
#define BUS_DMASYNC_POSTREAD 8
enum virtgpu_operation_kind { VIRTGPU_OPERATION_NONE,VIRTGPU_OPERATION_EXEC,
    VIRTGPU_OPERATION_TO_HOST,VIRTGPU_OPERATION_FROM_HOST };
C
extract virtgpu_operation_sync_ops "$src/sys/external/bsd/drm2/virtio/virtgpu_dma.c" int >> "$work/dma.c"
cat >> "$work/dma.c" <<'C'
int main(void) {
    assert(virtgpu_operation_sync_ops(VIRTGPU_OPERATION_TO_HOST,false)==BUS_DMASYNC_PREWRITE);
    assert(virtgpu_operation_sync_ops(VIRTGPU_OPERATION_TO_HOST,true)==BUS_DMASYNC_POSTWRITE);
    assert(virtgpu_operation_sync_ops(VIRTGPU_OPERATION_FROM_HOST,false)==BUS_DMASYNC_PREREAD);
    assert(virtgpu_operation_sync_ops(VIRTGPU_OPERATION_FROM_HOST,true)==BUS_DMASYNC_POSTREAD);
    puts("VirtGPU production immutable directional operation phases passed");
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror "$work/dma.c" -o "$work/dma"
"$work/dma"
cat > "$work/dumb.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#define PAGE_SIZE 4096ULL
/* The Linux compatibility header deliberately uses the inverse mask. */
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define roundup(x, n) (((x) + (n) - 1) / (n) * (n))
#define DRM_FORMAT_HOST_XRGB8888 1
struct drm_gem_object { int unused; };
struct drm_device { int unused; };
struct drm_file { int unused; };
struct drm_mode_create_dumb {
    uint32_t width, height, bpp, pitch, handle; uint64_t size;
};
struct virtio_gpu_object_params {
    uint32_t width, height, format; uint64_t size; bool dumb;
};
static int created;
static unsigned int virtio_gpu_translate_format(int format) { assert(format == 1); return 2; }
static int virtio_gpu_gem_create(struct drm_file *file, struct drm_device *dev,
    struct virtio_gpu_object_params *p, struct drm_gem_object **obj, uint32_t *handle) {
    (void)file; (void)dev; (void)obj;
    assert(p->dumb && p->format == 2 && p->size >= (uint64_t)p->width * p->height * 4);
    assert(p->size % PAGE_SIZE == 0); created++; *handle = 7; return 0;
}
C
gem="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_gem.c"
extract virtio_gpu_mode_dumb_create "$gem" '' >> "$work/dumb.c"
cat >> "$work/dumb.c" <<'C'
int main(void) {
    struct drm_mode_create_dumb a = { .width = 64, .height = 16, .bpp = 32 };
    assert(virtio_gpu_mode_dumb_create(NULL, NULL, &a) == 0);
    assert(a.handle == 7 && a.pitch == 256 && a.size == 4096);
    a = (struct drm_mode_create_dumb){ .width = 800, .height = 600, .bpp = 32 };
    assert(virtio_gpu_mode_dumb_create(NULL, NULL, &a) == 0);
    assert(a.handle == 7 && a.pitch == 3200 && a.size == 1921024);
    a.bpp = 24; assert(virtio_gpu_mode_dumb_create(NULL, NULL, &a) == -EINVAL);
    a.bpp = 32; a.width = UINT32_MAX;
    assert(virtio_gpu_mode_dumb_create(NULL, NULL, &a) == -EINVAL);
    assert(created == 2);
    puts("VirtGPU production 1-page and scanout dumb allocation under Linux PAGE_MASK passed");
    return 0;
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror "$work/dumb.c" -o "$work/dumb"
"$work/dumb"
cat > "$work/attach.c" <<'C'
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#define IPL_VM 1
#define VIRTIO_COMMON_FLAG_BITS ""
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
struct netbsd_virtqueue { int unused; };
typedef void vq_callback_t(struct netbsd_virtqueue *);
struct virtio_softc { uint64_t offered, requested, negotiated; int modern; };
struct virtio_device { uint64_t features; int initialized; };
struct virtiodrm_softc {
    void *sc_dev; struct virtio_device sc_vdev; struct netbsd_virtqueue *sc_vqs[2];
};
typedef void *device_t;
static int stage, fail_init, fail_queues, failures, finalized, deferred;
static struct netbsd_virtqueue queues[2];
static void *device_private(void *p) { return p; }
static void aprint_naive(const char *s) { (void)s; }
static void aprint_normal(const char *s) { (void)s; }
static void aprint_error_dev(void *d, const char *s, ...) { (void)d; (void)s; }
static void virtio_gpu_ctrl_ack(struct netbsd_virtqueue *q) { (void)q; }
static void virtio_gpu_cursor_ack(struct netbsd_virtqueue *q) { (void)q; }
static int virtiodrm_config_bridge(struct virtio_softc *v) { (void)v; return 0; }
static void virtiodrm_config_changed(struct virtio_device *v) { (void)v; }
static void virtio_gpu_cancel_vbuf(void *p) { (void)p; }
static void virtiodrm_attach_deferred(device_t d) { (void)d; }
static void virtio_child_attach_start(struct virtio_softc *v, device_t self,
    int ipl, uint64_t features, const char *bits) {
    (void)self; (void)bits;
    assert(ipl == IPL_VM && features == VIRGL_TEST_EXPECTED);
    v->requested = features; v->negotiated = v->offered & features; stage++;
}
static int virtio_version_1(struct virtio_softc *v) { return v->modern; }
static void virtio_child_attach_failed(struct virtio_softc *v) { (void)v; failures++; }
static int linux_virtio_init(struct virtio_device *v, struct virtio_softc *n,
    device_t d, int ipl, size_t bytes, int (*bridge)(struct virtio_softc *),
    void (*changed)(struct virtio_device *), void (*cancel)(void *)) {
    (void)d; (void)bridge; (void)changed; (void)cancel;
    assert(stage == 1 && ipl == IPL_VM && bytes == 1024 * 1024);
    if (fail_init) return -ENOMEM;
    v->features = n->negotiated;
    v->initialized = 1; stage++; return 0;
}
static int virtio_find_vqs(struct virtio_device *v, unsigned int n,
    struct netbsd_virtqueue **q, vq_callback_t **callbacks,
    const char *const *names, void *affinity) {
    assert(v->initialized && n == 2 && callbacks[0] && callbacks[1] &&
        names[0] && names[1] && !affinity && stage == 2);
    if (fail_queues) return -ENOMEM;
    q[0] = &queues[0]; q[1] = &queues[1]; stage++; return 0;
}
static void linux_virtio_fini(struct virtio_device *v) { assert(v->initialized); finalized++; }
static void config_interrupts(device_t d, void (*fn)(device_t)) {
    struct virtiodrm_softc *sc = d;
    assert(stage == 3 && sc->sc_vqs[0] && sc->sc_vqs[1] && fn);
    deferred++; /* native parent can now see queues/interrupt attach finished */
}
C
autoconf="$src/sys/external/bsd/drm2/virtio/virtgpu_autoconf.c"
sed -n '/^#define VIRTIO_GPU_F_VIRGL /p' \
    "$src/sys/external/bsd/drm2/include/linux/virtio_gpu.h" >> "$work/attach.c"
extract virtiodrm_attach "$autoconf" void >> "$work/attach.c"
cat >> "$work/attach.c" <<'C'
int main(void) {
    for (unsigned int offered = 0; offered < 32; offered++) {
        for (unsigned int fault = 0; fault < 4; fault++) {
            struct virtio_softc parent = { .offered = offered, .modern = fault != 1 };
            struct virtiodrm_softc child = { 0 };
            stage = failures = finalized = deferred = 0;
            fail_init = fault == 2; fail_queues = fault == 3;
            virtiodrm_attach(&parent, &child, NULL);
            assert(parent.requested == VIRGL_TEST_EXPECTED);
            assert(parent.negotiated == (offered & VIRGL_TEST_EXPECTED));
            assert(deferred == (fault == 0) && failures == (fault != 0));
            assert(finalized == (fault == 3));
            assert(child.sc_vdev.initialized == (fault == 0 || fault == 3));
            if (child.sc_vdev.initialized)
                assert(child.sc_vdev.features == parent.negotiated);
        }
    }
    puts("VirtGPU production attach: 128 feature-offer/failure cases passed");
    return 0;
}
C
for enabled in 0 1; do
    option=
    if [ "$enabled" = 1 ]; then option=-DVIRTGPU_VIRGL; fi
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${VIRTGPU_ATTACH_TEST_CFLAGS:-} $option -DVIRGL_TEST_EXPECTED="$enabled" \
        "$work/attach.c" -o "$work/attach-$enabled"
    "$work/attach-$enabled"
done
# Causal negatives: the old zero request and accidentally enabling EDID.
for mask in 0 3; do
    sed "s/features = UINT64_C(1) << VIRTIO_GPU_F_VIRGL;/features = UINT64_C($mask);/" \
        "$work/attach.c" > "$work/attach-mutant.c"
    grep -q "features = UINT64_C($mask);" "$work/attach-mutant.c"
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        -DVIRTGPU_VIRGL -DVIRGL_TEST_EXPECTED=1 \
        "$work/attach-mutant.c" -o "$work/attach-mutant"
    if (ulimit -c 0; "$work/attach-mutant") > "$work/attach-mutant.log" 2>&1; then
        echo "VirtGPU feature-mask mutant $mask unexpectedly passed" >&2
        exit 1
    fi
    grep -q 'features == VIRGL_TEST_EXPECTED' "$work/attach-mutant.log"
done
echo 'VirtGPU zero-mask and extra-feature mutants rejected'
cat > "$work/busid.c" <<'C'
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include "drm_native_identity.h"
typedef uint32_t pcitag_t, pcireg_t;
#define PCI_ID_REG 0
#define PCI_CLASS_REG 8
#define PCI_SUBSYS_ID_REG 44
#define PCI_VENDOR(v) ((v)&0xffff)
#define PCI_PRODUCT(v) (((v)>>16)&0xffff)
#define PCI_SUBSYS_VENDOR PCI_VENDOR
#define PCI_SUBSYS_ID PCI_PRODUCT
#define PCI_REVISION(v) ((v)&255)
static pcitag_t pci_make_tag(unsigned pc, int b, int d, int f) {
    assert(pc==3 && b==7 && d==4 && f==2); return 123;
}
static pcireg_t pci_conf_read(unsigned pc, pcitag_t tag, int reg) {
    assert(pc==3 && tag==123);
    return reg==PCI_ID_REG ? 0x10501af4 : reg==PCI_CLASS_REG ? 1 : 0x11001af4;
}
#define PCICF_DEV 0
#define PCICF_FUNCTION 1
struct device {
    struct device *parent; const char *name, *kind; void *priv; int locators[2];
};
typedef struct device *device_t;
struct drm_device { device_t dev; char unique[40]; struct drm_native_pci_record native_pci; };
struct pci_softc {
    unsigned int sc_pc; int sc_bus;
    struct { device_t c_dev; } sc_devices[32 * 8];
};
#define PCI_SC_DEVICESC(d,f) sc_devices[(d) * 8 + (f)]
static device_t device_parent(device_t d) { return d->parent; }
static int device_locator(device_t d, unsigned int n) { assert(n < 2); return d->locators[n]; }
static int device_is_a(device_t d, const char *kind) { return strcmp(d->kind, kind) == 0; }
static void *device_private(device_t d) { return d->priv; }
static const char *device_xname(device_t d) { return d->name; }
static unsigned int pci_get_segment(unsigned int pc) { return pc; }
static void aprint_normal_dev(device_t d, const char *format, ...) { (void)d; (void)format; }
static int drm_dev_set_unique(struct drm_device *dev, const char *name) {
    assert(strlen(name) < sizeof(dev->unique)); strcpy(dev->unique, name); return 0;
}
C
extract virtiodrm_set_busid "$autoconf" int >> "$work/busid.c"
cat >> "$work/busid.c" <<'C'
int main(void) {
    struct pci_softc psc = { .sc_pc = 3, .sc_bus = 7 };
    struct device bus = { .kind = "pci", .priv = &psc };
    struct device parent = { .parent = &bus, .name = "virtio33", .locators = {4, 2} };
    struct device sibling = { .parent = &bus, .name = "virtio34" };
    struct device self = { .parent = &parent, .name = "virtiodrm0" };
    struct drm_device dev = { .dev = &self };
    psc.PCI_SC_DEVICESC(1, 0).c_dev = &sibling;
    /* Real PCI config_found has not yet returned to fill this c_dev. */
    assert(psc.PCI_SC_DEVICESC(4, 2).c_dev == NULL);
    assert(virtiodrm_set_busid(&dev) == 0);
    assert(strcmp(dev.unique, "pci:0003:07:04.2") == 0);
    parent.locators[0] = -1;
    assert(virtiodrm_set_busid(&dev) == -ENODEV);
    parent.locators[0] = 32;
    assert(virtiodrm_set_busid(&dev) == -ENODEV);
    parent.locators[0] = 4; parent.locators[1] = 8;
    assert(virtiodrm_set_busid(&dev) == -ENODEV);
    bus.kind = "acpi";
    assert(virtiodrm_set_busid(&dev) == 0 && strcmp(dev.unique, "virtiodrm0") == 0);
    puts("VirtGPU production bus metadata uses exact PCI parent identity and preserves MMIO");
    return 0;
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$src/sys/external/bsd/drm2/include/drm" "$work/busid.c" -o "$work/busid"
"$work/busid"
