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
 * Origin: EmberBSD; AI-assisted typed A733 NPU identification only.
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
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kauth.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <arm/sunxi/sun60i_a733_ccu.h>
#include <arm/sunxi/sun60i_a733_pck600.h>
#include <arm/sunxi/sun60i_a733_npu_dma.h>
#include <arm/sunxi/sun60i_a733_npu_fe.h>
#include <arm/sunxi/sun60i_a733_npu_run.h>
#include <arm/sunxi/sun60i_a733_npu_task.h>

#define A733_NPU_BASE		0x03600000
#define A733_NPU_ID_SIZE	0x34
#define A733_NPU_ID_PID		0x30
#define A733_NPU_ID_DATE	0x28
#define A733_NPU_EXPECTED_PID	UINT64_C(0x1000003b)
#define A733_NPU_EXPECTED_REV	UINT64_C(0x9202)

/* Vendor order: AHB and AXI resets first, then core, then gates/clocks. */
#define A733_NPU_STAGE_DOMAIN	1
#define A733_NPU_STAGE_CLOCKS	2
#define A733_NPU_STAGE_PREFLIGHT	3
#define A733_NPU_STAGE_POWER_ID	4

/*
 * VIPLite dispatch ABI (BSP vip2, dual MIT/GPL): one ioctl number with a
 * four-word envelope {command, error, bytes, buffer}. Commands 0..29 carry
 * their own structures; this first native subset covers the handshake a
 * user-space library performs: INIT, DESTROY, READ_REG, WRITE_REG,
 * QUERY_DATABASE, QUERY_DRIVER_STATUS.
 */
#define VIP_DRV_IOCTL		30000
#define VIP_DRV_CMD_MAX		29
#define VIP_DRV_BUF_MAX		1024
#define VIP_STATUS_SUCCESS	0
#define VIP_STATUS_IO		(-2)
#define VIP_STATUS_TIMEOUT	(-1)
#define VIP_STATUS_INVALID	(-3)
#define VIP_STATUS_NOTSUP	(-4)
#define VIP_VERSION		((2u << 16) | (0u << 8) | 0u)
/* The silicon entry this driver answers for: VIP9000NANODI_PLUS. */
#define VIP_DB_PID		0x1000003b
#define VIP_DB_REV		0x9202
#define VIP_DB_NN_CORES		8
#define VIP_DB_VIP_CORES	1
#define VIP_DB_SRAM		(512 * 1024)

struct vip_drv_envelope {
	uint32_t command;
	int32_t error;
	uint32_t bytes;
	uint64_t buffer;
};
struct vip_drv_init {
	uint32_t video_mem_size;
	uint32_t version;
};
struct vip_drv_reg {
	uint64_t address_unused;
	uint32_t offset;
	uint32_t value;
};
struct vip_drv_database {
	uint32_t logical_vip_count;
	uint32_t core_count;
	uint32_t core_id;
	uint32_t nn_core_count;
	uint32_t tp_core_count;
	uint32_t vip_sram_size;
	uint32_t axi_sram_size;
	uint32_t axi_bus_width;
};
struct vip_drv_driver_status {
	uint32_t user_signal_count;
	uint32_t user_commit_count;
	uint32_t power_state;
	uint32_t init_counter;
};

struct sun60i_npu_softc {
	device_t sc_dev;
	int sc_phandle;
	bus_space_tag_t sc_bst;
	bus_dma_tag_t sc_dmat;
	kmutex_t sc_run_lock;
	struct sysctllog *sc_sysctllog;
	u_int sc_run_stage;
	u_int sc_run_result;
	uint64_t sc_pid, sc_rev, sc_date;
	bool sc_run_pass;
	bool sc_mmio_ready;
	struct clk *sc_owned_clock;
	int sc_owned_power;
	/* Engine1 is CPU preparation and a non-destructive raw snapshot only. */
	u_int sc_engine_attempted, sc_engine_prepared, sc_engine_stage;
	u_int sc_engine_result, sc_engine_release_error, sc_engine_snapshot_valid;
	u_int sc_engine_command_bytes;
	struct sun60i_a733_npu_fe_snapshot sc_engine_snapshot;
	struct sun60i_a733_npu_dma_info sc_engine_dma_info;
	struct sun60i_a733_npu_dma *sc_engine_dma;
	struct sun60i_a733_npu_run sc_engine_run;
	struct sun60i_a733_npu_run_status sc_engine_run_cache;
	/* One native profile is selected for this boot. TASK revokes legacy MMIO. */
	u_int sc_native_profile, sc_task_stage, sc_task_result, sc_task_attempted;
	uint32_t sc_task_command_bytes, sc_task_constants_bytes;
	uint32_t sc_task_input_bytes, sc_task_output_bytes;
	struct sun60i_a733_npu_task *sc_task;
	struct sun60i_a733_npu_task_status sc_task_cache;
	/* VIPLite dispatch state */
	devmajor_t sc_cmajor;
	bool sc_cdev_attached;
	bool sc_vip_initialized;
	uint32_t sc_vip_processes;
};

static struct sun60i_npu_softc *sun60i_npu_instance;
static devmajor_t sun60i_npu_cmajor = -1;

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
	if (pd == NULL || len != 2 * sizeof(*pd) || be32toh(pd[1]) != 4)
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
	return (pwpr & __BITS(3, 0)) == 8 ? 0 : EBUSY;
}

/* Stage 2: resolve and query the five nominal clocks without console I/O. */
static int
sun60i_npu_stage_clocks(struct sun60i_npu_softc *sc)
{
	static const char * const names[] = {
		"clk_npu", "clk_parent", "clk_bus", "clk_mbus_gate",
		"clk_ahb_gate",
	};
	struct clk *clk;

	for (u_int i = 0; i < __arraycount(names); i++) {
		clk = fdtbus_clock_get(sc->sc_phandle, names[i]);
		if (clk == NULL)
			return ENXIO;
		(void)clk_get_rate(clk);
		clk_put(clk);
	}
	return 0;
}

