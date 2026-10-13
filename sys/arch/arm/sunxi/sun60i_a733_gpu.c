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
 * Origin: EmberBSD; A733 GPU identification and explicit bootstrap diagnostics.
 * Hardware facts: Orange Pi BSP 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f.
 * The explicit experimental binding may prepare GPU-local clocks and reset.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/sha2.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/time.h>

#include <dev/fdt/fdtvar.h>
#include <dev/firmload.h>
#include <arm/sunxi/sun60i_a733_gpu_fwinfo.h>
#include <arm/sunxi/sun60i_a733_gpu_fwmem.h>
#include <arm/sunxi/sun60i_a733_gpu_fwboot.h>
#include <arm/sunxi/sun60i_a733_gpu_fwprobe.h>
#include <arm/sunxi/sun60i_a733_gpu_fwcompute.h>
#include <arm/sunxi/sun60i_a733_gpu_fwcompute_file.h>
#include <arm/sunxi/sun60i_a733_gpu_fwheap.h>
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
#define A733_GPU_STAGE_FWINFO	12
#define A733_GPU_STAGE_FWMEM	13
#define A733_GPU_STAGE_FWINIT	14
#define A733_GPU_STAGE_FWBOOT	15
#define A733_GPU_STAGE_FWPROBE	16
#define A733_GPU_STAGE_FWCACHE	17
#define A733_GPU_STAGE_FWCOMPUTE	18
#define A733_GPU_STAGE_FWHEAP	19
#define A733_GPU_STAGE_FWVM	20
#define A733_GPU_STAGE_FWKICK	21
#define A733_GPU_FW_MAX_SIZE	(1024 * 1024)
#define A733_GPU_TRACE_CAPTURE_WORDS	256U
#define A733_GPU_COMPUTE_FW_NAME	"a733-compute.bin"

struct sun60i_gpu_softc {
	device_t sc_dev;
	int sc_phandle;
	bus_space_tag_t sc_bst;
	bus_dma_tag_t sc_dmat;
	struct sun60i_a733_gpu_fwmem *sc_fw_memory;
	struct sun60i_a733_gpu_fwboot_result sc_boot_result;
	struct sun60i_a733_gpu_fwprobe_result sc_probe_result;
	struct sun60i_a733_gpu_fwprobe_result sc_cache_result;
	const char *sc_stage;
	uint64_t sc_bvnc;
	u_int sc_core_hz, sc_bus_hz, sc_uvol;
	u_int sc_clock_source;	/* runtime: 0 PERIPH0/400, 1 PLL_GPU0/600 */
	char sc_clock_state[160];	/* last complete stage-7 observation */
	char sc_fw_state[256];	/* metadata only; no firmware execution */
	char sc_memory_state[256]; /* cached stage-13 preparation */
	char sc_interface_state[256]; /* cached stage-14 interface preparation */
	char sc_boot_state[512];	/* cached stage-15 bootstrap observation */
	char sc_probe_state[1024]; /* cached stage-16 command observation */
	char sc_cache_state[1024]; /* cached stage-17 MMUCACHE observation */
	char sc_compute_state[768]; /* cached stage-18 CPU serialization */
	char sc_heap_state[512];	/* cached stage-19 heap mapping */
	char sc_vm_state[512];		/* cached stage-20 VM preparation */
	char sc_kick_state[1024];	/* cached stage-21 submission */
	char sc_trace_state[4096];	/* cached stage-21 firmware trace */
	char sc_hwr_state[8192];	/* cached stage-21 firmware HWR records */
	struct sun60i_a733_gpu_fwheap *sc_fwheap;
	struct sun60i_a733_gpu_fwvm *sc_fwvm;
	struct sun60i_a733_gpu_fwprobe_result sc_heap_result, sc_kick_result;
	struct sun60i_a733_gpu_fwprobe_result sc_trace_result;
	bool sc_have_id, sc_probed, sc_retained, sc_compute_done;
	bool sc_heap_done, sc_vm_done, sc_kick_done;
	kmutex_t sc_run_lock;
	u_int sc_run_stage;	/* last completed stage */
	u_int sc_run_result;	/* errno of the last attempt */
	bool sc_run_pass;	/* sequence may continue */
	bool sc_run_core_on;	/* stage 9 confirmed CORE ON */
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
	    (core_hz != 400000000 &&
	    !(quiet && sc->sc_clock_source == 1 && core_hz == 600000000)) ||
	    bus_hz != 200000000) {
		if (!quiet) aprint_normal_dev(sc->sc_dev, "TOP-only identification "
		    "skipped: supply, GPU_TOP or clock readiness changed\n");
		return;
	}
	if (bus_space_map(sc->sc_bst, addr, A733_GPU_ID_SIZE, 0, &bsh) != 0) {
		if (!quiet) aprint_normal_dev(sc->sc_dev, "TOP-only identification "
		    "unavailable: mapping\n");
		return;
	}
	if (bus_space_peek_4(sc->sc_bst, bsh, A733_GPU_CORE_ID, &core) != 0) {
		if (!quiet)
			aprint_normal_dev(sc->sc_dev, "TOP-only CORE_ID read failed\n");
		bus_space_unmap(sc->sc_bst, bsh, A733_GPU_ID_SIZE);
		return;
	}
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

/* Read and validate file metadata. Never transfer firmware to the GPU. */
static int
sun60i_gpu_stage_fwinfo(struct sun60i_gpu_softc *sc)
{
	struct sun60i_a733_gpu_fwinfo info;
	firmware_handle_t fh = NULL;
	void *bytes = NULL;
	off_t file_size;
	size_t size = 0;
	int error, close_error;

	if (sc->sc_run_stage != A733_GPU_STAGE_IDENTIFY ||
	    sc->sc_run_result != 0 || !sc->sc_run_pass ||
	    !sc->sc_have_id || sc->sc_bvnc != A733_GPU_EXPECTED) {
		error = EBUSY;
		goto out;
	}
	error = firmware_open("powervr", "rogue_36.56.104.183_v1.fw", &fh);
	if (error != 0)
		goto out;
	file_size = firmware_get_size(fh);
	if (file_size < A733_GPU_FW_BLOCK_SIZE ||
	    file_size > A733_GPU_FW_MAX_SIZE) {
		error = EINVAL;
		goto out;
	}
	size = (size_t)file_size;
	bytes = firmware_malloc(size);
	if (bytes == NULL) {
		error = ENOMEM;
		goto out;
	}
	error = firmware_read(fh, 0, bytes, size);
	if (error == 0)
		error = sun60i_a733_gpu_fwinfo_parse(bytes, size, sc->sc_bvnc,
		    &info);
out:
	if (fh != NULL) {
		close_error = firmware_close(fh);
		if (error == 0)
			error = close_error;
	}
	if (bytes != NULL)
		firmware_free(bytes, size);
	/* Publish only the final result, after parsing and all cleanup. */
	if (error != 0) {
		snprintf(sc->sc_fw_state, sizeof(sc->sc_fw_state),
		    "metadata only error=%d", error);
	} else {
		snprintf(sc->sc_fw_state, sizeof(sc->sc_fw_state),
		    "metadata only error=0 version=%u.%u.%u "
		    "bvnc=0x%016llx layouts=%u code=%u data=%u payload=%zu",
		    (u_int)info.version_major, (u_int)info.version_minor,
		    info.version_build, (unsigned long long)info.bvnc,
		    info.layout_count, info.allocation_size[A733_GPU_FW_CODE],
		    info.allocation_size[A733_GPU_FW_DATA], info.payload_size);
	}
	return error;
}

