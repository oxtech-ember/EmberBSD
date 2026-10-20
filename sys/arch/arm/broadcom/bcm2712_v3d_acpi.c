/* Origin: EmberBSD BCM2712 V3D passive identification, 2026-10-09. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Register facts: raspberrypi/linux 43c132e8863c3bff3647033b6a7d2bf87b15501c,
 * v3d_regs.h, v3d_drv.c and bcm2712-ds.dtsi. ACPI layout: eotics-com/
 * edk2-platforms c4b5d05de1f2ef633bdb4c175b5c118fcb2666ed, RPi5 Dsdt.asl.
 * This observer never enables clocks, changes power/reset/SMS, or submits
 * work. An unready firmware handoff ends the probe. No DRM device is exposed.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include "opt_bcmv3d.h"
#include "opt_bcm2712_display_components.h"
#include "opt_wsdisplay_bootfb.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/pserialize.h>
#include <sys/systm.h>

#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <arm/broadcom/bcm2835_mbox.h>
#include <arm/broadcom/bcm2835_pmwdogvar.h>
#include <arch/evbarm/rpi/vcprop.h>
#include <arch/evbarm/rpi/vcio.h>

/* Each experiment owns the same MMU and interrupt state until reboot. */
#if (defined(BCM2712_V3D_DMA_PROBE) || defined(BCM2712_V3D_QUEUE_PROBE) || \
    defined(BCM2712_V3D_IRQ_PROBE) || defined(BCM2712_V3D_FAULT_PROBE)) && \
    !defined(BCM2712_V3D_TAKEOVER)
#error "V3D experiments require BCM2712_V3D_TAKEOVER"
#endif
#if defined(BCM2712_V3D_QUEUE_PROBE) && !defined(BCM2712_V3D_DMA_PROBE)
#error "BCM2712_V3D_QUEUE_PROBE requires BCM2712_V3D_DMA_PROBE"
#endif
#if (defined(BCM2712_V3D_IRQ_PROBE) || defined(BCM2712_V3D_FAULT_PROBE)) && \
    (defined(BCM2712_V3D_DMA_PROBE) || defined(BCM2712_V3D_QUEUE_PROBE))
#error "V3D polling and interrupt experiments cannot share one boot"
#endif
#if defined(BCM2712_V3D_IRQ_PROBE) && defined(BCM2712_V3D_FAULT_PROBE)
#error "V3D IRQ and fault experiments cannot share one boot"
#endif

#ifdef BCM2712_V3D_TAKEOVER
int bcmv3d_takeover_probe(device_t, bus_space_tag_t);
#endif
#if defined(BCM2712_V3D_RENDER) && !defined(BCM2712_V3D_TAKEOVER)
#error "V3D render requires BCM2712_V3D_TAKEOVER"
#endif
#if defined(BCM2712_V3D_RENDER) && (defined(BCM2712_V3D_DMA_PROBE) || \
    defined(BCM2712_V3D_QUEUE_PROBE) || defined(BCM2712_V3D_IRQ_PROBE) || \
    defined(BCM2712_V3D_FAULT_PROBE))
#error "V3D render and boot probes cannot share one boot"
#endif
#ifdef BCM2712_V3D_RENDER
#include <arm/broadcom/bcm2712_v3d_render.h>
#endif
#if defined(BCM2712_V3D_RENDER) && defined(WSDISPLAY_BOOTFB_DEFER)
#include <dev/fdt/simplefbvar.h>
#include <dev/wscons/wsdisplay_bootfbvar.h>
#ifdef WSDISPLAY_BOOTFB_HANDOFF
#include <arm/broadcom/bcm2712_display_handoffvar.h>
#include <arm/broadcom/bcm2712_display_hsm.h>
#include <arm/broadcom/bcm2712_display_bvb.h>
#include <arm/broadcom/bcm2712_display_core.h>
#include <arm/broadcom/bcm2712_display_disp.h>
#include <arm/broadcom/bcm2712_display_quiesce.h>
#include <external/bsd/drm2/bcmv3d/bcmv3d_mmu4_native.h>
#include <external/bsd/drm2/bcmv3d/bcmv3d_hvs_publish.h>
#include <external/bsd/drm2/bcmv3d/bcmv3d_vblank_native.h>
#include <external/bsd/drm2/bcmv3d/bcmv3d_mmu4_check.h>
#ifndef BCM2712_DISPLAY_COMPONENTS
#error "BCM2712 native handoff requires the builtin display components"
#endif
#endif
#endif
#if defined(BCM2712_V3D_TAKEOVER) && defined(BCM2712_V3D_DMA_PROBE)
int bcmv3d_dma_probe(device_t, bus_dma_tag_t);
#endif
#if defined(BCM2712_V3D_TAKEOVER) && defined(BCM2712_V3D_QUEUE_PROBE)
int bcmv3d_queue_probe(device_t, bus_dma_tag_t);
#endif
#if defined(BCM2712_V3D_TAKEOVER) && defined(BCM2712_V3D_IRQ_PROBE)
int bcmv3d_irq_probe(device_t, bus_dma_tag_t, ACPI_HANDLE);
#endif
#if defined(BCM2712_V3D_TAKEOVER) && defined(BCM2712_V3D_FAULT_PROBE)
int bcmv3d_fault_probe(device_t, bus_dma_tag_t, ACPI_HANDLE);
#endif

#define BCMV3D_HUB	0
#define BCMV3D_CORE	1
#define BCMV3D_SMS	2
#define BCMV3D_NREG	3
#define BCMV3D_RESET_N	__BIT(6)
#define BCMV3D_SMS_TEE	0x400
#define BCMV3D_SMS_STATE_MASK	0x0f
#define BCMV3D_SMS_OLD_MODE_MASK	0x30
#define BCMV3D_SMS_NEW_MODE_MASK	0xc0
#define BCMV3D_HUB_IDENT1	0x0c
#define BCMV3D_HUB_IDENT3	0x14
#define BCMV3D_MMU_DEBUG	0x1238

static const bus_addr_t bcmv3d_addresses[BCMV3D_NREG] = {
	UINT64_C(0x1002000000), UINT64_C(0x1002008000),
	UINT64_C(0x1002030800)
};
static const bus_size_t bcmv3d_lengths[BCMV3D_NREG] = {
	0x4000, 0x6000, 0x700
};

enum bcmv3d_clock_status {
	BCMV3D_CLOCK_NOT_QUERIED,
	BCMV3D_CLOCK_VALID,
	BCMV3D_CLOCK_TRANSPORT_ERROR,
	BCMV3D_CLOCK_RESPONSE_ERROR,
	BCMV3D_CLOCK_INVALID_TAG
};

struct bcmv3d_clock_result {
	enum bcmv3d_clock_status status;
	int error;
	uint32_t value;
};

#if defined(BCM2712_V3D_RENDER) && defined(WSDISPLAY_BOOTFB_DEFER)
enum bcmv3d_bootfb_phase {
	BCMV3D_BOOTFB_IDLE,
	BCMV3D_BOOTFB_QUEUED,
	BCMV3D_BOOTFB_REFUSED,
	BCMV3D_BOOTFB_CLAIMED,
	BCMV3D_BOOTFB_CANCELED,
	BCMV3D_BOOTFB_RETAINED,
	BCMV3D_BOOTFB_CLOSED
};

#ifdef WSDISPLAY_BOOTFB_HANDOFF
/* One fixed boot continuation. All providers and mappings survive every fault. */
struct bcmv3d_display_stop {
	struct bcm2712_hsm_provider *hsm;
	struct bcm2712_bvb_provider *bvb;
	struct bcm2712_core_provider *core;
	struct bcm2712_disp_provider *disp;
	struct bcm2712_hsm_status hsm_status;
	struct bcm2712_bvb_status bvb_status;
	struct bcm2712_core_status core_status;
	struct bcm2712_disp_status disp_status;
	struct bcm2712_display_contract contract;
	bus_space_handle_t maps[BCM2712_QUIESCE_REGIONS];
	unsigned int mapped;
	struct bcm2712_quiesce_status stop;
	const char *stage;
	bool attempted, complete;
	int fault;
};
#endif