/*
 * BSP vip2 reads a 32-bit PID at 0x30, revision at 0x24 and date at 0x28.
 * Keep all accesses inside the 0x34-byte identification window.
 */
static int
sun60i_npu_read_ids(struct sun60i_npu_softc *sc)
{
	bus_space_handle_t bsh;
	uint32_t pid, revision, date;
	int error;

	error = bus_space_map(sc->sc_bst, A733_NPU_BASE, A733_NPU_ID_SIZE,
	    0, &bsh);
	if (error != 0)
		return error;
	error = 0;
	if (bus_space_peek_4(sc->sc_bst, bsh, A733_NPU_ID_PID, &pid) != 0 ||
	    bus_space_peek_4(sc->sc_bst, bsh, 0x24, &revision) != 0 ||
	    bus_space_peek_4(sc->sc_bst, bsh, A733_NPU_ID_DATE, &date) != 0) {
		error = EFAULT;
	} else {
		sc->sc_pid = pid;
		sc->sc_rev = revision;
		sc->sc_date = date;
	}
	bus_space_unmap(sc->sc_bst, bsh, A733_NPU_ID_SIZE);
	return error;
}

/* Stage 4: retain PCK before the first typed CCU write attempt. */
static int
sun60i_npu_stage_power_id(struct sun60i_npu_softc *sc)
{
	struct clk *mod;
	bool ccu_reserved = false, attempted = false;
	int power_node, error;

	KASSERT(mutex_owned(&sc->sc_run_lock));
	sc->sc_mmio_ready = false;
	power_node = sun60i_npu_power_node(sc);
	if (power_node <= 0 || !of_compatible_match(power_node, power_compat))
		return ENXIO;
	mod = fdtbus_clock_get(sc->sc_phandle, "clk_npu");
	if (mod == NULL)
		return ENODEV;
	error = sun60i_a733_pck_npu_reserve(power_node, sc);
	if (error != 0)
		goto put;
	error = sun60i_a733_ccu_npu_reserve(mod, sc);
	if (error != 0)
		goto clean;
	ccu_reserved = true;
	error = sun60i_a733_pck_npu_retain(power_node, sc);
	if (error != 0)
		goto clean;
	sc->sc_owned_clock = mod;
	sc->sc_owned_power = power_node;
	error = sun60i_a733_ccu_npu_prepare(mod, sc, &attempted);
	if (error != 0)
		return error;
	if (!attempted)
		return EIO;
	/* Fresh retained proofs precede even the internal identification map. */
	error = sun60i_a733_ccu_npu_ready(mod, sc);
	if (error == 0)
		error = sun60i_a733_pck_npu_ready(power_node, sc);
	if (error == 0)
		error = sun60i_npu_read_ids(sc);
	if (error == 0 && (sc->sc_pid != A733_NPU_EXPECTED_PID ||
	    sc->sc_rev != A733_NPU_EXPECTED_REV))
		error = ENODEV;
	if (error == 0)
		sc->sc_mmio_ready = true;
	return error;
clean:
	/* Release only our clean acquisitions. Never unwind retained state. */
	if (ccu_reserved)
		(void)sun60i_a733_ccu_npu_release(mod, sc);
	(void)sun60i_a733_pck_npu_release(power_node, sc);
put:
	clk_put(mod);
	return error;
}

static void
sun60i_npu_run_stage(struct sun60i_npu_softc *sc, u_int stage)
{
	int power_node, error = EINVAL;

	mutex_enter(&sc->sc_run_lock);
	/* Stage 0 is a dry run of the sysctl machinery: no hardware calls. */
	if (stage == 0) {
		sc->sc_run_result = 0;
		sc->sc_mmio_ready = false;
		mutex_exit(&sc->sc_run_lock);
		return;
	}
	if (sc->sc_native_profile != 0) {
		sc->sc_run_result = EBUSY;
		sc->sc_mmio_ready = false;
		mutex_exit(&sc->sc_run_lock);
		return;
	}
	if (stage < A733_NPU_STAGE_DOMAIN || stage > A733_NPU_STAGE_POWER_ID ||
	    stage != sc->sc_run_stage + 1 || !sc->sc_run_pass) {
		sc->sc_run_result = EINVAL;
		sc->sc_mmio_ready = false;
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
	case A733_NPU_STAGE_PREFLIGHT:
		/* Cold MMIO has no owner proof. Keep the staged preflight only. */
		error = EBUSY;
		break;
	case A733_NPU_STAGE_POWER_ID:
		error = sun60i_npu_stage_power_id(sc);
		break;
	}
	sc->sc_run_pass = (error == 0 || error == EBUSY);
	sc->sc_run_stage = stage;
out:
	sc->sc_run_result = error;
	if (error != 0)
		sc->sc_mmio_ready = false;
	mutex_exit(&sc->sc_run_lock);
}

/* Caller holds sc_run_lock. Database constants do not prove hardware state. */
static int
sun60i_npu_mmio_ready(struct sun60i_npu_softc *sc)
{
	int error;

	KASSERT(mutex_owned(&sc->sc_run_lock));
	if (sc->sc_task_attempted ||
	    sc->sc_engine_run.status.publication_attempted ||
	    !sc->sc_mmio_ready || sc->sc_run_stage != A733_NPU_STAGE_POWER_ID ||
	    sc->sc_run_result != 0 || !sc->sc_run_pass ||
	    sc->sc_pid != A733_NPU_EXPECTED_PID || sc->sc_rev != A733_NPU_EXPECTED_REV)
		return EBUSY;
	error = sun60i_a733_ccu_npu_ready(sc->sc_owned_clock, sc);
	if (error != 0)
		return error;
	return sun60i_a733_pck_npu_ready(sc->sc_owned_power, sc);
}

