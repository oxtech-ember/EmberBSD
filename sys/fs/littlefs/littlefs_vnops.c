/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <fs/littlefs/littlefs.h>
#include <sys/atomic.h>
#include <sys/buf.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/kauth.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/malloc.h>
#include <sys/lockf.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <uvm/uvm_extern.h>
#include <miscfs/genfs/genfs.h>
#include <miscfs/specfs/specdev.h>
#include <miscfs/fifofs/fifo.h>

static int littlefs_fsync(void *);
int littlefs_rename(void *);

static int
littlefs_lookup(void *v)
{
	struct vop_lookup_v2_args *ap = v;
	struct componentname *cnp = ap->a_cnp;
	struct littlefs_node *dir = VTOLITTLEFS(ap->a_dvp);
	int error;

	*ap->a_vpp = NULL;
	error = VOP_ACCESS(ap->a_dvp, VEXEC, cnp->cn_cred);
	if (error)
		return error;
	error = littlefs_getchild(dir, cnp->cn_nameptr, cnp->cn_namelen, ap->a_vpp);
	if (error == ENOENT && (cnp->cn_flags & ISLASTCN) &&
	    (cnp->cn_nameiop == CREATE || cnp->cn_nameiop == RENAME)) {
		error = VOP_ACCESS(ap->a_dvp, VWRITE, cnp->cn_cred);
		return error ? error : EJUSTRETURN;
	}
	if (!error)
		cache_enter(ap->a_dvp, *ap->a_vpp, cnp->cn_nameptr,
		    cnp->cn_namelen, cnp->cn_flags);
	return error;
}

static int
littlefs_access(void *v)
{
	struct vop_access_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct littlefs_node *node = VTOLITTLEFS(vp);
	struct littlefs_bsd_attr *a = &node->attr;

	if ((ap->a_accmode & VWRITE) &&
	    (vp->v_type == VREG || vp->v_type == VDIR || vp->v_type == VLNK)) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return EROFS;
		if (a->flags & IMMUTABLE)
			return EPERM;
	}
	return kauth_authorize_vnode(ap->a_cred,
	    KAUTH_ACCESS_ACTION(ap->a_accmode, vp->v_type, a->mode), vp, NULL,
	    genfs_can_access(vp, ap->a_cred, a->uid, a->gid, a->mode,
	    NULL, ap->a_accmode));
}

static int
littlefs_open(void *v)
{
	struct vop_open_args *ap = v;
	struct littlefs_node *node = VTOLITTLEFS(ap->a_vp);
	int error = 0;

	if ((ap->a_mode & FWRITE) && ap->a_vp->v_type == VREG) {
		if ((node->attr.flags & APPEND) && !(ap->a_mode & O_APPEND))
			return EPERM;
		mutex_enter(&node->mount->lock);
		error = littlefs_writable(node);
		mutex_exit(&node->mount->lock);
	}
	return error;
}

static int
littlefs_getattr(void *v)
{
	struct vop_getattr_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct littlefs_node *node = VTOLITTLEFS(vp);
	struct littlefs_bsd_attr attr, *a = &attr;
	struct vattr *va = ap->a_vap;
	nlink_t links;
	int result = 0;

	mutex_enter(&node->mount->lock);
	littlefs_update_times(node);
	attr = node->attr;
	links = node->entry->removed ? 0 : 1;
	if (vp->v_type == VDIR && links != 0) {
		struct lfs_info info;
		lfs_soff_t saved = lfs_dir_tell(&node->mount->lfs, &node->dir);
		links = 2;
		result = lfs_dir_rewind(&node->mount->lfs, &node->dir);
		if (!result) {
			while ((result = lfs_dir_read(&node->mount->lfs, &node->dir,
			    &info)) > 0) {
				if (info.type == LFS_TYPE_DIR && strcmp(info.name, ".") &&
				    strcmp(info.name, "..") &&
				    !(attr.ino == LITTLEFS_ROOT_INO &&
				    !strcmp(info.name, LITTLEFS_STAGING_NAME)))
					links++;
			}
		}
		if (saved >= 0)
			(void)lfs_dir_seek(&node->mount->lfs, &node->dir, saved);
	}
	mutex_exit(&node->mount->lock);
	if (result < 0)
		return littlefs_error(result);

	vattr_null(va);
	va->va_type = vp->v_type;
	va->va_mode = a->mode & ALLPERMS;
	va->va_nlink = links;
	va->va_uid = a->uid;
	va->va_gid = a->gid;
	va->va_fsid = vp->v_mount->mnt_stat.f_fsid;
	va->va_fileid = a->ino;
	va->va_size = vp->v_size;
	va->va_blocksize = node->mount->cfg.block_size;
	va->va_atime.tv_sec = a->atime;
	va->va_atime.tv_nsec = a->atimensec;
	va->va_mtime.tv_sec = a->mtime;
	va->va_mtime.tv_nsec = a->mtimensec;
	va->va_ctime.tv_sec = a->ctime;
	va->va_ctime.tv_nsec = a->ctimensec;
	va->va_gen = 0;
	va->va_flags = a->flags;
	va->va_rdev = a->rdev;
	va->va_bytes = vp->v_size;
	return 0;
}

