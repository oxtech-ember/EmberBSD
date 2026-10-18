/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD bcm2712-v3d-prime-check.c, adapted for native PowerVR.
 * AI-assisted. Exercise same-device sharing and CPU mapping lifetime.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pvr_drm.h"

#define BO_SIZE (1024 * 1024)
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "FAIL:%d %s errno=%d\n", __LINE__, #x, \
			    errno);                                            \
			exit(1);                                               \
		}                                                              \
	} while (0)
static void
gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close_arg = {.handle = handle};
	CHECK(ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_arg) == 0);
}
static void
send_fd(int socket, int fd)
{
	char byte = 'V';
	struct iovec iov = {&byte, 1};
	union {
		struct cmsghdr align;
		char data[CMSG_SPACE(sizeof(int))];
	} control;
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	memset(&control, 0, sizeof(control));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control.data;
	msg.msg_controllen = sizeof(control.data);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
	memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
	CHECK(sendmsg(socket, &msg, 0) == 1);
}
static int
receive_fd(int socket)
{
	char byte = 0;
	struct iovec iov = {&byte, 1};
	int fd;
	union {
		struct cmsghdr align;
		char data[CMSG_SPACE(sizeof(int))];
	} control;
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	memset(&control, 0, sizeof(control));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control.data;
	msg.msg_controllen = sizeof(control.data);
	CHECK(recvmsg(socket, &msg, 0) == 1 && byte == 'V');
	CHECK((msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) == 0);
	cmsg = CMSG_FIRSTHDR(&msg);
	CHECK(cmsg && cmsg->cmsg_level == SOL_SOCKET &&
	      cmsg->cmsg_type == SCM_RIGHTS &&
	      cmsg->cmsg_len == CMSG_LEN(sizeof(fd)));
	memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
	return fd;
}
static void
child(const char *node, int socket, unsigned cycle)
{
	int dma = receive_fd(socket), fd = open(node, O_RDWR | O_CLOEXEC);
	struct drm_prime_handle imported = {.fd = dma}, again = {.fd = dma};
	unsigned char *map;
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	CHECK(fd >= 0);
	CHECK(ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &imported) == 0);
	CHECK(ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &again) == 0 &&
	      again.handle == imported.handle);
	CHECK(lseek(dma, 0, SEEK_END) == BO_SIZE);
	errno = 0;
	CHECK(mmap(NULL, page, PROT_READ, MAP_SHARED, dma, BO_SIZE) ==
		  MAP_FAILED &&
	      errno == EINVAL);
	errno = 0;
	CHECK(mmap(NULL, page, PROT_READ, MAP_PRIVATE, dma, 0) == MAP_FAILED &&
	      errno == EINVAL);
	errno = 0;
	CHECK(mmap(NULL, page, PROT_READ | PROT_EXEC, MAP_SHARED, dma, 0) ==
		  MAP_FAILED &&
	      errno == EINVAL);
	map = mmap(
	    NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, dma, (off_t)page);
	CHECK(map != MAP_FAILED);
	gem_close(fd, imported.handle);
	CHECK(close(dma) == 0 && close(fd) == 0);
	for (size_t i = 0; i < page; i++)
		CHECK(map[i] == (unsigned char)(cycle + i));
	/* The remaining mmap must retain the object after every handle/fd
	 * closes. */
	map[0] = (unsigned char)(cycle ^ 0xa5);
	CHECK(munmap(map, page) == 0 && close(socket) == 0);
	_exit(0);
}
static void
access_controls(int fd, size_t page)
{
	struct drm_pvr_ioctl_create_bo_args create = {
	    .size = BO_SIZE, .flags = DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS};
	struct drm_prime_handle prime = {.flags = DRM_CLOEXEC, .fd = -1};
	unsigned char *map;
	CHECK(ioctl(fd, DRM_IOCTL_PVR_CREATE_BO, &create) == 0);
	prime.handle = create.handle;
	CHECK(ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) == 0);
	CHECK((fcntl(prime.fd, F_GETFL) & O_ACCMODE) == O_RDONLY);
	errno = 0;
	CHECK(mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, prime.fd,
		  0) == MAP_FAILED &&
	      errno == EACCES);
	map = mmap(NULL, page, PROT_READ, MAP_SHARED, prime.fd, 0);
	CHECK(map != MAP_FAILED);
	errno = 0;
	CHECK(mprotect(map, page, PROT_READ | PROT_WRITE) == -1 &&
	      errno == EACCES);
	CHECK(munmap(map, page) == 0 && close(prime.fd) == 0);
	gem_close(fd, create.handle);
	create = (struct drm_pvr_ioctl_create_bo_args){.size = BO_SIZE};
	CHECK(ioctl(fd, DRM_IOCTL_PVR_CREATE_BO, &create) == 0);
	prime = (struct drm_prime_handle){
	    .handle = create.handle, .flags = DRM_RDWR, .fd = -1};
	CHECK(ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) == 0);
	errno = 0;
	CHECK(mmap(NULL, page, PROT_READ, MAP_SHARED, prime.fd, 0) ==
		  MAP_FAILED &&
	      errno == EACCES);
	CHECK(close(prime.fd) == 0);
	gem_close(fd, create.handle);
	create = (struct drm_pvr_ioctl_create_bo_args){
	    .size = BO_SIZE, .flags = DRM_PVR_BO_PM_FW_PROTECT};
	CHECK(ioctl(fd, DRM_IOCTL_PVR_CREATE_BO, &create) == 0);
	prime = (struct drm_prime_handle){
	    .handle = create.handle, .flags = DRM_RDWR, .fd = -1};
	errno = 0;
	CHECK(ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) == -1 &&
	      errno == EACCES);
	CHECK(prime.fd == -1);
	gem_close(fd, create.handle);
	puts("PASS: read-only fd rights, mprotect bound, CPU flag and firmware "
	     "protection");
}