/* Prepare native DMA allocations, but keep every address private to the CPU. */
static int
sun60i_gpu_stage_fwmem(struct sun60i_gpu_softc *sc)
{
	struct sun60i_a733_gpu_fwmem *mem = NULL;
	firmware_handle_t fh = NULL;
	void *bytes = NULL;
	off_t file_size;
	size_t size = 0;
	int error, close_error;

	if (sc->sc_run_stage != A733_GPU_STAGE_FWINFO ||
	    sc->sc_run_result != 0 || !sc->sc_run_pass ||
	    !sc->sc_have_id || sc->sc_bvnc != A733_GPU_EXPECTED ||
	    sc->sc_fw_memory != NULL) {
		error = EBUSY;
		goto out;
	}
	error = firmware_open("powervr", "rogue_36.56.104.183_v1.fw", &fh);
	if (error != 0)
		goto out;
	file_size = firmware_get_size(fh);
	if (file_size < A733_GPU_FW_BLOCK_SIZE ||
	    file_size > A733_GPU_FW_MAX_SIZE) {
		error = EINVAL;
		goto out;
	}
	size = (size_t)file_size;
	bytes = firmware_malloc(size);
	if (bytes == NULL) {
		error = ENOMEM;
		goto out;
	}
	error = firmware_read(fh, 0, bytes, size);
	if (error == 0)
		error = sun60i_a733_gpu_fwmem_prepare(sc->sc_dmat, bytes, size,
		    sc->sc_bvnc, &mem);
out:
	if (fh != NULL) {
		close_error = firmware_close(fh);
		if (error == 0)
			error = close_error;
	}
	if (bytes != NULL)
		firmware_free(bytes, size);
	if (error != 0) {
		sun60i_a733_gpu_fwmem_destroy(mem);
		snprintf(sc->sc_memory_state, sizeof(sc->sc_memory_state),
		    "CPU-owned memory only error=%d", error);
	} else {
		sc->sc_fw_memory = mem;
		snprintf(sc->sc_memory_state, sizeof(sc->sc_memory_state),
		    "CPU-owned memory only error=0 code=%zu data=%zu table=%zu "
		    "loads=%u entry=0x%08x boot=0x%llx stack=0x%llx",
		    mem->code.size, mem->data.size, mem->table.size,
		    mem->image.load_count, mem->image.entry,
		    (unsigned long long)mem->boot_code,
		    (unsigned long long)mem->stack);
	}
	return error;
}

/* Prepare the complete firmware interface without publishing any DMA address. */
static int
sun60i_gpu_stage_fwinit(struct sun60i_gpu_softc *sc)
{
	struct sun60i_a733_gpu_fwmem *mem = sc->sc_fw_memory;
	uint64_t clusters, slc_kib;
	int error;

	if (sc->sc_run_stage != A733_GPU_STAGE_FWMEM ||
	    sc->sc_run_result != 0 || !sc->sc_run_pass ||
	    !sc->sc_have_id || sc->sc_bvnc != A733_GPU_EXPECTED ||
	    mem == NULL || mem->image.info.bvnc != sc->sc_bvnc) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_a733_gpu_fwinfo_feature(&mem->image.info,
	    A733_GPU_FW_FEATURE_NUM_CLUSTERS, &clusters);
	if (error != 0)
		goto out;
	error = sun60i_a733_gpu_fwinfo_feature(&mem->image.info,
	    A733_GPU_FW_FEATURE_SLC_SIZE_IN_KILOBYTES, &slc_kib);
	if (error != 0)
		goto out;
	if (clusters == 0 || clusters > UINT32_MAX ||
	    slc_kib == 0 || slc_kib > UINT32_MAX) {
		error = EOPNOTSUPP;
		goto out;
	}
	error = sun60i_a733_gpu_fwmem_interface(mem, sc->sc_core_hz,
	    (uint32_t)clusters, (uint32_t)slc_kib);
out:
	if (error != 0) {
		snprintf(sc->sc_interface_state, sizeof(sc->sc_interface_state),
		    "CPU-owned interface only error=%d", error);
	} else {
		snprintf(sc->sc_interface_state, sizeof(sc->sc_interface_state),
		    "CPU-owned interface only error=0 work=%zu config=%zu "
		    "objects=%u core_hz=%u clusters=%u slc_kib=%u",
		    mem->work.size, mem->config.size,
		    (u_int)A733_GPU_FWINIT_OBJECT_COUNT, sc->sc_core_hz,
		    (u_int)clusters, (u_int)slc_kib);
	}
	return error;
}

/* Revalidate providers before any bootstrap register access. */
static int
sun60i_gpu_boot_ready(struct sun60i_gpu_softc *sc, int power_node)
{
	struct clk *gpu;
	u_int core_hz, bus_hz;
	int error;

	error = sun60i_gpu_runtime_supply_top(sc);
	if (error != 0)
		return error;
	error = sun60i_a733_pck_gpu_wait(power_node, sc, true);
	if (error != 0)
		return error;
	gpu = fdtbus_clock_get_index(sc->sc_phandle, 0);
	if (gpu == NULL)
		return ENXIO;
	error = sun60i_a733_ccu_gpu_ready(gpu, &core_hz, &bus_hz);
	clk_put(gpu);
	if (error != 0)
		return error;
	if (core_hz != sc->sc_core_hz || bus_hz != sc->sc_bus_hz)
		return EBUSY;
	return sun60i_gpu_stage_identify(sc);
}

static int
sun60i_gpu_stage_fwboot(struct sun60i_gpu_softc *sc, int power_node)
{
	struct sun60i_a733_gpu_fwboot_result *r = &sc->sc_boot_result;
	struct sun60i_a733_gpu_fwmem *mem = sc->sc_fw_memory;
	bus_space_handle_t bsh;
	bus_addr_t addr;
	bus_size_t size;
	int error;

	if (sc->sc_run_stage != A733_GPU_STAGE_FWINIT ||
	    sc->sc_run_result != 0 || !sc->sc_run_pass || !sc->sc_run_core_on ||
	    !sc->sc_have_id || sc->sc_bvnc != A733_GPU_EXPECTED ||
	    mem == NULL || mem->image.info.bvnc != sc->sc_bvnc ||
	    !mem->interface_prepared || r->attempted || r->device_owned) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_gpu_boot_ready(sc, power_node);
	if (error != 0)
		goto out;
	error = fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size);
	if (error != 0 || addr != A733_GPU_BASE ||
	    size < A733_GPU_FWSTART_REG_SIZE) {
		error = EINVAL;
		goto out;
	}
	error = bus_space_map(sc->sc_bst, addr, A733_GPU_FWSTART_REG_SIZE,
	    0, &bsh);
	if (error != 0)
		goto out;
	error = sun60i_a733_gpu_fwboot(sc->sc_bst, bsh, mem, r);
	/* This is only the CPU register mapping, never a DMA allocation. */
	bus_space_unmap(sc->sc_bst, bsh, A733_GPU_FWSTART_REG_SIZE);
out:
	if (r->device_owned) {
		/* Do not leave the earlier CPU-ownership reports visible as current. */
		snprintf(sc->sc_memory_state, sizeof(sc->sc_memory_state),
		    "device-owned; retained until hardware reset; see boot_state");
		snprintf(sc->sc_interface_state, sizeof(sc->sc_interface_state),
		    "device-owned; retained until hardware reset; see boot_state");
	}
	snprintf(sc->sc_boot_state, sizeof(sc->sc_boot_state),
	    "error=%d attempted=%u device_owned=%u touched=%u released=%u "
	    "register=0x%x writes=%u polls=%u snapshot=%u started=%u "
	    "timestamp=%u fw_state=%u os_state=%u alive_fw=%u alive_os=%u "
	    "compchecks=%u hw_bvnc=0x%016llx fw_bvnc=0x%016llx ddk_build=%u",
	    error, r->attempted, r->device_owned, r->start.touched,
	    r->start.released, r->start.last_register, r->start.writes,
	    r->polls, r->snapshot_valid, (u_int)r->firmware_started,
	    r->firmware_started_timestamp, r->fw_state, r->os_state,
	    r->alive_fw_token, r->alive_os_token, (u_int)r->compchecks_updated,
	    (unsigned long long)r->hw_bvnc, (unsigned long long)r->fw_bvnc,
	    r->ddk_build);
	return error;
}

static void
sun60i_gpu_probe_report(char *state, size_t size,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *r, int error)
{
	snprintf(state, size,
	    "error=%d attempted=%u published=%u kick_attempted=%u kicked=%u "
	    "device_owned=%u slot=%u polls=%u before_read=%u before_write=%u "
	    "after_read=%u after_write=%u rtn=0x%x fw_read=%u fw_write=%u "
	    "fw_command=0x%x executed_before=%u executed_after=%u "
	    "power=%u hwr=0x%x faults=%u log=0x%x started=%u "
	    "fw_state=%u os_state=%u alive_fw=%u alive_os=%u "
	    "compchecks=%u hw_bvnc=0x%016llx fw_bvnc=0x%016llx ddk_build=%u "
	    "command=0x%x cache_flags=0x%x sync_fw=0x%x sync_value=%u mmucache_sync=%u",
	    error, r->attempted, r->published, r->kick_attempted, r->kicked,
	    boot->device_owned, r->slot, r->polls,
	    r->before.read_offset, r->before.write_offset,
	    r->after.read_offset, r->after.write_offset, r->after.rtn,
	    r->after.fw_read_offset, r->after.fw_write_offset, r->after.fw_command,
	    r->before.cmds_executed, r->after.cmds_executed,
	    r->after.pow_state, r->after.hwr_state, r->after.fw_faults,
	    r->after.log_type, (u_int)r->after.firmware_started,
	    r->after.fw_state, r->after.os_state,
	    r->after.alive_fw_token, r->after.alive_os_token,
	    (u_int)r->after.compchecks_updated,
	    (unsigned long long)r->after.hw_bvnc,
	    (unsigned long long)r->after.fw_bvnc, r->after.ddk_build,
	    r->command, r->cache_flags, r->sync_fw_addr, r->sync_update_value,
	    r->after.mmucache_sync);
}

