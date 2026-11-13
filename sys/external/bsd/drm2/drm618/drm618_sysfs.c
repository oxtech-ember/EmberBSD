/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Written to the interface of
 * the MIT header drm_internal.h (names and signatures of the drm_sysfs.c
 * functions), the documentation of the DRM class devices and the 5.6 rewrite
 * in drm/drm_sysfs.c of this tree (the device monitor event); no text of the
 * GPL-only upstream drm_sysfs.c was read.
 *
 * The class devices (see drm618_kdev.h) are small objects of the layer; there
 * is no sysfs.  A hotplug or property change is published through the native
 * device monitor, as in the 5.6 core.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <prop/proplib.h>

#include <linux/err.h>
#include <linux/kdev_t.h>

#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_sysfs.h>

#include "../dist618/drm/drm_internal.h"

#define	KDEV_MAGIC	0x6b646576u	/* "kdev" */
#define	KDEV_NAMELEN	64

struct drm618_kdev {
	uint32_t	kd_magic;
	unsigned int	kd_devt;
	char		kd_name[KDEV_NAMELEN];
	bool		kd_registered;
	void		*kd_drvdata;
	volatile unsigned kd_refcnt;
};

static struct drm618_kdev *
kdev_from(const struct device *dev)
{
	const struct drm618_kdev *kd = (const void *)dev;

	KASSERTMSG(kd != NULL && kd->kd_magic == KDEV_MAGIC,
	    "not a class device: %p", dev);
	return __UNCONST(kd);
}

struct device *
drm618_kdev_create(unsigned int devt, void *drvdata, const char *fmt, ...)
{
	struct drm618_kdev *kd;
	va_list ap;

	kd = kmem_zalloc(sizeof(*kd), KM_SLEEP);
	kd->kd_magic = KDEV_MAGIC;
	kd->kd_devt = devt;
	kd->kd_drvdata = drvdata;
	kd->kd_refcnt = 1;
	va_start(ap, fmt);
	vsnprintf(kd->kd_name, sizeof(kd->kd_name), fmt, ap);
	va_end(ap);
	return (struct device *)kd;
}

void
drm618_kdev_put(struct device *dev)
{
	struct drm618_kdev *kd;

	if (dev == NULL)
		return;
	kd = kdev_from(dev);
	if (atomic_dec_uint_nv(&kd->kd_refcnt) == 0) {
		kd->kd_magic = 0;
		kmem_free(kd, sizeof(*kd));
	}
}

int
device_add(struct device *dev)
{
	struct drm618_kdev *kd = kdev_from(dev);

	KASSERT(!kd->kd_registered);
	kd->kd_registered = true;
	return 0;
}

void
device_del(struct device *dev)
{
	struct drm618_kdev *kd = kdev_from(dev);

	kd->kd_registered = false;
}

bool
device_is_registered(struct device *dev)
{

	return kdev_from(dev)->kd_registered;
}

void
dev_set_drvdata(struct device *dev, void *data)
{

	kdev_from(dev)->kd_drvdata = data;
}

void *
dev_get_drvdata(const struct device *dev)
{

	return kdev_from(dev)->kd_drvdata;
}

unsigned int
drm618_kdev_devt(const struct device *dev)
{

	return kdev_from(dev)->kd_devt;
}

const char *
drm618_kdev_name(const struct device *dev)
{

	return kdev_from(dev)->kd_name;
}

int
drm618_kdev_uevent(struct device *dev, char *envp[] __unused)
{

	aprint_debug("drm618: uevent on %s not delivered (no sysfs)\n",
	    drm618_kdev_name(dev));
	return 0;
}

int
drm618_kdev_create_link(struct device *dev __unused, const char *name __unused)
{

	return 0;
}

void
drm618_kdev_remove_link(struct device *dev __unused, const char *name __unused)
{
}

/* ---- the functions of drm_internal.h ----------------------------------- */

int
drm_sysfs_init(void)
{

	return 0;
}

void
drm_sysfs_destroy(void)
{
}

struct device *
drm_sysfs_minor_alloc(struct drm_minor *minor)
{
	const char *prefix;
	struct device *kdev;

	switch (minor->type) {
	case DRM_MINOR_RENDER:
		prefix = "renderD";
		break;
	case DRM_MINOR_ACCEL:
		prefix = "accel";
		break;
	default:
		prefix = "card";
		break;
	}
	kdev = drm618_kdev_create(MKDEV(DRM_MAJOR, minor->index), minor,
	    "%s%d", prefix, minor->index);
	return kdev;
}

int
drm_sysfs_connector_add(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	if (connector->kdev != NULL)
		return 0;
	connector->kdev = drm618_kdev_create(0, connector, "card%d-%s",
	    dev->primary != NULL ? dev->primary->index : 0, connector->name);
	return 0;
}

int
drm_sysfs_connector_add_late(struct drm_connector *connector __unused)
{

	return 0;
}

void
drm_sysfs_connector_remove_early(struct drm_connector *connector __unused)
{
}

void
drm_sysfs_connector_remove(struct drm_connector *connector)
{

	if (connector->kdev == NULL)
		return;
	drm618_kdev_put(connector->kdev);
	connector->kdev = NULL;
}

int
drm_class_device_register(struct device *dev __unused)
{

	return 0;
}

void
drm_class_device_unregister(struct device *dev __unused)
{
}

/*
 * Publish an advisory change through the native device monitor.  Callers
 * retain the DRM device and stop notification work before teardown.  This
 * can sleep: IRQ handlers must defer to process context.
 */
static void
drm_sysfs_device_change(struct drm_device *dev)
{
	prop_dictionary_t event;
	char path[32];
	int length;

	if (dev == NULL || !dev->registered || dev->primary == NULL ||
	    dev->dev == NULL || dev->primary->index < 0)
		return;
	if (cpu_intr_p() || cpu_softintr_p()) {
		KASSERTMSG(false, "DRM hotplug requires process context");
		return;
	}
	length = snprintf(path, sizeof(path), "dri/card%d", dev->primary->index);
	if (length < 0 || (size_t)length >= sizeof(path))
		return;
	event = prop_dictionary_create();
	if (event == NULL)
		return;
	if (!prop_dictionary_set_string(event, "device", path) ||
	    !prop_dictionary_set_string(event, "parent", device_xname(dev->dev))) {
		prop_object_release(event);
		return;
	}
	/* Success consumes event, including the no-listener case. */
	if ((*devmon_insert_vec)("device-change", event) != 0)
		prop_object_release(event);
}

void
drm_sysfs_hotplug_event(struct drm_device *dev)
{

	drm_sysfs_device_change(dev);
}

void
drm_sysfs_connector_hotplug_event(struct drm_connector *connector)
{

	if (connector != NULL)
		drm_sysfs_device_change(connector->dev);
}

void
drm_sysfs_connector_property_event(struct drm_connector *connector,
    struct drm_property *property __unused)
{

	if (connector != NULL)
		drm_sysfs_device_change(connector->dev);
}

void
drm_sysfs_lease_event(struct drm_device *dev)
{

	drm_sysfs_device_change(dev);
}
