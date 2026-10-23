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
#include <sys/once.h>
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
#include <arm/sunxi/sun60i_a733_hdmi_phy.h>
#include <arm/sunxi/sun60i_a733_de352_scanout.h>

#define DE352_BASE 0x05000000
#define DE352_SIZE 0x400000
#define DE352_IOMMU1_BASE 0x03910000
#define DE352_IOMMU1_SIZE 0xe40
#define DE352_NSI_BASE 0x02020000
#define DE352_NSI_SIZE 0x420
#define DE352_TIMEOUT_NS UINT64_C(100000000)
/*
 * DE2TCON mux value of TCON_TV0. The vendor DRM driver programs the DE0
 * output endpoint ID. That ID is 4 for TV0 and 5 for TV1; ID 3 is unused.
 */
#define DE352_TV0_MUX 4

CTASSERT(A733_DISPLAY_NMEMORY_REGS == 7);

/* Complete raw samples. Valid bits describe capture, not DMA acceptance. */
struct de352_memory_snapshot {
	uint32_t version, valid;
	uint32_t clocks[2][A733_DISPLAY_NMEMORY_REGS], iommu[2][5], nsi[2][3];
};

enum {
	CI_VERSION, CI_RESET, CI_ENABLE, CI_AUTOGATE, CI_TTB, CI_PREFETCH,
	CI_FLUSH, CI_TLB_MODE, CI_TLB_REQUEST, CI_PTW_MODE, CI_PTW_REQUEST,
	CI_IRQ_MASK, CI_IRQ_STATUS, CI_MIC_BASE, CI_COUNT = CI_MIC_BASE + 6 * 4
};
struct de352_cold_state { uint32_t words[2][CI_COUNT]; };

/* Cached diagnostics only. These values do not establish hardware readiness. */
enum {
	CT_NONE, CT_DE_MAP, CT_COLD_CHECK, CT_PEER_ACCESS, CT_IOMMU_MAP,
	CT_CAPTURE, CT_ADMIT, CT_GUARD, CT_PUBLISH, CT_STEP_CHECK,
	CT_STEP_CAPTURE, CT_STEP_COMPARE, CT_DELAY, CT_BEFORE, CT_WRITE,
	CT_AFTER, CT_FINAL_CHECK, CT_FINAL_CAPTURE, CT_FINAL_COMPARE, CT_DONE
};
enum { CT_REG = 1, CT_BEFORE_VALID = 2, CT_REQUESTED = 4, CT_AFTER_VALID = 8 };
struct de352_cold_trace {
	uint32_t version, phase, op, valid, error;
	uint32_t reg, mask, before, requested, after;
	uint32_t capture_attempt, capture_generation, capture_words;
	uint32_t capture_valid, capture_reg;
	struct de352_cold_state snapshot;
};
CTASSERT(sizeof(struct de352_cold_trace) == (15 + 2 * CI_COUNT) * 4);

static void
de352_trace_phase(struct de352_cold_trace *trace, uint32_t phase)
{
	trace->phase = phase;
	trace->valid = 0;
	trace->reg = UINT32_MAX;
	trace->mask = trace->before = trace->requested = trace->after = 0;
}

enum { RT_NONE, RT_OUTPUT, RT_CONFIGURE, RT_MIXER_ENABLE, RT_DE_ENABLE, RT_DONE };
struct de352_run_trace {
	uint32_t version, valid, phase, result, width, height, pixel_clock_hz;
	uint32_t field_valid, reg, mask, before, requested, after;
};
CTASSERT(sizeof(struct de352_run_trace) == 13 * sizeof(uint32_t));

/* Cached step 8 diagnostics. The record adds reads only after a failure. */
enum {
	ST_NONE, ST_START, ST_MEMORY, ST_PUBLISH, ST_QUEUE, ST_ACK,
	ST_REQUEST, ST_POLL, ST_COPY, ST_DONE
};
enum { ST_NREGS = 10 };
struct de352_submit_trace {
	uint32_t version, valid, phase, result;
	uint32_t request_samples, request_busy, request_first, request_last;
	uint32_t request_control, request_us;
	uint32_t poll_samples, poll_last, poll_us;
	uint32_t status_or, status_and;
	uint32_t regs_valid, regs[ST_NREGS];
	uint32_t sample_error;
	struct sun60i_a733_output_sample sample;
};
CTASSERT(sizeof(struct de352_submit_trace) == 37 * sizeof(uint32_t));
static const uint32_t de352_submit_regs[ST_NREGS] = {
	0x8100, 0x8104, 0x8108, 0x810c, 0x8110, 0x8114, 0x8118, 0x811c,
	0x8010, 0x804c
};

struct sun60i_a733_de352_client {
	const void *owner;
	bool busy, attempted;
};

struct de352_frame {
	struct sun60i_a733_de352_frame info;
	struct sun60i_a733_de352_dma *dma;
	bool held;
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
	struct sun60i_a733_de352_guard *guard;
	struct de352_cold_state cold;
	struct de352_cold_trace cold_trace;
	struct de352_run_trace run_trace;
	struct de352_submit_trace submit_trace;
	bool cold_initialized;
	bool valid, mapped, power_reserved, reserved, retained;
	int stage, step, result, release_error;
	uint32_t last_status;
	uint64_t vblank;
	struct sun60i_a733_de352_client client;
	struct de352_frame active, pending;
};

static ONCE_DECL(de352_once);
static kmutex_t de352_registry_lock;
static struct sun60i_de352_softc *de352_provider;

