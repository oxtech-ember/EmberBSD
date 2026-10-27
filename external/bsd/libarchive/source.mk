# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Select the source prepared by the common Ports libarchive producer.
.if !defined(EMBER_LIBARCHIVE_SOURCE)
.error Set EMBER_LIBARCHIVE_SOURCE to the prepared common Ports libarchive source
.endif
.if empty(EMBER_LIBARCHIVE_SOURCE:tW:M/*) || \
    "${EMBER_LIBARCHIVE_SOURCE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${EMBER_LIBARCHIVE_SOURCE}/libarchive/archive.h) || \
    !exists(${EMBER_LIBARCHIVE_SOURCE}/tar/bsdtar.c)
.error EMBER_LIBARCHIVE_SOURCE must name the prepared absolute libarchive source
.endif
LIBARCHIVEDIR=	${EMBER_LIBARCHIVE_SOURCE}
