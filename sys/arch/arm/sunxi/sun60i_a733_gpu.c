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
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Origin: EmberBSD; firmware-ready A733 GPU identification only.
 * Hardware facts: Orange Pi BSP 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f.
 * The explicit experimental binding may prepare GPU-local clocks and reset.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <arm/sunxi/sun60i_a733_ccu.h>
#include <arm/sunxi/sun60i_a733_pck600.h>

#define A733_GPU_BASE		0x01800000
#define A733_GPU_CORE_ID	0x18
#define A733_GPU_PBVNC		0x20
#define A733_GPU_ID_SIZE		(A733_GPU_PBVNC + sizeof(uint64_t))
#define A733_GPU_TOP		5
#define A733_GPU_EXPECTED	UINT64_C(0x00240038006800b7)

/* Runtime diagnostic stages, run one sysctl write at a time. */
#define A733_GPU_STAGE_SUPPLY	1
#define A733_GPU_STAGE_TOP	2
#define A733_GPU_STAGE_RESERVE_PCK	3
#define A733_GPU_STAGE_RESERVE_CCU	4
#define A733_GPU_STAGE_RETAIN	5
#define A733_GPU_STAGE_PREPARE	6
#define A733_GPU_STAGE_INSPECT	7
#define A733_GPU_STAGE_REQUEST	8
#define A733_GPU_STAGE_WAIT	9
#define A733_GPU_STAGE_TOP_ID	10
#define A733_GPU_STAGE_IDENTIFY	11

struct sun60i_gpu_softc {
	device_t sc_dev;
	int sc_phandle;
	bus_space_tag_t sc_bst;
	const char *sc_stage;
	uint64_t sc_bvnc;
	u_int sc_core_hz, sc_bus_hz, sc_uvol;
	bool sc_have_id, sc_probed, sc_retained;
	kmutex_t sc_run_lock;
	u_int sc_run_stage;	/* last completed stage */
	u_int sc_run_result;	/* errno of the last attempt */
	bool sc_run_pass;	/* sequence may continue */
	bool sc_run_core_on;	/* stage 3 confirmed CORE ON */
	struct sysctllog *sc_sysctllog;
};

/* Single-instance device: the sysctl handler reaches the softc through it. */
static struct sun60i_gpu_softc *sun60i_gpu_instance;

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-gpu" },
	DEVICE_COMPAT_EOL
};
static const struct device_compatible_entry power_compat[] = {
	{ .compat = "allwinner,sun60i-a733-pck-600" },
	DEVICE_COMPAT_EOL
};
static const struct device_compatible_entry pmic_compat[] = {
	{ .compat = "x-powers,axp8191" },
	DEVICE_COMPAT_EOL
};

