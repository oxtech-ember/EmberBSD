/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_drv.c, virtgpu_kms.c,
 * virtgpu_vq.c, virtgpu_drv.h of dist618 (MIT) for the calls the driver
 * makes; drm618/include/linux/virtio*.h; dev/pci/virtiovar.h, virtio.c and
 * ld_virtio.c of NetBSD; linux/linux_virtio.c of this tree (the layer of the
 * 5.6 virtio-gpu, BSD); the GPL sources of Linux were not read.
 *
 * The Linux view of a virtio device over NetBSD's virtio(4).
 *
 *  - A driver registers a struct virtio_driver; the autoconf glue
 *    (virtiogpu618, attached at virtio) matches the device id, negotiates the
 *    features of the driver's table and calls its probe from the attach.
 *  - The queues are found in probe (virtio_find_vqs), which also finishes the
 *    NetBSD child attach: from then on the device is live.
 *  - A command is a list of scatter lists.  Every piece that is physically
 *    contiguous becomes a DMA map of its own (load_raw of the physical
 *    address: the machine has no IOMMU, see drm618_scatterlist.c).
 *  - Queue callbacks run in the interrupt handler under the device lock and
 *    only schedule work; the owner drains that work before del_vqs.
 *
 * Failures are said: a command that cannot be queued is an error code to the
 * driver and a line in the log (never a silent drop), a buffer still queued
 * when the queues go is named.
 */

#define	DRM618_VIRTIO_NATIVE

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/systm.h>

#include <dev/pci/virtiovar.h>
#include <dev/pci/virtioreg.h>

#include <linux/virtio.h>
#include <linux/scatterlist.h>

#include <drm618_virtio.h>

#define	DRM618_VIRTIO_MAXQ	4
/* The most pieces one command may have (the segments of an indirect table). */
#define	DRM618_VIRTIO_MAXSEG	128
#define	DRM618_VIRTIO_MAXREQ	(1024 * 1024)

/* The log says what failed, rate limited by the caller's own rarity. */
#define	VQLOG(sc, ...)	do {						\
	aprint_error_dev((sc)->sc_dev, __VA_ARGS__);			\
} while (0)

struct drm618_vmap {
	bus_dmamap_t	map;
	bool		readable;	/* device reads it (host to device) */
	bool		loaded;
};

struct drm618_vreq {
	void		*cookie;
	unsigned int	capacity;	/* maps allocated */
	unsigned int	nmaps;		/* maps made */
	unsigned int	descriptors;
	unsigned int	cost;		/* slots of the ring it holds */
	struct drm618_vmap	maps[];
};

struct drm618_vq {
	struct linux_virtqueue	pub;		/* first: the handle of the driver */
	struct drm618_vdev	*sc;
	struct virtqueue	*nvq;		/* in sc->sc_nvq */
	struct drm618_vreq	**requests;	/* by slot */
	unsigned int		size;
	bool			callbacks;
};

enum drm618_vstate { VS_INIT, VS_QUEUES, VS_READY, VS_STOPPED };

struct drm618_vdev {
	device_t		sc_dev;
	struct virtio_softc	*sc_vsc;
	struct virtio_device	sc_vdev;
	struct virtio_driver	*sc_drv;
	bus_dma_tag_t		sc_dmat;
	kmutex_t		sc_lock;	/* IPL_VM, spin */
	enum drm618_vstate	sc_state;
	struct virtqueue	sc_nvq[DRM618_VIRTIO_MAXQ];
	struct drm618_vq	sc_vq[DRM618_VIRTIO_MAXQ];
	unsigned int		sc_nvqs;
	bool			sc_attached;	/* child attach finished */
	bool			sc_probed;	/* the driver's probe succeeded */
	bool			sc_lock_init;
	LIST_ENTRY(drm618_vdev)	sc_entry;
	bool			sc_listed;
};