static int
littlefs_resize(struct vnode *vp, off_t size)
{
	struct littlefs_node *node = VTOLITTLEFS(vp);
	struct littlefs_mount *lm = node->mount;
	int error;

	if (size < 0 || (uint64_t)size > LFS_FILE_MAX)
		return EFBIG;
	if (vp->v_type != VREG)
		return vp->v_type == VDIR ? EISDIR : EINVAL;
	if (size < vp->v_size && (size & PAGE_MASK) != 0)
		ubc_zerorange(&vp->v_uobj, size, PAGE_SIZE - (size & PAGE_MASK),
		    UBC_VNODE_FLAGS(vp));
	genfs_node_wrlock(vp);
	mutex_enter(&lm->lock);
	error = littlefs_writable(node);
	if (!error)
		error = littlefs_error(lfs_file_truncate(&lm->lfs, &node->file, size));
	mutex_exit(&lm->lock);
	if (!error) {
		uvm_vnp_setsize(vp, size);
		littlefs_touch(node, GOP_UPDATE_MODIFIED);
	}
	genfs_node_unlock(vp);
	return error;
}

static int
littlefs_setattr(void *v)
{
	struct vop_setattr_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct littlefs_node *node = VTOLITTLEFS(vp);
	struct littlefs_bsd_attr attr;
	struct vattr *va = ap->a_vap;
	kauth_cred_t cred = ap->a_cred;
	struct timespec now;
	int error;

	if (va->va_type != VNON || va->va_nlink != VNOVAL ||
	    va->va_fsid != VNOVAL || va->va_fileid != VNOVAL ||
	    va->va_blocksize != VNOVAL || va->va_rdev != VNOVAL ||
	    va->va_bytes != VNOVAL || va->va_gen != VNOVAL)
		return EINVAL;
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;
	/* Commit earlier data changes before explicitly restoring privilege bits. */
	if (vp->v_type == VREG && (va->va_mode != VNOVAL ||
	    va->va_uid != VNOVAL || va->va_gid != VNOVAL)) {
		error = VOP_FSYNC(vp, cred, FSYNC_WAIT, 0, 0);
		if (error)
			return error;
	}
	mutex_enter(&node->mount->lock);
	littlefs_update_times(node);
	attr = node->attr;
	mutex_exit(&node->mount->lock);
	if (va->va_flags != VNOVAL) {
		if (va->va_flags & ~(UF_SETTABLE | SF_SETTABLE))
			return EOPNOTSUPP;
		bool sysflags = ((va->va_flags ^ attr.flags) & SF_SETTABLE) != 0;
		kauth_action_t action = KAUTH_VNODE_WRITE_FLAGS;
		if (sysflags)
			action |= KAUTH_VNODE_WRITE_SYSFLAGS;
		if (attr.flags & (SF_IMMUTABLE | SF_APPEND))
			action |= KAUTH_VNODE_HAS_SYSFLAGS;
		error = kauth_authorize_vnode(cred, action, vp, NULL,
		    genfs_can_chflags(vp, cred, attr.uid, sysflags));
		if (error)
			return error;
		attr.flags = va->va_flags;
	}
	if ((attr.flags & (IMMUTABLE | APPEND)) &&
	    (va->va_mode != VNOVAL || va->va_uid != VNOVAL ||
	    va->va_gid != VNOVAL || va->va_size != VNOVAL ||
	    va->va_atime.tv_sec != VNOVAL || va->va_mtime.tv_sec != VNOVAL))
		return EPERM;
	if (va->va_uid != VNOVAL || va->va_gid != VNOVAL) {
		uid_t uid = va->va_uid == VNOVAL ? attr.uid : va->va_uid;
		gid_t gid = va->va_gid == VNOVAL ? attr.gid : va->va_gid;
		error = kauth_authorize_vnode(cred, KAUTH_VNODE_CHANGE_OWNERSHIP,
		    vp, NULL, genfs_can_chown(vp, cred, attr.uid, attr.gid, uid, gid));
		if (error)
			return error;
		attr.uid = uid;
		attr.gid = gid;
		attr.mode &= ~(S_ISUID | S_ISGID);
	}
	if (va->va_mode != VNOVAL) {
		error = kauth_authorize_vnode(cred, KAUTH_VNODE_WRITE_SECURITY,
		    vp, NULL, genfs_can_chmod(vp, cred, attr.uid, attr.gid, va->va_mode));
		if (error)
			return error;
		attr.mode = (attr.mode & S_IFMT) | (va->va_mode & ALLPERMS);
	}
	if (va->va_atime.tv_sec != VNOVAL || va->va_mtime.tv_sec != VNOVAL) {
		error = kauth_authorize_vnode(cred, KAUTH_VNODE_WRITE_TIMES,
		    vp, NULL, genfs_can_chtimes(vp, cred, attr.uid, va->va_vaflags));
		if (error)
			return error;
		if (va->va_atime.tv_sec != VNOVAL) {
			attr.atime = va->va_atime.tv_sec;
			attr.atimensec = va->va_atime.tv_nsec;
		}
		if (va->va_mtime.tv_sec != VNOVAL) {
			attr.mtime = va->va_mtime.tv_sec;
			attr.mtimensec = va->va_mtime.tv_nsec;
		}
	}
	if (va->va_size != VNOVAL) {
		error = VOP_ACCESS(vp, VWRITE, cred);
		if (error)
			return error;
		error = littlefs_resize(vp, va->va_size);
		if (error)
			return error;
	}
	vfs_timestamp(&now);
	attr.ctime = now.tv_sec;
	attr.ctimensec = now.tv_nsec;
	mutex_enter(&node->mount->lock);
	node->attr = attr;
	error = littlefs_writeattr(node);
	mutex_exit(&node->mount->lock);
	cache_enter_id(vp, attr.mode, attr.uid, attr.gid, true);
	return error;
}

