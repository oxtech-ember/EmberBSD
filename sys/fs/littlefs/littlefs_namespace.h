/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _FS_LITTLEFS_NAMESPACE_H_
#define _FS_LITTLEFS_NAMESPACE_H_

/* NetBSD's log-structured filesystem already exports lfs_* symbols. */
#define lfs_crc			littlefs_core_crc
#define lfs_dir_close		littlefs_core_dir_close
#define lfs_dir_open		littlefs_core_dir_open
#define lfs_dir_read		littlefs_core_dir_read
#define lfs_dir_rewind		littlefs_core_dir_rewind
#define lfs_dir_seek		littlefs_core_dir_seek
#define lfs_dir_tell		littlefs_core_dir_tell
#define lfs_file_close		littlefs_core_file_close
#define lfs_file_open		littlefs_core_file_open
#define lfs_file_opencfg		littlefs_core_file_opencfg
#define lfs_file_read		littlefs_core_file_read
#define lfs_file_rewind		littlefs_core_file_rewind
#define lfs_file_seek		littlefs_core_file_seek
#define lfs_file_size		littlefs_core_file_size
#define lfs_file_sync		littlefs_core_file_sync
#define lfs_file_tell		littlefs_core_file_tell
#define lfs_file_truncate		littlefs_core_file_truncate
#define lfs_file_write		littlefs_core_file_write
#define lfs_format		littlefs_core_format
#define lfs_fs_gc		littlefs_core_fs_gc
#define lfs_fs_grow		littlefs_core_fs_grow
#define lfs_fs_mkconsistent	littlefs_core_fs_mkconsistent
#define lfs_fs_size		littlefs_core_fs_size
#define lfs_fs_stat		littlefs_core_fs_stat
#define lfs_fs_traverse		littlefs_core_fs_traverse
#define lfs_getattr		littlefs_core_getattr
#define lfs_migrate		littlefs_core_migrate
#define lfs_mkdir		littlefs_core_mkdir
#define lfs_mount		littlefs_core_mount
#define lfs_remove		littlefs_core_remove
#define lfs_removeattr		littlefs_core_removeattr
#define lfs_rename		littlefs_core_rename
#define lfs_setattr		littlefs_core_setattr
#define lfs_stat			littlefs_core_stat
#define lfs_unmount		littlefs_core_unmount

#endif /* _FS_LITTLEFS_NAMESPACE_H_ */
