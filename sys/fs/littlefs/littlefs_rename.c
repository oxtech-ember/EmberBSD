/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <fs/littlefs/littlefs.h>
#include <sys/kmem.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <miscfs/genfs/genfs.h>

int littlefs_rename(void *);
static const struct genfs_rename_ops littlefs_rename_ops;

static int
littlefs_rename_lookup(struct mount *mp, struct vnode *dvp,
    struct componentname *cnp, void *unused, struct vnode **vpp)
{
	struct littlefs_node *dir = VTOLITTLEFS(dvp);
	return littlefs_getchild(dir, cnp->cn_nameptr, cnp->cn_namelen, vpp);
}

static int
littlefs_rename_lockdir(struct mount *mp, struct vnode *vp)
{
	int error = vn_lock(vp, LK_EXCLUSIVE);
	if (!error && VTOLITTLEFS(vp)->entry->removed) {
		VOP_UNLOCK(vp);
		error = ENOENT;
	}
	return error;
}

static bool
littlefs_directory_empty(struct mount *mp, kauth_cred_t cred,
    struct vnode *vp, struct vnode *dvp)
{
	struct littlefs_mount *lm = VFSTOLITTLEFS(mp);
	struct littlefs_node *node = VTOLITTLEFS(vp);
	struct lfs_info info;
	lfs_soff_t saved;
	int result;

	mutex_enter(&lm->lock);
	saved = lfs_dir_tell(&lm->lfs, &node->dir);
	result = lfs_dir_rewind(&lm->lfs, &node->dir);
	if (!result) {
		while ((result = lfs_dir_read(&lm->lfs, &node->dir, &info)) > 0) {
			if (strcmp(info.name, ".") && strcmp(info.name, ".."))
				break;
		}
	}
	if (saved >= 0)
		(void)lfs_dir_seek(&lm->lfs, &node->dir, saved);
	mutex_exit(&lm->lock);
	return result == 0;
}

static int
littlefs_rename_possible(struct mount *mp, struct vnode *fdvp,
    struct vnode *fvp, struct vnode *tdvp, struct vnode *tvp)
{
	if (mp->mnt_flag & MNT_RDONLY)
		return EROFS;
	return genfs_ufslike_rename_check_possible(
	    VTOLITTLEFS(fdvp)->attr.flags, VTOLITTLEFS(fvp)->attr.flags,
	    VTOLITTLEFS(tdvp)->attr.flags, tvp ? VTOLITTLEFS(tvp)->attr.flags : 0,
	    tvp != NULL, IMMUTABLE, APPEND);
}

static int
littlefs_rename_permitted(struct mount *mp, kauth_cred_t cred,
    struct vnode *fdvp, struct vnode *fvp, struct vnode *tdvp, struct vnode *tvp)
{
	return genfs_ufslike_rename_check_permitted(cred,
	    fdvp, VTOLITTLEFS(fdvp)->attr.mode, VTOLITTLEFS(fdvp)->attr.uid,
	    fvp, VTOLITTLEFS(fvp)->attr.uid,
	    tdvp, VTOLITTLEFS(tdvp)->attr.mode, VTOLITTLEFS(tdvp)->attr.uid,
	    tvp, tvp ? VTOLITTLEFS(tvp)->attr.uid : 0);
}

static int
littlefs_remove_possible(struct mount *mp, struct vnode *dvp, struct vnode *vp)
{
	if (mp->mnt_flag & MNT_RDONLY)
		return EROFS;
	return genfs_ufslike_remove_check_possible(VTOLITTLEFS(dvp)->attr.flags,
	    VTOLITTLEFS(vp)->attr.flags, IMMUTABLE, APPEND);
}

static int
littlefs_remove_permitted(struct mount *mp, kauth_cred_t cred,
    struct vnode *dvp, struct vnode *vp)
{
	return genfs_ufslike_remove_check_permitted(cred, dvp,
	    VTOLITTLEFS(dvp)->attr.mode, VTOLITTLEFS(dvp)->attr.uid,
	    vp, VTOLITTLEFS(vp)->attr.uid);
}

/* All renames hold genfs' mount rename lock; paths identify the ancestry. */
static int
littlefs_genealogy(struct mount *mp, kauth_cred_t cred,
    struct vnode *fdvp, struct vnode *tdvp, struct vnode **intermediate)
{
	struct littlefs_mount *lm = VFSTOLITTLEFS(mp);
	char *path = PNBUF_GET();
	const char *from, *to;
	size_t n;
	int error = 0;
	bool ancestor = false;

	*intermediate = NULL;
	mutex_enter(&lm->lock);
	if (VTOLITTLEFS(fdvp)->entry->removed || VTOLITTLEFS(tdvp)->entry->removed) {
		error = ENOENT;
		goto out;
	}
	from = VTOLITTLEFS(fdvp)->entry->path;
	to = VTOLITTLEFS(tdvp)->entry->path;
	n = strlen(from);
	if ((n == 1 && to[1] != '\0') ||
	    (!strncmp(from, to, n) && to[n] == '/')) {
		char *slash;
		strlcpy(path, to, MAXPATHLEN);
		slash = strchr(path + n + (n != 1), '/');
		if (slash != NULL)
			*slash = '\0';
		ancestor = true;
	}
out:
	mutex_exit(&lm->lock);
	if (!error && ancestor)
		error = littlefs_getpath(mp, path, intermediate);
	PNBUF_PUT(path);
	return error;
}