static LIST_HEAD(, drm618_vdev) drm618_vdevs = LIST_HEAD_INITIALIZER(drm618_vdevs);

#define	to_vdev(vd)	((struct drm618_vdev *)(vd)->layer)

/* ---- drivers ---------------------------------------------------------- */

static kmutex_t drm618_vdrv_lock;
static bool drm618_vdrv_init;
static struct virtio_driver *drm618_vdrv[4];

static void
vdrv_once(void)
{

	if (!drm618_vdrv_init) {
		mutex_init(&drm618_vdrv_lock, MUTEX_DEFAULT, IPL_NONE);
		drm618_vdrv_init = true;
	}
}

int
register_virtio_driver(struct virtio_driver *drv)
{
	unsigned int i;
	int ret = -ENOSPC;

	vdrv_once();
	mutex_enter(&drm618_vdrv_lock);
	for (i = 0; i < __arraycount(drm618_vdrv); i++) {
		if (drm618_vdrv[i] == drv) {
			ret = -EEXIST;
			break;
		}
		if (drm618_vdrv[i] == NULL) {
			drm618_vdrv[i] = drv;
			ret = 0;
			break;
		}
	}
	mutex_exit(&drm618_vdrv_lock);
	if (ret != 0)
		printf("virtio: cannot register driver %s: %d\n",
		    drv->driver.name, ret);
	return ret;
}

void
unregister_virtio_driver(struct virtio_driver *drv)
{
	unsigned int i;

	vdrv_once();
	mutex_enter(&drm618_vdrv_lock);
	for (i = 0; i < __arraycount(drm618_vdrv); i++)
		if (drm618_vdrv[i] == drv)
			drm618_vdrv[i] = NULL;
	mutex_exit(&drm618_vdrv_lock);
}

static struct virtio_driver *
vdrv_find(int devid)
{
	struct virtio_driver *found = NULL;
	unsigned int i;
	const struct virtio_device_id *id;

	vdrv_once();
	mutex_enter(&drm618_vdrv_lock);
	for (i = 0; i < __arraycount(drm618_vdrv) && found == NULL; i++) {
		if (drm618_vdrv[i] == NULL)
			continue;
		for (id = drm618_vdrv[i]->id_table; id != NULL && id->device != 0;
		    id++)
			if (id->device == (u32)devid) {
				found = drm618_vdrv[i];
				break;
			}
	}
	mutex_exit(&drm618_vdrv_lock);
	return found;
}

/* ---- the device ------------------------------------------------------- */

/* The first non-zero answer of busy for a probed device, 0 if none (the module unload asks). */
int
drm618_virtio_busy(int (*busy)(struct virtio_device *, void *), void *arg)
{
	struct drm618_vdev *sc;
	int error = 0;

	vdrv_once();
	mutex_enter(&drm618_vdrv_lock);
	LIST_FOREACH(sc, &drm618_vdevs, sc_entry) {
		if (!sc->sc_probed)
			continue;
		error = busy(&sc->sc_vdev, arg);
		if (error != 0)
			break;
	}
	mutex_exit(&drm618_vdrv_lock);
	return error;
}

const char *
drm618_virtio_dev_name(const struct virtio_device_dev *d)
{
	const struct virtio_device *vd = (const struct virtio_device *)
	    ((const char *)d - offsetof(struct virtio_device, dev));

	return device_xname(to_vdev(vd)->sc_dev);
}

bool
virtio_has_feature(const struct virtio_device *vd, unsigned int bit)
{

	return bit < 64 && (vd->features & ((u64)1 << bit)) != 0;
}

bool
virtio_get_shm_region(struct virtio_device *vd, struct virtio_shm_region *r,
    u8 id)
{

	/* The shared memory windows of a device (host visible blobs) are not mapped here. */
	(void)r;
	aprint_debug_dev(to_vdev(vd)->sc_dev, "no shared memory region %u: host"
	    " visible resources are not supported\n", id);
	return false;
}

