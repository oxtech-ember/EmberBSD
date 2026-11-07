/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <fs/littlefs/littlefs.h>
#include <sys/fcntl.h>
#include <sys/atomic.h>
#include <sys/kernel.h>
#include <sys/kauth.h>
#include <sys/kmem.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <miscfs/genfs/genfs.h>
#include <miscfs/specfs/specdev.h>

MODULE(MODULE_CLASS_VFS, littlefs, NULL);
static int littlefs_sync(struct mount *, int, kauth_cred_t);

int
littlefs_error(int error)
{
	if (error >= 0)
		return 0;
	switch (error) {
	case LFS_ERR_NOENT: return ENOENT;
	case LFS_ERR_EXIST: return EEXIST;
	case LFS_ERR_NOTDIR: return ENOTDIR;
	case LFS_ERR_ISDIR: return EISDIR;
	case LFS_ERR_NOTEMPTY: return ENOTEMPTY;
	case LFS_ERR_BADF: return EBADF;
	case LFS_ERR_FBIG: return EFBIG;
	case LFS_ERR_INVAL: return EINVAL;
	case LFS_ERR_NOSPC: return ENOSPC;
	case LFS_ERR_NOMEM: return ENOMEM;
	case LFS_ERR_NOATTR: return ENOATTR;
	case LFS_ERR_NAMETOOLONG: return ENAMETOOLONG;
	default: return EIO;
	}
}

/* Caller holds lm->lock.  Missing BSD metadata is never treated as root:0777. */
int
littlefs_readattr(struct littlefs_mount *lm, const char *path,
    struct littlefs_bsd_attr *attr, struct lfs_info *info)
{
	uint8_t raw[LITTLEFS_BSD_SIZE];
	int error;

	error = lfs_stat(&lm->lfs, path, info);
	if (error)
		return littlefs_error(error);
	error = lfs_getattr(&lm->lfs, path, LITTLEFS_BSD_ATTR, raw, sizeof(raw));
	if (error != sizeof(raw) || littlefs_bsd_decode(attr, raw, sizeof(raw)))
		return EIO;
	if (IFTOVT(attr->mode) == VNON ||
	    (info->type == LFS_TYPE_DIR) != S_ISDIR(attr->mode))
		return EIO;
	return 0;
}

int
littlefs_writeattr(struct littlefs_node *node)
{
	struct littlefs_mount *lm = node->mount;
	int error;

	KASSERT(mutex_owned(&lm->lock));
	if (littlefs_bsd_encode(node->attrbuf, &node->attr) != 0)
		return EINVAL;
	if (node->entry->removed)
		return 0;
	error = littlefs_error(lfs_setattr(&lm->lfs, node->entry->path,
	    LITTLEFS_BSD_ATTR, node->attrbuf, sizeof(node->attrbuf)));
	node->metadirty = error != 0;
	return error;
}

void
littlefs_update_times(struct littlefs_node *node)
{
	KASSERT(mutex_owned(&node->mount->lock));
	if (node->mount->readonly)
		return;
	unsigned updates = atomic_swap_uint(&node->updates, 0);
	if (updates != 0) {
		struct timespec now;
		vfs_timestamp(&now);
		if (updates & GOP_UPDATE_ACCESSED) {
			node->attr.atime = now.tv_sec;
			node->attr.atimensec = now.tv_nsec;
		}
		if (updates & GOP_UPDATE_MODIFIED) {
			if (node->vp->v_type == VREG)
				node->attr.mode &= ~(S_ISUID | S_ISGID);
			node->attr.mtime = now.tv_sec;
			node->attr.mtimensec = now.tv_nsec;
		}
		if (updates & (GOP_UPDATE_MODIFIED | LITTLEFS_UPDATE_CHANGED)) {
			node->attr.ctime = now.tv_sec;
			node->attr.ctimensec = now.tv_nsec;
		}
		node->metadirty = true;
	}

}

