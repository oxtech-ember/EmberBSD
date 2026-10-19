/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; AI-assisted native A733 HDMI output provider.
 * Hardware facts and experimental boundary: ember/boot/a733-tv0-native.md.
 * The DE coordinator owns the retained DISPLAY lease. Attach performs no I/O.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/syscon.h>
#include <dev/ic/dw_hdmi.h>
#include <dev/videomode/edidvar.h>
#include <dev/videomode/videomode.h>
#include <arm/sunxi/sun60i_a733_ccu.h>
#include <arm/sunxi/sun60i_a733_hdmi_phy.h>
#include <arm/sunxi/sun60i_a733_output.h>
#include <arm/sunxi/sun60i_a733_tv0.h>
#include <arm/sunxi/sunxi_rtcvar.h>

#define A733_HDMI_BASE UINT32_C(0x05520000)
#define A733_VO1_BASE UINT32_C(0x05510000)
#define A733_TV0_BASE UINT32_C(0x05730000)
#define A733_HDMI_TOP UINT32_C(0x000e0000)
#define A733_HDMI_SIZE UINT32_C(0x00100000)
#define A733_OUTPUT_SIZE UINT32_C(0x1000)

struct sun60i_a733_hdmi_softc {
	struct dwhdmi_softc dw;
	int phandle, rtc_phandle, fault;
	bus_space_tag_t bst;
	bus_space_handle_t hdmi, vo1, tv0;
	struct clk *de0;
	struct syscon *syscfg;
	struct fdtbus_regulator *vdd08, *vcc18;
	const void *owner;
	bool hdmi_ready, tv0_ready, written, syscfg_ready, mode_valid;
	struct sunxi_rtc_dcxo_state dcxo;
	struct sun60i_a733_hdmi_io phy_io;
	struct sun60i_a733_hdmi_phy phy;
	struct sun60i_a733_tv0_io tv_io;
	struct sun60i_a733_tv0 tv;
	struct sun60i_a733_output_mode mode;
	struct edid_info edid;
	uint8_t edid_block[128];
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-hdmi" },
	DEVICE_COMPAT_EOL
};

static int
native_fail(struct sun60i_a733_hdmi_softc *sc, int error)
{
	if (sc->fault == 0 && error != 0) {
		sc->fault = error;
		if (sc->dw.sc_io_error == 0)
			sc->dw.sc_io_error = error;
		if (sc->phy.fault == 0)
			sc->phy.fault = error;
		if (sc->tv.fault == 0)
			sc->tv.fault = error;
		if (sc->owner != NULL)
			(void)sun60i_a733_ccu_display_fault(sc->de0, sc->owner, error);
	}
	return sc->fault;
}

static int
native_rail(struct fdtbus_regulator *rail, u_int wanted)
{
	bool enabled;
	u_int voltage;
	int error;

	if (rail == NULL)
		return ENXIO;
	error = fdtbus_regulator_is_enabled(rail, &enabled);
	if (error != 0)
		return error;
	if (!enabled)
		return EOPNOTSUPP;
	error = fdtbus_regulator_get_voltage(rail, &voltage);
	if (error != 0)
		return error;
	return voltage == wanted ? 0 : EOPNOTSUPP;
}

/* Read-only proof. All native calls run under the output provider mutex. */
static int
native_ready(void *arg, const void *owner)
{
	struct sun60i_a733_hdmi_softc *sc = arg;
	struct sunxi_rtc_dcxo_state state;
	int error;

	if (sc->fault != 0)
		return sc->fault;
	if (owner == NULL || owner != sc->owner)
		return EBUSY;
	error = sun60i_a733_ccu_display_ready(sc->de0, owner);
	if (error == 0)
		error = native_rail(sc->vdd08, 800000);
	if (error == 0)
		error = native_rail(sc->vcc18, 1800000);
	if (error == 0)
		error = sunxi_rtc_dcxo_query(sc->rtc_phandle, &state);
	if (error == 0 && (state.rate_hz != sc->dcxo.rate_hz ||
	    state.sample[0] != sc->dcxo.sample[0] ||
	    state.sample[1] != sc->dcxo.sample[1]))
		error = EBUSY;
	if (error == 0 && sc->syscfg_ready) {
		syscon_lock(sc->syscfg);
		if ((syscon_read_4(sc->syscfg, 0x160) & __BIT(12)) == 0 ||
		    (syscon_read_4(sc->syscfg, 0x164) & UINT32_C(0xff000000)) != 0)
			error = EBUSY;
		syscon_unlock(sc->syscfg);
	}
	return native_fail(sc, error);
}