/* Caller holds sc_run_lock. No NPU register writes or FE start occur here. */
static int
sun60i_npu_engine_prepare(struct sun60i_npu_softc *sc)
{
	struct sun60i_a733_npu_fe_snapshot snapshot;
	struct sun60i_a733_npu_dma_info info;
	uint8_t image[A733_NPU_FE_EVENT_BYTES];
	int error;

	KASSERT(mutex_owned(&sc->sc_run_lock));
	if (sc->sc_native_profile != 0 &&
	    sc->sc_native_profile != A733_NPU_DMA_PROFILE_FE)
		return EBUSY;
	if (sc->sc_engine_attempted)
		return EALREADY;
	sc->sc_native_profile = A733_NPU_DMA_PROFILE_FE;
	error = sun60i_npu_mmio_ready(sc);
	/* A failed new observation must not leave the older MMIO proof usable. */
	sc->sc_mmio_ready = false;
	if (error != 0)
		return error;
	sc->sc_engine_attempted = 1;
	sc->sc_engine_stage = 1;
	error = sun60i_a733_npu_fe_snapshot(sc->sc_bst, sc->sc_owned_clock,
	    sc->sc_owned_power, sc, &snapshot);
	if (error != 0)
		return error;
	sc->sc_engine_snapshot = snapshot;
	sc->sc_engine_snapshot_valid = snapshot.valid;
	error = sun60i_a733_npu_fe_admit(&snapshot);
	if (error != 0)
		return error;
	error = sun60i_a733_npu_fe_event_image(image, sizeof(image));
	if (error != 0)
		return error;
	error = sun60i_a733_npu_dma_prepare(sc->sc_dmat, image, sizeof(image),
	    &sc->sc_engine_dma);
	if (error != 0)
		return error;
	error = sun60i_a733_npu_dma_info(sc->sc_engine_dma, &info);
	if (error == 0 && info.command_address == info.command_virtual)
		error = EADDRINUSE;
	if (error != 0) {
		sc->sc_engine_release_error = sun60i_a733_npu_dma_destroy(sc->sc_engine_dma);
		if (sc->sc_engine_release_error == 0)
			sc->sc_engine_dma = NULL;
		return error;
	}
	sc->sc_engine_dma_info = info;
	sc->sc_engine_command_bytes = (u_int)info.command_bytes;
	sc->sc_engine_prepared = 1;
	sc->sc_mmio_ready = true;
	return 0;
}

/* Caller holds sc_run_lock. A run attempt never restores legacy access. */
static int
sun60i_npu_engine_run(struct sun60i_npu_softc *sc)
{
	int error;

	KASSERT(mutex_owned(&sc->sc_run_lock));
	if (sc->sc_native_profile != A733_NPU_DMA_PROFILE_FE)
		return sc->sc_native_profile == 0 ? EINVAL : EBUSY;
	if (sc->sc_engine_run.status.attempted)
		return EALREADY;
	error = sun60i_npu_mmio_ready(sc);
	sc->sc_mmio_ready = false;
	if (error != 0)
		return error;
	if (!sc->sc_engine_prepared || sc->sc_engine_dma == NULL ||
	    sc->sc_engine_release_error != 0)
		return EINVAL;
	error = sun60i_a733_npu_run_once(&sc->sc_engine_run, sc->sc_bst,
	    sc->sc_owned_clock, sc->sc_owned_power, sc, sc->sc_phandle,
	    device_xname(sc->sc_dev), sc->sc_engine_dma);
	sun60i_a733_npu_run_status(&sc->sc_engine_run, &sc->sc_engine_run_cache);
	if (sc->sc_engine_run_cache.attempted)
		sc->sc_engine_stage = 2;
	if (sc->sc_engine_run_cache.command_bytes != 0)
		sc->sc_engine_command_bytes = sc->sc_engine_run_cache.command_bytes;
	return error;
}

/* Caller holds sc_run_lock. These values are cached, not live MMIO. */
static void
sun60i_npu_task_cache(struct sun60i_npu_softc *sc)
{

	KASSERT(mutex_owned(&sc->sc_run_lock));
	sun60i_a733_npu_task_status(sc->sc_task, &sc->sc_task_cache);
	/* Only the fixed bounded coordinator supplies these byte counts. */
	sc->sc_task_command_bytes =
	    (uint32_t)sc->sc_task_cache.memory.memory.command_bytes;
	sc->sc_task_constants_bytes =
	    (uint32_t)sc->sc_task_cache.memory.constants_bytes;
	sc->sc_task_input_bytes = (uint32_t)sc->sc_task_cache.memory.input_bytes;
	sc->sc_task_output_bytes = (uint32_t)sc->sc_task_cache.memory.output_bytes;
}

/* The first TASK choice permanently revokes legacy register access. */
static int
sun60i_npu_task_prepare(struct sun60i_npu_softc *sc)
{
	int error;

	KASSERT(mutex_owned(&sc->sc_run_lock));
	if (sc->sc_task_attempted)
		return EALREADY;
	if (sc->sc_native_profile != 0)
		return EBUSY;
	error = sun60i_npu_mmio_ready(sc);
	sc->sc_native_profile = A733_NPU_DMA_PROFILE_TASK;
	sc->sc_task_attempted = 1;
	sc->sc_task_stage = 1;
	sc->sc_mmio_ready = false;
	if (error != 0)
		return error;
	/* This fixed constructor has no NPU MMIO, IRQ setup or DMA sync. */
	error = sun60i_a733_npu_task_prepare(sc->sc_dmat, &sc->sc_task);
	if (sc->sc_task != NULL)
		sun60i_npu_task_cache(sc);
	return error;
}

