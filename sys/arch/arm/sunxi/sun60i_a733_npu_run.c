/* SPDX-License-Identifier: BSD-2-Clause AND MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; finite native PD/IRQ/FE stage. */
/*
 * Local init sequencing is adapted from VIP2 commit
 * 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f, vip_drv_hardware.c,
 * under its MIT license. Identity and capability facts also use
 * kernel/arch/gc_hal_kernel_hardware.c and kernel/inc/gc_feature_database.h
 * at that pin. The exact vip9000nano-di entry has eight INT8 cores and no BLT.
 * The dual-licensed files are used under MIT. No Linux IRQ code is copied.
 *
 * Copyright (c) 2017 - 2024 Vivante Corporation
 * Copyright (c) 2014 - 2024 Vivante Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <dev/ofw/openfirm.h>
#include <dev/fdt/fdtvar.h>
#include <arm/sunxi/sun60i_a733_ccu.h>
#include <arm/sunxi/sun60i_a733_pck600.h>
#include <arm/sunxi/sun60i_a733_npu_dma.h>
#include <arm/sunxi/sun60i_a733_npu_fe.h>
#include <arm/sunxi/sun60i_a733_npu_run.h>

static const bus_size_t npu_run_offsets[A733_NPU_RUN_NREGS] = {
	0x000, 0x004, 0x014, 0x024, 0x030, 0x388,
	0x3a4, 0x3b4, 0x654, 0x664, 0x668, 0x66c,
	0x090, 0x100, 0x104, 0x10c, 0x3a8, 0x414
};

static int
npu_run_ready(struct clk *clock, int power, const void *owner)
{
	int error;

	error = sun60i_a733_ccu_npu_ready(clock, owner);
	if (error == 0)
		error = sun60i_a733_pck_npu_ready(power, owner);
	return error;
}

/* Use the standard shared-capable FDT interface for this fixed device route. */
static int
npu_run_irq_route(int phandle)
{
	static const char * const compatible[] = { "arm,gic-v3", NULL };
	bus_addr_t addr;
	bus_size_t size;
	uint32_t cells[3], count;
	int parent;

	parent = fdtbus_intr_parent(phandle);
	if (parent <= 0 || !fdtbus_status_okay(phandle) ||
	    !fdtbus_status_okay(parent) || !of_compatible(parent, compatible) ||
	    OF_getproplen(parent, "interrupt-controller") != 0 ||
	    OF_getproplen(parent, "#interrupt-cells") != sizeof(count) ||
	    of_getprop_uint32_array(parent, "#interrupt-cells", &count, 1) != 0 ||
	    count != 3 || OF_getproplen(parent, "reg") != 16 ||
	    fdtbus_get_reg(parent, 0, &addr, &size) != 0 ||
	    addr != 0x03400000 || size != 0x10000 ||
	    fdtbus_get_reg(parent, 1, &addr, &size) != 0 ||
	    addr != 0x03460000 || size != 0x100000 ||
	    OF_getproplen(phandle, "interrupts-extended") >= 0 ||
	    OF_getproplen(phandle, "interrupts") != sizeof(cells) ||
	    of_getprop_uint32_array(phandle, "interrupts", cells, 3) != 0 ||
	    cells[0] != 0 || cells[1] != 65 || cells[2] != 4)
		return EINVAL;
	return 0;
}

static int
npu_run_snapshot(struct sun60i_a733_npu_run *s)
{
	struct sun60i_a733_npu_fe_snapshot sample = { 0 };
	int error;

	for (size_t row = 0; row < 2; row++) {
		for (size_t i = 0; i < A733_NPU_RUN_NREGS; i++) {
			if (bus_space_peek_4(s->bst, s->bsh, npu_run_offsets[i],
			    &s->status.raw[row][i]) != 0)
				return EFAULT;
		}
	}
	s->status.snapshot_valid = 1;
	if (memcmp(s->status.raw[0], s->status.raw[1],
	    sizeof(s->status.raw[0])) != 0)
		return EBUSY;
	for (size_t row = 0; row < 2; row++)
		memcpy(sample.words[row], s->status.raw[row],
		    sizeof(sample.words[row]));
	sample.valid = true;
	error = sun60i_a733_npu_fe_admit(&sample);
	if (error != 0)
		return error;
	if ((s->status.raw[1][0] & 0x80000) != 0 ||
	    (s->status.raw[1][15] & 1) != 0 ||
	    (s->status.raw[1][16] & ~0x10U) != 0)
		return EBUSY;
	return 0;
}

