/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include "opt_littlefs.h"
#include <fs/littlefs/littlefs.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/fcntl.h>
#include <sys/kmem.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <miscfs/specfs/specdev.h>
#ifdef LITTLEFS_FLASH
#include <dev/flash/flash.h>
extern const struct bdevsw flash_bdevsw;
#endif

static bool
littlefs_bounds(const struct lfs_config *c, lfs_block_t block,
    lfs_off_t off, lfs_size_t len)
{
	return block < c->block_count && off <= c->block_size &&
	    len <= c->block_size - off;
}

/* Buffer-cache backend for md/vnd and ordinary block devices. */
static int
littlefs_block_io(struct littlefs_mount *lm, uint64_t off, void *data,
    size_t len, bool writing)
{
	struct buf *bp;
	size_t n, skip;
	int error;

	while (len != 0) {
		skip = off % DEV_BSIZE;
		n = MIN(len, DEV_BSIZE - skip);
		error = bread(lm->devvp, off / DEV_BSIZE, DEV_BSIZE, 0, &bp);
		if (error)
			return error;
		if (writing) {
			memcpy((uint8_t *)bp->b_data + skip, data, n);
			error = bwrite(bp);
			if (error)
				return error;
		} else {
			memcpy(data, (uint8_t *)bp->b_data + skip, n);
			brelse(bp, 0);
		}
		off += n;
		data = (uint8_t *)data + n;
		len -= n;
	}
	return 0;
}

static int
littlefs_device_read(const struct lfs_config *c, lfs_block_t block,
    lfs_off_t off, void *data, lfs_size_t len)
{
	struct littlefs_mount *lm = c->context;
	uint64_t address = (uint64_t)block * c->block_size + off;
	int error;

	if (!littlefs_bounds(c, block, off, len))
		return LFS_ERR_INVAL;
#ifdef LITTLEFS_FLASH
	if (lm->flashdev != NULL) {
		size_t done = 0;
		error = flash_read(lm->flashdev, address, len, &done, data);
		return error || done != len ? LFS_ERR_IO : 0;
	}
#endif
	error = littlefs_block_io(lm, address, data, len, false);
	return error ? LFS_ERR_IO : 0;
}

static int
littlefs_device_prog(const struct lfs_config *c, lfs_block_t block,
    lfs_off_t off, const void *data, lfs_size_t len)
{
	struct littlefs_mount *lm = c->context;
	uint64_t address = (uint64_t)block * c->block_size + off;
	int error;

	if (lm->readonly)
		return LFS_ERR_IO;
	if (!littlefs_bounds(c, block, off, len))
		return LFS_ERR_INVAL;
#ifdef LITTLEFS_FLASH
	if (lm->flashdev != NULL) {
		size_t done = 0;
		error = flash_write(lm->flashdev, address, len, &done, data);
		return error || done != len ? LFS_ERR_IO : 0;
	}
#endif
	error = littlefs_block_io(lm, address, __UNCONST(data), len, true);
	return error ? LFS_ERR_IO : 0;
}

static int
littlefs_device_erase(const struct lfs_config *c, lfs_block_t block)
{
	struct littlefs_mount *lm = c->context;
	uint64_t address = (uint64_t)block * c->block_size;
	uint8_t erased[DEV_BSIZE];
	int error;

	if (lm->readonly)
		return LFS_ERR_IO;
	if (!littlefs_bounds(c, block, 0, c->block_size))
		return LFS_ERR_INVAL;
#ifdef LITTLEFS_FLASH
	if (lm->flashdev != NULL) {
		struct flash_erase_instruction ei = {
			.ei_addr = address,
			.ei_len = c->block_size,
			.ei_state = FLASH_ERASE_FAILED,
		};
		error = flash_erase(lm->flashdev, &ei);
		if (!error)
			error = flash_sync(lm->flashdev);
		return error || ei.ei_state != FLASH_ERASE_DONE ? LFS_ERR_IO : 0;
	}
#endif
	memset(erased, 0xff, sizeof(erased));
	for (lfs_size_t off = 0; off < c->block_size; off += sizeof(erased)) {
		error = littlefs_block_io(lm, address + off, erased,
		    sizeof(erased), true);
		if (error)
			return LFS_ERR_IO;
	}
	return 0;
}