static int
native_phy_read(void *arg, enum sun60i_a733_hdmi_space space, uint32_t reg,
    uint32_t *out)
{
	struct sun60i_a733_hdmi_softc *sc = arg;
	uint32_t word;
	uint8_t byte = 0;
	int error;

	if (sc->fault != 0)
		return sc->fault;
	if (!sc->hdmi_ready || sc->owner == NULL || out == NULL)
		return native_fail(sc, EACCES);
	if (space == A733_HDMI_CORE8 && reg < 0x8000) {
		error = bus_space_peek_1(sc->bst, sc->hdmi, reg, &byte);
		word = byte;
	} else if (space == A733_HDMI_TOP32 && reg <= 0x40 && reg % 4 == 0)
		error = bus_space_peek_4(sc->bst, sc->hdmi, A733_HDMI_TOP + reg, &word);
	else
		return native_fail(sc, ERANGE);
	if (error != 0)
		return native_fail(sc, EFAULT);
	*out = word;
	return 0;
}

static int
native_phy_write(void *arg, enum sun60i_a733_hdmi_space space, uint32_t reg,
    uint32_t value)
{
	struct sun60i_a733_hdmi_softc *sc = arg;
	bus_size_t offset, width;
	int error;

	if (sc->fault != 0)
		return sc->fault;
	if (!sc->hdmi_ready || sc->owner == NULL)
		return native_fail(sc, EACCES);
	if (space == A733_HDMI_CORE8 && reg < 0x8000 && value <= UINT8_MAX) {
		offset = reg;
		width = 1;
	} else if (space == A733_HDMI_TOP32 && reg <= 0x40 && reg % 4 == 0) {
		offset = A733_HDMI_TOP + reg;
		width = 4;
	} else
		return native_fail(sc, ERANGE);
	sc->written = true;
	error = width == 1 ? bus_space_poke_1(sc->bst, sc->hdmi, offset, value) :
	    bus_space_poke_4(sc->bst, sc->hdmi, offset, value);
	if (error != 0)
		return native_fail(sc, EFAULT);
	bus_space_barrier(sc->bst, sc->hdmi, offset, width, BUS_SPACE_BARRIER_WRITE);
	return 0;
}

static void
native_delay(void *arg, unsigned usec)
{
	struct sun60i_a733_hdmi_softc *sc = arg;

	if (sc->fault == 0)
		delay(usec);
}

static int
native_dw_read(struct dwhdmi_softc *dw, bus_size_t reg, uint8_t *out)
{
	struct sun60i_a733_hdmi_softc *sc = container_of(dw,
	    struct sun60i_a733_hdmi_softc, dw);
	uint32_t value;
	int error;

	if (out == NULL || reg >= 0x8000)
		return native_fail(sc, ERANGE);
	error = native_phy_read(sc, A733_HDMI_CORE8, reg, &value);
	if (error == 0)
		*out = value;
	return error;
}

static int
native_dw_write(struct dwhdmi_softc *dw, bus_size_t reg, uint8_t value)
{
	struct sun60i_a733_hdmi_softc *sc = container_of(dw,
	    struct sun60i_a733_hdmi_softc, dw);

	if (reg >= 0x8000)
		return native_fail(sc, ERANGE);
	return native_phy_write(sc, A733_HDMI_CORE8, reg, value);
}

static int
native_tv_access(struct sun60i_a733_hdmi_softc *sc,
    enum sun60i_a733_tv0_space space, uint32_t reg, bus_space_handle_t *handle)
{
	if (sc->fault != 0)
		return sc->fault;
	if (!sc->hdmi_ready || sc->owner == NULL)
		return native_fail(sc, EACCES);
	if (reg % 4 != 0)
		return native_fail(sc, ERANGE);
	if (space == A733_TV0_REGS && reg <= 0x340 && sc->tv0_ready)
		*handle = sc->tv0;
	else if (space == A733_VO1_REGS && (reg == 0 || reg == 0x20))
		*handle = sc->vo1;
	else
		return native_fail(sc, ERANGE);
	return 0;
}