u64
drm618_virtio_cread(struct virtio_device *vd, unsigned int off,
    unsigned int size)
{
	struct drm618_vdev *sc = to_vdev(vd);
	u64 v = 0;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_state != VS_STOPPED) {
		switch (size) {
		case 1:
			v = virtio_read_device_config_1(sc->sc_vsc, off);
			break;
		case 2:
			v = virtio_read_device_config_le_2(sc->sc_vsc, off);
			break;
		case 4:
			v = virtio_read_device_config_le_4(sc->sc_vsc, off);
			break;
		case 8:
			v = virtio_read_device_config_le_4(sc->sc_vsc, off) |
			    ((u64)virtio_read_device_config_le_4(sc->sc_vsc,
			    off + 4) << 32);
			break;
		default:
			VQLOG(sc, "config read of %u bytes\n", size);
			break;
		}
	}
	mutex_exit(&sc->sc_lock);
	return v;
}

void
drm618_virtio_cwrite(struct virtio_device *vd, unsigned int off,
    unsigned int size, u64 v)
{
	struct drm618_vdev *sc = to_vdev(vd);

	mutex_enter(&sc->sc_lock);
	if (sc->sc_state != VS_STOPPED) {
		switch (size) {
		case 1:
			virtio_write_device_config_1(sc->sc_vsc, off, v);
			break;
		case 2:
			virtio_write_device_config_le_2(sc->sc_vsc, off, v);
			break;
		case 4:
			virtio_write_device_config_le_4(sc->sc_vsc, off, v);
			break;
		case 8:
			virtio_write_device_config_le_4(sc->sc_vsc, off, (u32)v);
			virtio_write_device_config_le_4(sc->sc_vsc, off + 4,
			    (u32)(v >> 32));
			break;
		default:
			VQLOG(sc, "config write of %u bytes\n", size);
			break;
		}
	}
	mutex_exit(&sc->sc_lock);
}

/* ---- requests ----------------------------------------------------------- */

static size_t
vreq_size(unsigned int capacity)
{

	return sizeof(struct drm618_vreq) +
	    capacity * sizeof(struct drm618_vmap);
}

static void
vreq_free(struct drm618_vdev *sc, struct drm618_vreq *req)
{
	struct drm618_vmap *m;
	unsigned int i;

	for (i = 0; i < req->nmaps; i++) {
		m = &req->maps[i];
		if (m->loaded) {
			bus_dmamap_sync(sc->sc_dmat, m->map, 0,
			    m->map->dm_mapsize, m->readable ?
			    BUS_DMASYNC_POSTWRITE : BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, m->map);
		}
		bus_dmamap_destroy(sc->sc_dmat, m->map);
	}
	kmem_intr_free(req, vreq_size(req->capacity));
}

struct vreq_walk {
	struct drm618_vdev	*sc;
	struct drm618_vreq	*req;	/* NULL while counting */
	unsigned int		pieces;
	size_t			bytes;
	bool			readable;
	int			error;
};

static int
vreq_piece(void *arg, paddr_t pa, size_t len)
{
	struct vreq_walk *w = arg;
	struct drm618_vdev *sc = w->sc;
	struct drm618_vmap *m;
	bus_dma_segment_t seg;
	int error;

	if (w->pieces == DRM618_VIRTIO_MAXSEG ||
	    len > DRM618_VIRTIO_MAXREQ - w->bytes) {
		w->error = -EINVAL;
		return -EINVAL;
	}
	w->pieces++;
	w->bytes += len;
	if (w->req == NULL)
		return 0;
	m = &w->req->maps[w->req->nmaps];
	m->readable = w->readable;
	error = bus_dmamap_create(sc->sc_dmat, len, 1, len, 0, BUS_DMA_NOWAIT,
	    &m->map);
	if (error != 0) {
		w->error = -error;
		return w->error;
	}
	w->req->nmaps++;
	seg.ds_addr = pa;
	seg.ds_len = len;
	error = bus_dmamap_load_raw(sc->sc_dmat, m->map, &seg, 1, len,
	    BUS_DMA_NOWAIT | (m->readable ? BUS_DMA_WRITE : BUS_DMA_READ));
	if (error != 0) {
		w->error = -error;
		return w->error;
	}
	m->loaded = true;
	return 0;
}

