/* Origin: EmberBSD firmware framebuffer descriptor and console ownership. */
/* $NetBSD: simplefb.c,v 1.15 2021/08/30 22:47:24 jmcneill Exp $ */

/*-
 * Copyright (c) 2017 Jared McNeill <jmcneill@invisible.ca>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_wsdisplay_compat.h"
#include "opt_virtgpu_console.h"
#include "opt_wsdisplay_bootfb.h"

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: simplefb.c,v 1.15 2021/08/30 22:47:24 jmcneill Exp $");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <dev/fdt/simplefbvar.h>
#include <dev/wscons/wsdisplay_bootfbvar.h>
#ifdef WSDISPLAY_BOOTFB_DEFER
#include <dev/fdt/simplefb_bootvar.h>
#endif

#include <dev/wsfb/genfbvar.h>
#ifdef WSDISPLAY_BOOTFB_HANDOFF
#include <dev/wscons/wsdisplayvar.h>
#endif

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "simple-framebuffer" },
	DEVICE_COMPAT_EOL
};

struct simplefb_softc {
	struct genfb_softc sc_gen;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	int sc_phandle;

	bus_addr_t sc_paddr;
	struct simplefb_framebuffer sc_framebuffer;
	int sc_framebuffer_error;
};

static int simplefb_console_phandle = -1;

/* This snapshot is valid for the existing genfb signed arithmetic. */
static int
simplefb_read_framebuffer(struct simplefb_softc *sc,
    struct simplefb_framebuffer *framebuffer)
{
	struct simplefb_framebuffer fb;
	prop_dictionary_t dict = device_properties(sc->sc_gen.sc_dev);
	uint64_t address;
	int error;

	error = simplefb_read_node(sc->sc_phandle, sc->sc_bst, &fb);
	if (error != 0)
		return error;

	/* MD attachment may replace the firmware physical address. */
	if (prop_dictionary_get_uint64(dict, "simplefb-physaddr", &address) &&
	    address != 0) {
		error = simplefb_framebuffer_set_address(&fb, address);
		if (error != 0)
			return error;
	}

	*framebuffer = fb;
	return 0;
}

bool
simplefb_console_reserve(device_t owner)
{
	bool reserved;

	KERNEL_LOCK(1, NULL);
	reserved = wsdisplay_bootfb_reserve(owner);
	KERNEL_UNLOCK_ONE(NULL);
	return reserved;
}

int
simplefb_console_get_framebuffer(device_t owner,
    struct simplefb_framebuffer *framebuffer)
{
	struct simplefb_softc *sc;
	device_t dev;
	int error;

	if (framebuffer == NULL)
		return EINVAL;
	KERNEL_LOCK(1, NULL);
	dev = wsdisplay_bootfb_legacy_device(owner);
	if (dev == NULL) {
		error = EPERM;
	} else {
		sc = device_private(dev);
		error = sc->sc_framebuffer_error;
		if (error == 0)
			*framebuffer = sc->sc_framebuffer;
	}
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}

bool
simplefb_console_release(device_t owner)
{
	bool released;

	KERNEL_LOCK(1, NULL);
	released = wsdisplay_bootfb_release(owner);
	KERNEL_UNLOCK_ONE(NULL);
	return released;
}

void
simplefb_console_commit(device_t owner)
{
	KERNEL_LOCK(1, NULL);
	wsdisplay_bootfb_commit(owner);
	KERNEL_UNLOCK_ONE(NULL);
}

/* MI sets FALLBACK_STARTING before this built-in callback can have effects. */
static int simplefb_attach_genfb(struct simplefb_softc *);
static int
simplefb_console_fallback(device_t dev)
{
	return simplefb_attach_genfb(device_private(dev));
}

#ifdef WSDISPLAY_BOOTFB_DEFER
int
simplefb_console_claim(struct wsdisplay_bootfb_lease **out,
    struct simplefb_framebuffer *framebuffer)
{
	struct simplefb_softc *sc;
	struct wsdisplay_bootfb_lease *lease;
	device_t dev;
	int error;

