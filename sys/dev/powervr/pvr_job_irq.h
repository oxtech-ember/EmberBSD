/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_POWERVR_PVR_JOB_IRQ_H_
#define _DEV_POWERVR_PVR_JOB_IRQ_H_

#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/evcnt.h>
#include <sys/mutex.h>
#include <sys/workqueue.h>

struct pvr_job_irq_route {
	void *(*establish)(void *, int (*)(void *), void *);
	void (*mask)(void *, void *);
	void (*unmask)(void *, void *);
	void (*release)(void *, void *);
};

enum pvr_job_irq_count {
	PVR_IRQ_FIRED, PVR_IRQ_WAITS, PVR_IRQ_TIMEOUTS, PVR_IRQ_RACED,
	PVR_IRQ_REASSERT, PVR_IRQ_FAULTS, PVR_IRQ_ACTIVATED, PVR_IRQ_RETIRED,
	PVR_IRQ_NCOUNTS
};

struct pvr_job_irq {
	kmutex_t lock;
	kcondvar_t cv;
	device_t dev;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	const struct pvr_job_irq_route *route;
	void *route_arg, *cookie;
	struct workqueue *queue;
	struct work retire;
	struct evcnt count[PVR_IRQ_NCOUNTS];
	unsigned generation, strays, window;
	int fault;
	bool live, armed, wrapper_enable, retiring;
};

void pvr_job_irq_init(struct pvr_job_irq *, device_t);
void pvr_job_irq_fini(struct pvr_job_irq *);
int pvr_job_irq_start(struct pvr_job_irq *, bus_space_tag_t,
    bus_space_handle_t, const struct pvr_job_irq_route *, void *, bool);
void pvr_job_irq_stop(struct pvr_job_irq *);
bool pvr_job_irq_active(struct pvr_job_irq *);
unsigned pvr_job_irq_mark(void *);
void pvr_job_irq_wait(void *, unsigned, int);

/* Legacy explicit firmware probe. All snapshots are taken under the IRQ lock. */
int pvr_job_irq_probe_arm(void *);
int pvr_job_irq_probe(struct pvr_job_irq *, unsigned *, unsigned *, bool);
void pvr_job_irq_probe_wait(struct pvr_job_irq *, int);

#endif
