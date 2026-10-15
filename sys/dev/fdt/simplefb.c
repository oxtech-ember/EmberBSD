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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: simplefb.c,v 1.15 2021/08/30 22:47:24 jmcneill Exp $");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <dev/fdt/simplefbvar.h>

#include <dev/wsfb/genfbvar.h>

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

static struct simplefb_softc *simplefb_deferred;
static device_t simplefb_owner;
static bool simplefb_committed;

/* This snapshot is valid for the existing genfb signed arithmetic. */
static int
simplefb_read_framebuffer(struct simplefb_softc *sc,
    struct simplefb_framebuffer *framebuffer)
{
	struct simplefb_framebuffer fb = { .sf_bst = sc->sc_bst };
	prop_dictionary_t dict = device_properties(sc->sc_gen.sc_dev);
	const int phandle = sc->sc_phandle;
	const char *format;
	uint64_t address, span;

	if (fdtbus_get_reg(phandle, 0, &fb.sf_address, &fb.sf_size) != 0 ||
	    fb.sf_size == 0)
		return ENXIO;
	if (of_getprop_uint32(phandle, "width", &fb.sf_width) != 0 ||
	    of_getprop_uint32(phandle, "height", &fb.sf_height) != 0 ||
	    of_getprop_uint32(phandle, "stride", &fb.sf_stride) != 0 ||
	    (format = fdtbus_get_string(phandle, "format")) == NULL)
		return ENXIO;

	if (strcmp(format, "a8b8g8r8") == 0)
		fb.sf_format = SIMPLEFB_FORMAT_A8B8G8R8;
	else if (strcmp(format, "x8r8g8b8") == 0)
		fb.sf_format = SIMPLEFB_FORMAT_X8R8G8B8;
	else if (strcmp(format, "r8g8b8a8") == 0)
		fb.sf_format = SIMPLEFB_FORMAT_R8G8B8A8;
	else if (strcmp(format, "b8g8r8x8") == 0)
		fb.sf_format = SIMPLEFB_FORMAT_B8G8R8X8;
	else if (strcmp(format, "x2r10g10b10") == 0)
		fb.sf_format = SIMPLEFB_FORMAT_X2R10G10B10;
	else if (strcmp(format, "r5g6b5") == 0)
		fb.sf_format = SIMPLEFB_FORMAT_R5G6B5;
	else
		return EINVAL;
	fb.sf_depth = fb.sf_format == SIMPLEFB_FORMAT_R5G6B5 ? 16 : 32;

	if (fb.sf_width == 0 || fb.sf_height == 0 || fb.sf_stride == 0 ||
	    fb.sf_width > INT_MAX || fb.sf_height > INT_MAX ||
	    fb.sf_stride > INT_MAX ||
	    (uint64_t)fb.sf_width * (fb.sf_depth / 8) > fb.sf_stride)
		return EINVAL;
	span = (uint64_t)fb.sf_stride * fb.sf_height;
	if (span > INT_MAX || span > fb.sf_size)
		return EINVAL;
	fb.sf_fbsize = (size_t)span;

	/* MD attachment may replace the firmware physical address. */
	if (prop_dictionary_get_uint64(dict, "simplefb-physaddr", &address) &&
	    address != 0) {
		if ((uint64_t)(bus_addr_t)address != address)
			return EOVERFLOW;
		fb.sf_address = (bus_addr_t)address;
	}
	/* bus_space_map rounds the exclusive end up to a page boundary. */
	if (fb.sf_size > (bus_addr_t)-1 - fb.sf_address ||
	    fb.sf_address + fb.sf_size > (bus_addr_t)-1 - PAGE_MASK)
		return EOVERFLOW;

	*framebuffer = fb;
	return 0;
}

bool
simplefb_console_reserve(device_t owner)
{
	if (owner == NULL || simplefb_deferred == NULL ||
	    simplefb_owner != NULL || simplefb_committed)
		return false;

	simplefb_owner = owner;
	return true;
}

