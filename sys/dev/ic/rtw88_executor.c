/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* One native MPSAFE workqueue per device. The attach owner pins lifetime. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/workqueue.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw88_executor.h>

enum rtw88_diag_state {
	RTW88_DIAG_EMPTY, RTW88_DIAG_PENDING, RTW88_DIAG_ACTIVE,
	RTW88_DIAG_COMPLETE
};

struct rtw88_executor {
	kmutex_t lock;
	struct workqueue *wq;
	struct work work;
	struct rtw88_softc *sc;
	rtw88_executor_service_fn service;
	struct lwp *owner;
	uint32_t reasons;
	unsigned callers;
	bool queued, stopping, draining, waiter;
	enum rtw88_diag_state diag_state;
	uint64_t serial;
	struct rtw88_control_snapshot request;
	struct rtw88_control_result result;
};

static void
rtw88_executor_work(struct work *work, void *arg)
{
	struct rtw88_executor *e = arg;

	(void)work;
	mutex_enter(&e->lock);
	e->owner = curlwp;
	for (;;) {
		uint32_t reasons = e->reasons;

		e->reasons = 0;
		if (reasons == 0) {
			e->owner = NULL;
			e->queued = false;
			mutex_exit(&e->lock);
			return;
		}
		mutex_exit(&e->lock);
		e->service(e->sc, reasons);
		mutex_enter(&e->lock);
	}
}

int
rtw88_executor_create(struct rtw88_softc *sc, rtw88_executor_service_fn service)
{
	struct rtw88_executor *e;
	int error;

	if (sc == NULL || service == NULL)
		return EINVAL;
	if (sc->sc_executor != NULL)
		return EBUSY;
	e = kmem_zalloc(sizeof(*e), KM_SLEEP);
	e->sc = sc;
	e->service = service;
	mutex_init(&e->lock, MUTEX_DEFAULT, IPL_NET);
	error = workqueue_create(&e->wq, "rtw88io", rtw88_executor_work, e,
	    PRI_SOFTNET, IPL_NET, WQ_MPSAFE);
	if (error != 0) {
		mutex_destroy(&e->lock);
		kmem_free(e, sizeof(*e));
		return error;
	}
	sc->sc_executor = e;
	return 0;
}

void
rtw88_executor_schedule(struct rtw88_softc *sc, uint32_t reasons)
{
	struct rtw88_executor *e;

	if (sc == NULL || (e = sc->sc_executor) == NULL || reasons == 0)
		return;
	mutex_enter(&e->lock);
	if (!e->stopping) {
		e->reasons |= reasons;
		if (!e->queued && !e->draining) {
			e->queued = true;
			workqueue_enqueue(e->wq, &e->work, NULL);
		}
	}
	mutex_exit(&e->lock);
}

int
rtw88_executor_owned(struct rtw88_softc *sc)
{
	struct rtw88_executor *e;
	int owned;

	if (sc == NULL || (e = sc->sc_executor) == NULL)
		return 0;
	mutex_enter(&e->lock);
	owned = e->owner != NULL && e->owner == curlwp;
	mutex_exit(&e->lock);
	return owned;
}

/* workqueue_wait requires no concurrent enqueue. While draining, record
 * new reasons without queueing and service them in another pass. Callers
 * still must revoke producers before using this as a lifetime fence. */
int
rtw88_executor_drain(struct rtw88_softc *sc)
{
	struct rtw88_executor *e;
	int nlocks = 0, self;

	if (sc == NULL || (e = sc->sc_executor) == NULL)
		return EINVAL;
	mutex_enter(&e->lock);
	self = e->owner == curlwp;
	if (self || e->draining || e->stopping) {
		mutex_exit(&e->lock);
		return self ? EDEADLK : EBUSY;
	}
	e->draining = true;
	mutex_exit(&e->lock);
	KERNEL_UNLOCK_ALL(NULL, &nlocks);
	for (;;) {
		workqueue_wait(e->wq, &e->work);
		mutex_enter(&e->lock);
		if (e->reasons == 0) {
			e->draining = false;
			mutex_exit(&e->lock);
			break;
		}
		KASSERT(!e->queued);
		e->queued = true;
		workqueue_enqueue(e->wq, &e->work, NULL);
		mutex_exit(&e->lock);
	}
	KERNEL_LOCK(nlocks, NULL);
	return 0;
}

