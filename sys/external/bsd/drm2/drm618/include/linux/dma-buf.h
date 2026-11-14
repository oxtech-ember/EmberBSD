/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Shadow of <linux/dma-buf.h> for the 6.18 core; own
 * declarations written to the way drm_prime.c, drm_gem.c and drm_gem.h of
 * dist618 (MIT) use the dma-buf, and to the dma-buf of the shared layer
 * (include/linux/dma-buf.h, NetBSD Foundation, BSD-2-Clause) that serves the
 * 5.6 core.  Not the layer's header, because the two differ where it matters:
 *
 *   - here a dma-buf is a Linux file (struct dma_buf.file, drm618_file.h):
 *     fd_install(fd, dmabuf->file) gives it a descriptor, dma_buf_get(fd) finds
 *     it again, the references of the file are the references of the dma-buf,
 *     and the release of the file runs ops->release;
 *   - ops->mmap takes a vm_area_struct (the mmap of the file runs it), vmap
 *     and vunmap take an iosys_map, attach takes a struct device.
 *
 * The functions have names of their own (drm618_dma_buf_*): the shared layer
 * that this module sits on defines linux_dma_buf_* and a kernel cannot have
 * two definitions of one name.  Implemented in drm618_dma_buf.c.
 */

#ifndef _DRM618_LINUX_DMA_BUF_H_
#define _DRM618_LINUX_DMA_BUF_H_

#include <sys/types.h>

#include <drm618_file.h>
#include <linux/dma-mapping.h>
#include <linux/dma-resv.h>
#include <linux/err.h>
#include <linux/iosys-map.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>

struct device;
struct dma_buf;
struct dma_buf_attachment;
struct module;
struct vm_area_struct;

struct dma_buf_ops {
	/* The exporter's hooks; each may be NULL except map_dma_buf, unmap_dma_buf and release. */
	bool	cache_sgt_mapping;
	int	(*attach)(struct dma_buf *, struct dma_buf_attachment *);
	void	(*detach)(struct dma_buf *, struct dma_buf_attachment *);
	int	(*pin)(struct dma_buf_attachment *);
	void	(*unpin)(struct dma_buf_attachment *);
	struct sg_table *(*map_dma_buf)(struct dma_buf_attachment *,
		    enum dma_data_direction);
	void	(*unmap_dma_buf)(struct dma_buf_attachment *,
		    struct sg_table *, enum dma_data_direction);
	void	(*release)(struct dma_buf *);
	int	(*begin_cpu_access)(struct dma_buf *, enum dma_data_direction);
	int	(*end_cpu_access)(struct dma_buf *, enum dma_data_direction);
	int	(*mmap)(struct dma_buf *, struct vm_area_struct *);
	int	(*vmap)(struct dma_buf *, struct iosys_map *);
	void	(*vunmap)(struct dma_buf *, struct iosys_map *);
};

struct dma_buf {
	size_t				size;
	struct DRM618_FILE_TAG		*file;
	struct list_head		attachments;
	const struct dma_buf_ops	*ops;
	struct mutex			lock;
	const char			*exp_name;
	struct module			*owner;
	struct dma_resv			*resv;
	void				*priv;
	/* the layer's: what poll and kqueue wait on, and the reservation when the exporter gave none */
	struct dma_resv_poll		db_resv_poll;
	struct dma_resv			db_resv_int;
};

struct dma_buf_attach_ops;

struct dma_buf_attachment {
	struct dma_buf			*dmabuf;
	struct device			*dev;
	struct list_head		node;
	bool				peer2peer;
	const struct dma_buf_attach_ops	*importer_ops;
	void				*importer_priv;
	void				*priv;
};

struct dma_buf_export_info {
	const char			*exp_name;
	struct module			*owner;
	const struct dma_buf_ops	*ops;
	size_t				size;
	int				flags;
	struct dma_resv			*resv;
	void				*priv;
};

#define	DEFINE_DMA_BUF_EXPORT_INFO(name)				\
	struct dma_buf_export_info name = { .exp_name = KBUILD_MODNAME,	\
					    .owner = THIS_MODULE }

#define	dma_buf_export			drm618_dma_buf_export
#define	dma_buf_get			drm618_dma_buf_get
#define	dma_buf_put			drm618_dma_buf_put
#define	get_dma_buf			drm618_get_dma_buf
#define	dma_buf_fd			drm618_dma_buf_fd
#define	dma_buf_attach			drm618_dma_buf_attach
#define	dma_buf_detach			drm618_dma_buf_detach
#define	dma_buf_map_attachment		drm618_dma_buf_map_attachment
#define	dma_buf_unmap_attachment	drm618_dma_buf_unmap_attachment
#define	dma_buf_map_attachment_unlocked	drm618_dma_buf_map_attachment_unlocked
#define	dma_buf_unmap_attachment_unlocked drm618_dma_buf_unmap_attachment_unlocked

int	drm618_dma_buf_busy(void);	/* EBUSY while a dma-buf exists */

/* The dma-buf, or ERR_PTR(-EINVAL) for a bad argument; the file (and its descriptor) holds one reference. */
struct dma_buf *dma_buf_export(const struct dma_buf_export_info *);
/* A new reference of the dma-buf of a descriptor: ERR_PTR(-EBADF) if the descriptor is not open, ERR_PTR(-EINVAL) if it is no dma-buf. */
struct dma_buf *dma_buf_get(int);
void	dma_buf_put(struct dma_buf *);
void	get_dma_buf(struct dma_buf *);
int	dma_buf_fd(struct dma_buf *, int);

struct dma_buf_attachment *dma_buf_attach(struct dma_buf *, struct device *);
void	dma_buf_detach(struct dma_buf *, struct dma_buf_attachment *);
struct sg_table *dma_buf_map_attachment(struct dma_buf_attachment *,
	    enum dma_data_direction);
void	dma_buf_unmap_attachment(struct dma_buf_attachment *, struct sg_table *,
	    enum dma_data_direction);
struct sg_table *dma_buf_map_attachment_unlocked(struct dma_buf_attachment *,
	    enum dma_data_direction);
void	dma_buf_unmap_attachment_unlocked(struct dma_buf_attachment *,
	    struct sg_table *, enum dma_data_direction);

#endif	/* _DRM618_LINUX_DMA_BUF_H_ */