#ifdef WSDISPLAY_BOOTFB_HANDOFF
struct bcmv3d_bootfb_mmu4 {
	struct bcmv3d_mmu4_native *native;
	struct bcmv3d_mmu4_native_status status;
	bus_space_handle_t writeback_maps[2];
	uint32_t writeback_control[2];
	unsigned int writeback_mapped;
	bool writeback_valid[2];
	const char *stage;
	bool attempted, complete, status_valid;
	int fault, status_error;
};
#endif
#ifdef WSDISPLAY_BOOTFB_HANDOFF
struct bcmv3d_bootfb_publish {
	struct bcmv3d_hvs_publish_status status;
	struct bcm2712_hvs_frame frame;
	bus_space_handle_t maps[BCMV3D_PUBLISH_WINDOWS];
	uint32_t mapped, cob[3], writeback[2], progress[2], cxm_words;
	unsigned int dlist_first, dlist_words;
	bool writeback_valid[2], progress_valid[2], cxm_valid;
	bool attempted, complete, executing;
	int fault, io_error, progress_error;
};
#endif

/* The ACPI device and this embedded owner remain resident until reboot. */

struct bcmv3d_bootfb_owner {
	struct wsdisplay_bootfb_lease *lease;
	struct simplefb_framebuffer framebuffer;
	enum bcmv3d_bootfb_phase phase;
	int first_error, cancel_error;
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	struct bcm2712_display_handoff_status firmware;
	struct bcmv3d_display_stop display;
	struct bcmv3d_device *renderer;
	struct bcmv3d_bootfb_mmu4 mmu4;
	struct bcmv3d_bootfb_publish publish;
	struct bcmv3d_mmu4_check_status range_check;
#endif
};
#endif

struct bcmv3d_softc {
	device_t sc_dev;
	bus_space_tag_t sc_bst;
	bus_dma_tag_t sc_dmat;
	ACPI_HANDLE sc_handle;
	const char *sc_stage;
	struct bcmv3d_clock_result sc_clock_state, sc_clock_rate;
	uint32_t sc_pm;
	int sc_pm_error;
	bool sc_pm_valid;
	uint32_t sc_sms[2], sc_ident1, sc_ident3, sc_core_ident0, sc_mmu;
	bool sc_observed;
#if defined(BCM2712_V3D_RENDER) && defined(WSDISPLAY_BOOTFB_DEFER)
	struct bcmv3d_bootfb_owner sc_bootfb;
#endif
};

static int bcmv3d_match(device_t, cfdata_t, void *);
static void bcmv3d_attach(device_t, device_t, void *);
static int bcmv3d_finalize(device_t);

CFATTACH_DECL_NEW(bcmv3d_acpi, sizeof(struct bcmv3d_softc),
    bcmv3d_match, bcmv3d_attach, NULL, NULL);

#if defined(BCM2712_V3D_RENDER) && defined(WSDISPLAY_BOOTFB_DEFER)
static void bcmv3d_bootfb_cancel(struct bcmv3d_softc *, int);
static void bcmv3d_bootfb_retain(struct bcmv3d_softc *, int);

#ifdef WSDISPLAY_BOOTFB_HANDOFF
/* bcmmbox_request owns the stable DMA copy, including timeout quarantine. */
static int
bcmv3d_bootfb_exchange(void *arg, void *packet, size_t length)
{
	const uint8_t *reply = packet;
	uint32_t response;
	int error;

	if (arg != NULL || packet == NULL || length != 24)
		return EINVAL;
	error = bcmmbox_request(BCMMBOX_CHANARM2VC, packet, length, &response);
	if (error == 0) {
		aprint_normal("bcmv3d: display notify transport error %d; "
		    "reply %08x %08x %08x %08x %08x %08x\n", error,
		    le32dec(reply), le32dec(reply + 4), le32dec(reply + 8),
		    le32dec(reply + 12), le32dec(reply + 16), le32dec(reply + 20));
	} else {
		aprint_normal("bcmv3d: display notify transport error %d; "
		    "no response\n", error);
	}
	return error;
}

/* No other task can enter the attempted continuation or use these mappings. */
static const unsigned int bcmv3d_display_indices[BCM2712_QUIESCE_REGIONS] = {
	3, 5, 6
};

static int
bcmv3d_display_read(void *arg, unsigned int region, uint32_t offset,
    uint32_t *value)
{
	struct bcmv3d_softc *sc = arg;
	struct bcmv3d_display_stop *d = &sc->sc_bootfb.display;

	if (region >= d->mapped || offset >
	    d->contract.memory[bcmv3d_display_indices[region]].length - 4)
		return EINVAL;
	return bus_space_peek_4(sc->sc_bst, d->maps[region], offset, value) == 0 ?
	    0 : EIO;
}

static int
bcmv3d_display_write(void *arg, unsigned int region, uint32_t offset,
    uint32_t value)
{
	struct bcmv3d_softc *sc = arg;
	struct bcmv3d_display_stop *d = &sc->sc_bootfb.display;

	if (region >= d->mapped || offset >
	    d->contract.memory[bcmv3d_display_indices[region]].length - 4)
		return EINVAL;
	return bus_space_poke_4(sc->sc_bst, d->maps[region], offset, value) == 0 ?
	    0 : EIO;
}

