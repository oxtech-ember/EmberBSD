/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD A133 PowerVR FDT owner, AI-assisted. */
#include "opt_pvr_fw.h"
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/mutex.h>
#include <sys/intr.h>
#include <sys/kmem.h>
#include <sys/sha2.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <libfdt.h>
#include <dev/clk/clk.h>
#include <dev/fdt/fdtvar.h>
#include <dev/firmload.h>
#include <dev/ofw/openfirm.h>
#include <dev/powervr/pvr_ge8300.h>
#include <dev/powervr/pvr_fwboot.h>
#include <dev/powervr/pvr_fwprobe.h>
#include <dev/powervr/pvr_fwheap.h>
#include <dev/powervr/pvr_mips_irq.h>
#include <arm/sunxi/sun50i_a133_gpu_owner.h>
#ifdef PVR_GE8300_DRM
#include <external/bsd/drm2/powervr/pvr_drv.h>
#include <external/bsd/drm2/powervr/pvr_fwbridge.h>
#include <external/bsd/drm2/powervr/pvr_query.h>
#endif

#define A133_GPU_BASE UINT64_C(0x01800000)
#define A133_GPU_SIZE UINT64_C(0x80000)
#define A133_GPU_DFS_REG UINT64_C(0x01880020)
#define A133_GPU_DFS_SIZE 4
#define A133_GPU_BVNC_REG 0x20
#define A133_GPU_FW_SIZE 118784

struct a133_gpu_softc {
#ifdef PVR_GE8300_DRM
	struct pvr_fwbridge bridge;
	struct drm_device *render_device;
	bool drm_pending;
#endif
	struct a133_gpu_diagnostics diag;
	device_t dev;
	int phandle;
	bus_space_tag_t bst;
	bus_dma_tag_t dmat;
	bus_space_handle_t bsh, dfs_bsh;
	bus_addr_t address, dfs_address;
	bus_size_t size, dfs_size;
	struct clk *core, *bus, *pll;
	struct fdtbus_reset *reset;
	struct fdtbus_regulator *supply;
	void *firmware;
	size_t firmware_size;
	struct pvr_fwmem *memory;
	struct pvr_fwboot_result boot_result;
	struct pvr_fwprobe_result queue_log, queue_cache;
	struct pvr_fwprobe_observation queue_fresh;
	struct pvr_fwheap *compute_heap;
	struct pvr_fwvm *compute_vm;
	struct pvr_fwprobe_result compute_sync, compute_kick;
	int compute_stage, compute_error, compute_attempted, compute_polls;
	int compute_sync_read, compute_sync_write, compute_sync_return;
	int compute_kick_read, compute_kick_write, compute_kick_return;
	int compute_ccb_read, compute_ccb_write, compute_ufo, compute_canary;
	int compute_faults, compute_hwr;
	struct a133_gpu_owner owner;
	kmutex_t irq_lock;
	void *irq_cookie, *irq_soft;
	unsigned irq_total, irq_window;
	int irq_fault;
	bool irq_armed;
	int queue_stage, queue_error, queue_attempted;
	int queue_irq_total, queue_irq_window, queue_irq_fault;
	int queue_log_read, queue_log_write, queue_log_return;
	int queue_cache_read, queue_cache_write, queue_cache_return;
	int queue_fw_faults, queue_hwr_flags;
	kmutex_t lock;
	struct sysctllog *sysctllog;
	uint32_t core_hz;
	uint64_t effective_hz;
	bool mapped, dfs_mapped;
};
static struct a133_gpu_softc *a133_gpu_instance;
static int a133_gpu_match(device_t, cfdata_t, void *);
static void a133_gpu_attach(device_t, device_t, void *);
static int a133_gpu_control(SYSCTLFN_ARGS);
static int a133_gpu_queue_control(SYSCTLFN_ARGS);
static int a133_gpu_compute_control(SYSCTLFN_ARGS);
static int a133_gpu_interrupt(void *);
static void a133_gpu_irq_soft(void *);
static int a133_gpu_irq_arm(void *);
static int a133_gpu_status(SYSCTLFN_ARGS);
static int a133_gpu_reserved_supported(void);
static const struct device_compatible_entry a133_gpu_compat[] = {
	{ .compat = "allwinner,sun50i-a133-powervr" },
	DEVICE_COMPAT_EOL
};
CFATTACH_DECL_NEW(sun50i_a133_gpu, sizeof(struct a133_gpu_softc),
    a133_gpu_match, a133_gpu_attach, NULL, NULL);

static void
a133_gpu_diag_reset(struct a133_gpu_softc *sc)
{
	memset(&sc->diag, 0, sizeof(sc->diag));
	sc->diag.regulator_enabled = sc->diag.microvolts = -1;
	sc->diag.ppu_enabled = sc->diag.core_hz = sc->diag.pll_hz = -1;
	sc->diag.effective_hz = sc->diag.dfs = -1;
	sc->diag.bvnc = -1;
}

/* This is only used before activation, while no hardware owner exists. */
static void
a133_gpu_preflight_release(struct a133_gpu_softc *sc)
{
	if (sc->firmware != NULL) {
		firmware_free(sc->firmware, sc->firmware_size);
		sc->firmware = NULL;
		sc->firmware_size = 0;
	}
	if (sc->dfs_mapped) {
		bus_space_unmap(sc->bst, sc->dfs_bsh, sc->dfs_size);
		sc->dfs_mapped = false;
	}
	if (sc->mapped) {
		bus_space_unmap(sc->bst, sc->bsh, sc->size);
		sc->mapped = false;
	}
	if (sc->reset != NULL) {
		fdtbus_reset_put(sc->reset);
		sc->reset = NULL;
	}
	if (sc->pll != NULL) {
		clk_put(sc->pll);
		sc->pll = NULL;
	}
	if (sc->core != NULL) {
		clk_put(sc->core);
		sc->core = NULL;
	}
	if (sc->bus != NULL) {
		clk_put(sc->bus);
		sc->bus = NULL;
	}
	if (sc->supply != NULL) {
		fdtbus_regulator_release(sc->supply);
		sc->supply = NULL;
	}
}

/* Inspect individual properties directly: no OF_nextprop enumeration. */
static bool
a133_gpu_identity_dma(int node)
{
	for (; node != -1; node = OF_parent(node)) {
		if (OF_getproplen(node, "iommus") >= 0 ||
		    OF_getproplen(node, "iommu-map") >= 0 ||
		    OF_getproplen(node, "dma-ranges") >= 0)
			return false;
	}
	return true;
}

static bool
a133_gpu_opp_table(int node)
{
	static const uint32_t expected[] = {
		504000, 950000, 472500, 950000,
		441000, 950000, 252000, 950000
	};
	uint32_t cells[__arraycount(expected)];

	if (OF_getproplen(node, "operating-points") != sizeof(cells) ||
	    OF_getprop(node, "operating-points", cells, sizeof(cells)) !=
	    sizeof(cells))
		return false;
	for (size_t i = 0; i < __arraycount(cells); i++)
		if (be32toh(cells[i]) != expected[i])
			return false;
	return true;
}