int
littlefs_node_sync(struct littlefs_node *node)
{
	int error;
	KASSERT(mutex_owned(&node->mount->lock));
	if (node->mount->readonly)
		return 0;
	littlefs_update_times(node);
	if (node->metadirty) {
		error = littlefs_writeattr(node);
		if (error)
			return error;
	}
	if (!node->fileopen || !node->writable)
		return 0;
	if (littlefs_bsd_encode(node->attrbuf, &node->attr) != 0)
		return EINVAL;
	return littlefs_error(lfs_file_sync(&node->mount->lfs, &node->file));
}

void
littlefs_touch(struct littlefs_node *node, unsigned updates)
{
	if (node->vp->v_mount->mnt_flag & MNT_RDONLY)
		return;
	if (node->vp->v_mount->mnt_flag & MNT_NOATIME)
		updates &= ~GOP_UPDATE_ACCESSED;
	atomic_or_uint(&node->updates, updates);
}

int
littlefs_writable(struct littlefs_node *node)
{
	struct littlefs_mount *lm = node->mount;
	int error;

	KASSERT(mutex_owned(&lm->lock));
	if (lm->readonly)
		return EROFS;
	if (node->writable)
		return 0;
	if (!node->fileopen || node->entry->removed)
		return EIO;
	error = littlefs_error(lfs_file_close(&lm->lfs, &node->file));
	node->fileopen = false;
	if (error)
		return error;
	error = littlefs_error(lfs_file_opencfg(&lm->lfs, &node->file,
	    node->entry->path, LFS_O_RDWR, &node->filecfg));
	if (!error)
		node->fileopen = node->writable = true;
	return error;
}

void
littlefs_entry_drop(struct littlefs_mount *lm, struct littlefs_entry *entry)
{
	KASSERT(mutex_owned(&lm->lock));
	KASSERT(entry->refs > 0);
	if (--entry->refs != 0)
		return;
	KASSERT(entry->node == NULL);
	LIST_REMOVE(entry, link);
	kmem_free(entry->path, entry->pathlen);
	kmem_free(entry, sizeof(*entry));
}

/* Hold the pathname only while a vnode or an in-progress lookup needs it. */
int
littlefs_entry_hold_path(struct littlefs_mount *lm, const char *path,
    struct littlefs_entry **result)
{
	struct littlefs_entry *entry;
	struct littlefs_bsd_attr attr;
	struct lfs_info info;
	int error;

	KASSERT(mutex_owned(&lm->lock));
	*result = NULL;
	error = littlefs_readattr(lm, path, &attr, &info);
	if (error)
		goto out;
	if (attr.ino >= lm->nextino) {
		error = EIO;
		goto out;
	}
	LIST_FOREACH(entry, &lm->entries, link) {
		if (entry->ino == attr.ino)
			break;
	}
	if (entry != NULL) {
		if (entry->removed || strcmp(entry->path, path) != 0) {
			error = EIO;	/* duplicate inode in a malformed image */
			goto out;
		}
	} else {
		entry = kmem_zalloc(sizeof(*entry), KM_NOSLEEP);
		if (entry == NULL) {
			error = ENOMEM;
			goto out;
		}
		entry->ino = attr.ino;
		entry->pathlen = strlen(path) + 1;
		entry->path = kmem_alloc(entry->pathlen, KM_NOSLEEP);
		if (entry->path == NULL) {
			kmem_free(entry, sizeof(*entry));
			error = ENOMEM;
			goto out;
		}
		memcpy(entry->path, path, entry->pathlen);
		LIST_INSERT_HEAD(&lm->entries, entry, link);
	}
	entry->refs++;
	*result = entry;
out:
	return error;
}

/* Consume the registry reference without holding the core mutex over vcache. */
int
littlefs_entry_vnode(struct littlefs_mount *lm, struct littlefs_entry *entry,
    struct vnode **vpp)
{
	int error;

