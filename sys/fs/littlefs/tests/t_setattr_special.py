#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Exercise production littlefs_setattr against special vnode size requests."""

import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--cc-flag", action="append", default=[])
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

root = pathlib.Path(__file__).resolve().parents[4]
source = (root / "sys/fs/littlefs/littlefs_vnops.c").read_text()
match = re.search(r"static int\nlittlefs_setattr\(void \*v\)\n\{", source)
assert match, "missing production littlefs_setattr"
depth, end = 1, match.end()
while depth:
    if source[end] == "{":
        depth += 1
    elif source[end] == "}":
        depth -= 1
    end += 1
setattr_function = source[match.start():end]
resize_match = re.search(r"static int\nlittlefs_resize\(struct vnode \*vp, off_t size\)\n\{", source)
assert resize_match, "missing production littlefs_resize"
depth, resize_end = 1, resize_match.end()
while depth:
    if source[resize_end] == "{":
        depth += 1
    elif source[resize_end] == "}":
        depth -= 1
    resize_end += 1
resize_function = source[resize_match.start():resize_end]

fixture = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VNON 0
#define VREG 1
#define VDIR 2
#define VCHR 3
#define VBLK 4
#define VFIFO 5
#define VWRITE 2
#define VNOVAL (-1)
#define MNT_RDONLY 1
#define FSYNC_WAIT 1
#define IMMUTABLE 1
#define APPEND 2
#define UF_SETTABLE 3
#define SF_SETTABLE 12
#define SF_IMMUTABLE 4
#define SF_APPEND 8
#define GOP_UPDATE_MODIFIED 1
#define PAGE_MASK 4095
#define PAGE_SIZE 4096
#define LFS_FILE_MAX INT32_MAX
#define ALLPERMS 07777
#define S_IFMT 0170000
#define S_ISUID 04000
#define S_ISGID 02000
#define KAUTH_VNODE_WRITE_FLAGS 1
#define KAUTH_VNODE_WRITE_SYSFLAGS 2
#define KAUTH_VNODE_HAS_SYSFLAGS 4
#define KAUTH_VNODE_CHANGE_OWNERSHIP 8
#define KAUTH_VNODE_WRITE_SECURITY 16
#define KAUTH_VNODE_WRITE_TIMES 32
#define UBC_VNODE_FLAGS(vp) (0)
#define VTOLITTLEFS(vp) ((vp)->node)
#define mutex_enter(p) ((void)(p))
#define mutex_exit(p) ((void)(p))
#define genfs_node_wrlock(vp) ((void)(vp))
#define genfs_node_unlock(vp) ((void)(vp))
#define VOP_FSYNC(vp, c, w, s, e) (fsync_calls++, fsync_error)
#define VOP_ACCESS(vp, m, c) (access_calls++, access_error)

typedef int64_t off_t;
typedef int64_t uid_t;
typedef int64_t gid_t;
typedef void *kauth_cred_t;
typedef int kauth_action_t;
struct timespec { int64_t tv_sec, tv_nsec; };
struct vattr {
    int va_type, va_nlink, va_fsid, va_fileid, va_blocksize, va_rdev;
    int va_bytes, va_gen, va_mode, va_flags, va_uid, va_gid;
    off_t va_size;
    struct timespec va_atime, va_mtime;
    struct timespec va_birthtime;
    int va_vaflags;
};
struct mount { int mnt_flag; };
struct littlefs_bsd_attr {
    int64_t uid, gid;
    unsigned mode, flags;
    off_t size;
    int64_t atime, mtime, ctime;
    int32_t atimensec, mtimensec, ctimensec;
};
struct littlefs_mount { int lock; int lfs; };
struct littlefs_node { struct littlefs_mount *mount;
    struct littlefs_bsd_attr attr; int file; };
struct vnode { int v_type; struct mount *v_mount; struct littlefs_node *node;
    off_t v_size; int v_uobj; };
struct vop_setattr_args { struct vnode *a_vp; struct vattr *a_vap;
    kauth_cred_t a_cred; };

static int access_error, auth_error, fsync_error, fsync_calls, access_calls;
static int writable_error, truncate_error;
static int truncate_calls, attr_writes, cache_updates;
static off_t truncated_to;
static struct timespec fixed_time = { 123, 456 };
static void vfs_timestamp(struct timespec *t) { *t = fixed_time; }
static int littlefs_writable(struct littlefs_node *n)
{ (void)n; return writable_error; }
static void littlefs_update_times(struct littlefs_node *n) { (void)n; }
static int littlefs_error(int e) { return e; }
static int kauth_authorize_vnode(kauth_cred_t c, kauth_action_t a,
    struct vnode *v, void *p, int allowed)
{ (void)c; (void)a; (void)v; (void)p; (void)allowed; return auth_error; }
static int genfs_can_chflags(struct vnode *v, kauth_cred_t c, int64_t uid,
    bool sysflags) { (void)v; (void)c; (void)uid; (void)sysflags; return 0; }
static int genfs_can_chown(struct vnode *v, kauth_cred_t c, int64_t ou,
    int64_t og, int64_t nu, int64_t ng)
{ (void)v; (void)c; (void)ou; (void)og; (void)nu; (void)ng; return 0; }
static int genfs_can_chmod(struct vnode *v, kauth_cred_t c, int64_t uid,
    int64_t gid, int mode)
{ (void)v; (void)c; (void)uid; (void)gid; (void)mode; return 0; }
static int genfs_can_chtimes(struct vnode *v, kauth_cred_t c, int64_t uid,
    int flags) { (void)v; (void)c; (void)uid; (void)flags; return 0; }
