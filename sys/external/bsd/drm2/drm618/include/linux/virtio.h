/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_drv.c, virtgpu_drv.h,
 * virtgpu_kms.c, virtgpu_vq.c of dist618 (MIT) for the calls and members the
 * driver uses; dev/pci/virtiovar.h and virtio.c of NetBSD; the GPL headers
 * of Linux were not read.
 *
 * Shadow of <linux/virtio.h> for the 6.18 core: the Linux view of a virtio
 * device (the driver's probe, remove, config change; the queues as handles
 * with a number of free slots; commands as scatter lists) over NetBSD's
 * virtio(4).  The implementation is drm618_virtio.c.
 *
 * NetBSD's own struct virtqueue is never meant here: the sources of the 6.18
 * driver name struct virtqueue, which this header renames to
 * struct linux_virtqueue, except in drm618_virtio.c (DRM618_VIRTIO_NATIVE),
 * where the native one is in use.
 */

#ifndef _DRM618_LINUX_VIRTIO_H_
#define _DRM618_LINUX_VIRTIO_H_

#include <sys/types.h>
#include <sys/device.h>

#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/device.h>
#include <linux/scatterlist.h>

struct virtio_device;
struct linux_virtqueue;
struct irq_affinity;

#ifndef DRM618_VIRTIO_NATIVE
#define	virtqueue	linux_virtqueue
#endif

typedef void vq_callback_t(struct linux_virtqueue *);

/* What a driver may read of a queue; the rest of it is private. */
struct linux_virtqueue {
	struct virtio_device	*vdev;
	unsigned int		index;
	unsigned int		num_free;	/* slots of the ring still free */
	unsigned int		num_max;	/* slots of the ring */
	const char		*name;
	vq_callback_t		*callback;
	void			*priv;
};

struct virtqueue_info {
	const char	*name;
	vq_callback_t	*callback;
	bool		ctx;		/* not supported: must be false */
};

/* vdev->dev: the parent of the DRM device; the layer has no struct device inside. */
struct virtio_device_dev {
	struct device	*parent;
};

/* What a driver calls on vdev->config. */
struct virtio_config_ops {
	void	(*del_vqs)(struct virtio_device *);
	void	(*reset)(struct virtio_device *);
};

struct virtio_device {
	struct virtio_device_dev	dev;
	void				*priv;	/* the driver's */
	const struct virtio_config_ops	*config;
	u64				features;
	void				*layer;	/* drm618_virtio.c */
};

struct virtio_device_id {
	u32	device;
	u32	vendor;
};
#define	VIRTIO_DEV_ANY_ID	0xffffffffu

struct virtio_driver {
	struct {
		const char	*name;
	} driver;
	const struct virtio_device_id	*id_table;
	const unsigned int		*feature_table;
	unsigned int			feature_table_size;
	int	(*probe)(struct virtio_device *);
	void	(*remove)(struct virtio_device *);
	void	(*shutdown)(struct virtio_device *);
	void	(*config_changed)(struct virtio_device *);
};

struct virtio_shm_region {
	u64	addr;
	u64	len;
};

/* A driver that matches a device is probed at its attach; -EEXIST if one with the same id table is there. */
int	register_virtio_driver(struct virtio_driver *);
void	unregister_virtio_driver(struct virtio_driver *);

int	virtio_find_vqs(struct virtio_device *, unsigned int,
	    struct linux_virtqueue *[], struct virtqueue_info[],
	    struct irq_affinity *);
void	virtio_device_ready(struct virtio_device *);
void	virtio_reset_device(struct virtio_device *);
bool	virtio_has_feature(const struct virtio_device *, unsigned int);
bool	virtio_get_shm_region(struct virtio_device *, struct virtio_shm_region *, u8);

/*
 * Commands: out_sgs scatter lists the device reads, then in_sgs it writes;
 * data comes back from virtqueue_get_buf when the device is done.  Never
 * sleeps (gfp is not looked at).  -ENOSPC: no free slots (retry when
 * num_free is large enough), -EINVAL: cannot be queued, -ENODEV: reset.
 */
int	virtqueue_add_sgs(struct linux_virtqueue *, struct scatterlist *[],
	    unsigned int, unsigned int, void *, gfp_t);
bool	virtqueue_kick_prepare(struct linux_virtqueue *);
bool	virtqueue_notify(struct linux_virtqueue *);
void	*virtqueue_get_buf(struct linux_virtqueue *, unsigned int *);
void	virtqueue_disable_cb(struct linux_virtqueue *);
bool	virtqueue_enable_cb(struct linux_virtqueue *);
void	*virtqueue_detach_unused_buf(struct linux_virtqueue *);

/* Little endian access to the configuration space of a device (version 1: all of it is). */
u64	drm618_virtio_cread(struct virtio_device *, unsigned int, unsigned int);
void	drm618_virtio_cwrite(struct virtio_device *, unsigned int, unsigned int, u64);

static inline bool
virtio_has_dma_quirk(const struct virtio_device *vdev)
{

	/* VIRTIO_F_ACCESS_PLATFORM (bit 33) */
	return !virtio_has_feature(vdev, 33);
}

/* dev_name(&vdev->dev) and the memory region request of a virtio device. */
const char *drm618_virtio_dev_name(const struct virtio_device_dev *);
#undef	dev_name
#define	dev_name(d)	_Generic((d),					\
	struct virtio_device_dev *: drm618_virtio_dev_name,		\
	const struct virtio_device_dev *: drm618_virtio_dev_name,	\
	default: device_xname)(d)

/* No resource manager for the memory windows of a device: the request is granted. */
static inline void *
devm_request_mem_region(struct virtio_device_dev *dev __unused,
    u64 start __unused, u64 n __unused, const char *name __unused)
{
	static char granted;

	return &granted;
}

#endif	/* _DRM618_LINUX_VIRTIO_H_ */