static bool
a133_gpu_irq(int node)
{
	uint32_t cells[3];

	return OF_getproplen(node, "interrupts") == sizeof(cells) &&
	    OF_getprop(node, "interrupts", cells, sizeof(cells)) == sizeof(cells) &&
	    be32toh(cells[0]) == 0 && be32toh(cells[1]) == 97 &&
	    be32toh(cells[2]) == 4;
}

static int
a133_gpu_preflight(void *cookie)
{
	struct a133_gpu_softc *sc = cookie;
	struct pvr_fwinfo info;
	firmware_handle_t fh = NULL;
	bool enabled;
	int error, close_error;
	u_int microvolts;
	off_t length;

	sc->diag.stage = A133_GPU_STAGE_BINDING;
	sc->diag.detail_error = 0;
	if (sc->dmat == NULL || OF_getproplen(sc->phandle,
	    "ember,a133-gpu-exclusive") != 0 || OF_getproplen(sc->phandle,
	    "netbsd,consumer-managed-power") != 0 ||
	    OF_getproplen(sc->phandle, "memory-region") >= 0 ||
	    !a133_gpu_identity_dma(sc->phandle) || !a133_gpu_opp_table(sc->phandle) ||
	    !a133_gpu_irq(sc->phandle) || a133_gpu_reserved_supported() != 0 ||
	    fdtbus_get_reg_byname(sc->phandle, "gpu", &sc->address, &sc->size) != 0 ||
	    fdtbus_get_reg_byname(sc->phandle, "dfs", &sc->dfs_address,
	    &sc->dfs_size) != 0 || sc->address != A133_GPU_BASE ||
	    sc->size != A133_GPU_SIZE || sc->dfs_address != A133_GPU_DFS_REG ||
	    sc->dfs_size != A133_GPU_DFS_SIZE)
		return EOPNOTSUPP;
	sc->diag.stage = A133_GPU_STAGE_ACQUIRE;
	sc->diag.detail_error = 0;
	sc->core = fdtbus_clock_get(sc->phandle, "core");
	sc->bus = fdtbus_clock_get(sc->phandle, "bus");
	sc->reset = fdtbus_reset_get(sc->phandle, "bus");
	sc->supply = fdtbus_regulator_acquire(sc->phandle, "gpu-supply");
	if (sc->core != NULL)
		sc->pll = clk_get_parent(sc->core);
	if (sc->core == NULL || sc->pll == NULL || sc->bus == NULL || sc->reset == NULL ||
	    sc->supply == NULL) {
		error = ENXIO;
		goto fail;
	}
	sc->diag.stage = A133_GPU_STAGE_PREFLIGHT_ENABLED;
	sc->diag.detail_error = 0;
	error = fdtbus_regulator_is_enabled(sc->supply, &enabled);
	sc->diag.detail_error = error;
	if (error != 0) {
		error = EBUSY;
		goto fail;
	}
	sc->diag.regulator_enabled = enabled;
	if (!enabled) {
		error = EBUSY;
		goto fail;
	}
	sc->diag.stage = A133_GPU_STAGE_PREFLIGHT_VOLTAGE;
	sc->diag.detail_error = 0;
	error = fdtbus_regulator_get_voltage(sc->supply, &microvolts);
	sc->diag.detail_error = error;
	if (error != 0) {
		error = EBUSY;
		goto fail;
	}
	sc->diag.microvolts = microvolts;
	if (microvolts != 920000 && microvolts != 940000 &&
	    microvolts != 950000) {
		error = EBUSY;
		goto fail;
	}
	sc->diag.stage = A133_GPU_STAGE_MAP_GPU;
	sc->diag.detail_error = 0;
	error = bus_space_map(sc->bst, sc->address, sc->size, 0, &sc->bsh);
	if (error != 0)
		goto fail;
	sc->mapped = true;
	sc->diag.stage = A133_GPU_STAGE_MAP_DFS;
	sc->diag.detail_error = 0;
	error = bus_space_map(sc->bst, sc->dfs_address, sc->dfs_size, 0,
	    &sc->dfs_bsh);
	if (error != 0)
		goto fail;
	sc->dfs_mapped = true;
	sc->diag.stage = A133_GPU_STAGE_FW_OPEN;
	sc->diag.detail_error = 0;
	error = firmware_open("powervr", "rogue_22.102.54.38_v1.fw", &fh);
	if (error != 0)
		goto fail;
	sc->diag.stage = A133_GPU_STAGE_FW_SIZE;
	sc->diag.detail_error = 0;
	length = firmware_get_size(fh);
	if (length != A133_GPU_FW_SIZE) {
		error = EINVAL;
		goto close;
	}
	sc->firmware_size = (size_t)length;
	sc->diag.stage = A133_GPU_STAGE_FW_ALLOC;
	sc->diag.detail_error = 0;
	sc->firmware = firmware_malloc(sc->firmware_size);
	if (sc->firmware == NULL) {
		error = ENOMEM;
		goto close;
	}
	sc->diag.stage = A133_GPU_STAGE_FW_READ;
	sc->diag.detail_error = 0;
	error = firmware_read(fh, 0, sc->firmware, sc->firmware_size);
	if (error == 0) {
		sc->diag.stage = A133_GPU_STAGE_FW_PARSE;
		sc->diag.detail_error = 0;
		error = pvr_ge8300_fwinfo_parse(sc->firmware, sc->firmware_size,
		    PVR_GE8300_BVNC, &info);
	}
close:
	close_error = firmware_close(fh);
	if (error == 0) {
		sc->diag.stage = A133_GPU_STAGE_FW_CLOSE;
		sc->diag.detail_error = 0;
		error = close_error;
	}
	if (error == 0)
		return 0;
fail:
	a133_gpu_preflight_release(sc);
	return error;
}

