/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted test adapter. */
#ifndef PVR_RBTREE_TEST_H
#define PVR_RBTREE_TEST_H
#include <sys/types.h>
#include <sys/queue.h>
#include <stdint.h>
#define RBTEST 1
#define RBDEBUG 1
#ifndef __RCSID
#define __RCSID(s)
#endif
#ifndef __predict_false
#define __predict_false(e) __builtin_expect(!!(e), 0)
#endif
#ifndef __unused
#define __unused __attribute__((__unused__))
#endif
#endif