/* Exact HWDB identity precedes the fixed INT8/non-BLT capability profile. */
static int
npu_run_task_features(struct sun60i_a733_npu_run *s)
{
	static const bus_size_t offsets[A733_NPU_TASK_ID_WORDS] = {
		0x020, 0x024, 0x0a8, 0x0e8, 0x030, 0x020
	};
	static const uint32_t identity[] = {
		0x9000, 0x9202, 0x05090009, 0x08000000, 0x1000003b
	};

	/* Establish the known model before product/ECO register access. */
	for (size_t row = 0; row < 2; row++) {
		if (bus_space_peek_4(s->bst, s->bsh, 0x020,
		    &s->status.task_features[row][0]) != 0)
			return EFAULT;
		s->status.task_feature_read_mask[row] |= 1;
	}
	if (s->status.task_features[0][0] != 0x9000 ||
	    s->status.task_features[1][0] != 0x9000)
		return ENODEV;
	for (size_t row = 0; row < 2; row++) {
		for (size_t i = 0; i < A733_NPU_TASK_ID_WORDS; i++) {
			if (bus_space_peek_4(s->bst, s->bsh, offsets[i],
			    &s->status.task_identity[row][i]) != 0)
				return EFAULT;
			s->status.task_identity_read_mask[row] |= 1U << i;
			/* Each complete sample has its own model brackets. */
			if ((i == 0 || i == A733_NPU_TASK_ID_WORDS - 1) &&
			    s->status.task_identity[row][i] != 0x9000)
				return ENODEV;
		}
	}
	if (memcmp(s->status.task_identity[0], s->status.task_identity[1],
	    sizeof(s->status.task_identity[0])) != 0)
		return EBUSY;
	s->status.task_identity_valid = 1;
	/* Do not use the vendor database's masked-revision fallback. */
	if (memcmp(s->status.task_identity[1], identity, sizeof(identity)) != 0)
		return ENODEV;
	for (size_t row = 0; row < 2; row++) {
		if (bus_space_peek_4(s->bst, s->bsh, 0x034,
		    &s->status.task_features[row][1]) != 0)
			return EFAULT;
		s->status.task_feature_read_mask[row] |= 2;
	}
	if (s->status.task_features[0][1] != s->status.task_features[1][1])
		return EOPNOTSUPP;
	/* MORE guards raw minor5 access, not the separate HWDB capability. */
	if ((s->status.task_features[1][1] & 0x00200000) != 0) {
		for (size_t row = 0; row < 2; row++) {
			if (bus_space_peek_4(s->bst, s->bsh, 0x0a0,
			    &s->status.task_features[row][2]) != 0)
				return EFAULT;
			s->status.task_feature_read_mask[row] |= 4;
		}
		s->status.task_features_valid = 1;
		if (s->status.task_features[0][2] != s->status.task_features[1][2] ||
		    (s->status.task_features[1][2] & 0x80000000) != 0)
			return EOPNOTSUPP;
	}
	/* Source facts for this exact entry; these are not register samples. */
	s->status.task_capability_source = A733_NPU_TASK_CAP_HWDB;
	s->status.task_int8_cores = 8;
	s->status.task_blt = 0;
	s->status.task_profile_valid = 1;
	return 0;
}

