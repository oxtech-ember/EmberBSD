/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD. Shadow of <linux/poll.h>: poll_wait records the caller on the select queue of the wait queue head. */
#ifndef _DRM618_LINUX_POLL_H_
#define _DRM618_LINUX_POLL_H_
#include <sys/select.h>
#include <drm618_file.h>
#include <linux/wait.h>
#include <linux/uaccess.h>

typedef struct poll_table_struct {
	int	pt_unused;
} poll_table;

static inline void
poll_wait(struct DRM618_FILE_TAG *filp, wait_queue_head_t *wq, poll_table *pt)
{

	selrecord(curlwp, &wq->wq_sel);
}
#endif
