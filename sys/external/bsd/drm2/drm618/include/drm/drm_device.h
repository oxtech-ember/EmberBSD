/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * struct drm_device for the Linux 6.18 DRM core and its drivers.  The members
 * are those the MIT-licensed core, the drivers and the EmberBSD layer touch
 * (found by compiling them); the NetBSD block carries the bus handles and
 * the suspend and hotplug state of the EmberBSD attachment code.
 */

#ifndef _DRM618_DRM_DEVICE_H_
#define _DRM618_DRM_DEVICE_H_

#include <linux/bitops.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <drm/drm_mode_config.h>

#ifdef __NetBSD__
#include <dev/sysmon/sysmonvar.h>

#include <drm/drm_native_identity.h>
#include <drm/drm_wait_netbsd.h>
#endif

struct dentry;
struct device;
struct drm_driver;
struct drm_fb_helper;
struct drm_master;
struct drm_minor;
struct drm_vblank_crtc;
struct drm_vma_offset_manager;
struct inode;

/* Power state of a device that can be switched (vga_switcheroo). */
enum switch_power_state {
	DRM_SWITCH_POWER_ON = 0,
	DRM_SWITCH_POWER_OFF = 1,
	DRM_SWITCH_POWER_CHANGING = 2,
	DRM_SWITCH_POWER_DYNAMIC_OFF = 3,
};

/*
 * Recovery methods of a wedged device, one bit each; a driver passes the
 * methods it offers, least intrusive first, to drm_dev_wedged_event().
 */
#define DRM_WEDGE_RECOVERY_NONE		BIT(0)	/* already recovered by reset */
#define DRM_WEDGE_RECOVERY_REBIND	BIT(1)	/* unbind and rebind the driver */
#define DRM_WEDGE_RECOVERY_BUS_RESET	BIT(2)	/* reset the bus */
#define DRM_WEDGE_RECOVERY_VENDOR	BIT(3)	/* vendor-specific procedure */

/* The task blamed for a wedge; comm[0] == '\0' or pid < 0 means unknown. */
struct drm_wedge_task_info {
	pid_t pid;
	char comm[TASK_COMM_LEN];
};

struct drm_device {
	/* Identity and lifetime. */
	struct kref ref;
	struct device *dev;		/* parent bus device */
	struct device *dma_dev;		/* device that does the DMA, or NULL */
	const struct drm_driver *driver;
	u32 driver_features;		/* driver bits masked by the driver */
	char *unique;			/* bus id string */
	void *dev_private;		/* driver private data */
	bool registered;
	bool unplugged;
	int if_version;			/* interface version a client asked for */
	enum switch_power_state switch_power_state;

	/* Resources released with the device (drm_managed). */
	struct {
		struct list_head resources;
		void *final_kfree;
		spinlock_t lock;
	} managed;

	/* Device nodes. */
	struct drm_minor *primary;
	struct drm_minor *render;
	struct drm_minor *accel;
	struct inode *anon_inode;	/* its mapping backs all GEM mmaps */
	struct dentry *debugfs_root;

	/* Open files and in-kernel clients. */
	atomic_t open_count;
	struct mutex filelist_mutex;
	struct list_head filelist;
	struct list_head filelist_internal;
	struct mutex clientlist_mutex;
	struct list_head clientlist;
	struct mutex master_mutex;
	struct drm_master *master;

	/* GEM. */
	struct mutex object_name_lock;
	struct idr object_name_idr;
	struct drm_vma_offset_manager *vma_offset_manager;

	/* Display. */
	struct drm_mode_config mode_config;
	struct drm_fb_helper *fb_helper;

	/* Events and vblank. */
	spinlock_t event_lock;
	struct list_head vblank_event_list;
	spinlock_t vbl_lock;
	spinlock_t vblank_time_lock;
	struct drm_vblank_crtc *vblank;
	unsigned int num_crtcs;
	u32 max_vblank_count;
	bool vblank_disable_immediate;

#ifdef __NetBSD__
	/* Owned until sysctl readers drain during unregister. */
	struct drm_native_identity *native_identity;
	struct drm_native_pci_record native_pci;
	struct drm_native_platform_record native_platform;

	struct drm_bus_irq_cookie *irq_cookie;

	bus_space_tag_t bst;
	struct drm_bus_map *bus_maps;
	unsigned bus_nmaps;
	bus_dma_tag_t bus_dmat;		/* the bus's full DMA tag */
	bus_dma_tag_t bus_dmat32;	/* its 32-bit DMA tag */
	bus_dma_tag_t dmat;		/* the driver's tag, may be a subregion */
	bool dmat_subregion_p;
	bus_addr_t dmat_subregion_min;
	bus_addr_t dmat_subregion_max;
	struct vmem *cma_pool;

	struct sysmon_pswitch sc_monitor_hotplug;
	struct mutex suspend_lock;
	drm_waitqueue_t suspend_cv;
	uint64_t active_ioctls;
	struct lwp *suspender;
#endif
};

/* Name the device that does the DMA when it is not the parent; NULL clears. */
void drm_dev_set_dma_dev(struct drm_device *dev, struct device *dma_dev);

/* The device that does the DMA: the one set, else the parent. */
static inline struct device *
drm_dev_dma_dev(struct drm_device *dev)
{

	return dev->dma_dev != NULL ? dev->dma_dev : dev->dev;
}

#endif	/* _DRM618_DRM_DEVICE_H_ */