static int
a133_gpu_activate(void *cookie)
{
	struct a133_gpu_softc *sc = cookie;
	bool enabled, change;
	u_int uv;
	int error;

	/* A failed rail write can be uncertain; the owner already retains resources. */
	sc->diag.stage = A133_GPU_STAGE_SET_VOLTAGE;
	sc->diag.detail_error = 0;
	error = fdtbus_regulator_set_voltage(sc->supply, 950000, 950000);
	if (error != 0)
		return error;
	sc->diag.stage = A133_GPU_STAGE_VOLTAGE_READBACK;
	error = fdtbus_regulator_get_voltage(sc->supply, &uv);
	sc->diag.detail_error = error;
	if (error != 0)
		return error;
	sc->diag.microvolts = uv;
	if (uv != 950000)
		return EIO;

	/* The exclusive board owner requires an OFF transition snapshot. */
	sc->diag.stage = A133_GPU_STAGE_PPU_PREPARE;
	sc->diag.detail_error = 0;
	error = fdtbus_powerdomain_prepare_index(sc->phandle, 0, true, &change);
	if (error != 0)
		return error;
	if (!change)
		return EBUSY;

	/* All acquired resources remain retained even on an uncertain failure. */
	sc->diag.stage = A133_GPU_STAGE_PLL_ENABLE;
	sc->diag.detail_error = 0;
	error = clk_enable(sc->pll);
	if (error != 0)
		return error;
	sc->diag.pll_hz = clk_get_rate(sc->pll);
	if (sc->diag.pll_hz != 504000000) {
		sc->diag.stage = A133_GPU_STAGE_PLL_SET;
		error = clk_set_rate(sc->pll, 504000000);
		if (error != 0)
			return error;
		sc->diag.pll_hz = clk_get_rate(sc->pll);
		if (sc->diag.pll_hz != 504000000)
			return EIO;
	}
	sc->diag.stage = A133_GPU_STAGE_CORE_RATE;
	sc->diag.detail_error = 0;
	sc->core_hz = clk_get_rate(sc->core);
	sc->diag.core_hz = sc->core_hz;
	if (sc->core_hz != 504000000) {
		sc->diag.stage = A133_GPU_STAGE_CORE_SET;
		error = clk_set_rate(sc->core, 504000000);
		if (error != 0)
			return error;
		sc->core_hz = clk_get_rate(sc->core);
		sc->diag.core_hz = sc->core_hz;
		if (sc->core_hz != 504000000)
			return EIO;
	}
	sc->diag.stage = A133_GPU_STAGE_RESET;
	sc->diag.detail_error = 0;
	error = fdtbus_reset_deassert(sc->reset);
	if (error != 0)
		return error;
	sc->diag.stage = A133_GPU_STAGE_BUS_ENABLE;
	sc->diag.detail_error = 0;
	error = clk_enable(sc->bus);
	if (error != 0)
		return error;
	sc->diag.stage = A133_GPU_STAGE_CORE_ENABLE;
	sc->diag.detail_error = 0;
	error = clk_enable(sc->core);
	if (error != 0)
		return error;
	sc->diag.stage = A133_GPU_STAGE_PPU_ENABLE;
	sc->diag.detail_error = 0;
	error = fdtbus_powerdomain_enable(sc->phandle);
	if (error != 0)
		return error;
	sc->diag.stage = A133_GPU_STAGE_PPU_STATE;
	sc->diag.detail_error = 0;
	error = fdtbus_powerdomain_is_enabled_index(sc->phandle, 0, &enabled);
	sc->diag.detail_error = error;
	if (error != 0)
		return EBUSY;
	sc->diag.ppu_enabled = enabled;
	if (!enabled)
		return EBUSY;
	sc->diag.stage = A133_GPU_STAGE_POWER_ENABLED;
	sc->diag.detail_error = 0;
	error = fdtbus_regulator_is_enabled(sc->supply, &enabled);
	sc->diag.detail_error = error;
	if (error != 0)
		return EBUSY;
	sc->diag.regulator_enabled = enabled;
	if (!enabled)
		return EBUSY;
	sc->diag.stage = A133_GPU_STAGE_POWER_VOLTAGE;
	sc->diag.detail_error = 0;
	error = fdtbus_regulator_get_voltage(sc->supply, &uv);
	sc->diag.detail_error = error;
	if (error != 0)
		return EBUSY;
	sc->diag.microvolts = uv;
	if (uv != 950000)
		return EOPNOTSUPP;
	sc->diag.stage = A133_GPU_STAGE_POWER_CLOCK;
	sc->diag.detail_error = 0;
	sc->diag.core_hz = clk_get_rate(sc->core);
	if ((uint32_t)sc->diag.core_hz != sc->core_hz)
		return EOPNOTSUPP;
	return 0;
}

/* Revalidate power, clock, identity and DFS immediately before each use. */
static int
a133_gpu_live(struct a133_gpu_softc *sc, uint64_t *effective)
{
	uint64_t bvnc, rate;
	uint32_t dfs;
	u_int uv;
	bool enabled;
	int error;

	sc->diag.stage = A133_GPU_STAGE_LIVE_PPU;
	sc->diag.detail_error = 0;
	error = fdtbus_powerdomain_is_enabled_index(sc->phandle, 0, &enabled);
	sc->diag.detail_error = error;
	if (error != 0)
		return EBUSY;
	sc->diag.ppu_enabled = enabled;
	if (!enabled)
		return EBUSY;
	sc->diag.stage = A133_GPU_STAGE_LIVE_ENABLED;
	sc->diag.detail_error = 0;
	error = fdtbus_regulator_is_enabled(sc->supply, &enabled);
	sc->diag.detail_error = error;
	if (error != 0)
		return EBUSY;
	sc->diag.regulator_enabled = enabled;
	if (!enabled)
		return EBUSY;
	sc->diag.stage = A133_GPU_STAGE_LIVE_VOLTAGE;
	sc->diag.detail_error = 0;
	error = fdtbus_regulator_get_voltage(sc->supply, &uv);
	sc->diag.detail_error = error;
	if (error != 0)
		return EBUSY;
	sc->diag.microvolts = uv;
	if (uv != 950000)
		return EBUSY;
	sc->diag.stage = A133_GPU_STAGE_LIVE_CLOCK;
	sc->diag.detail_error = 0;
	sc->diag.core_hz = clk_get_rate(sc->core);
	if ((uint32_t)sc->diag.core_hz != sc->core_hz)
		return EBUSY;
	sc->diag.stage = A133_GPU_STAGE_BVNC;
	sc->diag.detail_error = 0;
	if (bus_space_peek_8(sc->bst, sc->bsh, A133_GPU_BVNC_REG, &bvnc) != 0)
		return EIO;
	sc->diag.bvnc = bvnc;
	if (bvnc != PVR_GE8300_BVNC)
		return ENODEV;
	/* The separate DFS word is read only after the PPU reports 3D. */
	sc->diag.stage = A133_GPU_STAGE_DFS;
	sc->diag.detail_error = 0;
	if (bus_space_peek_4(sc->bst, sc->dfs_bsh, 0, &dfs) != 0)
		return EIO;
	sc->diag.dfs = dfs;
	sc->diag.stage = A133_GPU_STAGE_OPP;
	sc->diag.detail_error = 0;
	error = a133_gpu_effective_hz(sc->core_hz, dfs, &rate);
	if (error != 0 || !a133_gpu_opp_valid(rate, uv))
		return EOPNOTSUPP;
	sc->diag.effective_hz = rate;
	*effective = rate;
	return 0;
}

static int
a133_gpu_prepare(void *cookie)
{
	struct a133_gpu_softc *sc = cookie;
	uint64_t clusters, slc;
	int error;

	error = a133_gpu_live(sc, &sc->effective_hz);
	if (error != 0)
		return error;
	sc->diag.stage = A133_GPU_STAGE_DMA;
	sc->diag.detail_error = 0;
	error = pvr_fwmem_prepare(sc->dmat, sc->firmware, sc->firmware_size,
	    PVR_GE8300_BVNC, PVR_FW_PROFILE_GE8300, sc->address, sc->size,
	    sc->dfs_address, sc->dfs_size, &sc->memory);
	/* The source file buffer is CPU-only; prepared DMA owns its own copy. */
	firmware_free(sc->firmware, sc->firmware_size);
	sc->firmware = NULL;
	sc->firmware_size = 0;
	if (error != 0)
		return error;
	sc->diag.stage = A133_GPU_STAGE_FEATURES;
	sc->diag.detail_error = 0;
	error = pvr_fwinfo_feature(&sc->memory->image.info,
	    PVR_FW_FEATURE_NUM_CLUSTERS, &clusters);
	if (error == 0)
		error = pvr_fwinfo_feature(&sc->memory->image.info,
		    PVR_FW_FEATURE_SLC_SIZE_IN_KILOBYTES, &slc);
	if (error != 0 || clusters != 1 || slc != 64)
		return EOPNOTSUPP;
	sc->diag.stage = A133_GPU_STAGE_INTERFACE;
	sc->diag.detail_error = 0;
	return pvr_fwmem_interface(sc->memory, sc->effective_hz, clusters, slc);
}