/* The closed coordinator supplies all completion and numerical predicates. */
static int
sun60i_npu_task_run(struct sun60i_npu_softc *sc)
{
	int error;

	KASSERT(mutex_owned(&sc->sc_run_lock));
	if (sc->sc_native_profile != A733_NPU_DMA_PROFILE_TASK)
		return sc->sc_native_profile == 0 ? EINVAL : EBUSY;
	if (sc->sc_task_stage == 2)
		return EALREADY;
	if (sc->sc_task_stage != 1 || sc->sc_task == NULL ||
	    !sc->sc_task_cache.prepared || sc->sc_task_cache.release_error != 0)
		return EINVAL;
	sc->sc_task_stage = 2;
	sc->sc_mmio_ready = false;
	error = sun60i_a733_npu_task_run(sc->sc_task, sc->sc_bst,
	    sc->sc_owned_clock, sc->sc_owned_power, sc, sc->sc_phandle,
	    device_xname(sc->sc_dev));
	sun60i_npu_task_cache(sc);
	return error;
}

/* One bounded register access through a freshly verified retained profile. */
static int
sun60i_npu_vip_read_reg(struct sun60i_npu_softc *sc,
    struct vip_drv_reg *reg)
{
	bus_space_handle_t bsh;
	uint32_t value;
	int error = VIP_STATUS_IO;

	if (reg->offset == 0 || reg->offset > A733_NPU_ID_SIZE - 4 ||
	    (reg->offset & 3) != 0)
		return VIP_STATUS_INVALID;
	mutex_enter(&sc->sc_run_lock);
	if (sun60i_npu_mmio_ready(sc) != 0)
		goto out;
	if (bus_space_map(sc->sc_bst, A733_NPU_BASE,
	    A733_NPU_ID_SIZE, 0, &bsh) != 0)
		goto out;
	if (bus_space_peek_4(sc->sc_bst, bsh, reg->offset, &value) == 0) {
		reg->value = value;
		error = VIP_STATUS_SUCCESS;
	}
	bus_space_unmap(sc->sc_bst, bsh, A733_NPU_ID_SIZE);
out:
	mutex_exit(&sc->sc_run_lock);
	return error;
}

static int
sun60i_npu_vip_write_reg(struct sun60i_npu_softc *sc,
    const struct vip_drv_reg *reg)
{

	/* Register writes need the accepted task model; not in this stage. */
	return VIP_STATUS_NOTSUP;
}

static int
sun60i_npu_vip_command(struct sun60i_npu_softc *sc, uint32_t command,
    void *data, uint32_t bytes)
{
	struct vip_drv_init *init;
	struct vip_drv_database *db;
	struct vip_drv_driver_status *st;

	switch (command) {
	case 0:	/* INIT */
		if (bytes < sizeof(*init))
			return VIP_STATUS_INVALID;
		init = data;
		if (init->version != VIP_VERSION)
			return VIP_STATUS_NOTSUP;
		mutex_enter(&sc->sc_run_lock);
		sc->sc_vip_initialized = true;
		sc->sc_vip_processes++;
		mutex_exit(&sc->sc_run_lock);
		return VIP_STATUS_SUCCESS;
	case 1:	/* DESTROY */
		mutex_enter(&sc->sc_run_lock);
		if (sc->sc_vip_processes > 0)
			sc->sc_vip_processes--;
		sc->sc_vip_initialized = sc->sc_vip_processes > 0;
		mutex_exit(&sc->sc_run_lock);
		return VIP_STATUS_SUCCESS;
	case 2:	/* READ_REG */
		if (bytes < sizeof(struct vip_drv_reg))
			return VIP_STATUS_INVALID;
		return sun60i_npu_vip_read_reg(sc, data);
	case 3:	/* WRITE_REG */
		if (bytes < sizeof(struct vip_drv_reg))
			return VIP_STATUS_INVALID;
		return sun60i_npu_vip_write_reg(sc, data);
	case 10: /* QUERY_DATABASE */
		if (bytes < sizeof(*db))
			return VIP_STATUS_INVALID;
		db = data;
		memset(db, 0, sizeof(*db));
		db->logical_vip_count = 1;
		db->core_count = VIP_DB_VIP_CORES;
		db->core_id = VIP_DB_PID;
		db->nn_core_count = VIP_DB_NN_CORES;
		db->tp_core_count = 0;
		db->vip_sram_size = VIP_DB_SRAM;
		db->axi_sram_size = 0;
		db->axi_bus_width = 128;
		return VIP_STATUS_SUCCESS;
	case 21: /* QUERY_DRIVER_STATUS */
		if (bytes < sizeof(*st))
			return VIP_STATUS_INVALID;
		st = data;
		memset(st, 0, sizeof(*st));
		mutex_enter(&sc->sc_run_lock);
		st->init_counter = sc->sc_vip_processes;
		st->power_state = sc->sc_vip_initialized ? 1 : 0;
		mutex_exit(&sc->sc_run_lock);
		return VIP_STATUS_SUCCESS;
	default:
		/* Memory, cache, task and wrap commands come with the
		 * full engine bring-up; fail closed until then. */
		return VIP_STATUS_NOTSUP;
	}
}

/*
 * The vendor picked a plain number (30000) as the ioctl command; its
 * NetBSD decode has a zero data size, so the framework hands the raw
 * user pointer through instead of copying. Copy the envelope explicitly.
 */
static int
sun60i_npu_vip_ioctl(dev_t dev, u_long cmd, void *vdata, int flags,
    struct lwp *l)
{
	struct sun60i_npu_softc *sc = sun60i_npu_instance;
	struct vip_drv_envelope env_s, *env = &env_s;
	uint8_t buf[VIP_DRV_BUF_MAX];
	void *data;
	uint32_t bytes;

