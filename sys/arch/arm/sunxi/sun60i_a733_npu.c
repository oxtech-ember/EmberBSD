/* $NetBSD$ */

/*-
 * Copyright (c) 2026 EmberBSD contributors
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Origin: EmberBSD; firmware-ready A733 NPU identification only.
 * Hardware facts: Orange Pi BSP 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f,
 * bsp/drivers/npu/aw_nna_vip/vip2 and bsp/configs/linux-6.6/sun60iw2p1.dtsi.
 * The runtime stages are single watchdog-bounded steps; no console output
 * happens inside the sysctl path (a prior experiment stalled the kernel
 * that way). Results leave through the sysctl nodes only.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <arm/sunxi/sun60i_a733_ccu.h>
#include <arm/sunxi/sun60i_a733_pck600.h>

#define A733_NPU_BASE		0x03600000
#define A733_NPU_ID_SIZE	0x34
#define A733_NPU_ID_PID		0x30
#define A733_NPU_ID_DATE	0x28
#define A733_NPU_EXPECTED_PID	UINT64_C(0x1000003b)
#define A733_NPU_EXPECTED_REV	UINT64_C(0x9202)

/* Vendor order: AHB and AXI resets first, then core, then gates/clocks. */
#define A733_NPU_STAGE_DOMAIN	1
#define A733_NPU_STAGE_CLOCKS	2
#define A733_NPU_STAGE_RAW_ID	3
#define A733_NPU_STAGE_POWER_ID	4

struct sun60i_npu_softc {
	device_t sc_dev;
	int sc_phandle;
	bus_space_tag_t sc_bst;
	kmutex_t sc_run_lock;
	struct sysctllog *sc_sysctllog;
	u_int sc_run_stage;
	u_int sc_run_result;
	uint64_t sc_pid, sc_rev, sc_date;
	bool sc_run_pass;
};

static struct sun60i_npu_softc *sun60i_npu_instance;

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-npu" },
	DEVICE_COMPAT_EOL
};
static const struct device_compatible_entry power_compat[] = {
	{ .compat = "allwinner,sun60i-a733-pck-600" },
	DEVICE_COMPAT_EOL
};

static int
sun60i_npu_power_node(struct sun60i_npu_softc *sc)
{
	const uint32_t *pd;
	int len;

	pd = fdtbus_get_prop(sc->sc_phandle, "power-domains", &len);
	if (pd == NULL || len != 2 * sizeof(*pd))
		return -1;
	return fdtbus_get_phandle_from_native(be32toh(pd[0]));
}

/* Stage 1: the PCK-600 domain the vendor associates with the NPU. */
static int
sun60i_npu_stage_domain(struct sun60i_npu_softc *sc, int power_node)
{
	uint32_t pwpr, pmer, pwsr;
	int error;

	error = sun60i_a733_pck_domain_state(power_node, 4, &pwpr, &pmer,
	    &pwsr);
	if (error != 0)
		return error;
	aprint_normal_dev(sc->sc_dev,
	    "NPU domain: PWPR 0x%08x PMER 0x%08x PWSR 0x%08x\n",
	    pwpr, pmer, pwsr);
	return (pwpr & __BITS(3, 0)) == 8 ? 0 : EBUSY;
}

/* Stage 2: decoded state of the five vendor clocks; read-only. */
static int
sun60i_npu_stage_clocks(struct sun60i_npu_softc *sc)
{
	static const char * const names[] = {
		"clk_npu", "clk_parent", "clk_bus", "clk_mbus_gate",
		"clk_ahb_gate",
	};
	struct clk *clk;
	u_int rate;

	for (u_int i = 0; i < __arraycount(names); i++) {
		clk = fdtbus_clock_get(sc->sc_phandle, names[i]);
		if (clk == NULL)
			return ENXIO;
		rate = clk_get_rate(clk);
		aprint_normal_dev(sc->sc_dev, "clock %s: %u Hz\n",
		    names[i], rate);
		clk_put(clk);
	}
	return 0;
}

/* The identification triple at fixed offsets of the NPU window. */
/*
 * One bounded identification read of the fixed offsets. The PID is a
 * 64-bit read at 0x30; the revision and date sit at 0x24 and 0x28.
 */
static int
sun60i_npu_read_ids(struct sun60i_npu_softc *sc)
{
	bus_space_handle_t bsh;
	uint32_t t;
	int error;

	error = bus_space_map(sc->sc_bst, A733_NPU_BASE, A733_NPU_ID_SIZE,
	    0, &bsh);
	if (error != 0)
		return error;
	error = 0;
	if (bus_space_peek_8(sc->sc_bst, bsh, A733_NPU_ID_PID,
	    &sc->sc_pid) != 0)
		error = EFAULT;
	if (bus_space_peek_4(sc->sc_bst, bsh, 0x24, &t) == 0) {
		sc->sc_rev = t;
		if (bus_space_peek_4(sc->sc_bst, bsh, A733_NPU_ID_DATE,
		    &t) == 0)
			sc->sc_date = t;
	}
	bus_space_unmap(sc->sc_bst, bsh, A733_NPU_ID_SIZE);
	return error;
}

