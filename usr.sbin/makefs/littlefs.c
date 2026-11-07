/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "makefs.h"
#include "littlefs_format.h"
#include "lfs.h"

struct littlefs_options {
	uint32_t block_size;
	uint32_t read_size;
	uint32_t prog_size;
	uint32_t cache_size;
	uint32_t block_count;
	uint32_t lookahead_size;
	int block_count_set;
};

struct littlefs_io {
	int fd;
	off_t offset;
	uint32_t block_size;
};

static struct littlefs_options defaults = {
	.block_size = 4096,
	.read_size = 16,
	.prog_size = 16,
	.cache_size = 256,
	.block_count = 0,
	.lookahead_size = 32,
};
static option_t littlefs_options[] = {
	{ .name = "block_size", .value = &defaults.block_size,
	  .type = OPT_INT32, .minimum = 1, .maximum = UINT32_MAX,
	  .desc = "littlefs erase block size" },
	{ .name = "read_size", .value = &defaults.read_size,
	  .type = OPT_INT32, .minimum = 1, .maximum = UINT32_MAX,
	  .desc = "minimum read size" },
	{ .name = "prog_size", .value = &defaults.prog_size,
	  .type = OPT_INT32, .minimum = 1, .maximum = UINT32_MAX,
	  .desc = "minimum program size" },
	{ .name = "cache_size", .value = &defaults.cache_size,
	  .type = OPT_INT32, .minimum = 1, .maximum = UINT32_MAX,
	  .desc = "littlefs cache size" },
	{ .name = "block_count", .value = &defaults.block_count,
	  .type = OPT_INT32, .minimum = 2, .maximum = UINT32_MAX,
	  .desc = "number of erase blocks" },
	{ .name = "lookahead_size", .value = &defaults.lookahead_size,
	  .type = OPT_INT32, .minimum = 8, .maximum = UINT32_MAX,
	  .desc = "allocation lookahead size" },
	{ .name = NULL }
};

static int littlefs_read(const struct lfs_config *, lfs_block_t, lfs_off_t,
	void *, lfs_size_t);
static int littlefs_prog(const struct lfs_config *, lfs_block_t, lfs_off_t,
	const void *, lfs_size_t);
static int littlefs_erase(const struct lfs_config *, lfs_block_t);
static int littlefs_sync(const struct lfs_config *);
static void put_attr(lfs_t *, const char *, const fsnode *, uint64_t);
static uint64_t add_tree(lfs_t *, const char *, fsnode *, uint64_t *);
static void make_path(char *, size_t, const char *, const char *);
static void validate_tree(fsnode *);

void
littlefs_prep_opts(fsinfo_t *fi)
{
	fi->fs_specific = ecalloc(1, sizeof(defaults));
	memcpy(fi->fs_specific, &defaults, sizeof(defaults));
	fi->fs_options = littlefs_options;
}

int
littlefs_parse_opts(const char *option, fsinfo_t *fi)
{
	struct littlefs_options *o = fi->fs_specific;
	char buf[128];
	char *eq;
	uint32_t *field = NULL;
	long long max = UINT32_MAX, min = 1;

	if (o == NULL)
		return 0;
	if (strlen(option) >= sizeof(buf))
		return 0;
	memcpy(buf, option, strlen(option) + 1);
	eq = strchr(buf, '=');
	if (eq == NULL)
		return 0;
	*eq++ = '\0';
	if (strcmp(buf, "block_size") == 0)
		field = &o->block_size;
	else if (strcmp(buf, "read_size") == 0)
		field = &o->read_size;
	else if (strcmp(buf, "prog_size") == 0)
		field = &o->prog_size;
	else if (strcmp(buf, "cache_size") == 0)
		field = &o->cache_size;
	else if (strcmp(buf, "block_count") == 0) {
		field = &o->block_count;
		min = 2;
	} else if (strcmp(buf, "lookahead_size") == 0) {
		field = &o->lookahead_size;
		min = 8;
	} else
		return 0;
	*field = (uint32_t)strsuftoll(buf, eq, min, max);
	if (strcmp(buf, "block_count") == 0)
		o->block_count_set = 1;
	return 1;
}

