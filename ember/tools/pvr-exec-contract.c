/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests. */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef uint16_t __u16;
typedef uint32_t __u32;
typedef int32_t __s32;
typedef uint64_t __u64;
typedef void *bus_dma_tag_t;
#define KM_SLEEP 0
#define GFP_KERNEL 0
#define GFP_NOWAIT 1
#define KAUTH_GENERIC_ISSUSER 1
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define NITEMS(a) (sizeof(a) / sizeof((a)[0]))
#include "backend.h"
#include "context.h"
#include "drm.h"
#include "exec.h"
#include "file.h"
#include "freelist.h"
#include "stream.h"
struct idr { void *slot[32]; };
struct drm_device { bus_dma_tag_t dmat; };
struct pvr_file_vm { unsigned refs, pins; bool alive, failed; };
struct drm_file {
	struct pvr_exec_file *execution;
	struct pvr_file_vm *vm;
	bool vm_handle, master;
};
struct pvr_context { unsigned marker; };
struct pvr_freelist { unsigned marker; };
static struct { int p_pid; } process = { 77 };
#define curproc (&process)
static int hz = 100;
static unsigned checks, allocations;
#define CHECK(e) do { checks++; if (!(e)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); \
} } while (0)
#define KASSERT(e) CHECK(e)
static struct drm_device device;
static struct drm_file first, second;
static struct pvr_file_vm vms[2];
static const struct pvr_backend backend = { .cookie = &device };
static bool gated, preloaded, backend_enabled, privileged;
static bool context_partial, freelist_partial;
static int idr_error, copy_error, id_error, publish_error, pin_error;
static int context_error, freelist_error, context_destroy_error, freelist_destroy_error;
static unsigned context_busy, freelist_busy, context_destroys, freelist_destroys;
static unsigned context_creates, freelist_creates, pauses, copies, ids, publishes;
static unsigned unpins, vm_puts, fails, retained_count, context_order, freelist_order;
static unsigned event_order, unpin_order, put_order;
static void *retained[8];
static bool retained_is_freelist;
static union { uint64_t align; uint8_t bytes[256]; } user_state;
static size_t observed_pin_size;
static uint8_t backing[4096], copied_state[PVR_STREAM_STATIC_RENDER_SIZE];
static struct pvr_context_config observed_config;
static struct pvr_freelist_config observed_freelist;
struct record { void *ptr; size_t size; };
static struct record records[128];

static void *kmem_zalloc(size_t n, int flags)
{
	void *p = calloc(1, n); size_t i;
	CHECK(p != NULL && n != 0 && flags == KM_SLEEP);
	for (i = 0; i < NITEMS(records); i++) if (records[i].ptr == NULL) break;
	CHECK(i < NITEMS(records)); records[i] = (struct record){ p, n };
	allocations++; return p;
}
static void kmem_free(void *p, size_t n)
{
	size_t i;
	for (i = 0; i < NITEMS(records); i++) if (records[i].ptr == p) break;
	CHECK(p != NULL && i < NITEMS(records) && records[i].size == n);
	records[i] = (struct record){ 0 }; CHECK(allocations != 0); allocations--; free(p);
}
static void idr_init(struct idr *id) { memset(id, 0, sizeof(*id)); }
static void idr_destroy(struct idr *id)
{ for (size_t i = 0; i < NITEMS(id->slot); i++) CHECK(id->slot[i] == NULL); }
static void *idr_find(struct idr *id, int h)
{ CHECK(gated); return h > 0 && h < 32 ? id->slot[h] : NULL; }
static void idr_preload(int flags) { CHECK(gated && !preloaded && flags == GFP_KERNEL); preloaded = true; }
static void idr_preload_end(void) { CHECK(preloaded); preloaded = false; }
static int idr_alloc(struct idr *id, void *obj, int start, int end, int flags)
{
	CHECK(gated && preloaded && obj != NULL && start == 1 && end == 0 && flags == GFP_NOWAIT);
	if (idr_error != 0) return -idr_error;
	for (int h = 1; h < 32; h++) if (id->slot[h] == NULL) { id->slot[h] = obj; return h; }
	return -ENOSPC;
}
static void idr_remove(struct idr *id, int h)
{ CHECK(gated && h > 0 && h < 32 && id->slot[h] != NULL); id->slot[h] = NULL; }
static void *idr_get_next(struct idr *id, int *h)
{
	CHECK(gated);
	for (; *h < 32; (*h)++) if (id->slot[*h] != NULL) return id->slot[*h];
	return NULL;
}
static bool drm_is_current_master(struct drm_file *f) { return f->master; }
static int kauth_cred_get(void) { return 0; }
static int kauth_authorize_generic(int cred, int what, void *arg)
{ CHECK(cred == 0 && what == KAUTH_GENERIC_ISSUSER && arg == NULL); return privileged ? 0 : EPERM; }
static int kpause(const char *name, bool interruptible, int ticks, void *lock)
{ CHECK(gated && name != NULL && !interruptible && ticks == 1 && lock == NULL); pauses++; return 0; }
static int copyin(const void *from, void *to, size_t size)
{
	uintptr_t address = (uintptr_t)from, base = (uintptr_t)user_state.bytes;
	CHECK(gated); copies++;
	if (copy_error != 0) return copy_error;
	if (address < base || address - base > sizeof(user_state.bytes) ||
	    size > sizeof(user_state.bytes) - (address - base)) return EFAULT;
	memcpy(to, from, size); return 0;
}
#include "exec-driver.h"