static int
native_tv_read(void *arg, enum sun60i_a733_tv0_space space, uint32_t reg,
    uint32_t *out)
{
	struct sun60i_a733_hdmi_softc *sc = arg;
	bus_space_handle_t handle;
	uint32_t value;
	int error;

	if (out == NULL)
		return native_fail(sc, EINVAL);
	error = native_tv_access(sc, space, reg, &handle);
	if (error != 0)
		return error;
	if (bus_space_peek_4(sc->bst, handle, reg, &value) != 0)
		return native_fail(sc, EFAULT);
	*out = value;
	return 0;
}

static int
native_tv_write(void *arg, enum sun60i_a733_tv0_space space, uint32_t reg,
    uint32_t value)
{
	struct sun60i_a733_hdmi_softc *sc = arg;
	bus_space_handle_t handle;
	int error = native_tv_access(sc, space, reg, &handle);

	if (error != 0)
		return error;
	sc->written = true;
	if (bus_space_poke_4(sc->bst, handle, reg, value) != 0)
		return native_fail(sc, EFAULT);
	bus_space_barrier(sc->bst, handle, reg, 4, BUS_SPACE_BARRIER_WRITE);
	return 0;
}

static int
native_resistor(struct sun60i_a733_hdmi_softc *sc)
{
	uint32_t value;
	int error = native_ready(sc, sc->owner);

	if (error != 0)
		return error;
	/* SYS_CFG uses the shared syscon mutex. Do not replace unrelated bits. */
	syscon_lock(sc->syscfg);
	value = syscon_read_4(sc->syscfg, 0x160);
	sc->written = true;
	syscon_write_4(sc->syscfg, 0x160, value | __BIT(12));
	if ((syscon_read_4(sc->syscfg, 0x160) & __BIT(12)) == 0)
		error = EIO;
	if (error == 0) {
		value = syscon_read_4(sc->syscfg, 0x164);
		syscon_write_4(sc->syscfg, 0x164, value & ~UINT32_C(0xff000000));
		if ((syscon_read_4(sc->syscfg, 0x164) & UINT32_C(0xff000000)) != 0)
			error = EIO;
	}
	syscon_unlock(sc->syscfg);
	if (error == 0)
		sc->syscfg_ready = true;
	return native_fail(sc, error);
}

/* Pinned BSP uses ceil(SFR[Hz] * time[ns] / 1e9), not the physical DCXO. */
static int
native_ddc_counts(uint32_t hz, u_int *high, u_int *low)
{
	uint64_t h, l;

	if (hz == 0 || high == NULL || low == NULL)
		return EINVAL;
	h = ((uint64_t)hz * 9184 + 999999999) / 1000000000;
	l = ((uint64_t)hz * 10204 + 999999999) / 1000000000;
	if (h == 0 || l == 0 || h > UINT16_MAX || l > UINT16_MAX)
		return ERANGE;
	*high = h;
	*low = l;
	return 0;
}

static int
native_mode(uint32_t dcxo, const struct videomode *v,
    struct sun60i_a733_output_mode *out)
{
	const unsigned flags = VID_PHSYNC | VID_NHSYNC | VID_PVSYNC | VID_NVSYNC;
	struct sun60i_a733_output_mode m;
	struct sun60i_a733_hdmi_plan plan;
	struct sun60i_a733_tcon_timing timing;
	int error;

	if (v == NULL || out == NULL || v->dot_clock <= 0 || v->dot_clock > 165000 ||
	    v->hdisplay <= 0 || v->hdisplay > 1920 ||
	    v->vdisplay <= 0 || v->vdisplay > 1080 ||
	    v->vtotal <= v->vdisplay || v->vtotal - v->vdisplay > 255 ||
	    v->vtotal - v->vdisplay <= 5 || (v->flags & ~flags) != 0 ||
	    (v->flags & (VID_PHSYNC | VID_NHSYNC)) == (VID_PHSYNC | VID_NHSYNC) ||
	    (v->flags & (VID_PVSYNC | VID_NVSYNC)) == (VID_PVSYNC | VID_NVSYNC))
		return EINVAL;
	m = (struct sun60i_a733_output_mode){
	    .pixel_clock_hz = (uint32_t)v->dot_clock * 1000,
	    .hdisplay = v->hdisplay, .hsync_start = v->hsync_start,
	    .hsync_end = v->hsync_end, .htotal = v->htotal,
	    .vdisplay = v->vdisplay, .vsync_start = v->vsync_start,
	    .vsync_end = v->vsync_end, .vtotal = v->vtotal,
	    .hsync_positive = (v->flags & VID_PHSYNC) != 0,
	    .vsync_positive = (v->flags & VID_PVSYNC) != 0
	};
	error = sun60i_a733_tcon_timing_encode(&m, &timing);
	if (error == 0)
		error = sun60i_a733_hdmi_plan(dcxo, m.pixel_clock_hz, &plan);
	if (error == 0)
		*out = m;
	return error;
}