static int
sun60i_gpu_stage_fwprobe(struct sun60i_gpu_softc *sc, int power_node)
{
	const struct sun60i_a733_gpu_fwboot_result *boot = &sc->sc_boot_result;
	struct sun60i_a733_gpu_fwprobe_result *r = &sc->sc_probe_result;
	struct sun60i_a733_gpu_fwmem *mem = sc->sc_fw_memory;
	bus_space_handle_t bsh;
	bus_addr_t addr;
	bus_size_t size;
	int error;

	if (sc->sc_run_stage != A733_GPU_STAGE_FWBOOT ||
	    sc->sc_run_result != 0 || !sc->sc_run_pass || !sc->sc_run_core_on ||
	    !sc->sc_have_id || sc->sc_bvnc != A733_GPU_EXPECTED ||
	    mem == NULL || mem->image.info.bvnc != sc->sc_bvnc ||
	    !mem->interface_prepared || !boot->attempted || !boot->device_owned ||
	    boot->error != 0 || !boot->start.touched || !boot->start.released ||
	    !boot->snapshot_valid || boot->firmware_started != 1 ||
	    r->attempted || r->published || r->kick_attempted || r->kicked) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_gpu_boot_ready(sc, power_node);
	if (error != 0)
		goto out;
	error = fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size);
	if (error != 0 || addr != A733_GPU_BASE ||
	    size < A733_GPU_FWSTART_REG_SIZE) {
		error = EINVAL;
		goto out;
	}
	error = bus_space_map(sc->sc_bst, addr, A733_GPU_FWSTART_REG_SIZE,
	    0, &bsh);
	if (error != 0)
		goto out;
	error = sun60i_a733_gpu_fwprobe(sc->sc_bst, bsh, mem, boot, r);
	/* Release only the CPU register mapping. All DMA memory stays owned. */
	bus_space_unmap(sc->sc_bst, bsh, A733_GPU_FWSTART_REG_SIZE);
out:
	sun60i_gpu_probe_report(sc->sc_probe_state, sizeof(sc->sc_probe_state),
	    boot, r, error);
	return error;
}

static int
sun60i_gpu_stage_fwcache(struct sun60i_gpu_softc *sc, int power_node)
{
	const struct sun60i_a733_gpu_fwboot_result *boot = &sc->sc_boot_result;
	struct sun60i_a733_gpu_fwprobe_result *r = &sc->sc_cache_result;
	const struct sun60i_a733_gpu_fwprobe_result *probe = &sc->sc_probe_result;
	struct sun60i_a733_gpu_fwmem *mem = sc->sc_fw_memory;
	bus_space_handle_t bsh;
	bus_addr_t addr;
	bus_size_t size;
	int error;

	if (sc->sc_run_stage != A733_GPU_STAGE_FWPROBE ||
	    sc->sc_run_result != 0 || !sc->sc_run_pass || !sc->sc_run_core_on ||
	    !sc->sc_have_id || sc->sc_bvnc != A733_GPU_EXPECTED ||
	    mem == NULL || mem->image.info.bvnc != sc->sc_bvnc ||
	    !mem->interface_prepared || !boot->attempted || !boot->device_owned ||
	    boot->error != 0 || !boot->start.touched || !boot->start.released ||
	    !boot->snapshot_valid || boot->firmware_started != 1 ||
	    !probe->attempted || !probe->published || !probe->kick_attempted ||
	    !probe->kicked || probe->error != 0 ||
	    probe->command != A733_GPU_FWPROBE_LOGTYPE || probe->slot != 0 ||
	    probe->polls < 2 || probe->polls > A733_GPU_FWPROBE_MAX_POLLS ||
	    probe->before.read_offset != 0 || probe->before.write_offset != 0 ||
	    probe->after.read_offset != 1 || probe->after.write_offset != 1 ||
	    probe->after.rtn != 1 || probe->cache_flags != 0 ||
	    probe->sync_fw_addr != 0 || probe->sync_update_value != 0 ||
	    r->attempted || r->published || r->kick_attempted || r->kicked) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_gpu_boot_ready(sc, power_node);
	if (error != 0)
		goto out;
	error = fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size);
	if (error != 0 || addr != A733_GPU_BASE ||
	    size < A733_GPU_FWSTART_REG_SIZE) {
		error = EINVAL;
		goto out;
	}
	error = bus_space_map(sc->sc_bst, addr, A733_GPU_FWSTART_REG_SIZE,
	    0, &bsh);
	if (error != 0)
		goto out;
	error = sun60i_a733_gpu_fwprobe_mmucache(sc->sc_bst, bsh, mem, boot,
	    probe, r);
	/* Release only the CPU register mapping. All DMA memory stays owned. */
	bus_space_unmap(sc->sc_bst, bsh, A733_GPU_FWSTART_REG_SIZE);
out:
	sun60i_gpu_probe_report(sc->sc_cache_state, sizeof(sc->sc_cache_state),
	    boot, r, error);
	return error;
}

/*
 * Stage 18: CPU-only compute serialization of the packed compiler image.
 * Loads the firmware-file image, validates it, fills the serializer inputs
 * with the real USC/PDS/CDM spans, and digests the complete 33 KiB image.
 * No firmware-heap allocation, page-table mapping, DMA, MMIO, publication,
 * or GPU execution happens here.
 */