/* Admit only reservation forms the current FDT/UVM owner excludes. */
static int
a133_gpu_reserved_supported(void)
{
	const void *fdt = fdtbus_get_data();
	uint64_t base, size;
	uint32_t ac, cells;
	int count, parent, node, len;

	if (fdt == NULL || fdt_check_header(fdt) != 0 ||
	    (count = fdt_num_mem_rsv(fdt)) < 0)
		return EINVAL;
	for (int i = 0; i < count; i++) {
		if (fdt_get_mem_rsv(fdt, i, &base, &size) != 0 || size == 0 ||
		    base > UINT64_MAX - size)
			return EOPNOTSUPP;
	}
	parent = OF_finddevice("/reserved-memory");
	if (parent == -1)
		return 0;
	if (of_getprop_uint32(parent, "#address-cells", &ac) != 0 ||
	    of_getprop_uint32(parent, "#size-cells", &cells) != 0 ||
	    ac == 0 || ac > 2 || cells == 0 || cells > 2)
		return EOPNOTSUPP;
	for (node = OF_child(parent); node != 0; node = OF_peer(node)) {
		if (!fdtbus_status_okay(node))
			continue;
		len = OF_getproplen(node, "reg");
		if (len != (int)((ac + cells) * 4) ||
		    fdtbus_get_reg64(node, 0, &base, &size) != 0 || size == 0 ||
		    base > UINT64_MAX - size)
			return EOPNOTSUPP;
	}
	return 0;
}

/* The hard interrupt handles only the native MIPS event and metadata. */
static int
a133_gpu_interrupt(void *cookie)
{
	struct a133_gpu_softc *sc = cookie;
	bool pending = false;
	int error;

	mutex_enter(&sc->irq_lock);
	error = pvr_mips_irq_pending(sc->bst, sc->bsh, &pending);
	if (error == 0 && pending)
		error = pvr_mips_irq_clear(sc->bst, sc->bsh);
	if (error != 0) {
		sc->irq_fault = error;
	} else if (pending) {
		if (sc->irq_total != INT_MAX)
			sc->irq_total++;
		if (sc->irq_armed && sc->irq_window != INT_MAX)
			sc->irq_window++;
	}
	mutex_exit(&sc->irq_lock);
	if (pending || error != 0)
		softint_schedule(sc->irq_soft);
	return pending || error != 0;
}

static void
a133_gpu_irq_soft(void *cookie)
{
	struct a133_gpu_softc *sc = cookie;

	wakeup(&sc->irq_window);
}

/* Serialize a stale-event drain against any currently executing handler. */
static int
a133_gpu_irq_arm(void *cookie)
{
	struct a133_gpu_softc *sc = cookie;
	bool pending;
	int error;

	mutex_enter(&sc->irq_lock);
	error = sc->irq_fault;
	if (error == 0)
		error = pvr_mips_irq_pending(sc->bst, sc->bsh, &pending);
	if (error == 0 && pending)
		error = pvr_mips_irq_clear(sc->bst, sc->bsh);
	if (error == 0) {
		sc->irq_window = 0;
		sc->irq_armed = true;
	} else {
		sc->irq_fault = error;
	}
	mutex_exit(&sc->irq_lock);
	return error;
}

static int
a133_gpu_start(void *cookie)
{
	struct a133_gpu_softc *sc = cookie;
	uint64_t effective;
	void *irq_cookie;
	int error;

	error = a133_gpu_live(sc, &effective);
	if (error != 0)
		return error;
	sc->diag.stage = A133_GPU_STAGE_EFFECTIVE_RECHECK;
	sc->diag.detail_error = 0;
	if (effective != sc->effective_hz)
		return EBUSY;
	/* PPU is already 3D; never inspect GPU IRQ registers at ordinary attach. */
	sc->diag.stage = A133_GPU_STAGE_IRQ_SETUP;
	sc->diag.detail_error = 0;
	sc->irq_soft = softint_establish(SOFTINT_CLOCK | SOFTINT_MPSAFE,
	    a133_gpu_irq_soft, sc);
	if (sc->irq_soft == NULL)
		return ENOMEM;
	irq_cookie = fdtbus_intr_establish_xname(sc->phandle, 0, IPL_VM,
	    FDT_INTR_MPSAFE, a133_gpu_interrupt, sc, device_xname(sc->dev));
	if (irq_cookie == NULL) {
		softint_disestablish(sc->irq_soft);
		sc->irq_soft = NULL;
		return ENXIO;
	}
	mutex_enter(&sc->irq_lock);
	sc->irq_cookie = irq_cookie;
	if (sc->irq_fault != 0)
		error = sc->irq_fault;
	mutex_exit(&sc->irq_lock);
	if (error != 0)
		return error;
	sc->diag.stage = A133_GPU_STAGE_FW_BOOT;
	sc->diag.detail_error = 0;
	return pvr_fwboot(PVR_FW_PROFILE_GE8300, sc->bst, sc->bsh,
	    sc->memory, &sc->boot_result);
}

static int
a133_gpu_control(SYSCTLFN_ARGS)
{
	static const struct a133_gpu_owner_ops ops = {
		a133_gpu_preflight, a133_gpu_activate, a133_gpu_prepare, a133_gpu_start
	};
	struct sysctlnode node = *rnode;
	struct a133_gpu_softc *sc = a133_gpu_instance;
	int value = 0, error;

	if (sc == NULL)
		return ENXIO;
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	if (value != 1)
		return EINVAL;
	sysctl_unlock();
	mutex_enter(&sc->lock);
	if (!sc->owner.attempted && !sc->owner.retain_resources)
		a133_gpu_diag_reset(sc);
	error = a133_gpu_owner_boot(&sc->owner, &ops, sc);
	sc->diag.last_error = error;
	sc->diag.attempted = sc->owner.attempted;
	sc->diag.retained = sc->owner.retain_resources;
	if (error == 0)
		sc->diag.stage = A133_GPU_STAGE_COMPLETE;
	else
		aprint_error_dev(sc->dev, "firmware stage %d failed: %d (detail %d)\n",
		    sc->diag.stage, error, sc->diag.detail_error);
	mutex_exit(&sc->lock);
	sysctl_relock();
	return error;
}

