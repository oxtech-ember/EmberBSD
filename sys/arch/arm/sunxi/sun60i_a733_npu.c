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
	kmutex_t sc_run_lock;
	struct sysctllog *sc_sysctllog;
	u_int sc_run_stage;
	u_int sc_run_result;
	uint64_t sc_pid, sc_rev, sc_date;
	bool sc_run_pass;
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
	struct clk *clk, *mod;
	bool retained = false;
	int error;

	/* The module clock's reset parent is the dead pll-npu: move the
	 * mux to the running PERIPH0-600M before any enable walks up. */
	mod = fdtbus_clock_get(sc->sc_phandle, "clk_npu");
	if (mod == NULL)
		return ENODEV;
	error = sun60i_a733_ccu_npu_prepare(mod, sc, &retained);
	clk_put(mod);
	if (error != 0)
		return error;

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

/* One bounded register access through the powered NPU window. */
static int
sun60i_npu_vip_read_reg(struct sun60i_npu_softc *sc,
    struct vip_drv_reg *reg)
{
	bus_space_handle_t bsh;
	uint32_t value;
	int error;

	if (reg->offset == 0 || reg->offset > A733_NPU_ID_SIZE - 4 ||
	    (reg->offset & 3) != 0)
		return VIP_STATUS_INVALID;
	error = bus_space_map(sc->sc_bst, A733_NPU_BASE,
	    A733_NPU_ID_SIZE, 0, &bsh);
	if (error != 0)
		return VIP_STATUS_IO;
	if (bus_space_peek_4(sc->sc_bst, bsh, reg->offset, &value) != 0) {
		bus_space_unmap(sc->sc_bst, bsh, A733_NPU_ID_SIZE);
		return VIP_STATUS_IO;
	}
	bus_space_unmap(sc->sc_bst, bsh, A733_NPU_ID_SIZE);
	reg->value = value;
	return VIP_STATUS_SUCCESS;
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