void
littlefs_cleanup_opts(fsinfo_t *fi)
{
	free(fi->fs_specific);
	fi->fs_specific = NULL;
}

static void
put_attr(lfs_t *lfs, const char *path, const fsnode *node, uint64_t ino)
{
	struct littlefs_bsd_attr a;
	uint8_t bytes[LITTLEFS_BSD_SIZE];
	const struct stat *st = &node->inode->st;

	if ((uintmax_t)st->st_uid > UINT32_MAX ||
	    (uintmax_t)st->st_gid > UINT32_MAX)
		errx(EXIT_FAILURE, "%s: metadata value out of littlefs range",
		    path);
	memset(&a, 0, sizeof(a));
	a.ino = ino;
	a.mode = (uint32_t)st->st_mode;
	a.uid = (uint32_t)st->st_uid;
	a.gid = (uint32_t)st->st_gid;
	a.flags = (uint32_t)FSINODE_ST_FLAGS(*node->inode);
	/* makefs stores the target encoding; mtree's device=netbsd is portable. */
	a.rdev = (uint64_t)st->st_rdev;
	a.atime = st->st_atime;
	a.mtime = st->st_mtime;
	a.ctime = st->st_ctime;
#if HAVE_STRUCT_STAT_ST_MTIMENSEC
	a.atimensec = (uint32_t)st->st_atimensec;
	a.mtimensec = (uint32_t)st->st_mtimensec;
	a.ctimensec = (uint32_t)st->st_ctimensec;
#endif
	if (littlefs_bsd_encode(bytes, &a) != 0 ||
	    lfs_setattr(lfs, path, LITTLEFS_BSD_ATTR, bytes, sizeof(bytes)) < 0)
		errx(EXIT_FAILURE, "%s: cannot write BSD metadata", path);
}

static void
make_path(char *dst, size_t size, const char *parent, const char *name)
{
	int n;

	if (strcmp(parent, "/") == 0)
		n = snprintf(dst, size, "/%s", name);
	else
		n = snprintf(dst, size, "%s/%s", parent, name);
	if (n < 0 || (size_t)n >= size)
		errx(EXIT_FAILURE, "littlefs path too long");
}

static void
write_file(lfs_t *lfs, const char *path, const fsnode *node)
{
	lfs_file_t file;
	char src[PATH_MAX];
	char buf[16384];
	ssize_t nr;
	int fd, r;

	if (S_ISLNK(node->inode->st.st_mode)) {
		r = lfs_file_open(lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT |
		    LFS_O_TRUNC);
		if (r < 0)
			errx(EXIT_FAILURE, "%s: littlefs open failed (%d)", path, r);
		nr = (ssize_t)strlen(node->symlink);
		if (lfs_file_write(lfs, &file, node->symlink, (lfs_size_t)nr) != nr)
			errx(EXIT_FAILURE, "%s: littlefs symlink write failed", path);
		if (lfs_file_close(lfs, &file) < 0)
			errx(EXIT_FAILURE, "%s: littlefs close failed", path);
		return;
	}
	if (!S_ISREG(node->inode->st.st_mode)) {
		r = lfs_file_open(lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT |
		    LFS_O_TRUNC);
		if (r < 0 || lfs_file_close(lfs, &file) < 0)
			errx(EXIT_FAILURE, "%s: littlefs placeholder creation failed",
			    path);
		return;
	}
	if (snprintf(src, sizeof(src), "%s/%s/%s", node->root,
	    node->path, node->name) >= (int)sizeof(src))
		errx(EXIT_FAILURE, "%s: source path too long", node->name);
	fd = open(src, O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "open %s", src);
	r = lfs_file_open(lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT |
	    LFS_O_TRUNC);
	if (r < 0)
		errx(EXIT_FAILURE, "%s: littlefs open failed (%d)", path, r);
	while ((nr = read(fd, buf, sizeof(buf))) > 0) {
		ssize_t nw = 0;
		while (nw < nr) {
			lfs_ssize_t n = lfs_file_write(lfs, &file, buf + nw,
			    (lfs_size_t)(nr - nw));
			if (n <= 0)
				errx(EXIT_FAILURE, "%s: littlefs write failed (%d)",
				    path, n);
			nw += n;
		}
	}
	if (nr < 0)
		err(EXIT_FAILURE, "read %s", src);
	if (close(fd) < 0 || lfs_file_close(lfs, &file) < 0)
		errx(EXIT_FAILURE, "%s: close failed", path);
}