const struct pvr_backend *pvr_device_enter(struct drm_device *d)
{ CHECK(d == &device && !gated); gated = true; return backend_enabled ? &backend : NULL; }
void pvr_device_leave(struct drm_device *d)
{ CHECK(d == &device && gated && !preloaded); gated = false; }
void pvr_device_retain(struct drm_device *d, void *owner)
{ CHECK(d == &device && gated && owner != NULL && retained_count < 8); retained[retained_count++] = owner; }
int pvr_device_next_id(struct drm_device *d, uint32_t *out)
{ CHECK(d == &device && gated); if (id_error != 0) return id_error; *out = ++ids; return 0; }
struct pvr_exec_file *pvr_file_execution(struct drm_file *f) { return f->execution; }
struct pvr_file_vm *pvr_file_vm_get(struct drm_file *f, uint32_t h)
{
	CHECK(gated); if (!f->vm_handle || h != 7) return NULL;
	CHECK(f->vm->alive && f->vm->refs != UINT_MAX); f->vm->refs++; return f->vm;
}
void pvr_file_vm_put(struct pvr_file_vm *vm)
{
	CHECK(gated && vm->alive && vm->refs != 0); vm_puts++; put_order = ++event_order;
	if (--vm->refs == 0) { CHECK(vm->pins == 0); vm->alive = false; }
}
void pvr_file_vm_fail(struct pvr_file_vm *vm)
{ CHECK(gated && vm->alive); vm->failed = true; fails++; }
int pvr_file_vm_publish(struct pvr_file_vm *vm, const struct pvr_backend *be, uint32_t *addr)
{ CHECK(gated && vm->alive && be == &backend); publishes++; if (publish_error != 0) return publish_error; *addr = 0xc0123000; return 0; }
int pvr_file_vm_pin(struct pvr_file_vm *vm, uint64_t address, size_t size,
    uint64_t flags, struct pvr_file_pin *pin)
{
	CHECK(gated && vm->alive && flags == DRM_PVR_BO_PM_FW_PROTECT && pin->mapping == NULL);
	if (pin_error != 0) return pin_error;
	if (vm->pins != 0) return EBUSY;
	CHECK(address == UINT64_C(0x8000100000));
	observed_pin_size = size;
	if (size > sizeof(backing)) return EINVAL;
	vm->pins++; pin->mapping = vm; pin->cpu = backing; pin->physical = 0x12340000; return 0;
}
void pvr_file_unpin(struct pvr_file_pin *pin)
{
	struct pvr_file_vm *vm = pin->mapping;
	CHECK(gated && vm != NULL && vm->alive && vm->pins == 1);
	vm->pins--; memset(pin, 0, sizeof(*pin)); unpins++; unpin_order = ++event_order;
}
void pvr_file_pin_sync(void *cookie, int operations)
{ struct pvr_file_pin *pin = cookie; CHECK(gated && pin->mapping != NULL && operations != 0); }
int pvr_context_create(const struct pvr_backend *be, const struct pvr_context_config *config,
    struct pvr_context **owner)
{
	CHECK(gated && be == &backend && *owner == NULL && config->id != 0);
	CHECK(config->pid == 77 && config->vm_address == 0xc0123000);
	context_creates++; observed_config = *config;
	if (config->static_size != 0) {
		CHECK(config->static_state != user_state.bytes && config->static_size <= sizeof(copied_state));
		memcpy(copied_state, config->static_state, config->static_size);
	} else CHECK(config->static_state == NULL);
	if (context_error == 0 || context_partial) *owner = kmem_zalloc(sizeof(**owner), KM_SLEEP);
	return context_error;
}
int pvr_context_destroy(struct pvr_context **owner)
{
	CHECK(gated && *owner != NULL); context_destroys++; context_order = ++event_order;
	if (context_busy != 0) { context_busy--; return EBUSY; }
	if (context_destroy_error != 0) return context_destroy_error;
	kmem_free(*owner, sizeof(**owner)); *owner = NULL; return 0;
}
int pvr_freelist_create(bus_dma_tag_t dmat, const struct pvr_backend *be,
    const struct pvr_freelist_config *config, struct pvr_freelist **owner)
{
	CHECK(gated && dmat == &device && be == &backend && *owner == NULL);
	CHECK(config->backing_cpu == backing && config->backing_sync == pvr_file_pin_sync);
	CHECK(((struct pvr_file_pin *)config->sync_cookie)->mapping != NULL);
	observed_freelist = *config; freelist_creates++;
	if (freelist_error == 0 || freelist_partial) *owner = kmem_zalloc(sizeof(**owner), KM_SLEEP);
	return freelist_error;
}
int pvr_freelist_destroy(struct pvr_freelist **owner)
{
	CHECK(gated && *owner != NULL); freelist_destroys++; freelist_order = ++event_order;
	CHECK(((struct pvr_file_vm *)((struct pvr_file_pin *)observed_freelist.sync_cookie)->mapping)->pins == 1);
	if (freelist_busy != 0) { freelist_busy--; return EBUSY; }
	if (freelist_destroy_error != 0) return freelist_destroy_error;
	kmem_free(*owner, sizeof(**owner)); *owner = NULL; return 0;
}
static void reset(void)
{
	CHECK(!gated && !preloaded && allocations == 0 && retained_count == 0);
	memset(&first, 0, sizeof(first)); memset(&second, 0, sizeof(second));
	memset(vms, 0, sizeof(vms)); memset(&observed_config, 0, sizeof(observed_config));
	memset(&observed_freelist, 0, sizeof(observed_freelist));
	memset(user_state.bytes, 0x4d, sizeof(user_state.bytes));
	first.vm = &vms[0]; second.vm = &vms[1]; first.vm_handle = second.vm_handle = true;
	vms[0].refs = vms[1].refs = 1; vms[0].alive = vms[1].alive = true;
	device.dmat = &device; first.execution = pvr_exec_open(); second.execution = pvr_exec_open();
	backend_enabled = true; privileged = context_partial = freelist_partial = false;
	idr_error = copy_error = id_error = publish_error = pin_error = 0;
	context_error = freelist_error = context_destroy_error = freelist_destroy_error = 0;
	context_busy = freelist_busy = context_destroys = freelist_destroys = 0;
	context_creates = freelist_creates = pauses = copies = ids = publishes = 0;
	unpins = vm_puts = fails = context_order = freelist_order = 0;
	event_order = unpin_order = put_order = 0; observed_pin_size = 0; retained_is_freelist = false;
}
static void drop_vm_handle(struct drm_file *f)
{ CHECK(gated && f->vm_handle); f->vm_handle = false; pvr_file_vm_put(f->vm); }
static void close_files(void)
{
	(void)pvr_device_enter(&device);
	pvr_exec_close(&device, first.execution); first.execution = NULL;
	if (first.vm_handle) drop_vm_handle(&first);
	pvr_exec_close(&device, second.execution); second.execution = NULL;
	if (second.vm_handle) drop_vm_handle(&second);
	pvr_device_leave(&device);
}
/* The fixture alone models a proven reset to reclaim intentional orphans. */
static void reset_reclaim(void)
{
	(void)pvr_device_enter(&device);
	while (retained_count != 0) {
		void *owner = retained[--retained_count];
		if (retained_is_freelist) {
			struct pvr_exec_freelist *fl = owner;
			CHECK(fl->vm->refs == 1 && fl->vm->pins == 1);
			kmem_free(fl->firmware, sizeof(*fl->firmware));
			pvr_file_unpin(&fl->pin); fl->vm->failed = false; pvr_file_vm_put(fl->vm);
			kmem_free(fl, sizeof(*fl));
		} else {
			struct pvr_exec_context *ctx = owner;
			CHECK(ctx->vm->refs == 1 && ctx->refs == 0);
			kmem_free(ctx->firmware, sizeof(*ctx->firmware));
			ctx->vm->failed = false; pvr_file_vm_put(ctx->vm); kmem_free(ctx, sizeof(*ctx));
		}
	}
	pvr_device_leave(&device);
}
static struct drm_pvr_ioctl_create_context_args context_args(uint32_t type)
{
	return (struct drm_pvr_ioctl_create_context_args){ .type = type,
	    .priority = DRM_PVR_CTX_PRIORITY_NORMAL, .handle = 0xa5a5a5a5,
	    .static_context_state = (uintptr_t)user_state.bytes,
	    .static_context_state_len = type == DRM_PVR_CTX_TYPE_COMPUTE ? 64 :
	    type == DRM_PVR_CTX_TYPE_RENDER ? 192 : 0, .vm_context_handle = 7 };
}
static struct drm_pvr_ioctl_create_free_list_args freelist_args(void)
{
	return (struct drm_pvr_ioctl_create_free_list_args){
	    .free_list_gpu_addr = UINT64_C(0x8000100000), .initial_num_pages = 4,
	    .max_num_pages = 8, .grow_num_pages = 4, .grow_threshold = 13,
	    .vm_context_handle = 7, .handle = 0xa5a5a5a5 };
}
static void context_inputs(void)
{
	struct drm_pvr_ioctl_create_context_args a, saved;
	struct drm_pvr_ioctl_destroy_context_args d;
	for (unsigned type = 0; type < 3; type++) {
		reset(); a = context_args(type);
		a.static_context_state++; /* User pointers are byte addresses in this UAPI. */
		if (type == DRM_PVR_CTX_TYPE_RENDER) a.callstack_addr = 0x12345678;
		CHECK(pvr_create_context(&device, &a, &first) == 0 && a.handle == 1);
		CHECK(observed_config.type == type && observed_config.priority == 1 && vms[0].refs == 2);
		CHECK(observed_config.callstack == a.callstack_addr);
		if (type != DRM_PVR_CTX_TYPE_TRANSFER_FRAG) CHECK(copies == 1 && copied_state[0] == 0x4d);
		else CHECK(copies == 0);
		d = (struct drm_pvr_ioctl_destroy_context_args){ .handle = a.handle };
		CHECK(pvr_destroy_context(&device, &d, &first) == 0);
		CHECK(context_destroys == 1 && vms[0].refs == 1); close_files();
	}
	for (unsigned i = 0; i < 12; i++) {
		int expected = EINVAL;
		reset(); a = context_args(DRM_PVR_CTX_TYPE_COMPUTE);
		switch (i) {
		case 0: a.flags = 1; expected = EOPNOTSUPP; break;
		case 1: a.type = 99; break;
		case 2: a.static_context_state_len--; break;
		case 3: a.callstack_addr = 8; break;
		case 4: a.priority = 1; break;
		case 5: a.priority = DRM_PVR_CTX_PRIORITY_HIGH; expected = EACCES; break;
		case 6: a.static_context_state = 0; expected = EFAULT; break;
		case 7: a.static_context_state = UINTPTR_MAX - 7; expected = EFAULT; break;
		case 8: copy_error = EFAULT; expected = EFAULT; break;
		case 9: a.vm_context_handle = 0; break;
		case 10: id_error = EOVERFLOW; expected = EOVERFLOW; break;
		case 11: publish_error = EIO; expected = EIO; break;
		}
		saved = a; CHECK(pvr_create_context(&device, &a, &first) == -expected);
		CHECK(memcmp(&a, &saved, sizeof(a)) == 0 && vms[0].refs == 1 && context_creates == 0);
		close_files();
	}
	for (unsigned i = 0; i < 3; i++) {
		reset(); a = context_args(DRM_PVR_CTX_TYPE_COMPUTE);
		a.priority = i == 0 ? DRM_PVR_CTX_PRIORITY_LOW : DRM_PVR_CTX_PRIORITY_HIGH;
		first.master = i == 1; privileged = i == 2;
		CHECK(pvr_create_context(&device, &a, &first) == 0);
		CHECK(observed_config.priority == (i == 0 ? 0U : 2U)); close_files();
	}
	reset(); backend_enabled = false; a = context_args(1); saved = a;
	CHECK(pvr_create_context(&device, &a, &first) == -EOPNOTSUPP);
	CHECK(memcmp(&a, &saved, sizeof(a)) == 0 && copies == 0); close_files();
}
static void context_ownership(void)
{
	struct drm_pvr_ioctl_create_context_args a, saved;
	struct drm_pvr_ioctl_destroy_context_args d;
	struct pvr_exec_context *ref;
	reset(); a = context_args(1); CHECK(pvr_create_context(&device, &a, &first) == 0);
	d = (struct drm_pvr_ioctl_destroy_context_args){ .handle = a.handle };
	CHECK(pvr_destroy_context(&device, &d, &second) == -EINVAL && vms[0].refs == 2);
	(void)pvr_device_enter(&device); ref = pvr_exec_context_get(&first, a.handle);
	CHECK(ref != NULL && ref->refs == 2 && pvr_exec_context_vm(ref) == &vms[0]);
	CHECK(pvr_exec_context_firmware(ref) == ref->firmware);
	ref->refs = UINT_MAX; CHECK(pvr_exec_context_get(&first, a.handle) == NULL);
	ref->refs = 2; drop_vm_handle(&first); pvr_device_leave(&device);
	CHECK(pvr_destroy_context(&device, &d, &first) == 0 && context_destroys == 0);
	CHECK(vms[0].alive && vms[0].refs == 1); close_files();
	(void)pvr_device_enter(&device); pvr_exec_context_put(&device, ref); pvr_device_leave(&device);
	CHECK(!vms[0].alive && context_destroys == 1 && allocations == 0);
	for (unsigned i = 0; i < 3; i++) {
		reset(); a = context_args(1); saved = a;
		if (i == 2) idr_error = ENOSPC;
		else { context_error = ENOMEM; context_partial = i == 1; }
		CHECK(pvr_create_context(&device, &a, &first) == -(i == 2 ? ENOSPC : ENOMEM));
		CHECK(memcmp(&a, &saved, sizeof(a)) == 0 && vms[0].refs == 1);
		CHECK(context_destroys == (i == 0 ? 0U : 1U)); close_files();
	}
	for (unsigned i = 0; i < 3; i++) {
		reset(); a = context_args(1); CHECK(pvr_create_context(&device, &a, &first) == 0);
		context_busy = i == 0 ? 2 : i == 1 ? 1000 : 0;
		context_destroy_error = i == 2 ? EIO : 0;
		close_files();
		if (i == 0) CHECK(context_destroys == 3 && pauses == 2 && retained_count == 0);
		else { CHECK(context_destroys == (i == 1 ? 100U : 1U));
			CHECK(retained_count == 1 && fails == 1 && vms[0].refs == 1 && vms[0].alive);
			CHECK(pauses == (i == 1 ? 100U : 0U)); reset_reclaim(); }
	}
	reset(); a = context_args(1); saved = a; context_error = ENOMEM;
	context_partial = true; context_destroy_error = EIO;
	CHECK(pvr_create_context(&device, &a, &first) == -ENOMEM);
	CHECK(memcmp(&a, &saved, sizeof(a)) == 0 && retained_count == 1 && vms[0].refs == 2);
	close_files(); reset_reclaim();
}
static void freelist_ownership(void)
{
	struct drm_pvr_ioctl_create_free_list_args a, saved;
	struct drm_pvr_ioctl_destroy_free_list_args d;
	reset(); a = freelist_args(); CHECK(pvr_create_free_list(&device, &a, &first) == 0);
	CHECK(a.handle == 1 && vms[0].refs == 2 && vms[0].pins == 1);
	CHECK(observed_freelist.initial_pages == 4 && observed_freelist.max_pages == 8);
	CHECK(observed_freelist.grow_pages == 4 && observed_freelist.grow_threshold == 13);
	CHECK(observed_freelist.backing_size == 32 && observed_freelist.backing_pa == 0x12340000);
	d = (struct drm_pvr_ioctl_destroy_free_list_args){ .handle = a.handle };
	CHECK(pvr_destroy_free_list(&device, &d, &second) == -EINVAL && unpins == 0);
	(void)pvr_device_enter(&device); drop_vm_handle(&first); pvr_device_leave(&device);
	CHECK(pvr_destroy_free_list(&device, &d, &first) == 0);
	CHECK(freelist_destroys == 1 && unpins == 1 && !vms[0].alive);
	CHECK(freelist_order < unpin_order && unpin_order < put_order); close_files();
	reset(); a = freelist_args(); a.max_num_pages = UINT32_MAX; saved = a;
	CHECK(pvr_create_free_list(&device, &a, &first) == -EINVAL);
	CHECK(memcmp(&a, &saved, sizeof(a)) == 0 && vms[0].refs == 1);
	if (SIZE_MAX > UINT32_MAX)
		CHECK(observed_pin_size == (uint64_t)UINT32_MAX * sizeof(uint32_t));
	else CHECK(observed_pin_size == 0);
	close_files();
	for (unsigned i = 0; i < 7; i++) {
		int expected;
		reset(); a = freelist_args();
		if (i == 0) { a.max_num_pages = 0; expected = EINVAL; }
		else if (i == 1) { pin_error = EBUSY; expected = EBUSY; }
		else if (i == 2) { id_error = EOVERFLOW; expected = EOVERFLOW; }
		else if (i == 3 || i == 4) { freelist_error = ENOMEM; freelist_partial = i == 4; expected = ENOMEM; }
		else if (i == 5) { idr_error = ENOSPC; expected = ENOSPC; }
		else { backend_enabled = false; expected = EOPNOTSUPP; }
		saved = a; CHECK(pvr_create_free_list(&device, &a, &first) == -expected);
		CHECK(memcmp(&a, &saved, sizeof(a)) == 0 && vms[0].refs == 1 && vms[0].pins == 0);
		CHECK(unpins == (i >= 2 && i <= 5 ? 1U : 0U)); close_files();
	}
	for (unsigned i = 0; i < 3; i++) {
		reset(); a = freelist_args(); CHECK(pvr_create_free_list(&device, &a, &first) == 0);
		freelist_busy = i == 0 ? 2 : i == 1 ? 1000 : 0;
		freelist_destroy_error = i == 2 ? EIO : 0; retained_is_freelist = true;
		close_files();
		if (i == 0) CHECK(freelist_destroys == 3 && pauses == 2 && unpins == 1);
		else { CHECK(freelist_destroys == (i == 1 ? 100U : 1U));
			CHECK(retained_count == 1 && unpins == 0 && vms[0].pins == 1 && vms[0].refs == 1);
			reset_reclaim(); }
	}
}
static void close_order(void)
{
	struct drm_pvr_ioctl_create_context_args c;
	struct drm_pvr_ioctl_create_free_list_args f, saved;
	reset(); c = context_args(1); f = freelist_args();
	CHECK(pvr_create_context(&device, &c, &first) == 0);
	CHECK(pvr_create_free_list(&device, &f, &first) == 0);
	CHECK(vms[0].refs == 3 && vms[0].pins == 1);
	close_files();
	CHECK(context_destroys == 1 && freelist_destroys == 1 && unpins == 1);
	CHECK(context_order < freelist_order && freelist_order < unpin_order);
	CHECK(unpin_order < put_order && !vms[0].alive);
	reset(); f = freelist_args(); saved = f;
	freelist_error = ENOMEM; freelist_partial = true;
	freelist_destroy_error = EIO; retained_is_freelist = true;
	CHECK(pvr_create_free_list(&device, &f, &first) == -ENOMEM);
	CHECK(memcmp(&f, &saved, sizeof(f)) == 0 && retained_count == 1);
	CHECK(vms[0].refs == 2 && vms[0].pins == 1 && unpins == 0);
	close_files(); CHECK(vms[0].refs == 1 && vms[0].alive); reset_reclaim();
}

