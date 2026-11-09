/* Origin: EmberBSD optional native damage and mode hooks, 2026-10-06. */
/*	$NetBSD: drmfb.c,v 1.16 2022/09/01 17:54:47 riastradh Exp $	*/

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
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
 * drmfb: wsdisplay support, via genfb, for any drm device.  Use this
 * if you're too lazy to write a hardware-accelerated framebuffer using
 * wsdisplay directly.
 *
 * This doesn't actually do anything interesting -- just hooks up
 * drmkms crap and genfb crap.
 */

#ifdef _KERNEL_OPT
#include "opt_wsdisplay_bootfb.h"
#endif

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: drmfb.c,v 1.16 2022/09/01 17:54:47 riastradh Exp $");

#ifdef _KERNEL_OPT
#include "vga.h"
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/kauth.h>

#if NVGA > 0
/*
 * XXX All we really need is vga_is_console from vgavar.h, but the
 * header files are missing their own dependencies, so we need to
 * explicitly drag in the other crap.
 */
#include <dev/ic/mc6845reg.h>
#include <dev/ic/pcdisplayvar.h>
#include <dev/ic/vgareg.h>
#include <dev/ic/vgavar.h>
#endif

#include <dev/wsfb/genfbvar.h>
#ifdef WSDISPLAY_BOOTFB_HANDOFF
#include <dev/wscons/wsdisplayvar.h>
#endif

#include <linux/backlight.h>

#include <drm/drm_device.h>
#include <drm/drm_fb_helper.h>
#include <drm/drmfb.h>

static int	drmfb_genfb_ioctl(void *, void *, unsigned long, void *, int,
		    struct lwp *);
static paddr_t	drmfb_genfb_mmap(void *, void *, off_t, int);
static int	drmfb_genfb_enable_polling(void *);
static int	drmfb_genfb_disable_polling(void *);
static bool	drmfb_genfb_setmode(struct genfb_softc *, int);
static int	drmfb_brightness_get(void *, int *);
static int	drmfb_brightness_set(void *, int);
static int	drmfb_brightness_upd(void *, int);

static void
drmfb_genfb_damage(void *cookie)
{
	struct drmfb_softc *sc = cookie;

	sc->sc_da.da_params->dp_damage(sc);
}

static const struct genfb_mode_callback drmfb_genfb_mode_callback = {
	.gmc_setmode = drmfb_genfb_setmode,
};

