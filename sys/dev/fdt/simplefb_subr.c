/* Origin: EmberBSD checked FDT framebuffer descriptor reader. */
/* Parser split from simplefb.c; its original notice follows. */
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
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <dev/fdt/simplefbvar.h>

/* Only the native EFI /chosen empty-ranges path proves this CPU domain. */
int
simplefb_node_cpu_domain(int phandle)
{
	const int chosen = OF_finddevice("/chosen");
	const int root = OF_finddevice("/");

	if (chosen <= 0 || root <= 0 || OF_parent(phandle) != chosen ||
	    OF_parent(chosen) != root || OF_getproplen(chosen, "ranges") != 0)
		return EOPNOTSUPP;
	return 0;
}

/* bus_space_map rounds its exclusive physical end in native address types. */
static int
simplefb_check_range(uint64_t address, uint64_t size)
{
	uint64_t end, rounded;
	const uint64_t mask = (uint64_t)PAGE_SIZE - 1;

	if (size == 0 || size > UINT64_MAX - address)
		return EOVERFLOW;
	if ((uint64_t)(bus_addr_t)address != address ||
	    (uint64_t)(paddr_t)address != address ||
	    (uint64_t)(bus_size_t)size != size || (uint64_t)(size_t)size != size)
		return EOVERFLOW;
	end = address + size;
	if (end > UINT64_MAX - mask)
		return EOVERFLOW;
	rounded = (end + mask) & ~mask;
	if (rounded > (uint64_t)(bus_addr_t)-1 ||
	    rounded > (uint64_t)(paddr_t)-1 ||
	    rounded - (address & ~mask) > (uint64_t)(size_t)-1)
		return EOVERFLOW;
	return 0;
}

int
simplefb_framebuffer_set_address(struct simplefb_framebuffer *fb,
    uint64_t address)
{
	int error;

	if (fb == NULL)
		return EINVAL;
	error = simplefb_check_range(address, fb->sf_size);
	if (error != 0)
		return error;
	fb->sf_address = (bus_addr_t)address;
	return 0;
}

int
simplefb_read_node(int phandle, bus_space_tag_t bst,
    struct simplefb_framebuffer *out)
{
	struct simplefb_framebuffer fb = { .sf_bst = bst };
	const char *format;
	uint64_t address, size, span;
	int parent, address_cells, size_cells, length, error;

	if (out == NULL || bst == NULL || phandle <= 0)
		return EINVAL;
	parent = OF_parent(phandle);
	if (parent <= 0)
		return ENXIO;
	address_cells = fdtbus_get_addr_cells(parent);
	size_cells = fdtbus_get_size_cells(parent);
	/* Reject unsupported schemas before get_reg64 can read cells or panic. */
	if ((address_cells != 1 && address_cells != 2) ||
	    (size_cells != 1 && size_cells != 2))
		return EINVAL;
	length = OF_getproplen(phandle, "reg");
	if (length != (address_cells + size_cells) * 4)
		return EINVAL;
	error = fdtbus_get_reg64(phandle, 0, &address, &size);
	if (error != 0)
		return error;
	error = simplefb_check_range(address, size);
	if (error != 0)
		return error;
	fb.sf_address = (bus_addr_t)address;
	fb.sf_size = (bus_size_t)size;
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
	if (span > INT_MAX || span > size)
		return EINVAL;
	fb.sf_fbsize = (size_t)span;
	*out = fb;
	return 0;
}