	if (out == NULL || framebuffer == NULL)
		return EINVAL;
	if (panicstr != NULL || cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	KERNEL_LOCK(1, NULL);
	dev = wsdisplay_bootfb_native_device();
	if (dev == NULL) {
		error = ENXIO;
	} else {
		sc = device_private(dev);
		error = fdtbus_bootfb_match(dev, sc->sc_phandle, sc->sc_bst,
		    &sc->sc_framebuffer);
		if (error == 0)
			error = wsdisplay_bootfb_claim(dev, &lease);
		if (error == 0) {
			*framebuffer = sc->sc_framebuffer;
			*out = lease;
		}
	}
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}

int
simplefb_console_cancel(struct wsdisplay_bootfb_lease *lease)
{
	struct simplefb_softc *sc;
	device_t dev;
	int error;

	if (panicstr != NULL || cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	KERNEL_LOCK(1, NULL);
	dev = wsdisplay_bootfb_lease_device(lease);
	if (dev == NULL) {
		error = EPERM;
	} else {
		sc = device_private(dev);
		error = fdtbus_bootfb_match(dev, sc->sc_phandle, sc->sc_bst,
		    &sc->sc_framebuffer);
		if (error != 0)
			wsdisplay_bootfb_retain(lease);
		else
			error = wsdisplay_bootfb_cancel(lease);
	}
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}
#endif

static bool
simplefb_shutdown(device_t self, int flags)
{
	genfb_enable_polling(self);
	return true;
}

static int
simplefb_ioctl(void *v, void *vs, u_long cmd, void *data, int flag, lwp_t *l)
{
	struct simplefb_softc * const sc = v;
	struct wsdisplayio_bus_id *busid;
	struct wsdisplayio_fbinfo *fbi;
	struct rasops_info *ri;
	u_int video;
	int error;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_GENFB;
		return 0;
	case WSDISPLAYIO_GET_BUSID:
		busid = data;
		busid->bus_type = WSDISPLAYIO_BUS_SOC;
		return 0;
	case WSDISPLAYIO_GET_FBINFO:
		fbi = data;
		ri = &sc->sc_gen.vd.active->scr_ri;
		error = wsdisplayio_get_fbinfo(ri, fbi);
		if (error == 0) {
	                /*
	                 * XXX
	                 * if the fb isn't page aligned, tell wsfb to skip the
			 * unaligned part
			 */
			fbi->fbi_fboffset = sc->sc_paddr & PAGE_MASK;
			fbi->fbi_flags |= WSFB_VRAM_IS_RAM;
		}
		return error;
	case WSDISPLAYIO_SVIDEO:
		video = *(u_int *)data;
		if (video == WSDISPLAYIO_VIDEO_OFF)
			pmf_event_inject(NULL, PMFE_DISPLAY_OFF);
		else if (video == WSDISPLAYIO_VIDEO_ON)
			pmf_event_inject(NULL, PMFE_DISPLAY_ON);
		else
			return EINVAL;
		return 0;
	default:
		return EPASSTHROUGH;
	}
}

static paddr_t
simplefb_mmap(void *v, void *vs, off_t off, int prot)
{
	struct simplefb_softc * const sc = v;

	if (off < 0 || off >= (sc->sc_paddr & PAGE_MASK) +
	    sc->sc_gen.sc_fbsize)
		return -1;

	return bus_space_mmap(sc->sc_bst, trunc_page(sc->sc_paddr), off, prot,
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE);
}

static int
simplefb_attach_genfb(struct simplefb_softc *sc)
{
	device_t dev = sc->sc_gen.sc_dev;
	prop_dictionary_t dict = device_properties(dev);
	struct genfb_ops ops;
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	int error;
#ifdef WSDISPLAY_MULTICONS
	const bool is_console = true;
#else
	const bool is_console = sc->sc_phandle == simplefb_console_phandle;
#endif
#endif
	const struct simplefb_framebuffer *fb = &sc->sc_framebuffer;
	const uint32_t width = fb->sf_width, height = fb->sf_height;
	const uint32_t stride = fb->sf_stride;
	const uint8_t depth = fb->sf_depth;
	const bus_addr_t addr = fb->sf_address;
	const bus_size_t size = fb->sf_size;

	if (sc->sc_framebuffer_error != 0) {
		aprint_error(": invalid firmware framebuffer (%d)\n",
		    sc->sc_framebuffer_error);
		return sc->sc_framebuffer_error;
	}
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	if (is_console) {
		error = wsdisplay_console_enter(dev);
		if (error != 0)
			return error;
	}
	prop_dictionary_set_bool(dict, "is_console", is_console);
#endif
	if (fb->sf_format == SIMPLEFB_FORMAT_R8G8B8A8 ||
	    fb->sf_format == SIMPLEFB_FORMAT_B8G8R8X8)
		prop_dictionary_set_bool(dict, "is_swapped", true);
	if (fb->sf_format == SIMPLEFB_FORMAT_X2R10G10B10)
		prop_dictionary_set_bool(dict, "is_10bit", true);

	if (bus_space_map(sc->sc_bst, addr, size,
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE,
	    &sc->sc_bsh) != 0) {
		aprint_error(": failed to map fb\n");
#ifdef WSDISPLAY_BOOTFB_HANDOFF
		error = ENXIO;
		goto out;
#else
		return ENXIO;
#endif
	}

	sc->sc_paddr = addr;

	prop_dictionary_set_uint32(dict, "width", width);
	prop_dictionary_set_uint32(dict, "height", height);
	prop_dictionary_set_uint8(dict, "depth", depth);
	prop_dictionary_set_uint32(dict, "linebytes", stride);
	prop_dictionary_set_uint64(dict, "address", addr);
	prop_dictionary_set_uint64(dict, "virtual_address",
	    (uintptr_t)bus_space_vaddr(sc->sc_bst, sc->sc_bsh));

	genfb_init(&sc->sc_gen);

	if (sc->sc_gen.sc_width == 0 || sc->sc_gen.sc_fbsize == 0) {
		aprint_normal(": disabled\n");
#ifdef WSDISPLAY_BOOTFB_HANDOFF
		error = ENXIO;
		goto out;
#else
		return ENXIO;
#endif
	}

	aprint_naive("\n");
	aprint_normal(": Simple Framebuffer (%ux%u %u-bpp @ 0x%" PRIxBUSADDR ")\n",
	    width, height, depth, addr);

	pmf_device_register1(dev, NULL, NULL, simplefb_shutdown);

	memset(&ops, 0, sizeof(ops));
	ops.genfb_ioctl = simplefb_ioctl;
	ops.genfb_mmap = simplefb_mmap;

#ifdef WSDISPLAY_MULTICONS
#ifndef WSDISPLAY_BOOTFB_HANDOFF
	const bool is_console = true;
#endif
	genfb_cnattach();
#else
#ifndef WSDISPLAY_BOOTFB_HANDOFF
	const bool is_console = sc->sc_phandle == simplefb_console_phandle;
#endif
	if (is_console)
		aprint_normal_dev(sc->sc_gen.sc_dev,
		    "switching to framebuffer console\n");
#endif

	prop_dictionary_set_bool(dict, "is_console", is_console);

#ifdef WSDISPLAY_BOOTFB_HANDOFF
	error = genfb_attach(&sc->sc_gen, &ops);
out:
	if (is_console)
		wsdisplay_console_exit(dev);
	return error;
#else
	genfb_attach(&sc->sc_gen, &ops);
	return 0;
#endif
}

static int
simplefb_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
simplefb_attach(device_t parent, device_t self, void *aux)
{
	struct simplefb_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;

	sc->sc_gen.sc_dev = self;
	sc->sc_phandle = phandle;
	sc->sc_bst = faa->faa_bst;
#ifdef WSDISPLAY_BOOTFB_DEFER
	/* Prove this native CPU domain before get_reg64 decodes any ranges. */
	sc->sc_framebuffer_error = phandle == simplefb_console_phandle ?
	    simplefb_node_cpu_domain(phandle) : 0;
	if (sc->sc_framebuffer_error == 0)
#endif
		sc->sc_framebuffer_error = simplefb_read_framebuffer(sc,
		    &sc->sc_framebuffer);

	if (phandle == simplefb_console_phandle) {
#ifdef WSDISPLAY_BOOTFB_DEFER
		if (sc->sc_framebuffer_error == 0 &&
		    fdtbus_bootfb_defer(self, phandle, sc->sc_bst,
		    &sc->sc_framebuffer, simplefb_console_fallback) == 0)
			return;
#endif
#ifdef VIRTGPU_CONSOLE
		/* Legacy reservation deliberately permits an unreadable descriptor. */
		if (wsdisplay_bootfb_register_legacy(self,
		    simplefb_console_fallback) == 0)
			return;
#endif
		(void)wsdisplay_bootfb_fallback(self, simplefb_console_fallback);
		return;
	}
	(void)simplefb_attach_genfb(sc);

}

CFATTACH_DECL_NEW(simplefb, sizeof(struct simplefb_softc),
	simplefb_match, simplefb_attach, NULL, NULL);

static int
simplefb_console_match(int phandle)
{
	return of_compatible_match(phandle, compat_data);
}

static void
simplefb_console_consinit(struct fdt_attach_args *faa, u_int uart_freq)
{
	simplefb_console_phandle = faa->faa_phandle;
	genfb_cnattach();
}

static const struct fdt_console simplefb_fdt_console = {
	.match = simplefb_console_match,
	.consinit = simplefb_console_consinit
};

FDT_CONSOLE(simplefb, &simplefb_fdt_console);