/* Visit every piece of the lists: the counting pass (req NULL) and the mapping pass. */
static int
vreq_pieces(struct vreq_walk *w, struct scatterlist *sgs[],
    unsigned int out, unsigned int in)
{
	struct scatterlist *sg;
	unsigned int i;
	int error;

	for (i = 0; i < out + in; i++) {
		w->readable = i < out;
		for (sg = sgs[i]; sg != NULL; sg = sg_next(sg)) {
			error = drm618_sg_walk(sg, vreq_piece, w);
			if (error != 0) {
				if (w->error == 0)
					w->error = error == -EFAULT ?
					    -EINVAL : error;
				return w->error;
			}
		}
	}
	return 0;
}

/* ---- queues ------------------------------------------------------------- */

static int
vq_intr(void *arg)
{
	struct drm618_vq *vq = arg;
	struct drm618_vdev *sc = vq->sc;
	int handled = 0;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_state == VS_READY && vq->callbacks &&
	    vq->pub.callback != NULL) {
		/* The callback schedules work; it does not reenter the transport. */
		vq->pub.callback(&vq->pub);
		handled = 1;
	}
	mutex_exit(&sc->sc_lock);
	return handled;
}

static void vdev_del_vqs(struct virtio_device *);
static void vdev_reset(struct virtio_device *);
static int vdev_config_change(struct virtio_softc *);

static const struct virtio_config_ops drm618_vconfig = {
	.del_vqs = vdev_del_vqs,
	.reset = vdev_reset,
};

int
virtio_find_vqs(struct virtio_device *vd, unsigned int nvqs,
    struct linux_virtqueue *vqs[], struct virtqueue_info info[],
    struct irq_affinity *desc)
{
	struct drm618_vdev *sc = to_vdev(vd);
	struct drm618_vq *vq;
	unsigned int i;
	int error;

	if (sc->sc_state != VS_INIT || nvqs == 0 || nvqs > DRM618_VIRTIO_MAXQ ||
	    vqs == NULL || info == NULL || desc != NULL)
		return -EINVAL;
	for (i = 0; i < nvqs; i++) {
		vqs[i] = NULL;
		if (info[i].callback == NULL || info[i].name == NULL ||
		    info[i].ctx) {
			VQLOG(sc, "queue %u: callback, name or context not usable\n", i);
			return -EINVAL;
		}
	}
	sc->sc_nvqs = nvqs;
	for (i = 0; i < nvqs; i++) {
		vq = &sc->sc_vq[i];
		vq->sc = sc;
		vq->nvq = &sc->sc_nvq[i];
		vq->pub.vdev = vd;
		vq->pub.index = i;
		vq->pub.name = info[i].name;
		vq->pub.callback = info[i].callback;
		virtio_init_vq(sc->sc_vsc, vq->nvq, i, vq_intr, vq);
		/* The native allocator uses 32768 as its end of chain. */
		if (vq->nvq->vq_num == 0 || vq->nvq->vq_num > 32768) {
			VQLOG(sc, "queue %u: size %u\n", i, vq->nvq->vq_num);
			error = -EINVAL;
			goto fail;
		}
		vq->size = vq->nvq->vq_num;
		error = virtio_alloc_vq(sc->sc_vsc, vq->nvq, DRM618_VIRTIO_MAXREQ,
		    MIN(vq->size, DRM618_VIRTIO_MAXSEG), info[i].name);
		if (error != 0) {
			VQLOG(sc, "queue %u (%s): no memory for the ring\n", i,
			    info[i].name);
			error = -ENOMEM;
			goto fail;
		}
		vq->requests = kmem_zalloc(vq->size * sizeof(*vq->requests),
		    KM_SLEEP);
		vq->pub.num_free = vq->pub.num_max = vq->size;
		vq->callbacks = true;
	}
	mutex_enter(&sc->sc_lock);
	sc->sc_state = VS_QUEUES;
	mutex_exit(&sc->sc_lock);
	/* Even a failed finish leaves sc_vqs of the native device pointing at our array. */
	sc->sc_attached = true;
	if (virtio_child_attach_finish(sc->sc_vsc, sc->sc_nvq, nvqs,
	    vdev_config_change, VIRTIO_F_INTR_MPSAFE) != 0) {
		VQLOG(sc, "the child attach did not finish\n");
		error = -EIO;
		goto fail;
	}
	for (i = 0; i < nvqs; i++)
		vqs[i] = &sc->sc_vq[i].pub;
	return 0;
fail:
	vdev_del_vqs(vd);
	return error;
}