int
drmfb_attach(struct drmfb_softc *sc, const struct drmfb_attach_args *da)
{
	const struct drm_fb_helper_surface_size *const sizes = da->da_fb_sizes;
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	const prop_dictionary_t dict = device_properties(da->da_dev);
	const device_t parent = device_parent(da->da_dev);
	const prop_dictionary_t pdict = device_properties(parent);
#if NVGA > 0
	struct drm_device *const dev = da->da_fb_helper->dev;
#endif
	static const struct genfb_ops zero_genfb_ops;
	struct genfb_ops genfb_ops = zero_genfb_ops;
	struct backlight_device *bd;
	bool is_console;
	int error __diagused;

	/* genfb requires this.  */
	KASSERTMSG((void *)&sc->sc_genfb == device_private(da->da_dev),
	    "drmfb_softc must be first member of device softc");

	sc->sc_da = *da;

	prop_dictionary_set_uint32(dict, "width", sizes->surface_width);
	prop_dictionary_set_uint32(dict, "height", sizes->surface_height);
	prop_dictionary_set_uint8(dict, "depth", sizes->surface_bpp);
	prop_dictionary_set_uint16(dict, "linebytes", da->da_fb_linebytes);
	prop_dictionary_set_uint32(dict, "address", 0); /* XXX >32-bit */
	CTASSERT(sizeof(uintptr_t) <= sizeof(uint64_t));
	prop_dictionary_set_uint64(dict, "virtual_address",
	    (uint64_t)(uintptr_t)da->da_fb_vaddr);

	prop_dictionary_set_uint64(dict, "mode_callback",
	    (uint64_t)(uintptr_t)&drmfb_genfb_mode_callback);

	/*
	 * If the DRM driver registered a backlight for this device,
	 * hand it to genfb as the brightness parameter.  genfb owns
	 * the wsdisplay ioctls and the pmf(9) brightness key events.
	 */
	sc->sc_backlight_dev = da->da_fb_helper->dev->dev;
	bd = linux_backlight_lookup(sc->sc_backlight_dev);
	if (bd != NULL) {
		backlight_force_update(bd, BACKLIGHT_UPDATE_HOTKEY);
		sc->sc_brightness.gpc_cookie = sc;
		sc->sc_brightness.gpc_get_parameter = drmfb_brightness_get;
		sc->sc_brightness.gpc_set_parameter = drmfb_brightness_set;
		sc->sc_brightness.gpc_upd_parameter = drmfb_brightness_upd;
		prop_dictionary_set_uint64(dict, "brightness_callback",
		    (uint64_t)(uintptr_t)&sc->sc_brightness);
		aprint_normal_dev(da->da_dev, "backlight %s, %d levels\n",
		    bd->bd_name, bd->props.max_brightness + 1);
		linux_backlight_release(bd);
	}

	/*
	 * Determine whether MD firmware logic has set the console to
	 * go through this device.
	 */
	if (prop_dictionary_get_bool(pdict, "is_console", &is_console)) {
		/* nothing */
	} else if (genfb_is_console() && genfb_is_enabled()) {
		is_console = true;
	} else {
		is_console = false;
	}

#if NVGA > 0
	/*
	 * Whether or not we were told to be the console, if the
	 * console was configured to go through a vga resource that we
	 * now own and that vga(4) is not going to take over, kick out
	 * the vga console before we take over as genfb console.
	 */
	if ((da->da_params->dp_is_vga_console != NULL) &&
	    (*da->da_params->dp_is_vga_console)(dev)) {
		vga_cndetach();
		if (da->da_params->dp_disable_vga)
			(*da->da_params->dp_disable_vga)(dev);
		is_console = true;
	}
#endif

#ifdef WSDISPLAY_BOOTFB_HANDOFF
	if (is_console) {
		error = wsdisplay_console_enter(da->da_dev);
		if (error != 0)
			return error;
	}
#endif
	prop_dictionary_set_bool(dict, "is_console", is_console);

	/* Make the first EDID we find available to wsfb */
	drm_connector_list_iter_begin(da->da_fb_helper->dev, &conn_iter);
	drm_client_for_each_connector_iter(connector, &conn_iter) {
		struct drm_property_blob *edid = connector->edid_blob_ptr;
		if (edid && edid->length) {
			prop_dictionary_set_data(dict, "EDID", edid->data,
			    edid->length);
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	sc->sc_genfb.sc_dev = sc->sc_da.da_dev;
	genfb_init(&sc->sc_genfb);
	genfb_ops.genfb_ioctl = drmfb_genfb_ioctl;
	genfb_ops.genfb_mmap = drmfb_genfb_mmap;
	genfb_ops.genfb_enable_polling = drmfb_genfb_enable_polling;
	genfb_ops.genfb_disable_polling = drmfb_genfb_disable_polling;
	if (da->da_params->dp_damage != NULL)
		genfb_ops.genfb_damage = drmfb_genfb_damage;

	KERNEL_LOCK(1, NULL);
	error = genfb_attach(&sc->sc_genfb, &genfb_ops);
	KERNEL_UNLOCK_ONE(NULL);
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	if (is_console)
		wsdisplay_console_exit(da->da_dev);
	if (error != 0)
		return error;
#else
	KASSERTMSG(error == 0, "genfb_attach failed, error=%d", error);
#endif

	/* Success!  */
	return 0;
}

int
drmfb_detach(struct drmfb_softc *sc, int flags)
{

	/* XXX genfb detach?  */
	return 0;
}

/*
 * genfb brightness parameter, 0..GENFB_BRIGHTNESS_MAX, mapped onto the
 * backlight device levels 0..props.max_brightness.  The backlight is
 * looked up on each call by the DRM device pointer saved at attach;
 * the DRM driver may have unregistered it.
 */
#define	GENFB_BRIGHTNESS_MAX	255

static struct backlight_device *
drmfb_backlight(struct drmfb_softc *sc)
{

	return linux_backlight_lookup(sc->sc_backlight_dev);
}

static int
drmfb_brightness_to_genfb(const struct backlight_device *bd, int level)
{
	const int64_t max = bd->props.max_brightness;
	int64_t val;

	level = MIN(MAX(level, 0), bd->props.max_brightness);
	val = ((int64_t)level * GENFB_BRIGHTNESS_MAX + max / 2) / max;
	return (int)val;
}

static int
drmfb_brightness_from_genfb(const struct backlight_device *bd, int val)
{
	const int64_t max = bd->props.max_brightness;
	int64_t level;

	val = MIN(MAX(val, 0), GENFB_BRIGHTNESS_MAX);
	level = ((int64_t)val * max + GENFB_BRIGHTNESS_MAX / 2) /
	    GENFB_BRIGHTNESS_MAX;
	return (int)level;
}

static int
drmfb_brightness_get(void *cookie, int *valp)
{
	struct drmfb_softc *const sc = cookie;
	struct backlight_device *const bd = drmfb_backlight(sc);
	int error = 0;

	if (bd == NULL)
		return ENODEV;
	if (bd->props.max_brightness <= 0)
		error = ENODEV;
	else
		*valp = drmfb_brightness_to_genfb(bd,
		    linux_backlight_get_brightness(bd));
	linux_backlight_release(bd);
	return error;
}

static int
drmfb_brightness_set(void *cookie, int val)
{
	struct drmfb_softc *const sc = cookie;
	struct backlight_device *const bd = drmfb_backlight(sc);
	int error;

	if (bd == NULL)
		return ENODEV;
	if (bd->props.max_brightness <= 0)
		error = ENODEV;
	else
		error = linux_backlight_set_brightness(bd,
		    drmfb_brightness_from_genfb(bd, val));
	linux_backlight_release(bd);
	return error;
}

/*
 * Step by delta genfb units, at least one device level, so that keys
 * move a coarse backlight too.
 */
static int
drmfb_brightness_upd(void *cookie, int delta)
{
	struct drmfb_softc *const sc = cookie;
	struct backlight_device *const bd = drmfb_backlight(sc);
	int cur, next, error;

	if (bd == NULL)
		return ENODEV;
	if (bd->props.max_brightness <= 0) {
		error = ENODEV;
		goto out;
	}
	cur = linux_backlight_get_brightness(bd);
	next = drmfb_brightness_from_genfb(bd,
	    drmfb_brightness_to_genfb(bd, cur) + delta);
	if (next == cur && delta != 0)
		next = cur + (delta > 0 ? 1 : -1);
	next = MIN(MAX(next, 0), bd->props.max_brightness);
	error = linux_backlight_set_brightness(bd, next);
out:	linux_backlight_release(bd);
	return error;
}

static int
drmfb_genfb_ioctl(void *v, void *vs, unsigned long cmd, void *data, int flag,
    struct lwp *l)
{
	struct genfb_softc *const genfb = v;
	struct drmfb_softc *const sc = container_of(genfb, struct drmfb_softc,
	    sc_genfb);
	int error;

	if (sc->sc_da.da_params->dp_ioctl) {
		error = (*sc->sc_da.da_params->dp_ioctl)(sc, cmd, data, flag,
		    l);
		if (error != EPASSTHROUGH)
			return error;
	}

	switch (cmd) {
	/*
	 * Screen blanking ioctls.  Not to be confused with backlight
	 * (can be disabled while stuff is still drawn on the screen),
	 * brightness, or contrast (which we don't support).  Backlight
	 * and brightness are done through WSDISPLAYIO_{GET,SET}PARAM.
	 * This toggles between DPMS ON and DPMS OFF; backlight toggles
	 * between DPMS ON and DPMS SUSPEND.
	 */
	case WSDISPLAYIO_GVIDEO: {
		int *onp = (int *)data;

		/* XXX Can't really determine a single answer here.  */
		*onp = 1;
		return 0;
	}
	case WSDISPLAYIO_SVIDEO: {
		const int on = *(const int *)data;
		const int dpms_mode = on? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF;
		struct drm_fb_helper *const fb_helper = sc->sc_da.da_fb_helper;

		mutex_lock(&fb_helper->lock);
		drm_client_modeset_dpms(&fb_helper->client, dpms_mode);
		mutex_unlock(&fb_helper->lock);

		return 0;
	}
	default:
		return EPASSTHROUGH;
	}
}

static paddr_t
drmfb_genfb_mmap(void *v, void *vs, off_t offset, int prot)
{
	struct genfb_softc *const genfb = v;
	struct drmfb_softc *const sc = container_of(genfb, struct drmfb_softc,
	    sc_genfb);

	KASSERT(0 <= offset);

	if (offset < genfb->sc_fbsize) {
		if (sc->sc_da.da_params->dp_mmapfb == NULL)
			return -1;
		return (*sc->sc_da.da_params->dp_mmapfb)(sc, offset, prot);
	} else {
		if (kauth_authorize_machdep(kauth_cred_get(),
			KAUTH_MACHDEP_UNMANAGEDMEM, NULL, NULL, NULL, NULL)
		    != 0)
			return -1;
		if (sc->sc_da.da_params->dp_mmap == NULL)
			return -1;
		return (*sc->sc_da.da_params->dp_mmap)(sc, offset, prot);
	}
}

static int
drmfb_genfb_enable_polling(void *cookie)
{
	struct genfb_softc *const genfb = cookie;
	struct drmfb_softc *const sc = container_of(genfb, struct drmfb_softc,
	    sc_genfb);

	return drm_fb_helper_debug_enter_fb(sc->sc_da.da_fb_helper);
}

static int
drmfb_genfb_disable_polling(void *cookie)
{
	struct genfb_softc *const genfb = cookie;
	struct drmfb_softc *const sc = container_of(genfb, struct drmfb_softc,
	    sc_genfb);

	return drm_fb_helper_debug_leave_fb(sc->sc_da.da_fb_helper);
}

static bool
drmfb_genfb_setmode(struct genfb_softc *genfb, int mode)
{
	struct drmfb_softc *sc = container_of(genfb, struct drmfb_softc,
	    sc_genfb);
	struct drm_fb_helper *fb_helper = sc->sc_da.da_fb_helper;

	if (sc->sc_da.da_params->dp_setmode != NULL)
		return sc->sc_da.da_params->dp_setmode(sc, mode);

	if (mode == WSDISPLAYIO_MODE_EMUL)
		drm_fb_helper_restore_fbdev_mode_unlocked(fb_helper);

	return true;
}

bool
drmfb_shutdown(struct drmfb_softc *sc, int flags __unused)
{

	genfb_enable_polling(sc->sc_da.da_dev);
	return true;
}