int
main(int argc, char **argv)
{
	int fd, sockets[2], status;
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	CHECK(argc == 2 && page == 4096);
	setvbuf(stdout, NULL, _IOLBF, 0);
	alarm(120);
	fd = open(argv[1], O_RDWR | O_CLOEXEC);
	CHECK(fd >= 0);
	access_controls(fd, page);
	for (unsigned cycle = 0; cycle < 80; cycle++) {
		struct drm_pvr_ioctl_create_bo_args create = {.size = BO_SIZE,
		    .flags = DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS};
		struct drm_pvr_ioctl_get_bo_mmap_offset_args offset = {0};
		struct drm_prime_handle prime = {
		    .flags = DRM_CLOEXEC | DRM_RDWR, .fd = -1};
		struct drm_prime_handle invalid = {.fd = -1};
		unsigned char *map;
		pid_t pid;
		CHECK(ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &invalid) == -1);
		CHECK(ioctl(fd, DRM_IOCTL_PVR_CREATE_BO, &create) == 0);
		offset.handle = create.handle;
		CHECK(
		    ioctl(fd, DRM_IOCTL_PVR_GET_BO_MMAP_OFFSET, &offset) == 0);
		map = mmap(NULL, BO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		    fd, (off_t)offset.offset);
		CHECK(map != MAP_FAILED);
		for (size_t i = 0; i < page; i++)
			map[page + i] = (unsigned char)(cycle + i);
		prime.handle = create.handle;
		CHECK(ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) == 0);
		CHECK((fcntl(prime.fd, F_GETFD) & FD_CLOEXEC) != 0);
		CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
		pid = fork();
		CHECK(pid >= 0);
		if (pid == 0) {
			alarm(30);
			CHECK(close(fd) == 0 && close(prime.fd) == 0 &&
			      close(sockets[0]) == 0);
			CHECK(munmap(map, BO_SIZE) == 0);
			child(argv[1], sockets[1], cycle);
		}
		CHECK(close(sockets[1]) == 0);
		send_fd(sockets[0], prime.fd);
		gem_close(fd, create.handle);
		CHECK(close(prime.fd) == 0 && close(sockets[0]) == 0);
		CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
		      WEXITSTATUS(status) == 0);
		CHECK(map[page] == (unsigned char)(cycle ^ 0xa5));
		CHECK(munmap(map, BO_SIZE) == 0);
		if (cycle % 16 == 15)
			printf("PASS: %u cross-process PRIME lifecycles\n",
			    cycle + 1);
	}
	CHECK(close(fd) == 0);
	puts("PASS: 80 MiB cumulative allocation, SCM_RIGHTS, duplicate "
	     "import, mmap bounds and last-reference lifetime");
	return 0;
}