static int
native_select(struct sun60i_a733_hdmi_softc *sc)
{
	struct edid_info *edid = &sc->edid;
	int error = EOPNOTSUPP;

	if (edid->edid_nmodes <= 0 ||
	    edid->edid_nmodes > __arraycount(edid->edid_modes))
		return EINVAL;
	/* Only use pointers that refer to a mode in this parsed base block. */
	for (u_int i = 0; i < edid->edid_nmodes; i++)
		if (&edid->edid_modes[i] == edid->edid_preferred_mode &&
		    native_mode(sc->dcxo.rate_hz, &edid->edid_modes[i], &sc->mode) == 0)
			return 0;
	for (u_int i = 0; i < edid->edid_nmodes; i++) {
		error = native_mode(sc->dcxo.rate_hz, &edid->edid_modes[i], &sc->mode);
		if (error == 0)
			break;
	}
	return error;
}

static int
native_get_mode(void *arg, const void *owner, struct sun60i_a733_output_mode *out)
{
	struct sun60i_a733_hdmi_softc *sc = arg;
	struct sun60i_a733_display_hdmi_clock sfr;
	uint32_t hpd;
	uint8_t block = 0;
	int error;

	if (sc->fault != 0)
		return sc->fault;
	if (sc->owner != NULL || owner == NULL || out == NULL)
		return native_fail(sc, EINVAL);
	sc->owner = owner;
	sc->phy.owner = owner;
	sc->tv.owner = owner;
	/* The coordinator must retain its lease before any provider action. */
	error = sun60i_a733_ccu_display_ready(sc->de0, owner);
	if (error == 0)
		error = sunxi_rtc_dcxo_query(sc->rtc_phandle, &sc->dcxo);
	if (error != 0)
		return native_fail(sc, error);
	sc->vdd08 = fdtbus_regulator_acquire(sc->phandle, "vdd08-supply");
	if (sc->vdd08 == NULL)
		return native_fail(sc, ENXIO);
	sc->vcc18 = fdtbus_regulator_acquire(sc->phandle, "vcc18-supply");
	error = native_ready(sc, owner);
	if (error == 0)
		error = sun60i_a733_ccu_display_begin_hdmi(sc->de0, owner);
	if (error != 0)
		return native_fail(sc, error);
	sc->hdmi_ready = true;
	error = sun60i_a733_tv0_route(&sc->tv);
	if (error == 0)
		error = native_resistor(sc);
	if (error == 0)
		error = sun60i_a733_hdmi_hpd_prepare(&sc->phy);
	if (error == 0)
		error = native_phy_read(sc, A733_HDMI_CORE8, 0x3004, &hpd);
	if (error == 0 && (hpd & 2) == 0)
		error = ENXIO;
	if (error == 0)
		error = dwhdmi_native_init(&sc->dw);
	if (error == 0)
		error = sun60i_a733_ccu_display_hdmi_clock_state(sc->de0, owner, &sfr);
	if (error == 0)
		error = native_ddc_counts(sfr.hz, &sc->dw.sc_scl_hcnt, &sc->dw.sc_scl_lcnt);
	if (error != 0)
		return native_fail(sc, error);
	error = iic_acquire_bus(sc->dw.sc_ic, 0);
	if (error != 0)
		return native_fail(sc, error);
	error = iic_exec(sc->dw.sc_ic, I2C_OP_READ_WITH_STOP, DDC_ADDR, &block, 1,
	    sc->edid_block, sizeof(sc->edid_block), 0);
	iic_release_bus(sc->dw.sc_ic, 0);
	if (error == 0) {
		memset(&sc->edid, 0, sizeof(sc->edid));
		if (edid_parse_quiet(sc->edid_block, &sc->edid) != 0)
			error = EINVAL;
	}
	if (error == 0)
		error = native_select(sc);
	if (error == 0)
		error = native_ready(sc, owner);
	if (error == 0)
		error = native_phy_read(sc, A733_HDMI_CORE8, 0x3004, &hpd);
	if (error == 0 && (hpd & 2) == 0)
		error = ENXIO;
	if (error != 0)
		return native_fail(sc, error);
	sc->mode_valid = true;
	*out = sc->mode;
	return 0;
}