	*vpp = NULL;
	error = vcache_get(lm->mp, &entry->ino, sizeof(entry->ino), vpp);
	mutex_enter(&lm->lock);
	littlefs_entry_drop(lm, entry);
	mutex_exit(&lm->lock);
	return error;
}

int
littlefs_getpath(struct mount *mp, const char *path, struct vnode **vpp)
{
	struct littlefs_mount *lm = VFSTOLITTLEFS(mp);
	struct littlefs_entry *entry;
	int error;

	*vpp = NULL;
	mutex_enter(&lm->lock);
	error = littlefs_entry_hold_path(lm, path, &entry);
	mutex_exit(&lm->lock);
	return error ? error : littlefs_entry_vnode(lm, entry, vpp);
}

int
littlefs_getchild(struct littlefs_node *dir, const char *name, size_t len,
    struct vnode **vpp)
{
	struct littlefs_mount *lm = dir->mount;
	struct littlefs_entry *entry;
	char *path = PNBUF_GET();
	int error;

	*vpp = NULL;
	mutex_enter(&lm->lock);
	error = littlefs_childpath(dir, name, len, path);
	if (!error)
		error = littlefs_entry_hold_path(lm, path, &entry);
	mutex_exit(&lm->lock);
	PNBUF_PUT(path);
	return error ? error : littlefs_entry_vnode(lm, entry, vpp);
}

int
littlefs_childpath(struct littlefs_node *dir, const char *name, size_t len,
    char *path)
{
	const char *base = dir->entry->path;
	size_t blen = strlen(base);

	KASSERT(mutex_owned(&dir->mount->lock));
	if (dir->entry->removed)
		return ENOENT;
	if (len == 1 && name[0] == '.') {
		strlcpy(path, base, MAXPATHLEN);
		return 0;
	}
	if (len == 2 && name[0] == '.' && name[1] == '.') {
		strlcpy(path, base, MAXPATHLEN);
		char *slash = strrchr(path, '/');
		if (slash == path)
			path[1] = '\0';
		else
			*slash = '\0';
		return 0;
	}
	if (blen == 1 && len == strlen(LITTLEFS_STAGING_NAME) &&
	    memcmp(name, LITTLEFS_STAGING_NAME, len) == 0)
		return EPERM;
	if (len == 0 || len > LFS_NAME_MAX || memchr(name, '/', len) != NULL)
		return EINVAL;
	if (blen + len + 2 > MAXPATHLEN)
		return ENAMETOOLONG;
	memcpy(path, base, blen);
	if (blen != 1)
		path[blen++] = '/';
	memcpy(path + blen, name, len);
	path[blen + len] = '\0';
	return 0;
}

