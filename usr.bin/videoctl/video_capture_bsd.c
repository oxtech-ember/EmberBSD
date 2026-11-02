/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/videoio.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "video_capture.h"

#define VIDEO_CAPTURE_NBUFS 4
#define VIDEO_CAPTURE_FRAME_TIMEOUT 5

struct video_capture_buffer {
	void *address;
	size_t length;
};

static int
video_capture_wait_ready(int fd, const struct timespec *deadline,
    const char **stage)
{
	struct pollfd pfd;
	struct timespec now;
	long long seconds, milliseconds;
	long nanoseconds;
	int n;

	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	for (;;) {
		if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
			*stage = "clock_gettime";
			return errno;
		}
		seconds = deadline->tv_sec - now.tv_sec;
		nanoseconds = deadline->tv_nsec - now.tv_nsec;
		if (nanoseconds < 0) {
			seconds--;
			nanoseconds += 1000000000;
		}
		milliseconds = seconds * 1000 +
		    (nanoseconds + 999999) / 1000000;
		if (milliseconds <= 0) {
			*stage = "frame timeout";
			return ETIMEDOUT;
		}
		n = poll(&pfd, 1, milliseconds > INT_MAX ? INT_MAX :
		    (int)milliseconds);
		if (n == -1 && errno == EINTR)
			continue;
		if (n == -1) {
			*stage = "poll";
			return errno;
		}
		if (n == 0) {
			*stage = "frame timeout";
			return ETIMEDOUT;
		}
		if (pfd.revents & POLLNVAL) {
			*stage = "invalid video descriptor";
			return EBADF;
		}
		if (pfd.revents & (POLLERR | POLLHUP)) {
			*stage = "video stream poll error";
			return EIO;
		}
		if (pfd.revents & POLLIN)
			return 0;
	}
}

static int
video_capture_ioctl(int fd, unsigned long command, void *data,
    const char **stage, const char *name)
{
	if (ioctl(fd, command, data) == -1) {
		*stage = name;
		return errno;
	}
	return 0;
}