static void
sun60i_gpu_clock_report(struct sun60i_gpu_softc *sc,
    const struct sun60i_a733_gpu_state *state)
{
	static const char * const reasons[] = {
		[A733_GPU_READY] = "ready",
		[A733_GPU_SNAPSHOT_CHANGED] = "CCU snapshot changed",
		[A733_GPU_HOSC_CHANGED] = "oscillator rate changed",
		[A733_GPU_DCXO_CHANGED] = "DCXO status changed",
		[A733_GPU_MODULE_GATED] = "GPU module gated",
		[A733_GPU_UPDATE_PENDING] = "GPU update pending",
		[A733_GPU_BUS_GATED] = "GPU bus gated",
		[A733_GPU_RESET_ASSERTED] = "GPU reset asserted",
		[A733_GPU_MASTER_GATED] = "GPU AHB master gated",
		[A733_GPU_REF_FLAGS] = "PLL_REF flags",
		[A733_GPU_REF_RATE] = "PLL_REF rate",
		[A733_GPU_CORE_PARENT] = "GPU parent unsupported",
		[A733_GPU_CORE_PLL] = "GPU parent PLL",
		[A733_GPU_CORE_DIVIDER] = "GPU divider",
		[A733_GPU_AHB_PARENT] = "AHB parent unsupported",
		[A733_GPU_AHB_PLL] = "AHB parent PLL",
		[A733_GPU_AHB_DIVIDER] = "AHB divider",
	};
	static const char * const names[A733_GPU_NREGS] = {
		[A733_GPU_REF] = "PLL_REF[0x000]",
		[A733_GPU_PERIPH] = "PLL_PERIPH0[0x0a0]",
		[A733_GPU_PERIPH_PAT0] = "PLL_PERIPH0_PAT0[0x0a8]",
		[A733_GPU_PERIPH_PAT1] = "PLL_PERIPH0_PAT1[0x0ac]",
		[A733_GPU_PLL] = "PLL_GPU0[0x0e0]",
		[A733_GPU_PAT0] = "PLL_GPU0_PAT0[0x0e8]",
		[A733_GPU_PAT1] = "PLL_GPU0_PAT1[0x0ec]",
		[A733_GPU_MODULE] = "GPU_CLK[0xb20]",
		[A733_GPU_BUS] = "GPU_BGR[0xb24]",
		[A733_GPU_AHB] = "AHB[0x500]",
		[A733_GPU_MASTER] = "AHB_MASTER[0x5c0]",
		[A733_GPU_PERIPH_GATE_EN] = "PERI0_GATE_EN[0x1908]",
		[A733_GPU_PERIPH_GATE_STAT] = "PERI0_GATE_STAT[0x1988]",
	};
	const char *reason = "unknown";

	if ((u_int)state->reason < __arraycount(reasons))
		reason = reasons[state->reason];
	aprint_normal_dev(sc->sc_dev, "CCU observation: %s (error %d), "
	    "DCDC4 %u uV, changed 0x%03x\n", reason, state->readiness_error,
	    sc->sc_uvol, state->changed);
	aprint_normal_dev(sc->sc_dev, "fixed hosc %u / %u Hz\n",
	    state->hosc_hz[0], state->hosc_hz[1]);
	for (u_int i = 0; i < 2; i++)
		aprint_normal_dev(sc->sc_dev, "DCXO %s CCU: 0x%08x / "
		    "0x%08x, %u Hz\n", i == 0 ? "before" : "after",
		    state->dcxo_sample[i][0], state->dcxo_sample[i][1],
		    state->dcxo_hz[i]);
	for (u_int i = 0; i < __arraycount(names); i++) {
		if (state->sample[0][i] == state->sample[1][i])
			aprint_normal_dev(sc->sc_dev, "%s 0x%08x\n", names[i],
			    state->sample[0][i]);
		else
			aprint_normal_dev(sc->sc_dev, "%s 0x%08x -> 0x%08x\n",
			    names[i], state->sample[0][i], state->sample[1][i]);
	}
	for (u_int i = 0; i < 2; i++)
		aprint_normal_dev(sc->sc_dev, "PERI0 gates sample %u: "
		    "configured 0x%03x, no-auto 0x%03x, effective 0x%03x "
		    "(400M bit1, 400M_ALL bit2, 600M bit9, 800M bit10); "
		    "diagnostic only\n", i, state->periph_gates[i].configured,
		    state->periph_gates[i].no_auto,
		    state->periph_gates[i].effective);
}

static void
sun60i_gpu_clock_terminal(struct sun60i_gpu_softc *sc, struct clk *gpu)
{
	struct sun60i_a733_gpu_state state;
	int error;

	/* A fresh observation cannot replace the failure or authorize GPU access. */
	aprint_normal_dev(sc->sc_dev, "terminal CCU observation after %s\n",
	    sc->sc_stage);
	error = sun60i_a733_ccu_gpu_inspect(gpu, &state);
	if (error != 0) {
		aprint_normal_dev(sc->sc_dev,
		    "terminal CCU observation unavailable: %d\n", error);
		return;
	}
	sun60i_gpu_clock_report(sc, &state);
}

/*
 * Vendor kernels keep GPU_CORE outside the GPU binding and read this bank
 * with only GPU_TOP managed.  After a failed CORE transition the same
 * resources still allow one bounded, fault-tolerant identification peek;
 * it never authorizes further GPU access.
 */