static int
littlefs_loadvnode(struct mount *mp, struct vnode *vp, const void *key,
    size_t keylen, const void **newkey)
{
	struct littlefs_mount *lm = VFSTOLITTLEFS(mp);
	struct littlefs_entry *entry;
	struct littlefs_node *node;
	struct lfs_info info;
	uint64_t ino;
	int error;

	KASSERT(keylen == sizeof(ino));
	memcpy(&ino, key, sizeof(ino));
	node = kmem_zalloc(sizeof(*node), KM_SLEEP);
	mutex_enter(&lm->lock);
	LIST_FOREACH(entry, &lm->entries, link) {
		if (entry->ino == ino)
			break;
	}
	if (entry == NULL || entry->removed) {
		error = ENOENT;
		goto fail;
	}
	error = littlefs_readattr(lm, entry->path, &node->attr, &info);
	if (error)
		goto fail;
	if (node->attr.ino != ino) {
		error = EIO;
		goto fail;
	}
	node->mount = lm;
	node->vp = vp;
	node->entry = entry;
	vp->v_type = IFTOVT(node->attr.mode);
	vp->v_op = littlefs_vnodeop_p;
	if (vp->v_type == VDIR) {
		error = littlefs_error(lfs_dir_open(&lm->lfs, &node->dir,
		    entry->path));
		if (error)
			goto fail;
		node->diropen = true;
		if (ino == LITTLEFS_ROOT_INO)
			vp->v_vflag |= VV_ROOT;
	} else if (vp->v_type == VREG || vp->v_type == VLNK) {
		node->filebuf = kmem_alloc(lm->cfg.cache_size, KM_NOSLEEP);
		if (node->filebuf == NULL) {
			error = ENOMEM;
			goto fail;
		}
		node->fileattr.type = LITTLEFS_BSD_ATTR;
		node->fileattr.buffer = node->attrbuf;
		node->fileattr.size = sizeof(node->attrbuf);
		node->filecfg.buffer = node->filebuf;
		node->filecfg.attrs = &node->fileattr;
		node->filecfg.attr_count = 1;
		error = littlefs_error(lfs_file_opencfg(&lm->lfs, &node->file,
		    entry->path, LFS_O_RDONLY, &node->filecfg));
		if (error)
			goto fail;
		node->fileopen = true;
	} else if (vp->v_type == VBLK || vp->v_type == VCHR) {
		vp->v_op = littlefs_specop_p;
		spec_node_init(vp, node->attr.rdev);
	} else if (vp->v_type == VFIFO) {
		vp->v_op = littlefs_fifoop_p;
	}
	entry->node = node;
	entry->refs++;
	vp->v_tag = VT_LITTLEFS;
	vp->v_data = node;
	genfs_node_init(vp, &littlefs_genfsops);
	uvm_vnp_setsize(vp, info.type == LFS_TYPE_REG ? info.size : 0);
	*newkey = &entry->ino;
	mutex_exit(&lm->lock);
	return 0;
fail:
	mutex_exit(&lm->lock);
	if (node->filebuf != NULL)
		kmem_free(node->filebuf, lm->cfg.cache_size);
	kmem_free(node, sizeof(*node));
	return error;
}

/* Only uncommitted creates live here; rename publishes a fully tagged inode. */
static int
littlefs_clean_staging(struct littlefs_mount *lm)
{
	lfs_dir_t dir;
	struct lfs_info info;
	char path[sizeof(LITTLEFS_STAGING_NAME) + LFS_NAME_MAX + 3];
	int error;

	error = lfs_mkdir(&lm->lfs, "/" LITTLEFS_STAGING_NAME);
	if (error && error != LFS_ERR_EXIST)
		return littlefs_error(error);
	error = lfs_dir_open(&lm->lfs, &dir, "/" LITTLEFS_STAGING_NAME);
	if (error)
		return littlefs_error(error);
	while ((error = lfs_dir_read(&lm->lfs, &dir, &info)) > 0) {
		if (!strcmp(info.name, ".") || !strcmp(info.name, ".."))
			continue;
		snprintf(path, sizeof(path), "/%s/%s", LITTLEFS_STAGING_NAME,
		    info.name);
		error = lfs_remove(&lm->lfs, path);
		if (error)
			break;
		/* Removal changes directory positions; restart, bounded by entries. */
		lfs_dir_rewind(&lm->lfs, &dir);
	}
	lfs_dir_close(&lm->lfs, &dir);
	return littlefs_error(error);
}

static int
littlefs_statvfs(struct mount *mp, struct statvfs *sb)
{
	struct littlefs_mount *lm = VFSTOLITTLEFS(mp);
	lfs_ssize_t used;

	mutex_enter(&lm->lock);
	used = lfs_fs_size(&lm->lfs);
	mutex_exit(&lm->lock);
	if (used < 0)
		return littlefs_error(used);
	sb->f_bsize = sb->f_frsize = lm->cfg.block_size;
	sb->f_iosize = PAGE_SIZE;
	sb->f_blocks = lm->cfg.block_count;
	sb->f_bfree = sb->f_bavail = sb->f_blocks - MIN(used, sb->f_blocks);
	sb->f_bresvd = 0;
	sb->f_files = sb->f_ffree = sb->f_favail = sb->f_fresvd = 0;
	sb->f_namemax = lm->lfs.name_max;
	copy_statvfs_info(sb, mp);
	return 0;
}

