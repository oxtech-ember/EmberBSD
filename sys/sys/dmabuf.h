/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 The EmberBSD contributors.
 * Origin: EmberBSD, AI-assisted. Native DMA-BUF synchronization ABI.
 * The operation numbers and field layouts match the public DMA-BUF UAPI.
 */
#ifndef _SYS_DMABUF_H_
#define _SYS_DMABUF_H_

#include <sys/types.h>
#include <sys/ioccom.h>

#define DMA_BUF_SYNC_READ	0x1U
#define DMA_BUF_SYNC_WRITE	0x2U
#define DMA_BUF_SYNC_RW		(DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)

struct dma_buf_export_sync_file {
	uint32_t flags;
	int32_t fd;
};

struct dma_buf_import_sync_file {
	uint32_t flags;
	int32_t fd;
};

#define DMA_BUF_IOCTL_EXPORT_SYNC_FILE \
	_IOWR('b', 2, struct dma_buf_export_sync_file)
#define DMA_BUF_IOCTL_IMPORT_SYNC_FILE \
	_IOW('b', 3, struct dma_buf_import_sync_file)

#endif /* _SYS_DMABUF_H_ */
