/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Exercise the production capture queue against a controlled video(4) API.
 */
#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "mock/sys/videoio.h"

#define CAPTURE_BUFFERS 4
#define CAPTURE_BYTES 64

static uint8_t capture_memory[CAPTURE_BUFFERS][CAPTURE_BYTES];
static struct timespec capture_now;
static bool capture_timeout;
static bool capture_dq_error;
static bool capture_streamoff_error;
static unsigned int capture_poll_calls;
static unsigned int capture_qbuf_calls;
static unsigned int capture_dqbuf_calls;
static unsigned int capture_streamoff_calls;
static unsigned int capture_unmap_calls;

static int capture_ioctl(int, unsigned long, void *);
static int capture_poll(struct pollfd *, nfds_t, int);
static void *capture_mmap(void *, size_t, int, int, int, off_t);
static int capture_munmap(void *, size_t);
static int capture_clock_gettime(clockid_t, struct timespec *);

#define ioctl capture_ioctl
#define poll capture_poll
#define mmap capture_mmap
#define munmap capture_munmap
#define clock_gettime capture_clock_gettime
#include "../video_capture_bsd.c"
#undef ioctl
#undef poll
#undef mmap
#undef munmap
#undef clock_gettime

static int
capture_ioctl(int fd, unsigned long request, void *data)
{
	(void)fd;
	switch (request) {
	case VIDIOC_QUERYCAP:
		((struct v4l2_capability *)data)->capabilities =
		    V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
		return 0;
	case VIDIOC_G_FMT: {
		struct v4l2_format *format = data;
		format->fmt.pix.width = 8;
		format->fmt.pix.height = 8;
		format->fmt.pix.pixelformat = 0x59565955;
		format->fmt.pix.sizeimage = CAPTURE_BYTES;
		return 0;
	}
	case VIDIOC_REQBUFS:
		assert(((struct v4l2_requestbuffers *)data)->count == CAPTURE_BUFFERS);
		return 0;
	case VIDIOC_QUERYBUF: {
		struct v4l2_buffer *buffer = data;
		buffer->length = CAPTURE_BYTES;
		buffer->m.offset = buffer->index * CAPTURE_BYTES;
		return 0;
	}
	case VIDIOC_QBUF:
		capture_qbuf_calls++;
		return 0;
	case VIDIOC_DQBUF: {
		struct v4l2_buffer *buffer = data;
		if (capture_dq_error) {
			errno = EIO;
			return -1;
		}
		buffer->index = capture_dqbuf_calls++ % CAPTURE_BUFFERS;
		buffer->bytesused = CAPTURE_BYTES;
		return 0;
	}
	case VIDIOC_STREAMON:
		return 0;
	case VIDIOC_STREAMOFF:
		capture_streamoff_calls++;
		if (capture_streamoff_error) {
			errno = EIO;
			return -1;
		}
		return 0;
	default:
		assert(0);
		return -1;
	}
}

static int
capture_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	assert(nfds == 1 && timeout > 0);
	capture_poll_calls++;
	if (capture_timeout) {
		capture_now.tv_sec += timeout / 1000;
		capture_now.tv_nsec += (long)(timeout % 1000) * 1000000;
		if (capture_now.tv_nsec >= 1000000000) {
			capture_now.tv_sec++;
			capture_now.tv_nsec -= 1000000000;
		}
		return 0;
	}
	fds[0].revents = POLLIN;
	return 1;
}

static void *
capture_mmap(void *address, size_t length, int prot, int flags, int fd,
    off_t offset)
{
	(void)address;
	(void)prot;
	(void)flags;
	(void)fd;
	assert(length == CAPTURE_BYTES && offset >= 0 &&
	    offset % CAPTURE_BYTES == 0 &&
	    (size_t)offset / CAPTURE_BYTES < CAPTURE_BUFFERS);
	return capture_memory[(size_t)offset / CAPTURE_BYTES];
}

static int
capture_munmap(void *address, size_t length)
{
	uintptr_t value = (uintptr_t)address;
	uintptr_t first = (uintptr_t)&capture_memory[0][0];
	uintptr_t last = (uintptr_t)&capture_memory[CAPTURE_BUFFERS - 1][0];

	assert(length == CAPTURE_BYTES);
	assert(value >= first && value <= last);
	capture_unmap_calls++;
	return 0;
}

static int
capture_clock_gettime(clockid_t clockid, struct timespec *time)
{
	assert(clockid == CLOCK_MONOTONIC);
	*time = capture_now;
	return 0;
}

static void
capture_reset(void)
{
	memset(capture_memory, 0x5a, sizeof(capture_memory));
	memset(&capture_now, 0, sizeof(capture_now));
	capture_timeout = false;
	capture_dq_error = false;
	capture_streamoff_error = false;
	capture_poll_calls = 0;
	capture_qbuf_calls = 0;
	capture_dqbuf_calls = 0;
	capture_streamoff_calls = 0;
	capture_unmap_calls = 0;
}

int
main(void)
{
	const char *stage;

	capture_reset();
	assert(video_capture_run(1, 2, &stage) == 0);
	assert(stage == NULL && capture_dqbuf_calls == 2 &&
	    capture_poll_calls == 2 && capture_qbuf_calls == 6);
	assert(capture_streamoff_calls == 1 &&
	    capture_unmap_calls == CAPTURE_BUFFERS);

	capture_reset();
	capture_timeout = true;
	assert(video_capture_run(1, 1, &stage) == ETIMEDOUT);
	assert(strcmp(stage, "frame timeout") == 0 && capture_dqbuf_calls == 0);
	assert(capture_streamoff_calls == 1 &&
	    capture_unmap_calls == CAPTURE_BUFFERS);

	capture_reset();
	capture_dq_error = true;
	assert(video_capture_run(1, 1, &stage) == EIO);
	assert(strcmp(stage, "VIDIOC_DQBUF") == 0 &&
	    capture_streamoff_calls == 1 &&
	    capture_unmap_calls == CAPTURE_BUFFERS);

	capture_reset();
	capture_timeout = true;
	capture_streamoff_error = true;
	assert(video_capture_run(1, 1, &stage) == ETIMEDOUT);
	assert(strcmp(stage, "frame timeout") == 0 &&
	    capture_streamoff_calls == 1 &&
	    capture_unmap_calls == CAPTURE_BUFFERS);
	return 0;
}