static uint64_t
add_tree(lfs_t *lfs, const char *parentpath, fsnode *level,
	uint64_t *nextino)
{
	fsnode *node;
	uint64_t maxino = LITTLEFS_ROOT_INO;

	if (level == NULL)
		return maxino;
	for (node = level->next; node != NULL; node = node->next) {
		char path[PATH_MAX];
		uint64_t ino;
		int r;

		if (strcmp(node->name, ".") == 0)
			continue;
		if (node->inode->nlink > 1)
			errx(EXIT_FAILURE,
		    "%s: hard links are not supported by littlefs images (EOPNOTSUPP)",
		    node->name);
		if (strcmp(parentpath, "/") == 0 &&
		    strcmp(node->name, LITTLEFS_STAGING_NAME) == 0)
			errx(EXIT_FAILURE, "%s is reserved by the littlefs mount",
			    LITTLEFS_STAGING_NAME);
		make_path(path, sizeof(path), parentpath, node->name);
		ino = (*nextino)++;
		if (S_ISDIR(node->inode->st.st_mode)) {
			r = lfs_mkdir(lfs, path);
			if (r < 0)
				errx(EXIT_FAILURE, "%s: littlefs mkdir failed (%d)",
				    path, r);
			put_attr(lfs, path, node, ino);
			if (ino > maxino)
				maxino = ino;
			{
				uint64_t childmax = add_tree(lfs, path, node->child,
				    nextino);
				if (childmax > maxino)
					maxino = childmax;
			}
		} else {
			write_file(lfs, path, node);
			put_attr(lfs, path, node, ino);
			if (ino > maxino)
				maxino = ino;
		}
	}
	return maxino;
}

static void
validate_tree(fsnode *level)
{
	fsnode *node;

	if (level == NULL)
		return;
	for (node = level->next; node != NULL; node = node->next) {
		if (node->inode->nlink > 1)
			errx(EXIT_FAILURE,
			    "%s: hard links are not supported by littlefs images (EOPNOTSUPP)",
			    node->name);
		if (S_ISDIR(node->inode->st.st_mode))
			validate_tree(node->child);
	}
}

static int
littlefs_read(const struct lfs_config *cfg, lfs_block_t block, lfs_off_t off,
    void *buffer, lfs_size_t size)
{
	struct littlefs_io *io = cfg->context;
	off_t at = io->offset + (off_t)block * io->block_size + off;
	return pread(io->fd, buffer, size, at) == (ssize_t)size ? 0 : LFS_ERR_IO;
}

static int
littlefs_prog(const struct lfs_config *cfg, lfs_block_t block, lfs_off_t off,
    const void *buffer, lfs_size_t size)
{
	struct littlefs_io *io = cfg->context;
	off_t at = io->offset + (off_t)block * io->block_size + off;
	return pwrite(io->fd, buffer, size, at) == (ssize_t)size ? 0 : LFS_ERR_IO;
}

