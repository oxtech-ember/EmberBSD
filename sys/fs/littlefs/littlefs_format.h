/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _FS_LITTLEFS_FORMAT_H_
#define _FS_LITTLEFS_FORMAT_H_

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/stdint.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

/*
 * BSD metadata is an upstream littlefs user attribute.  File data and the
 * littlefs disk format are unchanged.  Integers in this attribute are LE;
 * never write a native C structure to disk.
 */
#define LITTLEFS_BSD_ATTR	0x42
#define LITTLEFS_NEXTINO_ATTR	0x43
#define LITTLEFS_BSD_MAGIC	UINT32_C(0x464c4245) /* EBLF */
#define LITTLEFS_BSD_VERSION	1
#define LITTLEFS_BSD_SIZE	80
#define LITTLEFS_ROOT_INO	UINT64_C(2)
#define LITTLEFS_STAGING_NAME	".littlefs-tmp"

struct littlefs_bsd_attr {
	uint64_t ino;
	uint32_t mode;
	uint32_t uid;
	uint32_t gid;
	uint32_t flags;
	uint64_t rdev;
	int64_t atime;
	int64_t mtime;
	int64_t ctime;
	uint32_t atimensec;
	uint32_t mtimensec;
	uint32_t ctimensec;
};

static inline uint32_t
littlefs_le32dec(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void
littlefs_le32enc(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static inline uint64_t
littlefs_le64dec(const uint8_t *p)
{
	return littlefs_le32dec(p) | (uint64_t)littlefs_le32dec(p + 4) << 32;
}

static inline void
littlefs_le64enc(uint8_t *p, uint64_t v)
{
	littlefs_le32enc(p, (uint32_t)v);
	littlefs_le32enc(p + 4, (uint32_t)(v >> 32));
}

static inline int
littlefs_bsd_attr_valid(const struct littlefs_bsd_attr *a)
{
	switch (a->mode & UINT32_C(0170000)) {
	case 0010000: /* FIFO */
	case 0020000: /* character device */
	case 0040000: /* directory */
	case 0060000: /* block device */
	case 0100000: /* regular file */
	case 0120000: /* symbolic link */
	case 0140000: /* socket */
		break;
	default:
		return 0;
	}
	return a->ino >= LITTLEFS_ROOT_INO && a->mode <= UINT32_C(0177777) &&
	    a->atimensec < UINT32_C(1000000000) &&
	    a->mtimensec < UINT32_C(1000000000) &&
	    a->ctimensec < UINT32_C(1000000000);
}

static inline int
littlefs_bsd_decode(struct littlefs_bsd_attr *a, const uint8_t *p, size_t size)
{
	if (size != LITTLEFS_BSD_SIZE ||
	    littlefs_le32dec(p) != LITTLEFS_BSD_MAGIC ||
	    littlefs_le32dec(p + 4) !=
	    (LITTLEFS_BSD_SIZE << 16 | LITTLEFS_BSD_VERSION) ||
	    littlefs_le32dec(p + 76) != 0)
		return -1;
	a->ino = littlefs_le64dec(p + 8);
	a->mode = littlefs_le32dec(p + 16);
	a->uid = littlefs_le32dec(p + 20);
	a->gid = littlefs_le32dec(p + 24);
	a->flags = littlefs_le32dec(p + 28);
	a->rdev = littlefs_le64dec(p + 32);
	a->atime = (int64_t)littlefs_le64dec(p + 40);
	a->mtime = (int64_t)littlefs_le64dec(p + 48);
	a->ctime = (int64_t)littlefs_le64dec(p + 56);
	a->atimensec = littlefs_le32dec(p + 64);
	a->mtimensec = littlefs_le32dec(p + 68);
	a->ctimensec = littlefs_le32dec(p + 72);
	return littlefs_bsd_attr_valid(a) ? 0 : -1;
}

static inline int
littlefs_bsd_encode(uint8_t *p, const struct littlefs_bsd_attr *a)
{
	if (!littlefs_bsd_attr_valid(a))
		return -1;
	littlefs_le32enc(p, LITTLEFS_BSD_MAGIC);
	littlefs_le32enc(p + 4,
	    LITTLEFS_BSD_SIZE << 16 | LITTLEFS_BSD_VERSION);
	littlefs_le64enc(p + 8, a->ino);
	littlefs_le32enc(p + 16, a->mode);
	littlefs_le32enc(p + 20, a->uid);
	littlefs_le32enc(p + 24, a->gid);
	littlefs_le32enc(p + 28, a->flags);
	littlefs_le64enc(p + 32, a->rdev);
	littlefs_le64enc(p + 40, (uint64_t)a->atime);
	littlefs_le64enc(p + 48, (uint64_t)a->mtime);
	littlefs_le64enc(p + 56, (uint64_t)a->ctime);
	littlefs_le32enc(p + 64, a->atimensec);
	littlefs_le32enc(p + 68, a->mtimensec);
	littlefs_le32enc(p + 72, a->ctimensec);
	littlefs_le32enc(p + 76, 0);
	return 0;
}

#endif /* _FS_LITTLEFS_FORMAT_H_ */
