/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD, AI-assisted. Native DE3.52 single-plane consumer.
 * Register facts: BSP 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f and A733
 * User Manual v1.00. No GPL implementation is imported. See the native DE
 * contract. Attach does not read display MMIO or start a device.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/container_of.h>
#include <sys/mutex.h>
#include <sys/intr.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <arm/sunxi/sun60i_a733_ccu.h>
#include <arm/sunxi/sun60i_a733_pck600.h>
#include <arm/sunxi/sun60i_a733_display_owner.h>
#include <arm/sunxi/sun60i_a733_de352_dma.h>
#include <arm/sunxi/sun60i_a733_output.h>

#define DE352_BASE 0x05000000
#define DE352_SIZE 0x400000
#define DE352_IOMMU1_BASE 0x03910000
#define DE352_IOMMU1_SIZE 0x840
#define DE352_NSI_BASE 0x02020000
#define DE352_NSI_SIZE 0x420
#define DE352_TIMEOUT_NS UINT64_C(100000000)

/* Complete raw samples. Valid bits describe capture, not DMA acceptance. */
struct de352_memory_snapshot {
	uint32_t version, valid;
	uint32_t clocks[2][7], iommu[2][5], nsi[2][3];
};

struct sun60i_de352_softc {
	device_t dev;
	int phandle, power_phandle, output_phandle;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	bus_dma_tag_t dmat;
	struct clk *de0;
	kmutex_t lock;
	struct sysctllog *sysctllog;
	struct sun60i_a733_de352_dma *dma;
	struct sun60i_a733_output_mode mode;
	struct de352_memory_snapshot memory;
	bool valid, mapped, power_reserved, reserved, retained;
	int stage, step, result, release_error;
	uint32_t last_status;
	uint64_t vblank;
};

static const struct device_compatible_entry de352_compat[] = {
	{ .compat = "allwinner,display-engine-v352" }, DEVICE_COMPAT_EOL
};