static void
sun60i_gpu_top_identify(struct sun60i_gpu_softc *sc, bus_addr_t addr,
    struct fdtbus_regulator *supply, struct clk *gpu, bool quiet)
{
	bus_space_handle_t bsh;
	u_int core_hz, bus_hz;
	uint64_t id;
	uint32_t core;
	bool enabled;

	if (fdtbus_regulator_is_enabled(supply, &enabled) != 0 || !enabled ||
	    fdtbus_regulator_get_voltage(supply, &sc->sc_uvol) != 0 ||
	    sc->sc_uvol != 800000 ||
	    fdtbus_powerdomain_is_enabled_index(sc->sc_phandle, 0, &enabled) != 0 ||
	    !enabled || sun60i_a733_ccu_gpu_ready(gpu, &core_hz, &bus_hz) != 0 ||
	    core_hz != 400000000 || bus_hz != 200000000) {
		if (!quiet) aprint_normal_dev(sc->sc_dev, "TOP-only identification "
		    "skipped: supply, GPU_TOP or clock readiness changed\n");
		return;
	}
	if (bus_space_map(sc->sc_bst, addr, A733_GPU_ID_SIZE, 0, &bsh) != 0) {
		if (!quiet) aprint_normal_dev(sc->sc_dev, "TOP-only identification "
		    "unavailable: mapping\n");
		return;
	}
	if (bus_space_peek_4(sc->sc_bst, bsh, A733_GPU_CORE_ID, &core) != 0)
		if (!quiet) aprint_normal_dev(sc->sc_dev, "TOP-only CORE_ID read failed\n");
	if (bus_space_peek_8(sc->sc_bst, bsh, A733_GPU_PBVNC, &id) != 0) {
		if (!quiet)
			aprint_normal_dev(sc->sc_dev,
			    "TOP-only PBVNC read failed\n");
		bus_space_unmap(sc->sc_bst, bsh, A733_GPU_ID_SIZE);
		return;
	}
	sc->sc_bvnc = id;
	sc->sc_have_id = true;
	if (!quiet) aprint_normal_dev(sc->sc_dev, "TOP-only identification: CORE_ID "
	    "0x%08x, PBVNC 0x%016llx: %u.%u.%u.%u%s\n", core,
	    (unsigned long long)id,
	    (u_int)(id >> 48),
	    (u_int)((id >> 32) & 0xffff),
	    (u_int)((id >> 16) & 0xffff),
	    (u_int)(id & 0xffff),
	    id == A733_GPU_EXPECTED ?
	    " (expected A733 GPU; GPU_CORE not confirmed ON)" :
	    " (unexpected)");
	bus_space_unmap(sc->sc_bst, bsh, A733_GPU_ID_SIZE);
}

/*
 * Runtime staged diagnostics.  The boot-time finalize path stays
 * property-gated and observe-only on normal DTBs; these stages re-run the
 * experiment pieces one explicit sysctl write at a time on a live system.
 * Each stage re-validates its own preconditions and prints to the log, so a
 * hardware watchdog armed in external-tickle mode bounds the whole
 * sequence: a stage that stalls the bus resets the board, and the invoking
 * transcript names the stalled stage.
 */
static int
sun60i_gpu_runtime_supply(struct sun60i_gpu_softc *sc)
{
	struct fdtbus_regulator *supply;
	bool enabled;
	int error;

	supply = fdtbus_regulator_acquire(sc->sc_phandle, "gpu-supply");
	if (supply == NULL)
		return ENXIO;
	error = fdtbus_regulator_is_enabled(supply, &enabled);
	if (error == 0 && !enabled)
		error = EBUSY;
	if (error == 0)
		error = fdtbus_regulator_get_voltage(supply, &sc->sc_uvol);
	if (error == 0 && sc->sc_uvol != 800000)
		error = EOPNOTSUPP;
	fdtbus_regulator_release(supply);
	return error;
}

static int
sun60i_gpu_powerdomain_top(struct sun60i_gpu_softc *sc)
{
	bool enabled;
	int error;

	error = fdtbus_powerdomain_is_enabled_index(sc->sc_phandle, 0, &enabled);
	if (error != 0)
		return error;
	return enabled ? 0 : EBUSY;
}