int
rtw88_executor_destroy(struct rtw88_softc *sc)
{
	struct rtw88_executor *e;
	unsigned callers;
	int nlocks = 0;

	if (sc == NULL || (e = sc->sc_executor) == NULL)
		return EINVAL;
	mutex_enter(&e->lock);
	if (e->owner == curlwp || e->draining) {
		int error = e->owner == curlwp ? EDEADLK : EBUSY;
		mutex_exit(&e->lock);
		return error;
	}
	e->stopping = true;
	mutex_exit(&e->lock);
	KERNEL_UNLOCK_ALL(NULL, &nlocks);
	workqueue_wait(e->wq, &e->work);
	/* Existing callers must observe cancellation before their storage dies. */
	do {
		mutex_enter(&e->lock);
		callers = e->callers;
		mutex_exit(&e->lock);
		if (callers != 0)
			(void)kpause("rtwclose", false, 1, NULL);
	} while (callers != 0);
	workqueue_destroy(e->wq);
	sc->sc_executor = NULL;
	mutex_destroy(&e->lock);
	kmem_free(e, sizeof(*e));
	KERNEL_LOCK(nlocks, NULL);
	return 0;
}

int
rtw88_executor_call(struct rtw88_softc *sc,
    const struct rtw88_control_snapshot *request,
    struct rtw88_control_result *result, unsigned timeout_ms)
{
	struct rtw88_executor *e;
	uint64_t ticks;
	uint32_t started;
	int nlocks = 0, error;

	if (sc == NULL || request == NULL || result == NULL ||
	    request->kind != RTW88_CTL_DIAG || timeout_ms == 0 ||
	    (e = sc->sc_executor) == NULL)
		return EINVAL;
	ticks = ((uint64_t)timeout_ms * hz + 999) / 1000;
	if (ticks == 0 || ticks > INT_MAX)
		return EINVAL;
	mutex_enter(&e->lock);
	if (e->owner == curlwp)
		error = EDEADLK;
	else if (e->stopping)
		error = ECANCELED;
	else if (e->diag_state != RTW88_DIAG_EMPTY)
		error = EBUSY;
	else if (e->serial == UINT64_MAX)
		error = EOVERFLOW;
	else
		error = 0;
	if (error != 0) {
		mutex_exit(&e->lock);
		return error;
	}
	e->serial++;
	e->request = *request;
	e->diag_state = RTW88_DIAG_PENDING;
	e->waiter = true;
	e->callers++;
	mutex_exit(&e->lock);
	KERNEL_UNLOCK_ALL(NULL, &nlocks);
	started = (uint32_t)getticks();
	rtw88_executor_schedule(sc, RTW88_EXEC_CONTROL);
	mutex_enter(&e->lock);
	for (;;) {
		if (e->stopping) {
			error = ECANCELED;
			break;
		}
		if (e->diag_state == RTW88_DIAG_COMPLETE) {
			*result = e->result;
			error = e->result.error;
			e->diag_state = RTW88_DIAG_EMPTY;
			break;
		}
		if ((uint32_t)((uint32_t)getticks() - started) >= ticks) {
			error = ETIMEDOUT;
			break;
		}
		mutex_exit(&e->lock);
		(void)kpause("rtwdiag", false, 1, NULL);
		mutex_enter(&e->lock);
	}
	e->waiter = false;
	if (e->diag_state == RTW88_DIAG_PENDING ||
	    e->diag_state == RTW88_DIAG_COMPLETE)
		e->diag_state = RTW88_DIAG_EMPTY;
	e->callers--;
	mutex_exit(&e->lock);
	KERNEL_LOCK(nlocks, NULL);
	return error;
}

int
rtw88_executor_diag_take(struct rtw88_softc *sc,
    struct rtw88_control_snapshot *request, uint64_t *serial)
{
	struct rtw88_executor *e;
	int error;

	if (sc == NULL || request == NULL || serial == NULL ||
	    (e = sc->sc_executor) == NULL)
		return EINVAL;
	mutex_enter(&e->lock);
	if (e->owner != curlwp)
		error = EPERM;
	else if (e->stopping || e->diag_state != RTW88_DIAG_PENDING)
		error = EAGAIN;
	else {
		*request = e->request;
		*serial = e->serial;
		e->diag_state = RTW88_DIAG_ACTIVE;
		error = 0;
	}
	mutex_exit(&e->lock);
	return error;
}

int
rtw88_executor_diag_done(struct rtw88_softc *sc, uint64_t serial,
    const struct rtw88_control_result *result)
{
	struct rtw88_executor *e;
	int error;

	if (sc == NULL || result == NULL || (e = sc->sc_executor) == NULL)
		return EINVAL;
	mutex_enter(&e->lock);
	if (e->owner != curlwp)
		error = EPERM;
	else if (e->diag_state != RTW88_DIAG_ACTIVE || serial != e->serial ||
	    result->id.generation != e->request.id.generation ||
	    result->id.epoch != e->request.id.epoch ||
	    result->id.key_epoch != e->request.id.key_epoch)
		error = ESTALE;
	else {
		e->result = *result;
		e->diag_state = e->waiter ? RTW88_DIAG_COMPLETE : RTW88_DIAG_EMPTY;
		error = 0;
	}
	mutex_exit(&e->lock);
	return error;
}