static uint64_t
de352_now(void)
{
	struct timespec ts;
	getnanouptime(&ts);
	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

static int
de352_read(struct sun60i_de352_softc *sc, uint32_t reg, uint32_t *value)
{
	KASSERT(sc->retained && sc->mapped);
	if ((reg & 3) != 0 || reg > DE352_SIZE - 4)
		return EINVAL;
	/* A fault-aware read does not bound an unclocked bus transaction. */
	return bus_space_peek_4(sc->bst, sc->bsh, reg, value);
}

static int
de352_field(struct sun60i_de352_softc *sc, uint32_t reg,
    uint32_t mask, uint32_t value)
{
	uint32_t before, after;
	int error;

	if ((value & ~mask) != 0)
		return EINVAL;
	error = de352_read(sc, reg, &before);
	if (error != 0)
		return error;
	error = bus_space_poke_4(sc->bst, sc->bsh, reg,
	    (before & ~mask) | value);
	if (error != 0)
		return error;
	bus_space_barrier(sc->bst, sc->bsh, reg, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	error = de352_read(sc, reg, &after);
	if (error != 0)
		return error;
	return (after & mask) == value ? 0 : EIO;
}

/* Caller holds the one display reservation. No IOMMU or NSI write is made. */
static int
de352_memory(struct sun60i_de352_softc *sc)
{
	static const uint32_t iregs[] = { 0, 0x10, 0x14, 0x804, 0x838 };
	static const uint32_t nregs[] = { 0x410, 0x414, 0x418 };
	uint32_t clocks[2][7], iommu[2][5], nsi[2][3];
	bus_space_handle_t ih, nh;
	int error;

	memset(&sc->memory, 0, sizeof(sc->memory));
	sc->memory.version = 1;
	error = sun60i_a733_ccu_display_memory_state(sc->de0, sc, clocks);
	if (error != 0)
		return error;
	memcpy(sc->memory.clocks, clocks, sizeof(clocks));
	sc->memory.valid = 1;
	if (memcmp(clocks[0], clocks[1], sizeof(clocks[0])) != 0)
		return EBUSY;
	/* Fixed borrowed profile: static PERIPH0-600M for MBUS. No retune. */
	if ((clocks[0][0] & 0x10007) != 0x10007 ||
	    (clocks[0][1] & 0x10001) != 0x10001 ||
	    (clocks[0][2] & 0x9f000000) != 0x81000000 ||
	    (clocks[0][3] & 0x802) != 0x802 ||
	    (clocks[0][4] & 0x02000200) != 0x02000200 ||
	    (clocks[0][5] & 0x02000000) == 0 ||
	    (clocks[0][6] & 0xdf000000) != 0xc3000000)
		return EOPNOTSUPP;
	error = bus_space_map(sc->bst, DE352_IOMMU1_BASE, DE352_IOMMU1_SIZE,
	    0, &ih);
	if (error != 0)
		return error;
	error = bus_space_map(sc->bst, DE352_NSI_BASE, DE352_NSI_SIZE, 0, &nh);
	if (error != 0) {
		bus_space_unmap(sc->bst, ih, DE352_IOMMU1_SIZE);
		return error;
	}
	for (u_int s = 0; s < 2 && error == 0; s++) {
		for (u_int i = 0; i < __arraycount(iregs) && error == 0; i++)
			error = bus_space_peek_4(sc->bst, ih, iregs[i], &iommu[s][i]);
		for (u_int i = 0; i < __arraycount(nregs) && error == 0; i++)
			error = bus_space_peek_4(sc->bst, nh, nregs[i], &nsi[s][i]);
	}
	bus_space_unmap(sc->bst, nh, DE352_NSI_SIZE);
	bus_space_unmap(sc->bst, ih, DE352_IOMMU1_SIZE);
	if (error != 0)
		return error;
	memcpy(sc->memory.iommu, iommu, sizeof(iommu));
	memcpy(sc->memory.nsi, nsi, sizeof(nsi));
	sc->memory.valid |= 2;
	if (memcmp(iommu[0], iommu[1], sizeof(iommu[0])) != 0 ||
	    memcmp(nsi[0], nsi[1], sizeof(nsi[0])) != 0)
		return EBUSY;
	/* Unknown bypass/global-disable behavior is not an identity mapping. */
	if (iommu[0][0] != 0x16 || (iommu[0][1] & 4) == 0 ||
	    iommu[0][2] != 1 || iommu[0][3] != 1 || iommu[0][4] != 0 ||
	    (nsi[0][0] & 3) != 0 || (nsi[0][2] & 1) == 0)
		return EOPNOTSUPP;
	return 0;
}

/* Read only DE top until the internal core clock and reset are established. */
static int
de352_open_core(struct sun60i_de352_softc *sc)
{
	uint32_t value;
	int error;

	error = de352_read(sc, 0x8004, &value);
	if (error != 0)
		return error;
	/* No known or unknown module gate may be open before shared MBUS. */
	if (value != 0)
		return EBUSY;
	/* A closed gate alone must not revive a retained RTMX/DMA state. */
	error = de352_read(sc, 0x8000, &value);
	if (error != 0 || (value & 1) != 0)
		return error != 0 ? error : EBUSY;
	error = de352_read(sc, 0x8028, &value);
	if (error != 0 || (value & 0xfff) != 0)
		return error != 0 ? error : EOPNOTSUPP;
	error = de352_read(sc, 0x802c, &value);
	if (error != 0 || (value & 0xffff) != 0)
		return error != 0 ? error : EOPNOTSUPP;
	error = de352_read(sc, 0x8010, &value);
	if (error != 0)
		return error;
	for (u_int shift = 4; shift <= 12; shift += 4)
		if (((value >> shift) & 0xf) == 3)
			return EBUSY;
	error = de352_field(sc, 0x8000, 1, 1);
	if (error == 0)
		error = de352_field(sc, 0x8004, 1, 1);
	if (error == 0)
		error = de352_field(sc, 0x8008, 0x10, 0x10);
	if (error == 0)
		error = de352_field(sc, 0x8008, 1, 1);
	return error;
}

/* No unowned active processing block is reset or silently overwritten. */
static int
de352_idle(struct sun60i_de352_softc *sc)
{
	static const struct { uint32_t reg, mask; } idle[] = {
		{ 0x8100, 1 }, { 0x8110, UINT32_MAX },
		{ 0x80d0, 1 }, { 0x8020, 1 },
		{ 0x811c, UINT32_MAX }, { 0x281000, UINT32_MAX },
		{ 0x2810b0, 0x11111 },
		{ 0x105000, 1 }, { 0x105100, 1 },
		{ 0x105400, 1 }, { 0x125400, 1 }, { 0x1e5400, 1 },
		{ 0x1c0800, 1 }, { 0x1c4000, 0x40000001 },
		{ 0x1c5400, 1 }, { 0x285000, 1 },
		{ 0x287000, 0x11111 }, { 0x288000, 1 },
		{ 0x289000, 1 }, { 0x289040, 0x1f },
		{ 0x289050, 1 }, { 0x289070, 1 }, { 0x28907c, 1 },
		{ 0x28b000, 0x80000313 }
	};
	uint32_t value;
	int error;

	for (u_int i = 0; i < __arraycount(idle); i++) {
		error = de352_read(sc, idle[i].reg, &value);
		if (error != 0)
			return error;
		if ((value & idle[i].mask) != 0)
			return EBUSY;
	}
	/* All present OVL layers are mapped to the now-clocked core0. */
	for (u_int channel = 0; channel < 7; channel++) {
		const uint32_t base = 0x101000 +
		    (channel < 3 ? channel : channel + 3) * 0x20000;
		const uint32_t stride = channel < 3 ? 0x30 : 0x20;
		for (u_int layer = 0; layer < 4; layer++) {
			error = de352_read(sc, base + layer * stride, &value);
			if (error != 0 || (value & 1) != 0)
				return error != 0 ? error : EBUSY;
		}
	}
	/* Unknown RTMX control fields are not admitted by the fixed profile. */
	error = de352_read(sc, 0x8100, &value);
	if (error != 0 || (value & ~UINT32_C(0x302d1)) != 0)
		return error != 0 ? error : EOPNOTSUPP;
	/* The RCQ layer block includes only these seven fully known words. */
	error = de352_read(sc, 0x1c1000, &value);
	if (error != 0)
		return error;
	if ((value & ~UINT32_C(0xffb31f17)) != 0)
		return EOPNOTSUPP;
	error = de352_read(sc, 0x1c1004, &value);
	if (error != 0)
		return error;
	return (value & ~UINT32_C(0x1fff1fff)) == 0 ? 0 : EOPNOTSUPP;
}

static int
de352_configure(struct sun60i_de352_softc *sc)
{
	struct sun60i_a733_de352_rgb_words *w = &sc->dma->layer;
	const struct { uint32_t reg, mask, value; } fields[] = {
		{ 0x8050, 0xffff, 0x2000 },
		{ 0x8080, 0xffff0000, 0x08000000 },
		{ 0x802c, 0xf, 0 }, { 0x8030, 0xf000, 0x8000 },
		{ 0x8010, 0xf, 3 },
		{ 0x8108, 0x1fff1fff, w->size },
		{ 0x8100, 0x302d0, 0 },
		{ 0x1c1080, 0xff, w->address_high_byte },
		{ 0x1c1084, 0xff, 0 },
		{ 0x1c1088, 0x1fff1fff, w->channel_size },
		{ 0x1c10f0, 0x3fff3fff, 0 }, { 0x1c10f8, 0x3fff3fff, 0 },
		{ 0x281004, UINT32_MAX, 0 },
		{ 0x281008, 0x1fff1fff, w->size },
		{ 0x28100c, UINT32_MAX, 0 },
		{ 0x281080, 0xf, 3 }, { 0x281084, 0x3f, 0 },
		{ 0x281088, 0xffffff, 0 },
		{ 0x28108c, 0x1fff1fff, w->size },
		{ 0x281090, 0x0f0f0f0f, 0x00010001 },
		{ 0x2810fc, 0x303, 0 }
	};
	int error;

	for (u_int i = 0; i < __arraycount(fields); i++) {
		error = de352_field(sc, fields[i].reg, fields[i].mask,
		    fields[i].value);
		if (error != 0)
			return error;
	}
	return 0;
}

static int
de352_poll(struct sun60i_de352_softc *sc, uint32_t mask)
{
	const uint64_t start = de352_now();
	int error;

	do {
		error = de352_read(sc, 0x8104, &sc->last_status);
		if (error != 0)
			return error;
		if ((sc->last_status & mask) == mask)
			return 0;
		delay(10);
	} while (de352_now() - start < DE352_TIMEOUT_NS);
	return ETIMEDOUT;
}

/* The request is a command. It need not remain set after the write. */
static int
de352_request(struct sun60i_de352_softc *sc)
{
	const uint64_t start = de352_now();
	uint32_t status, control;
	int error, level;
	bool submitted;

	do {
		submitted = false;
		level = splhigh();
		error = de352_read(sc, 0x8104, &status);
		if (error == 0)
			sc->last_status = status;
		if (error == 0 && (status & 0x10) != 0) {
			error = de352_read(sc, 0x8110, &control);
			if (error == 0 && control != 0)
				error = EBUSY;
			if (error == 0) {
				error = bus_space_poke_4(sc->bst, sc->bsh,
				    0x8110, control | 1);
				bus_space_barrier(sc->bst, sc->bsh, 0x8110, 4,
				    BUS_SPACE_BARRIER_WRITE);
				submitted = error == 0;
			}
		}
		splx(level);
		if (error != 0 || submitted)
			return error;
		delay(10);
	} while (de352_now() - start < DE352_TIMEOUT_NS);
	return ETIMEDOUT;
}

static int
de352_vblank(struct sun60i_de352_softc *sc)
{
	const uint64_t start = de352_now();
	uint64_t first = 0, sequence;
	int error;
	bool have_first = false;

	/* Consume an old pending event, then require a distinct later event. */
	do {
		error = sun60i_a733_output_observed_vblank(sc->output_phandle,
		    sc, &sequence);
		if (error == 0) {
			if (have_first && sequence > first) {
				sc->vblank = sequence;
				return 0;
			}
			first = sequence;
			have_first = true;
		} else if (error != ENOENT) {
			return error;
		}
		delay(10);
	} while (de352_now() - start < DE352_TIMEOUT_NS);
	return ETIMEDOUT;
}

static int
de352_submit(struct sun60i_de352_softc *sc)
{
	uint32_t value;
	int error;

	error = de352_memory(sc);
	if (error != 0)
		return error;
	/* Retain both maps before even the first attempted pointer write. */
	error = sun60i_a733_de352_dma_publish(sc->dma);
	if (error != 0)
		return error;
	error = de352_field(sc, 0x8114, UINT32_MAX, (uint32_t)sc->dma->queue.address);
	if (error == 0)
		error = de352_field(sc, 0x8118, 0xff, 0);
	if (error == 0)
		/* Only zero -> 32 is admitted; no full-width length field claim. */
		error = de352_read(sc, 0x811c, &value);
	if (error == 0 && value != 0)
		error = EOPNOTSUPP;
	if (error == 0)
		error = bus_space_poke_4(sc->bst, sc->bsh, 0x811c, 32);
	if (error != 0)
		return error;
	bus_space_barrier(sc->bst, sc->bsh, 0x8114, 12,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	error = de352_read(sc, 0x811c, &value);
	if (error != 0 || value != 32)
		return error != 0 ? error : EIO;
	/* W1C: acknowledge only the prior RCQ finish and accept flags. */
	error = bus_space_poke_4(sc->bst, sc->bsh, 0x8104, 0x0c);
	if (error != 0)
		return error;
	bus_space_barrier(sc->bst, sc->bsh, 0x8104, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	error = de352_read(sc, 0x8104, &value);
	if (error != 0 || (value & 0x0c) != 0)
		return error != 0 ? error : EIO;
	error = de352_request(sc);
	if (error == 0)
		error = de352_poll(sc, 4);
	if (error != 0)
		return error;
	/* A flag alone is not proof that this list reached the owned registers. */
	error = de352_read(sc, 0x1c1000, &value);
	if (error != 0 || value != sc->dma->layer.attribute)
		return error != 0 ? error : EIO;
	error = de352_read(sc, 0x1c1010, &value);
	if (error != 0 || value != sc->dma->layer.address_low)
		return error != 0 ? error : EIO;
	error = de352_read(sc, 0x281000, &value);
	return error != 0 ? error : value == 0x100 ? 0 : EIO;
}

static int
de352_run(struct sun60i_de352_softc *sc)
{
	bool held = false;
	int error;

	if (!sc->valid)
		return ENXIO;
	if (sc->retained || sc->reserved || sc->power_reserved)
		return EBUSY;
	sc->step = 1;
	error = sun60i_a733_pck_display_reserve(sc->power_phandle, sc);
	if (error != 0)
		return error;
	sc->power_reserved = true;
	error = sun60i_a733_ccu_display_reserve(sc->de0, sc->power_phandle, sc);
	if (error != 0)
		goto fail;
	sc->reserved = true;
	sc->step = 2;
	/* This claim excludes native clock writers for the full DMA lifetime. */
	error = sun60i_a733_ccu_display_memory_reserve(sc->de0, sc);
	if (error != 0)
		goto fail;
	error = de352_memory(sc);
	if (error != 0)
		goto fail;
	sc->step = 3;
	error = sun60i_a733_ccu_display_begin_de0(sc->de0, sc, &held);
	sc->retained = held;
	if (error == 0 && !held)
		error = EPROTO;
	if (error != 0)
		goto fail;
	sc->step = 4;
	error = bus_space_map(sc->bst, DE352_BASE, DE352_SIZE, 0, &sc->bsh);
	if (error != 0)
		goto fail;
	sc->mapped = true;
	error = de352_open_core(sc);
	if (error == 0)
		error = de352_idle(sc);
	if (error != 0)
		goto fail;
	sc->step = 5;
	error = sun60i_a733_output_get_mode(sc->output_phandle, sc, &sc->mode);
	if (error != 0)
		goto fail;
	sc->step = 6;
	error = sun60i_a733_de352_dma_prepare(sc->dmat, sc->mode.hdisplay,
	    sc->mode.vdisplay, &sc->dma);
	if (error != 0)
		goto fail;
	sc->step = 7;
	error = sun60i_a733_output_prepare(sc->output_phandle, sc, &sc->mode);
	if (error != 0)
		goto fail;
	error = de352_configure(sc);
	if (error == 0)
		error = de352_field(sc, 0x810c, 1, 1);
	if (error == 0)
		error = de352_field(sc, 0x8100, 1, 1);
	if (error != 0)
		goto fail;
	sc->step = 8;
	error = sun60i_a733_output_start(sc->output_phandle, sc);
	if (error == 0)
		error = de352_submit(sc);
	if (error != 0)
		goto fail;
	sc->step = 9;
	error = de352_vblank(sc);
	if (error == 0)
		error = de352_memory(sc);
	if (error == 0) {
		sc->step = 10;
		return 0;
	}
fail:
	if (sc->retained) {
		(void)sun60i_a733_ccu_display_fault(sc->de0, sc, error);
		(void)sun60i_a733_output_fault(sc->output_phandle, sc, error);
		/* No reset, stop, pointer rewrite or DMA release after an error. */
	} else {
		if (sc->reserved) {
			sc->release_error = sun60i_a733_ccu_display_release(sc->de0, sc);
			if (sc->release_error == 0)
				sc->reserved = false;
		}
		/* Keep PCK if the dependent CCU claim did not release. */
		if (!sc->reserved && sc->power_reserved) {
			sc->release_error = sun60i_a733_pck_display_release(
			    sc->power_phandle, sc);
			if (sc->release_error == 0)
				sc->power_reserved = false;
		}
	}
	if (sc->dma != NULL && !sc->dma->published) {
		(void)sun60i_a733_de352_dma_destroy(sc->dma);
		sc->dma = NULL;
	}
	return error;
}

static int
de352_sysctl(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = container_of((int *)rnode->sysctl_data,
	    struct sun60i_de352_softc, stage);
	struct sysctlnode node = *rnode;
	int stage;
	int error;

	mutex_enter(&sc->lock);
	stage = sc->stage;
	mutex_exit(&sc->lock);
	node.sysctl_data = &stage;
	node.sysctl_size = sizeof(stage);
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	if (stage != 1)
		return EINVAL;
	if (!mutex_tryenter(&sc->lock))
		return EBUSY;
	if (sc->reserved || sc->retained || sc->power_reserved) {
		mutex_exit(&sc->lock);
		return EBUSY;
	}
	error = de352_run(sc);
	sc->stage = 1;
	sc->result = error;
	mutex_exit(&sc->lock);
	return error;
}

static int
de352_sysctl_memory(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct de352_memory_snapshot snapshot;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	snapshot = sc->memory;
	mutex_exit(&sc->lock);
	node.sysctl_data = &snapshot;
	node.sysctl_size = sizeof(snapshot);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

/* The initial profile permits no active IOMMU or NSI provider. */
static bool
de352_disabled_node(int node, const char *compatible, bus_addr_t base,
    bus_size_t length, const char *cell_name, uint32_t cell_count)
{
	const struct device_compatible_entry table[] = {
		{ .compat = compatible }, DEVICE_COMPAT_EOL
	};
	const char *status;
	bus_addr_t address;
	bus_size_t size;
	uint32_t cells;
	int len;

	if (node <= 0 || of_compatible_match(node, table) == 0 ||
	    fdtbus_get_reg(node, 0, &address, &size) != 0 ||
	    address != base || size != length ||
	    of_getprop_uint32(node, cell_name, &cells) != 0 || cells != cell_count)
		return false;
	status = fdtbus_get_prop(node, "status", &len);
	return status != NULL && len == 9 && memcmp(status, "disabled", 9) == 0;
}

static bool
de352_bindings(struct sun60i_de352_softc *sc)
{
	const uint32_t *p;
	int node, len;

	p = fdtbus_get_prop(sc->phandle, "iommus", &len);
	if (p == NULL || len != 12 || be32toh(p[1]) != 8 || be32toh(p[2]) != 1)
		return false;
	node = fdtbus_get_phandle_from_native(be32toh(p[0]));
	if (!de352_disabled_node(node, "allwinner,iommu-v20", 0x03900000,
	    0x20000, "#iommu-cells", 2))
		return false;
	p = fdtbus_get_prop(sc->phandle, "nsi", &len);
	if (p == NULL || len != 8 || be32toh(p[1]) != 2)
		return false;
	node = fdtbus_get_phandle_from_native(be32toh(p[0]));
	if (!de352_disabled_node(node, "allwinner,sunxi-nsi-v2", DE352_NSI_BASE,
	    0x10000, "#nsi-cells", 1))
		return false;
	/* No translated parent DMA window is accepted by this first profile. */
	for (node = sc->phandle; node > 0; node = OF_parent(node)) {
		if (OF_getproplen(node, "dma-ranges") > 0 ||
		    of_hasprop(node, "iommu-map") ||
		    (node != sc->phandle && of_hasprop(node, "iommus")))
			return false;
	}
	return true;
}

static int
de352_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct fdt_attach_args *faa = aux;
	(void)parent;
	(void)cf;
	return of_compatible_match(faa->faa_phandle, de352_compat);
}

static void
de352_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_de352_softc *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	const struct sysctlnode *node;
	const uint32_t *cells;
	bus_addr_t address;
	bus_size_t size;
	int len;

	(void)parent;
	sc->dev = self;
	sc->phandle = faa->faa_phandle;
	sc->bst = faa->faa_bst;
	sc->dmat = faa->faa_dmat;
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_NONE);
	aprint_naive("\n");
	aprint_normal(": A733 DE3.52, explicit single-plane probe only\n");
	if (fdtbus_get_reg(sc->phandle, 0, &address, &size) != 0 ||
	    address != DE352_BASE || size != DE352_SIZE)
		return;
	cells = fdtbus_get_prop(sc->phandle, "power-domains", &len);
	if (cells == NULL || len != 8 || be32toh(cells[1]) != 1)
		return;
	sc->power_phandle = fdtbus_get_phandle_from_native(be32toh(cells[0]));
	sc->de0 = fdtbus_clock_get_index(sc->phandle, 0);
	sc->output_phandle = fdtbus_get_phandle(sc->phandle, "output");
	if (sc->de0 == NULL || sc->power_phandle <= 0 || sc->output_phandle <= 0 ||
	    sc->dmat == NULL || !de352_bindings(sc)) {
		if (sc->de0 != NULL)
			clk_put(sc->de0);
		sc->de0 = NULL;
		return;
	}
	sc->valid = true;
	if (sysctl_createv(&sc->sysctllog, 0, NULL, &node, 0, CTLTYPE_NODE,
	    device_xname(self), NULL, NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL) != 0)
		return;
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "stage", SYSCTL_DESCR("Run one retained native RGB plane"),
	    de352_sysctl, 0, &sc->stage, sizeof(sc->stage), CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "step", NULL, NULL, 0, &sc->step, 0,
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "result", NULL, NULL, 0, &sc->result, 0,
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "release_error", NULL, NULL, 0, &sc->release_error, 0,
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "memory", SYSCTL_DESCR("Complete raw memory samples"),
	    de352_sysctl_memory, 0, sc, sizeof(sc->memory), CTL_CREATE, CTL_EOL);
}

CFATTACH_DECL_NEW(sun60i_a733_de352, sizeof(struct sun60i_de352_softc),
    de352_match, de352_attach, NULL, NULL);