void
virtio_device_ready(struct virtio_device *vd)
{
	struct drm618_vdev *sc = to_vdev(vd);

	mutex_enter(&sc->sc_lock);
	if (sc->sc_state == VS_QUEUES)
		sc->sc_state = VS_READY;
	mutex_exit(&sc->sc_lock);
}

int
virtqueue_add_sgs(struct linux_virtqueue *lvq, struct scatterlist *sgs[],
    unsigned int out, unsigned int in, void *cookie, gfp_t gfp __unused)
{
	struct drm618_vq *vq = (struct drm618_vq *)lvq;
	struct drm618_vdev *sc = vq->sc;
	struct drm618_vreq *req = NULL;
	struct vreq_walk w;
	unsigned int j;
	int error, slot;

	if (cookie == NULL || sgs == NULL || out + in == 0) {
		VQLOG(sc, "%s: a command without data or without a token\n",
		    vq->pub.name);
		return -EINVAL;
	}
	memset(&w, 0, sizeof(w));
	w.sc = sc;
	error = vreq_pieces(&w, sgs, out, in);	/* count, validate */
	if (error != 0) {
		VQLOG(sc, "%s: a command that cannot be queued (%u pieces,"
		    " %zu bytes): %d\n", vq->pub.name, w.pieces, w.bytes, error);
		return error;
	}
	if (w.pieces == 0) {
		VQLOG(sc, "%s: an empty command\n", vq->pub.name);
		return -EINVAL;
	}
	mutex_enter(&sc->sc_lock);
	if (sc->sc_state != VS_READY) {
		error = -ENODEV;
		goto unlock;
	}
	req = kmem_intr_zalloc(vreq_size(w.pieces), KM_NOSLEEP);
	if (req == NULL) {
		error = -ENOMEM;
		VQLOG(sc, "%s: no memory for a command\n", vq->pub.name);
		goto unlock;
	}
	req->capacity = w.pieces;
	req->cookie = cookie;
	{
		struct vreq_walk w2;

		memset(&w2, 0, sizeof(w2));
		w2.sc = sc;
		w2.req = req;
		error = vreq_pieces(&w2, sgs, out, in);
		if (error != 0) {
			VQLOG(sc, "%s: the command cannot be mapped: %d\n",
			    vq->pub.name, error);
			goto free_req;
		}
		req->descriptors = req->nmaps;
	}
	error = virtio_enqueue_prep(sc->sc_vsc, vq->nvq, &slot);
	if (error != 0) {
		error = error == EAGAIN ? -ENOSPC : -error;
		goto free_req;
	}
	error = virtio_enqueue_reserve(sc->sc_vsc, vq->nvq, slot,
	    req->descriptors);
	if (error != 0) {
		/* The reserve gives the head back itself. */
		error = error == EAGAIN ? -ENOSPC : -error;
		goto free_req;
	}
	req->cost = vq->nvq->vq_descx[slot].use_indirect ? 1 : req->descriptors;
	KASSERT(vq->requests[slot] == NULL);
	for (j = 0; j < req->nmaps; j++) {
		struct drm618_vmap *m = &req->maps[j];

		bus_dmamap_sync(sc->sc_dmat, m->map, 0, m->map->dm_mapsize,
		    m->readable ? BUS_DMASYNC_PREWRITE : BUS_DMASYNC_PREREAD);
		/* Native "write" is memory to device: what the device reads. */
		virtio_enqueue(sc->sc_vsc, vq->nvq, slot, m->map, m->readable);
	}
	vq->requests[slot] = req;
	KASSERT(req->cost <= vq->pub.num_free);
	vq->pub.num_free -= req->cost;
	/*
	 * Publish now: the driver may wait for room before it kicks.  The
	 * notification follows the event suppression of the native ring.
	 */
	virtio_enqueue_commit(sc->sc_vsc, vq->nvq, slot, true);
	mutex_exit(&sc->sc_lock);
	return 0;

free_req:
	if (req != NULL)
		vreq_free(sc, req);
unlock:
	mutex_exit(&sc->sc_lock);
	return error;
}

