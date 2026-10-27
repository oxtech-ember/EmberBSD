/* $NetBSD: arm_simplefb.c,v 1.13 2022/10/14 22:10:15 jmcneill Exp $ */

/*-
 * Copyright (c) 2019 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jared McNeill <jmcneill@invisible.ca>.
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

#include "pci.h"
#include "opt_pci.h"
#include "opt_vcons.h"
#include "opt_wsdisplay_earlyfb.h"
#include "opt_wsdisplay_bootfb.h"

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: arm_simplefb.c,v 1.13 2022/10/14 22:10:15 jmcneill Exp $");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/device.h>
#ifdef WSDISPLAY_EARLYFB_HOOKS
#include <sys/systm.h>
#endif

#ifdef WSDISPLAY_BOOTFB_HANDOFF
#include <sys/atomic.h>
#include <sys/kernel.h>
#include <sys/sha2.h>
#include <sys/systm.h>
#include <dev/cons_handoffvar.h>
#include <dev/fdt/bootfb_source.h>
#endif
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/simplefbvar.h>
#ifdef WSDISPLAY_BOOTFB_DEFER
#include <sys/atomic.h>
#include <sys/kernel.h>
#include <dev/fdt/simplefb_bootvar.h>
#include <dev/wscons/wsdisplay_bootfbvar.h>
#endif
#include <arm/fdt/arm_fdtvar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#include <dev/wsfont/wsfont.h>
#include <dev/wscons/wsdisplay_vconsvar.h>

#if NPCI > 0 && defined(PCI_NETBSD_CONFIGURE)
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pciconf.h>
#endif

#include <arm/fdt/arm_simplefb.h>

#include <libfdt.h>

extern struct bus_space arm_generic_bs_tag;

static struct arm_simplefb_softc {
	uint32_t	sc_width;
	uint32_t	sc_height;
	uint32_t	sc_stride;
	uint16_t	sc_depth;
	bool		sc_swapped;
	bool		sc_10bit;
	void		*sc_bits;
} arm_simplefb_softc;

static struct wsscreen_descr arm_simplefb_stdscreen = {
	.name = "std",
	.ncols = 0,
	.nrows = 0,
	.textops = NULL,
	.fontwidth = 0,
	.fontheight = 0,
	.capabilities = 0,
	.modecookie = NULL
};

static struct wsdisplay_accessops arm_simplefb_accessops;
static struct vcons_data arm_simplefb_vcons_data;
static struct vcons_screen arm_simplefb_screen;

static bus_addr_t arm_simplefb_addr;

#ifdef WSDISPLAY_BOOTFB_DEFER
/* Written only by the real preattach, then published once after replay. */
static struct {
	const void *blob;
	int phandle;
	uint64_t address, size;
	struct simplefb_framebuffer framebuffer;
	bus_space_handle_t handle;
	int flags;
	bool map_present;
	int (*map_method)(void *, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
	const struct wsscreen_descr *type;
	void *cookie;
} arm_simplefb_boot;
static unsigned int arm_simplefb_captured;
#endif

#ifdef WSDISPLAY_BOOTFB_HANDOFF
/* Source facts only. The first capture attempt is final until reboot. */
enum arm_simplefb_source_phase {
	ARM_SIMPLEFB_SOURCE_UNATTEMPTED,
	ARM_SIMPLEFB_SOURCE_UNKNOWN,
	ARM_SIMPLEFB_SOURCE_CAPTURED
};
struct arm_simplefb_source_fact {
	const void *blob;
	size_t capacity;
	size_t length;
	int chosen;
	struct bootfb_source_record record;
	uint8_t digest[SHA256_DIGEST_LENGTH];
};
static struct arm_simplefb_source_fact arm_simplefb_source_fact;
static unsigned int arm_simplefb_source_phase;

/* A change detector for trusted boot data, not an access-permission query. */
static int
arm_simplefb_source_revalidate(const struct arm_simplefb_source_fact *fact)
{
	struct bootfb_source_tree tree;
	SHA256_CTX hash;
	uint8_t digest[SHA256_DIGEST_LENGTH];
	int error;

	if (fact->blob == NULL || fact->blob != fdtbus_get_data() ||
	    fact->capacity < sizeof(struct fdt_header))
		return EINVAL;
	error = bootfb_source_scan(fact->blob, fact->capacity, &tree);
	if (error != 0)
		return error;
	if (!tree.record_present || tree.framebuffer ||
	    tree.record.producer != BOOTFB_SOURCE_EFI_ACPI64 ||
	    tree.record.disposition != BOOTFB_SOURCE_ABSENT ||
	    fact->length != fdt_totalsize(fact->blob) ||
	    fact->chosen != tree.chosen ||
	    fact->record.producer != tree.record.producer ||
	    fact->record.disposition != tree.record.disposition ||
	    fact->record.status != tree.record.status ||
	    fact->record.count != tree.record.count)
		return EINVAL;
	if (SHA256_Init(&hash) != 1 ||
	    SHA256_Update(&hash, fact->blob, fact->length) != 1 ||
	    SHA256_Final(digest, &hash) != 1)
		return EIO;
	if (memcmp(digest, fact->digest, sizeof(digest)) != 0)
		return EINVAL;
	return 0;
}

void
arm_simplefb_source_capture(const void *blob, size_t capacity)
{
	struct arm_simplefb_source_fact value = {0};
	struct bootfb_source_tree tree;
	SHA256_CTX hash;
	int error;

	if (atomic_cas_uint(&arm_simplefb_source_phase,
	    ARM_SIMPLEFB_SOURCE_UNATTEMPTED, ARM_SIMPLEFB_SOURCE_UNKNOWN) !=
	    ARM_SIMPLEFB_SOURCE_UNATTEMPTED)
		return;
	if (!cold || !KERNEL_LOCKED_P() || panicstr != NULL ||
	    cpu_intr_p() || cpu_softintr_p())
		return;
	if (blob == NULL || blob != fdtbus_get_data() ||
	    capacity < sizeof(struct fdt_header))
		return;
	error = bootfb_source_scan(blob, capacity, &tree);
	if (error != 0 || !tree.record_present || tree.framebuffer ||
	    tree.record.producer != BOOTFB_SOURCE_EFI_ACPI64 ||
	    tree.record.disposition != BOOTFB_SOURCE_ABSENT)
		return;
	value.blob = blob;
	value.capacity = capacity;
	value.length = fdt_totalsize(blob);
	value.chosen = tree.chosen;
	value.record = tree.record;
	if (SHA256_Init(&hash) != 1 ||
	    SHA256_Update(&hash, blob, value.length) != 1 ||
	    SHA256_Final(value.digest, &hash) != 1)
		return;
	if (arm_simplefb_source_revalidate(&value) != 0)
		return;
	arm_simplefb_source_fact = value;
	atomic_store_release(&arm_simplefb_source_phase,
	    ARM_SIMPLEFB_SOURCE_CAPTURED);
}
#endif

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "simple-framebuffer" },
	DEVICE_COMPAT_EOL
};