int
littlefs_sync_device(const struct lfs_config *c)
{
	struct littlefs_mount *lm = c->context;
	int error, force = 0;

	if (lm->readonly)
		return 0;
#ifdef LITTLEFS_FLASH
	if (lm->flashdev != NULL)
		return flash_sync(lm->flashdev) ? LFS_ERR_IO : 0;
#endif
	/* Every buffer write above completes synchronously; flush device caches. */
	error = bdev_ioctl(lm->devvp->v_rdev, DIOCCACHESYNC, &force, FWRITE, curlwp);
	if (error == ENOTTY || error == EOPNOTSUPP)
		error = 0;
	return error ? LFS_ERR_IO : 0;
}

int
littlefs_device_init(struct littlefs_mount *lm, uint32_t block_size)
{
	struct lfs_config *c = &lm->cfg;
	struct partinfo pi;
	int sectors, error;

	c->read_size = c->prog_size = 16;
#ifdef LITTLEFS_FLASH
	if (bdevsw_lookup(lm->devvp->v_rdev) == &flash_bdevsw) {
		const struct flash_interface *fi;
		const struct flash_softc *sc;
		fi = flash_get_interface(lm->devvp->v_rdev);
		sc = flash_get_softc(lm->devvp->v_rdev);
		if (fi == NULL || sc == NULL || fi->type != FLASH_TYPE_NOR)
			return ENODEV;
		if (sc->sc_readonly && !(lm->mp->mnt_flag & MNT_RDONLY))
			return EROFS;
		lm->writeprotected = sc->sc_readonly;
		if (block_size != 0 && block_size != fi->erasesize)
			return EINVAL;
		block_size = fi->erasesize;
		c->prog_size = MAX(fi->writesize, 1);
		c->read_size = 1;
		lm->flashdev = flash_get_device(lm->devvp->v_rdev);
		lm->mediasize = flash_get_size(lm->devvp->v_rdev);
	} else
#endif
	{
		error = VOP_IOCTL(lm->devvp, DIOCGPARTINFO, &pi, FREAD, NOCRED);
		if (!error) {
			lm->mediasize = (uint64_t)pi.pi_size * pi.pi_secsize;
		} else {
			sectors = bdev_size(lm->devvp->v_rdev);
			if (sectors <= 0)
				return ENXIO;
			lm->mediasize = (uint64_t)sectors * DEV_BSIZE;
		}
		if (block_size == 0)
			block_size = LITTLEFS_DEFAULT_BLOCK_SIZE;
	}
	if (block_size < DEV_BSIZE || !powerof2(block_size) ||
	    !powerof2(c->prog_size) || c->prog_size > block_size ||
	    lm->mediasize / block_size < 2 ||
	    lm->mediasize / block_size > UINT32_MAX ||
	    lm->mediasize % block_size != 0)
		return EINVAL;
	c->context = lm;
	c->read = littlefs_device_read;
	c->prog = littlefs_device_prog;
	c->erase = littlefs_device_erase;
	c->sync = littlefs_sync_device;
	c->block_size = block_size;
	c->block_count = lm->mediasize / block_size;
	c->block_cycles = 500;
	c->cache_size = MAX(256, c->prog_size);
	c->lookahead_size = 32;
	lm->readbuf = kmem_alloc(c->cache_size, KM_SLEEP);
	lm->progbuf = kmem_alloc(c->cache_size, KM_SLEEP);
	lm->lookbuf = kmem_alloc(c->lookahead_size, KM_SLEEP);
	c->read_buffer = lm->readbuf;
	c->prog_buffer = lm->progbuf;
	c->lookahead_buffer = lm->lookbuf;
	return 0;
}

void
littlefs_device_fini(struct littlefs_mount *lm)
{
	if (lm->readbuf != NULL)
		kmem_free(lm->readbuf, lm->cfg.cache_size);
	if (lm->progbuf != NULL)
		kmem_free(lm->progbuf, lm->cfg.cache_size);
	if (lm->lookbuf != NULL)
		kmem_free(lm->lookbuf, lm->cfg.lookahead_size);
}