static int
native_prepare(void *arg, const void *owner,
    const struct sun60i_a733_output_mode *mode)
{
	struct sun60i_a733_hdmi_softc *sc = arg;
	struct drm_display_mode drm = { 0 };
	int error = native_ready(sc, owner);

	if (error != 0)
		return error;
	if (!sc->mode_valid || mode == NULL)
		return native_fail(sc, EINVAL);
	/* The public dispatcher already compares every admitted mode field. */
	error = sun60i_a733_hdmi_phy_prepare(&sc->phy, sc->dcxo.rate_hz,
	    mode->pixel_clock_hz);
	if (error == 0)
		error = sun60i_a733_ccu_display_begin_tv0(sc->de0, owner);
	if (error != 0)
		return native_fail(sc, error);
	sc->tv0_ready = true;
	error = sun60i_a733_tv0_prepare(&sc->tv, mode, A733_TV0_EXPERIMENT_VENDOR_ZERO);
	if (error != 0)
		return native_fail(sc, error);
	drm.clock = drm.crtc_clock = mode->pixel_clock_hz / 1000;
	drm.hdisplay = drm.crtc_hdisplay = mode->hdisplay;
	drm.hsync_start = drm.crtc_hsync_start = mode->hsync_start;
	drm.hsync_end = drm.crtc_hsync_end = mode->hsync_end;
	drm.htotal = drm.crtc_htotal = mode->htotal;
	drm.vdisplay = drm.crtc_vdisplay = mode->vdisplay;
	drm.vsync_start = drm.crtc_vsync_start = mode->vsync_start;
	drm.vsync_end = drm.crtc_vsync_end = mode->vsync_end;
	drm.vtotal = drm.crtc_vtotal = mode->vtotal;
	drm.flags = (mode->hsync_positive ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC) |
	    (mode->vsync_positive ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC);
	error = dwhdmi_native_prepare(&sc->dw, &drm);
	if (error == 0)
		error = native_ready(sc, owner);
	return native_fail(sc, error);
}

static int
native_start(void *arg, const void *owner)
{
	struct sun60i_a733_hdmi_softc *sc = arg;
	int error = native_ready(sc, owner);

	if (error == 0)
		error = sun60i_a733_hdmi_phy_observe(&sc->phy);
	if (error == 0)
		error = dwhdmi_native_start(&sc->dw);
	if (error == 0)
		error = sun60i_a733_tv0_start(&sc->tv);
	return native_fail(sc, error);
}

static int
native_observe(void *arg, const void *owner, uint64_t *sequence)
{
	struct sun60i_a733_hdmi_softc *sc = arg;
	int error = native_ready(sc, owner);

	if (error == 0)
		error = sun60i_a733_hdmi_phy_observe(&sc->phy);
	if (error == 0)
		error = sun60i_a733_tv0_observe(&sc->tv, sequence);
	return error == ENOENT ? error : native_fail(sc, error);
}

static void
native_fault(void *arg, const void *owner, int error)
{
	struct sun60i_a733_hdmi_softc *sc = arg;

	if (sc->owner == NULL)
		sc->owner = owner;
	(void)native_fail(sc, error);
}

static const struct sun60i_a733_output_ops output_ops = {
	native_get_mode, native_prepare, native_start, native_observe, native_fault
};

static int
sun60i_a733_hdmi_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

/* Bind the first native profile to exact providers and board rail names. */
static bool
native_compatible(int node, const char *name)
{
	const struct device_compatible_entry table[] = {
		{ .compat = name }, DEVICE_COMPAT_EOL
	};

	return node > 0 && of_compatible_match(node, table) != 0;
}

static bool
native_range(int node, bus_addr_t base, bus_size_t length)
{
	bus_addr_t address;
	bus_size_t size;

	return node > 0 && fdtbus_get_reg(node, 0, &address, &size) == 0 &&
	    address == base && size == length;
}

static bool
native_supply(int phandle, const char *property, const char *rail)
{
	const int node = fdtbus_get_phandle(phandle, property);
	const char *name;
	int provider;

	if (node <= 0)
		return false;
	name = fdtbus_get_string(node, "regulator-name");
	provider = OF_parent(OF_parent(node));
	/* This provider's acquire callback performs no register access. */
	return name != NULL && strcmp(name, rail) == 0 &&
	    native_compatible(provider, "x-powers,axp8191");
}

