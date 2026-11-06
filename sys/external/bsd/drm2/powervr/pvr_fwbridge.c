/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native implementation. */
/* Origin: EmberBSD A733 firmware bridge, factored to the common owner. */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <external/bsd/drm2/powervr/pvr_fwbridge.h>

static void
fwbridge_enter(void *cookie)
{
	struct pvr_fwbridge *bridge = cookie;

	mutex_enter(bridge->lock);
}

static void
fwbridge_leave(void *cookie)
{
	struct pvr_fwbridge *bridge = cookie;

	mutex_exit(bridge->lock);
}

static bool
fwbridge_ready(struct pvr_fwbridge *bridge)
{

	KASSERT(mutex_owned(bridge->lock));
	if (bridge->service.queue.failed || !bridge->service.queue.attached)
		return false;
	if (bridge->health != NULL && bridge->health(bridge->health_cookie) != 0) {
		bridge->service.queue.failed = true;
		return false;
	}
	return true;
}

/* Never dereference a caller's owner pointer before membership is proven. */
static struct pvr_fwobject *
fwbridge_object(const struct pvr_fwbridge *bridge,
    const struct pvr_fw_object *object)
{
	struct pvr_fwobject *obj;

	if (object == NULL || object->owner == NULL)
		return NULL;
	for (obj = bridge->service.objects; obj != NULL; obj = obj->next) {
		if (obj != object->owner)
			continue;
		if (!obj->mapped || object->cpu != obj->dma.cpu ||
		    object->size != obj->size || object->address != obj->fw_addr)
			return NULL;
		return obj;
	}
	return NULL;
}

static int
fwbridge_alloc(void *cookie, size_t size, struct pvr_fw_object *out)
{
	struct pvr_fwbridge *bridge = cookie;
	struct pvr_fwobject *obj = NULL;
	int error;

	if (!fwbridge_ready(bridge))
		return EIO;
	if (out == NULL || out->cpu != NULL || out->owner != NULL ||
	    out->size != 0 || out->address != 0)
		return EINVAL;
	error = pvr_fwservice_alloc(&bridge->service, size, &obj);
	if (error != 0)
		return error;
	out->cpu = obj->dma.cpu;
	out->owner = obj;
	out->size = obj->size;
	out->address = obj->fw_addr;
	return 0;
}

static int
fwbridge_free(void *cookie, struct pvr_fw_object *object)
{
	struct pvr_fwbridge *bridge = cookie;
	struct pvr_fwobject *obj;
	int error;

	if (!fwbridge_ready(bridge))
		return EIO;
	obj = fwbridge_object(bridge, object);
	if (obj == NULL)
		return EINVAL;
	error = pvr_fwservice_free(&bridge->service, &obj);
	if (error == 0)
		memset(object, 0, sizeof(*object));
	return error;
}

static void
fwbridge_sync(void *cookie, struct pvr_fw_object *object, int operations)
{
	const int pre = BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE;
	const int post = BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE;
	struct pvr_fwbridge *bridge = cookie;
	struct pvr_fwobject *obj;

	if (!fwbridge_ready(bridge))
		return;
	obj = fwbridge_object(bridge, object);
	if (obj == NULL || operations == 0 || (operations & ~(pre | post)) != 0 ||
	    ((operations & pre) != 0 && (operations & post) != 0)) {
		bridge->service.queue.failed = true;
		return;
	}
	pvr_fwservice_sync(&bridge->service, obj, operations);
}

static int
fwbridge_command(void *cookie, const struct rogue_fwif_kccb_cmd *command,
    uint32_t *rtn)
{
	struct pvr_fwbridge *bridge = cookie;
	struct pvr_fwprobe_result result = { 0 };
	int error;

	KASSERT(mutex_owned(bridge->lock));
	if (rtn == NULL)
		return EINVAL;
	*rtn = 0;
	if (!fwbridge_ready(bridge))
		return EIO;
	error = pvr_fwservice_command(&bridge->service, command,
	    &result);
	*rtn = result.after.rtn;
	return error;
}

static int
fwbridge_flush(void *cookie)
{
	struct pvr_fwbridge *bridge = cookie;

	if (!fwbridge_ready(bridge))
		return EIO;
	return pvr_fwservice_flush(&bridge->service);
}

static int
fwbridge_check(void *cookie)
{
	struct pvr_fwbridge *bridge = cookie;
	struct pvr_fwprobe_observation observation = { 0 };
	int error;

	if (!fwbridge_ready(bridge))
		return EIO;
	error = bridge->health != NULL ? bridge->health(bridge->health_cookie) : 0;
	if (error == 0)
		error = pvr_fwservice_observe(&bridge->service, &observation);
	if (error == 0 && bridge->health != NULL)
		error = bridge->health(bridge->health_cookie);
	if (error != 0)
		bridge->service.queue.failed = true;
	return error;
}

