# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
# Shared complete metadata source. This fragment has no capability presets.
COMPAT_FILEMETA_SRCS= compat_filemeta.c
COMPAT_FILEMETA_HEADERS= compat_filemeta.h compat_filemeta_features.h \
    compat_native_features.h compat_acl_features.h compat_native_acl.h
COMPAT_FILEMETA_PROBES= probes/filemeta-open.c probes/filemeta-regular-open.c \
    probes/filemeta-acl-darwin.c probes/filemeta-acl-bsd.c \
    probes/filemeta-xattr-darwin.c probes/filemeta-xattr-bsd-int.c \
    probes/filemeta-xattr-bsd-ssize.c probes/filemeta-xattr-bsd-probe.h \
    probes/filemeta-representation-darwin.c