/* Caller holds the device vnode locked and owns its sole open reference. */
static int
littlefs_mountfs(struct mount *mp, struct vnode *devvp, uint32_t block_size)
{
	struct littlefs_mount *lm;
	struct littlefs_bsd_attr attr;
	struct lfs_info info;
	uint8_t next[8];
	int error;

	if (devvp->v_type != VBLK)
		return ENOTBLK;
	if (spec_node_getmountedfs(devvp) != NULL)
		return EBUSY;
	lm = kmem_zalloc(sizeof(*lm), KM_SLEEP);
	lm->mp = mp;
	lm->devvp = devvp;
	lm->openflags = FREAD | ((mp->mnt_flag & MNT_RDONLY) ? 0 : FWRITE);
	lm->readonly = (mp->mnt_flag & MNT_RDONLY) != 0;
	mutex_init(&lm->lock, MUTEX_DEFAULT, IPL_NONE);
	LIST_INIT(&lm->entries);
	error = littlefs_device_init(lm, block_size);
	if (error)
		goto fail;
	error = littlefs_error(lfs_mount(&lm->lfs, &lm->cfg));
	if (error)
		goto fail;
	error = littlefs_readattr(lm, "/", &attr, &info);
	if (error || attr.ino != LITTLEFS_ROOT_INO || !S_ISDIR(attr.mode)) {
		error = EIO;
		goto unmount;
	}
	error = lfs_getattr(&lm->lfs, "/", LITTLEFS_NEXTINO_ATTR,
	    next, sizeof(next));
	if (error != sizeof(next) ||
	    (lm->nextino = littlefs_le64dec(next)) <= LITTLEFS_ROOT_INO) {
		error = EIO;
		goto unmount;
	}
	if (!(mp->mnt_flag & MNT_RDONLY)) {
		error = littlefs_clean_staging(lm);
		if (error)
			goto unmount;
	}
	mp->mnt_data = lm;
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_iflag |= IMNT_MPSAFE;
	mp->mnt_fs_bshift = PAGE_SHIFT;
	mp->mnt_dev_bshift = DEV_BSHIFT;
	vfs_getnewfsid(mp);
	spec_node_setmountedfs(devvp, mp);
	return 0;
unmount:
	lfs_unmount(&lm->lfs);
fail:
	littlefs_device_fini(lm);
	mutex_destroy(&lm->lock);
	kmem_free(lm, sizeof(*lm));
	return error;
}

static int
littlefs_remount(struct mount *mp)
{
	struct littlefs_mount *lm = VFSTOLITTLEFS(mp);
	int error;
	bool readonly = lm->readonly;

	if (mp->mnt_flag & MNT_RELOAD)
		return EOPNOTSUPP;
	if (mp->mnt_iflag & IMNT_WANTRDWR)
		readonly = false;
	if (mp->mnt_iflag & IMNT_WANTRDONLY)
		readonly = true;
	if (readonly == lm->readonly)
		return 0;
	if (!readonly) {
		if (lm->writeprotected)
			return EROFS;
		error = kauth_authorize_system(curlwp->l_cred, KAUTH_SYSTEM_MOUNT,
		    KAUTH_REQ_SYSTEM_MOUNT_DEVICE, mp, lm->devvp,
		    KAUTH_ARG(VREAD | VWRITE));
		if (error)
			return error;
	} else {
		error = littlefs_sync(mp, MNT_WAIT, curlwp->l_cred);
		if (!error)
			error = vflush(mp, NULL, WRITECLOSE);
		if (error)
			return error;
	}
	/* Like FFS, retain the existing exclusive block-device open on update. */
	error = 0;
	mutex_enter(&lm->lock);
	lm->readonly = readonly;
	if (!readonly) {
		error = littlefs_clean_staging(lm);
		if (error)
			lm->readonly = true;
	}
	if (!error)
		lm->openflags = FREAD | (readonly ? 0 : FWRITE);
	mutex_exit(&lm->lock);
	return error;
}