static int
sun60i_gpu_stage_fwcompute(struct sun60i_gpu_softc *sc)
{
	const struct sun60i_a733_gpu_fwboot_result *boot = &sc->sc_boot_result;
	const struct sun60i_a733_gpu_fwprobe_result *probe = &sc->sc_probe_result;
	const struct sun60i_a733_gpu_fwprobe_result *cache = &sc->sc_cache_result;
	struct sun60i_a733_gpu_fwmem *mem = sc->sc_fw_memory;
	struct sun60i_a733_gpu_fwcompute_inputs inputs;
	struct a733_gpu_fwcompute_file file;
	const struct a733_gpu_fwcompute_file_entry *role[3];
	struct sun60i_a733_gpu_fwcompute_image *image = NULL;
	firmware_handle_t fh = NULL;
	uint8_t *bytes = NULL;
	SHA256_CTX ctx;
	uint8_t digest[SHA256_DIGEST_LENGTH];
	char hex[2 * SHA256_DIGEST_LENGTH + 1];
	off_t file_size;
	size_t size = 0, i;
	int error, close_error;

	if (sc->sc_run_stage != A733_GPU_STAGE_FWCACHE ||
	    sc->sc_run_result != 0 || !sc->sc_run_pass || !sc->sc_run_core_on ||
	    !sc->sc_have_id || sc->sc_bvnc != A733_GPU_EXPECTED ||
	    mem == NULL || mem->image.info.bvnc != sc->sc_bvnc ||
	    !mem->interface_prepared || !boot->attempted || !boot->device_owned ||
	    boot->error != 0 || !boot->start.touched || !boot->start.released ||
	    !boot->snapshot_valid || boot->firmware_started != 1 ||
	    !probe->attempted || !probe->published || !probe->kick_attempted ||
	    !probe->kicked || probe->error != 0 ||
	    !cache->attempted || !cache->published || !cache->kick_attempted ||
	    !cache->kicked || cache->error != 0 ||
	    cache->command != A733_GPU_FWPROBE_MMUCACHE || cache->slot != 1 ||
	    cache->cache_flags != 0x0400001f ||
	    cache->sync_fw_addr == 0 || cache->sync_update_value != 0 ||
	    cache->before.read_offset != 1 || cache->before.write_offset != 1 ||
	    cache->after.read_offset != 2 || cache->after.write_offset != 2 ||
	    cache->after.rtn != 1 ||
	    sc->sc_compute_done) {
		error = EBUSY;
		goto out;
	}
	/* The declared span window must stay clear of every live mapping. */
	if (UINT64_C(0xc0000000) + mem->code.size >
	    A733_GPU_FWCOMPUTE_SPAN_BASE ||
	    (uint64_t)A733_GPU_FWINIT_WORK_VA + mem->work.size >
	    A733_GPU_FWCOMPUTE_SPAN_BASE ||
	    (uint64_t)A733_GPU_FWCOMPUTE_SPAN_BASE +
	    A733_GPU_FWCOMPUTE_SPAN_BYTES > A733_GPU_FWINIT_CONNECTION_VA) {
		error = EBUSY;
		goto out;
	}
	error = firmware_open("powervr", A733_GPU_COMPUTE_FW_NAME, &fh);
	if (error != 0)
		goto out;
	file_size = firmware_get_size(fh);
	if (file_size < A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY +
	    A733_GPU_FWCOMPUTE_STATE_BYTES ||
	    file_size > A733_GPU_FWCOMPUTE_FILE_MAX) {
		error = EINVAL;
		goto out;
	}
	size = (size_t)file_size;
	bytes = kmem_alloc(size, KM_SLEEP);
	error = firmware_read(fh, 0, bytes, size);
	if (error != 0)
		goto out;
	error = sun60i_a733_gpu_fwcompute_file_parse(bytes, size, sc->sc_bvnc,
	    &file);
	if (error != 0)
		goto out;
	/* Integrity and profile guards before any compiler bytes are used. */
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, bytes + file.header_bytes, file.payload_bytes);
	SHA256_Final(digest, &ctx);
	if (memcmp(digest, file.payload_digest, sizeof(digest)) != 0) {
		error = EAUTH;
		goto out;
	}
	if (mem->image.info.feature_mask[0] !=
	    UINT64_C(0x92d9ff57fdbf7cdb) ||
	    mem->image.info.feature_mask[1] != UINT64_C(0x0000001f) ||
	    mem->image.info.brn_mask != UINT64_C(0x0000201)) {
		error = EOPNOTSUPP;
		goto out;
	}
	{
		static const uint16_t program_role[3] = {
			A733_GPU_FWCOMPUTE_USC_PROGRAMS,
			A733_GPU_FWCOMPUTE_PDS_PROGRAMS,
			A733_GPU_FWCOMPUTE_CDM_STREAM
		};
		size_t j, k;

		for (j = 0; j < 3; j++) {
			for (k = 0; k < A733_GPU_FWCOMPUTE_RESOURCES; k++) {
				if (file.resource[k].role == program_role[j])
					break;
			}
			if (k == A733_GPU_FWCOMPUTE_RESOURCES) {
				error = EINVAL;
				goto out;
			}
			role[j] = &file.resource[k];
		}
	}
	image = kmem_alloc(sizeof(*image), KM_SLEEP);
	memset(&inputs, 0, sizeof(inputs));
	sun60i_a733_gpu_fwcompute_file_fwspans(inputs.fw);
	for (i = 0; i < 3; i++) {
		inputs.program[i].bytes = bytes + file.header_bytes +
		    role[i]->offset;
		inputs.program[i].size = role[i]->bytes;
		inputs.program[i].gpu_va = role[i]->va;
	}
	memcpy(&inputs.static_state, bytes + file.header_bytes +
	    file.static_offset, A733_GPU_FWCOMPUTE_STATE_BYTES);
	sun60i_a733_gpu_fwcompute_file_relocate(
	    (uint8_t *)&inputs.static_state, UINT64_C(0x10000000));

	/* The kernel owns the typed command, as in the upstream submission. */
	inputs.command = file.command;
	inputs.priority = file.priority;
	inputs.pid = file.pid;
	inputs.context_id = file.context_id;
	inputs.job_id = file.job_id;
	error = sun60i_a733_gpu_fwcompute_prepare(&inputs, image, sizeof(*image));
	if (error != 0)
		goto out;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, (const uint8_t *)image, sizeof(*image));
	SHA256_Final(digest, &ctx);
	for (i = 0; i < sizeof(digest); i++)
		snprintf(hex + 2 * i, 3, "%02x", digest[i]);
out:
	sc->sc_compute_done = true;
	if (error != 0) {
		snprintf(sc->sc_compute_state, sizeof(sc->sc_compute_state),
		    "cpu-only serialization error=%d; no mapping, no publication",
		    error);
	} else {
		snprintf(sc->sc_compute_state, sizeof(sc->sc_compute_state),
		    "cpu-only serialization error=0 used=%u kick_context=0x%08x "
		    "wrap=%u usc=0x%llx+%u pds=0x%llx+%u stream=0x%llx+%u "
		    "ctx_state=0x%llx spans=0x%08x..0x%08x payload=%u "
		    "digest=%s verified=1 declared-unmapped no-publication",
		    (u_int)image->used_bytes,
		    (u_int)image->kick.cmd_data.cmd_kick_data.context_fw_addr,
		    (u_int)image->ctl.wrap_mask,
		    (unsigned long long)role[0]->va, (u_int)role[0]->bytes,
		    (unsigned long long)role[1]->va, (u_int)role[1]->bytes,
		    (unsigned long long)role[2]->va, (u_int)role[2]->bytes,
		    (unsigned long long)file.context_state_va,
		    (u_int)A733_GPU_FWCOMPUTE_SPAN_BASE,
		    (u_int)(A733_GPU_FWCOMPUTE_SPAN_BASE +
		    A733_GPU_FWCOMPUTE_SPAN_BYTES), (u_int)file.payload_bytes,
		    hex);
	}
	if (fh != NULL) {
		close_error = firmware_close(fh);
		if (error == 0)
			error = close_error;
	}
	if (bytes != NULL)
		kmem_free(bytes, size);
	if (image != NULL)
		kmem_free(image, sizeof(*image));
	return error;
}

/* Gate shared by the heap, VM, and kick stages after stage 18. */

/*
 * Shared loader for the later compute stages: read the packed file,
 * verify its payload digest and the pinned firmware profile, serialize
 * the image, and hand the caller both the file view and the bytes.
 * The caller frees *image and *bytes and closes nothing else.
 */
static int
sun60i_gpu_compute_load(struct sun60i_gpu_softc *sc,
    struct a733_gpu_fwcompute_file *file, uint8_t **bytes_out,
    struct sun60i_a733_gpu_fwcompute_image **image_out)
{
	struct sun60i_a733_gpu_fwmem *mem = sc->sc_fw_memory;
	struct sun60i_a733_gpu_fwcompute_inputs inputs;
	struct sun60i_a733_gpu_fwcompute_image *image;
	const struct a733_gpu_fwcompute_file_entry *role[3];
	firmware_handle_t fh = NULL;
	uint8_t *bytes = NULL;
	SHA256_CTX ctx;
	uint8_t digest[SHA256_DIGEST_LENGTH];
	off_t file_size;
	size_t size = 0, i, j, k;
	int error;

	*bytes_out = NULL;
	*image_out = NULL;
	error = firmware_open("powervr", A733_GPU_COMPUTE_FW_NAME, &fh);
	if (error != 0)
		return error;
	file_size = firmware_get_size(fh);
	if (file_size < A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY ||
	    file_size > A733_GPU_FWCOMPUTE_FILE_MAX) {
		error = EINVAL;
		goto out;
	}
	size = (size_t)file_size;
	bytes = kmem_alloc(size, KM_SLEEP);
	error = firmware_read(fh, 0, bytes, size);
	if (error != 0)
		goto out;
	error = sun60i_a733_gpu_fwcompute_file_parse(bytes, size, sc->sc_bvnc,
	    file);
	if (error != 0)
		goto out;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, bytes + file->header_bytes, file->payload_bytes);
	SHA256_Final(digest, &ctx);
	if (memcmp(digest, file->payload_digest, sizeof(digest)) != 0) {
		error = EAUTH;
		goto out;
	}
	if (mem->image.info.feature_mask[0] != UINT64_C(0x92d9ff57fdbf7cdb) ||
	    mem->image.info.feature_mask[1] != UINT64_C(0x0000001f) ||
	    mem->image.info.brn_mask != UINT64_C(0x0000201)) {
		error = EOPNOTSUPP;
		goto out;
	}
	{
		static const uint16_t program_role[3] = {
			A733_GPU_FWCOMPUTE_USC_PROGRAMS,
			A733_GPU_FWCOMPUTE_PDS_PROGRAMS,
			A733_GPU_FWCOMPUTE_CDM_STREAM
		};

		for (j = 0; j < 3; j++) {
			for (k = 0; k < A733_GPU_FWCOMPUTE_RESOURCES; k++) {
				if (file->resource[k].role == program_role[j])
					break;
			}
			if (k == A733_GPU_FWCOMPUTE_RESOURCES) {
				error = EINVAL;
				goto out;
			}
			role[j] = &file->resource[k];
		}
	}
	image = kmem_alloc(sizeof(*image), KM_SLEEP);
	memset(&inputs, 0, sizeof(inputs));
	sun60i_a733_gpu_fwcompute_file_fwspans(inputs.fw);
	for (i = 0; i < 3; i++) {
		inputs.program[i].bytes = bytes + file->header_bytes +
		    role[i]->offset;
		inputs.program[i].size = role[i]->bytes;
		inputs.program[i].gpu_va = role[i]->va;
	}
	memcpy(&inputs.static_state, bytes + file->header_bytes +
	    file->static_offset, A733_GPU_FWCOMPUTE_STATE_BYTES);
	sun60i_a733_gpu_fwcompute_file_relocate(
	    (uint8_t *)&inputs.static_state, UINT64_C(0x10000000));

	inputs.command = file->command;
	inputs.priority = file->priority;
	inputs.pid = file->pid;
	inputs.context_id = file->context_id;
	inputs.job_id = file->job_id;
	error = sun60i_a733_gpu_fwcompute_prepare(&inputs, image,
	    sizeof(*image));
	if (error != 0) {
		kmem_free(image, sizeof(*image));
		goto out;
	}
	*bytes_out = bytes;
	*image_out = image;