/* Caller holds the sleepable owner lock; hard IRQ never touches the queue. */
static int
a133_gpu_queue_run(struct a133_gpu_softc *sc)
{
	struct timespec start, now, elapsed;
	unsigned window = 0;
	int error;

	if (sc->queue_attempted) {
		error = EBUSY;
		goto out;
	}
	if (sc->memory == NULL || sc->irq_cookie == NULL ||
	    !sc->owner.retain_resources || sc->boot_result.error != 0 ||
	    sc->diag.stage != A133_GPU_STAGE_COMPLETE) {
		error = ENXIO;
		goto out;
	}
	sc->queue_attempted = 1;
	sc->queue_stage = 1;
	mutex_enter(&sc->irq_lock);
	error = sc->irq_fault;
	mutex_exit(&sc->irq_lock);
	if (error != 0)
		goto done;
	error = pvr_fwprobe(sc->bst, sc->bsh, sc->memory, &sc->boot_result,
	    &sc->queue_log);
	if (sc->queue_log.polls != 0) {
		sc->queue_log_read = sc->queue_log.after.read_offset;
		sc->queue_log_write = sc->queue_log.after.write_offset;
		sc->queue_log_return = sc->queue_log.after.rtn;
		sc->queue_fw_faults = sc->queue_log.after.fw_faults;
		sc->queue_hwr_flags = sc->queue_log.after.hwr_state;
	}
	if (error != 0)
		goto done;
	sc->queue_stage = 2;
	error = pvr_fwprobe_mmucache_irq(sc->bst, sc->bsh, sc->memory,
	    &sc->boot_result, &sc->queue_log, a133_gpu_irq_arm, sc,
	    &sc->queue_cache);
	if (sc->queue_cache.polls != 0) {
		sc->queue_cache_read = sc->queue_cache.after.read_offset;
		sc->queue_cache_write = sc->queue_cache.after.write_offset;
		sc->queue_cache_return = sc->queue_cache.after.rtn;
		sc->queue_fw_faults = sc->queue_cache.after.fw_faults;
		sc->queue_hwr_flags = sc->queue_cache.after.hwr_state;
	}
	if (error != 0)
		goto done;
	sc->queue_stage = 3;
	getnanouptime(&start);
	for (unsigned i = 0; i < 1001; i++) {
		mutex_enter(&sc->irq_lock);
		window = sc->irq_window;
		error = sc->irq_fault;
		mutex_exit(&sc->irq_lock);
		if (error != 0 || window != 0)
			break;
		getnanouptime(&now);
		timespecsub(&now, &start, &elapsed);
		if (elapsed.tv_sec >= 1 || i == 1000) {
			error = ETIMEDOUT;
			break;
		}
		(void)tsleep(&sc->irq_window, PWAIT, "pvrirq", 1);
	}
	if (error != 0)
		goto done;
	sc->queue_stage = 4;
	error = pvr_fwprobe_observe(sc->memory, &sc->boot_result, 1,
	    PVR_FWPROBE_MMUCACHE, 0, &sc->queue_fresh);
	if (sc->queue_fresh.valid) {
		sc->queue_fw_faults = sc->queue_fresh.state.fw_faults;
		sc->queue_hwr_flags = sc->queue_fresh.state.hwr_state;
	}
done:
	mutex_enter(&sc->irq_lock);
	sc->irq_armed = false;
	sc->queue_irq_total = sc->irq_total;
	sc->queue_irq_window = sc->irq_window;
	sc->queue_irq_fault = sc->irq_fault;
	if (error == 0 && sc->irq_fault != 0)
		error = sc->irq_fault;
	mutex_exit(&sc->irq_lock);
	sc->queue_error = error;
	if (error == 0)
		sc->queue_stage = 5;
	if (error != 0)
		aprint_error_dev(sc->dev, "firmware queue stage %d failed: %d\n",
		    sc->queue_stage, error);
out:
	return error;
}

/* Explicit, one-shot firmware queue check, always in thread context. */
static int
a133_gpu_queue_control(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct a133_gpu_softc *sc = a133_gpu_instance;
	int value = 0, error;

	if (sc == NULL)
		return ENXIO;
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	if (value != 1)
		return EINVAL;
	sysctl_unlock();
	mutex_enter(&sc->lock);
	error = a133_gpu_queue_run(sc);
	mutex_exit(&sc->lock);
	sysctl_relock();
	return error;
}

