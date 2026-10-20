/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; fixed native NN coordinator, no vendor code. */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_npu_mmu.h>
#include <arm/sunxi/sun60i_a733_npu_nn.h>
#include <arm/sunxi/sun60i_a733_npu_task.h>

struct sun60i_a733_npu_task {
	struct sun60i_a733_npu_dma_task_reference reference;
	struct sun60i_a733_npu_run run;
	struct sun60i_a733_npu_dma *dma;
	struct sun60i_a733_npu_task_status status;
};

int
sun60i_a733_npu_task_destroy(struct sun60i_a733_npu_task *t)
{

	if (t == NULL)
		return EINVAL;
	if (t->run.task_retained || t->run.status.publication_attempted ||
	    t->status.retained)
		return EBUSY;
	if (t->dma != NULL) {
		int error;

		error = sun60i_a733_npu_dma_destroy(t->dma);
		if (error != 0) {
			t->status.release_error = error;
			return error;
		}
		t->dma = NULL;
	}
	if (t->run.lock_initialized)
		mutex_destroy(&t->run.irq_lock);
	kmem_free(t, sizeof(*t));
	return 0;
}

int
sun60i_a733_npu_task_prepare(bus_dma_tag_t parent,
    struct sun60i_a733_npu_task **out)
{
	struct sun60i_a733_npu_mmu_page pages[4];
	struct sun60i_a733_npu_mmu_plan plan;
	struct sun60i_a733_npu_task *t;
	struct sun60i_a733_npu_dma_task_info *info;
	int error, release_error;

	if (parent == NULL || out == NULL ||
	    sizeof(*out) > UINTPTR_MAX - (uintptr_t)out)
		return EINVAL;
	t = kmem_zalloc(sizeof(*t), KM_SLEEP);
	if (t == NULL)
		return ENOMEM;
	error = sun60i_a733_npu_nn_tiny_image(&t->reference.image,
	    sizeof(t->reference.image));
	if (error == 0)
		error = sun60i_a733_npu_nn_task_commands(t->reference.commands,
		    sizeof(t->reference.commands));
	if (error != 0)
		goto fail;
	error = sun60i_a733_npu_dma_task_prepare(parent,
	    t->reference.commands, sizeof(t->reference.commands),
	    t->reference.image.constants, sizeof(t->reference.image.constants),
	    t->reference.image.input, sizeof(t->reference.image.input), &t->dma);
	if (error != 0)
		goto fail;
	info = &t->status.memory;
	error = sun60i_a733_npu_dma_task_info(t->dma, info);
	if (error != 0)
		goto fail;
	pages[0] = (struct sun60i_a733_npu_mmu_page){
		info->memory.command_address, 0
	};
	pages[1] = (struct sun60i_a733_npu_mmu_page){ info->constants_address, 0 };
	pages[2] = (struct sun60i_a733_npu_mmu_page){ info->input_address, 0 };
	pages[3] = (struct sun60i_a733_npu_mmu_page){
		info->output_address, A733_NPU_MMU_WRITABLE
	};
	plan = (struct sun60i_a733_npu_mmu_plan){
		.mtlb_physical = info->memory.mtlb_address,
		.stlb_physical = info->memory.stlb_address,
		.virtual_base = info->memory.command_virtual,
		.pages = pages, .page_count = 4
	};
	/* The existing builder is the only table format implementation. */
	error = sun60i_a733_npu_mmu_build(&plan, t->reference.mtlb,
	    sizeof(t->reference.mtlb), t->reference.stlb,
	    sizeof(t->reference.stlb));
	if (error == 0)
		error = sun60i_a733_npu_dma_task_validate(t->dma, &t->reference);
	if (error != 0)
		goto fail;
	t->status.prepared = 1;
	*out = t;
	return 0;
fail:
	/* If release fails, keep every asset and give the caller its pointer. */
	t->status.result = error;
	release_error = sun60i_a733_npu_task_destroy(t);
	if (release_error != 0)
		*out = t;
	return error;
}

int
sun60i_a733_npu_task_run(struct sun60i_a733_npu_task *t, bus_space_tag_t bst,
    struct clk *clock, int power, const void *owner, int phandle,
    const char *name)
{
	int error, info_error;

	if (t == NULL || !t->status.prepared)
		return EINVAL;
	if (t->status.attempted)
		return EALREADY;
	t->status.attempted = 1;
	error = sun60i_a733_npu_run_task_once(&t->run, bst, clock, power, owner,
	    phandle, name, t->dma, &t->reference);
	info_error = sun60i_a733_npu_dma_task_info(t->dma, &t->status.memory);
	t->status.retained = t->run.task_retained ||
	    (info_error == 0 && t->status.memory.memory.retained);
	if (error == 0)
		error = info_error;
	if (error == 0)
		error = sun60i_a733_npu_dma_task_check(t->dma, &t->run,
		    &t->reference, &t->status.compare);
	if (error == 0)
		error = sun60i_a733_npu_run_task_finish(&t->run, t->dma, clock,
		    power, owner);
	t->status.result = error;
	return error;
}

void
sun60i_a733_npu_task_status(struct sun60i_a733_npu_task *t,
    struct sun60i_a733_npu_task_status *out)
{
	uintptr_t a = (uintptr_t)t, b = (uintptr_t)out;

	if (t == NULL || out == NULL || sizeof(*t) > UINTPTR_MAX - a ||
	    sizeof(*out) > UINTPTR_MAX - b ||
	    (a < b + sizeof(*out) && b < a + sizeof(*t)))
		return;
	*out = t->status;
	sun60i_a733_npu_run_status(&t->run, &out->run);
}