static void ubc_zerorange(void *u, off_t s, off_t n, int f)
{ (void)u; (void)s; (void)n; (void)f; }
static int lfs_file_truncate(void *l, void *f, off_t n)
{ (void)l; (void)f; truncated_to = n; truncate_calls++; return truncate_error; }
static void uvm_vnp_setsize(struct vnode *v, off_t n) { v->v_size = n; }
static void littlefs_touch(struct littlefs_node *n, int flags)
{ (void)n; (void)flags; }
static int littlefs_writeattr(struct littlefs_node *n)
{ (void)n; attr_writes++; return 0; }
static void cache_enter_id(struct vnode *v, unsigned m, int64_t u,
    int64_t g, bool b) { (void)v; (void)m; (void)u; (void)g; (void)b;
    cache_updates++; }

""" + resize_function + "\n" + setattr_function + r"""
static void reset(void)
{
    access_error = auth_error = fsync_error = fsync_calls = access_calls = 0;
    writable_error = truncate_error = 0;
    truncate_calls = attr_writes = cache_updates = 0;
    truncated_to = -1;
}

static struct vattr empty_vattr(void)
{
    struct vattr va;
    memset(&va, 0, sizeof(va));
    va.va_type = VNON;
    va.va_nlink = va.va_fsid = va.va_fileid = VNOVAL;
    va.va_blocksize = va.va_rdev = va.va_bytes = va.va_gen = VNOVAL;
    va.va_mode = va.va_flags = va.va_uid = va.va_gid = VNOVAL;
    va.va_size = VNOVAL;
    va.va_atime.tv_sec = va.va_mtime.tv_sec = VNOVAL;
    va.va_birthtime.tv_sec = VNOVAL;
    return va;
}

static int setattr(int type, bool readonly, off_t size, off_t oldsize,
    unsigned flags, int mode)
{
    struct littlefs_mount lm = {0};
    struct littlefs_node node = { .mount = &lm,
        .attr = { .uid = 10, .gid = 20, .mode = 0100644,
            .flags = flags, .size = oldsize } };
    struct mount mp = { .mnt_flag = readonly ? MNT_RDONLY : 0 };
    struct vnode vp = { .v_type = type, .v_mount = &mp, .node = &node,
        .v_size = oldsize };
    struct vattr va = empty_vattr();
    struct vop_setattr_args ap = { .a_vp = &vp, .a_vap = &va };
    if (size != VNOVAL) va.va_size = size;
    if (mode != VNOVAL) va.va_mode = mode;
    int error = littlefs_setattr(&ap);
    return error;
}

int main(void)
{
    const int special[] = { VCHR, VBLK, VFIFO };
    for (unsigned i = 0; i < sizeof(special) / sizeof(special[0]); i++) {
        reset();
        assert(setattr(special[i], false, i == 0 ? 0 : 4096, 0, 0,
            VNOVAL) == 0);
        assert(truncate_calls == 0 && attr_writes == 0 && access_calls == 1);
        reset();
        assert(setattr(special[i], true, 0, 0, 0, VNOVAL) == 0);
        assert(truncate_calls == 0 && attr_writes == 0 && access_calls == 1);
    }

    reset();
    access_error = EACCES;
    assert(setattr(VCHR, true, 0, 0, 0, VNOVAL) == EACCES);
    assert(truncate_calls == 0 && attr_writes == 0);
    reset();
    assert(setattr(VFIFO, true, 0, 0, IMMUTABLE, VNOVAL) == EPERM);
    assert(truncate_calls == 0 && attr_writes == 0 && access_calls == 0);
    reset();
    assert(setattr(VBLK, false, 0, 0, APPEND, VNOVAL) == EPERM);
    assert(truncate_calls == 0 && attr_writes == 0 && access_calls == 0);

    reset();
    assert(setattr(VCHR, true, 0, 0, 0, 0600) == EROFS);
    assert(truncate_calls == 0 && attr_writes == 0);
    reset();
    assert(setattr(VCHR, false, 0, 0, 0, 0600) == 0);
    assert(truncate_calls == 0 && attr_writes == 1);

    reset();
    assert(setattr(VREG, false, 0, 32, 0, VNOVAL) == 0);
    assert(truncate_calls == 1 && truncated_to == 0);
    reset();
    assert(setattr(VDIR, false, 0, 0, 0, VNOVAL) == EISDIR);

    reset();
    assert(setattr(VREG, true, VNOVAL, 32, 0, 0600) == EROFS);
    assert(attr_writes == 0);
    reset();
    assert(setattr(VREG, false, VNOVAL, 32, IMMUTABLE, 0600) == EPERM);
    assert(attr_writes == 0);
    reset();
    auth_error = EACCES;
    assert(setattr(VREG, false, VNOVAL, 32, 0, 0600) == EACCES);
    assert(attr_writes == 0);
    return 0;
}
"""

out = pathlib.Path(args.out_dir)
out.mkdir(parents=True, exist_ok=True)
cfile = out / "t_setattr_special.c"
binary = out / "t_setattr_special"
cfile.write_text(fixture)
subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                *args.cc_flag, str(cfile), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