int
simplefb_console_get_framebuffer(device_t owner,
    struct simplefb_framebuffer *framebuffer)
{
	if (framebuffer == NULL)
		return EINVAL;
	if (owner == NULL || simplefb_deferred == NULL ||
	    simplefb_owner != owner || simplefb_committed)
		return EPERM;
	if (simplefb_deferred->sc_framebuffer_error != 0)
		return simplefb_deferred->sc_framebuffer_error;
	*framebuffer = simplefb_deferred->sc_framebuffer;
	return 0;
}

bool
simplefb_console_release(device_t owner)
{
	if (owner == NULL || simplefb_deferred == NULL ||
	    simplefb_owner != owner || simplefb_committed)
		return false;
	simplefb_owner = NULL;
	return true;
}

void
simplefb_console_commit(device_t owner)
{
	if (owner == NULL || simplefb_deferred == NULL ||
	    simplefb_owner != owner || simplefb_committed) {
		KASSERTMSG(false, "invalid firmware console commit");
		return;
	}
	simplefb_committed = true;
}

/* config_finalize runs after every config_interrupts job has completed. */
static int simplefb_attach_genfb(struct simplefb_softc *);
#ifdef VIRTGPU_CONSOLE
static int
simplefb_console_fallback(device_t dev)
{
	struct simplefb_softc *sc = device_private(dev);

	if (simplefb_deferred == NULL || simplefb_deferred != sc)
		return 0;
	simplefb_deferred = NULL;
	simplefb_owner = NULL;
	if (!simplefb_committed)
		(void)simplefb_attach_genfb(sc);
	return 0;
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
	if (fb->sf_format == SIMPLEFB_FORMAT_R8G8B8A8 ||
	    fb->sf_format == SIMPLEFB_FORMAT_B8G8R8X8)
		prop_dictionary_set_bool(dict, "is_swapped", true);
	if (fb->sf_format == SIMPLEFB_FORMAT_X2R10G10B10)
		prop_dictionary_set_bool(dict, "is_10bit", true);

	if (bus_space_map(sc->sc_bst, addr, size,
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE,
	    &sc->sc_bsh) != 0) {
		aprint_error(": failed to map fb\n");
		return ENXIO;
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
		return ENXIO;
	}

	aprint_naive("\n");
	aprint_normal(": Simple Framebuffer (%ux%u %u-bpp @ 0x%" PRIxBUSADDR ")\n",
	    width, height, depth, addr);

	pmf_device_register1(dev, NULL, NULL, simplefb_shutdown);

	memset(&ops, 0, sizeof(ops));
	ops.genfb_ioctl = simplefb_ioctl;
	ops.genfb_mmap = simplefb_mmap;

#ifdef WSDISPLAY_MULTICONS
	const bool is_console = true;
	genfb_cnattach();
#else
	const bool is_console = sc->sc_phandle == simplefb_console_phandle;
	if (is_console)
		aprint_normal_dev(sc->sc_gen.sc_dev,
		    "switching to framebuffer console\n");
#endif

	prop_dictionary_set_bool(dict, "is_console", is_console);

	genfb_attach(&sc->sc_gen, &ops);

	return 0;
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
	sc->sc_framebuffer_error = simplefb_read_framebuffer(sc,
	    &sc->sc_framebuffer);

#ifdef VIRTGPU_CONSOLE
	/* Keep the early arm renderer until exactly one final attachment. */
	if (phandle == simplefb_console_phandle && simplefb_deferred == NULL) {
		simplefb_deferred = sc;
		if (config_finalize_register(self, simplefb_console_fallback) == 0) {
			aprint_normal(": reserving early firmware console\n");
			return;
		}
		simplefb_deferred = NULL;
	}
#endif
	if (simplefb_attach_genfb(sc) != 0)
		return;

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