static int
littlefs_read(void *v)
{
	struct vop_read_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	size_t len;
	int error;

	if (vp->v_type != VREG)
		return vp->v_type == VDIR ? EISDIR : EINVAL;
	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_offset >= vp->v_size)
		return 0;
	len = MIN(uio->uio_resid, vp->v_size - uio->uio_offset);
	error = ubc_uiomove(&vp->v_uobj, uio, len, IO_ADV_DECODE(ap->a_ioflag),
	    UBC_READ | UBC_PARTIALOK | UBC_VNODE_FLAGS(vp));
	littlefs_touch(VTOLITTLEFS(vp), GOP_UPDATE_ACCESSED);
	return error;
}

static int
littlefs_write(void *v)
{
	struct vop_write_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct littlefs_node *node = VTOLITTLEFS(vp);
	struct uio *uio = ap->a_uio;
	int error;
	off_t end, oldsize, start;
	size_t resid;

	if (vp->v_type != VREG)
		return EINVAL;
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;
	if (node->attr.flags & IMMUTABLE)
		return EPERM;
	if (ap->a_ioflag & IO_APPEND)
		uio->uio_offset = vp->v_size;
	if ((node->attr.flags & APPEND) && uio->uio_offset != vp->v_size)
		return EPERM;
	if (uio->uio_offset < 0 || (uint64_t)uio->uio_offset > LFS_FILE_MAX ||
	    (uint64_t)uio->uio_resid > LFS_FILE_MAX - uio->uio_offset)
		return EFBIG;
	if (uio->uio_resid == 0)
		return 0;
	oldsize = vp->v_size;
	start = uio->uio_offset;
	resid = uio->uio_resid;
	end = uio->uio_offset + uio->uio_resid;
	if (end > vp->v_size) {
		error = littlefs_resize(vp, end);
		if (error)
			return error;
	}
	error = ubc_uiomove(&vp->v_uobj, uio, uio->uio_resid,
	    IO_ADV_DECODE(ap->a_ioflag), UBC_WRITE | UBC_VNODE_FLAGS(vp));
	if (resid != uio->uio_resid) {
		mutex_enter(&node->mount->lock);
		/* Data modification must not retain privilege bits. */
		node->attr.mode &= ~(S_ISUID | S_ISGID);
		mutex_exit(&node->mount->lock);
		littlefs_touch(node, GOP_UPDATE_MODIFIED);
	}
	if (!error && ((ap->a_ioflag & (IO_SYNC | IO_DSYNC)) ||
	    (vp->v_mount->mnt_flag & MNT_SYNCHRONOUS)))
		error = VOP_FSYNC(vp, ap->a_cred, FSYNC_WAIT, 0, 0);
	if (error && (ap->a_ioflag & IO_UNIT)) {
		/* Match the BSD write contract: undo extension and reported progress. */
		if (vp->v_size > oldsize)
			(void)littlefs_resize(vp, oldsize);
		uio->uio_offset = start;
		uio->uio_resid = resid;
	} else if (error && vp->v_size > MAX(oldsize, uio->uio_offset)) {
		(void)littlefs_resize(vp, MAX(oldsize, uio->uio_offset));
	}
	return error;
}