void *
virtqueue_get_buf(struct linux_virtqueue *lvq, unsigned int *len)
{
	struct drm618_vq *vq = (struct drm618_vq *)lvq;
	struct drm618_vdev *sc = vq->sc;
	struct drm618_vreq *req;
	void *cookie = NULL;
	int slot, length;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_state != VS_READY ||
	    !virtio_vq_is_enqueued(sc->sc_vsc, vq->nvq) ||
	    virtio_dequeue(sc->sc_vsc, vq->nvq, &slot, &length) != 0)
		goto out;
	req = vq->requests[slot];
	if (req == NULL) {
		VQLOG(sc, "%s: the device returned slot %d that holds no"
		    " command\n", vq->pub.name, slot);
		virtio_dequeue_commit(sc->sc_vsc, vq->nvq, slot);
		goto out;
	}
	cookie = req->cookie;
	if (len != NULL)
		*len = (unsigned int)length;
	vq->pub.num_free += req->cost;
	vq->requests[slot] = NULL;
	/* The memory of the response is the driver's again before it sees the token. */
	vreq_free(sc, req);
	virtio_dequeue_commit(sc->sc_vsc, vq->nvq, slot);
out:
	mutex_exit(&sc->sc_lock);
	return cookie;
}

void
virtqueue_disable_cb(struct linux_virtqueue *lvq)
{
	struct drm618_vq *vq = (struct drm618_vq *)lvq;
	struct drm618_vdev *sc = vq->sc;

	mutex_enter(&sc->sc_lock);
	vq->callbacks = false;
	if (sc->sc_state == VS_READY)
		virtio_stop_vq_intr(sc->sc_vsc, vq->nvq);
	mutex_exit(&sc->sc_lock);
}

bool
virtqueue_enable_cb(struct linux_virtqueue *lvq)
{
	struct drm618_vq *vq = (struct drm618_vq *)lvq;
	struct drm618_vdev *sc = vq->sc;
	bool empty = true;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_state == VS_READY) {
		vq->callbacks = true;
		/* Native: non-zero means used buffers are there already. */
		empty = virtio_start_vq_intr(sc->sc_vsc, vq->nvq) == 0;
	}
	/* After a reset true lets a disable, drain, enable loop end. */
	mutex_exit(&sc->sc_lock);
	return empty;
}

bool
virtqueue_kick_prepare(struct linux_virtqueue *lvq)
{
	struct drm618_vq *vq = (struct drm618_vq *)lvq;
	bool ready;

	mutex_enter(&vq->sc->sc_lock);
	ready = vq->sc->sc_state == VS_READY;
	mutex_exit(&vq->sc->sc_lock);
	return ready;
}