static int
sun60i_gpu_runtime_supply_top(struct sun60i_gpu_softc *sc)
{
	int error;

	error = sun60i_gpu_runtime_supply(sc);
	if (error != 0)
		return error;
	return sun60i_gpu_powerdomain_top(sc);
}

static int
sun60i_gpu_power_node(struct sun60i_gpu_softc *sc)
{
	const uint32_t *pd;
	int len;

	pd = fdtbus_get_prop(sc->sc_phandle, "power-domains", &len);
	if (pd == NULL || len != 2 * sizeof(*pd))
		return -1;
	return fdtbus_get_phandle_from_native(be32toh(pd[0]));
}

static int
sun60i_gpu_stage_request(struct sun60i_gpu_softc *sc, int power_node)
{
	int error;

	error = sun60i_gpu_runtime_supply_top(sc);
	if (error != 0)
		return error;
	return sun60i_a733_pck_gpu_request_on(power_node, sc, true);
}

static int
sun60i_gpu_stage_top_id(struct sun60i_gpu_softc *sc)
{
	struct fdtbus_regulator *supply;
	bus_addr_t addr;
	bus_size_t size;
	struct clk *gpu;
	int error;

	error = sun60i_gpu_runtime_supply_top(sc);
	if (error != 0)
		return error;
	error = fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size);
	if (error != 0 || addr != A733_GPU_BASE || size < A733_GPU_ID_SIZE)
		return EINVAL;
	supply = fdtbus_regulator_acquire(sc->sc_phandle, "gpu-supply");
	if (supply == NULL)
		return ENXIO;
	gpu = fdtbus_clock_get_index(sc->sc_phandle, 0);
	if (gpu == NULL) {
		fdtbus_regulator_release(supply);
		return ENXIO;
	}
	sun60i_gpu_top_identify(sc, addr, supply, gpu, true);
	clk_put(gpu);
	fdtbus_regulator_release(supply);
	return sc->sc_have_id ? 0 : EFAULT;
}

static int
sun60i_gpu_stage_identify(struct sun60i_gpu_softc *sc)
{
	bus_space_handle_t bsh;
	bus_addr_t addr;
	bus_size_t size;
	uint64_t id;
	int error;

	error = sun60i_gpu_runtime_supply_top(sc);
	if (error != 0)
		return error;
	error = fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size);
	if (error != 0 || addr != A733_GPU_BASE || size < A733_GPU_ID_SIZE)
		return EINVAL;
	error = bus_space_map(sc->sc_bst, addr, A733_GPU_ID_SIZE, 0, &bsh);
	if (error != 0)
		return error;
	error = bus_space_peek_8(sc->sc_bst, bsh, A733_GPU_PBVNC, &id);
	bus_space_unmap(sc->sc_bst, bsh, A733_GPU_ID_SIZE);
	if (error != 0)
		return EFAULT;
	sc->sc_bvnc = id;
	sc->sc_have_id = true;
	return id == A733_GPU_EXPECTED ? 0 : ENODEV;
}