static int
littlefs_bmap(void *v)
{
	struct vop_bmap_args *ap = v;

	if (ap->a_bn < 0 || (uint64_t)ap->a_bn > LFS_FILE_MAX >> PAGE_SHIFT)
		return EFBIG;
	if (ap->a_vpp != NULL)
		*ap->a_vpp = ap->a_vp;
	if (ap->a_bnp != NULL)
		*ap->a_bnp = ap->a_bn << (PAGE_SHIFT - DEV_BSHIFT);
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	return 0;
}

/* Logical page offsets, not physical flash blocks, arrive from genfs. */
static int
littlefs_strategy(void *v)
{
	struct vop_strategy_args *ap = v;
	struct buf *bp = ap->a_bp;
	struct vnode *vp = ap->a_vp;
	struct littlefs_node *node = VTOLITTLEFS(vp);
	struct littlefs_mount *lm = node->mount;
	off_t off = (off_t)bp->b_blkno << DEV_BSHIFT;
	lfs_ssize_t result;
	size_t len;
	int error = 0;

	bp->b_resid = bp->b_bcount;
	if (off < 0 || (uint64_t)off > LFS_FILE_MAX) {
		error = EINVAL;
		goto done;
	}
	len = MIN((off_t)bp->b_bcount, MAX(vp->v_writesize - off, 0));
	mutex_enter(&lm->lock);
	if (!(bp->b_flags & B_READ))
		error = littlefs_writable(node);
	if (!error && !node->fileopen)
		error = EIO;
	if (!error) {
		result = lfs_file_seek(&lm->lfs, &node->file, off, LFS_SEEK_SET);
		error = littlefs_error(result);
	}
	if (!error) {
		if (bp->b_flags & B_READ)
			result = lfs_file_read(&lm->lfs, &node->file, bp->b_data, len);
		else
			result = lfs_file_write(&lm->lfs, &node->file, bp->b_data, len);
		error = littlefs_error(result);
		if (!error && result != len)
			error = EIO;
		if (!error) {
			if ((bp->b_flags & B_READ) && len < bp->b_bcount)
				memset((uint8_t *)bp->b_data + len, 0, bp->b_bcount - len);
			bp->b_resid = 0;
		}
	}
	mutex_exit(&lm->lock);
done:
	bp->b_error = error;
	biodone(bp);
	return 0;
}

static void
littlefs_gop_size(struct vnode *vp, off_t size, off_t *end, int flags)
{
	*end = (flags & GOP_SIZE_MEM) ? round_page(size) : size;
}

static int
littlefs_gop_alloc(struct vnode *vp, off_t off, off_t len, int flags,
    kauth_cred_t cred)
{
	struct littlefs_node *node = VTOLITTLEFS(vp);
	int error;

	mutex_enter(&node->mount->lock);
	error = littlefs_writable(node);
	mutex_exit(&node->mount->lock);
	return error;
}

static void
littlefs_gop_markupdate(struct vnode *vp, int flags)
{
	littlefs_touch(VTOLITTLEFS(vp), flags);
}

const struct genfs_ops littlefs_genfsops = {
	.gop_size = littlefs_gop_size,
	.gop_alloc = littlefs_gop_alloc,
	.gop_write = genfs_gop_write,
	.gop_markupdate = littlefs_gop_markupdate,
	.gop_putrange = genfs_gop_putrange,
};

static int
littlefs_fsync(void *v)
{
	struct vop_fsync_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct littlefs_node *node = VTOLITTLEFS(vp);
	int error;

	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return 0;
	if (vp->v_type == VREG) {
		rw_enter(vp->v_uobj.vmobjlock, RW_WRITER);
		error = VOP_PUTPAGES(vp, trunc_page(ap->a_offlo),
		    ap->a_offhi ? round_page(ap->a_offhi) : 0,
		    PGO_CLEANIT | PGO_SYNCIO);
		if (error)
			return error;
	}
	mutex_enter(&node->mount->lock);
	error = littlefs_node_sync(node);
	if (!error)
		error = littlefs_error(littlefs_sync_device(&node->mount->cfg));
	mutex_exit(&node->mount->lock);
	return error;
}