out:
	if (fh != NULL) {
		int close_error = firmware_close(fh);

		if (error == 0)
			error = close_error;
	}
	if (*image_out == NULL && bytes != NULL)
		kmem_free(bytes, size);
	return error;
}

static bool sun60i_gpu_compute_chain_ready(const struct sun60i_gpu_softc *,
    const struct sun60i_a733_gpu_fwboot_result *,
    const struct sun60i_a733_gpu_fwprobe_result *,
    const struct sun60i_a733_gpu_fwprobe_result *);

/*
 * Stage 19: place the serialized objects in a real coherent firmware-heap
 * buffer, map its pages into the live wrapper table, and re-sync the
 * firmware MMU caches through a second MMUCACHE command in slot 2.
 */
static int
sun60i_gpu_stage_fwheap(struct sun60i_gpu_softc *sc, int power_node)
{
	const struct sun60i_a733_gpu_fwboot_result *boot = &sc->sc_boot_result;
	const struct sun60i_a733_gpu_fwprobe_result *probe = &sc->sc_probe_result;
	const struct sun60i_a733_gpu_fwprobe_result *cache = &sc->sc_cache_result;
	struct sun60i_a733_gpu_fwprobe_result *r = &sc->sc_heap_result;
	struct rogue_fwif_kccb_cmd kcmd;
	struct a733_gpu_fwcompute_file file;
	struct sun60i_a733_gpu_fwcompute_image *image = NULL;
	uint8_t *bytes = NULL;
	bus_space_handle_t bsh;
	bus_addr_t addr;
	bus_size_t size;
	uint32_t heap_pa;
	int error;

	if (sc->sc_run_stage != A733_GPU_STAGE_FWCOMPUTE ||
	    !sun60i_gpu_compute_chain_ready(sc, boot, probe, cache) ||
	    !sc->sc_compute_done || sc->sc_fwheap != NULL || r->attempted) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_gpu_compute_load(sc, &file, &bytes, &image);
	if (error != 0)
		goto out;
	error = sun60i_a733_gpu_fwheap_prepare(sc->sc_dmat, sc->sc_fw_memory,
	    image, &sc->sc_fwheap);
	if (error != 0)
		goto out;
	error = sun60i_a733_gpu_fwheap_map(sc->sc_fwheap, sc->sc_fw_memory);
	if (error != 0)
		goto out;
	/* The mapping becomes visible to the firmware only after this sync. */
	error = sun60i_gpu_boot_ready(sc, power_node);
	if (error != 0)
		goto out;
	error = fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size);
	if (error != 0 || addr != A733_GPU_BASE ||
	    size < A733_GPU_FWSTART_REG_SIZE) {
		error = EINVAL;
		goto out;
	}
	error = bus_space_map(sc->sc_bst, addr, A733_GPU_FWSTART_REG_SIZE,
	    0, &bsh);
	if (error != 0)
		goto out;
	memset(&kcmd, 0, sizeof(kcmd));
	kcmd.cmd_type = ROGUE_FWIF_KCCB_CMD_MMUCACHE;
	kcmd.cmd_data.mmu_cache_data.cache_flags = 0x0400001f;
	kcmd.cmd_data.mmu_cache_data.mmu_cache_sync_fw_addr =
	    sc->sc_fw_memory->interface_layout.object[
	    A733_GPU_FWINIT_MMUCACHE_SYNC].fw_addr;
	kcmd.cmd_data.mmu_cache_data.mmu_cache_sync_update_value = 0;
	error = sun60i_a733_gpu_fwprobe_cmd(sc->sc_bst, bsh, sc->sc_fw_memory,
	    boot, cache, &kcmd, 2, 0, r);
	bus_space_unmap(sc->sc_bst, bsh, A733_GPU_FWSTART_REG_SIZE);
out:
	sc->sc_heap_done = true;
	heap_pa = sc->sc_fwheap != NULL ?
	    (uint32_t)sc->sc_fwheap->heap.address : 0;
	if (error != 0) {
		snprintf(sc->sc_heap_state, sizeof(sc->sc_heap_state),
		    "heap error=%d mapped=%u sync_rtn=0x%x", error,
		    (u_int)(sc->sc_fwheap != NULL && sc->sc_fwheap->mapped),
		    (u_int)r->after.rtn);
	} else {
		snprintf(sc->sc_heap_state, sizeof(sc->sc_heap_state),
		    "heap error=0 fw=0x%08x pa=0x%08x pages=%u mapped=1 "
		    "sync=slot%u rtn=0x%x polls=%u kicked=%u",
		    (u_int)A733_GPU_FWCOMPUTE_SPAN_BASE, heap_pa,
		    (u_int)A733_GPU_FWHEAP_PAGES, (u_int)r->slot,
		    (u_int)r->after.rtn, (u_int)r->polls, (u_int)r->kicked);
	}
	if (image != NULL)
		kmem_free(image, sizeof(*image));
	if (bytes != NULL)
		kmem_free(bytes, file.payload_bytes + file.header_bytes);
	return error;
}

/* Stage 20: back all ten resources, build the VM tables, set the context. */
static int
sun60i_gpu_stage_fwvm(struct sun60i_gpu_softc *sc)
{
	const struct sun60i_a733_gpu_fwboot_result *boot = &sc->sc_boot_result;
	const struct sun60i_a733_gpu_fwprobe_result *probe = &sc->sc_probe_result;
	const struct sun60i_a733_gpu_fwprobe_result *cache = &sc->sc_cache_result;
	struct a733_gpu_fwcompute_file file;
	struct sun60i_a733_gpu_fwcompute_image *image = NULL;
	uint8_t *bytes = NULL;
	int error;

	if (sc->sc_run_stage != A733_GPU_STAGE_FWHEAP ||
	    !sun60i_gpu_compute_chain_ready(sc, boot, probe, cache) ||
	    !sc->sc_heap_done || sc->sc_run_result != 0 ||
	    sc->sc_fwheap == NULL || !sc->sc_fwheap->mapped ||
	    sc->sc_fwvm != NULL) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_gpu_compute_load(sc, &file, &bytes, &image);
	if (error != 0)
		goto out;
	error = sun60i_a733_gpu_fwvm_prepare(sc->sc_dmat, &file, bytes,
	    sc->sc_fwheap, &sc->sc_fwvm);
	if (error != 0)
		goto out;
out:
	sc->sc_vm_done = true;
	if (error != 0) {
		snprintf(sc->sc_vm_state, sizeof(sc->sc_vm_state),
		    "vm error=%d", error);
	} else {
		snprintf(sc->sc_vm_state, sizeof(sc->sc_vm_state),
		    "vm error=0 pc_pa=0x%08llx windows=%u dirs=%u leaves=%u "
		    "resources=%u vm_context=0x%08x",
		    (unsigned long long)sc->sc_fwvm->catalog_address,
		    (u_int)sc->sc_fwvm->windows, (u_int)sc->sc_fwvm->directories,
		    (u_int)sc->sc_fwvm->leaves,
		    (u_int)A733_GPU_FWCOMPUTE_RESOURCES,
		    (u_int)A733_GPU_FWCOMPUTE_SPAN_BASE +
		    A733_GPU_FWHEAP_VM_CONTEXT_OFF);
	}
	if (image != NULL)
		kmem_free(image, sizeof(*image));
	if (bytes != NULL)
		kmem_free(bytes, file.payload_bytes + file.header_bytes);
	return error;
}