bool
virtqueue_notify(struct linux_virtqueue *lvq)
{
	struct drm618_vq *vq = (struct drm618_vq *)lvq;
	struct drm618_vdev *sc = vq->sc;
	bool ready;

	mutex_enter(&sc->sc_lock);
	ready = sc->sc_state == VS_READY;
	if (ready)
		virtio_enqueue_commit(sc->sc_vsc, vq->nvq, -1, true);
	mutex_exit(&sc->sc_lock);
	return ready;
}

/* A command still queued after the device was reset: its token, the slots given back. */
void *
virtqueue_detach_unused_buf(struct linux_virtqueue *lvq)
{
	struct drm618_vq *vq = (struct drm618_vq *)lvq;
	struct drm618_vdev *sc = vq->sc;
	struct drm618_vreq *req;
	void *cookie = NULL;
	unsigned int slot;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_state != VS_STOPPED) {
		/* A live device may still own it: only after the reset. */
		mutex_exit(&sc->sc_lock);
		return NULL;
	}
	for (slot = 0; slot < vq->size && vq->requests != NULL; slot++) {
		req = vq->requests[slot];
		if (req == NULL)
			continue;
		vq->requests[slot] = NULL;
		cookie = req->cookie;
		vq->pub.num_free += req->cost;
		vreq_free(sc, req);
		virtio_dequeue_commit(sc->sc_vsc, vq->nvq, slot);
		break;
	}
	mutex_exit(&sc->sc_lock);
	return cookie;
}

/* ---- reset and teardown -------------------------------------------------- */

static void
vdev_reset(struct virtio_device *vd)
{
	struct drm618_vdev *sc = to_vdev(vd);
	unsigned int i;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_state != VS_STOPPED) {
		sc->sc_state = VS_STOPPED;
		for (i = 0; i < sc->sc_nvqs; i++)
			sc->sc_vq[i].callbacks = false;
		/* Waits for a running callback: it holds the same lock. */
		virtio_reset(sc->sc_vsc);
	}
	mutex_exit(&sc->sc_lock);
}

void
virtio_reset_device(struct virtio_device *vd)
{

	vdev_reset(vd);
}

static void
vdev_del_vqs(struct virtio_device *vd)
{
	struct drm618_vdev *sc = to_vdev(vd);
	struct drm618_vq *vq;
	struct drm618_vreq *req;
	unsigned int i, slot;
	int error;

	vdev_reset(vd);
	/* Disestablish and drain the interrupts before a ring goes. */
	if (sc->sc_attached) {
		virtio_child_detach(sc->sc_vsc);
		sc->sc_attached = false;
	}
	for (i = 0; i < sc->sc_nvqs; i++) {
		vq = &sc->sc_vq[i];
		if (vq->nvq == NULL)
			continue;
		if (vq->requests != NULL) {
			for (slot = 0; slot < vq->size; slot++) {
				req = vq->requests[slot];
				if (req == NULL)
					continue;
				VQLOG(sc, "%s: a command still queued at the"
				    " end, dropped\n", vq->pub.name);
				vq->requests[slot] = NULL;
				virtio_dequeue_commit(sc->sc_vsc, vq->nvq, slot);
				vreq_free(sc, req);
			}
			kmem_free(vq->requests, vq->size * sizeof(*vq->requests));
			vq->requests = NULL;
		}
		error = virtio_free_vq(sc->sc_vsc, vq->nvq);
		if (error != 0)
			panic("%s: cannot free queue %u: %d",
			    device_xname(sc->sc_dev), i, error);
		vq->nvq = NULL;
		vq->pub.num_free = 0;
	}
	sc->sc_nvqs = 0;
}

/* ---- autoconf ----------------------------------------------------------- */

static int	virtiogpu618_match(device_t, cfdata_t, void *);
static void	virtiogpu618_attach(device_t, device_t, void *);
static int	virtiogpu618_detach(device_t, int);