static int
arm_simplefb_find_node(void)
{
	int chosen_phandle, child;

	chosen_phandle = OF_finddevice("/chosen");
	if (chosen_phandle == -1)
		return -1;

	for (child = OF_child(chosen_phandle); child; child = OF_peer(child)) {
		if (!fdtbus_status_okay(child))
			continue;
		if (!of_compatible_match(child, compat_data))
			continue;

		return child;
	}

	return -1;
}

static void
arm_simplefb_init_screen(void *cookie, struct vcons_screen *scr,
    int existing, long *defattr)
{
	struct arm_simplefb_softc * const sc = cookie;
	struct rasops_info *ri = &scr->scr_ri;

	ri->ri_width = sc->sc_width;
	ri->ri_height = sc->sc_height;
	ri->ri_depth = sc->sc_depth;
	ri->ri_stride = sc->sc_stride;
	ri->ri_bits = sc->sc_bits;
	ri->ri_flg = RI_CENTER | RI_FULLCLEAR | RI_CLEAR;

	if (sc->sc_swapped) {
		KASSERT(ri->ri_depth == 32);
		ri->ri_rnum = ri->ri_gnum = ri->ri_bnum = 8;
		ri->ri_rpos =  8;
		ri->ri_gpos = 16;
		ri->ri_bpos = 24;
	} else if (sc->sc_10bit) {
		KASSERT(ri->ri_depth == 32);
		ri->ri_rnum = ri->ri_gnum = ri->ri_bnum = 10;
		ri->ri_rpos = 20;
		ri->ri_gpos = 10;
		ri->ri_bpos =  0;
	}

	scr->scr_flags |= VCONS_LOADFONT;
	scr->scr_flags |= VCONS_DONT_READ;

	rasops_init(ri, ri->ri_height / 8, ri->ri_width / 8);
	ri->ri_caps = WSSCREEN_WSCOLORS | WSSCREEN_HILIT | WSSCREEN_UNDERLINE;
	rasops_reconfig(ri, sc->sc_height / ri->ri_font->fontheight,
	    sc->sc_width / ri->ri_font->fontwidth);

	ri->ri_hw = scr;
}