/* Cache raw trace words. The firmware pointer is a ring index, not a count. */
static void
sun60i_gpu_trace_report(struct sun60i_gpu_softc *sc)
{
	const struct sun60i_a733_gpu_fwmem *m = sc->sc_fw_memory;
	const struct sun60i_a733_gpu_fwinit_region *ctl, *data;
	const struct rogue_fwif_tracebuf *tc;
	const struct sun60i_a733_gpu_fwprobe_result *r = &sc->sc_trace_result;
	const uint32_t *words = NULL;
	uint32_t total = 0, captured = 0, log_type = 0, pointer[2] = { 0, 0 };
	size_t used, k;
	int error = EINVAL, count;

	if (m == NULL || !m->interface_prepared || m->work.cpu == NULL ||
	    m->work.map == NULL || !m->work.loaded ||
	    (uintptr_t)m->work.cpu % 8 != 0)
		goto report;
	ctl = &m->interface_layout.object[A733_GPU_FWINIT_TRACE_CTL];
	data = &m->interface_layout.object[A733_GPU_FWINIT_TRACE0];
	if (ctl->buffer != A733_GPU_FWINIT_WORK ||
	    data->buffer != A733_GPU_FWINIT_WORK || ctl->offset % 8 != 0 ||
	    data->offset % sizeof(uint32_t) != 0 ||
	    ctl->size < sizeof(*tc) || ctl->offset > m->work.size ||
	    ctl->size > m->work.size - ctl->offset ||
	    data->offset > m->work.size ||
	    data->size > m->work.size - data->offset ||
	    data->size == 0 || data->size % sizeof(uint32_t) != 0)
		goto report;
	bus_dmamap_sync(m->dmat, m->work.map, 0, m->work.size,
	    BUS_DMASYNC_POSTREAD);
	tc = (const struct rogue_fwif_tracebuf *)
	    ((const uint8_t *)m->work.cpu + ctl->offset);
	log_type = tc->log_type;
	pointer[0] = tc->tracebuf[0].trace_pointer;
	pointer[1] = tc->tracebuf[1].trace_pointer;
	total = tc->tracebuf_size_in_dwords;
	if (total == 0 || total > data->size / sizeof(uint32_t)) {
		error = EPROTO;
		goto report;
	}
	words = (const uint32_t *)((const uint8_t *)m->work.cpu + data->offset);
	captured = total < A733_GPU_TRACE_CAPTURE_WORDS ?
	    total : A733_GPU_TRACE_CAPTURE_WORDS;
	error = 0;
report:
	count = snprintf(sc->sc_trace_state, sizeof(sc->sc_trace_state),
	    "err=%d polls=%u pre=%u/%u post=%u/%u kexec=%u "
	    "log=0x%x ptr0=%u ptr1=%u trace_rtn=0x%x "
	    "capture_error=%d total=%u captured=%u truncated=%u:",
	    r->error, r->polls, r->before.read_offset, r->before.write_offset,
	    r->after.read_offset, r->after.write_offset, r->after.cmds_executed,
	    log_type, pointer[0], pointer[1], (u_int)r->after.rtn,
	    error, total, captured, total > captured);
	if (count < 0 || (size_t)count >= sizeof(sc->sc_trace_state))
		return;
	used = (size_t)count;
	for (k = 0; k < captured && used + 8 < sizeof(sc->sc_trace_state); k++) {
		count = snprintf(sc->sc_trace_state + used,
		    sizeof(sc->sc_trace_state) - used, "%08x", words[k]);
		if (count != 8)
			return;
		used += (size_t)count;
	}
}

/* Copy existing HWR DMA data. Firmware counters never select RAM offsets. */
static void
sun60i_gpu_hwr_report(struct sun60i_gpu_softc *sc)
{
	const struct sun60i_a733_gpu_fwmem *m = sc->sc_fw_memory;
	const struct sun60i_a733_gpu_fwinit_region *region;
	struct rogue_fwif_hwrinfobuf snapshot;
	size_t used, i;
	int count, error = EINVAL;

	_Static_assert(sizeof(snapshot) == 2336, "firmware HWR buffer ABI");
	_Static_assert(sizeof(snapshot.hwr_info[0]) == 136,
	    "firmware HWR record ABI");
	if (m == NULL || !m->interface_prepared || m->work.cpu == NULL ||
	    m->work.map == NULL || !m->work.loaded ||
	    (uintptr_t)m->work.cpu % 8 != 0)
		goto fail;
	region = &m->interface_layout.object[A733_GPU_FWINIT_HWR];
	if (region->buffer != A733_GPU_FWINIT_WORK ||
	    region->offset % 8 != 0 || region->size < sizeof(snapshot) ||
	    region->offset > m->work.size ||
	    region->size > m->work.size - region->offset)
		goto fail;
	bus_dmamap_sync(m->dmat, m->work.map, region->offset,
	    sizeof(snapshot), BUS_DMASYNC_POSTREAD);
	memcpy(&snapshot, (const uint8_t *)m->work.cpu + region->offset,
	    sizeof(snapshot));
	count = snprintf(sc->sc_hwr_state, sizeof(sc->sc_hwr_state),
	    "capture_error=0 captured=%u counter=%u write_index=%u "
	    "dd_req_count=%u flags=0x%x:",
	    (u_int)__arraycount(snapshot.hwr_info), snapshot.hwr_counter,
	    snapshot.write_index, snapshot.dd_req_count,
	    snapshot.hwr_info_buf_flags);
	if (count < 0 || (size_t)count >= sizeof(sc->sc_hwr_state))
		goto full;
	used = (size_t)count;
	for (i = 0; i < __arraycount(snapshot.hwr_info); i++) {
		const struct rogue_hwrinfo *h = &snapshot.hwr_info[i];
		const char *name0 = "raw0", *name1 = "raw1", *name2 = "raw2";
		uint64_t raw[3];

		/* Keep unrecognized union variants raw for offline decoding. */
		memcpy(raw, &h->hwr_data, sizeof(raw));
		switch (h->hwr_type) {
		case ROGUE_HWRTYPE_BIF0FAULT:
		case ROGUE_HWRTYPE_BIF1FAULT:
		case ROGUE_HWRTYPE_TEXASBIF0FAULT:
			name0 = "req";
			name1 = "mmu";
			name2 = "pc";
			break;
		case ROGUE_HWRTYPE_MMUFAULT:
		case ROGUE_HWRTYPE_MMUMETAFAULT:
			name0 = "mmu0";
			name1 = "mmu1";
			name2 = "pc";
			break;
		default:
			break;
		}
		count = snprintf(sc->sc_hwr_state + used,
		    sizeof(sc->sc_hwr_state) - used,
		    " slot=%u number=%u type=%u dm=%u core=%u pid=%u "
		    "frame=%u hwrt=0x%x event=0x%x flags=0x%x "
		    "%s=0x%llx %s=0x%llx %s=0x%llx;",
		    (u_int)i, h->hwr_number, (u_int)h->hwr_type, h->dm,
		    h->core_id, h->pid, h->frame_num, h->active_hwrt_data,
		    h->event_status, h->hwr_recovery_flags,
		    name0, (unsigned long long)raw[0],
		    name1, (unsigned long long)raw[1],
		    name2, (unsigned long long)raw[2]);
		if (count < 0 || (size_t)count >= sizeof(sc->sc_hwr_state) - used)
			goto full;
		used += (size_t)count;
	}
	return;
full:
	error = ENOSPC;
fail:
	snprintf(sc->sc_hwr_state, sizeof(sc->sc_hwr_state),
	    "capture_error=%d captured=0:", error);
}

/* Sample the manifest's SCRATCH backing after device-to-host DMA sync. */
static int
sun60i_gpu_scratch_snapshot(const struct sun60i_a733_gpu_fwvm *vm,
    const struct a733_gpu_fwcompute_file *file, uint32_t *value)
{
	const struct sun60i_a733_gpu_dma *dma;
	size_t i;

	if (vm == NULL || !vm->prepared || file == NULL || value == NULL)
		return EINVAL;
	for (i = 0; i < __arraycount(file->resource); i++) {
		if (file->resource[i].role == A733_GPU_FWCOMPUTE_SCRATCH)
			break;
	}
	if (i == __arraycount(file->resource))
		return ENOENT;
	dma = &vm->resource[i];
	if (dma->cpu == NULL || dma->map == NULL || !dma->loaded ||
	    (uintptr_t)dma->cpu % sizeof(*value) != 0 ||
	    file->resource[i].bytes < sizeof(*value) ||
	    file->resource[i].bytes > dma->size)
		return EINVAL;
	bus_dmamap_sync(vm->dmat, dma->map, 0, sizeof(*value),
	    BUS_DMASYNC_POSTREAD);
	memcpy(value, dma->cpu, sizeof(*value));
	return 0;
}