static int
native_bindings(struct sun60i_a733_hdmi_softc *sc)
{
	const uint32_t *cells;
	int node, len;

	if (!of_hasprop(sc->phandle, "netbsd,consumer-managed-power") ||
	    !of_hasprop(sc->phandle, "allwinner,external-resistor"))
		return EINVAL;
	cells = fdtbus_get_prop(sc->phandle, "power-domains", &len);
	if (cells == NULL || len != 8 || be32toh(cells[1]) != 10)
		return EINVAL;
	node = fdtbus_get_phandle_from_native(be32toh(cells[0]));
	if (!native_compatible(node, "allwinner,sun60i-a733-pck-600") ||
	    !native_range(node, 0x07060000, 0xb000))
		return EINVAL;
	sc->rtc_phandle = fdtbus_get_phandle(sc->phandle, "netbsd,dcxo-source");
	if (!native_compatible(sc->rtc_phandle, "allwinner,sun60i-a733-rtc") ||
	    !native_range(sc->rtc_phandle, 0x07090000, 0x400))
		return EINVAL;
	node = fdtbus_get_phandle(sc->phandle, "syscon");
	if (!native_compatible(node, "syscon") ||
	    !native_range(node, 0x03000000, 0x1000) ||
	    !native_supply(sc->phandle, "vdd08-supply", "dcdc2") ||
	    !native_supply(sc->phandle, "vcc18-supply", "cldo2"))
		return EINVAL;
	sc->de0 = fdtbus_clock_get(sc->phandle, "de0");
	sc->syscfg = fdtbus_syscon_acquire(sc->phandle, "syscon");
	return sc->de0 != NULL && sc->syscfg != NULL ? 0 : ENXIO;
}

static void
sun60i_a733_hdmi_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_a733_hdmi_softc *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	const bus_addr_t addresses[] = { A733_HDMI_BASE, A733_VO1_BASE, A733_TV0_BASE };
	const bus_size_t sizes[] = { A733_HDMI_SIZE, A733_OUTPUT_SIZE, A733_OUTPUT_SIZE };
	const char * const names[] = { "hdmi", "vo1", "tv0" };
	bus_space_handle_t *handles[] = { &sc->hdmi, &sc->vo1, &sc->tv0 };
	bus_addr_t address;
	bus_size_t size;
	u_int mapped = 0;
	int error = EINVAL;

	sc->phandle = faa->faa_phandle;
	sc->bst = faa->faa_bst;
	sc->dw.sc_dev = self;
	aprint_naive("\n");
	aprint_normal(": A733 native HDMI output (explicit stages)\n");
	error = native_bindings(sc);
	if (error != 0)
		goto failed;
	for (u_int i = 0; i < __arraycount(names); i++) {
		if (fdtbus_get_reg_byname(sc->phandle, names[i], &address, &size) != 0 ||
		    address != addresses[i] || size != sizes[i]) {
			error = EINVAL;
			goto failed;
		}
		error = bus_space_map(sc->bst, address, size, 0, handles[i]);
		if (error != 0)
			goto failed;
		mapped++;
	}
	sc->dw.sc_bst = sc->bst;
	sc->dw.sc_bsh = sc->hdmi;
	sc->dw.sc_reg_width = 1;
	sc->dw.sc_read = native_dw_read;
	sc->dw.sc_write = native_dw_write;
	sc->phy_io = (struct sun60i_a733_hdmi_io){ sc, native_ready,
	    native_phy_read, native_phy_write, native_delay };
	sc->phy.io = &sc->phy_io;
	sc->tv_io = (struct sun60i_a733_tv0_io){ sc, native_ready,
	    native_tv_read, native_tv_write };
	sc->tv.io = &sc->tv_io;
	error = sun60i_a733_output_register(sc->phandle, &output_ops, sc);
	if (error == 0)
		return;
failed:
	while (mapped != 0) {
		mapped--;
		bus_space_unmap(sc->bst, *handles[mapped], sizes[mapped]);
	}
	if (sc->de0 != NULL)
		clk_put(sc->de0);
	aprint_error_dev(self, "native output unavailable: %d\n", error);
}

CFATTACH_DECL_NEW(sun60i_a733_hdmi, sizeof(struct sun60i_a733_hdmi_softc),
    sun60i_a733_hdmi_match, sun60i_a733_hdmi_attach, NULL, NULL);
