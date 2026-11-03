/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Minimal V4L2 ABI subset for the videoctl capture-I/O contract fixture.
 */
#ifndef _TEST_SYS_VIDEOIO_H_
#define _TEST_SYS_VIDEOIO_H_

#include <stdint.h>

enum v4l2_buf_type {
	V4L2_BUF_TYPE_VIDEO_CAPTURE = 1
};

#define V4L2_CAP_VIDEO_CAPTURE 0x00000001U
#define V4L2_CAP_STREAMING 0x04000000U
#define V4L2_MEMORY_MMAP 1U
#define V4L2_BUF_FLAG_ERROR 0x00000040U
#define V4L2_PIX_FMT_UYVY 0x59565955U

#define VIDIOC_QUERYCAP 1UL
#define VIDIOC_G_FMT 2UL
#define VIDIOC_REQBUFS 3UL
#define VIDIOC_QUERYBUF 4UL
#define VIDIOC_QBUF 5UL
#define VIDIOC_DQBUF 6UL
#define VIDIOC_STREAMON 7UL
#define VIDIOC_STREAMOFF 8UL

struct v4l2_capability {
	uint32_t capabilities;
};

struct v4l2_pix_format {
	uint32_t width, height, pixelformat, field;
	uint32_t bytesperline, sizeimage, colorspace, priv, flags;
	uint32_t ycbcr_enc, quantization, xfer_func;
};

struct v4l2_format {
	uint32_t type;
	union {
		struct v4l2_pix_format pix;
	} fmt;
};

struct v4l2_requestbuffers {
	uint32_t count, type, memory;
};

struct v4l2_buffer {
	uint32_t index, type, bytesused, flags;
	uint32_t memory;
	union {
		uint32_t offset;
	} m;
	uint32_t length;
};

#endif