static int
littlefs_construct(struct vnode *dvp, struct vnode **vpp,
    struct componentname *cnp, struct vattr *va, const char *target)
{
	struct littlefs_node *dir = VTOLITTLEFS(dvp);
	struct littlefs_mount *lm = dir->mount;
	struct littlefs_entry *entry;
	struct littlefs_bsd_attr attr = {0};
	struct timespec now;
	uint8_t raw[LITTLEFS_BSD_SIZE], next[8];
	char staging[64], *path;
	lfs_file_t file;
	struct lfs_file_config fc = {0};
	struct lfs_attr fa;
	int error, close_error;
	bool made = false;

	*vpp = NULL;
	error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred);
	if (error)
		return error;
	path = PNBUF_GET();
	fc.buffer = kmem_alloc(lm->cfg.cache_size, KM_SLEEP);
	mutex_enter(&lm->lock);
	error = littlefs_childpath(dir, cnp->cn_nameptr, cnp->cn_namelen, path);
	if (error)
		goto out;
	if (lm->nextino == UINT64_MAX) {
		error = ENOSPC;
		goto out;
	}
	attr.ino = lm->nextino++;
	/* Reserve identity before publishing any inode; interrupted creates leave gaps. */
	littlefs_le64enc(next, lm->nextino);
	error = littlefs_error(lfs_setattr(&lm->lfs, "/", LITTLEFS_NEXTINO_ATTR,
	    next, sizeof(next)));
	if (error)
		goto out;
	vfs_timestamp(&now);
	attr.mode = VTTOIF(va->va_type) | (va->va_mode & ALLPERMS);
	attr.uid = kauth_cred_geteuid(cnp->cn_cred);
	attr.gid = dir->attr.gid;
	attr.rdev = (va->va_type == VCHR || va->va_type == VBLK) ? va->va_rdev : 0;
	attr.atime = attr.mtime = attr.ctime = now.tv_sec;
	attr.atimensec = attr.mtimensec = attr.ctimensec = now.tv_nsec;
	littlefs_bsd_encode(raw, &attr);
	snprintf(staging, sizeof(staging), "/%s/%016" PRIx64,
	    LITTLEFS_STAGING_NAME, attr.ino);
	if (va->va_type == VDIR) {
		error = littlefs_error(lfs_mkdir(&lm->lfs, staging));
		made = !error;
		if (!error)
			error = littlefs_error(lfs_setattr(&lm->lfs, staging,
			    LITTLEFS_BSD_ATTR, raw, sizeof(raw)));
	} else {
		fa.type = LITTLEFS_BSD_ATTR;
		fa.buffer = raw;
		fa.size = sizeof(raw);
		fc.attrs = &fa;
		fc.attr_count = 1;
		error = littlefs_error(lfs_file_opencfg(&lm->lfs, &file, staging,
		    LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL, &fc));
		made = !error;
		if (!error) {
			if (target != NULL) {
				lfs_ssize_t written = lfs_file_write(&lm->lfs, &file,
				    target, strlen(target));
				error = littlefs_error(written);
				if (!error && written != strlen(target))
					error = EIO;
			}
			close_error = littlefs_error(lfs_file_close(&lm->lfs, &file));
			if (!error)
				error = close_error;
		}
	}
	if (!error)
		error = littlefs_error(lfs_rename(&lm->lfs, staging, path));
	if (error && made)
		(void)lfs_remove(&lm->lfs, staging);
	if (!error) {
		littlefs_touch(dir, GOP_UPDATE_MODIFIED);
		error = littlefs_entry_hold_path(lm, path, &entry);
	}
out:
	mutex_exit(&lm->lock);
	kmem_free(fc.buffer, lm->cfg.cache_size);
	if (!error)
		error = littlefs_entry_vnode(lm, entry, vpp);
	PNBUF_PUT(path);
	if (!error)
		cache_enter(dvp, *vpp, cnp->cn_nameptr, cnp->cn_namelen, cnp->cn_flags);
	return error;
}

static int
littlefs_create(void *v)
{
	struct vop_create_v3_args *ap = v;
	return littlefs_construct(ap->a_dvp, ap->a_vpp, ap->a_cnp, ap->a_vap, NULL);
}

static int
littlefs_mkdir(void *v)
{
	struct vop_mkdir_v3_args *ap = v;
	return littlefs_construct(ap->a_dvp, ap->a_vpp, ap->a_cnp, ap->a_vap, NULL);
}

static int
littlefs_mknod(void *v)
{
	struct vop_mknod_v3_args *ap = v;
	return littlefs_construct(ap->a_dvp, ap->a_vpp, ap->a_cnp, ap->a_vap, NULL);
}

static int
littlefs_symlink(void *v)
{
	struct vop_symlink_v3_args *ap = v;
	return littlefs_construct(ap->a_dvp, ap->a_vpp, ap->a_cnp, ap->a_vap,
	    ap->a_target);
}

