/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/intr.h>
#include <sys/systm.h>

#include <dev/powervr/pvr_job_irq.h>
#include <dev/powervr/pvr_mips_irq.h>

static const char *const pvr_irq_names[PVR_IRQ_NCOUNTS] = {
	[PVR_IRQ_FIRED] = "mips irq fired",
	[PVR_IRQ_WAITS] = "mips irq waits",
	[PVR_IRQ_TIMEOUTS] = "mips irq timeouts",
	[PVR_IRQ_RACED] = "mips irq raced",
	[PVR_IRQ_REASSERT] = "mips irq reassert",
	[PVR_IRQ_FAULTS] = "mips irq faults",
	[PVR_IRQ_ACTIVATED] = "mips irq activated",
	[PVR_IRQ_RETIRED] = "mips irq retired",
};

static void
pvr_job_irq_retire(struct work *work, void *arg)
{
	struct pvr_job_irq *irq = arg;
	void *cookie;
	int fault;

	(void)work;
	mutex_enter(&irq->lock);
	cookie = irq->cookie;
	fault = irq->fault;
	mutex_exit(&irq->lock);
	KASSERT(cookie != NULL);
	irq->route->release(irq->route_arg, cookie);
	mutex_enter(&irq->lock);
	irq->cookie = NULL;
	irq->count[PVR_IRQ_RETIRED].ev_count++;
	mutex_exit(&irq->lock);
	if (fault != 0)
		aprint_error_dev(irq->dev,
		    "firmware IRQ retired (%d); using bounded job polling\n",
		    fault);
}

/* Owner must keep the register mapping and GPU power valid until stop returns. */
static void
pvr_job_irq_disable_locked(struct pvr_job_irq *irq, int fault)
{
	KASSERT(mutex_owned(&irq->lock));
	if (irq->cookie == NULL || irq->retiring)
		return;
	irq->live = false;
	irq->retiring = true;
	if (fault != 0) {
		irq->fault = fault;
		irq->count[PVR_IRQ_FAULTS].ev_count++;
	}
	irq->route->mask(irq->route_arg, irq->cookie);
	if (irq->wrapper_enable &&
	    pvr_mips_irq_enable(irq->bst, irq->bsh, false) != 0) {
		irq->fault = EIO;
		irq->count[PVR_IRQ_FAULTS].ev_count++;
	}
	cv_broadcast(&irq->cv);
	workqueue_enqueue(irq->queue, &irq->retire, NULL);
}

static int
pvr_job_irq_intr(void *arg)
{
	struct pvr_job_irq *irq = arg;
	bool owned = false, pending;
	int error;

	mutex_enter(&irq->lock);
	if (!irq->live)
		goto out;
	error = pvr_mips_irq_pending(irq->bst, irq->bsh, &owned);
	if (error != 0 || !owned) {
		if (error != 0 || ++irq->strays >= 8)
			pvr_job_irq_disable_locked(irq, error != 0 ? error : EIO);
		goto out;
	}
	irq->strays = 0;
	for (unsigned i = 0; i < 4; i++) {
		error = pvr_mips_irq_clear(irq->bst, irq->bsh);
		if (error != 0)
			break;
		/* GE8300 retains the firmware-owned wrapper and legacy W1C. */
		pending = false;
		if (irq->wrapper_enable)
			error = pvr_mips_irq_pending(irq->bst, irq->bsh,
			    &pending);
		if (error != 0)
			break;
		if (!pending) {
			irq->generation++;
			irq->count[PVR_IRQ_FIRED].ev_count++;
			if (irq->armed && irq->window < INT_MAX)
				irq->window++;
			cv_broadcast(&irq->cv);
			goto out;
		}
		irq->count[PVR_IRQ_REASSERT].ev_count++;
	}
	pvr_job_irq_disable_locked(irq, error != 0 ? error : EIO);
out:
	mutex_exit(&irq->lock);
	return owned;
}

void
pvr_job_irq_init(struct pvr_job_irq *irq, device_t dev)
{
	irq->dev = dev;
	mutex_init(&irq->lock, MUTEX_DEFAULT, IPL_VM);
	cv_init(&irq->cv, "pvrjob");
	for (unsigned i = 0; i < PVR_IRQ_NCOUNTS; i++)
		evcnt_attach_dynamic(&irq->count[i],
		    i == PVR_IRQ_FIRED ? EVCNT_TYPE_INTR : EVCNT_TYPE_MISC,
		    NULL, device_xname(dev), pvr_irq_names[i]);
}