static void
sun60i_gpu_run_stage(struct sun60i_gpu_softc *sc, u_int stage)
{
	int power_node, error = EINVAL;

	mutex_enter(&sc->sc_run_lock);
	/* Stage 0 is a dry run of the sysctl machinery: no provider calls. */
	if (stage == 0) {
		sc->sc_run_result = 0;
		mutex_exit(&sc->sc_run_lock);
		return;
	}
	/* Strictly ordered single sequence; a replay needs a fresh boot. */
	if (stage < A733_GPU_STAGE_SUPPLY || stage > A733_GPU_STAGE_IDENTIFY ||
	    stage != sc->sc_run_stage + 1 || !sc->sc_run_pass) {
		sc->sc_run_result = EINVAL;
		mutex_exit(&sc->sc_run_lock);
		return;
	}
	power_node = sun60i_gpu_power_node(sc);
	if (power_node < 0)
		goto out;
	switch (stage) {
	case A733_GPU_STAGE_SUPPLY:
		error = sun60i_gpu_runtime_supply(sc);
		break;
	case A733_GPU_STAGE_TOP:
		error = sun60i_gpu_powerdomain_top(sc);
		break;
	case A733_GPU_STAGE_RESERVE_PCK:
		error = sun60i_a733_pck_gpu_reserve(power_node, sc);
		break;
	case A733_GPU_STAGE_RESERVE_CCU: {
		struct clk *gpu = fdtbus_clock_get_index(sc->sc_phandle, 0);

		if (gpu == NULL) {
			error = ENXIO;
			break;
		}
		error = sun60i_a733_ccu_gpu_reserve(gpu, sc);
		clk_put(gpu);
		break;
	}
	case A733_GPU_STAGE_RETAIN:
		error = sun60i_a733_pck_gpu_retain(power_node, sc);
		break;
	case A733_GPU_STAGE_PREPARE: {
		struct clk *gpu = fdtbus_clock_get_index(sc->sc_phandle, 0);
		bool retained = false;

		if (gpu == NULL) {
			error = ENXIO;
			break;
		}
		error = sun60i_a733_ccu_gpu_prepare(gpu, sc, &retained);
		clk_put(gpu);
		break;
	}
	case A733_GPU_STAGE_INSPECT: {
		struct sun60i_a733_gpu_state state;
		struct clk *gpu = fdtbus_clock_get_index(sc->sc_phandle, 0);

		if (gpu == NULL) {
			error = ENXIO;
			break;
		}
		error = sun60i_a733_ccu_gpu_inspect(gpu, &state);
		if (error == 0) {
			error = state.readiness_error;
			sc->sc_core_hz = state.core_hz;
			sc->sc_bus_hz = state.bus_hz;
		}
		clk_put(gpu);
		break;
	}
	case A733_GPU_STAGE_REQUEST:
		error = sun60i_gpu_stage_request(sc, power_node);
		break;
	case A733_GPU_STAGE_WAIT:
		error = sun60i_a733_pck_gpu_wait(power_node, sc, true);
		sc->sc_run_core_on = (error == 0);
		break;
	case A733_GPU_STAGE_TOP_ID:
		error = sun60i_gpu_stage_top_id(sc);
		break;
	case A733_GPU_STAGE_IDENTIFY:
		if (!sc->sc_run_core_on) {
			error = EBUSY;
			break;
		}
		error = sun60i_gpu_stage_identify(sc);
		break;
	}
	/* A bounded timeout still allows the read-only waiter afterwards. */
	sc->sc_run_pass = (error == 0 || error == ETIMEDOUT);
	sc->sc_run_stage = stage;
out:
	sc->sc_run_result = error;
	mutex_exit(&sc->sc_run_lock);
}

static int
sun60i_gpu_sysctl_stage(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct sun60i_gpu_softc *sc = sun60i_gpu_instance;
	u_int value;

	if (sc == NULL)
		return ENXIO;
	int error;

	value = sc->sc_run_stage;
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	/* No console output from this handler: printing under the sysctl
	 * dispatch recurses into console/msgbuf locks and stalls the kernel. */
	sun60i_gpu_run_stage(sc, value);
	return sc->sc_run_result;
}

static void
sun60i_gpu_sysctl_setup(struct sun60i_gpu_softc *sc)
{
	const struct sysctlnode *rnode;
	const char *name;

	name = device_xname(sc->sc_dev);
	if (sysctl_createv(&sc->sc_sysctllog, 0, NULL, &rnode, 0,
	    CTLTYPE_NODE, name, NULL, NULL, 0,
	    NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL) != 0) {
		aprint_error_dev(sc->sc_dev, "cannot create sysctl node\n");
		return;
	}
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "stage",
		    SYSCTL_DESCR("run runtime diagnostic stage 1..5"),
		    sun60i_gpu_sysctl_stage, 0, NULL, 0,
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create stage node\n");
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "result",
		    SYSCTL_DESCR("errno of the last runtime stage"),
		    NULL, 0, (int *)&sc->sc_run_result, 0,
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create result node\n");
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_QUAD, "pbvnc",
		    SYSCTL_DESCR("last read GPU PBVNC value"),
		    NULL, 0, (int64_t *)&sc->sc_bvnc, 0,
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create pbvnc node\n");
}