static int
de352_registry_init(void)
{
	mutex_init(&de352_registry_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static struct sun60i_de352_softc *
de352_lookup(int phandle, const struct sun60i_a733_de352_client *client)
{
	struct sun60i_de352_softc *sc;

	RUN_ONCE(&de352_once, de352_registry_init);
	mutex_enter(&de352_registry_lock);
	sc = de352_provider;
	if (sc != NULL && (client != NULL ? client != &sc->client :
	    phandle != sc->phandle))
		sc = NULL;
	mutex_exit(&de352_registry_lock);
	/* This provider has no detach operation. Its lifetime is the whole boot. */
	return sc;
}

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
	struct de352_run_trace *trace = &sc->run_trace;
	const bool capture = sc->step == 7 && trace->valid == 1;
	uint32_t before, after;
	int error;

	if (capture) {
		trace->field_valid = 5;
		trace->reg = reg;
		trace->mask = mask;
		trace->requested = value;
		trace->before = trace->after = 0;
	}
	if ((value & ~mask) != 0)
		return EINVAL;
	error = de352_read(sc, reg, &before);
	if (error != 0)
		return error;
	if (capture) {
		trace->before = before;
		trace->field_valid |= 2;
	}
	error = bus_space_poke_4(sc->bst, sc->bsh, reg,
	    (before & ~mask) | value);
	if (error != 0)
		return error;
	bus_space_barrier(sc->bst, sc->bsh, reg, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	error = de352_read(sc, reg, &after);
	if (error != 0)
		return error;
	if (capture) {
		trace->after = after;
		trace->field_valid |= 8;
	}
	return (after & mask) == value ? 0 : EIO;
}

static int de352_cold_verify(struct sun60i_de352_softc *);

/* Caller holds the one display reservation. No IOMMU or NSI write is made. */
static int
de352_memory(struct sun60i_de352_softc *sc)
{
	static const uint32_t iregs[] = { 0, 0x10, 0x14, 0x804, 0x838 };
	static const uint32_t nregs[] = { 0x410, 0x414, 0x418 };
	uint32_t clocks[2][A733_DISPLAY_NMEMORY_REGS], iommu[2][5], nsi[2][3];
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
	if ((clocks[0][A733_DISPLAY_MEMORY_IOMMU1] & 0x10007) != 0x10007 ||
	    (clocks[0][A733_DISPLAY_MEMORY_NSI_CFG] & 0x10001) != 0x10001 ||
	    (clocks[0][A733_DISPLAY_MEMORY_MBUS] & 0x9f000000) != 0x81000000 ||
	    (clocks[0][A733_DISPLAY_MEMORY_MBUS_MASTER] & 0x802) != 0x802 ||
	    (clocks[0][A733_DISPLAY_MEMORY_PERIPH_GATE] & 0x02000200) != 0x02000200 ||
	    (clocks[0][A733_DISPLAY_MEMORY_PERIPH_STATUS] & 0x02000000) == 0 ||
	    (clocks[0][A733_DISPLAY_MEMORY_NSI] & 0xdf000000) != 0xc3000000)
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
	return sc->cold_initialized ? de352_cold_verify(sc) : 0;
}

/* A complete stable capture must precede this restricted local profile. */
static bool
de352_memory_manageable(const struct de352_memory_snapshot *m)
{
	return m->valid == 3 && m->iommu[0][0] == 0x16 &&
	    m->iommu[0][2] == 1 && m->iommu[0][3] <= 1 &&
	    m->iommu[0][4] == 0;
}

/* No allocated DMA or released internal core may precede local preparation. */
static int
de352_memory_cold(struct sun60i_de352_softc *sc)
{
	uint32_t word;
	int error;

	if (!sc->reserved || !sc->power_reserved || !sc->retained ||
	    !sc->mapped || sc->dma != NULL)
		return EINVAL;
	error = sun60i_a733_ccu_display_ready(sc->de0, sc);
	if (error == 0) {
		uint32_t peers[2][A733_DISPLAY_NISOLATION_REGS];

		error = sun60i_a733_ccu_display_isolation_state(sc->de0, sc, peers);
	}
	if (error != 0)
		return error;
	error = de352_read(sc, 0x8004, &word);
	if (error != 0 || word != 0)
		return error != 0 ? error : EBUSY;
	error = de352_read(sc, 0x8000, &word);
	if (error != 0 || (word & 1) != 0)
		return error != 0 ? error : EBUSY;
	return 0;
}

/* Fixed v0x16 global and MIC controls. No error-clear register is included. */
static uint32_t
de352_cold_reg(u_int index)
{
	static const uint32_t global[] = {
		0, 0x10, 0x14, 0x1c, 0x20, 0x2c, 0x30, 0x40, 0x54,
		0x60, 0x6c, 0x80, 0x88
	};
	static const uint32_t micro[] = { 4, 0x18, 0x30, 0x38 };

	KASSERT(index < CI_COUNT);
	if (index < CI_MIC_BASE)
		return global[index];
	return 0x400 + ((index - CI_MIC_BASE) / 4) * 0x200 +
	    micro[(index - CI_MIC_BASE) % 4];
}

static int
de352_cold_read(struct sun60i_de352_softc *sc, bus_space_handle_t ih,
    struct de352_cold_state *out, struct de352_cold_trace *trace)
{
	struct de352_cold_state state;
	uint32_t peers[2][A733_DISPLAY_NISOLATION_REGS];
	int error;

	if (trace != NULL) {
		trace->capture_attempt++;
		trace->capture_words = 0;
		trace->capture_reg = UINT32_MAX;
	}
	error = sun60i_a733_ccu_display_ready(sc->de0, sc);
	if (error == 0)
		error = sun60i_a733_ccu_display_isolation_state(sc->de0, sc, peers);
	if (error != 0)
		return error;
	for (u_int sample = 0; sample < 2; sample++) {
		for (u_int i = 0; i < CI_COUNT; i++) {
			if (trace != NULL)
				trace->capture_reg = de352_cold_reg(i);
			error = bus_space_peek_4(sc->bst, ih, de352_cold_reg(i),
			    &state.words[sample][i]);
			if (error != 0)
				return error;
			if (trace != NULL)
				trace->capture_words++;
		}
	}
	if (trace != NULL) {
		trace->snapshot = state;
		trace->capture_generation = trace->capture_attempt;
		trace->capture_valid = 1;
	}
	if (memcmp(state.words[0], state.words[1], sizeof(state.words[0])) != 0)
		return EBUSY;
	if (trace != NULL)
		trace->capture_valid |= 2;
	error = sun60i_a733_ccu_display_ready(sc->de0, sc);
	if (error == 0)
		error = sun60i_a733_ccu_display_isolation_state(sc->de0, sc, peers);
	if (error != 0)
		return error;
	if (trace != NULL)
		trace->capture_valid |= 4;
	*out = state;
	return 0;
}

static bool
de352_cold_admit(const struct de352_cold_state *state)
{
	const uint32_t *w = state->words[0];

	if (w[CI_VERSION] != 0x16 || w[CI_ENABLE] != 0 ||
	    (w[CI_AUTOGATE] != 0 && w[CI_AUTOGATE] != 0x7fff0003) ||
	    w[CI_TTB] > 0x3ffff || (w[CI_PREFETCH] & ~0x3007fU) != 0 ||
	    w[CI_FLUSH] != 0 || w[CI_TLB_MODE] > 1 || w[CI_TLB_REQUEST] != 0 ||
	    w[CI_PTW_MODE] > 1 || w[CI_PTW_REQUEST] != 0 ||
	    (w[CI_IRQ_MASK] & ~3U) != 0 || w[CI_IRQ_STATUS] != 0)
		return false;
	for (u_int i = CI_MIC_BASE; i < CI_COUNT; i += 4) {
		if (w[i] > 1 || w[i + 1] != 0 || w[i + 2] > 1 || w[i + 3] != 0)
			return false;
	}
	return true;
}

/* Observed request states. Neither transition proves flush completion. */
static bool
de352_cold_request_cleared(u_int index, uint32_t previous, uint32_t actual)
{
	if (actual != 0)
		return false;
	if (index == CI_FLUSH)
		return previous == 3;
	return index >= CI_MIC_BASE && (index - CI_MIC_BASE) % 4 == 1 &&
	    previous == 1;
}

static int
de352_cold_same(struct de352_cold_state *expected,
    const struct de352_cold_state *actual)
{
	for (u_int i = 0; i < CI_COUNT; i++) {
		if (actual->words[0][i] == expected->words[0][i])
			continue;
		if (de352_cold_request_cleared(i, expected->words[0][i],
		    actual->words[0][i]))
			continue;
		return EBUSY;
	}
	*expected = *actual;
	return 0;
}

static int
de352_cold_verify(struct sun60i_de352_softc *sc)
{
	struct de352_cold_state current;
	bus_space_handle_t ih;
	int error;

	if (sc->guard == NULL || !sc->guard->published)
		return EINVAL;
	error = bus_space_map(sc->bst, DE352_IOMMU1_BASE, DE352_IOMMU1_SIZE, 0, &ih);
	if (error != 0)
		return error;
	error = de352_cold_read(sc, ih, &current, NULL);
	if (error == 0)
		error = de352_cold_same(&sc->cold, &current);
	bus_space_unmap(sc->bst, ih, DE352_IOMMU1_SIZE);
	return error;
}

/* A bounded native request sequence. Physical RCQ remains the acceptance test. */
static int
de352_cold_prepare(struct sun60i_de352_softc *sc)
{
	struct { u_int index; uint32_t mask, value; } steps[32];
	struct de352_cold_state expected, current;
	struct de352_cold_trace *trace = &sc->cold_trace;
	bus_space_handle_t ih;
	uint32_t before, after, value;
	u_int count = 0;
	int error;

	de352_trace_phase(trace, CT_COLD_CHECK);
	error = de352_memory_cold(sc);
	if (error != 0)
		goto done;
	de352_trace_phase(trace, CT_PEER_ACCESS);
	error = sun60i_a733_ccu_display_begin_peer_access(sc->de0, sc);
	if (error != 0)
		goto done;
	/* Domain 2 is now retained ON. Only now may MIC0/1 be accessed. */
	de352_trace_phase(trace, CT_IOMMU_MAP);
	error = bus_space_map(sc->bst, DE352_IOMMU1_BASE, DE352_IOMMU1_SIZE, 0, &ih);
	if (error != 0)
		goto done;
	de352_trace_phase(trace, CT_CAPTURE);
	error = de352_cold_read(sc, ih, &expected, trace);
	if (error != 0)
		goto out;
	de352_trace_phase(trace, CT_ADMIT);
	if (!de352_cold_admit(&expected)) {
		error = EOPNOTSUPP;
		goto out;
	}
	de352_trace_phase(trace, CT_GUARD);
	error = sun60i_a733_de352_guard_prepare(sc->dmat, &sc->guard);
	if (error != 0)
		goto out;
#define CI_STEP(i, m, v) do { \
	KASSERT(count < __arraycount(steps)); \
	steps[count].index = (i); steps[count].mask = (m); \
	steps[count++].value = (v); \
} while (0)
	CI_STEP(CI_RESET, 0x3f, 0);
	CI_STEP(CI_IRQ_MASK, 3, 0);
	for (u_int i = CI_MIC_BASE; i < CI_COUNT; i += 4)
		CI_STEP(i + 2, 1, 0);
	for (u_int i = CI_MIC_BASE; i < CI_COUNT; i += 4)
		CI_STEP(i, 1, 1);
	CI_STEP(CI_TTB, 0x3ffff, (uint32_t)(sc->guard->directory.address >> 14));
	CI_STEP(CI_PREFETCH, 0x3007f, 0);
	CI_STEP(CI_TLB_MODE, 1, 1);
	CI_STEP(CI_PTW_MODE, 1, 1);
	CI_STEP(CI_RESET, 0x3f, 0x3f);
	CI_STEP(CI_FLUSH, 3, 3);
	for (u_int i = CI_MIC_BASE; i < CI_COUNT; i += 4)
		CI_STEP(i + 1, 1, 1);
	CI_STEP(CI_COUNT, 0, 10);
	CI_STEP(CI_FLUSH, 3, 0);
	CI_STEP(CI_ENABLE, 1, 1);
#undef CI_STEP
	/* No later uncertainty permits release of this guard directory. */
	de352_trace_phase(trace, CT_PUBLISH);
	error = sun60i_a733_de352_guard_publish(sc->guard);
	if (error != 0)
		goto out;
	for (u_int n = 0; n < count; n++) {
		const u_int index = steps[n].index;
		uint32_t reg;

		trace->op = n;
		de352_trace_phase(trace, CT_STEP_CHECK);
		error = de352_memory_cold(sc);
		if (error != 0)
			goto out;
		de352_trace_phase(trace, CT_STEP_CAPTURE);
		error = de352_cold_read(sc, ih, &current, trace);
		if (error != 0)
			goto out;
		de352_trace_phase(trace, CT_STEP_COMPARE);
		error = de352_cold_same(&expected, &current);
		if (error != 0)
			goto out;
		if (index == CI_COUNT) {
			/* Pinned BSP timing input, not observed completion. */
			de352_trace_phase(trace, CT_DELAY);
			trace->requested = steps[n].value;
			trace->valid = CT_REQUESTED;
			delay(steps[n].value);
			continue;
		}
		reg = de352_cold_reg(index);
		de352_trace_phase(trace, CT_BEFORE);
		trace->reg = reg;
		trace->mask = steps[n].mask;
		trace->valid = CT_REG;
		error = bus_space_peek_4(sc->bst, ih, reg, &before);
		if (error != 0)
			goto out;
		trace->before = before;
		trace->valid |= CT_BEFORE_VALID;
		if (before != expected.words[0][index] &&
		    !de352_cold_request_cleared(index,
		    expected.words[0][index], before)) {
			error = EBUSY;
			goto out;
		}
		value = (before & ~steps[n].mask) | steps[n].value;
		trace->requested = value;
		trace->valid |= CT_REQUESTED;
		/* Deassert the global request even when its readback is zero. */
		if (value == before && !(index == CI_FLUSH && steps[n].value == 0))
			continue;
		trace->phase = CT_WRITE;
		error = bus_space_poke_4(sc->bst, ih, reg, value);
		if (error != 0)
			goto out;
		bus_space_barrier(sc->bst, ih, reg, 4,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		trace->phase = CT_AFTER;
		error = bus_space_peek_4(sc->bst, ih, reg, &after);
		if (error != 0)
			goto out;
		trace->after = after;
		trace->valid |= CT_AFTER_VALID;
		if (after != value &&
		    !de352_cold_request_cleared(index, value, after)) {
			error = EIO;
			goto out;
		}
		expected.words[0][index] = expected.words[1][index] = after;
	}
	trace->op = UINT32_MAX;
	de352_trace_phase(trace, CT_FINAL_CHECK);
	error = de352_memory_cold(sc);
	if (error != 0)
		goto out;
	de352_trace_phase(trace, CT_FINAL_CAPTURE);
	error = de352_cold_read(sc, ih, &current, trace);
	if (error != 0)
		goto out;
	de352_trace_phase(trace, CT_FINAL_COMPARE);
	error = de352_cold_same(&expected, &current);
	if (error == 0) {
		sc->cold = expected;
		sc->cold_initialized = true;
		de352_trace_phase(trace, CT_DONE);
	}
out:
	bus_space_unmap(sc->bst, ih, DE352_IOMMU1_SIZE);
	if (sc->guard != NULL && !sc->guard->published) {
		(void)sun60i_a733_de352_guard_destroy(sc->guard);
		sc->guard = NULL;
	}
done:
	trace->error = error;
	return error;
}

/* No global IOMMU, table, interrupt, shared clock or peer register is written. */
static int
de352_memory_prepare(struct sun60i_de352_softc *sc)
{
	static const struct {
		bool iommu;
		uint32_t reg, mask, value, sample;
	} updates[] = {
		{ true, 0x804, 1, 1, 3 },
		{ false, 0x410, 3, 0, 0 },
		{ false, 0x418, 1, 1, 2 },
		{ true, 0x10, 4, 4, 1 },
	};
	struct de352_memory_snapshot expected = sc->memory;
	bus_space_handle_t ih, nh;
	uint32_t before, after;
	int error;

	error = de352_memory_cold(sc);
	if (error != 0)
		return error;
	if (!de352_memory_manageable(&expected))
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
	for (u_int i = 0; i < __arraycount(updates); i++) {
		bus_space_handle_t handle;
		uint32_t value, *words;

		/* Repeat the retained owner, cold-core and exact snapshot proof. */
		error = de352_memory_cold(sc);
		if (error != 0)
			break;
		error = de352_memory(sc);
		if (error != 0 && error != EOPNOTSUPP)
			break;
		if (!de352_memory_manageable(&sc->memory) ||
		    memcmp(&expected, &sc->memory, sizeof(expected)) != 0) {
			error = EBUSY;
			break;
		}
		handle = updates[i].iommu ? ih : nh;
		words = updates[i].iommu ? expected.iommu[0] : expected.nsi[0];
		error = bus_space_peek_4(sc->bst, handle, updates[i].reg, &before);
		if (error != 0)
			break;
		if (before != words[updates[i].sample]) {
			error = EBUSY;
			break;
		}
		value = (before & ~updates[i].mask) | updates[i].value;
		if (value == before)
			continue;
		/* The retained lease also covers a faulting first write attempt. */
		error = bus_space_poke_4(sc->bst, handle, updates[i].reg, value);
		if (error != 0)
			break;
		bus_space_barrier(sc->bst, handle, updates[i].reg, 4,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		error = bus_space_peek_4(sc->bst, handle, updates[i].reg, &after);
		if (error != 0)
			break;
		if (after != value) {
			error = EIO;
			break;
		}
		for (u_int sample = 0; sample < 2; sample++) {
			words = updates[i].iommu ? expected.iommu[sample] :
			    expected.nsi[sample];
			words[updates[i].sample] = value;
		}
	}
	bus_space_unmap(sc->bst, nh, DE352_NSI_SIZE);
	bus_space_unmap(sc->bst, ih, DE352_IOMMU1_SIZE);
	if (error == 0)
		error = de352_memory_cold(sc);
	if (error == 0)
		error = de352_memory(sc);
	if (error == 0 && memcmp(&expected, &sc->memory, sizeof(expected)) != 0)
		error = EBUSY;
	/* The outer coordinator quarantines failure. This helper never releases. */
	return error;
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
		if (((value >> shift) & 0xf) == DE352_TV0_MUX)
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
		{ 0x8010, 0xf, DE352_TV0_MUX },
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

static void
de352_trace_status(struct de352_submit_trace *t, uint32_t status)
{
	t->status_or |= status;
	t->status_and &= status;
}

static uint32_t
de352_elapsed_us(uint64_t start)
{
	const uint64_t us = (de352_now() - start) / 1000;

	return us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

/* The trace pointer is NULL except for the first step 8 submission. */
static int
de352_poll(struct sun60i_de352_softc *sc, uint32_t mask,
    struct de352_submit_trace *t)
{
	const uint64_t start = de352_now();
	int error;

	do {
		error = de352_read(sc, 0x8104, &sc->last_status);
		if (error != 0)
			return error;
		if (t != NULL) {
			t->poll_samples++;
			t->poll_last = sc->last_status;
			t->poll_us = de352_elapsed_us(start);
			de352_trace_status(t, sc->last_status);
		}
		if ((sc->last_status & mask) == mask)
			return 0;
		delay(10);
	} while (de352_now() - start < DE352_TIMEOUT_NS);
	if (t != NULL)
		t->poll_us = de352_elapsed_us(start);
	return ETIMEDOUT;
}

/* The request is a command. It need not remain set after the write. */
static int
de352_request(struct sun60i_de352_softc *sc, struct de352_submit_trace *t)
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
		if (error == 0 && t != NULL) {
			if (t->request_samples++ == 0)
				t->request_first = status;
			t->request_last = status;
			t->request_busy += (status & 0x10) != 0;
			de352_trace_status(t, status);
		}
		if (error == 0 && (status & 0x10) != 0) {
			error = de352_read(sc, 0x8110, &control);
			if (error == 0 && t != NULL)
				t->request_control = control;
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
		if (t != NULL)
			t->request_us = de352_elapsed_us(start);
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
de352_submit_dma(struct sun60i_de352_softc *sc,
    struct sun60i_a733_de352_dma *dma, bool initial)
{
	const uint32_t words[] = { dma->layer.attribute, dma->layer.size,
	    dma->layer.coordinate, dma->layer.pitch, dma->layer.address_low,
	    dma->layer.bottom_low, dma->layer.fill_color };
	struct de352_submit_trace *t = initial && sc->step == 8 &&
	    sc->submit_trace.valid == 1 ? &sc->submit_trace : NULL;
	uint32_t value;
	int error;

	if (t != NULL)
		t->phase = ST_MEMORY;
	error = de352_memory(sc);
	if (error != 0)
		return error;
	if (t != NULL)
		t->phase = ST_PUBLISH;
	/* Retain both maps before even the first attempted pointer write. */
	error = sun60i_a733_de352_dma_publish(dma);
	if (error != 0)
		return error;
	if (t != NULL)
		t->phase = ST_QUEUE;
	error = de352_field(sc, 0x8114, UINT32_MAX, (uint32_t)dma->queue.address);
	if (error == 0)
		error = de352_field(sc, 0x8118, 0xff, 0);
	if (error == 0)
		/* Admit only initial zero -> 32, or an owned 32-byte replacement. */
		error = de352_read(sc, 0x811c, &value);
	if (error == 0 && value != (initial ? 0 : 32))
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
	if (t != NULL)
		t->phase = ST_ACK;
	/* W1C: acknowledge only the prior RCQ finish and accept flags. */
	error = bus_space_poke_4(sc->bst, sc->bsh, 0x8104, 0x0c);
	if (error != 0)
		return error;
	bus_space_barrier(sc->bst, sc->bsh, 0x8104, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	error = de352_read(sc, 0x8104, &value);
	if (error != 0 || (value & 0x0c) != 0)
		return error != 0 ? error : EIO;
	if (t != NULL)
		t->phase = ST_REQUEST;
	error = de352_request(sc, t);
	if (error == 0 && t != NULL)
		t->phase = ST_POLL;
	if (error == 0)
		error = de352_poll(sc, 4, t);
	if (error != 0)
		return error;
	if (t != NULL)
		t->phase = ST_COPY;
	/* A flag alone is not proof that this list reached the owned registers. */
	for (u_int i = 0; i < __arraycount(words); i++) {
		error = de352_read(sc, 0x1c1000 + 4 * i, &value);
		if (error != 0 || value != words[i])
			return error != 0 ? error : EIO;
	}
	error = de352_read(sc, 0x281000, &value);
	return error != 0 ? error : value == (dma->disabled ? 0x101 : 0x100) ? 0 : EIO;
}

static int
de352_submit(struct sun60i_de352_softc *sc)
{
	return de352_submit_dma(sc, sc->dma, true);
}

/* Reads only. Capture state before the fault latches the output provider. */
static void
de352_submit_failure(struct sun60i_de352_softc *sc, int error)
{
	struct de352_submit_trace *t = &sc->submit_trace;

	t->result = error;
	for (u_int i = 0; i < ST_NREGS; i++)
		if (de352_read(sc, de352_submit_regs[i], &t->regs[i]) == 0)
			t->regs_valid |= 1U << i;
	t->sample_error = sun60i_a733_output_sample(sc->output_phandle, sc,
	    &t->sample);
	t->valid = 3;
}

static int
de352_run(struct sun60i_de352_softc *sc)
{
	bool held = false, managed = false, cold = false;
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
	if (error == EOPNOTSUPP && sc->memory.valid == 3 &&
	    sc->memory.iommu[0][0] == 0x16 && sc->memory.iommu[0][2] == 0 &&
	    sc->memory.iommu[0][3] <= 1 && sc->memory.iommu[0][4] == 0)
		cold = true;
	if (error == EOPNOTSUPP && (cold || de352_memory_manageable(&sc->memory))) {
		/* Reserve inactive peers before any retained local write. */
		error = sun60i_a733_ccu_display_isolation_reserve(sc->de0, sc);
		managed = error == 0;
	}
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
	if (cold) {
		memset(&sc->cold_trace, 0, sizeof(sc->cold_trace));
		sc->cold_trace.version = 1;
		sc->cold_trace.op = UINT32_MAX;
		sc->cold_trace.capture_reg = UINT32_MAX;
		de352_trace_phase(&sc->cold_trace, CT_DE_MAP);
	}
	error = bus_space_map(sc->bst, DE352_BASE, DE352_SIZE, 0, &sc->bsh);
	if (error != 0) {
		if (cold)
			sc->cold_trace.error = error;
		goto fail;
	}
	sc->mapped = true;
	if (cold) {
		error = de352_cold_prepare(sc);
		if (error != 0)
			goto fail;
		error = de352_memory(sc);
		if (error != 0 && error != EOPNOTSUPP)
			goto fail;
	}
	if (managed) {
		error = de352_memory_prepare(sc);
		if (error != 0)
			goto fail;
	}
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
	memset(&sc->run_trace, 0, sizeof(sc->run_trace));
	sc->run_trace.version = sc->run_trace.valid = 1;
	sc->run_trace.phase = RT_OUTPUT;
	sc->run_trace.width = sc->mode.hdisplay;
	sc->run_trace.height = sc->mode.vdisplay;
	sc->run_trace.pixel_clock_hz = sc->mode.pixel_clock_hz;
	error = sun60i_a733_output_prepare(sc->output_phandle, sc, &sc->mode);
	if (error != 0)
		goto fail;
	sc->run_trace.phase = RT_CONFIGURE;
	error = de352_configure(sc);
	if (error == 0) {
		sc->run_trace.phase = RT_MIXER_ENABLE;
		error = de352_field(sc, 0x810c, 1, 1);
	}
	if (error == 0) {
		sc->run_trace.phase = RT_DE_ENABLE;
		error = de352_field(sc, 0x8100, 1, 1);
	}
	if (error != 0)
		goto fail;
	sc->run_trace.valid = 3;
	sc->run_trace.phase = RT_DONE;
	sc->step = 8;
	memset(&sc->submit_trace, 0, sizeof(sc->submit_trace));
	sc->submit_trace.version = sc->submit_trace.valid = 1;
	sc->submit_trace.phase = ST_START;
	sc->submit_trace.status_and = UINT32_MAX;
	error = sun60i_a733_output_start(sc->output_phandle, sc);
	if (error == 0)
		error = de352_submit(sc);
	if (error != 0)
		goto fail;
	sc->submit_trace.phase = ST_DONE;
	sc->submit_trace.valid = 3;
	sc->step = 9;
	error = de352_vblank(sc);
	if (error == 0)
		error = de352_memory(sc);
	if (error == 0) {
		sc->step = 10;
		return 0;
	}
fail:
	if (sc->step == 7 && sc->run_trace.valid == 1) {
		sc->run_trace.result = error;
		sc->run_trace.valid = 3;
	}
	if (sc->step == 8 && sc->submit_trace.valid == 1)
		de352_submit_failure(sc, error);
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

static int
de352_sysctl_cold_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct de352_cold_trace trace;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	trace = sc->cold_trace;
	mutex_exit(&sc->lock);
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_run_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct de352_run_trace trace;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	trace = sc->run_trace;
	mutex_exit(&sc->lock);
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_submit_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct de352_submit_trace trace;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	trace = sc->submit_trace;
	mutex_exit(&sc->lock);
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_output_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct sun60i_a733_output_trace trace;
	int error;

	if (newp != NULL)
		return EROFS;
	/* The normal run uses this same DE -> output lock order. */
	mutex_enter(&sc->lock);
	error = sun60i_a733_output_cached_trace(sc->output_phandle, &trace);
	mutex_exit(&sc->lock);
	if (error != 0)
		return error;
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_phy_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct sun60i_a733_hdmi_phy_trace trace;
	int error;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	error = sun60i_a733_output_cached_phy_trace(sc->output_phandle, &trace);
	mutex_exit(&sc->lock);
	if (error != 0)
		return error;
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
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
	RUN_ONCE(&de352_once, de352_registry_init);
	mutex_enter(&de352_registry_lock);
	if (de352_provider != NULL) {
		mutex_exit(&de352_registry_lock);
		clk_put(sc->de0);
		sc->de0 = NULL;
		return;
	}
	de352_provider = sc;
	mutex_exit(&de352_registry_lock);
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
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "cold_trace", SYSCTL_DESCR("Cached cold-init diagnostics"),
	    de352_sysctl_cold_trace, 0, sc, sizeof(sc->cold_trace),
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "run_trace", SYSCTL_DESCR("Cached step 7 diagnostics"),
	    de352_sysctl_run_trace, 0, sc, sizeof(sc->run_trace), CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "submit_trace", SYSCTL_DESCR("Cached step 8 diagnostics"),
	    de352_sysctl_submit_trace, 0, sc, sizeof(sc->submit_trace),
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "output_trace", SYSCTL_DESCR("Cached output preparation"),
	    de352_sysctl_output_trace, 0, sc, sizeof(struct sun60i_a733_output_trace),
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "phy_trace", SYSCTL_DESCR("Cached PHY I2C comparison"),
	    de352_sysctl_phy_trace, 0, sc, sizeof(struct sun60i_a733_hdmi_phy_trace),
	    CTL_CREATE, CTL_EOL);
}

CFATTACH_DECL_NEW(sun60i_a733_de, sizeof(struct sun60i_de352_softc),
    de352_match, de352_attach, NULL, NULL);

static int
de352_state_locked(struct sun60i_de352_softc *sc,
    struct sun60i_a733_de352_state *state)
{
	KASSERT(mutex_owned(&sc->lock));
	if (!sc->valid)
		return ENXIO;
	if (sc->stage != 1)
		return EAGAIN;
	if (sc->result != 0)
		return sc->result;
	if (sc->step != 10 || !sc->retained || sc->dma == NULL ||
	    !sc->dma->published || !sc->dma->synchronized)
		return EIO;
	/* Bootstrap storage and its restricted native tag stay retained. */
	state->mode = sc->mode;
	state->dmat = sc->dma->tag;
	state->max_dma_address = UINT32_MAX;
	state->observed_sequence = sc->vblank;
	return 0;
}

int
sun60i_a733_de352_get_state(int phandle, struct sun60i_a733_de352_state *out)
{
	struct sun60i_a733_de352_state state = { 0 };
	struct sun60i_de352_softc *sc;
	int error;

	if (out == NULL)
		return EINVAL;
	sc = de352_lookup(phandle, NULL);
	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->lock);
	error = de352_state_locked(sc, &state);
	if (error == 0)
		*out = state;
	mutex_exit(&sc->lock);
	return error;
}

int
sun60i_a733_de352_acquire(int phandle, const void *owner,
    struct sun60i_a733_de352_client **out)
{
	struct sun60i_a733_de352_state state = { 0 };
	struct sun60i_de352_softc *sc;
	int error;

	if (out == NULL || owner == NULL)
		return EINVAL;
	sc = de352_lookup(phandle, NULL);
	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->lock);
	error = de352_state_locked(sc, &state);
	if (error == 0 && sc->client.owner != NULL)
		error = EBUSY;
	if (error == 0) {
		sc->client.owner = owner;
		*out = &sc->client;
	}
	mutex_exit(&sc->lock);
	return error;
}

int
sun60i_a733_de352_release(struct sun60i_a733_de352_client *client)
{
	struct sun60i_de352_softc *sc;
	int error = 0;

	if (client == NULL || (sc = de352_lookup(0, client)) == NULL)
		return EINVAL;
	mutex_enter(&sc->lock);
	if (client->owner == NULL)
		error = EINVAL;
	else if (client->busy || client->attempted)
		error = EBUSY;
	else
		client->owner = NULL;
	mutex_exit(&sc->lock);
	return error;
}

int
sun60i_a733_de352_set_vblank_handler(struct sun60i_a733_de352_client *client,
    sun60i_a733_vblank_fn fn, void *arg)
{
	struct sun60i_a733_de352_state state = { 0 };
	struct sun60i_de352_softc *sc;
	int error;

	if (client == NULL || (fn != NULL && arg == NULL) ||
	    (sc = de352_lookup(0, client)) == NULL)
		return EINVAL;
	mutex_enter(&sc->lock);
	error = client->owner == NULL ? EINVAL : 0;
	/* A removal is allowed after a later DE failure. */
	if (error == 0 && fn != NULL)
		error = de352_state_locked(sc, &state);
	if (error == 0)
		error = sun60i_a733_output_set_vblank_handler(sc->output_phandle,
		    sc, fn, arg);
	mutex_exit(&sc->lock);
	return error;
}

int
sun60i_a733_de352_detect(struct sun60i_a733_de352_client *client, bool *out)
{
	struct sun60i_de352_softc *sc;
	int error;

	if (client == NULL || out == NULL ||
	    (sc = de352_lookup(0, client)) == NULL)
		return EINVAL;
	mutex_enter(&sc->lock);
	error = client->owner == NULL ? EINVAL : 0;
	if (error == 0)
		error = sun60i_a733_output_detect(sc->output_phandle, sc, out);
	mutex_exit(&sc->lock);
	return error;
}

/* Check the owned old list before replacing any pointer. No writes here. */
static int
de352_replace_ready(struct sun60i_de352_softc *sc)
{
	const struct sun60i_a733_de352_dma *old =
	    sc->active.dma != NULL ? sc->active.dma : sc->dma;
	const struct { uint32_t reg, mask, value; } fields[] = {
		{ 0x8110, UINT32_MAX, 0 },
		{ 0x8114, UINT32_MAX, (uint32_t)old->queue.address },
		{ 0x8118, 0xff, 0 }, { 0x811c, UINT32_MAX, 32 },
		{ 0x1c1080, 0xff, 0 }, { 0x8100, 1, 1 }
	};
	uint32_t value;
	int error;

	error = sun60i_a733_ccu_display_ready(sc->de0, sc);
	if (error != 0)
		return error;
	for (u_int i = 0; i < __arraycount(fields); i++) {
		error = de352_read(sc, fields[i].reg, &value);
		if (error != 0)
			return error;
		if ((value & fields[i].mask) != fields[i].value)
			return EBUSY;
	}
	return 0;
}

/* Discard a pending old event; only later observed events retire old storage. */
static int
de352_replacement_vblank(struct sun60i_de352_softc *sc,
    struct sun60i_a733_de352_completion *completion)
{
	const uint64_t start = de352_now();
	uint64_t sequence, last = sc->vblank;
	u_int count = 0;
	int error;

	error = sun60i_a733_output_observed_vblank(sc->output_phandle, sc,
	    &sequence);
	if (error == 0)
		last = sequence;
	else if (error != ENOENT)
		return error;
	do {
		error = sun60i_a733_output_observed_vblank(sc->output_phandle,
		    sc, &sequence);
		if (error == 0) {
			if (sequence <= last)
				return EPROTO;
			last = sequence;
			if (++count == 2) {
				completion->observed_sequence = sequence;
				completion->observed_time_ns = de352_now();
				return 0;
			}
		} else if (error != ENOENT) {
			return error;
		}
		delay(10);
	} while (de352_now() - start < DE352_TIMEOUT_NS);
	return ETIMEDOUT;
}

static void
de352_frame_drop(struct de352_frame *frame, bool retired)
{
	if (frame->dma != NULL) {
		int error = retired ? sun60i_a733_de352_dma_retire(frame->dma) :
		    sun60i_a733_de352_dma_destroy(frame->dma);
		KASSERT(error == 0);
		if (error != 0)
			return; /* Preserve the pixel reference on an internal fault. */
	}
	if (frame->held)
		frame->info.release(frame->info.cookie);
}

static int
de352_update(struct sun60i_a733_de352_client *client,
    const struct sun60i_a733_de352_frame *frame,
    struct sun60i_a733_de352_completion *out)
{
	struct sun60i_a733_de352_completion completion;
	struct sun60i_a733_de352_state state = { 0 };
	struct de352_frame next = { 0 }, retired = { 0 };
	struct sun60i_de352_softc *sc;
	int error;

	if (client == NULL || out == NULL ||
	    (frame != NULL && (frame->retain == NULL ||
	    frame->release == NULL || frame->map == NULL)))
		return EINVAL;
	sc = de352_lookup(0, client);
	if (sc == NULL)
		return EINVAL;
	if (frame != NULL)
		next.info = *frame;
	mutex_enter(&sc->lock);
	error = de352_state_locked(sc, &state);
	if (error == 0 && (client->owner == NULL || client->busy))
		error = EBUSY;
	if (error == 0 && frame != NULL && (next.info.dmat != state.dmat ||
	    next.info.width != state.mode.hdisplay ||
	    next.info.height != state.mode.vdisplay))
		error = EINVAL;
	if (error != 0) {
		mutex_exit(&sc->lock);
		return error;
	}
	client->busy = true;
	mutex_exit(&sc->lock);

	/* Both callbacks, allocation and producer lifetime are outside our lock. */
	error = frame != NULL ? next.info.retain(next.info.cookie) : 0;
	if (error == 0 && frame != NULL) {
		next.held = true;
		error = sun60i_a733_de352_dma_prepare_map(state.dmat, next.info.map,
		    next.info.bytes, next.info.width, next.info.height,
		    next.info.pitch, &next.dma);
	} else if (frame == NULL)
		error = sun60i_a733_de352_dma_prepare_disabled(state.dmat, &next.dma);
	mutex_enter(&sc->lock);
	if (error == 0) {
		error = de352_replace_ready(sc);
		if (error == 0) {
			sc->pending = next;
			error = de352_submit_dma(sc, next.dma, false);
		}
		if (next.dma->published)
			client->attempted = true;
		if (error == 0)
			error = de352_replacement_vblank(sc, &completion);
		if (error == 0)
			error = de352_memory(sc);
		if (error != 0) {
			sc->result = error;
			(void)sun60i_a733_ccu_display_fault(sc->de0, sc, error);
			(void)sun60i_a733_output_fault(sc->output_phandle, sc, error);
		} else {
			retired = sc->active;
			sc->active = next;
			sc->vblank = completion.observed_sequence;
			*out = completion;
		}
	}
	if (error == 0 || next.dma == NULL || !next.dma->published)
		memset(&sc->pending, 0, sizeof(sc->pending));
	client->busy = false;
	mutex_exit(&sc->lock);

	if (error != 0 && (next.dma == NULL || !next.dma->published))
		de352_frame_drop(&next, false);
	if (retired.dma != NULL)
		de352_frame_drop(&retired, true);
	return error;
}

int
sun60i_a733_de352_submit(struct sun60i_a733_de352_client *client,
    const struct sun60i_a733_de352_frame *frame,
    struct sun60i_a733_de352_completion *out)
{
	if (frame == NULL)
		return EINVAL;
	return de352_update(client, frame, out);
}

int
sun60i_a733_de352_disable_plane(struct sun60i_a733_de352_client *client,
    struct sun60i_a733_de352_completion *out)
{
	return de352_update(client, NULL, out);
}