/* One fixed, CPU-produced canary image; no arbitrary shader interface. */
static int
a133_gpu_compute_run(struct a133_gpu_softc *sc)
{
	static const uint8_t pinned[SHA256_DIGEST_LENGTH] = {
		0xb7, 0x02, 0x6d, 0xf9, 0x48, 0x75, 0x09, 0x73,
		0x38, 0x24, 0x3e, 0x6b, 0xc1, 0x53, 0x2b, 0xe7,
		0x0c, 0x46, 0x1f, 0x73, 0x99, 0x5f, 0x78, 0xc2,
		0x9c, 0xb1, 0x72, 0xb0, 0x4b, 0x98, 0x9d, 0xc6
	};
	static const uint16_t roles[PVR_FWCOMPUTE_PROGRAMS] = {
		PVR_FWCOMPUTE_USC_PROGRAMS, PVR_FWCOMPUTE_PDS_PROGRAMS,
		PVR_FWCOMPUTE_CDM_STREAM
	};
	struct pvr_fwcompute_file file;
	struct pvr_fwcompute_inputs inputs;
	struct pvr_fwcompute_image *image = NULL;
	struct pvr_fwprobe_observation fresh = { 0 };
	struct a133_gpu_diagnostics saved_diag;
	struct pvr_dma *scratch = NULL;
	firmware_handle_t fh = NULL;
	SHA256_CTX hash;
	uint8_t digest[SHA256_DIGEST_LENGTH], *bytes = NULL;
	uint64_t rate;
	uint32_t read_offset = 0, write_offset = 0, ufo = 0, canary;
	struct timespec start, now, elapsed;
	off_t length;
	size_t size = 0, i, j;
	int error, irq_error;

#ifdef PVR_GE8300_DRM
	if (sc->drm_pending || sc->bridge.attempted)
		return EBUSY;
#endif
	if (sc->compute_attempted)
		return EBUSY;
	if (sc->queue_stage != 5 || sc->queue_error != 0 ||
	    sc->memory == NULL || sc->compute_heap != NULL ||
	    sc->compute_vm != NULL)
		return ENXIO;
	sc->compute_attempted = 1;
	sc->compute_stage = 1;
	mutex_enter(&sc->irq_lock);
	irq_error = sc->irq_fault;
	mutex_exit(&sc->irq_lock);
	if (irq_error != 0) {
		error = irq_error;
		goto out;
	}
	/* Keep the boot diagnostics unchanged when rechecking the live OPP. */
	saved_diag = sc->diag;
	error = a133_gpu_live(sc, &rate);
	sc->diag = saved_diag;
	if (error != 0 || rate != sc->effective_hz) {
		if (error == 0)
			error = EBUSY;
		goto out;
	}
	error = pvr_fwprobe_observe(sc->memory, &sc->boot_result, 1,
	    PVR_FWPROBE_MMUCACHE, 0, &fresh);
	if (error != 0)
		goto out;
	sc->compute_stage = 2;
	error = firmware_open("powervr", "ge8300-compute.bin", &fh);
	if (error != 0)
		goto out;
	length = firmware_get_size(fh);
	if (length != 322452) {
		error = EINVAL;
		goto out;
	}
	size = (size_t)length;
	bytes = kmem_alloc(size, KM_SLEEP);
	error = firmware_read(fh, 0, bytes, size);
	if (error != 0)
		goto out;
	error = firmware_close(fh);
	fh = NULL;
	if (error != 0)
		goto out;
	SHA256_Init(&hash);
	SHA256_Update(&hash, bytes, size);
	SHA256_Final(digest, &hash);
	if (memcmp(digest, pinned, sizeof(pinned)) != 0) {
		error = EAUTH;
		goto out;
	}
	error = pvr_fwcompute_file_parse(bytes, size, PVR_FWCOMPUTE_GE8300_BVNC,
	    &file);
	if (error != 0)
		goto out;
	SHA256_Init(&hash);
	SHA256_Update(&hash, bytes + file.header_bytes, file.payload_bytes);
	SHA256_Final(digest, &hash);
	if (memcmp(digest, file.payload_digest, sizeof(digest)) != 0) {
		error = EAUTH;
		goto out;
	}
	memset(&inputs, 0, sizeof(inputs));
	pvr_fwcompute_file_fwspans(inputs.fw);
	for (i = 0; i < PVR_FWCOMPUTE_PROGRAMS; i++) {
		for (j = 0; j < PVR_FWCOMPUTE_RESOURCES; j++)
			if (file.resource[j].role == roles[i])
				break;
		if (j == PVR_FWCOMPUTE_RESOURCES) {
			error = EINVAL;
			goto out;
		}
		inputs.program[i].bytes = bytes + file.header_bytes +
		    file.resource[j].offset;
		inputs.program[i].size = file.resource[j].bytes;
		inputs.program[i].gpu_va = file.resource[j].va;
	}
	memcpy(&inputs.static_state, bytes + file.header_bytes +
	    file.static_offset, PVR_FWCOMPUTE_STATE_BYTES);
	inputs.command = file.command;
	inputs.priority = file.priority;
	inputs.pid = file.pid;
	inputs.context_id = file.context_id;
	inputs.job_id = file.job_id;
	image = kmem_alloc(sizeof(*image), KM_SLEEP);
	error = pvr_fwcompute_prepare(PVR_FW_PROFILE_GE8300, &inputs,
	    image, sizeof(*image));
	if (error != 0)
		goto out;
	sc->compute_stage = 3;
	error = pvr_fwheap_prepare(sc->dmat, sc->memory, image,
	    &sc->compute_heap);
	if (error != 0)
		goto out;
	error = pvr_fwheap_map(sc->compute_heap, sc->memory);
	if (error != 0)
		goto out;
	sc->compute_stage = 4;
	error = pvr_fwprobe_ge_heap_sync(sc->bst, sc->bsh, sc->memory,
	    &sc->boot_result, &sc->queue_cache, sc->compute_heap,
	    &sc->compute_sync);
	if (sc->compute_sync.polls != 0) {
		sc->compute_sync_read = sc->compute_sync.after.read_offset;
		sc->compute_sync_write = sc->compute_sync.after.write_offset;
		sc->compute_sync_return = sc->compute_sync.after.rtn;
		sc->compute_faults = sc->compute_sync.after.fw_faults;
		sc->compute_hwr = sc->compute_sync.after.hwr_state;
	}
	if (error != 0)
		goto out;
	mutex_enter(&sc->irq_lock);
	irq_error = sc->irq_fault;
	mutex_exit(&sc->irq_lock);
	if (irq_error != 0) {
		error = irq_error;
		goto out;
	}
	sc->compute_stage = 5;
	error = pvr_fwvm_prepare(sc->dmat, &file, bytes, sc->compute_heap,
	    &sc->compute_vm);
	if (error != 0)
		goto out;
	for (i = 0; i < PVR_FWCOMPUTE_RESOURCES; i++) {
		if (file.resource[i].role == PVR_FWCOMPUTE_SCRATCH) {
			if (file.resource[i].va != UINT64_C(0x8000000000) ||
			    file.resource[i].bytes != sizeof(canary)) {
				error = EINVAL;
				goto out;
			}
			scratch = &sc->compute_vm->resource[i];
			break;
		}
	}
	if (scratch == NULL || scratch->cpu == NULL || scratch->map == NULL) {
		error = EINVAL;
		goto out;
	}
	mutex_enter(&sc->irq_lock);
	irq_error = sc->irq_fault;
	mutex_exit(&sc->irq_lock);
	if (irq_error != 0) {
		error = irq_error;
		goto out;
	}
	sc->compute_stage = 6;
	error = pvr_fwprobe_ge_kick(sc->bst, sc->bsh, sc->memory,
	    &sc->boot_result, &sc->compute_sync, sc->compute_heap,
	    sc->compute_vm, &sc->compute_kick);
	if (sc->compute_kick.polls != 0) {
		sc->compute_kick_read = sc->compute_kick.after.read_offset;
		sc->compute_kick_write = sc->compute_kick.after.write_offset;
		sc->compute_kick_return = sc->compute_kick.after.rtn;
		sc->compute_faults = sc->compute_kick.after.fw_faults;
		sc->compute_hwr = sc->compute_kick.after.hwr_state;
	}
	if (error != 0)
		goto out;
	sc->compute_stage = 7;
	getnanouptime(&start);
	for (i = 0; i < 40001; i++) {
		pvr_fwheap_observe(sc->compute_heap, &read_offset, &write_offset, &ufo);
		sc->compute_polls++;
		memset(&fresh, 0, sizeof(fresh));
		error = pvr_fwprobe_observe(sc->memory, &sc->boot_result, 3,
		    ROGUE_FWIF_KCCB_CMD_KICK, 0, &fresh);
		if (fresh.valid) {
			sc->compute_faults = fresh.state.fw_faults;
			sc->compute_hwr = fresh.state.hwr_state;
		}
		if (error != 0)
			break;
		mutex_enter(&sc->irq_lock);
		irq_error = sc->irq_fault;
		mutex_exit(&sc->irq_lock);
		if (irq_error != 0) {
			error = irq_error;
			break;
		}
		if (read_offset == PVR_FWCOMPUTE_USED_BYTES &&
		    write_offset == PVR_FWCOMPUTE_USED_BYTES && ufo == 1)
			break;
		getnanouptime(&now);
		timespecsub(&now, &start, &elapsed);
		if (elapsed.tv_sec >= 4 || i == 40000) {
			error = ETIMEDOUT;
			break;
		}
		(void)tsleep(&sc->compute_polls, PWAIT, "pvrcomp", 1);
	}
	sc->compute_ccb_read = (int)read_offset;
	sc->compute_ccb_write = (int)write_offset;
	sc->compute_ufo = (int)ufo;
	if (error != 0)
		goto out;
	sc->compute_stage = 8;
	bus_dmamap_sync(sc->dmat, scratch->map, 0, sizeof(canary),
	    BUS_DMASYNC_POSTREAD);
	memcpy(&canary, scratch->cpu, sizeof(canary));
	sc->compute_canary = (int)canary;
	memset(&fresh, 0, sizeof(fresh));
	error = pvr_fwprobe_observe(sc->memory, &sc->boot_result, 3,
	    ROGUE_FWIF_KCCB_CMD_KICK, 0, &fresh);
	if (fresh.valid) {
		sc->compute_faults = fresh.state.fw_faults;
		sc->compute_hwr = fresh.state.hwr_state;
	}
	if (error == 0 && canary != UINT32_C(0x13579bdf))
		error = EIO;
	mutex_enter(&sc->irq_lock);
	irq_error = sc->irq_fault;
	mutex_exit(&sc->irq_lock);
	if (error == 0 && irq_error != 0)
		error = irq_error;
	if (error == 0)
		sc->compute_stage = 9;
out:
	if (fh != NULL)
		(void)firmware_close(fh);
	if (image != NULL)
		kmem_free(image, sizeof(*image));
	if (bytes != NULL)
		kmem_free(bytes, size);
	if (sc->compute_vm != NULL && !sc->compute_vm->device_exposed)
		(void)pvr_fwvm_release(&sc->compute_vm);
	if (sc->compute_heap != NULL && !sc->compute_heap->heap.device_exposed)
		(void)pvr_fwheap_release(&sc->compute_heap);
	sc->compute_error = error;
	if (error != 0)
		aprint_error_dev(sc->dev, "compute stage %d failed: %d\n",
		    sc->compute_stage, error);
	return error;
}

