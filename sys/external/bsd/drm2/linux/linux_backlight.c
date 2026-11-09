/*	$NetBSD: linux_backlight.c,v 1.1 2021/12/19 10:46:55 riastradh Exp $	*/

/*-
 * Copyright (c) 2020 The NetBSD Foundation, Inc.
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
 * Minimal Linux backlight class.  See <linux/backlight.h>.
 *
 * EmberBSD extension (AI-assisted).
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: linux_backlight.c,v 1.1 2021/12/19 10:46:55 riastradh Exp $");

#include <sys/types.h>
#include <sys/condvar.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/queue.h>

#include <linux/backlight.h>
#include <linux/err.h>

/*
 * linux_backlight_lock protects the registry list, bd_refs and
 * bd_dead.  bd_lock serializes the driver ops and props of one device.
 */
static kmutex_t linux_backlight_lock;
static kcondvar_t linux_backlight_cv;
static TAILQ_HEAD(, backlight_device) linux_backlight_list =
    TAILQ_HEAD_INITIALIZER(linux_backlight_list);
static ONCE_DECL(linux_backlight_once);

static int
linux_backlight_init(void)
{

	mutex_init(&linux_backlight_lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&linux_backlight_cv, "lxbklt");
	return 0;
}

struct backlight_device *
backlight_device_register(const char *name, struct device *dev, void *data,
    const struct backlight_ops *ops, const struct backlight_properties *props)
{
	struct backlight_device *bd;

	if (ops == NULL || ops->update_status == NULL)
		return ERR_PTR(-EINVAL);
	RUN_ONCE(&linux_backlight_once, linux_backlight_init);

	bd = kmem_zalloc(sizeof(*bd), KM_SLEEP);
	if (props != NULL)
		bd->props = *props;
	bd->ops = ops;
	bd->bd_data = data;
	bd->bd_dev = dev;
	strlcpy(bd->bd_name, name, sizeof(bd->bd_name));
	mutex_init(&bd->bd_lock, MUTEX_DEFAULT, IPL_NONE);

	mutex_enter(&linux_backlight_lock);
	TAILQ_INSERT_TAIL(&linux_backlight_list, bd, bd_list);
	mutex_exit(&linux_backlight_lock);

	return bd;
}

/*
 * Remove the device from the registry and wait for the users that
 * found it through linux_backlight_lookup() to release it.
 */
void
backlight_device_unregister(struct backlight_device *bd)
{

	if (bd == NULL)
		return;
	mutex_enter(&linux_backlight_lock);
	TAILQ_REMOVE(&linux_backlight_list, bd, bd_list);
	bd->bd_dead = true;
	while (bd->bd_refs != 0)
		cv_wait(&linux_backlight_cv, &linux_backlight_lock);
	mutex_exit(&linux_backlight_lock);

	mutex_destroy(&bd->bd_lock);
	kmem_free(bd, sizeof(*bd));
}

/*
 * Convert a driver result to an errno: Linux drivers return a
 * negative errno; amdgpu_dm returns 1 on failure.
 */
static int
linux_backlight_error(int ret)
{

	if (ret == 0)
		return 0;
	return ret < 0 ? -ret : EIO;
}

int
backlight_update_status(struct backlight_device *bd)
{
	int ret;

	mutex_enter(&bd->bd_lock);
	ret = (*bd->ops->update_status)(bd);
	mutex_exit(&bd->bd_lock);
	return ret;
}

void
backlight_force_update(struct backlight_device *bd,
    enum backlight_update_reason reason)
{
	int level;

	if (bd->ops->get_brightness == NULL)
		return;
	mutex_enter(&bd->bd_lock);
	/* A blanked device reports 0; keep the level for power on. */
	if (!backlight_is_blank(bd)) {
		level = (*bd->ops->get_brightness)(bd);
		if (level >= 0 && level <= bd->props.max_brightness)
			bd->props.brightness = level;
	}
	mutex_exit(&bd->bd_lock);
}

int
backlight_enable(struct backlight_device *bd)
{

	if (bd == NULL)
		return 0;
	return -linux_backlight_set_power(bd, true);
}

int
backlight_disable(struct backlight_device *bd)
{

	if (bd == NULL)
		return 0;
	return -linux_backlight_set_power(bd, false);
}

/*
 * Return the first backlight registered for the DRM device dev with a
 * reference held, or NULL.  The caller releases it with
 * linux_backlight_release().
 */
struct backlight_device *
linux_backlight_lookup(struct device *dev)
{
	struct backlight_device *bd;

	RUN_ONCE(&linux_backlight_once, linux_backlight_init);
	mutex_enter(&linux_backlight_lock);
	TAILQ_FOREACH(bd, &linux_backlight_list, bd_list) {
		if (bd->bd_dev == dev) {
			bd->bd_refs++;
			break;
		}
	}
	mutex_exit(&linux_backlight_lock);
	return bd;
}

void
linux_backlight_release(struct backlight_device *bd)
{

	mutex_enter(&linux_backlight_lock);
	KASSERT(bd->bd_refs > 0);
	if (--bd->bd_refs == 0 && bd->bd_dead)
		cv_broadcast(&linux_backlight_cv);
	mutex_exit(&linux_backlight_lock);
}

/*
 * Return the current brightness in the device range 0..max_brightness.
 */
int
linux_backlight_get_brightness(struct backlight_device *bd)
{
	int level;

	mutex_enter(&bd->bd_lock);
	level = bd->props.brightness;
	mutex_exit(&bd->bd_lock);
	return level;
}

/*
 * Report whether the backlight is on.  Off means blanked by power
 * state or by the FBBLANK/SUSPENDED core flags.
 */
bool
linux_backlight_get_power(struct backlight_device *bd)
{
	bool on;

	mutex_enter(&bd->bd_lock);
	on = !backlight_is_blank(bd);
	mutex_exit(&bd->bd_lock);
	return on;
}

/*
 * Turn the backlight on or off.  The brightness level is kept, so
 * that power on restores it.  Returns an errno.
 */
int
linux_backlight_set_power(struct backlight_device *bd, bool on)
{
	int old_power, ret;
	unsigned old_state;

	mutex_enter(&bd->bd_lock);
	old_power = bd->props.power;
	old_state = bd->props.state;
	if (on) {
		bd->props.power = FB_BLANK_UNBLANK;
		bd->props.state &= ~BL_CORE_FBBLANK;
	} else {
		bd->props.power = FB_BLANK_POWERDOWN;
		bd->props.state |= BL_CORE_FBBLANK;
	}
	ret = linux_backlight_error((*bd->ops->update_status)(bd));
	if (ret != 0) {
		bd->props.power = old_power;
		bd->props.state = old_state;
	}
	mutex_exit(&bd->bd_lock);
	return ret;
}

/*
 * Set the brightness in the device range 0..max_brightness and push it
 * to the hardware.  Returns 0 or an errno; on failure the previous
 * brightness is kept.
 */
int
linux_backlight_set_brightness(struct backlight_device *bd, int level)
{
	int old, ret;

	if (level < 0 || level > bd->props.max_brightness)
		return EINVAL;
	mutex_enter(&bd->bd_lock);
	old = bd->props.brightness;
	bd->props.brightness = level;
	ret = linux_backlight_error((*bd->ops->update_status)(bd));
	if (ret != 0)
		bd->props.brightness = old;
	mutex_exit(&bd->bd_lock);
	return ret;
}
