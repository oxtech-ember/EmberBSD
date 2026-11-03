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
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <libfdt.h>
#include <dev/clk/clk.h>
#include <dev/fdt/fdtvar.h>
#include <dev/firmload.h>
#include <dev/ofw/openfirm.h>
#include <dev/powervr/pvr_ge8300.h>
#include <dev/powervr/pvr_fwboot.h>
#include <arm/sunxi/sun50i_a133_gpu_owner.h>

#define A133_GPU_BASE UINT64_C(0x01800000)
#define A133_GPU_SIZE UINT64_C(0x80000)
#define A133_GPU_DFS_REG UINT64_C(0x01880020)
#define A133_GPU_DFS_SIZE 4
#define A133_GPU_BVNC_REG 0x20
#define A133_GPU_FW_SIZE 118784

struct a133_gpu_softc {
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
	struct a133_gpu_owner owner;
	kmutex_t lock;
	struct sysctllog *sysctllog;
	uint32_t core_hz;
	uint64_t effective_hz;
	int last_error;
	bool mapped, dfs_mapped;
};
static struct a133_gpu_softc *a133_gpu_instance;
static int a133_gpu_match(device_t, cfdata_t, void *);
static void a133_gpu_attach(device_t, device_t, void *);
static int a133_gpu_control(SYSCTLFN_ARGS);
static int a133_gpu_reserved_supported(void);
static const struct device_compatible_entry a133_gpu_compat[] = {
	{ .compat = "allwinner,sun50i-a133-powervr" },
	DEVICE_COMPAT_EOL
};
CFATTACH_DECL_NEW(sun50i_a133_gpu, sizeof(struct a133_gpu_softc),
    a133_gpu_match, a133_gpu_attach, NULL, NULL);

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
	if (fdtbus_regulator_is_enabled(sc->supply, &enabled) != 0 || !enabled ||
	    fdtbus_regulator_get_voltage(sc->supply, &microvolts) != 0 ||
	    microvolts != 950000) {
		error = EBUSY;
		goto fail;
	}
	error = bus_space_map(sc->bst, sc->address, sc->size, 0, &sc->bsh);
	if (error != 0)
		goto fail;
	sc->mapped = true;
	error = bus_space_map(sc->bst, sc->dfs_address, sc->dfs_size, 0,
	    &sc->dfs_bsh);
	if (error != 0)
		goto fail;
	sc->dfs_mapped = true;
	error = firmware_open("powervr", "rogue_22.102.54.38_v1.fw", &fh);
	if (error != 0)
		goto fail;
	length = firmware_get_size(fh);
	if (length != A133_GPU_FW_SIZE) {
		error = EINVAL;
		goto close;
	}
	sc->firmware_size = (size_t)length;
	sc->firmware = firmware_malloc(sc->firmware_size);
	if (sc->firmware == NULL) {
		error = ENOMEM;
		goto close;
	}
	error = firmware_read(fh, 0, sc->firmware, sc->firmware_size);
	if (error == 0)
		error = pvr_ge8300_fwinfo_parse(sc->firmware, sc->firmware_size,
		    PVR_GE8300_BVNC, &info);
close:
	close_error = firmware_close(fh);
	if (error == 0)
		error = close_error;
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
	bool enabled;
	u_int uv;
	int error;

	/* All acquired resources remain retained even on an uncertain failure. */
	error = clk_enable(sc->pll);
	if (error != 0)
		return error;
	/* Reject an unqualified programmed parent/divider before enabling gates. */
	sc->core_hz = clk_get_rate(sc->core);
	if (sc->core_hz != 504000000 && sc->core_hz != 252000000)
		return EOPNOTSUPP;
	error = fdtbus_reset_deassert(sc->reset);
	if (error == 0)
		error = clk_enable(sc->bus);
	if (error == 0)
		error = clk_enable(sc->core);
	if (error == 0)
		error = fdtbus_powerdomain_enable(sc->phandle);
	if (error != 0)
		return error;
	if (fdtbus_powerdomain_is_enabled_index(sc->phandle, 0, &enabled) != 0 ||
	    !enabled || fdtbus_regulator_is_enabled(sc->supply, &enabled) != 0 ||
	    !enabled || fdtbus_regulator_get_voltage(sc->supply, &uv) != 0)
		return EBUSY;
	if (uv != 950000 || clk_get_rate(sc->core) != sc->core_hz)
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

	if (fdtbus_powerdomain_is_enabled_index(sc->phandle, 0, &enabled) != 0 ||
	    !enabled || fdtbus_regulator_is_enabled(sc->supply, &enabled) != 0 ||
	    !enabled || fdtbus_regulator_get_voltage(sc->supply, &uv) != 0 ||
	    uv != 950000 || clk_get_rate(sc->core) != sc->core_hz)
		return EBUSY;
	if (bus_space_peek_8(sc->bst, sc->bsh, A133_GPU_BVNC_REG, &bvnc) != 0)
		return EIO;
	if (bvnc != PVR_GE8300_BVNC)
		return ENODEV;
	/* The separate DFS word is read only after the PPU reports 3D. */
	if (bus_space_peek_4(sc->bst, sc->dfs_bsh, 0, &dfs) != 0)
		return EIO;
	error = a133_gpu_effective_hz(sc->core_hz, dfs, &rate);
	if (error != 0 || !a133_gpu_opp_valid(rate, uv))
		return EOPNOTSUPP;
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
	error = pvr_fwmem_prepare(sc->dmat, sc->firmware, sc->firmware_size,
	    PVR_GE8300_BVNC, PVR_FW_PROFILE_GE8300, sc->address, sc->size,
	    sc->dfs_address, sc->dfs_size, &sc->memory);
	/* The source file buffer is CPU-only; prepared DMA owns its own copy. */
	firmware_free(sc->firmware, sc->firmware_size);
	sc->firmware = NULL;
	sc->firmware_size = 0;
	if (error != 0)
		return error;
	error = pvr_fwinfo_feature(&sc->memory->image.info,
	    PVR_FW_FEATURE_NUM_CLUSTERS, &clusters);
	if (error == 0)
		error = pvr_fwinfo_feature(&sc->memory->image.info,
		    PVR_FW_FEATURE_SLC_SIZE_IN_KILOBYTES, &slc);
	if (error != 0 || clusters != 1 || slc != 64)
		return EOPNOTSUPP;
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

static int
a133_gpu_start(void *cookie)
{
	struct a133_gpu_softc *sc = cookie;
	uint64_t effective;
	int error;

	error = a133_gpu_live(sc, &effective);
	if (error != 0)
		return error;
	if (effective != sc->effective_hz)
		return EBUSY;
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
	error = a133_gpu_owner_boot(&sc->owner, &ops, sc);
	sc->last_error = error;
	mutex_exit(&sc->lock);
	sysctl_relock();
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
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_NONE);
	if (sysctl_createv(&sc->sysctllog, 0, NULL, &node, 0,
	    CTLTYPE_NODE, device_xname(self), NULL, NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL) != 0 ||
	    sysctl_createv(NULL, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "firmware_boot", SYSCTL_DESCR("explicit GE8300 firmware boot"),
	    a133_gpu_control, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0) {
		aprint_error_dev(self, "cannot create firmware control\n");
		sysctl_teardown(&sc->sysctllog);
		mutex_destroy(&sc->lock);
		return;
	}
	a133_gpu_instance = sc;
	aprint_normal_dev(self, "experimental GE8300 firmware owner; idle\n");
}
