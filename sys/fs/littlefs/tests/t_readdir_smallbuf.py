#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the production littlefs_readdir function with a bounded uio."""

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
match = re.search(r"static int\nlittlefs_readdir\(void \*v\)\n\{", source)
assert match, "missing production littlefs_readdir"
depth, end = 1, match.end()
while depth:
    if source[end] == "{":
        depth += 1
    elif source[end] == "}":
        depth -= 1
    end += 1
function = source[match.start():end]
function = function.replace("malloc(", "k_malloc(").replace(
    "free(cookies, M_TEMP)", "k_free(cookies)")

fixture = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int64_t off_t;

#define VDIR 1
#define M_TEMP 0
#define M_WAITOK 0
#define GOP_UPDATE_ACCESSED 0
#define LITTLEFS_ROOT_INO 1
#define LITTLEFS_STAGING_NAME ".littlefs-tmp"
#define _DIRENT_MINSIZE(de) 16
#define _DIRENT_RECLEN(de, n) (16 + (n) + 1)
#define IFTODT(mode) (mode)
#define VTOLITTLEFS(vp) ((vp)->node)
#define PNBUF_GET() ((char *)calloc(1, 256))
#define PNBUF_PUT(p) free(p)
#define mutex_enter(p) ((void)(p))
#define mutex_exit(p) ((void)(p))
static void *k_malloc(size_t n, int type, int flags)
{ (void)type; (void)flags; return calloc(1, n); }
static void k_free(void *p) { free(p); }

struct vnode { int v_type; struct littlefs_node *node; };
struct lfs { int index; };
struct lfs_dir { int unused; };
struct lfs_info { char name[64]; };
struct littlefs_bsd_attr { unsigned long ino; unsigned mode; };
struct littlefs_entry { bool removed; };
struct littlefs_mount { int lock; struct lfs lfs; };
struct littlefs_node {
    struct littlefs_mount *mount;
    struct littlefs_entry *entry;
    struct littlefs_bsd_attr attr;
    struct lfs_dir dir;
};
struct uio { off_t uio_offset; size_t uio_resid; };
struct dirent { unsigned long d_fileno; unsigned short d_namlen, d_reclen;
    unsigned char d_type; char d_name[64]; };
struct vop_readdir_args { struct vnode *a_vp; struct uio *a_uio;
    bool *a_eofflag; int *a_ncookies; off_t **a_cookies; };

static const char **names;
static size_t name_count;
static int lfs_dir_seek(struct lfs *l, struct lfs_dir *d, off_t off)
{ (void)d; l->index = (int)off; return 0; }
static int lfs_dir_read(struct lfs *l, struct lfs_dir *d, struct lfs_info *i)
{
    (void)d;
    if ((size_t)l->index >= name_count) return 0;
    strncpy(i->name, names[l->index], sizeof(i->name) - 1);
    l->index++;
    return 1;
}
static off_t lfs_dir_tell(struct lfs *l, struct lfs_dir *d)
{ (void)d; return l->index; }
static int littlefs_childpath(struct littlefs_node *n, const char *s,
    size_t z, char *p) { (void)n; (void)z; strcpy(p, s); return 0; }
static int littlefs_readattr(struct littlefs_mount *m, const char *p,
    struct littlefs_bsd_attr *a, struct lfs_info *i)
{ (void)m; (void)p; (void)i; a->ino = 2; a->mode = 8; return 0; }
static int littlefs_error(int e) { return e; }
static int uiomove(void *p, size_t n, struct uio *u)
{ (void)p; u->uio_resid -= n; return 0; }
static void littlefs_touch(struct littlefs_node *n, int flags)
{ (void)n; (void)flags; }

""" + function + r"""
static int run(const char **entry_names, size_t n, size_t resid,
    off_t start, off_t *end, bool *eof)
{
    struct littlefs_mount lm = {0};
    struct littlefs_entry ent = {0};
    struct littlefs_node node = { .mount = &lm, .entry = &ent,
        .attr = { .ino = LITTLEFS_ROOT_INO } };
    struct vnode vp = { .v_type = VDIR, .node = &node };
    struct uio uio = { .uio_offset = start, .uio_resid = resid };
    struct vop_readdir_args ap = { .a_vp = &vp, .a_uio = &uio,
        .a_eofflag = eof };
    names = entry_names; name_count = n;
    int error = littlefs_readdir(&ap);
    *end = uio.uio_offset;
    return error;
}

int main(void)
{
    off_t end;
    bool eof = false;
    const char *one_long[] = { "a-name-too-long-for-the-buffer" };
    assert(run(one_long, 1, 20, 0, &end, &eof) == EINVAL);
    assert(end == 0 && !eof);
    assert(run(one_long, 1, 8, 0, &end, &eof) == EINVAL);

    const char *visible_then_long[] = { "a", "a-name-too-long-for-the-buffer" };
    eof = false;
    assert(run(visible_then_long, 2, 20, 0, &end, &eof) == 0);
    assert(end == 1 && !eof);

    const char *hidden_then_long[] = { LITTLEFS_STAGING_NAME,
        "a-name-too-long-for-the-buffer" };
    eof = false;
    assert(run(hidden_then_long, 2, 20, 0, &end, &eof) == EINVAL);
    assert(end == 1 && !eof);
    return 0;
}
"""

out = pathlib.Path(args.out_dir)
out.mkdir(parents=True, exist_ok=True)
cfile = out / "t_readdir_smallbuf.c"
binary = out / "t_readdir_smallbuf"
cfile.write_text(fixture)
subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                *args.cc_flag,
                str(cfile), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