/*
 * Stage 21: publish the compute KICK in slot 4 and wait for the client
 * queue to drain and the timeline word to complete. KCCB acceptance is
 * not compute completion; the CCCB and timeline observations are.
 */
static int
sun60i_gpu_stage_fwkick(struct sun60i_gpu_softc *sc, int power_node)
{
	const struct sun60i_a733_gpu_fwboot_result *boot = &sc->sc_boot_result;
	const struct sun60i_a733_gpu_fwprobe_result *probe = &sc->sc_probe_result;
	const struct sun60i_a733_gpu_fwprobe_result *cache = &sc->sc_cache_result;
	const struct sun60i_a733_gpu_fwprobe_result *heap = &sc->sc_heap_result;
	struct sun60i_a733_gpu_fwprobe_result *r = &sc->sc_kick_result;
	struct a733_gpu_fwcompute_file file;
	struct sun60i_a733_gpu_fwcompute_image *image = NULL;
	uint8_t *bytes = NULL;
	bus_space_handle_t bsh;
	bus_addr_t addr;
	bus_size_t size;
	uint32_t read_off = 0, write_off = 0, timeline = 0;
	uint32_t scratch_before = 0, scratch_after = 0;
	bool file_loaded = false, scratch_before_valid = false;
	bool scratch_after_valid = false;
	struct rogue_fwif_kccb_cmd kcmd;
	uint32_t reset_reason = 0, reset_dm = 0, reset_job = 0;
	uint32_t reset_ctx = 0, reset_flags = 0;
	uint64_t reset_pc = 0, reset_fault = 0;
	struct timespec start, now, elapsed;
	int error;

	if (sc->sc_run_stage != A733_GPU_STAGE_FWVM ||
	    !sun60i_gpu_compute_chain_ready(sc, boot, probe, cache) ||
	    !heap->attempted || !heap->kicked || heap->error != 0 ||
	    sc->sc_fwvm == NULL || !sc->sc_fwvm->prepared ||
	    r->attempted) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_gpu_compute_load(sc, &file, &bytes, &image);
	if (error != 0)
		goto out;
	file_loaded = true;
	scratch_before_valid = sun60i_gpu_scratch_snapshot(sc->sc_fwvm,
	    &file, &scratch_before) == 0;
	error = sun60i_gpu_boot_ready(sc, power_node);
	if (error != 0)
		goto out;
	error = fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size);
	if (error != 0 || addr != A733_GPU_BASE ||
	    size < A733_GPU_FWSTART_REG_SIZE) {
		error = EINVAL;
		goto out;
	}
	error = bus_space_map(sc->sc_bst, addr, A733_GPU_FWSTART_REG_SIZE,
	    0, &bsh);
	if (error != 0)
		goto out;
	/* Turn on firmware tracing first: the command rereads TRACE_CTL. */
	{
		struct sun60i_a733_gpu_fwmem *m = sc->sc_fw_memory;
		size_t off = m->interface_layout.object[
		    A733_GPU_FWINIT_TRACE_CTL].offset;
		struct rogue_fwif_tracebuf *tc;

		bus_dmamap_sync(m->dmat, m->work.map, off, sizeof(*tc),
		    BUS_DMASYNC_POSTREAD);
		tc = (struct rogue_fwif_tracebuf *)((uint8_t *)m->work.cpu +
		    off);
		tc->log_type = A733_GPU_FWPROBE_TRACE_LOGTYPE;
		bus_dmamap_sync(m->dmat, m->work.map, off, sizeof(*tc),
		    BUS_DMASYNC_PREWRITE);
	}
	memset(&kcmd, 0, sizeof(kcmd));
	kcmd.cmd_type = ROGUE_FWIF_KCCB_CMD_LOGTYPE_UPDATE;
	error = sun60i_a733_gpu_fwprobe_cmd(sc->sc_bst, bsh, sc->sc_fw_memory,
	    boot, heap, &kcmd, 3, A733_GPU_FWPROBE_TRACE_LOGTYPE,
	    &sc->sc_trace_result);
	bus_space_unmap(sc->sc_bst, bsh, A733_GPU_FWSTART_REG_SIZE);
	if (error != 0)
		goto out;
	error = bus_space_map(sc->sc_bst, addr, A733_GPU_FWSTART_REG_SIZE,
	    0, &bsh);
	if (error != 0)
		goto out;
	error = sun60i_a733_gpu_fwprobe_cmd(sc->sc_bst, bsh, sc->sc_fw_memory,
	    boot, &sc->sc_trace_result, &image->kick, 4,
	    A733_GPU_FWPROBE_TRACE_LOGTYPE, r);
	bus_space_unmap(sc->sc_bst, bsh, A733_GPU_FWSTART_REG_SIZE);
	if (error != 0)
		goto out;
	/* Bounded completion wait: client queue drains and timeline updates. */
	sun60i_a733_gpu_fwheap_observe(sc->sc_fwheap, &read_off, &write_off,
	    &timeline);
	getnanouptime(&start);
	while (read_off != A733_GPU_FWCOMPUTE_USED_BYTES || timeline != 1) {
		delay(100);
		sun60i_a733_gpu_fwheap_observe(sc->sc_fwheap, &read_off,
		    &write_off, &timeline);
		getnanouptime(&now);
		timespecsub(&now, &start, &elapsed);
		if (elapsed.tv_sec >= 4) {
			error = ETIMEDOUT;
			break;
		}
	}
out:
	sc->sc_kick_done = true;
	if (file_loaded)
		scratch_after_valid = sun60i_gpu_scratch_snapshot(sc->sc_fwvm,
		    &file, &scratch_after) == 0;
	if (sc->sc_fwheap != NULL)
		sun60i_a733_gpu_fwheap_observe(sc->sc_fwheap, &read_off,
		    &write_off, &timeline);
	/* Decode a pending firmware response; a context reset explains EIO. */
	if (r->after.fw_write_offset > r->after.fw_read_offset &&
	    r->after.fw_command == 0x2abc0069 && sc->sc_fw_memory != NULL) {
		const struct sun60i_a733_gpu_fwmem *m = sc->sc_fw_memory;
		size_t off = m->interface_layout.object[
		    A733_GPU_FWINIT_FWCCB_CMDS].offset +
		    r->after.fw_read_offset *
		    sizeof(struct rogue_fwif_fwccb_cmd);
		const struct rogue_fwif_fwccb_cmd *cmd;

		bus_dmamap_sync(m->dmat, m->work.map, off,
		    sizeof(*cmd), BUS_DMASYNC_POSTREAD);
		cmd = (const struct rogue_fwif_fwccb_cmd *)
		    ((const uint8_t *)m->work.cpu + off);
		reset_reason = cmd->cmd_data.cmd_context_reset_notification.
		    reset_reason;
		reset_dm = cmd->cmd_data.cmd_context_reset_notification.dm;
		reset_job = cmd->cmd_data.cmd_context_reset_notification.
		    reset_job_ref;
		reset_ctx = cmd->cmd_data.cmd_context_reset_notification.
		    server_common_context_id;
		reset_flags = cmd->cmd_data.cmd_context_reset_notification.flags;
		reset_pc = cmd->cmd_data.cmd_context_reset_notification.
		    pc_address;
		reset_fault = cmd->cmd_data.cmd_context_reset_notification.
		    fault_address;
	}
	{
		uint32_t kick_rtn = r->after.rtn;

		snprintf(sc->sc_kick_state, sizeof(sc->sc_kick_state),
		    "kick error=%d rtn=0x%x polls=%u ccb_read=%u ccb_write=%u "
		    "timeline=%u scratch=0x%x->0x%x scratch_valid=%u/%u "
		    "context=0x%08x "
		    "pow=%u hwr=0x%x faults=%u log=0x%x kexec=%u "
		    "fw_read=%u fw_write=%u fw_cmd=0x%x fw_state=%u os_state=%u "
		    "compchecks=%u reset_ctx=%u reason=%u dm=%u job=%u "
		    "rflags=0x%x rpc=0x%llx rfault=0x%llx",
		    error, (u_int)kick_rtn, (u_int)r->polls,
		    (u_int)read_off, (u_int)write_off, (u_int)timeline,
		    (u_int)scratch_before, (u_int)scratch_after,
		    (u_int)scratch_before_valid, (u_int)scratch_after_valid,
		    (u_int)A733_GPU_FWCOMPUTE_SPAN_BASE,
		    (u_int)r->after.pow_state, (u_int)r->after.hwr_state,
		    (u_int)r->after.fw_faults, (u_int)r->after.log_type,
		    (u_int)r->after.cmds_executed,
		    (u_int)r->after.fw_read_offset,
		    (u_int)r->after.fw_write_offset,
		    (u_int)r->after.fw_command,
		    (u_int)r->after.fw_state, (u_int)r->after.os_state,
		    (u_int)r->after.compchecks_updated,
		    (u_int)reset_ctx, (u_int)reset_reason, (u_int)reset_dm,
		    (u_int)reset_job, (u_int)reset_flags,
		    (unsigned long long)reset_pc,
		    (unsigned long long)reset_fault);
	}
	if (image != NULL)
		kmem_free(image, sizeof(*image));
	if (bytes != NULL)
		kmem_free(bytes, file.payload_bytes + file.header_bytes);
	
	sun60i_gpu_trace_report(sc);
	sun60i_gpu_hwr_report(sc);