	if (sc == NULL)
		return ENXIO;
	if (cmd != VIP_DRV_IOCTL)
		return ENOTTY;
	if ((flags & FWRITE) == 0)
		return EACCES;
	if (copyin(vdata, env, sizeof(*env)) != 0)
		return EFAULT;
	if (env->command > VIP_DRV_CMD_MAX || env->bytes > VIP_DRV_BUF_MAX)
		return EINVAL;
	bytes = env->bytes;
	data = bytes > 0 ? buf : NULL;
	if (bytes > 0) {
		if (copyin((void *)(uintptr_t)env->buffer, buf, bytes) != 0)
			return EFAULT;
	}
	env->error = sun60i_npu_vip_command(sc, env->command, data, bytes);
	if (bytes > 0 && env->error == VIP_STATUS_SUCCESS) {
		if (copyout(buf, (void *)(uintptr_t)env->buffer, bytes) != 0)
			return EFAULT;
	}
	if (copyout(env, vdata, sizeof(*env)) != 0)
		return EFAULT;
	return 0;
}

static int
sun60i_npu_vip_open(dev_t dev, int flags, int fmt, struct lwp *l)
{

	return sun60i_npu_instance == NULL ? ENXIO : 0;
}

static int
sun60i_npu_vip_close(dev_t dev, int flags, int fmt, struct lwp *l)
{

	return 0;
}

const struct cdevsw sun60i_npu_cdevsw = {
	.d_open = sun60i_npu_vip_open,
	.d_close = sun60i_npu_vip_close,
	.d_read = noread,
	.d_write = nowrite,
	.d_ioctl = sun60i_npu_vip_ioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = nodiscard,
	.d_flag = D_OTHER
};

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

/* Copy cached values under the run mutex. No register access is permitted. */
static int
sun60i_npu_sysctl_engine_value(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct sun60i_npu_softc *sc = sun60i_npu_instance;
	union { uint32_t word; uint64_t quad; } value;
	size_t bytes;

	if (sc == NULL)
		return ENXIO;
	if (newp != NULL)
		return EPERM;
	switch (SYSCTL_TYPE(rnode->sysctl_flags)) {
	case CTLTYPE_INT:
		node.sysctl_data = &value.word;
		bytes = sizeof(value.word);
		break;
	case CTLTYPE_QUAD:
		node.sysctl_data = &value.quad;
		bytes = sizeof(value.quad);
		break;
	default:
		return EINVAL;
	}
	mutex_enter(&sc->sc_run_lock);
	memcpy(node.sysctl_data, rnode->sysctl_data, bytes);
	mutex_exit(&sc->sc_run_lock);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
sun60i_npu_sysctl_engine_stage(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct sun60i_npu_softc *sc = sun60i_npu_instance;
	u_int value;
	int error;

	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->sc_run_lock);
	value = sc->sc_engine_stage;
	mutex_exit(&sc->sc_run_lock);
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	mutex_enter(&sc->sc_run_lock);
	if (value == 0)
		error = 0;
	else if (value == 1)
		error = sun60i_npu_engine_prepare(sc);
	else if (value == 2)
		error = sun60i_npu_engine_run(sc);
	else
		error = EINVAL;
	sc->sc_engine_result = error;
	mutex_exit(&sc->sc_run_lock);
	return error;
}

static int
sun60i_npu_sysctl_task_stage(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct sun60i_npu_softc *sc = sun60i_npu_instance;
	u_int value;
	int error;

	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->sc_run_lock);
	value = sc->sc_task_stage;
	mutex_exit(&sc->sc_run_lock);
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	mutex_enter(&sc->sc_run_lock);
	if (value == 0)
		error = 0;
	else if (value == 1)
		error = sun60i_npu_task_prepare(sc);
	else if (value == 2)
		error = sun60i_npu_task_run(sc);
	else
		error = EINVAL;
	sc->sc_task_result = error;
	mutex_exit(&sc->sc_run_lock);
	return error;
}

