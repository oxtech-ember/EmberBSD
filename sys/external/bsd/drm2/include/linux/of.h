/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface, no Linux code.
 *
 * Device tree: the CONFIG_OF=n side.  Only the type names the 6.18 DRM headers
 * use; the device tree functions are not declared, so a use that is not
 * under IS_ENABLED(CONFIG_OF) fails to compile.
 */
#ifndef _LINUX_OF_H_
#define _LINUX_OF_H_

struct device_node;
struct of_device_id;

#endif