CFATTACH_DECL_NEW(virtiogpudrm, sizeof(struct drm618_vdev),
    virtiogpu618_match, virtiogpu618_attach, virtiogpu618_detach, NULL);

static int
virtiogpu618_match(device_t parent, cfdata_t cf __unused, void *aux)
{
	struct virtio_attach_args *va = aux;

	if (virtio_child(device_private(parent)) != NULL)
		return 0;
	return vdrv_find(va->sc_childdevid) != NULL ? 2 : 0;
}

static int
vdev_config_change(struct virtio_softc *vsc)
{
	struct drm618_vdev *sc = device_private(virtio_child(vsc));
	int handled = 0;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_state == VS_READY && sc->sc_drv->config_changed != NULL) {
		sc->sc_drv->config_changed(&sc->sc_vdev);
		handled = 1;
	}
	mutex_exit(&sc->sc_lock);
	return handled;
}

static void
virtiogpu618_attach(device_t parent, device_t self, void *aux)
{
	struct drm618_vdev *sc = device_private(self);
	struct virtio_softc *vsc = device_private(parent);
	struct virtio_attach_args *va = aux;
	struct virtio_driver *drv;
	uint64_t req = 0;
	unsigned int i;
	int error;

	aprint_naive("\n");
	drv = vdrv_find(va->sc_childdevid);
	if (drv == NULL) {
		aprint_error(": no driver any more\n");
		return;
	}
	aprint_normal(": %s on virtio\n", drv->driver.name);
	sc->sc_dev = self;
	sc->sc_vsc = vsc;
	sc->sc_drv = drv;
	sc->sc_dmat = virtio_dmat(vsc);
	mutex_init(&sc->sc_lock, MUTEX_SPIN, IPL_VM);
	sc->sc_lock_init = true;
	sc->sc_state = VS_INIT;
	for (i = 0; i < drv->feature_table_size; i++)
		req |= (uint64_t)1 << drv->feature_table[i];
	virtio_child_attach_start(vsc, self, IPL_VM, req,
	    VIRTIO_COMMON_FLAG_BITS);
	sc->sc_vdev.layer = sc;
	sc->sc_vdev.config = &drm618_vconfig;
	sc->sc_vdev.features = virtio_features(vsc);
	sc->sc_vdev.dev.parent = self;
	if (!virtio_version_1(vsc)) {
		aprint_error_dev(self, "requires modern virtio (version 1)\n");
		virtio_child_attach_failed(vsc);
		return;
	}
	error = drv->probe(&sc->sc_vdev);
	if (error != 0) {
		aprint_error_dev(self, "%s: probe failed: %d\n",
		    drv->driver.name, error);
		if (sc->sc_nvqs == 0 && !sc->sc_attached)
			virtio_child_attach_failed(vsc);
		return;
	}
	sc->sc_probed = true;
	vdrv_once();
	mutex_enter(&drm618_vdrv_lock);
	LIST_INSERT_HEAD(&drm618_vdevs, sc, sc_entry);
	sc->sc_listed = true;
	mutex_exit(&drm618_vdrv_lock);
}

static int
virtiogpu618_detach(device_t self, int flags __unused)
{
	struct drm618_vdev *sc = device_private(self);

	if (sc->sc_listed) {
		mutex_enter(&drm618_vdrv_lock);
		LIST_REMOVE(sc, sc_entry);
		sc->sc_listed = false;
		mutex_exit(&drm618_vdrv_lock);
	}
	if (sc->sc_probed && sc->sc_drv->remove != NULL) {
		/* The driver refuses (open files) through the module, before this. */
		sc->sc_drv->remove(&sc->sc_vdev);
		sc->sc_probed = false;
	}
	if (sc->sc_nvqs != 0 || sc->sc_attached)
		vdev_del_vqs(&sc->sc_vdev);
	if (sc->sc_lock_init) {
		mutex_destroy(&sc->sc_lock);
		sc->sc_lock_init = false;
	}
	return 0;
}