/* Only the literal operation index is accepted. There is no register API. */
static int
npu_run_step(struct sun60i_a733_npu_run *s, unsigned op,
    const struct sun60i_a733_npu_dma_info *info)
{
	static const struct {
		bus_size_t offset;
		uint32_t value, mask;
	} steps[] = {
		{ 0x000, 0x00070900, UINT32_MAX },
		{ 0x000, 0x00070100, UINT32_MAX },
		{ 0x3a8, 2, UINT32_MAX ^ 0x10U },
		{ 0x100, 0x00140021, UINT32_MAX },
		{ 0x3b4, 0, UINT32_MAX },
		{ 0x388, 0x21, UINT32_MAX },
		{ 0x104, 0, UINT32_MAX },
		{ 0x10c, 0x20000, 0x30000 },
		{ 0x090, 0x40, 0x40 },
		{ 0x414, 0, 1 },
		{ 0x014, 0xc0000010, UINT32_MAX },
		{ 0x654, 0, UINT32_MAX },
		{ 0x3a4, 0x00010200, 0 }
	};
	uint32_t value, before, after;
	int error;

	if (op >= sizeof(steps) / sizeof(steps[0]))
		return EINVAL;
	value = steps[op].value;
	if (op == 4)
		value = info->pd_entry;
	if (op == 11)
		value = info->command_virtual;
	if (op == 2) {
		/* JOB_STATUS bit 4 is read-only. Keep the literal init write. */
		if (bus_space_peek_4(s->bst, s->bsh, steps[op].offset,
		    &before) != 0)
			return EFAULT;
		if ((before & ~0x10U) != 0)
			return EBUSY;
	} else if (steps[op].mask != UINT32_MAX && steps[op].mask != 0) {
		if (bus_space_peek_4(s->bst, s->bsh, steps[op].offset,
		    &before) != 0)
			return EFAULT;
		value |= before & ~steps[op].mask;
	}
	s->status.step = op + 1;
	error = bus_space_poke_4(s->bst, s->bsh, steps[op].offset, value);
	if (error != 0)
		return EFAULT;
	if (op == 11)
		bus_space_barrier(s->bst, s->bsh, 0x654, 4,
		    BUS_SPACE_BARRIER_WRITE);
	if (op == 12)
		bus_space_barrier(s->bst, s->bsh, 0x3a4, 4,
		    BUS_SPACE_BARRIER_WRITE);
	if (steps[op].mask != 0) {
		if (bus_space_peek_4(s->bst, s->bsh, steps[op].offset,
		    &after) != 0)
			return EFAULT;
		s->status.last_readback = after;
		if (op == 11) {
			/* The bounded observed profile permits no immediate echo. */
			if (after != 0 && after != value)
				return EIO;
		} else if ((after & steps[op].mask) !=
		    (value & steps[op].mask)) {
			return EIO;
		}
	}
	return 0;
}

static int
npu_run_irq(void *arg)
{
	struct sun60i_a733_npu_run *s = arg;
	uint32_t raw;

	mutex_enter(&s->irq_lock);
	if (!s->armed) {
		mutex_exit(&s->irq_lock);
		return 0;
	}
	/* Never use peek: it changes the interrupted LWP's onfault state. */
	raw = bus_space_read_4(s->bst, s->bsh, 0x010);
	if (raw == 0) {
		mutex_exit(&s->irq_lock);
		return 0;
	}
	s->status.irq_raw = raw;
	s->status.irq_bits |= raw;
	if (s->status.irq_count == UINT64_MAX)
		s->status.irq_fault = 1;
	else
		s->status.irq_count++;
	if (!s->status.started || raw != 0x10)
		s->status.irq_fault = 1;
	/* Drain the peripheral mask before GIC EOI can expose a level again. */
	bus_space_write_4(s->bst, s->bsh, 0x014, 0);
	bus_space_barrier(s->bst, s->bsh, 0x014, 4, BUS_SPACE_BARRIER_WRITE);
	s->status.irq_mask_readback = bus_space_read_4(s->bst, s->bsh, 0x014);
	if (s->status.irq_mask_readback != 0)
		s->status.irq_fault = 1;
	else
		s->armed = false;
	mutex_exit(&s->irq_lock);
	return 1;
}

static int
npu_run_mask(struct sun60i_a733_npu_run *s)
{
	uint32_t value = UINT32_MAX;
	int error;

	mutex_enter(&s->irq_lock);
	error = bus_space_poke_4(s->bst, s->bsh, 0x014, 0);
	if (error == 0) {
		bus_space_barrier(s->bst, s->bsh, 0x014, 4,
		    BUS_SPACE_BARRIER_WRITE);
		error = bus_space_peek_4(s->bst, s->bsh, 0x014, &value);
	}
	s->status.irq_mask_readback = value;
	if (error == 0 && value == 0)
		s->armed = false;
	else
		s->status.irq_fault = 1;
	mutex_exit(&s->irq_lock);
	return error != 0 ? EFAULT : value != 0 ? EIO : 0;
}

