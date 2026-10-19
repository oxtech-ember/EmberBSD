/* Origin: EmberBSD firmware framebuffer descriptor and console ownership. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_FDT_SIMPLEFBVAR_H_
#define _DEV_FDT_SIMPLEFBVAR_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/device.h>

enum simplefb_pixel_format {
	SIMPLEFB_FORMAT_A8B8G8R8,
	SIMPLEFB_FORMAT_X8R8G8B8,
	SIMPLEFB_FORMAT_R8G8B8A8,
	SIMPLEFB_FORMAT_B8G8R8X8,
	SIMPLEFB_FORMAT_X2R10G10B10,
	SIMPLEFB_FORMAT_R5G6B5,
};

/* Checked immutable firmware snapshot. No mapping or display-mode change. */
struct simplefb_framebuffer {
	bus_space_tag_t sf_bst;
	bus_addr_t sf_address;
	bus_size_t sf_size;
	size_t sf_fbsize;
	uint32_t sf_width, sf_height, sf_stride;
	enum simplefb_pixel_format sf_format;
	uint8_t sf_depth;
};

/* Shared read-only parser. Error leaves the output unchanged. */
int simplefb_node_cpu_domain(int);
int simplefb_read_node(int, bus_space_tag_t, struct simplefb_framebuffer *);
int simplefb_framebuffer_set_address(struct simplefb_framebuffer *, uint64_t);

#ifdef WSDISPLAY_BOOTFB_DEFER
struct wsdisplay_bootfb_lease;
/* Process context. The two claim outputs must be disjoint caller storage.
 * Cancel is valid only before any hardware handoff.
 * These calls do not close or drain console output, or grant device access. */
int simplefb_console_claim(struct wsdisplay_bootfb_lease **,
    struct simplefb_framebuffer *);
int simplefb_console_cancel(struct wsdisplay_bootfb_lease *);
#endif

/*
 * Boot attachment only, before config_finalize ends the reservation window.
 * reserve does not require a readable descriptor: VirtGPU replaces scanout.
 * get_framebuffer requires the current uncommitted owner. The caller retains
 * its copy after commit. The descriptor alone grants no mapping ownership.
 * release can cancel only the caller's uncommitted reservation. commit ends
 * the window and prevents the one genfb fallback. These APIs do not arbitrate
 * runtime DRM master, wsdisplay mode, or concurrent console drawing.
 */
bool simplefb_console_reserve(device_t);
int simplefb_console_get_framebuffer(device_t, struct simplefb_framebuffer *);
bool simplefb_console_release(device_t);
void simplefb_console_commit(device_t);

#endif