static int
fwbridge_set_events(void *cookie, const struct pvr_fw_events *events)
{
	struct pvr_fwbridge *bridge = cookie;
	int error;

	if (!fwbridge_ready(bridge))
		return EIO;
	if (events == NULL)
		return EINVAL;
	error = pvr_fwservice_set_events(&bridge->service,
	    events->cookie, events->grow_prepare, events->grow_complete);
	if (error == 0) {
		bridge->service.queue.fault_cookie = events->cookie;
		bridge->service.queue.page_fault = events->page_fault;
	}
	return error;
}

static void
fwbridge_fail(void *cookie)
{
	struct pvr_fwbridge *bridge = cookie;

	KASSERT(mutex_owned(bridge->lock));
	bridge->service.queue.failed = true;
}

int
pvr_fwbridge_prepare(struct pvr_fwbridge *bridge,
    kmutex_t *lock, bus_space_tag_t bst, bus_space_handle_t bsh,
    struct pvr_fwmem *memory,
    const struct pvr_fwboot_result *boot,
    const struct pvr_fwprobe_result *previous,
    enum pvr_fw_profile profile, int (*health)(void *), void *health_cookie)
{
	int error;

	if (bridge == NULL || lock == NULL)
		return EINVAL;
	KASSERT(mutex_owned(lock));
	if (bridge->attempted)
		return EBUSY;
	bridge->attempted = true;
	bridge->lock = lock;
	if (memory == NULL || (uintptr_t)memory % _Alignof(struct pvr_fwmem) != 0 ||
	    sizeof(*memory) - 1 > (uintptr_t)-1 - (uintptr_t)memory ||
	    memory->profile != profile ||
	    (profile != PVR_FW_PROFILE_A733 && profile != PVR_FW_PROFILE_GE8300) ||
	    (profile == PVR_FW_PROFILE_GE8300 &&
	    (health == NULL || health_cookie == NULL)) ||
	    ((health == NULL) != (health_cookie == NULL))) {
		bridge->service.queue.failed = true;
		return EINVAL;
	}
	bridge->health = health;
	bridge->health_cookie = health_cookie;
	error = pvr_fwservice_attach(&bridge->service, bst, bsh,
	    memory, boot, previous);
	if (error != 0) {
		bridge->service.queue.failed = true;
		return error;
	}
	bridge->backend = (struct pvr_backend) {
		.cookie = bridge,
		.enter = fwbridge_enter,
		.leave = fwbridge_leave,
		.alloc = fwbridge_alloc,
		.free = fwbridge_free,
		.sync = fwbridge_sync,
		.command = fwbridge_command,
		.flush = fwbridge_flush,
		.check = fwbridge_check,
		.fail = fwbridge_fail,
		.set_events = fwbridge_set_events,
	};
	return 0;
}

static unsigned
fwbridge_irq_mark(void *cookie)
{
	struct pvr_fwbridge *bridge = cookie;

	return bridge->irq_mark(bridge->health_cookie);
}

static void
fwbridge_irq_wait(void *cookie, unsigned mark, int ticks)
{
	struct pvr_fwbridge *bridge = cookie;

	bridge->irq_wait(bridge->health_cookie, mark, ticks);
}

void
pvr_fwbridge_set_irq(struct pvr_fwbridge *bridge, unsigned (*mark)(void *),
    void (*wait)(void *, unsigned, int))
{

	KASSERT(mutex_owned(bridge->lock));
	KASSERT(!bridge->registered && !bridge->publishing);
	KASSERT(mark != NULL && wait != NULL && bridge->health_cookie != NULL);
	bridge->irq_mark = mark;
	bridge->irq_wait = wait;
	bridge->backend.irq_mark = fwbridge_irq_mark;
	bridge->backend.irq_wait = fwbridge_irq_wait;
}

int
pvr_fwbridge_publish(struct pvr_fwbridge *bridge,
    struct drm_device *dev)
{
	int error;

	if (bridge == NULL || bridge->lock == NULL || dev == NULL)
		return EINVAL;
	KASSERT(!mutex_owned(bridge->lock));
	mutex_enter(bridge->lock);
	if (!fwbridge_ready(bridge))
		error = EIO;
	else if (bridge->registered)
		error = EALREADY;
	else if (bridge->publishing)
		error = EBUSY;
	else
		error = 0;
	if (error != 0) {
		mutex_exit(bridge->lock);
		return error;
	}
	bridge->publishing = true;
	mutex_exit(bridge->lock);
	error = pvr_backend_attach(dev, &bridge->backend);
	mutex_enter(bridge->lock);
	bridge->publishing = false;
	if (error != 0)
		bridge->service.queue.failed = true;
	else
		bridge->registered = true;
	mutex_exit(bridge->lock);
	return error;
}
