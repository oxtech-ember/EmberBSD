/* Origin: EmberBSD private FDT boot framebuffer association. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_FDT_SIMPLEFB_BOOTVAR_H_
#define _DEV_FDT_SIMPLEFB_BOOTVAR_H_

#include <dev/fdt/simplefbvar.h>

/* ARM implements these only for its actual early framebuffer mapping. */
int fdtbus_bootfb_defer(device_t, int, bus_space_tag_t,
    const struct simplefb_framebuffer *, int (*)(device_t));
int fdtbus_bootfb_match(device_t, int, bus_space_tag_t,
    const struct simplefb_framebuffer *);
bool fdtbus_bootfb_cpu_map(bus_space_tag_t);

#endif