static int
littlefs_erase(const struct lfs_config *cfg, lfs_block_t block)
{
	struct littlefs_io *io = cfg->context;
	uint8_t erased[4096];
	off_t at = io->offset + (off_t)block * io->block_size;
	uint32_t left = io->block_size;
	memset(erased, 0xff, sizeof(erased));

	while (left != 0) {
		size_t n = left < sizeof(erased) ? left : sizeof(erased);
		if (pwrite(io->fd, erased, n, at) != (ssize_t)n)
			return LFS_ERR_IO;
		at += n;
		left -= n;
	}
	return 0;
}

static int
littlefs_sync(const struct lfs_config *cfg)
{
	struct littlefs_io *io = cfg->context;
	return fsync(io->fd) == 0 ? 0 : LFS_ERR_IO;
}

void
littlefs_makefs(const char *image, const char *directory, fsnode *root,
    fsinfo_t *fi)
{
	struct littlefs_options *o = fi->fs_specific;
	struct littlefs_io io;
	struct lfs_config cfg;
	struct littlefs_bsd_attr rootattr;
	lfs_t lfs;
	uint8_t bytes[LITTLEFS_BSD_SIZE], nextbytes[8], erased[16384];
	uint64_t nextino = LITTLEFS_ROOT_INO + 1, maxino;
	off_t image_size, pos;
	int fd, r;
	(void)directory;

	if (o == NULL || o->block_size == 0 || o->read_size == 0 ||
	    o->prog_size == 0 || o->cache_size == 0 || o->lookahead_size == 0 ||
	    o->block_size % o->read_size != 0 ||
	    o->block_size % o->prog_size != 0 ||
	    o->block_size % o->cache_size != 0 ||
	    o->cache_size % o->read_size != 0 ||
	    o->cache_size % o->prog_size != 0 || o->lookahead_size % 8 != 0)
		errx(EXIT_FAILURE, "invalid littlefs geometry");
	if (fi->sparse)
		errx(EXIT_FAILURE, "littlefs images do not support -Z");
	if (fi->offset < 0 || fi->offset % o->block_size != 0)
		errx(EXIT_FAILURE, "littlefs image offset must align to block size");
	if (fi->maxsize > 0) {
		if (fi->maxsize % o->block_size != 0)
			errx(EXIT_FAILURE, "littlefs image size must be a block multiple");
		image_size = fi->maxsize;
		if (o->block_count_set &&
		    (uint64_t)image_size / o->block_size != o->block_count)
			errx(EXIT_FAILURE, "block_count does not match -s image size");
		o->block_count = (uint32_t)(image_size / o->block_size);
	} else if (!o->block_count_set) {
		errx(EXIT_FAILURE, "littlefs requires -s size or -o block_count=N");
	} else {
		uint64_t bytes = (uint64_t)o->block_count * o->block_size;
		if (bytes > UINT32_MAX)
			errx(EXIT_FAILURE, "littlefs image geometry is out of range");
		image_size = (off_t)bytes;
	}
	if (o->block_count < 2 || (uint64_t)image_size > UINT32_MAX ||
	    (uint64_t)o->block_count * o->block_size != (uint64_t)image_size)
		errx(EXIT_FAILURE, "littlefs image geometry is out of range");
	if (fi->minsize > image_size ||
	    (fi->maxsize > 0 && fi->maxsize < image_size))
		errx(EXIT_FAILURE, "littlefs image size violates -M or -m");
	if (fi->needswap)
		errx(EXIT_FAILURE, "littlefs on-disk byte order is always little-endian");
	if (root == NULL)
		errx(EXIT_FAILURE, "littlefs root directory is unavailable");
	if ((uintmax_t)root->inode->st.st_uid > UINT32_MAX ||
	    (uintmax_t)root->inode->st.st_gid > UINT32_MAX)
		errx(EXIT_FAILURE, "root ownership is out of littlefs metadata range");
	validate_tree(root);
	for (fsnode *n = root->next; n != NULL; n = n->next)
		if (strcmp(n->name, LITTLEFS_STAGING_NAME) == 0)
			errx(EXIT_FAILURE, "%s is reserved by the littlefs mount",
			    LITTLEFS_STAGING_NAME);

	fd = open(image, O_RDWR | O_CREAT | (fi->offset == 0 ? O_TRUNC : 0), 0666);
	if (fd < 0)
		err(EXIT_FAILURE, "open %s", image);
	if (fi->offset == 0 && ftruncate(fd, image_size) < 0)
		err(EXIT_FAILURE, "size %s", image);
	memset(erased, 0xff, sizeof(erased));
	for (pos = 0; pos < image_size; pos += sizeof(erased)) {
		size_t n = image_size - pos < (off_t)sizeof(erased) ?
		    (size_t)(image_size - pos) : sizeof(erased);
		if (pwrite(fd, erased, n, fi->offset + pos) != (ssize_t)n)
			err(EXIT_FAILURE, "initialize %s", image);
	}
	memset(&io, 0, sizeof(io));
	io.fd = fd;
	io.offset = fi->offset;
	io.block_size = o->block_size;
	memset(&cfg, 0, sizeof(cfg));
	cfg.context = &io;
	cfg.read = littlefs_read;
	cfg.prog = littlefs_prog;
	cfg.erase = littlefs_erase;
	cfg.sync = littlefs_sync;
	cfg.read_size = o->read_size;
	cfg.prog_size = o->prog_size;
	cfg.block_size = o->block_size;
	cfg.block_count = o->block_count;
	cfg.block_cycles = 500;
	cfg.cache_size = o->cache_size;
	cfg.lookahead_size = o->lookahead_size;
	memset(&lfs, 0, sizeof(lfs));
	r = lfs_format(&lfs, &cfg);
	if (r < 0)
		errx(EXIT_FAILURE, "littlefs format failed (%d)", r);
	r = lfs_mount(&lfs, &cfg);
	if (r < 0)
		errx(EXIT_FAILURE, "littlefs mount after format failed (%d)", r);
	r = lfs_mkdir(&lfs, "/" LITTLEFS_STAGING_NAME);
	if (r < 0)
		errx(EXIT_FAILURE, "cannot create internal staging directory (%d)", r);
	memset(&rootattr, 0, sizeof(rootattr));
	rootattr.ino = LITTLEFS_ROOT_INO;
	rootattr.mode = (uint32_t)root->inode->st.st_mode;
	rootattr.uid = (uint32_t)root->inode->st.st_uid;
	rootattr.gid = (uint32_t)root->inode->st.st_gid;
	rootattr.flags = (uint32_t)FSINODE_ST_FLAGS(*root->inode);
	rootattr.atime = root->inode->st.st_atime;
	rootattr.mtime = root->inode->st.st_mtime;
	rootattr.ctime = root->inode->st.st_ctime;
#if HAVE_STRUCT_STAT_ST_MTIMENSEC
	rootattr.atimensec = (uint32_t)root->inode->st.st_atimensec;
	rootattr.mtimensec = (uint32_t)root->inode->st.st_mtimensec;
	rootattr.ctimensec = (uint32_t)root->inode->st.st_ctimensec;
#endif
	if (littlefs_bsd_encode(bytes, &rootattr) != 0 ||
	    lfs_setattr(&lfs, "/", LITTLEFS_BSD_ATTR, bytes, sizeof(bytes)) < 0)
		errx(EXIT_FAILURE, "cannot write root BSD metadata");
	maxino = add_tree(&lfs, "/", root, &nextino);
	(void)maxino;
	littlefs_le64enc(nextbytes, nextino);
	if (lfs_setattr(&lfs, "/", LITTLEFS_NEXTINO_ATTR, nextbytes,
	    sizeof(nextbytes)) < 0 || lfs_unmount(&lfs) < 0 || fsync(fd) < 0)
		errx(EXIT_FAILURE, "cannot finish littlefs image");
	if (close(fd) < 0)
		err(EXIT_FAILURE, "close %s", image);
}
