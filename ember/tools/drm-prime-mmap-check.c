/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Native PRIME mmap hardware acceptance. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#define REQUIRE(x) do {                                                \
	if (!(x)) {                                                    \
		fprintf(stderr, "FAIL:%d %s errno=%d\n", __LINE__, #x,   \
		    errno);                                            \
		goto out;                                               \
	}                                                               \
} while (0)

static bool
unmap(void **p, size_t size)
{
	if (*p == MAP_FAILED)
		return true;
	if (munmap(*p, size) != 0)
		return false;
	*p = MAP_FAILED;
	return true;
}

static bool
close_fd(int *fd)
{
	int value;

	if (*fd < 0)
		return true;
	value = *fd;
	*fd = -1;
	return close(value) == 0;
}

static bool
close_bo(int fd, uint32_t *handle)
{
	struct drm_gem_close args = { .handle = *handle };

	if (*handle == 0)
		return true;
	if (ioctl(fd, DRM_IOCTL_GEM_CLOSE, &args) != 0)
		return false;
	*handle = 0;
	return true;
}

static bool
reject_map(int fd, size_t size, int prot, off_t off, int expected)
{
	void *p;
	int error;

	errno = 0;
	p = mmap(NULL, size, prot, MAP_SHARED, fd, off);
	error = errno;
	if (p != MAP_FAILED) {
		(void)munmap(p, size);
		fprintf(stderr, "unexpected mmap success: offset=%jd prot=%d\n",
		    (intmax_t)off, prot);
		return false;
	}
	if (error != expected)
		fprintf(stderr, "mmap errno=%d, expected=%d\n", error, expected);
	return error == expected;
}

static uint32_t
pattern(size_t index)
{
	return UINT32_C(0x51a7c300) ^ (uint32_t)index;
}

static bool
check_pattern(const volatile uint32_t *p, size_t words)
{
	for (size_t i = 0; i < words; i++) {
		if (p[i] != pattern(i))
			return false;
	}
	return true;
}