static int
littlefs_mount(struct mount *mp, const char *path, void *data, size_t *len)
{
	struct littlefs_args *args = data;
	struct pathbuf *pb;
	struct nameidata nd;
	struct vnode *devvp;
	int error, openflags;

	if (args == NULL || *len < sizeof(*args))
		return EINVAL;
	if (mp->mnt_flag & MNT_GETARGS) {
		args->fspec = NULL;
		args->version = LITTLEFS_ARGSVERSION;
		args->block_size = VFSTOLITTLEFS(mp)->cfg.block_size;
		*len = sizeof(*args);
		return 0;
	}
	if (args->version != LITTLEFS_ARGSVERSION)
		return EINVAL;
	if (mp->mnt_flag & MNT_UPDATE)
		return littlefs_remount(mp);
	error = pathbuf_copyin(args->fspec, &pb);
	if (error)
		return error;
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, pb);
	error = namei(&nd);
	pathbuf_destroy(pb);
	if (error)
		return error;
	devvp = nd.ni_vp;
	error = kauth_authorize_system(curlwp->l_cred, KAUTH_SYSTEM_MOUNT,
	    KAUTH_REQ_SYSTEM_MOUNT_DEVICE, mp, devvp,
	    KAUTH_ARG(VREAD | ((mp->mnt_flag & MNT_RDONLY) ? 0 : VWRITE)));
	if (error)
		goto fail;
	error = set_statvfs_info(path, UIO_USERSPACE, args->fspec,
	    UIO_USERSPACE, mp->mnt_op->vfs_name, mp, curlwp);
	if (error)
		goto fail;
	openflags = FREAD | ((mp->mnt_flag & MNT_RDONLY) ? 0 : FWRITE);
	error = VOP_OPEN(devvp, openflags, FSCRED);
	if (error)
		goto fail;
	error = littlefs_mountfs(mp, devvp, args->block_size);
	if (error) {
		(void)VOP_CLOSE(devvp, openflags, NOCRED);
		goto fail;
	}
	VOP_UNLOCK(devvp);
	return 0;
fail:
	vput(devvp);
	return error;
}

static int
littlefs_sync(struct mount *mp, int waitfor, kauth_cred_t cred)
{
	struct vnode_iterator *iter;
	struct vnode *vp;
	struct littlefs_mount *lm = VFSTOLITTLEFS(mp);
	int error, result = 0;

	if (lm->readonly)
		return 0;
	vfs_vnode_iterator_init(mp, &iter);
	while ((vp = vfs_vnode_iterator_next(iter, NULL, NULL)) != NULL) {
		error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		if (!error) {
			error = VOP_FSYNC(vp, cred, FSYNC_WAIT, 0, 0);
			VOP_UNLOCK(vp);
		}
		vrele(vp);
		if (error && !result)
			result = error;
	}
	vfs_vnode_iterator_destroy(iter);
	mutex_enter(&lm->lock);
	error = littlefs_error(littlefs_sync_device(&lm->cfg));
	mutex_exit(&lm->lock);
	return result ? result : error;
}