int
video_capture_run(int fd, unsigned int count, const char **stage)
{
	struct v4l2_capability capability;
	struct v4l2_format format;
	struct v4l2_requestbuffers request;
	struct video_capture_buffer *buffers = NULL;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	uint32_t fourcc;
	unsigned int i;
	bool streaming = false;
	int error = 0;

	*stage = NULL;
	memset(&capability, 0, sizeof(capability));
	if ((error = video_capture_ioctl(fd, VIDIOC_QUERYCAP, &capability, stage,
	    "VIDIOC_QUERYCAP")) != 0)
		goto out;
	if ((capability.capabilities & (V4L2_CAP_VIDEO_CAPTURE |
	    V4L2_CAP_STREAMING)) != (V4L2_CAP_VIDEO_CAPTURE |
	    V4L2_CAP_STREAMING)) {
		*stage = "device lacks capture streaming";
		error = ENOTSUP;
		goto out;
	}

	memset(&format, 0, sizeof(format));
	format.type = type;
	if ((error = video_capture_ioctl(fd, VIDIOC_G_FMT, &format, stage,
	    "VIDIOC_G_FMT")) != 0)
		goto out;
	if (format.fmt.pix.sizeimage == 0) {
		*stage = "empty frame size";
		error = EINVAL;
		goto out;
	}
	fourcc = format.fmt.pix.pixelformat;

	memset(&request, 0, sizeof(request));
	request.count = VIDEO_CAPTURE_NBUFS;
	request.type = type;
	request.memory = V4L2_MEMORY_MMAP;
	if ((error = video_capture_ioctl(fd, VIDIOC_REQBUFS, &request, stage,
	    "VIDIOC_REQBUFS")) != 0)
		goto out;
	if (request.count < 2 || request.count > VIDEO_CAPTURE_NBUFS) {
		*stage = "unsupported buffer count";
		error = ENOBUFS;
		goto out;
	}
	buffers = calloc(request.count, sizeof(*buffers));
	if (buffers == NULL) {
		*stage = "allocate buffer table";
		error = ENOMEM;
		goto out;
	}

	for (i = 0; i < request.count; i++) {
		struct v4l2_buffer buffer;

		memset(&buffer, 0, sizeof(buffer));
		buffer.type = type;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.index = i;
		if ((error = video_capture_ioctl(fd, VIDIOC_QUERYBUF, &buffer, stage,
		    "VIDIOC_QUERYBUF")) != 0)
			goto out;
		if (buffer.length == 0) {
			*stage = "empty mapped buffer";
			error = EINVAL;
			goto out;
		}
		buffers[i].length = buffer.length;
		buffers[i].address = mmap(NULL, buffer.length, PROT_READ, MAP_SHARED,
		    fd, (off_t)buffer.m.offset);
		if (buffers[i].address == MAP_FAILED) {
			buffers[i].address = NULL;
			*stage = "mmap capture buffer";
			error = errno;
			goto out;
		}
		if ((error = video_capture_ioctl(fd, VIDIOC_QBUF, &buffer, stage,
		    "VIDIOC_QBUF")) != 0)
			goto out;
	}

	if ((error = video_capture_ioctl(fd, VIDIOC_STREAMON, &type, stage,
	    "VIDIOC_STREAMON")) != 0)
		goto out;
	streaming = true;
	printf("capture.format=%ux%u fourcc=%c%c%c%c size=%u buffers=%u\n",
	    format.fmt.pix.width, format.fmt.pix.height,
	    (char)(fourcc & 0xff), (char)((fourcc >> 8) & 0xff),
	    (char)((fourcc >> 16) & 0xff), (char)((fourcc >> 24) & 0xff),
	    format.fmt.pix.sizeimage, request.count);

	for (i = 0; i < count; i++) {
		struct v4l2_buffer buffer;
		struct timespec deadline;

		if (clock_gettime(CLOCK_MONOTONIC, &deadline) == -1) {
			*stage = "clock_gettime";
			error = errno;
			goto out;
		}
		deadline.tv_sec += VIDEO_CAPTURE_FRAME_TIMEOUT;
		for (;;) {
			error = video_capture_wait_ready(fd, &deadline, stage);
			if (error != 0)
				goto out;

			memset(&buffer, 0, sizeof(buffer));
			buffer.type = type;
			buffer.memory = V4L2_MEMORY_MMAP;
			error = video_capture_ioctl(fd, VIDIOC_DQBUF, &buffer, stage,
			    "VIDIOC_DQBUF");
			if (error == EAGAIN)
				continue;
			if (error != 0)
				goto out;
			break;
		}
		if (buffer.index >= request.count) {
			*stage = "driver returned invalid buffer index";
			error = EIO;
			goto out;
		}
		error = video_capture_validate_sample(buffer.bytesused,
		    buffers[buffer.index].length,
		    (buffer.flags & V4L2_BUF_FLAG_ERROR) != 0);
		if (error != 0) {
			*stage = "invalid frame payload";
			goto out;
		}
		printf("capture.frame=%u bytes=%u fnv1a64=%016" PRIx64 "\n",
		    i, buffer.bytesused,
		    video_capture_checksum(buffers[buffer.index].address,
		    buffer.bytesused));
		if ((error = video_capture_ioctl(fd, VIDIOC_QBUF, &buffer, stage,
		    "VIDIOC_QBUF")) != 0)
			goto out;
	}

out:
	if (streaming) {
		int stream_error = ioctl(fd, VIDIOC_STREAMOFF, &type);
		if (stream_error == -1) {
			if (error == 0) {
				error = errno;
				*stage = "VIDIOC_STREAMOFF";
			}
		}
	}
	if (buffers != NULL) {
		for (i = 0; i < request.count; i++) {
			if (buffers[i].address != NULL)
				(void)munmap(buffers[i].address, buffers[i].length);
		}
		free(buffers);
	}
	return error;
}
