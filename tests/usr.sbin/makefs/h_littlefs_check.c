/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lfs.h"
#include "littlefs_format.h"

struct image {
	int fd;
	uint32_t block_size;
};

static int
rd(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, void *p,
    lfs_size_t n)
{
	struct image *i = c->context;
	return pread(i->fd, p, n, (off_t)b * i->block_size + o) ==
	    (ssize_t)n ? 0 : LFS_ERR_IO;
}

static int
prog(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, const void *p,
    lfs_size_t n)
{
	struct image *i = c->context;
	return pwrite(i->fd, p, n, (off_t)b * i->block_size + o) ==
	    (ssize_t)n ? 0 : LFS_ERR_IO;
}

static int
erase(const struct lfs_config *c, lfs_block_t b)
{
	struct image *i = c->context;
	uint8_t buf[4096];
	off_t off = (off_t)b * i->block_size;
	uint32_t left = i->block_size;
	memset(buf, 0xff, sizeof(buf));
	while (left != 0) {
		size_t n = left < sizeof(buf) ? left : sizeof(buf);
		if (pwrite(i->fd, buf, n, off) != (ssize_t)n)
			return LFS_ERR_IO;
		left -= n;
		off += n;
	}
	return 0;
}

static int
sync_image(const struct lfs_config *c)
{
	struct image *i = c->context;
	return fsync(i->fd) == 0 ? 0 : LFS_ERR_IO;
}

static uint64_t
check_attr(lfs_t *lfs, const char *path, mode_t type)
{
	uint8_t b[LITTLEFS_BSD_SIZE];
	struct littlefs_bsd_attr a;
	int n = lfs_getattr(lfs, path, LITTLEFS_BSD_ATTR, b, sizeof(b));
	if (n != sizeof(b) || littlefs_bsd_decode(&a, b, sizeof(b)) != 0 ||
	    (a.mode & S_IFMT) != type)
		errx(EXIT_FAILURE, "%s BSD metadata mismatch", path);
	return a.ino;
}

static void
check_file(lfs_t *lfs, const char *path, const char *expected)
{
	lfs_file_t f;
	char b[128];
	lfs_ssize_t n;
	if (lfs_file_open(lfs, &f, path, LFS_O_RDONLY) < 0)
		errx(EXIT_FAILURE, "%s missing", path);
	n = lfs_file_read(lfs, &f, b, sizeof(b) - 1);
	if (n < 0)
		errx(EXIT_FAILURE, "%s read failed", path);
	b[n] = '\0';
	if (strcmp(b, expected) != 0)
		errx(EXIT_FAILURE, "%s contents mismatch", path);
	if (lfs_file_close(lfs, &f) < 0)
		errx(EXIT_FAILURE, "%s close failed", path);
}

static uint64_t
check_device(lfs_t *lfs)
{
	static const uint8_t target_rdev_le[8] = {
		0xde, 0xc7, 0xc0, 0xab, 0x00, 0x00, 0x00, 0x00
	};
	uint8_t b[LITTLEFS_BSD_SIZE];
	struct littlefs_bsd_attr a;
	int n = lfs_getattr(lfs, "/device", LITTLEFS_BSD_ATTR, b, sizeof(b));

	if (n != sizeof(b) || littlefs_bsd_decode(&a, b, sizeof(b)) != 0 ||
	    a.mode != (S_IFCHR | 0600) || a.uid != 42 ||
	    a.gid != 43 || memcmp(b + 32, target_rdev_le,
	    sizeof(target_rdev_le)) != 0)
		errx(EXIT_FAILURE, "target device metadata encoding mismatch");
	return a.ino;
}

int
main(int argc, char **argv)
{
	struct image image;
	struct stat st;
	struct lfs_config cfg;
	lfs_t lfs;
	uint8_t next[8];
	uint64_t inos[5], maxino = LITTLEFS_ROOT_INO;
	int n;

	if (argc != 2)
		errx(EXIT_FAILURE, "usage: h_littlefs_check image");
	image.fd = open(argv[1], O_RDONLY);
	if (image.fd < 0 || fstat(image.fd, &st) < 0)
		err(EXIT_FAILURE, "%s", argv[1]);
	image.block_size = 4096;
	memset(&cfg, 0, sizeof(cfg));
	cfg.context = &image;
	cfg.read = rd;
	cfg.prog = prog;
	cfg.erase = erase;
	cfg.sync = sync_image;
	cfg.read_size = 16;
	cfg.prog_size = 16;
	cfg.block_size = image.block_size;
	cfg.block_count = (lfs_size_t)(st.st_size / image.block_size);
	cfg.block_cycles = 500;
	cfg.cache_size = 256;
	cfg.lookahead_size = 32;
	memset(&lfs, 0, sizeof(lfs));
	if (lfs_mount(&lfs, &cfg) < 0)
		errx(EXIT_FAILURE, "mount failed");
	if (check_attr(&lfs, "/", S_IFDIR) != LITTLEFS_ROOT_INO)
		errx(EXIT_FAILURE, "root inode mismatch");
	inos[0] = check_device(&lfs);
	inos[1] = check_attr(&lfs, "/hello", S_IFREG);
	inos[2] = check_attr(&lfs, "/link", S_IFLNK);
	inos[3] = check_attr(&lfs, "/sub", S_IFDIR);
	inos[4] = check_attr(&lfs, "/sub/file", S_IFREG);
	for (size_t i = 0; i < sizeof(inos) / sizeof(inos[0]); i++) {
		if (inos[i] <= LITTLEFS_ROOT_INO)
			errx(EXIT_FAILURE, "non-root inode is out of range");
		for (size_t j = 0; j < i; j++)
			if (inos[i] == inos[j])
				errx(EXIT_FAILURE, "duplicate inode number");
		if (inos[i] > maxino)
			maxino = inos[i];
	}
	check_file(&lfs, "/hello", "hello littlefs\n");
	check_file(&lfs, "/link", "hello");
	check_file(&lfs, "/sub/file", "nested\n");
	n = lfs_getattr(&lfs, "/", LITTLEFS_NEXTINO_ATTR, next,
	    sizeof(next));
	if (n != sizeof(next) || littlefs_le64dec(next) <= maxino)
		errx(EXIT_FAILURE, "root next inode mismatch");
	if (lfs_stat(&lfs, "/" LITTLEFS_STAGING_NAME, &(struct lfs_info){0}) < 0)
		errx(EXIT_FAILURE, "mount staging directory missing");
	if (lfs_unmount(&lfs) < 0)
		errx(EXIT_FAILURE, "unmount failed");
	close(image.fd);
	return EXIT_SUCCESS;
}