static int
npu_run_wait(struct sun60i_a733_npu_run *s, uint32_t va)
{
	for (unsigned poll = 0; poll < 100; poll++) {
		bool fault, event;

		mutex_enter(&s->irq_lock);
		fault = s->status.irq_fault != 0;
		event = s->status.irq_count > s->status.irq_baseline &&
		    s->status.irq_bits == 0x10;
		mutex_exit(&s->irq_lock);
		if (fault)
			return EIO;
		if (event) {
			for (size_t row = 0; row < 2; row++) {
				if (bus_space_peek_4(s->bst, s->bsh, 0x004,
				    &s->status.terminal_idle[row]) != 0 ||
				    bus_space_peek_4(s->bst, s->bsh, 0x664,
				    &s->status.terminal_dma[row]) != 0)
					return EFAULT;
			}
			if (s->status.terminal_idle[0] == 0x7fffffff &&
			    s->status.terminal_idle[1] == 0x7fffffff &&
			    s->status.terminal_dma[0] == s->status.terminal_dma[1] &&
			    s->status.terminal_dma[1] >= va &&
			    s->status.terminal_dma[1] - va < 4096)
				return 0;
		}
		delay(1000);
	}
	return ETIMEDOUT;
}

void
sun60i_a733_npu_run_status(struct sun60i_a733_npu_run *s,
    struct sun60i_a733_npu_run_status *out)
{
	if (s == NULL || out == NULL)
		return;
	if (s->lock_initialized)
		mutex_enter(&s->irq_lock);
	*out = s->status;
	if (s->lock_initialized)
		mutex_exit(&s->irq_lock);
}

static bool
npu_run_task_completed_locked(const struct sun60i_a733_npu_run *s,
    const struct sun60i_a733_npu_dma *dma)
{

	return s->profile == A733_NPU_DMA_PROFILE_TASK && s->task_retained &&
	    s->bound_dma == dma && s->mapped && s->ih != NULL && !s->armed &&
	    s->status.attempted == 1 && s->status.publication_attempted == 1 &&
	    s->status.retained == 1 && s->status.started == 1 &&
	    s->status.terminal == 1 && s->status.error == 0 &&
	    s->status.snapshot_valid == 1 && s->status.step == 13 &&
	    s->status.irq_raw == 0x10 &&
	    s->status.command_bytes == A733_NPU_NN_COMMAND_BYTES &&
	    s->status.irq_count > s->status.irq_baseline &&
	    s->status.irq_bits == 0x10 && s->status.irq_fault == 0 &&
	    s->status.irq_mask_readback == 0 && s->status.terminal_status == 0 &&
	    s->status.terminal_idle[0] == 0x7fffffff &&
	    s->status.terminal_idle[1] == 0x7fffffff &&
	    s->status.terminal_dma[0] == s->status.terminal_dma[1] &&
	    s->status.terminal_dma[1] >= 0x01001000 &&
	    s->status.terminal_dma[1] - 0x01001000 < 4096;
}

bool
sun60i_a733_npu_run_task_completed(struct sun60i_a733_npu_run *s,
    const struct sun60i_a733_npu_dma *dma)
{
	bool completed;

	if (s == NULL || dma == NULL || !s->lock_initialized)
		return false;
	mutex_enter(&s->irq_lock);
	completed = npu_run_task_completed_locked(s, dma);
	mutex_exit(&s->irq_lock);
	return completed;
}