static int
sun60i_gpu_identify(struct sun60i_gpu_softc *sc)
{
	struct sun60i_a733_gpu_state clocks_state;
	struct fdtbus_regulator *supply = NULL;
	struct clk *gpu = NULL;
	const uint32_t *pd, *clocks;
	const char *name;
	bus_space_handle_t bsh;
	bus_addr_t addr;
	bus_size_t size;
	uint32_t cells;
	uint64_t id;
	int len, node, error, power_node;
	bool enabled, observe_only, prepare, request;
	bool power_reserved = false, clock_reserved = false;
	sc->sc_have_id = false;
	sc->sc_stage = "binding";
	if (OF_getproplen(sc->sc_phandle, "netbsd,consumer-managed-power") != 0)
		return EINVAL;
	len = OF_getproplen(sc->sc_phandle, "netbsd,observe-only");
	if (len != -1 && len != 0)
		return EINVAL;
	observe_only = len == 0;
	len = OF_getproplen(sc->sc_phandle, "netbsd,experimental-clock-prepare");
	if (len != -1 && len != 0)
		return EINVAL;
	prepare = len == 0;
	if (prepare && observe_only)
		return EINVAL;
	len = OF_getproplen(sc->sc_phandle, "netbsd,experimental-domain-request");
	if (len != -1 && len != 0)
		return EINVAL;
	request = len == 0;
	/* The fresh static request re-uses the prepared clock reservation. */
	if (request && !prepare)
		return EINVAL;
	error = fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size);
	if (error != 0)
		return error;
	if (addr != A733_GPU_BASE || size < A733_GPU_ID_SIZE)
		return EINVAL;
	pd = fdtbus_get_prop(sc->sc_phandle, "power-domains", &len);
	if (pd == NULL || len != 2 * sizeof(*pd) ||
	    be32toh(pd[1]) != A733_GPU_TOP)
		return EINVAL;
	node = fdtbus_get_phandle_from_native(be32toh(pd[0]));
	power_node = node;
	if (!of_compatible_match(node, power_compat) ||
	    of_getprop_uint32(node, "#power-domain-cells", &cells) != 0 ||
	    cells != 1)
		return EINVAL;
	clocks = fdtbus_get_prop(sc->sc_phandle, "clocks", &len);
	if (clocks == NULL || len != 2 * sizeof(*clocks) ||
	    be32toh(clocks[1]) != A733_CLK_GPU0)
		return EINVAL;
	node = fdtbus_get_phandle_from_native(be32toh(clocks[0]));
	if (of_getprop_uint32(node, "#clock-cells", &cells) != 0 || cells != 1)
		return EINVAL;
	if (OF_getproplen(sc->sc_phandle, "gpu-supply") != sizeof(uint32_t))
		return EINVAL;
	node = fdtbus_get_phandle(sc->sc_phandle, "gpu-supply");
	name = node > 0 ? fdtbus_get_string(node, "name") : NULL;
	if (name == NULL || strcmp(name, "dcdc4") != 0 ||
	    !of_compatible_match(OF_parent(OF_parent(node)), pmic_compat))
		return EINVAL;

	sc->sc_stage = "supply provider";
	supply = fdtbus_regulator_acquire(sc->sc_phandle, "gpu-supply");
	if (supply == NULL)
		return ENXIO;
	sc->sc_stage = "supply state";
	error = fdtbus_regulator_is_enabled(supply, &enabled);
	if (error != 0)
		goto out;
	if (!enabled) {
		error = EBUSY;
		goto out;
	}
	sc->sc_stage = "supply voltage";
	error = fdtbus_regulator_get_voltage(supply, &sc->sc_uvol);
	if (error != 0)
		goto out;
	sc->sc_stage = "GPU_TOP state";
	error = fdtbus_powerdomain_is_enabled_index(sc->sc_phandle, 0, &enabled);
	if (error != 0)
		goto out;
	if (!enabled) {
		error = EBUSY;
		goto out;
	}
	sc->sc_stage = "clock provider";
	gpu = fdtbus_clock_get_index(sc->sc_phandle, 0);
	if (gpu == NULL) {
		error = ENXIO;
		goto out;
	}
	if (prepare) {
		sc->sc_stage = "experimental operating point";
		if (sc->sc_uvol != 800000) {
			error = EOPNOTSUPP;
			goto out;
		}
		sc->sc_stage = "experimental initial clock observation";
		error = sun60i_a733_ccu_gpu_inspect(gpu, &clocks_state);
		if (error != 0)
			goto out;
		aprint_normal_dev(sc->sc_dev,
		    "initial CCU observation before preparation\n");
		sun60i_gpu_clock_report(sc, &clocks_state);
		sc->sc_stage = "experimental domain reservation";
		error = sun60i_a733_pck_gpu_reserve(power_node, sc);
		if (error != 0)
			goto out;
		power_reserved = true;
		sc->sc_stage = "experimental clock reservation";
		error = sun60i_a733_ccu_gpu_reserve(gpu, sc);
		if (error != 0)
			goto out;
		clock_reserved = true;
		/* Last supply check before committing the boot-owned resources. */
		sc->sc_stage = "experimental supply recheck";
		error = fdtbus_regulator_is_enabled(supply, &enabled);
		if (error != 0)
			goto out;
		if (!enabled) {
			error = EBUSY;
			goto out;
		}
		error = fdtbus_regulator_get_voltage(supply, &sc->sc_uvol);
		if (error != 0)
			goto out;
		if (sc->sc_uvol != 800000) {
			error = EOPNOTSUPP;
			goto out;
		}
		sc->sc_stage = "experimental domain retention";
		error = sun60i_a733_pck_gpu_retain(power_node, sc);
		if (error != 0)
			goto out;
		sc->sc_retained = true;
		sc->sc_stage = "experimental 400 MHz preparation";
		error = sun60i_a733_ccu_gpu_prepare(gpu, sc, &sc->sc_retained);
		if (error == ETIMEDOUT)
			sc->sc_stage = "experimental clock UPDATE completion";
		if (error != 0)
			goto out;
	}
	sc->sc_stage = "clock/reset state";
	error = sun60i_a733_ccu_gpu_inspect(gpu, &clocks_state);
	if (error != 0)
		goto out;
	error = clocks_state.readiness_error;
	if (error != 0 || observe_only) {
		sun60i_gpu_clock_report(sc, &clocks_state);
		if (observe_only)
			aprint_normal_dev(sc->sc_dev, "observation only; "
			    "GPU registers not mapped or read\n");
		goto out;
	}
	sc->sc_core_hz = clocks_state.core_hz;
	sc->sc_bus_hz = clocks_state.bus_hz;
	/* These two BSP operating points use 800 mV across all listed bins. */
	sc->sc_stage = "firmware operating point";
	if (sc->sc_uvol != 800000 ||
	    (sc->sc_core_hz != 400000000 && sc->sc_core_hz != 600000000)) {
		error = EOPNOTSUPP;
		goto out;
	}

	if (prepare) {
		/* Completed UPDATE and strict CCU readiness precede CORE waiting. */
		sc->sc_stage = "experimental prepared clock check";
		if (sc->sc_core_hz != 400000000 || sc->sc_bus_hz != 200000000) {
			error = EBUSY;
			goto out;
		}
		if (request) {
			sc->sc_stage = "experimental domain request";
			error = sun60i_a733_pck_gpu_request_on(power_node, sc, false);
			if (error != 0)
				goto out;
		}
		sc->sc_stage = "experimental CORE ON/Q acceptance";
		error = sun60i_a733_pck_gpu_wait(power_node, sc, false);
		if (error != 0)
			goto out;
		/* No GPU mapping is permitted on a lost supply or domain check. */
		sc->sc_stage = "experimental final supply check";
		error = fdtbus_regulator_is_enabled(supply, &enabled);
		if (error != 0)
			goto out;
		if (!enabled) {
			error = EBUSY;
			goto out;
		}
		error = fdtbus_regulator_get_voltage(supply, &sc->sc_uvol);
		if (error != 0)
			goto out;
		if (sc->sc_uvol != 800000) {
			error = EOPNOTSUPP;
			goto out;
		}
		sc->sc_stage = "experimental final CORE check";
		error = sun60i_a733_pck_gpu_wait(power_node, sc, false);
		if (error != 0)
			goto out;
		sc->sc_stage = "experimental final clock check";
		error = sun60i_a733_ccu_gpu_ready(gpu,
		    &sc->sc_core_hz, &sc->sc_bus_hz);
		if (error != 0)
			goto out;
		if (sc->sc_core_hz != 400000000 || sc->sc_bus_hz != 200000000) {
			error = EBUSY;
			goto out;
		}
	}

	sc->sc_stage = "register mapping";
	error = bus_space_map(sc->sc_bst, addr, A733_GPU_ID_SIZE, 0, &bsh);
	if (error != 0)
		goto out;
	sc->sc_stage = "PBVNC read";
	error = bus_space_peek_8(sc->sc_bst, bsh, A733_GPU_PBVNC, &id);
	bus_space_unmap(sc->sc_bst, bsh, A733_GPU_ID_SIZE);
	if (error != 0) {
		error = EFAULT;
		goto out;
	}
	sc->sc_bvnc = id;
	sc->sc_have_id = true;
	sc->sc_stage = "PBVNC value";
	error = id == A733_GPU_EXPECTED ? 0 : ENODEV;
