/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_POWERVR_PVR_JOB_IRQ_FDT_H_
#define _DEV_POWERVR_PVR_JOB_IRQ_FDT_H_

#include <dev/powervr/pvr_job_irq.h>

enum pvr_job_irq_profile { PVR_JOB_IRQ_GE8300, PVR_JOB_IRQ_A733 };

struct pvr_job_irq_fdt {
	struct pvr_job_irq irq;
	int phandle;
};

int pvr_job_irq_fdt_start(struct pvr_job_irq_fdt *, int, bus_space_tag_t,
    bus_space_handle_t, enum pvr_job_irq_profile);
#endif