static int
arm_simplefb_ioctl(void *v, void *vs, u_long cmd, void *data, int flag,
    lwp_t *l)
{
	return EPASSTHROUGH;
}

static paddr_t
arm_simplefb_mmap(void *v, void *vs, off_t offset, int prot)
{
	return -1;
}

static void
arm_simplefb_pollc(void *v, int on)
{
}

uint64_t
arm_simplefb_physaddr(void)
{
	return arm_simplefb_addr;
}

#ifdef WSDISPLAY_EARLYFB_HOOKS
/* This callback is synchronous. MI keeps no pointer after the wrapper returns. */
static void
arm_simplefb_replay(void *arg)
{

	vcons_replay_msgbuf(arg);
}
#endif

void
arm_simplefb_preattach(void)
{
	struct arm_simplefb_softc * const sc = &arm_simplefb_softc;
	struct rasops_info *ri = &arm_simplefb_screen.scr_ri;
	bus_space_tag_t bst = &arm_generic_bs_tag;
	bus_space_handle_t bsh;
	struct simplefb_framebuffer fb;
	bus_addr_t addr;
	bus_size_t size;
	long defattr;
	bool swapped, is_10bit;
#ifdef WSDISPLAY_EARLYFB_HOOKS
	struct wsdisplay_early_output early;
	int error;
#ifdef WSDISPLAY_BOOTFB_DEFER
	int (*map_method)(void *, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
#endif
#endif

	const int phandle = arm_simplefb_find_node();
#ifdef WSDISPLAY_EARLYFB_HOOKS
	/* This real attachment follows MI bootstrap. It must not start in panic/IRQ. */
	if (panicstr != NULL || cpu_intr_p() || cpu_softintr_p())
		return;
	if (wsdisplay_early_output_snapshot(&early) == 0)
		return;
#endif
	if (phandle == -1)
		return;

#ifdef WSDISPLAY_BOOTFB_HANDOFF
	error = cn_publication_enter();
	if (error != 0)
		return;
#endif

#ifdef WSDISPLAY_BOOTFB_DEFER
	if (simplefb_node_cpu_domain(phandle) != 0)
		goto out;
#endif
	if (simplefb_read_node(phandle, bst, &fb) != 0)
		goto out;
	addr = fb.sf_address;
	size = fb.sf_size;
	swapped = fb.sf_format == SIMPLEFB_FORMAT_R8G8B8A8 ||
	    fb.sf_format == SIMPLEFB_FORMAT_B8G8R8X8;
	is_10bit = fb.sf_format == SIMPLEFB_FORMAT_X2R10G10B10;

#ifdef WSDISPLAY_BOOTFB_DEFER
	if (!fdtbus_bootfb_cpu_map(bst))
		goto out;
	map_method = bst->bs_map;
#endif
	if (bus_space_map(bst, addr, size,
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE, &bsh) != 0)
		goto out;

	arm_simplefb_addr = addr;

	sc->sc_width = fb.sf_width;
	sc->sc_height = fb.sf_height;
	sc->sc_depth = fb.sf_depth;
	sc->sc_stride = fb.sf_stride;
	sc->sc_bits = bus_space_vaddr(bst, bsh);
	sc->sc_swapped = swapped;
	sc->sc_10bit = is_10bit;

	wsfont_init();

	arm_simplefb_accessops.ioctl = arm_simplefb_ioctl;
	arm_simplefb_accessops.mmap = arm_simplefb_mmap;
	arm_simplefb_accessops.pollc = arm_simplefb_pollc;

	vcons_earlyinit(&arm_simplefb_vcons_data, sc, &arm_simplefb_stdscreen,
	    &arm_simplefb_accessops);
	arm_simplefb_vcons_data.init_screen = arm_simplefb_init_screen;
	vcons_init_screen(&arm_simplefb_vcons_data, &arm_simplefb_screen, 1,
	    &defattr);
	arm_simplefb_screen.scr_flags |= VCONS_SCREEN_IS_STATIC;

	if (ri->ri_rows < 1 || ri->ri_cols < 1)
		goto out;

	arm_simplefb_stdscreen.nrows = ri->ri_rows;
	arm_simplefb_stdscreen.ncols = ri->ri_cols;
	arm_simplefb_stdscreen.textops = &ri->ri_ops;
	arm_simplefb_stdscreen.capabilities = ri->ri_caps;

#ifdef WSDISPLAY_EARLYFB_HOOKS
	error = wsdisplay_early_preattach(&arm_simplefb_stdscreen, ri, 0, 0,
	    defattr, arm_simplefb_replay, &arm_simplefb_screen);
#ifdef WSDISPLAY_BOOTFB_DEFER
	if (error == 0) {
		arm_simplefb_boot.blob = fdtbus_get_data();
		arm_simplefb_boot.phandle = phandle;
		arm_simplefb_boot.address = fb.sf_address;
		arm_simplefb_boot.size = fb.sf_size;
		arm_simplefb_boot.framebuffer = fb;
		arm_simplefb_boot.handle = bsh;
		arm_simplefb_boot.flags = BUS_SPACE_MAP_LINEAR |
		    BUS_SPACE_MAP_PREFETCHABLE;
		arm_simplefb_boot.map_present = true;
		arm_simplefb_boot.map_method = map_method;
		arm_simplefb_boot.type = &arm_simplefb_stdscreen;
		arm_simplefb_boot.cookie = ri;
		atomic_store_release(&arm_simplefb_captured, 1);
	}
#else
	(void)error;
#endif
#else
	wsdisplay_preattach(&arm_simplefb_stdscreen, ri, 0, 0, defattr);
	vcons_replay_msgbuf(&arm_simplefb_screen);
#endif
out:
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	cn_publication_exit();
#endif
	return;
}

#ifdef WSDISPLAY_BOOTFB_DEFER
int
fdtbus_bootfb_match(device_t self, int phandle, bus_space_tag_t bst,
    const struct simplefb_framebuffer *checked)
{
	struct simplefb_framebuffer fb;
	const struct simplefb_framebuffer *saved = &arm_simplefb_boot.framebuffer;
	int error;

	KASSERT(KERNEL_LOCKED_P());
	if (self == NULL || checked == NULL || bst == NULL)
		return EINVAL;
	if (!device_is_a(self, "genfb") ||
	    strcmp(device_cfattach(self)->ca_name, "simplefb") != 0)
		return EINVAL;
	if (atomic_load_acquire(&arm_simplefb_captured) == 0 ||
	    !arm_simplefb_boot.map_present ||
	    arm_simplefb_boot.blob != fdtbus_get_data() ||
	    arm_simplefb_boot.phandle != phandle ||
	    phandle != fdtbus_get_stdout_phandle())
		return ENXIO;
	error = simplefb_node_cpu_domain(phandle);
	if (error != 0)
		return error;
	if (!fdtbus_bootfb_cpu_map(bst) ||
	    arm_simplefb_boot.map_method != arm_generic_bs_tag.bs_map ||
	    arm_simplefb_boot.flags !=
	    (BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE))
		return EOPNOTSUPP;
	error = simplefb_read_node(phandle, bst, &fb);
	if (error != 0)
		return error;
	/* Compare fields, not padding or a visible-byte-only subrange. */
	if (checked->sf_bst != bst || fb.sf_address != arm_simplefb_boot.address ||
	    fb.sf_size != arm_simplefb_boot.size ||
	    fb.sf_address != checked->sf_address || fb.sf_size != checked->sf_size ||
	    fb.sf_fbsize != saved->sf_fbsize || fb.sf_fbsize != checked->sf_fbsize ||
	    fb.sf_width != saved->sf_width || fb.sf_width != checked->sf_width ||
	    fb.sf_height != saved->sf_height || fb.sf_height != checked->sf_height ||
	    fb.sf_stride != saved->sf_stride || fb.sf_stride != checked->sf_stride ||
	    fb.sf_format != saved->sf_format || fb.sf_format != checked->sf_format ||
	    fb.sf_depth != saved->sf_depth || fb.sf_depth != checked->sf_depth)
		return EINVAL;
	return 0;
}

int
fdtbus_bootfb_defer(device_t self, int phandle, bus_space_tag_t bst,
    const struct simplefb_framebuffer *checked, int (*fallback)(device_t))
{
	int error;

	KASSERT(KERNEL_LOCKED_P());
	if (!cold)
		return EBUSY;
	error = fdtbus_bootfb_match(self, phandle, bst, checked);
	if (error != 0)
		return error;
	return wsdisplay_bootfb_defer(self, arm_simplefb_boot.type,
	    arm_simplefb_boot.cookie, fallback);
}
#endif
