/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _FS_LITTLEFS_H_
#define _FS_LITTLEFS_H_

#include <fs/littlefs/littlefs_kernel.h>
#include <fs/littlefs/littlefs_format.h>
#include <fs/littlefs/littlefs_mount.h>
#include <sys/mutex.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <miscfs/genfs/genfs_node.h>
#include <lfs.h>

struct littlefs_node;
struct littlefs_entry {
	LIST_ENTRY(littlefs_entry) link;
	uint64_t ino;
	char *path;
	size_t pathlen;
	unsigned refs;
	bool removed;
	struct littlefs_node *node;
};

struct littlefs_mount {
	struct mount *mp;
	struct vnode *devvp;
	kmutex_t lock;		/* upstream instance and entry registry */
	lfs_t lfs;
	struct lfs_config cfg;
	void *readbuf, *progbuf, *lookbuf;
	uint64_t nextino;
	LIST_HEAD(, littlefs_entry) entries;
	device_t flashdev;	/* generic flash(4), NULL for block devices */
	uint64_t mediasize;
	int openflags;
	bool readonly, writeprotected;
};

struct littlefs_node {
	struct genfs_node gen;
	struct littlefs_entry *entry;
	struct littlefs_mount *mount;
	struct vnode *vp;
	struct lockf *lockf;
	struct littlefs_bsd_attr attr;
	lfs_file_t file;
	lfs_dir_t dir;
	struct lfs_file_config filecfg;
	struct lfs_attr fileattr;
	uint8_t attrbuf[LITTLEFS_BSD_SIZE];
	void *filebuf;
	bool fileopen, diropen, writable, metadirty;
	volatile unsigned updates;
};

#define VFSTOLITTLEFS(mp) ((struct littlefs_mount *)(mp)->mnt_data)
#define VTOLITTLEFS(vp) ((struct littlefs_node *)(vp)->v_data)
#define LITTLEFS_UPDATE_CHANGED 4

extern int (**littlefs_vnodeop_p)(void *);
extern int (**littlefs_specop_p)(void *);
extern int (**littlefs_fifoop_p)(void *);
extern const struct genfs_ops littlefs_genfsops;

int littlefs_error(int);
int littlefs_readattr(struct littlefs_mount *, const char *,
    struct littlefs_bsd_attr *, struct lfs_info *);
int littlefs_writeattr(struct littlefs_node *);
int littlefs_getpath(struct mount *, const char *, struct vnode **);
int littlefs_getchild(struct littlefs_node *, const char *, size_t, struct vnode **);
int littlefs_entry_hold_path(struct littlefs_mount *, const char *,
    struct littlefs_entry **);
int littlefs_entry_vnode(struct littlefs_mount *, struct littlefs_entry *,
    struct vnode **);
void littlefs_entry_drop(struct littlefs_mount *, struct littlefs_entry *);
int littlefs_childpath(struct littlefs_node *, const char *, size_t, char *);
int littlefs_node_sync(struct littlefs_node *);
int littlefs_writable(struct littlefs_node *);
void littlefs_touch(struct littlefs_node *, unsigned);
void littlefs_update_times(struct littlefs_node *);
int littlefs_sync_device(const struct lfs_config *);
int littlefs_device_init(struct littlefs_mount *, uint32_t);
void littlefs_device_fini(struct littlefs_mount *);

#endif