static int
littlefs_unmount(struct mount *mp, int flags)
{
	struct littlefs_mount *lm = VFSTOLITTLEFS(mp);
	int error, close_error;

	error = littlefs_sync(mp, MNT_WAIT, curlwp->l_cred);
	if (error)
		return error;
	error = vflush(mp, NULL, (flags & MNT_FORCE) ? FORCECLOSE : 0);
	if (error)
		return error;
	cache_purgevfs(mp);
	KASSERT(LIST_EMPTY(&lm->entries));
	error = littlefs_error(lfs_unmount(&lm->lfs));
	vn_lock(lm->devvp, LK_EXCLUSIVE | LK_RETRY);
	spec_node_setmountedfs(lm->devvp, NULL);
	close_error = VOP_CLOSE(lm->devvp, lm->openflags, NOCRED);
	vput(lm->devvp);
	littlefs_device_fini(lm);
	mutex_destroy(&lm->lock);
	kmem_free(lm, sizeof(*lm));
	mp->mnt_data = NULL;
	return error ? error : close_error;
}

static int
littlefs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	int error;

	error = littlefs_getpath(mp, "/", vpp);
	if (!error) {
		error = vn_lock(*vpp, flags);
		if (error) {
			vrele(*vpp);
			*vpp = NULL;
		}
	}
	return error;
}

static int
littlefs_mountroot(void)
{
	struct mount *mp;
	int error;

	if (rootvp == NULL || rootvp->v_type != VBLK)
		return ENODEV;
	error = vfs_rootmountalloc(MOUNT_LITTLEFS, "root_device", &mp);
	if (error)
		return error;
	vn_lock(rootvp, LK_EXCLUSIVE | LK_RETRY);
	error = littlefs_mountfs(mp, rootvp, 0);
	VOP_UNLOCK(rootvp);
	if (error) {
		vfs_unbusy(mp);
		vfs_rele(mp);
		return error;
	}
	mountlist_append(mp);
	littlefs_statvfs(mp, &mp->mnt_stat);
	vfs_unbusy(mp);
	return 0;
}

extern const struct vnodeopv_desc littlefs_vnodeop_opv_desc;
extern const struct vnodeopv_desc littlefs_specop_opv_desc;
extern const struct vnodeopv_desc littlefs_fifoop_opv_desc;
static const struct vnodeopv_desc * const littlefs_vnodeopv_descs[] = {
	&littlefs_vnodeop_opv_desc, &littlefs_specop_opv_desc,
	&littlefs_fifoop_opv_desc, NULL
};

struct vfsops littlefs_vfsops = {
	.vfs_name = MOUNT_LITTLEFS,
	.vfs_min_mount_data = sizeof(struct littlefs_args),
	.vfs_mount = littlefs_mount,
	.vfs_start = (void *)nullop,
	.vfs_unmount = littlefs_unmount,
	.vfs_root = littlefs_root,
	.vfs_quotactl = (void *)eopnotsupp,
	.vfs_statvfs = littlefs_statvfs,
	.vfs_sync = littlefs_sync,
	.vfs_vget = (void *)eopnotsupp,
	.vfs_loadvnode = littlefs_loadvnode,
	.vfs_newvnode = (void *)eopnotsupp,
	.vfs_fhtovp = (void *)eopnotsupp,
	.vfs_vptofh = (void *)eopnotsupp,
	.vfs_init = (void *)nullop,
	.vfs_reinit = (void *)nullop,
	.vfs_done = (void *)nullop,
	.vfs_mountroot = littlefs_mountroot,
	.vfs_snapshot = (void *)eopnotsupp,
	.vfs_extattrctl = vfs_stdextattrctl,
	.vfs_suspendctl = genfs_suspendctl,
	.vfs_renamelock_enter = genfs_renamelock_enter,
	.vfs_renamelock_exit = genfs_renamelock_exit,
	.vfs_fsync = (void *)eopnotsupp,
	.vfs_opv_descs = littlefs_vnodeopv_descs,
};

static int
littlefs_modcmd(modcmd_t cmd, void *arg)
{
	switch (cmd) {
	case MODULE_CMD_INIT: return vfs_attach(&littlefs_vfsops);
	case MODULE_CMD_FINI: return vfs_detach(&littlefs_vfsops);
	default: return ENOTTY;
	}
}
