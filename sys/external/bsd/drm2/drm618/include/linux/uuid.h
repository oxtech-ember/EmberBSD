/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_drv.h, virtgpu_prime.c,
 * virtgpu_vq.c of dist618 (MIT); the GPL header of Linux was not read.
 *
 * Shadow of <linux/uuid.h> for the 6.18 core: the 16 byte uuid_t and the
 * calls the virtio-gpu driver makes on it, besides the guid_t of the layer.
 */

#ifndef _DRM618_LINUX_UUID_H_
#define _DRM618_LINUX_UUID_H_

#include_next <linux/uuid.h>

#include <sys/types.h>
#include <sys/systm.h>

#include <linux/types.h>

typedef struct {
	__u8	b[16];
} uuid_t;

static inline void
uuid_copy(uuid_t *dst, const uuid_t *src)
{

	memcpy(dst, src, sizeof(*dst));
}

/* The bytes are the uuid in the order of its text form. */
static inline void
import_uuid(uuid_t *u, const __u8 *src)
{

	memcpy(u->b, src, sizeof(u->b));
}

static inline void
export_uuid(__u8 *dst, const uuid_t *u)
{

	memcpy(dst, u->b, sizeof(u->b));
}

static inline bool
uuid_equal(const uuid_t *a, const uuid_t *b)
{

	return memcmp(a, b, sizeof(*a)) == 0;
}

#endif	/* _DRM618_LINUX_UUID_H_ */