struct littlefs_pathchange {
	struct littlefs_pathchange *next;
	struct littlefs_entry *entry;
	char *path;
	size_t len;
};

static int
littlefs_do_rename(struct mount *mp, kauth_cred_t cred,
    struct vnode *fdvp, struct componentname *fcnp, void *fde, struct vnode *fvp,
    struct vnode *tdvp, struct componentname *tcnp, void *tde, struct vnode *tvp,
    nlink_t *nlink)
{
	struct littlefs_mount *lm = VFSTOLITTLEFS(mp);
	struct littlefs_pathchange *changes = NULL, *change;
	struct littlefs_entry *entry;
	const char *oldpath;
	char *path = PNBUF_GET();
	size_t oldlen, len;
	int error;

	mutex_enter(&lm->lock);
	error = littlefs_childpath(VTOLITTLEFS(tdvp), tcnp->cn_nameptr,
	    tcnp->cn_namelen, path);
	if (error)
		goto out;
	oldpath = VTOLITTLEFS(fvp)->entry->path;
	oldlen = strlen(oldpath);
	len = strlen(path);
	/* Allocate every affected cached pathname before the atomic disk rename. */
	LIST_FOREACH(entry, &lm->entries, link) {
		if (entry->removed || strncmp(entry->path, oldpath, oldlen) ||
		    (entry->path[oldlen] != '\0' && entry->path[oldlen] != '/'))
			continue;
		if (len + entry->pathlen - oldlen > MAXPATHLEN) {
			error = ENAMETOOLONG;
			goto out;
		}
		change = kmem_alloc(sizeof(*change), KM_NOSLEEP);
		if (change == NULL) {
			error = ENOMEM;
			goto out;
		}
		change->entry = entry;
		change->len = len + entry->pathlen - oldlen;
		change->path = kmem_alloc(change->len, KM_NOSLEEP);
		if (change->path == NULL) {
			kmem_free(change, sizeof(*change));
			error = ENOMEM;
			goto out;
		}
		memcpy(change->path, path, len);
		memcpy(change->path + len, entry->path + oldlen, entry->pathlen - oldlen);
		change->next = changes;
		changes = change;
	}
	error = littlefs_error(lfs_rename(&lm->lfs, oldpath, path));
	if (!error) {
		if (tvp != NULL) {
			VTOLITTLEFS(tvp)->entry->removed = true;
			*nlink = 0;
		}
		for (change = changes; change != NULL; change = change->next) {
			entry = change->entry;
			kmem_free(entry->path, entry->pathlen);
			entry->path = change->path;
			entry->pathlen = change->len;
			change->path = NULL;
		}
		littlefs_touch(VTOLITTLEFS(fdvp), GOP_UPDATE_MODIFIED);
		littlefs_touch(VTOLITTLEFS(tdvp), GOP_UPDATE_MODIFIED);
		littlefs_touch(VTOLITTLEFS(fvp), LITTLEFS_UPDATE_CHANGED);
	}
out:
	mutex_exit(&lm->lock);
	while ((change = changes) != NULL) {
		changes = change->next;
		if (change->path != NULL)
			kmem_free(change->path, change->len);
		kmem_free(change, sizeof(*change));
	}
	PNBUF_PUT(path);
	if (!error)
		genfs_rename_cache_purge(fdvp, fvp, tdvp, tvp);
	return error;
}

static int
littlefs_rename_remove(struct mount *mp, kauth_cred_t cred,
    struct vnode *dvp, struct componentname *cnp, void *de,
    struct vnode *vp, nlink_t *nlink)
{
	/* There cannot be distinct hard links to the same inode. */
	return EOPNOTSUPP;
}

static const struct genfs_rename_ops littlefs_rename_ops = {
	.gro_directory_empty_p = littlefs_directory_empty,
	.gro_rename_check_possible = littlefs_rename_possible,
	.gro_rename_check_permitted = littlefs_rename_permitted,
	.gro_remove_check_possible = littlefs_remove_possible,
	.gro_remove_check_permitted = littlefs_remove_permitted,
	.gro_rename = littlefs_do_rename,
	.gro_remove = littlefs_rename_remove,
	.gro_lookup = littlefs_rename_lookup,
	.gro_genealogy = littlefs_genealogy,
	.gro_lock_directory = littlefs_rename_lockdir,
};

static int
littlefs_sane_rename(struct vnode *fdvp, struct componentname *fcnp,
    struct vnode *tdvp, struct componentname *tcnp, kauth_cred_t cred, bool posix)
{
	return genfs_sane_rename(&littlefs_rename_ops, fdvp, fcnp, NULL,
	    tdvp, tcnp, NULL, cred, posix);
}

int
littlefs_rename(void *v)
{
	return genfs_insane_rename(v, littlefs_sane_rename);
}