out:
	if (prepare && sc->sc_retained && error == ETIMEDOUT)
		sun60i_gpu_clock_terminal(sc, gpu);
	if (request && error != 0 && sc->sc_retained)
		sun60i_gpu_top_identify(sc, addr, supply, gpu, false);
	if (!sc->sc_retained) {
		if (clock_reserved)
			(void)sun60i_a733_ccu_gpu_release(gpu, sc);
		if (power_reserved)
			(void)sun60i_a733_pck_gpu_release(power_node, sc);
	}
	if (gpu != NULL)
		clk_put(gpu);
	fdtbus_regulator_release(supply);
	return error;
}

static int
sun60i_gpu_finalize(device_t dev)
{
	struct sun60i_gpu_softc * const sc = device_private(dev);
	int error;

	/* Other finalizers may request another pass; never probe twice. */
	if (sc->sc_probed)
		return 0;
	sc->sc_probed = true;
	error = sun60i_gpu_identify(sc);
	if (sc->sc_have_id) {
		aprint_normal_dev(dev, "PBVNC 0x%016llx: %u.%u.%u.%u%s\n",
		    (unsigned long long)sc->sc_bvnc,
		    (u_int)(sc->sc_bvnc >> 48),
		    (u_int)((sc->sc_bvnc >> 32) & 0xffff),
		    (u_int)((sc->sc_bvnc >> 16) & 0xffff),
		    (u_int)(sc->sc_bvnc & 0xffff),
		    sc->sc_bvnc == A733_GPU_EXPECTED ?
		    " (expected A733 GPU)" : " (unexpected)");
	}
	if (error != 0)
		aprint_error_dev(dev, "identification unavailable at %s: %d; %s\n",
		    sc->sc_stage, error, sc->sc_retained ?
		    "experimental resources retained until reboot" :
		    "firmware state left unchanged");
	else if (sc->sc_retained)
		aprint_normal_dev(dev,
		    "experimental resources retained until reboot\n");
	return 0;
}

static int
sun60i_gpu_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_gpu_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_gpu_softc * const sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	int error;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;
	mutex_init(&sc->sc_run_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->sc_run_pass = true;
	sun60i_gpu_instance = sc;
	sun60i_gpu_sysctl_setup(sc);
	aprint_naive("\n");
	aprint_normal(": firmware-ready GPU identification only\n");
	/* The FDT root scan can attach us after our /soc parent has returned. */
	error = config_finalize_register(self, sun60i_gpu_finalize);
	if (error != 0)
		aprint_error_dev(self, "cannot register identification hook: %d\n",
		    error);
}

CFATTACH_DECL_NEW(sun60i_a733_gpu, sizeof(struct sun60i_gpu_softc),
    sun60i_gpu_match, sun60i_gpu_attach, NULL, NULL);