int
sun60i_a733_npu_run_task_finish(struct sun60i_a733_npu_run *s,
    const struct sun60i_a733_npu_dma *dma, struct clk *clock, int power,
    const void *owner)
{
	int error;

	if (s == NULL || dma == NULL || clock == NULL || power <= 0 ||
	    owner == NULL || !s->lock_initialized)
		return EINVAL;
	mutex_enter(&s->irq_lock);
	if (s->status.final_attempted) {
		mutex_exit(&s->irq_lock);
		return EALREADY;
	}
	if (!npu_run_task_completed_locked(s, dma) ||
	    !sun60i_a733_npu_dma_task_verified(dma, s)) {
		mutex_exit(&s->irq_lock);
		return EBUSY;
	}
	s->status.final_attempted = 1;
	mutex_exit(&s->irq_lock);
	/* Clock and power helpers can sleep. Never call them under irq_lock. */
	error = npu_run_ready(clock, power, owner);
	mutex_enter(&s->irq_lock);
	if (error == 0 && (!npu_run_task_completed_locked(s, dma) ||
	    !sun60i_a733_npu_dma_task_verified(dma, s)))
		error = EIO;
	if (error == 0 && bus_space_peek_4(s->bst, s->bsh, 0x014,
	    &s->status.final_mask[0]) != 0)
		error = EFAULT;
	for (size_t row = 0; error == 0 && row < 2; row++) {
		if (bus_space_peek_4(s->bst, s->bsh, 0x004,
		    &s->status.final_idle[row]) != 0 ||
		    bus_space_peek_4(s->bst, s->bsh, 0x664,
		    &s->status.final_dma[row]) != 0)
			error = EFAULT;
	}
	if (error == 0 && bus_space_peek_4(s->bst, s->bsh, 0x014,
	    &s->status.final_mask[1]) != 0)
		error = EFAULT;
	/* ACK is last: a late error cannot hide behind cached completion. */
	if (error == 0 && bus_space_peek_4(s->bst, s->bsh, 0x010,
	    &s->status.final_status) != 0)
		error = EFAULT;
	if (error == 0) {
		s->status.final_valid = 1;
		if (s->status.final_mask[0] != 0 || s->status.final_mask[1] != 0 ||
		    s->status.final_idle[0] != 0x7fffffff ||
		    s->status.final_idle[1] != 0x7fffffff ||
		    s->status.final_dma[0] != s->status.final_dma[1] ||
		    s->status.final_dma[1] < 0x01001000 ||
		    s->status.final_dma[1] - 0x01001000 >= 4096 ||
		    s->status.final_status != 0 || s->status.irq_fault != 0)
			error = EIO;
	}
	/* The final reads and success publication have the same IRQ lock. */
	if (error == 0)
		s->status.model_pass = 1;
	else
		s->status.error = error;
	mutex_exit(&s->irq_lock);
	return error;
}