return error;
}


/* Gate shared by the heap, VM, and kick stages after stage 18. */
static bool
sun60i_gpu_compute_chain_ready(const struct sun60i_gpu_softc *sc,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *probe,
    const struct sun60i_a733_gpu_fwprobe_result *cache)
{
	const struct sun60i_a733_gpu_fwmem *mem = sc->sc_fw_memory;

	return sc->sc_run_result == 0 && sc->sc_run_pass && sc->sc_run_core_on &&
	    sc->sc_have_id && sc->sc_bvnc == A733_GPU_EXPECTED &&
	    mem != NULL && mem->image.info.bvnc == sc->sc_bvnc &&
	    mem->interface_prepared && boot->attempted && boot->device_owned &&
	    boot->error == 0 && boot->start.touched && boot->start.released &&
	    boot->snapshot_valid && boot->firmware_started == 1 &&
	    probe->attempted && probe->published && probe->kick_attempted &&
	    probe->kicked && probe->error == 0 &&
	    cache->attempted && cache->published && cache->kick_attempted &&
	    cache->kicked && cache->error == 0 &&
	    cache->command == A733_GPU_FWPROBE_MMUCACHE && cache->slot == 1 &&
	    cache->after.rtn == 1;
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
	if (stage < A733_GPU_STAGE_SUPPLY || stage > A733_GPU_STAGE_FWKICK ||
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
		if (sc->sc_clock_source == 1) {
			error = sun60i_gpu_runtime_supply_top(sc);
			if (error == 0)
				error = sun60i_a733_ccu_gpu_prepare_pll(gpu, sc,
				    &retained);
		} else {
			error = sun60i_a733_ccu_gpu_prepare(gpu, sc, &retained);
		}
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
			snprintf(sc->sc_clock_state, sizeof(sc->sc_clock_state),
			    "error=%d reason=%u pll=0x%08x module=0x%08x "
			    "bus=0x%08x core_hz=%u bus_hz=%u", error,
			    (u_int)state.reason, state.sample[0][A733_GPU_PLL],
			    state.sample[0][A733_GPU_MODULE],
			    state.sample[0][A733_GPU_BUS],
			    state.core_hz, state.bus_hz);
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
	case A733_GPU_STAGE_FWINFO:
		error = sun60i_gpu_stage_fwinfo(sc);
		break;
	case A733_GPU_STAGE_FWMEM:
		error = sun60i_gpu_stage_fwmem(sc);
		break;
	case A733_GPU_STAGE_FWINIT:
		error = sun60i_gpu_stage_fwinit(sc);
		break;
	case A733_GPU_STAGE_FWBOOT:
		error = sun60i_gpu_stage_fwboot(sc, power_node);
		break;
	case A733_GPU_STAGE_FWPROBE:
		error = sun60i_gpu_stage_fwprobe(sc, power_node);
		break;
	case A733_GPU_STAGE_FWCACHE:
		error = sun60i_gpu_stage_fwcache(sc, power_node);
		break;
	case A733_GPU_STAGE_FWCOMPUTE:
		error = sun60i_gpu_stage_fwcompute(sc);
		break;
	case A733_GPU_STAGE_FWHEAP:
		error = sun60i_gpu_stage_fwheap(sc, power_node);
		break;
	case A733_GPU_STAGE_FWVM:
		error = sun60i_gpu_stage_fwvm(sc);
		break;
	case A733_GPU_STAGE_FWKICK:
		error = sun60i_gpu_stage_fwkick(sc, power_node);
		break;
	}
	/* A bounded timeout still allows the read-only waiter afterwards. */
	sc->sc_run_pass = (error == 0 || error == ETIMEDOUT);
	sc->sc_run_stage = stage;
out:
	sc->sc_run_result = error;
	mutex_exit(&sc->sc_run_lock);
}

/* Select the experiment before it acquires any hardware resource. */
static int
sun60i_gpu_clock_source(struct sun60i_gpu_softc *sc, u_int source)
{
	int error = 0;

	mutex_enter(&sc->sc_run_lock);
	if (source > 1)
		error = EINVAL;
	else if (sc->sc_run_stage != 0 || sc->sc_retained)
		error = EBUSY;
	else
		sc->sc_clock_source = source;
	mutex_exit(&sc->sc_run_lock);
	return error;
}

static int
sun60i_gpu_sysctl_clock_source(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct sun60i_gpu_softc *sc = sun60i_gpu_instance;
	u_int value;
	int error;

	if (sc == NULL)
		return ENXIO;
	value = sc->sc_clock_source;
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	return sun60i_gpu_clock_source(sc, value);
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
		    SYSCTL_DESCR("run runtime diagnostic stage 0..21"),
		    sun60i_gpu_sysctl_stage, 0, NULL, 0,
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create stage node\n");
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "clock_source",
	    SYSCTL_DESCR("runtime clock: 0 PERIPH0 400MHz, 1 PLL_GPU0 600MHz"),
	    sun60i_gpu_sysctl_clock_source, 0, NULL, 0,
	    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create clock source node\n");
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "clock_state",
	    SYSCTL_DESCR("cached stage-7 clock observation; no register access"),
	    NULL, 0, sc->sc_clock_state, sizeof(sc->sc_clock_state),
	    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create clock state node\n");
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "fw_state",
	    SYSCTL_DESCR("cached firmware metadata result; no GPU transfer"),
	    NULL, 0, sc->sc_fw_state, sizeof(sc->sc_fw_state),
	    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create firmware state node\n");
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "memory_state",
	    SYSCTL_DESCR("cached firmware memory ownership; no device access"),
	    NULL, 0, sc->sc_memory_state, sizeof(sc->sc_memory_state),
	    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create memory state node\n");

	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "interface_state",
	    SYSCTL_DESCR("cached firmware interface preparation; no register access"),
	    NULL, 0, sc->sc_interface_state, sizeof(sc->sc_interface_state),
	    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create interface state node\n");

	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "boot_state",
	    SYSCTL_DESCR("cached firmware bootstrap result; no device access"),
	    NULL, 0, sc->sc_boot_state, sizeof(sc->sc_boot_state),
	    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create boot state node\n");

	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "probe_state",
	    SYSCTL_DESCR("cached firmware command probe result; no device access"),
	    NULL, 0, sc->sc_probe_state, sizeof(sc->sc_probe_state),
	    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create probe state node\n");

	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "cache_state",
		    SYSCTL_DESCR("cached firmware MMUCACHE probe result; no device access"),
		    NULL, 0, sc->sc_cache_state, sizeof(sc->sc_cache_state),
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create cache state node\n");

	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "compute_state",
		    SYSCTL_DESCR("cached CPU compute serialization; no mapping, no publication"),
		    NULL, 0, sc->sc_compute_state, sizeof(sc->sc_compute_state),
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create compute state node\n");

	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "heap_state",
		    SYSCTL_DESCR("cached firmware-heap mapping and MMUCACHE resync"),
		    NULL, 0, sc->sc_heap_state, sizeof(sc->sc_heap_state),
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create heap state node\n");

	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "vm_state",
		    SYSCTL_DESCR("cached compute VM preparation"),
		    NULL, 0, sc->sc_vm_state, sizeof(sc->sc_vm_state),
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create vm state node\n");

	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "trace_state",
		    SYSCTL_DESCR("cached firmware trace words after the kick"),
		    NULL, 0, sc->sc_trace_state, sizeof(sc->sc_trace_state),
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create trace state node\n");

	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "hwr_state",
	    SYSCTL_DESCR("cached firmware HWR records after the kick"),
	    NULL, 0, sc->sc_hwr_state, sizeof(sc->sc_hwr_state),
	    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create HWR state node\n");

	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRING, "kick_state",
		    SYSCTL_DESCR("cached compute KICK submission and completion"),
		    NULL, 0, sc->sc_kick_state, sizeof(sc->sc_kick_state),
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create kick state node\n");

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
	sc->sc_dmat = faa->faa_dmat;
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