static int
littlefs_remove_node(struct vnode *dvp, struct vnode *vp,
    struct componentname *cnp)
{
	struct littlefs_node *dir = VTOLITTLEFS(dvp);
	struct littlefs_node *node = VTOLITTLEFS(vp);
	struct littlefs_mount *lm = dir->mount;
	int error;

	if (vp == dvp || (vp->v_vflag & VV_ROOT))
		return EINVAL;
	if (dvp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;
	error = genfs_ufslike_remove_check_possible(dir->attr.flags,
	    node->attr.flags, IMMUTABLE, APPEND);
	if (!error)
		error = genfs_ufslike_remove_check_permitted(cnp->cn_cred,
		    dvp, dir->attr.mode, dir->attr.uid, vp, node->attr.uid);
	if (error)
		return error;
	mutex_enter(&lm->lock);
	error = littlefs_error(lfs_remove(&lm->lfs, node->entry->path));
	if (!error) {
		node->entry->removed = true;
		littlefs_touch(dir, GOP_UPDATE_MODIFIED);
	}
	mutex_exit(&lm->lock);
	if (!error)
		cache_purge(vp);
	return error;
}

static int
littlefs_remove(void *v)
{
	struct vop_remove_v3_args *ap = v;
	int error;

	error = ap->a_vp->v_type == VDIR ? EPERM :
	    littlefs_remove_node(ap->a_dvp, ap->a_vp, ap->a_cnp);
	if (!error)
		ap->ctx_vp_new_nlink = 0;
	if (ap->a_dvp == ap->a_vp)
		vrele(ap->a_vp);
	else
		vput(ap->a_vp);
	return error;
}

static int
littlefs_rmdir(void *v)
{
	struct vop_rmdir_v2_args *ap = v;
	int error;

	error = littlefs_remove_node(ap->a_dvp, ap->a_vp, ap->a_cnp);
	if (ap->a_dvp == ap->a_vp)
		vrele(ap->a_vp);
	else
		vput(ap->a_vp);
	return error;
}

static int
littlefs_readdir(void *v)
{
	struct vop_readdir_args *ap = v;
	struct littlefs_node *node = VTOLITTLEFS(ap->a_vp);
	struct littlefs_mount *lm = node->mount;
	struct uio *uio = ap->a_uio;
	struct lfs_info info, childinfo;
	struct littlefs_bsd_attr attr;
	struct dirent de;
	char *path;
	off_t *cookies = NULL, cookie;
	int error = 0, count = 0, maxcookies = 0;
	bool emitted = false;

	if (ap->a_vp->v_type != VDIR)
		return ENOTDIR;
	if (uio->uio_offset < 0 || (uint64_t)uio->uio_offset > UINT32_MAX)
		return EINVAL;
	if (uio->uio_resid < _DIRENT_MINSIZE(&de))
		return EINVAL;
	if (ap->a_eofflag != NULL)
		*ap->a_eofflag = false;
	if (ap->a_ncookies != NULL) {
		*ap->a_ncookies = 0;
		*ap->a_cookies = NULL;
		maxcookies = uio->uio_resid / _DIRENT_MINSIZE(&de);
		if (maxcookies != 0)
			cookies = malloc(maxcookies * sizeof(*cookies), M_TEMP, M_WAITOK);
	}
	path = PNBUF_GET();
	while (uio->uio_resid >= _DIRENT_MINSIZE(&de)) {
		mutex_enter(&lm->lock);
		if (node->entry->removed) {
			mutex_exit(&lm->lock);
			error = ENOENT;
			break;
		}
		error = lfs_dir_seek(&lm->lfs, &node->dir, uio->uio_offset);
		if (!error)
			error = lfs_dir_read(&lm->lfs, &node->dir, &info);
		if (error <= 0) {
			if (!error && ap->a_eofflag != NULL)
				*ap->a_eofflag = true;
			error = littlefs_error(error);
			mutex_exit(&lm->lock);
			break;
		}
		cookie = lfs_dir_tell(&lm->lfs, &node->dir);
		if (node->attr.ino == LITTLEFS_ROOT_INO &&
		    !strcmp(info.name, LITTLEFS_STAGING_NAME)) {
			uio->uio_offset = cookie;
			mutex_exit(&lm->lock);
			continue;
		}
		error = littlefs_childpath(node, info.name, strlen(info.name), path);
		if (!error)
			error = littlefs_readattr(lm, path, &attr, &childinfo);
		mutex_exit(&lm->lock);
		if (error)
			break;
		memset(&de, 0, sizeof(de));
		de.d_fileno = attr.ino;
		de.d_namlen = strlen(info.name);
		de.d_reclen = _DIRENT_RECLEN(&de, de.d_namlen);
		de.d_type = IFTODT(attr.mode);
		memcpy(de.d_name, info.name, de.d_namlen + 1);
		if (de.d_reclen > uio->uio_resid) {
			if (!emitted)
				error = EINVAL;
			break;
		}
		error = uiomove(&de, de.d_reclen, uio);
		if (error)
			break;
		emitted = true;
		uio->uio_offset = cookie;
		if (cookies != NULL) {
			cookies[count++] = cookie;
			if (count == maxcookies)
				break;
		}
	}
	PNBUF_PUT(path);
	if (cookies != NULL) {
		if (!error && count != 0) {
			*ap->a_cookies = cookies;
			*ap->a_ncookies = count;
		} else {
			free(cookies, M_TEMP);
		}
	}
	littlefs_touch(node, GOP_UPDATE_ACCESSED);
	return error;
}

static int
littlefs_readlink(void *v)
{
	struct vop_readlink_args *ap = v;
	struct littlefs_node *node = VTOLITTLEFS(ap->a_vp);
	struct littlefs_mount *lm = node->mount;
	char *buffer;
	lfs_ssize_t n;
	size_t size = ap->a_vp->v_size;
	int error;

	if (ap->a_vp->v_type != VLNK || size >= MAXPATHLEN)
		return EINVAL;
	if (size == 0)
		return 0;
	buffer = kmem_alloc(size, KM_SLEEP);
	mutex_enter(&lm->lock);
	error = littlefs_error(lfs_file_seek(&lm->lfs, &node->file, 0, LFS_SEEK_SET));
	if (!error) {
		n = lfs_file_read(&lm->lfs, &node->file, buffer, size);
		error = littlefs_error(n);
		if (!error && n != size)
			error = EIO;
	}
	mutex_exit(&lm->lock);
	if (!error)
		error = uiomove(buffer, MIN(size, ap->a_uio->uio_resid), ap->a_uio);
	kmem_free(buffer, size);
	return error;
}

static int
littlefs_inactive(void *v)
{
	struct vop_inactive_v2_args *ap = v;
	int error = 0;
	*ap->a_recycle = VTOLITTLEFS(ap->a_vp)->entry->removed;
	if (!*ap->a_recycle)
		error = VOP_FSYNC(ap->a_vp, NOCRED, FSYNC_WAIT, 0, 0);
	return error;
}

static int
littlefs_reclaim(void *v)
{
	struct vop_reclaim_v2_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct littlefs_node *node = VTOLITTLEFS(vp);
	struct littlefs_mount *lm = node->mount;

	mutex_enter(&lm->lock);
	if (node->fileopen)
		(void)lfs_file_close(&lm->lfs, &node->file);
	if (node->diropen)
		(void)lfs_dir_close(&lm->lfs, &node->dir);
	node->entry->node = NULL;
	littlefs_entry_drop(lm, node->entry);
	mutex_exit(&lm->lock);
	VOP_UNLOCK(vp);
	genfs_node_destroy(vp);
	if (node->filebuf != NULL)
		kmem_free(node->filebuf, lm->cfg.cache_size);
	kmem_free(node, sizeof(*node));
	vp->v_data = NULL;
	return 0;
}

static int
littlefs_advlock(void *v)
{
	struct vop_advlock_args *ap = v;
	struct littlefs_node *node = VTOLITTLEFS(ap->a_vp);
	return lf_advlock(ap, &node->lockf, ap->a_vp->v_size);
}

static int
littlefs_pathconf(void *v)
{
	struct vop_pathconf_args *ap = v;
	switch (ap->a_name) {
	case _PC_LINK_MAX: *ap->a_retval = 1; return 0;
	case _PC_NAME_MAX: *ap->a_retval = LFS_NAME_MAX; return 0;
	case _PC_PATH_MAX: *ap->a_retval = MAXPATHLEN; return 0;
	case _PC_CHOWN_RESTRICTED: *ap->a_retval = 1; return 0;
	case _PC_NO_TRUNC: *ap->a_retval = 1; return 0;
	default: return genfs_pathconf(v);
	}
}

int (**littlefs_vnodeop_p)(void *);
static const struct vnodeopv_entry_desc littlefs_vnodeop_entries[] = {
	{ &vop_default_desc, vn_default_error },
	{ &vop_parsepath_desc, genfs_parsepath },
	{ &vop_lookup_desc, littlefs_lookup },
	{ &vop_create_desc, littlefs_create },
	{ &vop_mknod_desc, littlefs_mknod },
	{ &vop_open_desc, littlefs_open },
	{ &vop_close_desc, genfs_nullop },
	{ &vop_access_desc, littlefs_access },
	{ &vop_accessx_desc, genfs_accessx },
	{ &vop_getattr_desc, littlefs_getattr },
	{ &vop_setattr_desc, littlefs_setattr },
	{ &vop_read_desc, littlefs_read },
	{ &vop_write_desc, littlefs_write },
	{ &vop_fallocate_desc, genfs_eopnotsupp },
	{ &vop_fdiscard_desc, genfs_eopnotsupp },
	{ &vop_ioctl_desc, genfs_enoioctl },
	{ &vop_fcntl_desc, genfs_fcntl },
	{ &vop_poll_desc, genfs_poll },
	{ &vop_kqfilter_desc, genfs_kqfilter },
	{ &vop_revoke_desc, genfs_revoke },
	{ &vop_mmap_desc, genfs_mmap },
	{ &vop_fsync_desc, littlefs_fsync },
	{ &vop_seek_desc, genfs_seek },
	{ &vop_remove_desc, littlefs_remove },
	{ &vop_link_desc, genfs_eopnotsupp },
	{ &vop_rename_desc, littlefs_rename },
	{ &vop_mkdir_desc, littlefs_mkdir },
	{ &vop_rmdir_desc, littlefs_rmdir },
	{ &vop_symlink_desc, littlefs_symlink },
	{ &vop_readdir_desc, littlefs_readdir },
	{ &vop_readlink_desc, littlefs_readlink },
	{ &vop_abortop_desc, genfs_abortop },
	{ &vop_inactive_desc, littlefs_inactive },
	{ &vop_reclaim_desc, littlefs_reclaim },
	{ &vop_lock_desc, genfs_lock },
	{ &vop_unlock_desc, genfs_unlock },
	{ &vop_islocked_desc, genfs_islocked },
	{ &vop_bmap_desc, littlefs_bmap },
	{ &vop_print_desc, genfs_nullop },
	{ &vop_pathconf_desc, littlefs_pathconf },
	{ &vop_advlock_desc, littlefs_advlock },
	{ &vop_getpages_desc, genfs_getpages },
	{ &vop_putpages_desc, genfs_putpages },
	{ &vop_bwrite_desc, vn_bwrite },
	{ &vop_strategy_desc, littlefs_strategy },
	{ NULL, NULL }
};
const struct vnodeopv_desc littlefs_vnodeop_opv_desc = {
	&littlefs_vnodeop_p, littlefs_vnodeop_entries
};

#define LITTLEFS_SPECIAL_OPS \
	{ &vop_access_desc, littlefs_access }, \
	{ &vop_accessx_desc, genfs_accessx }, \
	{ &vop_getattr_desc, littlefs_getattr }, \
	{ &vop_setattr_desc, littlefs_setattr }, \
	{ &vop_inactive_desc, littlefs_inactive }, \
	{ &vop_reclaim_desc, littlefs_reclaim }, \
	{ &vop_lock_desc, genfs_lock }, \
	{ &vop_unlock_desc, genfs_unlock }, \
	{ &vop_islocked_desc, genfs_islocked }, \
	{ &vop_print_desc, genfs_nullop }

int (**littlefs_specop_p)(void *);
static const struct vnodeopv_entry_desc littlefs_specop_entries[] = {
	{ &vop_default_desc, vn_default_error },
	GENFS_SPECOP_ENTRIES,
	LITTLEFS_SPECIAL_OPS,
	{ &vop_close_desc, spec_close },
	{ &vop_read_desc, spec_read },
	{ &vop_write_desc, spec_write },
	{ &vop_fsync_desc, spec_fsync },
	{ &vop_fcntl_desc, genfs_fcntl },
	{ &vop_bwrite_desc, vn_bwrite },
	{ NULL, NULL }
};
const struct vnodeopv_desc littlefs_specop_opv_desc = {
	&littlefs_specop_p, littlefs_specop_entries
};

int (**littlefs_fifoop_p)(void *);
static const struct vnodeopv_entry_desc littlefs_fifoop_entries[] = {
	{ &vop_default_desc, vn_default_error },
	GENFS_FIFOOP_ENTRIES,
	LITTLEFS_SPECIAL_OPS,
	{ &vop_close_desc, vn_fifo_bypass },
	{ &vop_read_desc, vn_fifo_bypass },
	{ &vop_write_desc, vn_fifo_bypass },
	{ &vop_fsync_desc, vn_fifo_bypass },
	{ &vop_fcntl_desc, genfs_fcntl },
	{ &vop_bwrite_desc, vn_bwrite },
	{ &vop_strategy_desc, vn_fifo_bypass },
	{ NULL, NULL }
};
const struct vnodeopv_desc littlefs_fifoop_opv_desc = {
	&littlefs_fifoop_p, littlefs_fifoop_entries
};
