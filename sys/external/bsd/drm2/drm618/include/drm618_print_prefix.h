/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; no Linux code.
 *
 * Included (-include) into drm_print.c only: its snprintf, printk and the log
 * functions of a device take Linux's %pV and %ps, which the kernel's printf
 * does not know (drm618_printf.c does).
 */

#ifndef _DRM618_PRINT_PREFIX_H_
#define _DRM618_PRINT_PREFIX_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <linux/printk.h>
#include <linux/device.h>

int	drm618_snprintf(char *, size_t, const char *, ...) __printflike(3, 4);
int	drm618_printk(const char *, ...) __printflike(1, 2);
void	drm618_aprint_normal_dev(device_t, const char *, ...);
void	drm618_aprint_error_dev(device_t, const char *, ...);
void	drm618_aprint_debug_dev(device_t, const char *, ...);

#undef	snprintf
#define	snprintf	drm618_snprintf
#undef	printk
#define	printk		drm618_printk
#undef	aprint_normal_dev
#define	aprint_normal_dev	drm618_aprint_normal_dev
#undef	aprint_error_dev
#define	aprint_error_dev	drm618_aprint_error_dev
#undef	aprint_debug_dev
#define	aprint_debug_dev	drm618_aprint_debug_dev

#endif	/* _DRM618_PRINT_PREFIX_H_ */