static int
a133_gpu_compute_control(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct a133_gpu_softc *sc = a133_gpu_instance;
	int value = 0, error;

	if (sc == NULL)
		return ENXIO;
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	if (value != 1)
		return EINVAL;
	sysctl_unlock();
	mutex_enter(&sc->lock);
	error = a133_gpu_compute_run(sc);
	mutex_exit(&sc->lock);
	sysctl_relock();
	return error;
}

/* Copy the cached observation under the owner lock; never access hardware. */
static int
a133_gpu_status(SYSCTLFN_ARGS)
{
	struct a133_gpu_softc *sc = a133_gpu_instance;
	struct sysctlnode node = *rnode;
	union { int i; int64_t q; } value;
	int error;

	if (sc == NULL)
		return ENXIO;
	if (newp != NULL)
		return EPERM;
	if (node.sysctl_size != sizeof(value.i) && node.sysctl_size != sizeof(value.q))
		return EINVAL;
	sysctl_unlock();
	mutex_enter(&sc->lock);
	memcpy(&value, node.sysctl_data, node.sysctl_size);
	mutex_exit(&sc->lock);
	sysctl_relock();
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	return error;
}

#ifdef PVR_GE8300_DRM
/* Cached IRQ failure only; hard IRQ never enters the firmware service. */
static int
a133_gpu_drm_health(void *cookie)
{
	struct a133_gpu_softc *sc = cookie;
	int error;

	mutex_enter(&sc->irq_lock);
	error = sc->irq_fault;
	mutex_exit(&sc->irq_lock);
	return error;
}

/* The caller does not hold either the sysctl tree or the platform lock. */
static int
a133_gpu_drm_attach(struct a133_gpu_softc *sc)
{
	struct pvr_query_input input;
	struct drm_device *dev = NULL;
	int error;

	mutex_enter(&sc->lock);
	if (sc->drm_pending || sc->bridge.attempted ||
	    sc->compute_attempted || sc->compute_heap != NULL ||
	    sc->compute_vm != NULL || sc->render_device != NULL) {
		error = EBUSY;
		goto out;
	}
	if (sc->memory == NULL || !sc->owner.retain_resources ||
	    sc->boot_result.error != 0 ||
	    sc->diag.stage != A133_GPU_STAGE_COMPLETE ||
	    sc->memory->image.info.bvnc != PVR_GE8300_BVNC) {
		error = ENXIO;
		goto out;
	}
	error = a133_gpu_drm_health(sc);
	if (error != 0)
		goto out;
	/* Adopt a successful queue or perform its bounded first check here. */
	if (sc->queue_stage != 5 || sc->queue_error != 0) {
		error = a133_gpu_queue_run(sc);
		if (error != 0)
			goto out;
	}
	error = pvr_query_input_from_fwinfo(&sc->memory->image.info, &input);
	if (error != 0)
		goto out;
	sc->drm_pending = true;
	mutex_exit(&sc->lock);

	error = pvr_render_attach(sc->dev, sc->dmat, PVR_GE8300_BVNC,
	    &input, &dev);
	mutex_enter(&sc->lock);
	if (error == 0) {
		sc->render_device = dev;
		error = pvr_fwbridge_prepare(&sc->bridge, &sc->lock, sc->bst,
		    sc->bsh, sc->memory, &sc->boot_result, &sc->queue_cache,
		    PVR_FW_PROFILE_GE8300, a133_gpu_drm_health, sc);
	}
	mutex_exit(&sc->lock);
	if (error == 0)
		error = pvr_fwbridge_publish(&sc->bridge, dev);
	mutex_enter(&sc->lock);
	sc->drm_pending = false;
out:
	mutex_exit(&sc->lock);
	return error;
}

