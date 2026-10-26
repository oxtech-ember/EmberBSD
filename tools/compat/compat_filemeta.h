/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#ifndef EMBERBSD_COMPAT_FILEMETA_H
#define EMBERBSD_COMPAT_FILEMETA_H

#define NB_FILEMETA_XATTR 0x01U
#define NB_FILEMETA_ACL   0x02U

/*
 * Overlay requested source metadata; do not erase destination-only metadata.
 * The caller owns both descriptors, including descriptor zero.
 * A failure may leave partial destination metadata. errno identifies failure.
 * Neither function copies data, closes caller descriptors, or removes paths.
 */
int __nbcompat_copy_filemeta_fd(int, int, unsigned int);
int __nbcompat_copy_filemeta_nofollow(const char *, const char *, unsigned int);

#endif /* EMBERBSD_COMPAT_FILEMETA_H */