int
pvr_job_irq_start(struct pvr_job_irq *irq, bus_space_tag_t bst,
    bus_space_handle_t bsh, const struct pvr_job_irq_route *route, void *arg,
    bool wrapper_enable)
{
	bool pending;
	int error;

	KASSERT(irq->queue == NULL && irq->cookie == NULL);
	error = workqueue_create(&irq->queue, "pvrirq",
	    pvr_job_irq_retire, irq, PRI_NONE, IPL_VM, WQ_MPSAFE);
	if (error != 0)
		return error;
	irq->bst = bst;
	irq->bsh = bsh;
	irq->route = route;
	irq->route_arg = arg;
	irq->wrapper_enable = wrapper_enable;
	irq->cookie = route->establish(arg, pvr_job_irq_intr, irq);
	if (irq->cookie == NULL) {
		workqueue_destroy(irq->queue);
		irq->queue = NULL;
		return ENXIO;
	}
	mutex_enter(&irq->lock);
	if (wrapper_enable &&
	    (pvr_mips_irq_enable(bst, bsh, false) != 0 ||
	     pvr_mips_irq_clear(bst, bsh) != 0 ||
	     pvr_mips_irq_pending(bst, bsh, &pending) != 0 || pending ||
	     pvr_mips_irq_enable(bst, bsh, true) != 0)) {
		pvr_job_irq_disable_locked(irq, EIO);
		error = EIO;
	} else {
		irq->live = true;
		irq->count[PVR_IRQ_ACTIVATED].ev_count++;
		route->unmask(arg, irq->cookie);
	}
	mutex_exit(&irq->lock);
	if (error != 0)
		pvr_job_irq_stop(irq);
	return error;
}

/* Thread context. Do not call under an IRQ lock. Caller serializes start/stop. */
void
pvr_job_irq_stop(struct pvr_job_irq *irq)
{
	bool retiring;

	mutex_enter(&irq->lock);
	pvr_job_irq_disable_locked(irq, 0);
	retiring = irq->retiring;
	mutex_exit(&irq->lock);
	if (retiring) {
		workqueue_wait(irq->queue, &irq->retire);
		workqueue_destroy(irq->queue);
		irq->queue = NULL;
		irq->retiring = false;
	}
}

void
pvr_job_irq_fini(struct pvr_job_irq *irq)
{
	/* Backend users must already have drained before destroying wait state. */
	pvr_job_irq_stop(irq);
	for (unsigned i = 0; i < PVR_IRQ_NCOUNTS; i++)
		evcnt_detach(&irq->count[i]);
	cv_destroy(&irq->cv);
	mutex_destroy(&irq->lock);
}

bool
pvr_job_irq_active(struct pvr_job_irq *irq)
{
	bool live;

	mutex_enter(&irq->lock);
	live = irq->live;
	mutex_exit(&irq->lock);
	return live;
}

unsigned
pvr_job_irq_mark(void *arg)
{
	struct pvr_job_irq *irq = arg;
	unsigned mark;

	mutex_enter(&irq->lock);
	mark = irq->generation;
	mutex_exit(&irq->lock);
	return mark;
}

void
pvr_job_irq_wait(void *arg, unsigned mark, int ticks)
{
	struct pvr_job_irq *irq = arg;
	bool live;

	mutex_enter(&irq->lock);
	live = irq->live;
	if (live && irq->generation != mark) {
		irq->count[PVR_IRQ_RACED].ev_count++;
	} else if (live) {
		irq->count[PVR_IRQ_WAITS].ev_count++;
		if (cv_timedwait(&irq->cv, &irq->lock, ticks) == EWOULDBLOCK &&
		    irq->live && irq->generation == mark)
			irq->count[PVR_IRQ_TIMEOUTS].ev_count++;
	}
	mutex_exit(&irq->lock);
	if (!live)
		(void)kpause("pvrjob", false, ticks, NULL);
}

int
pvr_job_irq_probe_arm(void *arg)
{
	struct pvr_job_irq *irq = arg;
	bool pending;
	int error;

	mutex_enter(&irq->lock);
	error = irq->live ? pvr_mips_irq_pending(irq->bst, irq->bsh, &pending)
	    : (irq->fault != 0 ? irq->fault : ENXIO);
	if (error == 0 && pending)
		error = pvr_mips_irq_clear(irq->bst, irq->bsh);
	if (error == 0) {
		irq->window = 0;
		irq->armed = true;
	} else if (irq->live) {
		pvr_job_irq_disable_locked(irq, error);
	}
	mutex_exit(&irq->lock);
	return error;
}

int
pvr_job_irq_probe(struct pvr_job_irq *irq, unsigned *total, unsigned *window,
    bool finish)
{
	int error;

	mutex_enter(&irq->lock);
	error = irq->fault;
	if (total != NULL)
		*total = MIN(irq->count[PVR_IRQ_FIRED].ev_count, INT_MAX);
	if (window != NULL)
		*window = irq->window;
	if (finish)
		irq->armed = false;
	mutex_exit(&irq->lock);
	return error;
}

void
pvr_job_irq_probe_wait(struct pvr_job_irq *irq, int ticks)
{
	mutex_enter(&irq->lock);
	if (irq->armed && irq->live && irq->window == 0)
		(void)cv_timedwait(&irq->cv, &irq->lock, ticks);
	mutex_exit(&irq->lock);
}