static int
npu_run_once(struct sun60i_a733_npu_run *s, bus_space_tag_t bst,
    struct clk *clock, int power, const void *owner, int phandle,
    const char *name, struct sun60i_a733_npu_dma *dma,
    const struct sun60i_a733_npu_dma_task_reference *reference)
{
	struct sun60i_a733_npu_dma_info info;
	uint8_t image[A733_NPU_FE_RUN_BYTES];
	bool mask_done = false, task = reference != NULL;
	int error, mask_error;

	if (s == NULL || bst == NULL || clock == NULL || power <= 0 ||
	    owner == NULL || phandle <= 0 || name == NULL || dma == NULL)
		return EINVAL;
	if (s->status.attempted)
		return EALREADY;
	s->status.attempted = 1;
	error = npu_run_irq_route(phandle);
	if (error != 0)
		goto out;
	error = sun60i_a733_npu_dma_info(dma, &info);
	if (error != 0)
		goto out;
	if (info.profile != (task ? A733_NPU_DMA_PROFILE_TASK :
	    A733_NPU_DMA_PROFILE_FE) ||
	    info.retained || info.command_virtual != 0x01001000 ||
	    info.command_address == info.command_virtual ||
	    info.command_bytes != (task ? A733_NPU_NN_COMMAND_BYTES : 16) ||
	    info.pd_entry != ((info.mtlb_address >> 12) << 4 | 1)) {
		error = EINVAL;
		goto out;
	}
	error = npu_run_ready(clock, power, owner);
	if (error != 0)
		goto out;
	s->bst = bst;
	mutex_init(&s->irq_lock, MUTEX_DEFAULT, IPL_VM);
	s->lock_initialized = true;
	error = bus_space_map(bst, 0x03600000, 0x1000, 0, &s->bsh);
	if (error != 0)
		goto out;
	s->mapped = true;
	error = npu_run_snapshot(s);
	if (error == 0 && task)
		error = npu_run_task_features(s);
	if (error == 0)
		error = npu_run_ready(clock, power, owner);
	if (error != 0)
		goto out;
	if (task) {
		if (!s->status.task_profile_valid ||
		    s->status.task_capability_source != A733_NPU_TASK_CAP_HWDB ||
		    s->status.task_int8_cores != 8 || s->status.task_blt != 0) {
			error = EOPNOTSUPP;
			goto out;
		}
		error = sun60i_a733_npu_dma_task_validate(dma, reference);
	} else {
		error = sun60i_a733_npu_fe_run_image(image, sizeof(image));
		if (error == 0)
			error = sun60i_a733_npu_dma_set_commands(dma, image,
			    sizeof(image));
	}
	if (error != 0)
		goto out;
	s->status.command_bytes = task ? A733_NPU_NN_COMMAND_BYTES : sizeof(image);
	error = npu_run_ready(clock, power, owner);
	if (error != 0)
		goto out;
	/* Native DMA retain sets its sticky flag before the first PREWRITE sync. */
	s->profile = task ? A733_NPU_DMA_PROFILE_TASK : A733_NPU_DMA_PROFILE_FE;
	s->bound_dma = dma;
	s->status.publication_attempted = 1;
	if (task) {
		/* Retain the native context and references before the first sync. */
		s->task_retained = true;
		error = sun60i_a733_npu_dma_task_retain_checked(dma, s, reference);
	} else {
		error = sun60i_a733_npu_dma_retain(dma);
	}
	if (sun60i_a733_npu_dma_info(dma, &info) == 0)
		s->status.retained = info.retained;
	if (error == 0 && !s->status.retained)
		error = EIO;
	if (error != 0)
		goto out;
	s->ih = fdtbus_intr_establish_xname(phandle, 0, IPL_VM,
	    FDT_INTR_MPSAFE, npu_run_irq, s, name);
	if (s->ih == NULL) {
		error = ENXIO;
		goto out;
	}
	error = npu_run_ready(clock, power, owner);
	if (error != 0)
		goto out;
	for (unsigned op = 0; op < 10; op++) {
		error = npu_run_step(s, op, &info);
		if (error != 0)
			goto out;
	}
	mutex_enter(&s->irq_lock);
	if (bus_space_peek_4(bst, s->bsh, 0x010, &s->status.stale[0]) != 0 ||
	    bus_space_peek_4(bst, s->bsh, 0x010, &s->status.stale[1]) != 0)
		error = EFAULT;
	else if (s->status.stale[1] != 0)
		error = EBUSY;
	s->status.irq_baseline = s->status.irq_count;
	if (error == 0)
		s->armed = true;
	mutex_exit(&s->irq_lock);
	if (error != 0)
		goto out;
	error = npu_run_step(s, 10, &info);
	if (error == 0)
		error = npu_run_step(s, 11, &info);
	if (error == 0)
		error = npu_run_ready(clock, power, owner);
	if (error != 0)
		goto out;
	mutex_enter(&s->irq_lock);
	if (s->status.irq_fault || s->status.irq_count != s->status.irq_baseline)
		error = EIO;
	else {
		s->status.started = 1;
		error = npu_run_step(s, 12, &info);
	}
	mutex_exit(&s->irq_lock);
	if (error == 0)
		error = npu_run_wait(s, info.command_virtual);
	mask_done = true;
	mask_error = npu_run_mask(s);
	if (error == 0)
		error = mask_error;
	if (error == 0) {
		mutex_enter(&s->irq_lock);
		if (bus_space_peek_4(bst, s->bsh, 0x010,
		    &s->status.terminal_status) != 0)
			error = EFAULT;
		else if (s->status.terminal_status != 0 || s->status.irq_fault)
			error = EIO;
		mutex_exit(&s->irq_lock);
	}
	if (error == 0)
		s->status.terminal = 1;
out:
	if (!s->status.publication_attempted) {
		if (s->ih != NULL) {
			fdtbus_intr_disestablish(phandle, s->ih);
			s->ih = NULL;
		}
		if (s->mapped) {
			bus_space_unmap(bst, s->bsh, 0x1000);
			s->mapped = false;
		}
	} else if (s->status.step != 0 && !mask_done) {
		/* No reset/free follows an attempted publication, including errors. */
		mask_error = npu_run_mask(s);
		if (error == 0)
			error = mask_error;
	}
	s->status.error = error;
	return error;
}

int
sun60i_a733_npu_run_once(struct sun60i_a733_npu_run *s, bus_space_tag_t bst,
    struct clk *clock, int power, const void *owner, int phandle,
    const char *name, struct sun60i_a733_npu_dma *dma)
{

	return npu_run_once(s, bst, clock, power, owner, phandle, name, dma, NULL);
}

int
sun60i_a733_npu_run_task_once(struct sun60i_a733_npu_run *s, bus_space_tag_t bst,
    struct clk *clock, int power, const void *owner, int phandle,
    const char *name, struct sun60i_a733_npu_dma *dma,
    const struct sun60i_a733_npu_dma_task_reference *reference)
{

	if (reference == NULL)
		return EINVAL;
	return npu_run_once(s, bst, clock, power, owner, phandle, name, dma,
	    reference);
}