static void
sun60i_npu_sysctl_engine_setup(struct sun60i_npu_softc *sc,
    const struct sysctlnode *rnode)
{
	const struct {
		const char *name;
		int *data;
	} integers[] = {
		{ "engine_result", (int *)&sc->sc_engine_result },
		{ "engine_attempted", (int *)&sc->sc_engine_attempted },
		{ "engine_prepared", (int *)&sc->sc_engine_prepared },
		{ "engine_snapshot_valid",
		    (int *)&sc->sc_engine_snapshot_valid },
		{ "engine_release_error", (int *)&sc->sc_engine_release_error },
		{ "engine_command_va",
		    (int *)&sc->sc_engine_dma_info.command_virtual },
		{ "engine_pd", (int *)&sc->sc_engine_dma_info.pd_entry },
		{ "engine_command_bytes", (int *)&sc->sc_engine_command_bytes },
		{ "run_attempted", (int *)&sc->sc_engine_run_cache.attempted },
		{ "run_publication_attempted", (int *)&sc->sc_engine_run_cache.publication_attempted },
		{ "run_retained", (int *)&sc->sc_engine_run_cache.retained },
		{ "run_started", (int *)&sc->sc_engine_run_cache.started },
		{ "run_terminal", (int *)&sc->sc_engine_run_cache.terminal },
		{ "run_snapshot_valid", (int *)&sc->sc_engine_run_cache.snapshot_valid },
		{ "run_step", (int *)&sc->sc_engine_run_cache.step },
		{ "run_error", (int *)&sc->sc_engine_run_cache.error },
		{ "run_last_readback", (int *)&sc->sc_engine_run_cache.last_readback },
		{ "run_command_bytes", (int *)&sc->sc_engine_run_cache.command_bytes },
		{ "run_irq_raw", (int *)&sc->sc_engine_run_cache.irq_raw },
		{ "run_irq_bits", (int *)&sc->sc_engine_run_cache.irq_bits },
		{ "run_irq_fault", (int *)&sc->sc_engine_run_cache.irq_fault },
		{ "run_irq_mask_readback", (int *)&sc->sc_engine_run_cache.irq_mask_readback },
		{ "run_terminal_status", (int *)&sc->sc_engine_run_cache.terminal_status },
		{ "run_stale0", (int *)&sc->sc_engine_run_cache.stale[0] },
		{ "run_stale1", (int *)&sc->sc_engine_run_cache.stale[1] },
		{ "run_terminal_idle0", (int *)&sc->sc_engine_run_cache.terminal_idle[0] },
		{ "run_terminal_idle1", (int *)&sc->sc_engine_run_cache.terminal_idle[1] },
		{ "run_terminal_dma0", (int *)&sc->sc_engine_run_cache.terminal_dma[0] },
		{ "run_terminal_dma1", (int *)&sc->sc_engine_run_cache.terminal_dma[1] }
	};
	const struct {
		const char *name;
		int64_t *data;
	} addresses[] = {
		{ "engine_mtlb",
		    (int64_t *)&sc->sc_engine_dma_info.mtlb_address },
		{ "engine_stlb",
		    (int64_t *)&sc->sc_engine_dma_info.stlb_address },
		{ "engine_command_dma",
		    (int64_t *)&sc->sc_engine_dma_info.command_address },
		{ "run_irq_count", (int64_t *)&sc->sc_engine_run_cache.irq_count },
		{ "run_irq_baseline", (int64_t *)&sc->sc_engine_run_cache.irq_baseline }
	};
	static const char * const names[2][A733_NPU_FE_NREGS] = {
		{
			"engine_s0_r000", "engine_s0_r004", "engine_s0_r014",
			"engine_s0_r024", "engine_s0_r030", "engine_s0_r388",
			"engine_s0_r3a4", "engine_s0_r3b4", "engine_s0_r654",
			"engine_s0_r664", "engine_s0_r668", "engine_s0_r66c"
		},
		{
			"engine_s1_r000", "engine_s1_r004", "engine_s1_r014",
			"engine_s1_r024", "engine_s1_r030", "engine_s1_r388",
			"engine_s1_r3a4", "engine_s1_r3b4", "engine_s1_r654",
			"engine_s1_r664", "engine_s1_r668", "engine_s1_r66c"
		}
	};

	static const char * const run_names[2][A733_NPU_RUN_NREGS] = {
		{
			"run_s0_r000", "run_s0_r004", "run_s0_r014",
			"run_s0_r024", "run_s0_r030", "run_s0_r388",
			"run_s0_r3a4", "run_s0_r3b4", "run_s0_r654",
			"run_s0_r664", "run_s0_r668", "run_s0_r66c",
			"run_s0_r090", "run_s0_r100", "run_s0_r104",
			"run_s0_r10c", "run_s0_r3a8", "run_s0_r414"
		},
		{
			"run_s1_r000", "run_s1_r004", "run_s1_r014",
			"run_s1_r024", "run_s1_r030", "run_s1_r388",
			"run_s1_r3a4", "run_s1_r3b4", "run_s1_r654",
			"run_s1_r664", "run_s1_r668", "run_s1_r66c",
			"run_s1_r090", "run_s1_r100", "run_s1_r104",
			"run_s1_r10c", "run_s1_r3a8", "run_s1_r414"
		}
	};

	/* The framework restricts writes without CTLFLAG_ANYWRITE. */
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "engine_stage",
	    SYSCTL_DESCR("0 dry; 1 prepare; 2 one retained native FE run"),
	    sun60i_npu_sysctl_engine_stage, 0, NULL, 0,
	    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev,
		    "cannot create engine stage node\n");
	for (size_t i = 0; i < __arraycount(integers); i++) {
		if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
		    CTLTYPE_INT, integers[i].name,
		    SYSCTL_DESCR("cached native NPU engine value"),
		    sun60i_npu_sysctl_engine_value, 0, integers[i].data, 0,
		    CTL_CREATE, CTL_EOL) != 0)
			aprint_error_dev(sc->sc_dev,
			    "cannot create cached engine node\n");
	}
	for (size_t i = 0; i < __arraycount(addresses); i++) {
		if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
		    CTLTYPE_QUAD, addresses[i].name,
		    SYSCTL_DESCR("cached owned NPU DMA address"),
		    sun60i_npu_sysctl_engine_value, 0, addresses[i].data, 0,
		    CTL_CREATE, CTL_EOL) != 0)
			aprint_error_dev(sc->sc_dev,
			    "cannot create cached DMA node\n");
	}
	for (size_t row = 0; row < 2; row++) {
		for (size_t i = 0; i < A733_NPU_FE_NREGS; i++) {
			if (sysctl_createv(NULL, 0, &rnode, NULL,
			    CTLFLAG_READONLY, CTLTYPE_INT, names[row][i],
			    SYSCTL_DESCR("cached raw peek32 sample"),
			    sun60i_npu_sysctl_engine_value, 0,
			    (int *)&sc->sc_engine_snapshot.words[row][i], 0,
			    CTL_CREATE, CTL_EOL) != 0)
				aprint_error_dev(sc->sc_dev,
				    "cannot create engine raw node\n");
		}
	}
	for (size_t row = 0; row < 2; row++) {
		for (size_t i = 0; i < A733_NPU_RUN_NREGS; i++) {
			if (sysctl_createv(NULL, 0, &rnode, NULL,
			    CTLFLAG_READONLY, CTLTYPE_INT, run_names[row][i],
			    SYSCTL_DESCR("cached pre-run raw peek32 sample"),
			    sun60i_npu_sysctl_engine_value, 0,
			    (int *)&sc->sc_engine_run_cache.raw[row][i], 0,
			    CTL_CREATE, CTL_EOL) != 0)
				aprint_error_dev(sc->sc_dev,
				    "cannot create run raw node\n");
		}
	}
}