static void
bcmv3d_display_barrier(void *arg, unsigned int region)
{
	struct bcmv3d_softc *sc = arg;
	struct bcmv3d_display_stop *d = &sc->sc_bootfb.display;

	bus_space_barrier(sc->sc_bst, d->maps[region], 0,
	    d->contract.memory[bcmv3d_display_indices[region]].length,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static void
bcmv3d_display_delay(void *arg, unsigned int usec)
{
	(void)arg;
	delay(usec);
}

static int
bcmv3d_display_clocks(struct bcmv3d_softc *sc)
{
	struct bcmv3d_display_stop *d = &sc->sc_bootfb.display;
	uint32_t hsm, bvb, core, disp;
	int error;

	/* Finish all GET-only creates before the first possible SET. */
#define CREATE(n) do { \
	d->stage = #n " create"; \
	error = bcm2712_display_ ## n ## _create(sc->sc_dev, sc->sc_handle, &d->n); \
	if (error != 0) return error; \
	error = bcm2712_display_ ## n ## _status(d->n, &d->n ## _status); \
	if (error != 0) return error; \
} while (0)
	CREATE(hsm);
	CREATE(bvb);
	CREATE(core);
	CREATE(disp);
#undef CREATE
	/* Do not lower an inherited positive rate before hardware stops. */
	hsm = MAX(149985000U, d->hsm_status.rate);
	bvb = MAX(75000000U, MAX(d->bvb_status.floor, d->bvb_status.rate));
	core = d->core_status.floor;
	disp = MAX(d->disp_status.floor, d->disp_status.rate);
#define HOLD(n) do { \
	d->stage = #n " acquire"; \
	error = bcm2712_display_ ## n ## _acquire(d->n, 0, n); \
	if (error != 0) return error; \
	error = bcm2712_display_ ## n ## _status(d->n, &d->n ## _status); \
	if (error != 0) return error; \
	if (d->n ## _status.fault != 0 || d->n ## _status.state != 1 || \
	    d->n ## _status.rate < n || d->n ## _status.leases != 1) \
		return EIO; \
} while (0)
	HOLD(hsm);
	HOLD(bvb);
	HOLD(core);
	HOLD(disp);
#undef HOLD
	return 0;
}

static int
bcmv3d_bootfb_stop_display(struct bcmv3d_softc *sc)
{
	static const struct bcm2712_quiesce_ops ops = {
		.read = bcmv3d_display_read,
		.write = bcmv3d_display_write,
		.barrier = bcmv3d_display_barrier,
		.delay = bcmv3d_display_delay
	};
	struct simplefb_console_close_status close;
	struct bcmv3d_bootfb_owner *owner;
	struct bcmv3d_display_stop *d;
	const struct bcm2712_display_memory *memory;
	unsigned int i;
	int count, error;

	if (sc == NULL)
		return EINVAL;
	if (cold || panicstr != NULL || cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (pserialize_in_read_section())
		return EDEADLK;
	ASSERT_SLEEPABLE();
	KERNEL_LOCK(1, NULL);
	owner = &sc->sc_bootfb;
	d = &owner->display;
	if (d->attempted || d->complete || d->fault != 0) {
		error = d->fault != 0 ? d->fault : EBUSY;
		goto out;
	}
	d->stage = "admission";
	if (sc->sc_dev == NULL || device_private(sc->sc_dev) != sc ||
	    sc->sc_bst == NULL || owner->phase != BCMV3D_BOOTFB_CLOSED ||
	    owner->renderer == NULL || !owner->firmware.attempted ||
	    !owner->firmware.complete || owner->firmware.fault != 0) {
		error = EPERM;
		goto publish;
	}
	error = simplefb_console_close_status(owner->lease, &close);
	if (error == 0 && (!close.complete || !close.attempted || !close.closed ||
	    !close.drained || close.error != 0 || close.mi_error != 0 ||
	    close.md_error != 0))
		error = ENXIO;
	if (error == 0)
		error = wsdisplay_bootfb_serial_closed(owner->lease);
	if (error != 0)
		goto publish;

	/* No retry, even if create or the first map fails without a mutation. */
	d->attempted = true;
	/* config_finalize owns Giant. Restore its exact recursion at publication. */
	KERNEL_UNLOCK_ALL(NULL, &count);
	error = bcmv3d_display_clocks(sc);
	if (error != 0)
		goto restore;
	d->stage = "resources";
	error = bcm2712_display_acpi_resources(sc->sc_dev, sc->sc_handle,
	    &d->contract);
	if (error != 0)
		goto restore;
	d->stage = "map";
	for (i = 0; i < BCM2712_QUIESCE_REGIONS; i++) {
		memory = &d->contract.memory[bcmv3d_display_indices[i]];
		error = bus_space_map(sc->sc_bst, memory->address, memory->length,
		    0, &d->maps[i]);
		if (error != 0)
			goto restore;
		d->mapped++;
	}
	d->stage = "PV0/1 and HVS0/1 stop";
	error = bcm2712_display_quiesce(&ops, sc, &d->stop);
restore:
	KERNEL_LOCK(count, NULL);
publish:
	d->fault = error;
	d->complete = error == 0;
	if (error != 0 && owner->first_error == 0)
		owner->first_error = error;
	if (sc->sc_dev == NULL)
		goto out;
	aprint_normal_dev(sc->sc_dev, "display stop at %s: error %d, complete %u; "
	    "cached clocks %u/%u/%u/%u Hz, maps %u\n", d->stage, error,
	    d->complete, d->hsm_status.rate, d->bvb_status.rate,
	    d->core_status.rate, d->disp_status.rate, d->mapped);
	if (d->stop.snapshot_valid) {
		aprint_normal_dev(sc->sc_dev, "display before: HVS %08x; "
		    "channels %08x/%08x %08x/%08x %08x/%08x; "
		    "PV0 %08x/%08x PV1 %08x/%08x\n", d->stop.before.global,
		    d->stop.before.control[0], d->stop.before.status[0],
		    d->stop.before.control[1], d->stop.before.status[1],
		    d->stop.before.control[2], d->stop.before.status[2],
		    d->stop.before.pv_control[0], d->stop.before.pv_video[0],
		    d->stop.before.pv_control[1], d->stop.before.pv_video[1]);
	}
	for (i = 0; i < 2; i++) {
		aprint_normal_dev(sc->sc_dev, "display last %u: version %08x, "
		    "PV %08x/%08x, HVS %08x/%08x, EMPTY %u, stage %u, "
		    "error %d, mutation %u, complete %u; no AXI drain or "
		    "display ownership\n", i, d->stop.version,
		    d->stop.pipe[i].pv_control, d->stop.pipe[i].pv_video,
		    d->stop.pipe[i].control, d->stop.pipe[i].status,
		    (d->stop.pipe[i].status >> 22) & 1, d->stop.pipe[i].stage,
		    d->stop.pipe[i].error, d->stop.pipe[i].mutation_attempted,
		    d->stop.pipe[i].complete);
	}
out:
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}

/* MOP and MOPlet also use MMU4. This gate performs no writeback control write. */
static int
bcmv3d_bootfb_writeback_idle(struct bcmv3d_softc *sc)
{
	struct bcmv3d_bootfb_mmu4 *m = &sc->sc_bootfb.mmu4;
	const struct bcm2712_display_memory *memory;
	unsigned int i;
	int error;

	for (i = 0; i < 2; i++) {
		memory = &sc->sc_bootfb.display.contract.memory[7 + i];
		m->stage = "writeback map";
		error = bus_space_map(sc->sc_bst, memory->address, memory->length,
		    0, &m->writeback_maps[i]);
		if (error != 0)
			return error;
		m->writeback_mapped++;
		m->stage = "writeback read";
		if (bus_space_peek_4(sc->sc_bst, m->writeback_maps[i], 0x0c,
		    &m->writeback_control[i]) != 0)
			return EIO;
		m->writeback_valid[i] = true;
		if ((m->writeback_control[i] & 3U) != 0)
			return EBUSY;
	}
	return 0;
}

static int
bcmv3d_bootfb_program_mmu4(struct bcmv3d_softc *sc)
{
	struct simplefb_console_close_status close;
	struct bcmv3d_bootfb_owner *owner;
	struct bcmv3d_display_stop *d;
	struct bcmv3d_bootfb_mmu4 *m;
	unsigned int i;
	int count, error;

	if (sc == NULL)
		return EINVAL;
	if (cold || panicstr != NULL || cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (pserialize_in_read_section())
		return EDEADLK;
	ASSERT_SLEEPABLE();
	KERNEL_LOCK(1, NULL);
	owner = &sc->sc_bootfb;
	d = &owner->display;
	m = &owner->mmu4;
	if (m->attempted || m->complete || m->fault != 0) {
		error = m->fault != 0 ? m->fault : EBUSY;
		goto out;
	}
	m->stage = "admission";
	if (sc->sc_dev == NULL || device_private(sc->sc_dev) != sc ||
	    owner->phase != BCMV3D_BOOTFB_CLOSED || owner->renderer == NULL ||
	    !owner->firmware.attempted || !owner->firmware.complete ||
	    owner->firmware.fault != 0 || !d->attempted || !d->complete ||
	    d->fault != 0 || d->mapped != BCM2712_QUIESCE_REGIONS ||
	    !d->stop.attempted || !d->stop.snapshot_valid || !d->stop.complete ||
	    d->stop.error != 0 || d->hsm == NULL || d->bvb == NULL ||
	    d->core == NULL || d->disp == NULL) {
		error = EPERM;
		goto publish;
	}
	for (i = 0; i < 2; i++) {
		if (!d->stop.pipe[i].complete || d->stop.pipe[i].error != 0 ||
		    !d->stop.pipe[i].pv_disabled || !d->stop.pipe[i].hvs_disabled) {
			error = ENXIO;
			goto publish;
		}
	}
#define HELD(n) do { \
	if (d->n ## _status.fault != 0 || d->n ## _status.state != 1 || \
	    d->n ## _status.leases != 1 || d->n ## _status.minimum[0] == 0 || \
	    d->n ## _status.rate < d->n ## _status.minimum[0]) { \
		error = ENXIO; \
		goto publish; \
	} \
} while (0)
	HELD(hsm);
	HELD(bvb);
	HELD(core);
	HELD(disp);
#undef HELD
	error = simplefb_console_close_status(owner->lease, &close);
	if (error == 0 && (!close.complete || !close.attempted || !close.closed ||
	    !close.drained || close.error != 0 || close.mi_error != 0 ||
	    close.md_error != 0))
		error = ENXIO;
	if (error == 0)
		error = wsdisplay_bootfb_serial_closed(owner->lease);
	if (error == 0)
		error = bcmv3d_render_bootfb_validate(owner->renderer, sc->sc_dev,
		    sc->sc_bst, sc->sc_dmat, sc->sc_handle, owner);
	if (error != 0)
		goto publish;

	m->attempted = true;
	KERNEL_UNLOCK_ALL(NULL, &count);
	error = bcmv3d_bootfb_writeback_idle(sc);
	if (error != 0)
		goto restore;
	/* The actual constructor checks identity and pins this exact renderer. */
	m->stage = "create";
	error = bcmv3d_mmu4_native_create(owner->renderer, &m->native);
	if (error != 0)
		goto restore;
	m->stage = "prepare";
	error = bcmv3d_mmu4_native_prepare(m->native);
	if (error == 0) {
		m->stage = "program";
		error = bcmv3d_mmu4_native_program(m->native);
	}
	/* Preserve the primary operation errno if the diagnostic query fails. */
	m->status_error = bcmv3d_mmu4_native_cached_status(m->native, &m->status);
	m->status_valid = m->status_error == 0;
	if (error == 0)
		error = m->status_error;
	if (error == 0 && m->status.fault != 0)
		error = m->status.fault;
	if (error == 0 && (m->status.stage != BCMV3D_MMU4_NATIVE_COMPLETE ||
	    !m->status.backing_valid || !m->status.backing.prepared ||
	    !m->status.backing.sealed || m->status.backing.first_error != 0 ||
	    !m->status.sequence_valid || !m->status.sequence.sequence_complete ||
	    !m->status.sequence.retained || m->status.sequence.fault != 0 ||
	    !m->status.shared_valid || !m->status.shared.flush_complete ||
	    (m->status.shared.retained & 2U) == 0 || !m->status.icac_valid))
		error = EIO;
restore:
	KERNEL_LOCK(count, NULL);
publish:
	m->fault = error;
	m->complete = error == 0;
	if (error != 0 && owner->first_error == 0)
		owner->first_error = error;
	if (sc->sc_dev != NULL) {
		aprint_normal_dev(sc->sc_dev, "boot MMU4 at %s: error %d, "
		    "complete %u; writeback maps %u, control %08x/%08x, "
		    "valid %u/%u; cached status %d, backing prepared %u "
		    "sealed %u, sequence complete %u; no HVS activation\n",
		    m->stage, error, m->complete, m->writeback_mapped,
		    m->writeback_control[0], m->writeback_control[1],
		    m->writeback_valid[0], m->writeback_valid[1], m->status_error,
		    m->status_valid && m->status.backing_valid &&
		    m->status.backing.prepared,
		    m->status_valid && m->status.backing_valid &&
		    m->status.backing.sealed,
		    m->status_valid && m->status.sequence_valid &&
		    m->status.sequence.sequence_complete);
		aprint_normal_dev(sc->sc_dev, "boot MMU4 cached native: valid %u, "
		    "stage %u, DEBUG %08x valid %u; ICAC valid %u, observation "
		    "stage %u, CONTROL %08x valid %u error %d; prepare %d, program %d\n",
		    m->status_valid, m->status.stage, m->status.debug,
		    m->status.debug_valid, m->status.icac_valid,
		    m->status.icac.observation.stage, m->status.icac.observation.raw_control,
		    m->status.icac.observation.raw_valid,
		    m->status.icac.observation.first_error,
		    m->status.prepare_error, m->status.program_error);
	}
out:
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}

/* Canonical contract indices. Only HVS and PV0 have a write route. */
static const unsigned int bcmv3d_publish_indices[BCMV3D_PUBLISH_WINDOWS] = {
	3, 5, 6, 11, 16, 17, 18, 19, 22
};

static int
bcmv3d_publish_io_error(struct bcmv3d_bootfb_publish *p, int error)
{

	if (p->io_error == 0)
		p->io_error = error;
	return p->io_error;
}

static int
bcmv3d_publish_read(void *arg, enum bcmv3d_hvs_publish_window window,
    uint32_t offset, uint32_t *out)
{
	struct bcmv3d_softc *sc = arg;
	struct bcmv3d_bootfb_owner *owner = &sc->sc_bootfb;
	struct bcmv3d_bootfb_publish *p = &owner->publish;
	const struct bcm2712_display_memory *memory;
	uint32_t value, cob, stride;
	unsigned int i;
	int error;

	if (!p->executing)
		return EPERM;
	if (p->io_error != 0)
		return p->io_error;
	if ((unsigned int)window >= BCMV3D_PUBLISH_WINDOWS || out == NULL)
		return bcmv3d_publish_io_error(p, EINVAL);
	memory = &owner->display.contract.memory[bcmv3d_publish_indices[window]];
	if ((offset & 3U) != 0 || memory->length < 4 ||
	    offset > memory->length - 4)
		return bcmv3d_publish_io_error(p, EINVAL);
	/* The fixed helper has completed its full actual-native status gate. */
	for (i = 0; i < 2; i++) {
		if (p->writeback_valid[i])
			continue;
		if (bus_space_peek_4(sc->sc_bst, owner->mmu4.writeback_maps[i],
		    0x0c, &p->writeback[i]) != 0)
			return bcmv3d_publish_io_error(p, EIO);
		p->writeback_valid[i] = true;
		if ((p->writeback[i] & 3U) != 0)
			return bcmv3d_publish_io_error(p, EBUSY);
	}
	if ((p->mapped & (1U << window)) == 0) {
		/* New windows are read-only and map only after native admission. */
		error = bus_space_map(sc->sc_bst, memory->address, memory->length,
		    0, &p->maps[window]);
		if (error != 0)
			return bcmv3d_publish_io_error(p, error);
		p->mapped |= 1U << window;
	}
	if (bus_space_peek_4(sc->sc_bst, p->maps[window], offset, &value) != 0)
		return bcmv3d_publish_io_error(p, EIO);
	/* CXM_SIZE is a word count, not proof that this region is unowned. */
	if (window == BCMV3D_PUBLISH_HVS && offset == 0 &&
	    (value & 0xff) == (uint32_t)p->frame.variant && !p->cxm_valid) {
		if (bus_space_peek_4(sc->sc_bst, p->maps[window], 4,
		    &p->cxm_words) != 0)
			return bcmv3d_publish_io_error(p, EIO);
		p->cxm_valid = true;
		if (p->cxm_words < p->dlist_first + p->dlist_words)
			return bcmv3d_publish_io_error(p, EOPNOTSUPP);
	}
	/* Record the inherited partition. A matching value is not a free pool. */
	cob = p->frame.variant == BCM2712_HVS_C0 ? 0x40 : 0x114;
	stride = p->frame.variant == BCM2712_HVS_C0 ? 0x20 : 0x40;
	for (i = 0; i < 3; i++) {
		if (window == BCMV3D_PUBLISH_HVS && offset == cob + i * stride)
			p->cob[i] = value;
	}
	*out = value;
	return 0;
}

static int
bcmv3d_publish_write(void *arg, enum bcmv3d_hvs_publish_window window,
    uint32_t offset, uint32_t value)
{
	struct bcmv3d_softc *sc = arg;
	struct bcmv3d_bootfb_publish *p = &sc->sc_bootfb.publish;
	const struct bcm2712_display_memory *memory;

	if (!p->executing)
		return EPERM;
	if (p->io_error != 0)
		return p->io_error;
	if (window != BCMV3D_PUBLISH_HVS && window != BCMV3D_PUBLISH_PV0)
		return bcmv3d_publish_io_error(p, EACCES);
	memory = &sc->sc_bootfb.display.contract.memory[bcmv3d_publish_indices[window]];
	if ((p->mapped & (1U << window)) == 0 || (offset & 3U) != 0 ||
	    memory->length < 4 || offset > memory->length - 4)
		return bcmv3d_publish_io_error(p, EINVAL);
	if (bus_space_poke_4(sc->sc_bst, p->maps[window], offset, value) != 0)
		return bcmv3d_publish_io_error(p, EIO);
	return 0;
}

static void
bcmv3d_publish_barrier(void *arg, enum bcmv3d_hvs_publish_window window)
{
	struct bcmv3d_softc *sc = arg;
	struct bcmv3d_bootfb_publish *p = &sc->sc_bootfb.publish;

	if (!p->executing || p->io_error != 0)
		return;
	if ((window != BCMV3D_PUBLISH_HVS && window != BCMV3D_PUBLISH_PV0) ||
	    (p->mapped & (1U << window)) == 0) {
		(void)bcmv3d_publish_io_error(p, EINVAL);
		return;
	}
	bus_space_barrier(sc->sc_bst, p->maps[window], 0,
	    sc->sc_bootfb.display.contract.memory[bcmv3d_publish_indices[window]].length,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static void
bcmv3d_publish_delay(void *arg, unsigned int usec)
{
	struct bcmv3d_softc *sc = arg;

	if (sc->sc_bootfb.publish.executing && sc->sc_bootfb.publish.io_error == 0)
		delay(usec);
}

static int
bcmv3d_bootfb_publish(struct bcmv3d_softc *sc)
{
	static const struct bcmv3d_hvs_publish_io io = {
		.read = bcmv3d_publish_read,
		.write = bcmv3d_publish_write,
		.barrier = bcmv3d_publish_barrier,
		.delay = bcmv3d_publish_delay
	};
	struct bcmv3d_bootfb_owner *owner;
	struct bcmv3d_bootfb_publish *p;
	struct bcmv3d_display_stop *d;
	struct simplefb_console_close_status close;
	unsigned int i;
	uint32_t stat;
	int count, error, check_error = 0;

	if (sc == NULL)
		return EINVAL;
	if (cold || panicstr != NULL || cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (pserialize_in_read_section())
		return EDEADLK;
	ASSERT_SLEEPABLE();
	KERNEL_LOCK(1, NULL);
	owner = &sc->sc_bootfb;
	p = &owner->publish;
	d = &owner->display;
	if (p->attempted || p->complete || p->fault != 0) {
		error = p->fault != 0 ? p->fault : EBUSY;
		goto out;
	}
	if (sc->sc_dev == NULL || device_private(sc->sc_dev) != sc ||
	    owner->phase != BCMV3D_BOOTFB_CLOSED || owner->first_error != 0 ||
	    !owner->firmware.complete || owner->firmware.fault != 0 ||
	    !d->complete || d->fault != 0 || d->mapped != BCM2712_QUIESCE_REGIONS ||
	    !d->stop.complete || d->stop.error != 0 ||
	    !owner->mmu4.complete || owner->mmu4.fault != 0 ||
	    owner->mmu4.native == NULL || owner->mmu4.writeback_mapped != 2) {
		error = EPERM;
		goto publish;
	}
	for (i = 0; i < 2; i++) {
		if (!d->stop.pipe[i].complete || d->stop.pipe[i].error != 0 ||
		    !d->stop.pipe[i].pv_disabled || !d->stop.pipe[i].hvs_disabled) {
			error = ENXIO;
			goto publish;
		}
	}
#define HELD(n) do { \
	if (d->n == NULL || d->n ## _status.fault != 0 || \
	    d->n ## _status.state != 1 || d->n ## _status.leases != 1 || \
	    d->n ## _status.minimum[0] == 0 || \
	    d->n ## _status.rate < d->n ## _status.minimum[0]) { \
		error = ENXIO; goto publish; \
	} \
} while (0)
	HELD(hsm);
	HELD(bvb);
	HELD(core);
	HELD(disp);
#undef HELD
	error = simplefb_console_close_status(owner->lease, &close);
	if (error == 0 && (!close.attempted || !close.complete || !close.closed ||
	    !close.drained || close.error != 0 || close.mi_error != 0 ||
	    close.md_error != 0))
		error = ENXIO;
	if (error == 0)
		error = wsdisplay_bootfb_serial_closed(owner->lease);
	if (error == 0)
		error = bcmv3d_render_bootfb_validate(owner->renderer, sc->sc_dev,
		    sc->sc_bst, sc->sc_dmat, sc->sc_handle, owner);
	if (error != 0)
		goto publish;
	/*
	 * This fixed profile is the first and only HVS boot writer. The same
	 * canonical GPU owner reserves SRAM32..41, UPM0..15615/slot0 and keeps
	 * the inherited COB partition. No other display driver/probe may run.
	 * Stopped channels, firmware ACK and metadata do not prove free space.
	 * The actual native helper validates fixed IOVA0 backing before I/O.
	 */
	p->dlist_first = 32;
	p->dlist_words = BCM2712_HVS_DLIST_WORDS;
	p->frame = (struct bcm2712_hvs_frame) {
		.variant = d->stop.version & 0xff,
		.dma_base = 0, .dma_span = 8294400,
		.upm_offset = 0, .upm_length = 15616, .upm_slot = 0
	};
	for (i = 0; i < BCM2712_QUIESCE_REGIONS; i++)
		p->maps[i] = d->maps[i];
	p->mapped = 7;
	p->attempted = p->executing = true;
	KERNEL_UNLOCK_ALL(NULL, &count);
	/* Fresh actual native status precedes every callback/map/register read. */
	error = bcmv3d_hvs_publish_1080p60(owner->mmu4.native, &io, sc,
	    &p->frame, &p->status);
	if (error == 0)
		error = p->io_error;
	if (error == 0) {
		/* Diagnostic samples only. No progress value authorizes later I/O. */
		stat = p->frame.variant == BCM2712_HVS_C0 ? 0x44 : 0x118;
		p->progress[0] = p->status.channel_status;
		p->progress_valid[0] = true;
		delay(20000);
		p->progress_error = bcmv3d_publish_read(sc, BCMV3D_PUBLISH_HVS,
		    stat, &p->progress[1]);
		p->progress_valid[1] = p->progress_error == 0;
		if (p->progress_error != 0)
			error = p->progress_error;
	}
	p->executing = false;
	/* The same retained owner checks only never-HVS-latched private windows. */
	if (error == 0)
		check_error = bcmv3d_mmu4_check_ranges(owner->renderer,
		    owner->mmu4.native, &owner->range_check);
	KERNEL_LOCK(count, NULL);
publish:
	p->fault = error;
	p->complete = error == 0;
	if (error != 0 && owner->first_error == 0)
		owner->first_error = error;
	if (sc->sc_dev != NULL) {
		aprint_normal_dev(sc->sc_dev, "boot HVS publish: error %d, complete %u, "
		    "helper %u/stage %u, maps %x, CXM %u valid %u; "
		    "MOP %08x/%08x valid %u/%u; mismatch window %u offset %x "
		    "raw %08x valid %u mask %08x expected %08x; RUN %08x "
		    "active DL %u; progress %08x/%08x valid %u/%u error %d "
		    "frame count %u/%u; no KMS or drain\n", error, p->complete,
		    p->status.complete, p->status.stage, p->mapped, p->cxm_words,
		    p->cxm_valid, p->writeback[0], p->writeback[1],
		    p->writeback_valid[0], p->writeback_valid[1],
		    p->status.window, p->status.offset, p->status.raw,
		    p->status.raw_valid, p->status.reject_mask, p->status.reject_expected,
		    p->status.channel_status, p->status.active_list,
		    p->progress[0], p->progress[1], p->progress_valid[0],
		    p->progress_valid[1], p->progress_error,
		    (p->progress[0] >> 16) & 0x3f, (p->progress[1] >> 16) & 0x3f);
		aprint_normal_dev(sc->sc_dev, "boot HVS timing: saved words %u/12; "
		    "PV VCTRL/VSYNCD/HORZA/HORZB/VERTA/VERTB "
		    "%08x/%08x/%08x/%08x/%08x/%08x; "
		    "HDMI HORZA/HORZB/VERTA0/VERTA1/VERTB0/VERTB1 "
		    "%08x/%08x/%08x/%08x/%08x/%08x\n",
		    p->status.timing.words, p->status.timing.pv[0],
		    p->status.timing.pv[1], p->status.timing.pv[2],
		    p->status.timing.pv[3], p->status.timing.pv[4],
		    p->status.timing.pv[5], p->status.timing.hdmi[0],
		    p->status.timing.hdmi[1], p->status.timing.hdmi[2],
		    p->status.timing.hdmi[3], p->status.timing.hdmi[4],
		    p->status.timing.hdmi[5]);
		aprint_normal_dev(sc->sc_dev, "boot HVS link: saved words %u/26; "
		    "profile %08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/"
		    "%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x\n",
		    p->status.link_words,
		    p->status.link[0], p->status.link[1], p->status.link[2],
		    p->status.link[3], p->status.link[4], p->status.link[5],
		    p->status.link[6], p->status.link[7], p->status.link[8],
		    p->status.link[9], p->status.link[10], p->status.link[11],
		    p->status.link[12], p->status.link[13], p->status.link[14],
		    p->status.link[15], p->status.link[16], p->status.link[17],
		    p->status.link[18], p->status.link[19]);
		aprint_normal_dev(sc->sc_dev, "boot HVS CSC: "
		    "%08x/%08x/%08x/%08x/%08x/%08x\n",
		    p->status.link[20], p->status.link[21], p->status.link[22],
		    p->status.link[23], p->status.link[24], p->status.link[25]);
	}
	if (owner->range_check.attempted) {
		struct bcmv3d_mmu4_check_status *r = &owner->range_check;

		aprint_normal_dev(sc->sc_dev, "boot MMU4 unused ranges: error %d, "
		    "complete %u, cycles %u, staged/published/unmapped %u/%u/%u; "
		    "BO bytes %ju/%ju reservations/releases %ju/%ju; no HVS submit\n",
		    check_error, r->complete, r->cycles, r->staged, r->published,
		    r->unmapped, (uintmax_t)r->before_bytes, (uintmax_t)r->after_bytes,
		    (uintmax_t)r->reservations, (uintmax_t)r->releases);
	}
out:
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}

/* A new retained runtime owner borrows maps, not the one-shot execute flag. */
static int
bcmv3d_bootfb_start_vblank(struct bcmv3d_softc *sc)
{
	struct bcmv3d_bootfb_owner *owner = &sc->sc_bootfb;
	struct bcmv3d_bootfb_publish *p = &owner->publish;
	struct bcmv3d_vblank_native *native;
	int count, error;

	if (owner->phase != BCMV3D_BOOTFB_CLOSED || owner->first_error != 0 ||
	    owner->renderer == NULL || !owner->firmware.complete ||
	    owner->firmware.fault != 0 || !owner->display.complete ||
	    owner->display.fault != 0 || !owner->mmu4.complete ||
	    owner->mmu4.fault != 0 || !p->complete || p->fault != 0 ||
	    !p->status.complete || p->executing || p->mapped != 0x1ff ||
	    !owner->range_check.complete || owner->range_check.first_error != 0)
		return EPERM;
	error = bcmv3d_render_bootfb_validate(owner->renderer, sc->sc_dev,
	    sc->sc_bst, sc->sc_dmat, sc->sc_handle, owner);
	if (error != 0)
		return error;
	KERNEL_UNLOCK_ALL(NULL, &count);
	error = bcmv3d_vblank_native_create(owner->renderer,
	    &owner->display.contract, p->maps[BCMV3D_PUBLISH_PV0],
	    p->maps[BCMV3D_PUBLISH_HVS], p->frame.variant, &native);
	KERNEL_LOCK(count, NULL);
	aprint_normal_dev(sc->sc_dev, "native PV0 event route: error %d, "
	    "GSI 133; no DRM event or BO retirement\n", error);
	return error;
}

static int
bcmv3d_bootfb_notify(struct bcmv3d_softc *sc)
{
	struct simplefb_console_close_status close;
	struct bcmv3d_bootfb_owner *owner;
	int error;

	if (sc == NULL)
		return EINVAL;
	if (cold || panicstr != NULL || cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (pserialize_in_read_section())
		return EDEADLK;
	ASSERT_SLEEPABLE();
	KERNEL_LOCK(1, NULL);
	owner = &sc->sc_bootfb;
	if (owner->firmware.attempted || owner->firmware.complete ||
	    owner->firmware.fault != 0) {
		error = owner->firmware.fault != 0 ? owner->firmware.fault : EBUSY;
		goto out;
	}
	if (sc->sc_dev == NULL || device_private(sc->sc_dev) != sc ||
	    owner->phase != BCMV3D_BOOTFB_CLOSED) {
		error = EPERM;
		goto refused;
	}
	error = simplefb_console_close_status(owner->lease, &close);
	if (error == 0 && (!close.complete || !close.attempted || !close.closed ||
	    !close.drained || close.error != 0 || close.mi_error != 0 ||
	    close.md_error != 0))
		error = ENXIO;
	if (error == 0)
		error = wsdisplay_bootfb_serial_closed(owner->lease);
	if (error != 0)
		goto refused;
	/* The helper sets attempted before the fixed transport can yield. */
	error = bcm2712_display_notify_done(bcmv3d_bootfb_exchange, NULL,
	    &owner->firmware);
	if (error != 0 && owner->first_error == 0)
		owner->first_error = error;
	goto out;
refused:
	owner->firmware.fault = error;
	if (owner->first_error == 0)
		owner->first_error = error;
out:
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}
#endif

static void
bcmv3d_bootfb_claim_job(device_t dev)
{
	struct bcmv3d_softc *sc = device_private(dev);
	struct bcmv3d_bootfb_owner *owner = &sc->sc_bootfb;
	struct wsdisplay_bootfb_lease *lease;
	struct simplefb_framebuffer framebuffer;
	int error;

	if (owner->phase != BCMV3D_BOOTFB_QUEUED)
		return;
	owner->phase = BCMV3D_BOOTFB_REFUSED;
	error = simplefb_console_claim(&lease, &framebuffer);
	owner->first_error = error;
	if (error != 0) {
		aprint_normal_dev(dev, "boot framebuffer claim refused: %d\n",
		    error);
		return;
	}
	owner->lease = lease;
	owner->framebuffer = framebuffer;
	owner->phase = BCMV3D_BOOTFB_CLAIMED;
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	/* This pending job joins before either finalizer and before init console. */
	error = simplefb_console_close_serial(lease);
	owner->first_error = error;
	{
		struct simplefb_console_close_status status;
		int status_error = simplefb_console_close_status(lease, &status);

		if (status_error != 0) {
			/* Unknown admission cannot authorize fallback. */
			bcmv3d_bootfb_retain(sc, status_error);
		} else if (status.closed) {
			owner->phase = BCMV3D_BOOTFB_CLOSED;
			aprint_normal_dev(dev,
			    "boot framebuffer CPU console closed: error %d, drained %d; "
			    "serial console retained, no display handoff\n",
			    error, status.drained);
			error = bcmv3d_bootfb_notify(sc);
			aprint_normal_dev(dev,
			    "firmware display notification: error %d, complete %d; "
			    "no display ownership\n", error, owner->firmware.complete);
		} else {
			/* No irreversible admission occurred. Preserve genfb fallback. */
			bcmv3d_bootfb_cancel(sc, error);
		}
	}
#endif
}

static void
bcmv3d_bootfb_queue(struct bcmv3d_softc *sc)
{
	struct bcmv3d_bootfb_owner *owner = &sc->sc_bootfb;

	if (!cold || sc->sc_bst == NULL || sc->sc_dmat == NULL ||
	    owner->phase != BCMV3D_BOOTFB_IDLE)
		return;
	owner->lease = NULL;
	memset(&owner->framebuffer, 0, sizeof(owner->framebuffer));
	owner->first_error = owner->cancel_error = 0;
	owner->phase = BCMV3D_BOOTFB_QUEUED;
	config_interrupts(sc->sc_dev, bcmv3d_bootfb_claim_job);
}

static void
bcmv3d_bootfb_retain(struct bcmv3d_softc *sc, int error)
{
	struct bcmv3d_bootfb_owner *owner = &sc->sc_bootfb;

	if (owner->phase != BCMV3D_BOOTFB_CLAIMED)
		return;
	if (owner->first_error == 0)
		owner->first_error = error;
	KERNEL_LOCK(1, NULL);
	wsdisplay_bootfb_retain(owner->lease);
	KERNEL_UNLOCK_ONE(NULL);
	owner->phase = BCMV3D_BOOTFB_RETAINED;
	aprint_error_dev(sc->sc_dev,
	    "boot framebuffer retained: error %d, cancel error %d; "
	    "no display handoff\n", owner->first_error, owner->cancel_error);
}

static void
bcmv3d_bootfb_cancel(struct bcmv3d_softc *sc, int error)
{
	struct bcmv3d_bootfb_owner *owner = &sc->sc_bootfb;

	if (owner->phase != BCMV3D_BOOTFB_CLAIMED)
		return;
	if (owner->first_error == 0)
		owner->first_error = error;
	owner->cancel_error = simplefb_console_cancel(owner->lease);
	if (owner->cancel_error != 0) {
		bcmv3d_bootfb_retain(sc, error);
		return;
	}
	owner->lease = NULL;
	owner->phase = BCMV3D_BOOTFB_CANCELED;
}
#endif

/* A single property tag has eight payload bytes and a four-byte terminator. */
static int
bcmv3d_clock(struct bcmv3d_softc *sc, uint32_t tag,
    struct bcmv3d_clock_result *result)
{
	uint32_t request[8] __aligned(16) = {
		htole32(32), 0, htole32(tag), htole32(8), 0,
		htole32(VCPROP_CLK_V3D), 0, 0
	};
	uint32_t response;
	const char *name;
	int error;

	/* No caller can use this helper to send a firmware write request. */
	result->status = BCMV3D_CLOCK_INVALID_TAG;
	result->error = EINVAL;
	result->value = 0;
	if (tag == VCPROPTAG_GET_CLOCKSTATE)
		name = "state";
	else if (tag == VCPROPTAG_GET_CLOCKRATE)
		name = "rate";
	else
		return EINVAL;
	error = bcmmbox_request(BCMMBOX_CHANARM2VC, request,
	    sizeof(request), &response);
	if (error != 0) {
		result->status = BCMV3D_CLOCK_TRANSPORT_ERROR;
		result->error = error;
		aprint_normal_dev(sc->sc_dev, "clock5 %s: transport error %d\n",
		    name, error);
		return error;
	}
	if (le32toh(request[0]) != sizeof(request) ||
	    le32toh(request[1]) != VCPROP_REQ_SUCCESS ||
	    le32toh(request[2]) != tag || le32toh(request[3]) != 8 ||
	    le32toh(request[4]) != (VCPROPTAG_RESPONSE | 8) ||
	    le32toh(request[5]) != VCPROP_CLK_V3D || request[7] != 0) {
		result->status = BCMV3D_CLOCK_RESPONSE_ERROR;
		result->error = EIO;
		/* Only a completed transport supplies firmware response words. */
		aprint_normal_dev(sc->sc_dev,
		    "clock5 %s: rejected reply %08x %08x %08x %08x "
		    "%08x %08x %08x %08x\n", name,
		    le32toh(request[0]), le32toh(request[1]),
		    le32toh(request[2]), le32toh(request[3]),
		    le32toh(request[4]), le32toh(request[5]),
		    le32toh(request[6]), le32toh(request[7]));
		return EIO;
	}
	result->status = BCMV3D_CLOCK_VALID;
	result->error = 0;
	result->value = le32toh(request[6]);
	if (tag == VCPROPTAG_GET_CLOCKSTATE)
		aprint_normal_dev(sc->sc_dev, "clock5 state=%#x\n", result->value);
	else
		aprint_normal_dev(sc->sc_dev, "clock5 %s=%u Hz\n",
		    name, result->value);
	return 0;
}

static int
bcmv3d_prerequisites(struct bcmv3d_softc *sc)
{
	int error;

	/*
	 * Linux firmware-clock operations use state and configured rate.
	 * Its V3D probe does not require the measured-clock property.
	 */
	sc->sc_stage = "firmware V3D clock state";
	error = bcmv3d_clock(sc, VCPROPTAG_GET_CLOCKSTATE, &sc->sc_clock_state);
	if (error != 0)
		goto pm;
	sc->sc_stage = "firmware V3D clock rate";
	error = bcmv3d_clock(sc, VCPROPTAG_GET_CLOCKRATE, &sc->sc_clock_rate);
pm:
	/* PM is independent; retain the first clock failure and its stage. */
	if (error == 0)
		sc->sc_stage = "BCM2712 PM owner";
	sc->sc_pm_error = bcmpmwdog_v3d_status(&sc->sc_pm);
	sc->sc_pm_valid = sc->sc_pm_error == 0;
	if (sc->sc_pm_valid)
		aprint_normal_dev(sc->sc_dev, "PM_GRAFX2712=%#x\n", sc->sc_pm);
	else
		aprint_normal_dev(sc->sc_dev, "PM_GRAFX2712: error %d\n",
		    sc->sc_pm_error);
	if (error != 0)
		return error;
	if (sc->sc_pm_error != 0)
		return sc->sc_pm_error;
	sc->sc_stage = "firmware clock/reset readiness";
	/* State bit 1 means nonexistent; unknown bits are not accepted. */
	if (sc->sc_clock_state.value != 1 || sc->sc_clock_rate.value == 0 ||
	    (sc->sc_pm & BCMV3D_RESET_N) == 0)
		return EBUSY;
	return 0;
}

static bool
bcmv3d_sms_idle(uint32_t value)
{
	const uint32_t allowed = BCMV3D_SMS_STATE_MASK |
	    BCMV3D_SMS_OLD_MODE_MASK | BCMV3D_SMS_NEW_MODE_MASK;
	uint32_t old_mode = (value & BCMV3D_SMS_OLD_MODE_MASK) >> 4;
	uint32_t new_mode = (value & BCMV3D_SMS_NEW_MODE_MASK) >> 6;

	/*
	 * Linux checks STATE for IDLE, not the whole register. Mode values
	 * are opaque here: require no transition and no other status bits.
	 * This permits observation only, not ownership or initialization.
	 */
	return (value & BCMV3D_SMS_STATE_MASK) == 0 &&
	    (value & ~allowed) == 0 && old_mode == new_mode;
}

static int
bcmv3d_observe(struct bcmv3d_softc *sc)
{
	bus_space_handle_t handles[BCMV3D_NREG];
	bool mapped[BCMV3D_NREG] = { false, false, false };
	uint32_t sms[2];
	unsigned int i;
	int error;

	error = bcmv3d_prerequisites(sc);
	if (error != 0)
		return error;
	sc->sc_stage = "SMS mapping";
	error = bus_space_map(sc->sc_bst, bcmv3d_addresses[BCMV3D_SMS],
	    bcmv3d_lengths[BCMV3D_SMS], 0, &handles[BCMV3D_SMS]);
	if (error != 0)
		return error;
	mapped[BCMV3D_SMS] = true;
	sc->sc_stage = "SMS readiness";
	/*
	 * Require idle REE/TEE fields and identical complete snapshots before
	 * reading IDs. Peek catches bus faults; it cannot time out a stalled
	 * hardware bus. No SMS command is issued to make an unready GPU ready.
	 */
	for (i = 0; i < 2; i++) {
		error = bus_space_peek_4(sc->sc_bst, handles[BCMV3D_SMS],
		    i * BCMV3D_SMS_TEE, &sc->sc_sms[i]);
		if (error != 0)
			goto out;
	}
	aprint_normal_dev(sc->sc_dev, "SMS REE=%#x TEE=%#x\n",
	    sc->sc_sms[0], sc->sc_sms[1]);
	if (!bcmv3d_sms_idle(sc->sc_sms[0]) ||
	    !bcmv3d_sms_idle(sc->sc_sms[1])) {
		error = EBUSY;
		goto out;
	}
	for (i = 0; i < 2; i++) {
		error = bus_space_peek_4(sc->sc_bst, handles[BCMV3D_SMS],
		    i * BCMV3D_SMS_TEE, &sms[i]);
		if (error != 0)
			goto out;
		if (sms[i] != sc->sc_sms[i]) {
			error = EBUSY;
			goto out;
		}
	}
	for (i = BCMV3D_HUB; i <= BCMV3D_CORE; i++) {
		sc->sc_stage = i == BCMV3D_HUB ? "HUB mapping" : "CORE mapping";
		error = bus_space_map(sc->sc_bst, bcmv3d_addresses[i],
		    bcmv3d_lengths[i], 0, &handles[i]);
		if (error != 0)
			goto out;
		mapped[i] = true;
	}
	sc->sc_stage = "V3D identification";
	error = bus_space_peek_4(sc->sc_bst, handles[BCMV3D_HUB],
	    BCMV3D_HUB_IDENT1, &sc->sc_ident1);
	if (error != 0)
		goto out;
	if ((sc->sc_ident1 & 15) != 7 ||
	    ((sc->sc_ident1 >> 4) & 15) != 1 ||
	    ((sc->sc_ident1 >> 8) & 15) != 1) {
		error = ENODEV;
		goto out;
	}
	error = bus_space_peek_4(sc->sc_bst, handles[BCMV3D_HUB],
	    BCMV3D_HUB_IDENT3, &sc->sc_ident3);
	if (error != 0)
		goto out;
	error = bus_space_peek_4(sc->sc_bst, handles[BCMV3D_CORE], 0,
	    &sc->sc_core_ident0);
	if (error != 0)
		goto out;
	error = bus_space_peek_4(sc->sc_bst, handles[BCMV3D_HUB],
	    BCMV3D_MMU_DEBUG, &sc->sc_mmu);
	if (error != 0)
		goto out;
	aprint_normal_dev(sc->sc_dev,
	    "V3D 7.1 IDENT1=%#x IDENT3=%#x CORE_IDENT0=%#x MMU_DEBUG=%#x; "
	    "identification only, no command submission\n", sc->sc_ident1,
	    sc->sc_ident3, sc->sc_core_ident0, sc->sc_mmu);
out:
	for (i = 0; i < BCMV3D_NREG; i++)
		if (mapped[i])
			bus_space_unmap(sc->sc_bst, handles[i], bcmv3d_lengths[i]);
	return error;
}

static int
bcmv3d_finalize(device_t dev)
{
	struct bcmv3d_softc *sc = device_private(dev);
#ifdef BCM2712_V3D_RENDER
	const struct bcmv3d_bootfb_owner *bootfb = NULL;
	struct bcmv3d_device *renderer = NULL;
#endif
	int error;

	if (sc->sc_observed)
		return 0;
	sc->sc_observed = true;
	bcm2712_display_acpi_observe(dev, sc->sc_handle);
	error = bcmv3d_observe(sc);
	if (error != 0) {
		aprint_normal_dev(dev, "observation stopped at %s: error %d; "
		    "GPU state unchanged\n", sc->sc_stage, error);
#if defined(BCM2712_V3D_RENDER) && defined(WSDISPLAY_BOOTFB_DEFER)
		bcmv3d_bootfb_cancel(sc, error);
#endif
	}
#ifdef BCM2712_V3D_TAKEOVER
	else {
		error = bcmv3d_takeover_probe(dev, sc->sc_bst);
#if defined(BCM2712_V3D_RENDER) && defined(WSDISPLAY_BOOTFB_DEFER)
		/* The takeover errno does not distinguish pre/post-seal errors. */
		if (error != 0)
			bcmv3d_bootfb_retain(sc, error);
#endif
#ifdef BCM2712_V3D_DMA_PROBE
		/* The bounded DMA experiment runs only after a full takeover. */
		if (error == 0)
			error = bcmv3d_dma_probe(dev, sc->sc_dmat);
#endif
#ifdef BCM2712_V3D_QUEUE_PROBE
		/* A failed DMA job may still be active: stop the entire chain. */
		if (error == 0)
			error = bcmv3d_queue_probe(dev, sc->sc_dmat);
#endif
#ifdef BCM2712_V3D_IRQ_PROBE
		/* The interrupt experiment owns the jobs; no polling probe. */
		if (error == 0)
			error = bcmv3d_irq_probe(dev, sc->sc_dmat,
			    sc->sc_handle);
#endif
#ifdef BCM2712_V3D_FAULT_PROBE
		/* The fault experiment owns the jobs; no polling probe. */
		if (error == 0)
			error = bcmv3d_fault_probe(dev, sc->sc_dmat,
			    sc->sc_handle);
#endif
#ifdef BCM2712_V3D_RENDER
		if (error == 0) {
#if defined(WSDISPLAY_BOOTFB_DEFER)
			if (sc->sc_bootfb.phase == BCMV3D_BOOTFB_CLAIMED
#ifdef WSDISPLAY_BOOTFB_HANDOFF
			    || sc->sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED
#endif
			    )
				bootfb = &sc->sc_bootfb;
#endif
			error = bcmv3d_render_attach(dev, sc->sc_bst,
			    sc->sc_dmat, sc->sc_handle, bootfb, &renderer);
#if defined(WSDISPLAY_BOOTFB_DEFER)
			/*
			 * Render setup has no display or MMU4 calls. Release only a
			 * live pre-close lease after setup cleanup. A CLOSED owner
			 * retains its serial route and CPU drain on every outcome.
			 */
			bcmv3d_bootfb_cancel(sc, error);
#endif
			if (error != 0)
				aprint_error_dev(dev, "render attachment failed: %d\n", error);
#if defined(WSDISPLAY_BOOTFB_DEFER) && defined(WSDISPLAY_BOOTFB_HANDOFF)
			else {
				sc->sc_bootfb.renderer = renderer;
				if (bcmv3d_bootfb_stop_display(sc) == 0 &&
				    bcmv3d_bootfb_program_mmu4(sc) == 0 &&
				    bcmv3d_bootfb_publish(sc) == 0)
					(void)bcmv3d_bootfb_start_vblank(sc);
			}
#endif
		}
#endif
	}
#endif
	return 0;
}

static int
bcmv3d_match(device_t parent, cfdata_t cf, void *aux)
{
	struct acpi_attach_args *aa = aux;
	ACPI_DEVICE_INFO *info = aa->aa_node->ad_devinfo;
	static const char * const ids[] = { "BCM2850", NULL };

	if (aa->aa_node->ad_type != ACPI_TYPE_DEVICE ||
	    (info->Valid & ACPI_VALID_HID) == 0 ||
	    strcmp(info->HardwareId.String, "BCM2712") != 0)
		return 0;
	return acpi_match_hid(info, ids);
}

static void
bcmv3d_attach(device_t parent, device_t self, void *aux)
{
	struct bcmv3d_softc *sc = device_private(self);
	struct acpi_attach_args *aa = aux;
	struct acpi_resources res;
	struct acpi_mem *mem;
	ACPI_INTEGER cca, sta;
	ACPI_STATUS status;
	unsigned int i;
	int error;

	sc->sc_dev = self;
	sc->sc_bst = aa->aa_memt;
	/* GPU0's own ACPI DMA tag: the 32-bit window honoring _CCA=0. */
	sc->sc_dmat = aa->aa_dmat;
	sc->sc_handle = aa->aa_node->ad_handle;
	aprint_naive("\n");
	aprint_normal(": BCM2712 V3D passive observer\n");
	if (ACPI_FAILURE(acpi_eval_integer(aa->aa_node->ad_handle,
	    "_STA", &sta)) || sta != 0x0f ||
	    ACPI_FAILURE(acpi_eval_integer(aa->aa_node->ad_handle,
	    "_CCA", &cca)) || cca != 0) {
		aprint_error_dev(self, "unsupported _STA/_CCA; no GPU access\n");
		return;
	}
	status = acpi_resource_parse(self, aa->aa_node->ad_handle, "_CRS",
	    &res, &acpi_resource_parse_ops_default);
	if (ACPI_FAILURE(status)) {
		aprint_error_dev(self, "cannot parse resources; no GPU access\n");
		return;
	}
	for (i = 0; i < BCMV3D_NREG; i++) {
		mem = acpi_res_mem(&res, i);
		if (mem == NULL || mem->ar_base != bcmv3d_addresses[i] ||
		    mem->ar_length != bcmv3d_lengths[i])
			break;
	}
	acpi_resource_cleanup(&res);
	if (i != BCMV3D_NREG) {
		aprint_error_dev(self, "unsupported resource %u; no GPU access\n", i);
		return;
	}
	/* Mailbox and PM providers must finish autoconfiguration first. */
	error = config_finalize_register(self, bcmv3d_finalize);
	if (error != 0)
		aprint_error_dev(self, "cannot defer observation: %d\n", error);
#if defined(BCM2712_V3D_RENDER) && defined(WSDISPLAY_BOOTFB_DEFER)
	else
		bcmv3d_bootfb_queue(sc);
#endif
}