/* Stage 4: the vendor power order through the generic FDT interfaces. */
static int
sun60i_npu_stage_power_id(struct sun60i_npu_softc *sc)
{
	static const char * const resets[] = {
		"npu_ahb_rst", "npu_axi_rst", "npu_rst",
	};
	static const char * const gates[] = {
		"clk_ahb_gate", "clk_mbus_gate", "clk_bus", "clk_npu",
	};
	struct fdtbus_reset *rst;
	struct clk *clk;
	int error;

	for (u_int i = 0; i < __arraycount(resets); i++) {
		rst = fdtbus_reset_get(sc->sc_phandle, resets[i]);
		if (rst == NULL)
			return ENOENT;
		error = fdtbus_reset_deassert(rst);
		fdtbus_reset_put(rst);
		if (error != 0)
			return error;
	}
	for (u_int i = 0; i < __arraycount(gates); i++) {
		clk = fdtbus_clock_get(sc->sc_phandle, gates[i]);
		if (clk == NULL)
			return ENODEV;
		error = clk_enable(clk);
		clk_put(clk);
		if (error != 0)
			return error;
	}
	return sun60i_npu_read_ids(sc);
}

static void
sun60i_npu_run_stage(struct sun60i_npu_softc *sc, u_int stage)
{
	int power_node, error = EINVAL;

	mutex_enter(&sc->sc_run_lock);
	/* Stage 0 is a dry run of the sysctl machinery: no hardware calls. */
	if (stage == 0) {
		sc->sc_run_result = 0;
		mutex_exit(&sc->sc_run_lock);
		return;
	}
	if (stage < A733_NPU_STAGE_DOMAIN || stage > A733_NPU_STAGE_POWER_ID ||
	    stage != sc->sc_run_stage + 1 || !sc->sc_run_pass) {
		sc->sc_run_result = EINVAL;
		mutex_exit(&sc->sc_run_lock);
		return;
	}
	power_node = sun60i_npu_power_node(sc);
	if (power_node < 0 || !of_compatible_match(power_node, power_compat))
		goto out;
	switch (stage) {
	case A733_NPU_STAGE_DOMAIN:
		error = sun60i_npu_stage_domain(sc, power_node);
		break;
	case A733_NPU_STAGE_CLOCKS:
		error = sun60i_npu_stage_clocks(sc);
		break;
	case A733_NPU_STAGE_RAW_ID:
		error = sun60i_npu_read_ids(sc);
		break;
	case A733_NPU_STAGE_POWER_ID:
		error = sun60i_npu_stage_power_id(sc);
		break;
	}
	sc->sc_run_pass = (error == 0 || error == EBUSY);
	sc->sc_run_stage = stage;
out:
	sc->sc_run_result = error;
	mutex_exit(&sc->sc_run_lock);
}

static int
sun60i_npu_sysctl_stage(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct sun60i_npu_softc *sc = sun60i_npu_instance;
	u_int value;
	int error;

	if (sc == NULL)
		return ENXIO;
	value = sc->sc_run_stage;
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	/* No console output from this handler; see the GPU staging lesson. */
	sun60i_npu_run_stage(sc, value);
	return sc->sc_run_result;
}

static void
sun60i_npu_sysctl_setup(struct sun60i_npu_softc *sc)
{
	const struct sysctlnode *rnode;

	if (sysctl_createv(&sc->sc_sysctllog, 0, NULL, &rnode, 0,
	    CTLTYPE_NODE, device_xname(sc->sc_dev), NULL, NULL, 0,
	    NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL) != 0) {
		aprint_error_dev(sc->sc_dev, "cannot create sysctl node\n");
		return;
	}
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "stage",
		    SYSCTL_DESCR("run NPU diagnostic stage 0..4"),
		    sun60i_npu_sysctl_stage, 0, NULL, 0,
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create stage node\n");
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "result",
		    SYSCTL_DESCR("errno of the last NPU stage"),
		    NULL, 0, (int *)&sc->sc_run_result, 0,
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create result node\n");
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_QUAD, "pid",
		    SYSCTL_DESCR("last read NPU PID value"),
		    NULL, 0, (int64_t *)&sc->sc_pid, 0,
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create pid node\n");
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_QUAD, "revision",
		    SYSCTL_DESCR("last read NPU revision value"),
		    NULL, 0, (int64_t *)&sc->sc_rev, 0,
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create revision node\n");
}

static int
sun60i_npu_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_npu_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_npu_softc * const sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	bus_addr_t addr;
	bus_size_t size;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;
	sc->sc_run_pass = true;
	mutex_init(&sc->sc_run_lock, MUTEX_DEFAULT, IPL_NONE);
	sun60i_npu_instance = sc;
	if (fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size) != 0 ||
	    addr != A733_NPU_BASE || size < A733_NPU_ID_SIZE)
		aprint_error_dev(self, "unexpected register window\n");
	sun60i_npu_sysctl_setup(sc);
	aprint_naive("\n");
	aprint_normal(": A733 NPU staged identification\n");
}

CFATTACH_DECL_NEW(sun60i_a733_npu, sizeof(struct sun60i_npu_softc),
    sun60i_npu_match, sun60i_npu_attach, NULL, NULL);