static void
sun60i_npu_sysctl_task_setup(struct sun60i_npu_softc *sc,
    const struct sysctlnode *rnode)
{
	const struct {
		const char *name;
		uint32_t *data;
	} words[] = {
		{ "native_profile", &sc->sc_native_profile },
		{ "task_result", &sc->sc_task_result },
		{ "task_attempted", &sc->sc_task_attempted },
		{ "task_prepared", &sc->sc_task_cache.prepared },
		{ "task_retained", &sc->sc_task_cache.retained },
		{ "task_release_error", &sc->sc_task_cache.release_error },
		{ "task_run_attempted", &sc->sc_task_cache.run.attempted },
		{ "task_run_publication_attempted",
		    &sc->sc_task_cache.run.publication_attempted },
		{ "task_run_retained", &sc->sc_task_cache.run.retained },
		{ "task_run_started", &sc->sc_task_cache.run.started },
		{ "task_run_terminal", &sc->sc_task_cache.run.terminal },
		{ "task_run_snapshot_valid", &sc->sc_task_cache.run.snapshot_valid },
		{ "task_run_step", &sc->sc_task_cache.run.step },
		{ "task_run_error", &sc->sc_task_cache.run.error },
		{ "task_run_last_readback", &sc->sc_task_cache.run.last_readback },
		{ "task_run_command_bytes", &sc->sc_task_cache.run.command_bytes },
		{ "task_run_irq_raw", &sc->sc_task_cache.run.irq_raw },
		{ "task_run_irq_bits", &sc->sc_task_cache.run.irq_bits },
		{ "task_run_irq_fault", &sc->sc_task_cache.run.irq_fault },
		{ "task_run_irq_mask_readback", &sc->sc_task_cache.run.irq_mask_readback },
		{ "task_run_terminal_status", &sc->sc_task_cache.run.terminal_status },
		{ "task_run_stale0", &sc->sc_task_cache.run.stale[0] },
		{ "task_run_stale1", &sc->sc_task_cache.run.stale[1] },
		{ "task_run_terminal_idle0", &sc->sc_task_cache.run.terminal_idle[0] },
		{ "task_run_terminal_idle1", &sc->sc_task_cache.run.terminal_idle[1] },
		{ "task_run_terminal_dma0", &sc->sc_task_cache.run.terminal_dma[0] },
		{ "task_run_terminal_dma1", &sc->sc_task_cache.run.terminal_dma[1] },
		{ "task_features_valid", &sc->sc_task_cache.run.task_features_valid },
		{ "task_compare_valid", &sc->sc_task_cache.compare.valid },
		{ "task_compare_pass", &sc->sc_task_cache.compare.pass },
		{ "task_model_pass", &sc->sc_task_cache.run.model_pass },
		{ "task_final_attempted", &sc->sc_task_cache.run.final_attempted },
		{ "task_final_valid", &sc->sc_task_cache.run.final_valid },
		{ "task_final_status", &sc->sc_task_cache.run.final_status },
		{ "task_final_mask0", &sc->sc_task_cache.run.final_mask[0] },
		{ "task_final_mask1", &sc->sc_task_cache.run.final_mask[1] },
		{ "task_final_idle0", &sc->sc_task_cache.run.final_idle[0] },
		{ "task_final_idle1", &sc->sc_task_cache.run.final_idle[1] },
		{ "task_final_dma0", &sc->sc_task_cache.run.final_dma[0] },
		{ "task_final_dma1", &sc->sc_task_cache.run.final_dma[1] },
		{ "task_command_va", &sc->sc_task_cache.memory.memory.command_virtual },
		{ "task_pd", &sc->sc_task_cache.memory.memory.pd_entry },
		{ "task_constants_va", &sc->sc_task_cache.memory.constants_virtual },
		{ "task_input_va", &sc->sc_task_cache.memory.input_virtual },
		{ "task_output_va", &sc->sc_task_cache.memory.output_virtual },
		{ "task_command_bytes", &sc->sc_task_command_bytes },
		{ "task_constants_bytes", &sc->sc_task_constants_bytes },
		{ "task_input_bytes", &sc->sc_task_input_bytes },
		{ "task_output_bytes", &sc->sc_task_output_bytes },
		{ "task_mismatch0", &sc->sc_task_cache.compare.mismatches[0] },
		{ "task_mismatch1", &sc->sc_task_cache.compare.mismatches[1] },
		{ "task_mismatch2", &sc->sc_task_cache.compare.mismatches[2] },
		{ "task_mismatch3", &sc->sc_task_cache.compare.mismatches[3] },
		{ "task_mismatch4", &sc->sc_task_cache.compare.mismatches[4] },
		{ "task_mismatch5", &sc->sc_task_cache.compare.mismatches[5] },
		{ "task_first0", &sc->sc_task_cache.compare.first[0] },
		{ "task_first1", &sc->sc_task_cache.compare.first[1] },
		{ "task_first2", &sc->sc_task_cache.compare.first[2] },
		{ "task_first3", &sc->sc_task_cache.compare.first[3] },
		{ "task_first4", &sc->sc_task_cache.compare.first[4] },
		{ "task_first5", &sc->sc_task_cache.compare.first[5] },
		{ "task_feature_s0_model", &sc->sc_task_cache.run.task_features[0][0] },
		{ "task_feature_s0_minor0", &sc->sc_task_cache.run.task_features[0][1] },
		{ "task_feature_s0_minor5", &sc->sc_task_cache.run.task_features[0][2] },
		{ "task_feature_s1_model", &sc->sc_task_cache.run.task_features[1][0] },
		{ "task_feature_s1_minor0", &sc->sc_task_cache.run.task_features[1][1] },
		{ "task_feature_s1_minor5", &sc->sc_task_cache.run.task_features[1][2] }
	};
	const struct {
		const char *name;
		uint64_t *data;
	} quads[] = {
		{ "task_mtlb", &sc->sc_task_cache.memory.memory.mtlb_address },
		{ "task_stlb", &sc->sc_task_cache.memory.memory.stlb_address },
		{ "task_command_dma", &sc->sc_task_cache.memory.memory.command_address },
		{ "task_constants_dma", &sc->sc_task_cache.memory.constants_address },
		{ "task_input_dma", &sc->sc_task_cache.memory.input_address },
		{ "task_output_dma", &sc->sc_task_cache.memory.output_address },
		{ "task_run_irq_count", &sc->sc_task_cache.run.irq_count },
		{ "task_run_irq_baseline", &sc->sc_task_cache.run.irq_baseline }
	};
	static const char * const raw_names[2][A733_NPU_RUN_NREGS] = {
		{
			"task_run_s0_r000",
			"task_run_s0_r004",
			"task_run_s0_r014",
			"task_run_s0_r024",
			"task_run_s0_r030",
			"task_run_s0_r388",
			"task_run_s0_r3a4",
			"task_run_s0_r3b4",
			"task_run_s0_r654",
			"task_run_s0_r664",
			"task_run_s0_r668",
			"task_run_s0_r66c",
			"task_run_s0_r090",
			"task_run_s0_r100",
			"task_run_s0_r104",
			"task_run_s0_r10c",
			"task_run_s0_r3a8",
			"task_run_s0_r414"
		},
		{
			"task_run_s1_r000",
			"task_run_s1_r004",
			"task_run_s1_r014",
			"task_run_s1_r024",
			"task_run_s1_r030",
			"task_run_s1_r388",
			"task_run_s1_r3a4",
			"task_run_s1_r3b4",
			"task_run_s1_r654",
			"task_run_s1_r664",
			"task_run_s1_r668",
			"task_run_s1_r66c",
			"task_run_s1_r090",
			"task_run_s1_r100",
			"task_run_s1_r104",
			"task_run_s1_r10c",
			"task_run_s1_r3a8",
			"task_run_s1_r414"
		}
	};

	/* No ANYWRITE flag: only the framework-authorized root may write. */
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "task_stage",
	    SYSCTL_DESCR("0 dry; 1 fixed TASK prepare; 2 one retained NN run"),
	    sun60i_npu_sysctl_task_stage, 0, NULL, 0,
	    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create TASK stage node\n");
	for (size_t i = 0; i < __arraycount(words); i++) {
		if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
		    CTLTYPE_INT, words[i].name,
		    SYSCTL_DESCR("cached fixed NPU TASK value"),
		    sun60i_npu_sysctl_engine_value, 0, words[i].data, 0,
		    CTL_CREATE, CTL_EOL) != 0)
			aprint_error_dev(sc->sc_dev,
			    "cannot create cached TASK node\n");
	}
	for (size_t i = 0; i < __arraycount(quads); i++) {
		if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
		    CTLTYPE_QUAD, quads[i].name,
		    SYSCTL_DESCR("cached fixed NPU TASK address or IRQ count"),
		    sun60i_npu_sysctl_engine_value, 0, quads[i].data, 0,
		    CTL_CREATE, CTL_EOL) != 0)
			aprint_error_dev(sc->sc_dev,
			    "cannot create TASK address node\n");
	}
	for (size_t row = 0; row < 2; row++) {
		for (size_t i = 0; i < A733_NPU_RUN_NREGS; i++) {
			if (sysctl_createv(NULL, 0, &rnode, NULL,
			    CTLFLAG_READONLY, CTLTYPE_INT, raw_names[row][i],
			    SYSCTL_DESCR("cached TASK pre-run raw peek32 sample"),
			    sun60i_npu_sysctl_engine_value, 0,
			    &sc->sc_task_cache.run.raw[row][i], 0,
			    CTL_CREATE, CTL_EOL) != 0)
				aprint_error_dev(sc->sc_dev,
				    "cannot create TASK raw node\n");
		}
	}
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
	sun60i_npu_sysctl_engine_setup(sc, rnode);
	sun60i_npu_sysctl_task_setup(sc, rnode);
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
	if (sysctl_createv(NULL, 0, &rnode, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "vipmajor",
		    SYSCTL_DESCR("character device major of /dev/vipcore"),
		    NULL, 0, &sc->sc_cmajor, 0,
		    CTL_CREATE, CTL_EOL) != 0)
		aprint_error_dev(sc->sc_dev, "cannot create vipmajor node\n");
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
	sc->sc_dmat = faa->faa_dmat;
	sc->sc_run_pass = true;
	mutex_init(&sc->sc_run_lock, MUTEX_DEFAULT, IPL_NONE);
	sun60i_npu_instance = sc;
	if (fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size) != 0 ||
	    addr != A733_NPU_BASE || size < A733_NPU_ID_SIZE)
		aprint_error_dev(self, "unexpected register window\n");
	sun60i_npu_sysctl_setup(sc);
	if (sun60i_npu_cmajor == -1) {
		devmajor_t bmajor = -1, cmajor = -1;

		if (devsw_attach("vipcore", NULL, &bmajor,
		    &sun60i_npu_cdevsw, &cmajor) != 0) {
			aprint_error_dev(self,
			    "cannot attach vipcore devsw\n");
		} else {
			sun60i_npu_cmajor = cmajor;
			sc->sc_cdev_attached = true;
		}
	} else
		sc->sc_cdev_attached = true;
	sc->sc_cmajor = sun60i_npu_cmajor;
	aprint_naive("\n");
	aprint_normal(": A733 NPU, /dev/vipcore major %d\n", sc->sc_cmajor);
}

CFATTACH_DECL_NEW(sun60i_a733_npu, sizeof(struct sun60i_npu_softc),
    sun60i_npu_match, sun60i_npu_attach, NULL, NULL);
