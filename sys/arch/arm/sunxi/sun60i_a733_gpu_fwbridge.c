/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native implementation. */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <arm/sunxi/sun60i_a733_gpu_fwbridge.h>

static void
fwbridge_enter(void *cookie)
{
	struct sun60i_a733_gpu_fwbridge *bridge = cookie;

	mutex_enter(bridge->lock);
}

static void
fwbridge_leave(void *cookie)
{
	struct sun60i_a733_gpu_fwbridge *bridge = cookie;

	mutex_exit(bridge->lock);
}

static bool
fwbridge_ready(const struct sun60i_a733_gpu_fwbridge *bridge)
{

	KASSERT(mutex_owned(bridge->lock));
	return bridge->service.queue.attached && !bridge->service.queue.failed;
}

/* Never dereference a caller's owner pointer before membership is proven. */
static struct sun60i_a733_gpu_fwobject *
fwbridge_object(const struct sun60i_a733_gpu_fwbridge *bridge,
    const struct pvr_fw_object *object)
{
	struct sun60i_a733_gpu_fwobject *obj;

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
	struct sun60i_a733_gpu_fwbridge *bridge = cookie;
	struct sun60i_a733_gpu_fwobject *obj = NULL;
	int error;

	if (!fwbridge_ready(bridge))
		return EIO;
	if (out == NULL || out->cpu != NULL || out->owner != NULL ||
	    out->size != 0 || out->address != 0)
		return EINVAL;
	error = sun60i_a733_gpu_fwservice_alloc(&bridge->service, size, &obj);
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
	struct sun60i_a733_gpu_fwbridge *bridge = cookie;
	struct sun60i_a733_gpu_fwobject *obj;
	int error;

	if (!fwbridge_ready(bridge))
		return EIO;
	obj = fwbridge_object(bridge, object);
	if (obj == NULL)
		return EINVAL;
	error = sun60i_a733_gpu_fwservice_free(&bridge->service, &obj);
	if (error == 0)
		memset(object, 0, sizeof(*object));
	return error;
}

static void
fwbridge_sync(void *cookie, struct pvr_fw_object *object, int operations)
{
	const int pre = BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE;
	const int post = BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE;
	struct sun60i_a733_gpu_fwbridge *bridge = cookie;
	struct sun60i_a733_gpu_fwobject *obj;

	if (!fwbridge_ready(bridge))
		return;
	obj = fwbridge_object(bridge, object);
	if (obj == NULL || operations == 0 || (operations & ~(pre | post)) != 0 ||
	    ((operations & pre) != 0 && (operations & post) != 0)) {
		bridge->service.queue.failed = true;
		return;
	}
	sun60i_a733_gpu_fwservice_sync(&bridge->service, obj, operations);
}

static int
fwbridge_command(void *cookie, const struct rogue_fwif_kccb_cmd *command,
    uint32_t *rtn)
{
	struct sun60i_a733_gpu_fwbridge *bridge = cookie;
	struct sun60i_a733_gpu_fwprobe_result result = { 0 };
	int error;

	KASSERT(mutex_owned(bridge->lock));
	if (rtn == NULL)
		return EINVAL;
	*rtn = 0;
	if (!fwbridge_ready(bridge))
		return EIO;
	error = sun60i_a733_gpu_fwservice_command(&bridge->service, command,
	    &result);
	*rtn = result.after.rtn;
	return error;
}

static int
fwbridge_flush(void *cookie)
{
	struct sun60i_a733_gpu_fwbridge *bridge = cookie;

	if (!fwbridge_ready(bridge))
		return EIO;
	return sun60i_a733_gpu_fwservice_flush(&bridge->service);
}

static int
fwbridge_check(void *cookie)
{
	struct sun60i_a733_gpu_fwbridge *bridge = cookie;
	struct sun60i_a733_gpu_fwprobe_observation observation = { 0 };
	int error;

	if (!fwbridge_ready(bridge))
		return EIO;
	error = sun60i_a733_gpu_fwservice_observe(&bridge->service, &observation);
	if (error != 0)
		bridge->service.queue.failed = true;
	return error;
}

static void
fwbridge_fail(void *cookie)
{
	struct sun60i_a733_gpu_fwbridge *bridge = cookie;

	KASSERT(mutex_owned(bridge->lock));
	bridge->service.queue.failed = true;
}

int
sun60i_a733_gpu_fwbridge_prepare(struct sun60i_a733_gpu_fwbridge *bridge,
    kmutex_t *lock, bus_space_tag_t bst, bus_space_handle_t bsh,
    struct sun60i_a733_gpu_fwmem *memory,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *previous)
{
	int error;

	if (bridge == NULL || lock == NULL)
		return EINVAL;
	KASSERT(mutex_owned(lock));
	if (bridge->attempted)
		return EBUSY;
	bridge->attempted = true;
	bridge->lock = lock;
	error = sun60i_a733_gpu_fwservice_attach(&bridge->service, bst, bsh,
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
	};
	return 0;
}

int
sun60i_a733_gpu_fwbridge_publish(struct sun60i_a733_gpu_fwbridge *bridge,
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
