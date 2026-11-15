/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/gem/i915_gemfs.c (the calls and what it does with
 * their results); the GPL original was not read.
 *
 * The mount of a file system by the kernel for 6.18 sources (the private tmpfs of the driver): types
 * and calls, declared, not defined.  There is no mounting of a second shmem file system here; the
 * shmem files of the layer come from shmem_file_setup.
 */
#ifndef _DRM618_LINUX_FS_CONTEXT_H_
#define _DRM618_LINUX_FS_CONTEXT_H_

struct file_system_type;
struct fs_context;
struct vfsmount;

#define	SB_KERNMOUNT	0x00400000

struct fs_context *fs_context_for_mount(struct file_system_type *, unsigned int);
struct vfsmount *fc_mount_longterm(struct fs_context *);
void	put_fs_context(struct fs_context *);
struct file_system_type *get_fs_type(const char *);
int	vfs_parse_fs_string(struct fs_context *, const char *, const char *);
void	kern_unmount(struct vfsmount *);

#endif