static int
a133_gpu_drm_failed_status(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct a133_gpu_softc *sc = a133_gpu_instance;
	int value;

	if (sc == NULL)
		return ENXIO;
	sysctl_unlock();
	mutex_enter(&sc->lock);
	value = sc->bridge.service.queue.failed;
	mutex_exit(&sc->lock);
	sysctl_relock();
	node.sysctl_data = &value;
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
a133_gpu_drm_control(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct a133_gpu_softc *sc = a133_gpu_instance;
	int value = 0, error;

	if (sc == NULL)
		return ENXIO;
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	if (value != 1)
		return EINVAL;
	sysctl_unlock();
	error = a133_gpu_drm_attach(sc);
	sysctl_relock();
	return error;
}
#endif

static int
a133_gpu_match(device_t parent __unused, cfdata_t cf __unused, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return a133_gpu_instance == NULL &&
	    of_compatible_match(faa->faa_phandle, a133_gpu_compat);
}

static void
a133_gpu_attach(device_t parent __unused, device_t self, void *aux)
{
	struct a133_gpu_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	const struct sysctlnode *node;

	sc->dev = self;
	sc->phandle = faa->faa_phandle;
	sc->bst = faa->faa_bst;
	sc->dmat = faa->faa_dmat;
	if (sc->bst == NULL) {
		aprint_error_dev(self, "missing bus-space provider\n");
		return;
	}
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->irq_lock, MUTEX_DEFAULT, IPL_VM);
	a133_gpu_diag_reset(sc);
	sc->queue_log_read = sc->queue_log_write = sc->queue_log_return = -1;
	sc->queue_cache_read = sc->queue_cache_write = sc->queue_cache_return = -1;
	sc->queue_fw_faults = sc->queue_hwr_flags = -1;
	sc->compute_sync_read = sc->compute_sync_write = sc->compute_sync_return = -1;
	sc->compute_kick_read = sc->compute_kick_write = sc->compute_kick_return = -1;
	sc->compute_ccb_read = sc->compute_ccb_write = sc->compute_ufo = -1;
	sc->compute_canary = sc->compute_faults = sc->compute_hwr = -1;
	if (sysctl_createv(&sc->sysctllog, 0, NULL, &node, 0,
	    CTLTYPE_NODE, device_xname(self), NULL, NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL) != 0 ||
	    sysctl_createv(NULL, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "firmware_boot", SYSCTL_DESCR("explicit GE8300 firmware boot"),
	    a133_gpu_control, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0 ||
	    sysctl_createv(NULL, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "queue_probe", SYSCTL_DESCR("explicit firmware queue check"),
	    a133_gpu_queue_control, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0 ||
	    sysctl_createv(NULL, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "compute_probe", SYSCTL_DESCR("explicit GE8300 canary compute"),
	    a133_gpu_compute_control, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0) {
		aprint_error_dev(self, "cannot create firmware control\n");
		sysctl_teardown(&sc->sysctllog);
		mutex_destroy(&sc->irq_lock);
		mutex_destroy(&sc->lock);
		return;
	}
#ifdef PVR_GE8300_DRM
	if (sysctl_createv(NULL, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "drm_attach", SYSCTL_DESCR("explicit GE8300 DRM compute"),
	    a133_gpu_drm_control, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0 ||
	    sysctl_createv(NULL, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "drm_failed", SYSCTL_DESCR("cached DRM backend failure"),
	    a133_gpu_drm_failed_status, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0) {
		aprint_error_dev(self, "cannot create DRM control\n");
		sysctl_teardown(&sc->sysctllog);
		mutex_destroy(&sc->irq_lock);
		mutex_destroy(&sc->lock);
		return;
	}
#endif
	{
		const struct { const char *name; int type; void *data; } status[] = {
#ifdef PVR_GE8300_DRM
			{ "drm_queue_read", CTLTYPE_INT,
			    &sc->bridge.service.queue.last.after.read_offset },
			{ "drm_queue_write", CTLTYPE_INT,
			    &sc->bridge.service.queue.last.after.write_offset },
			{ "drm_queue_error", CTLTYPE_INT,
			    &sc->bridge.service.queue.last.error },
			{ "drm_queue_command", CTLTYPE_INT,
			    &sc->bridge.service.queue.last.command },
#endif
			{ "stage", CTLTYPE_INT, &sc->diag.stage },
			{ "last_error", CTLTYPE_INT, &sc->diag.last_error },
			{ "detail_error", CTLTYPE_INT, &sc->diag.detail_error },
			{ "attempted", CTLTYPE_INT, &sc->diag.attempted },
			{ "retained", CTLTYPE_INT, &sc->diag.retained },
			{ "regulator_enabled", CTLTYPE_INT, &sc->diag.regulator_enabled },
			{ "microvolts", CTLTYPE_INT, &sc->diag.microvolts },
			{ "ppu_enabled", CTLTYPE_INT, &sc->diag.ppu_enabled },
			{ "core_hz", CTLTYPE_INT, &sc->diag.core_hz },
			{ "pll_hz", CTLTYPE_INT, &sc->diag.pll_hz },
			{ "effective_hz", CTLTYPE_INT, &sc->diag.effective_hz },
			{ "dfs", CTLTYPE_INT, &sc->diag.dfs },
			{ "bvnc", CTLTYPE_QUAD, &sc->diag.bvnc },
			{ "queue_stage", CTLTYPE_INT, &sc->queue_stage },
			{ "queue_error", CTLTYPE_INT, &sc->queue_error },
			{ "queue_attempted", CTLTYPE_INT, &sc->queue_attempted },
			{ "queue_irq_total", CTLTYPE_INT, &sc->queue_irq_total },
			{ "queue_irq_window", CTLTYPE_INT, &sc->queue_irq_window },
			{ "queue_irq_fault", CTLTYPE_INT, &sc->queue_irq_fault },
			{ "queue_log_read", CTLTYPE_INT, &sc->queue_log_read },
			{ "queue_log_write", CTLTYPE_INT, &sc->queue_log_write },
			{ "queue_log_return", CTLTYPE_INT, &sc->queue_log_return },
			{ "queue_cache_read", CTLTYPE_INT, &sc->queue_cache_read },
			{ "queue_cache_write", CTLTYPE_INT, &sc->queue_cache_write },
			{ "queue_cache_return", CTLTYPE_INT, &sc->queue_cache_return },
			{ "queue_fw_faults", CTLTYPE_INT, &sc->queue_fw_faults },
			{ "queue_hwr_flags", CTLTYPE_INT, &sc->queue_hwr_flags },
			{ "compute_stage", CTLTYPE_INT, &sc->compute_stage },
			{ "compute_error", CTLTYPE_INT, &sc->compute_error },
			{ "compute_attempted", CTLTYPE_INT, &sc->compute_attempted },
			{ "compute_polls", CTLTYPE_INT, &sc->compute_polls },
			{ "compute_sync_read", CTLTYPE_INT, &sc->compute_sync_read },
			{ "compute_sync_write", CTLTYPE_INT, &sc->compute_sync_write },
			{ "compute_sync_return", CTLTYPE_INT, &sc->compute_sync_return },
			{ "compute_kick_read", CTLTYPE_INT, &sc->compute_kick_read },
			{ "compute_kick_write", CTLTYPE_INT, &sc->compute_kick_write },
			{ "compute_kick_return", CTLTYPE_INT, &sc->compute_kick_return },
			{ "compute_ccb_read", CTLTYPE_INT, &sc->compute_ccb_read },
			{ "compute_ccb_write", CTLTYPE_INT, &sc->compute_ccb_write },
			{ "compute_ufo", CTLTYPE_INT, &sc->compute_ufo },
			{ "compute_canary", CTLTYPE_INT, &sc->compute_canary },
			{ "compute_faults", CTLTYPE_INT, &sc->compute_faults },
			{ "compute_hwr", CTLTYPE_INT, &sc->compute_hwr },
		};

		for (size_t i = 0; i < __arraycount(status); i++) {
			int error;

			if (status[i].type == CTLTYPE_QUAD)
				error = sysctl_createv(NULL, 0, &node, NULL, CTLFLAG_READONLY,
				    CTLTYPE_QUAD, status[i].name, NULL, a133_gpu_status,
				    0, (int64_t *)status[i].data, 0, CTL_CREATE, CTL_EOL);
			else
				error = sysctl_createv(NULL, 0, &node, NULL, CTLFLAG_READONLY,
				    CTLTYPE_INT, status[i].name, NULL, a133_gpu_status,
				    0, (int *)status[i].data, 0, CTL_CREATE, CTL_EOL);
			if (error != 0) {
				aprint_error_dev(self, "cannot create firmware status\n");
				sysctl_teardown(&sc->sysctllog);
				mutex_destroy(&sc->irq_lock);
				mutex_destroy(&sc->lock);
				return;
			}
		}
	}
	a133_gpu_instance = sc;
	aprint_normal_dev(self, "experimental GE8300 firmware owner; idle\n");
}