static bool
cycle(const char *node, bool readonly, size_t page)
{
	struct drm_mode_create_dumb bo = { .height = 128, .width = 128,
	    .bpp = 32 };
	struct drm_mode_map_dumb map = { 0 };
	struct drm_prime_handle prime = { .flags = DRM_CLOEXEC, .fd = -1 };
	volatile uint32_t *owned_words, *prime_words, *slice_words;
	void *owned = MAP_FAILED, *mapped = MAP_FAILED, *slice = MAP_FAILED;
	int owner = -1, foreign = -1, dma = -1, status;
	uint32_t handle = 0;
	size_t size = 0, words;
	pid_t child;
	bool ok = false;

	owner = open(node, O_RDWR | O_CLOEXEC);
	REQUIRE(owner >= 0);
	foreign = open(node, O_RDWR | O_CLOEXEC);
	REQUIRE(foreign >= 0);
	REQUIRE(ioctl(owner, DRM_IOCTL_MODE_CREATE_DUMB, &bo) == 0);
	handle = bo.handle;
	REQUIRE(handle != 0 && bo.size >= 3 * page && bo.size % page == 0);
	REQUIRE(bo.size <= SIZE_MAX - page && bo.size <= INT64_MAX - page);
	size = (size_t)bo.size;
	words = size / sizeof(uint32_t);
	map.handle = handle;
	REQUIRE(ioctl(owner, DRM_IOCTL_MODE_MAP_DUMB, &map) == 0);
	REQUIRE(map.offset <= INT64_MAX);
	owned = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, owner,
	    (off_t)map.offset);
	REQUIRE(owned != MAP_FAILED);
	owned_words = owned;
	for (size_t i = 0; i < words; i++)
		owned_words[i] = pattern(i);

	/* An offset is not authority to map another DRM file's object. */
	REQUIRE(reject_map(foreign, page, PROT_READ, (off_t)map.offset,
	    EACCES));
	prime.handle = handle;
	if (!readonly)
		prime.flags |= DRM_RDWR;
	REQUIRE(ioctl(owner, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) == 0);
	dma = prime.fd;
	REQUIRE(dma >= 0);
	REQUIRE((fcntl(dma, F_GETFL) & O_ACCMODE) ==
	    (readonly ? O_RDONLY : O_RDWR));
	REQUIRE((fcntl(dma, F_GETFD) & FD_CLOEXEC) != 0);
	mapped = mmap(NULL, size, readonly ? PROT_READ : PROT_READ | PROT_WRITE,
	    MAP_SHARED, dma, 0);
	REQUIRE(mapped != MAP_FAILED);
	prime_words = mapped;
	REQUIRE(check_pattern(prime_words, words));

	/* Test both a middle page and the final page of the exported object. */
	for (unsigned i = 0; i < 2; i++) {
		size_t offset = i == 0 ? page : size - page;
		size_t word = offset / sizeof(uint32_t);

		slice = mmap(NULL, page, readonly ? PROT_READ : PROT_READ | PROT_WRITE,
		    MAP_SHARED, dma, (off_t)offset);
		REQUIRE(slice != MAP_FAILED);
		slice_words = slice;
		REQUIRE(slice_words[0] == pattern(word));
		if (!readonly) {
			slice_words[0] = UINT32_C(0x1234abcd);
			REQUIRE(owned_words[word] == UINT32_C(0x1234abcd));
			REQUIRE(prime_words[word] == UINT32_C(0x1234abcd));
			owned_words[word] = pattern(word);
			REQUIRE(slice_words[0] == pattern(word));
		}
		REQUIRE(unmap(&slice, page));
	}
	REQUIRE(reject_map(dma, page, PROT_READ, (off_t)size, EINVAL));
	REQUIRE(reject_map(dma, size + page, PROT_READ, 0, EINVAL));
	REQUIRE(reject_map(dma, page, PROT_READ, -(off_t)page, EINVAL));
	REQUIRE(reject_map(dma, page, PROT_READ,
	    INT64_MAX - INT64_MAX % (off_t)page, EINVAL));
	REQUIRE(reject_map(dma, page, PROT_READ | PROT_EXEC, 0, EINVAL));
	errno = 0;
	REQUIRE(mprotect(mapped, page, PROT_READ | PROT_EXEC) == -1 &&
	    errno == EACCES);
	if (readonly) {
		REQUIRE(reject_map(dma, page, PROT_READ | PROT_WRITE, 0, EACCES));
		errno = 0;
		REQUIRE(mprotect(mapped, page, PROT_READ | PROT_WRITE) == -1 &&
		    errno == EACCES);
	}
	/* The temporary PRIME authorization must not leak to a foreign fd. */
	REQUIRE(reject_map(foreign, page, PROT_READ, (off_t)map.offset,
	    EACCES));
	REQUIRE(close_fd(&foreign));
	REQUIRE(unmap(&owned, size));
	REQUIRE(close_bo(owner, &handle));
	REQUIRE(close_fd(&owner));
	REQUIRE(close_fd(&dma));

	/* Only this PRIME mapping now owns a userland reference to the BO. */
	REQUIRE(check_pattern(prime_words, words));
	child = fork();
	REQUIRE(child >= 0);
	if (child == 0)
		_exit(check_pattern(prime_words, words) ? 0 : 1);
	REQUIRE(waitpid(child, &status, 0) == child);
	REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	if (!readonly) {
		prime_words[0] = UINT32_C(0xcafebeef);
		REQUIRE(prime_words[0] == UINT32_C(0xcafebeef));
	}
	ok = true;
out:
	if (!unmap(&slice, page)) ok = false;
	if (!unmap(&mapped, size)) ok = false;
	if (!unmap(&owned, size)) ok = false;
	if (!close_bo(owner, &handle)) ok = false;
	if (!close_fd(&dma)) ok = false;
	if (!close_fd(&foreign)) ok = false;
	if (!close_fd(&owner)) ok = false;
	return ok;
}

int
main(int argc, char **argv)
{
	long page;

	if (argc != 2) {
		fprintf(stderr, "usage: %s PRIMARY_DRM_NODE\n", argv[0]);
		return 2;
	}
	alarm(30);
	page = sysconf(_SC_PAGESIZE);
	if (page <= 0 || !cycle(argv[1], false, (size_t)page) ||
	    !cycle(argv[1], true, (size_t)page))
		return 1;
	puts("PASS: PRIME mmap patterns, offsets, lifetime, rights and foreign-fd isolation");
	return 0;
}
