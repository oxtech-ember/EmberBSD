/*	$NetBSD: backlight.h,v 1.3 2021/12/19 10:40:03 riastradh Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Minimal Linux backlight class for DRM drivers.
 *
 * A driver registers one backlight device per panel it controls.  The
 * class keeps the registered devices in a list so that drmfb(4) can
 * find the backlight of its DRM device and expose it through the
 * wsdisplay(4) brightness parameter.  There is no sysfs; user space
 * reaches the backlight only through wsdisplay(4).
 *
 * EmberBSD extension (AI-assisted).
 */

#ifndef _LINUX_BACKLIGHT_H_
#define _LINUX_BACKLIGHT_H_

#include <sys/types.h>
#include <sys/device_if.h>
#include <sys/mutex.h>
#include <sys/queue.h>

#include <linux/fb.h>

struct device;
struct fb_info;

enum backlight_type {
	BACKLIGHT_RAW = 1,
	BACKLIGHT_PLATFORM,
	BACKLIGHT_FIRMWARE,
};

enum backlight_update_reason {
	BACKLIGHT_UPDATE_HOTKEY,
	BACKLIGHT_UPDATE_SYSFS,
};

#define	BL_CORE_SUSPENDRESUME	__BIT(0)
#define	BL_CORE_SUSPENDED	__BIT(0)
#define	BL_CORE_FBBLANK		__BIT(1)

struct backlight_properties {
	int		brightness;
	int		max_brightness;
	int		power;
	int		fb_blank;
	enum backlight_type type;
	unsigned int	state;
};

struct backlight_device;

struct backlight_ops {
	unsigned int	options;
	int		(*update_status)(struct backlight_device *);
	int		(*get_brightness)(struct backlight_device *);
	int		(*check_fb)(struct backlight_device *,
			    struct fb_info *);
};

struct backlight_device {
	struct backlight_properties	props;
	const struct backlight_ops	*ops;
	void				*bd_data;
	struct device			*bd_dev;
	char				bd_name[32];
	kmutex_t			bd_lock;	/* ops and props */
	TAILQ_ENTRY(backlight_device)	bd_list;	/* registry */
	unsigned			bd_refs;	/* registry */
	bool				bd_dead;	/* registry */
};

#define	backlight_device_register	linux_backlight_device_register
#define	backlight_device_unregister	linux_backlight_device_unregister
#define	backlight_disable		linux_backlight_disable
#define	backlight_enable		linux_backlight_enable
#define	backlight_force_update		linux_backlight_force_update
#define	backlight_update_status		linux_backlight_update_status

struct backlight_device *
	backlight_device_register(const char *, struct device *, void *,
	    const struct backlight_ops *, const struct backlight_properties *);
void	backlight_device_unregister(struct backlight_device *);
int	backlight_update_status(struct backlight_device *);
void	backlight_force_update(struct backlight_device *,
	    enum backlight_update_reason);
int	backlight_disable(struct backlight_device *);
int	backlight_enable(struct backlight_device *);

static inline void *
bl_get_data(struct backlight_device *bd)
{
	return bd->bd_data;
}

static inline int
backlight_get_brightness(const struct backlight_device *bd)
{
	return bd->props.brightness;
}

/*
 * NetBSD side.  drmfb(4) uses these to find and drive the backlight of
 * the DRM device whose framebuffer it serves.  A lookup holds a
 * reference until the matching release; unregister waits for all
 * references.  Brightness is expressed in the device's own range
 * 0..max_brightness.
 */
struct backlight_device *
	linux_backlight_lookup(struct device *);
void	linux_backlight_release(struct backlight_device *);
int	linux_backlight_get_brightness(struct backlight_device *);
int	linux_backlight_set_brightness(struct backlight_device *, int);

#endif  /* _LINUX_BACKLIGHT_H_ */
