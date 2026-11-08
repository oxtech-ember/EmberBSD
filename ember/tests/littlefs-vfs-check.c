/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/* Explicit device-side check; creates and removes one private directory. */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FILESIZE (16 * 1024)

static void
stage(const char *name)
{
	printf("littlefs-vfs-check: %s\n", name);
	fflush(stdout);
}

static void
check(int condition, const char *what)
{
	if (!condition)
		errx(1, "%s", what);
}

static void
write_all(int fd, const void *data, size_t len)
{
	const char *p = data;
	ssize_t n;

	while (len != 0) {
		n = write(fd, p, len);
		if (n <= 0)
			err(1, "write");
		p += n;
		len -= n;
	}
}

static int
make_file(const char *name, unsigned char byte)
{
	unsigned char data[512];
	int fd;

	memset(data, byte, sizeof(data));
	fd = open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd < 0)
		err(1, "open %s", name);
	for (size_t i = 0; i < FILESIZE / sizeof(data); i++)
		write_all(fd, data, sizeof(data));
	if (fsync(fd) == -1)
		err(1, "fsync %s", name);
	return fd;
}

static void
check_bytes(int fd, off_t offset, size_t len, unsigned char value)
{
	unsigned char data[512];
	ssize_t n;

	while (len != 0) {
		size_t amount = len < sizeof(data) ? len : sizeof(data);
		n = pread(fd, data, amount, offset);
		check(n == (ssize_t)amount, "short pread");
		for (ssize_t i = 0; i < n; i++)
			check(data[i] == value, "file contents mismatch");
		offset += n;
		len -= n;
	}
}

int
main(int argc, char **argv)
{
	struct statvfs sv;
	struct stat st;
	char path[PATH_MAX], linkbuf[32];
	unsigned char *map;
	int oldfd, newfd, rofd, cwd;
	long pagesize;

	if (argc != 2)
		errx(1, "usage: littlefs-vfs-check writable-littlefs-directory");
	stage("query page size");
	pagesize = sysconf(_SC_PAGESIZE);
	stage("statvfs");
	if (statvfs(argv[1], &sv) == -1)
		err(1, "statvfs");
	check(strcmp(sv.f_fstypename, "littlefs") == 0,
	    "test directory must be on littlefs");
	check(pagesize > 0 && pagesize <= FILESIZE, "unsupported page size");
	check(snprintf(path, sizeof(path), "%s/.vfs-check.XXXXXX", argv[1]) <
	    (int)sizeof(path), "test path too long");
	stage("open current directory and create private directory");
	cwd = open(".", O_RDONLY);
	if (cwd == -1 || mkdtemp(path) == NULL || chdir(path) == -1)
		err(1, "create test directory");

	stage("create, write, and sync initial file");
	oldfd = make_file("old", 0x31);
	stage("mmap file");
	map = mmap(NULL, FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, oldfd, 0);
	if (map == MAP_FAILED)
		err(1, "mmap");
	for (size_t i = 0; i < FILESIZE; i++)
		check(map[i] == 0x31, "mmap read mismatch");
	stage("modify shared mapping and msync");
	memset(map + pagesize, 0x52, pagesize);
	if (msync(map, FILESIZE, MS_SYNC) == -1)
		err(1, "msync");
	check_bytes(oldfd, pagesize, pagesize, 0x52);
	stage("munmap and metadata operations");
	if (munmap(map, FILESIZE) == -1 || fchmod(oldfd, 0640) == -1 ||
	    fstat(oldfd, &st) == -1)
		err(1, "metadata");
	check((st.st_mode & 0777) == 0640, "chmod mismatch");
	stage("symlink and readlink");
	if (symlink("old", "link") == -1)
		err(1, "symlink");
	check(readlink("link", linkbuf, sizeof(linkbuf)) == 3 &&
	    memcmp(linkbuf, "old", 3) == 0, "symlink mismatch");
	stage("truncate and fsync");
	if (ftruncate(oldfd, 513) == -1 || ftruncate(oldfd, FILESIZE) == -1 ||
	    fsync(oldfd) == -1)
		err(1, "truncate");
	check_bytes(oldfd, 0, 513, 0x31);
	check_bytes(oldfd, 513, FILESIZE - 513, 0);
	stage("close old file and reopen read-only");
	if (close(oldfd) == -1)
		err(1, "close old");
	rofd = open("old", O_RDONLY);
	if (rofd == -1)
		err(1, "open read-only");
	stage("create replacement file");
	newfd = make_file("new", 0x73);
	stage("rename over open old file and unlink");
	if (rename("new", "old") == -1 || unlink("old") == -1)
		err(1, "rename/unlink");
	check_bytes(rofd, 0, 513, 0x31);
	check_bytes(rofd, 513, FILESIZE - 513, 0);
	check_bytes(newfd, 0, FILESIZE, 0x73);
	if (fstat(rofd, &st) == -1)
		err(1, "fstat removed file");
	check(st.st_nlink == 0, "removed file link count");
	stage("close handles and remove private directory");
	if (close(rofd) == -1 || close(newfd) == -1 || unlink("link") == -1 ||
	    fchdir(cwd) == -1 || close(cwd) == -1 || rmdir(path) == -1)
		err(1, "cleanup");
	puts("littlefs: mmap, sync, metadata, symlink, truncate, rename and unlink passed");
	return 0;
}
