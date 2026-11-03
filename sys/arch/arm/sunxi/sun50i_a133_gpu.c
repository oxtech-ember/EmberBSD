/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD A133 PowerVR FDT owner, AI-assisted. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/mutex.h>
#include <sys/intr.h>
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
#include <dev/powervr/pvr_mips_irq.h>
#include <arm/sunxi/sun50i_a133_gpu_owner.h>

#define A133_GPU_BASE UINT64_C(0x01800000)
#define A133_GPU_SIZE UINT64_C(0x80000)
#define A133_GPU_DFS_REG UINT64_C(0x01880020)
#define A133_GPU_DFS_SIZE 4
#define A133_GPU_BVNC_REG 0x20
#define A133_GPU_FW_SIZE 118784

struct a133_gpu_softc {
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
	if (microvolts != 940000 && microvolts != 950000) {
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
	if (sysctl_createv(&sc->sysctllog, 0, NULL, &node, 0,
	    CTLTYPE_NODE, device_xname(self), NULL, NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL) != 0 ||
	    sysctl_createv(NULL, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "firmware_boot", SYSCTL_DESCR("explicit GE8300 firmware boot"),
	    a133_gpu_control, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0 ||
	    sysctl_createv(NULL, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "queue_probe", SYSCTL_DESCR("explicit firmware queue check"),
	    a133_gpu_queue_control, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0) {
		aprint_error_dev(self, "cannot create firmware control\n");
		sysctl_teardown(&sc->sysctllog);
		mutex_destroy(&sc->irq_lock);
		mutex_destroy(&sc->lock);
		return;
	}
	{
		const struct { const char *name; int type; void *data; } status[] = {
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
