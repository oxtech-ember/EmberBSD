/*	$NetBSD: device.h,v 1.17 2022/07/29 23:50:44 riastradh Exp $	*/

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

#ifndef _LINUX_DEVICE_H_
#define _LINUX_DEVICE_H_

#include <sys/types.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/hrtimer.h>
#include <linux/ratelimit.h>

#define	dev_crit(DEV, FMT, ...)	do {					      \
	if (DEV)							      \
		aprint_error_dev((DEV), "critical: " FMT, ##__VA_ARGS__);     \
	else								      \
		aprint_error("critical: " FMT, ##__VA_ARGS__);		      \
} while (0)

#define	dev_err(DEV, FMT, ...)	do {					      \
	if (DEV)							      \
		aprint_error_dev((DEV), "error: " FMT, ##__VA_ARGS__);	      \
	else								      \
		aprint_error("error: " FMT, ##__VA_ARGS__);		      \
} while (0)

#define	dev_err_once	dev_err	/* XXX rate-limit */

#define	dev_warn(DEV, FMT, ...)	do {					      \
	if (DEV)							      \
		aprint_normal_dev((DEV), "warn: " FMT, ##__VA_ARGS__);	      \
	else								      \
		aprint_normal("warn: " FMT, ##__VA_ARGS__);		      \
} while (0)
#define	dev_WARN	dev_warn

#define	dev_notice(DEV, FMT, ...)	do {				      \
	if (DEV)							      \
		aprint_normal_dev((DEV), "notice: " FMT, ##__VA_ARGS__);      \
	else								      \
		aprint_normal("notice: " FMT, ##__VA_ARGS__);		      \
} while (0)

#define	dev_info(DEV, FMT, ...)	do {					      \
	if (DEV)							      \
		aprint_normal_dev((DEV), FMT, ##__VA_ARGS__);		      \
	else								      \
		aprint_normal(FMT, ##__VA_ARGS__);			      \
} while (0)

#define	dev_dbg(DEV, FMT, ...)	do {					      \
	if (DEV)							      \
		aprint_debug_dev((DEV), "debug: " FMT, ##__VA_ARGS__);	      \
	else								      \
		aprint_debug("debug: " FMT, ##__VA_ARGS__);		      \
} while (0)

#define	dev_name	device_xname

/*
 * Device-managed resources (linux_devres.c): released newest first by
 * devres_release_all(dev), which the owner of the device calls when it goes
 * away.  devm_kmalloc memory is zeroed whatever the flags.
 */
int	devm_add_action(struct device *, void (*)(void *), void *);
int	devm_add_action_or_reset(struct device *, void (*)(void *), void *);
void	devm_release_action(struct device *, void (*)(void *), void *);
void	*devm_kmalloc(struct device *, size_t, gfp_t);
void	devm_kfree(struct device *, void *);
void	devres_release_all(struct device *);

static inline void *
devm_kzalloc(struct device *dev, size_t size, gfp_t gfp)
{
	return devm_kmalloc(dev, size, gfp);
}

/* The device is a NetBSD device_t: no reference count to drop. */
#define	put_device(x)	((void)(x))

/*
 * dev_printk(LEVEL, dev, fmt, ...): LEVEL is one of the KERN_ strings, which
 * carry the severity as a prefix of the text.
 */
#define	dev_printk(LEVEL, DEV, FMT, ...)	do {			      \
	if (DEV)							      \
		aprint_normal_dev((DEV), LEVEL FMT, ##__VA_ARGS__);	      \
	else								      \
		aprint_normal(LEVEL FMT, ##__VA_ARGS__);		      \
} while (0)

/*
 * Report an error unless the driver is only not ready yet (-EPROBE_DEFER,
 * which is quiet) and return it.
 */
#define	dev_err_probe(DEV, ERR, FMT, ...)	({			      \
	int __err = (ERR);						      \
	if (__err != -EPROBE_DEFER)						      \
		dev_err((DEV), FMT, ##__VA_ARGS__);			      \
	__err;								      \
})

/*
 * dev_driver_string(dev)
 *
 *	The name of the driver bound to dev (NetBSD: the autoconf driver
 *	name, as in "i915drmkms"), or "" if there is no device.
 *	EmberBSD addition, AI-assisted.
 */
static inline const char *
dev_driver_string(const struct device *dev)
{

	if (dev == NULL)
		return "";
	return device_cfdriver(__UNCONST(dev))->cd_name;
}
#define	get_device(x)	(x)

#define	DPM_FLAG_NEVER_SKIP	0

#define	dev_warn_ratelimited	dev_warn

static inline void
dev_pm_set_driver_flags(struct device *dev, uint32_t flags)
{
}

#endif  /* _LINUX_DEVICE_H_ */
