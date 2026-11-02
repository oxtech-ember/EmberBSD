/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _VIDEO_CAPTURE_H_
#define _VIDEO_CAPTURE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int video_capture_validate_sample(size_t, size_t, bool);
uint64_t video_capture_checksum(const void *, size_t);
int video_capture_run(int, unsigned int, const char **);

#endif /* _VIDEO_CAPTURE_H_ */