static void unknown_handles(void)
{
	const uint32_t values[] = { 0, 31, INT_MAX, (uint32_t)INT_MAX + 1, UINT_MAX };
	struct drm_pvr_ioctl_destroy_context_args c = { 0 };
	struct drm_pvr_ioctl_destroy_free_list_args f = { 0 };
	reset();
	for (unsigned i = 0; i < NITEMS(values); i++) {
		c.handle = f.handle = values[i];
		CHECK(pvr_destroy_context(&device, &c, &first) == -EINVAL);
		CHECK(pvr_destroy_free_list(&device, &f, &first) == -EINVAL);
		(void)pvr_device_enter(&device); CHECK(pvr_exec_context_get(&first, values[i]) == NULL); pvr_device_leave(&device);
	}
	c._padding_4 = f._padding_4 = 1;
	CHECK(pvr_destroy_context(&device, &c, &first) == -EINVAL);
	CHECK(pvr_destroy_free_list(&device, &f, &first) == -EINVAL);
	CHECK(!gated && context_destroys == 0 && freelist_destroys == 0); close_files();
}
int main(void)
{
	context_inputs(); context_ownership(); freelist_ownership(); close_order(); unknown_handles();
	CHECK(allocations == 0 && retained_count == 0 && !gated && !preloaded);
	printf("pvr exec contract: %u checks passed\n", checks); return 0;
}
