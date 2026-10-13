/* Origin: EmberBSD bounded per-device G2 USB transport, 2026-10-14. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/ucomvar.h>
#include <dev/usb/umoxa_g2var.h>
#include <dev/usb/umoxa_frame.h>

static uint64_t
umoxa_g2_now(void)
{
	struct timespec t;

	getnanouptime(&t);
	return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void
umoxa_g2_notify(struct umoxa_g2_softc *sc, uint32_t mask)
{
	struct ucom_softc *child;
	uint64_t epoch;
	int error;
	unsigned p;

	for (p = 0; p < 4; p++) {
		if (!(mask & (1U << p)))
			continue;
		mutex_enter(&sc->sc_lock);
		child = sc->sc_port[p].sc_child;
		epoch = sc->sc_port[p].sc_epoch;
		error = sc->sc_txq.ports[p].error;
		sc->sc_port[p].sc_fault = error;
		sc->sc_port[p].sc_paused = true;
		sc->sc_flow_dirty |= 1U << p;
		mutex_exit(&sc->sc_lock);
		if (child != NULL)
			ucom_transport_fault(child, epoch, error);
	}
}

void
umoxa_g2_fault_port(struct umoxa_g2_softc *sc, unsigned p, int error)
{
	uint32_t mask;

	mutex_enter(&sc->sc_lock);
	mask = umoxa_g2_txq_fault_port(&sc->sc_txq, p, error);
	mutex_exit(&sc->sc_lock);
	umoxa_g2_notify(sc, mask);
}

static void
umoxa_g2_shared_fault(struct umoxa_g2_softc *sc, int error)
{
	uint32_t mask;

	mutex_enter(&sc->sc_lock);
	mask = umoxa_g2_txq_fault_all(&sc->sc_txq, error);
	mutex_exit(&sc->sc_lock);
	umoxa_g2_notify(sc, mask);
}

static void
umoxa_g2_input(void *arg, uint16_t p, const uint8_t *data, size_t size)
{
	struct umoxa_g2_softc *sc = arg;
	struct ucom_softc *child;
	uint64_t epoch;
	int error;

	mutex_enter(&sc->sc_lock);
	child = sc->sc_txq.ports[p].state == UMOXA_OPEN ?
	    sc->sc_port[p].sc_child : NULL;
	epoch = sc->sc_port[p].sc_epoch;
	mutex_exit(&sc->sc_lock);
	if (child != NULL) {
		error = ucom_transport_input(child, epoch, data, size);
		if (error)
			umoxa_g2_fault_port(sc, p, error);
	}
}

static void
umoxa_g2_event(void *arg, uint16_t p, uint16_t event, const uint8_t *detail)
{
	struct umoxa_g2_softc *sc = arg;
	struct ucom_softc *child = NULL;

	if (event != 3 && event != 4)
		return;
	mutex_enter(&sc->sc_lock);
	if (sc->sc_txq.ports[p].state == UMOXA_OPEN) {
		if (event == 3)
			sc->sc_port[p].sc_msr = detail[2] & 0xf0;
		else
			sc->sc_port[p].sc_lsr = detail[0] & ULSR_RCV_MASK;
		child = sc->sc_port[p].sc_child;
	}
	mutex_exit(&sc->sc_lock);
	if (child != NULL)
		ucom_status_change(child);
}

static usbd_status
umoxa_g2_transfer(struct umoxa_g2_softc *sc, unsigned pipe, uint16_t flags,
    uint32_t timeout, uint32_t *actual)
{
	usbd_status status;

	/* The bulk helper clears STALL even for TIMEOUT, with an unbounded extra
	 * control request. Empty polling must not reset endpoint data toggles. */
	usbd_setup_xfer(sc->sc_xfer[pipe], NULL, sc->sc_buf[pipe], *actual,
	    flags, timeout, NULL);
	status = usbd_sync_transfer(sc->sc_xfer[pipe]);
	usbd_get_xfer_status(sc->sc_xfer[pipe], NULL, NULL, actual, NULL);
	/* STALL faults the shared stream; recovery requires close/reconnect. */
	return status;
}

static int
umoxa_g2_read(struct umoxa_g2_softc *sc, unsigned pipe, uint32_t *actual)
{
	usbd_status status;
	uint32_t mask;
	int error;

	*actual = sc->sc_layout.packet;
	status = umoxa_g2_transfer(sc, pipe, USBD_SHORT_XFER_OK, 10, actual);
	if (status == USBD_TIMEOUT && *actual == 0)
		return 0;
	if (status != USBD_NORMAL_COMPLETION || *actual > sc->sc_layout.packet) {
		umoxa_g2_shared_fault(sc, EIO);
		return EIO;
	}
	if (pipe == UMOXA_G2_PIPE_RX) {
		error = umoxa_g2_rx_data(sc->sc_buf[pipe], *actual,
		    sc->sc_layout.packet, umoxa_g2_input, sc);
		if (error)
			umoxa_g2_shared_fault(sc, error);
	} else {
		mutex_enter(&sc->sc_lock);
		error = umoxa_g2_txq_events(&sc->sc_txq, sc->sc_buf[pipe], *actual, &mask);
		mutex_exit(&sc->sc_lock);
		umoxa_g2_notify(sc, mask);
		if (!error)
			error = umoxa_g2_rx_events(sc->sc_buf[pipe], *actual, umoxa_g2_event, sc);
	}
	return error;
}

int
umoxa_g2_drain(struct umoxa_g2_softc *sc)
{
	uint32_t data, events;
	unsigned i;
	int error;

	/* A reopened owner stays CLOSED while both shared streams are drained. */
	for (i = 0; i < 8; i++) {
		error = umoxa_g2_read(sc, UMOXA_G2_PIPE_RX, &data);
		if (!error)
			error = umoxa_g2_read(sc, UMOXA_G2_PIPE_EVENT, &events);
		if (error)
			return error;
		if (data == 0 && events == 0)
			return 0;
	}
	return EBUSY;
}

void
umoxa_g2_kick(struct umoxa_g2_softc *sc)
{
	KASSERT(mutex_owned(&sc->sc_lock));
	if (!sc->sc_ready || sc->sc_dying || sc->sc_gate || sc->sc_queued || sc->sc_wq == NULL)
		return;
	sc->sc_queued = true;
	workqueue_enqueue(sc->sc_wq, &sc->sc_work, NULL);
}

static void
umoxa_g2_poll(void *arg)
{
	struct umoxa_g2_softc *sc = arg;

	mutex_enter(&sc->sc_lock);
	umoxa_g2_kick(sc);
	mutex_exit(&sc->sc_lock);
}

void
umoxa_g2_gate(struct umoxa_g2_softc *sc)
{
	KASSERT(mutex_owned(&sc->sc_ctl_lock));
	mutex_enter(&sc->sc_lock);
	sc->sc_gate = true;
	mutex_exit(&sc->sc_lock);
	if (sc->sc_poll_initialized)
		callout_halt(&sc->sc_poll, NULL);
	if (sc->sc_wq != NULL)
		workqueue_wait(sc->sc_wq, &sc->sc_work);
}

void
umoxa_g2_ungate(struct umoxa_g2_softc *sc)
{
	KASSERT(mutex_owned(&sc->sc_ctl_lock));
	mutex_enter(&sc->sc_lock);
	sc->sc_gate = false;
	umoxa_g2_kick(sc);
	mutex_exit(&sc->sc_lock);
}

static void
umoxa_g2_worker(struct work *w, void *arg)
{
	struct umoxa_g2_softc *sc = arg;
	struct umoxa_g2_xfer x;
	struct umoxa_g2_result r;
	struct ucom_softc *child;
	uint32_t mask, actual;
	unsigned p;
	int error, picked;
	bool paused, run, poll = false;
	usbd_status status;

	mutex_enter(&sc->sc_lock);
	run = sc->sc_ready && !sc->sc_dying && !sc->sc_gate;
	mutex_exit(&sc->sc_lock);
	if (!run)
		goto out;
	for (p = 0; p < 4; p++) {
		mutex_enter(&sc->sc_lock);
		run = (sc->sc_flow_dirty & (1U << p)) != 0;
		paused = sc->sc_port[p].sc_paused;
		sc->sc_flow_dirty &= ~(1U << p);
		mutex_exit(&sc->sc_lock);
		if (run) {
			error = sc->sc_io.control(sc, 0, 7, !paused, p, NULL, 0);
			if (error)
				umoxa_g2_fault_port(sc, p, error);
		}
	}
	mutex_enter(&sc->sc_lock);
	(void)umoxa_g2_txq_tick(&sc->sc_txq, umoxa_g2_now(), &mask);
	mutex_exit(&sc->sc_lock);
	umoxa_g2_notify(sc, mask);
	mutex_enter(&sc->sc_lock);
	picked = umoxa_g2_txq_pick(&sc->sc_txq, umoxa_g2_now(), &x);
	if (!picked)
		memcpy(sc->sc_buf[UMOXA_G2_PIPE_TX], x.data, x.len);
	mutex_exit(&sc->sc_lock);
	umoxa_g2_notify(sc, x.fault_mask);
	if (!picked) {
		actual = x.len;
		status = umoxa_g2_transfer(sc, UMOXA_G2_PIPE_TX, 0, 1000, &actual);
		/* Partial shared writes can desynchronise framing for every port. */
		error = status != USBD_NORMAL_COMPLETION || actual != x.len ? EIO : 0;
		mutex_enter(&sc->sc_lock);
		(void)umoxa_g2_txq_done(&sc->sc_txq, x.token, actual, error, &r);
		child = r.deliver ? sc->sc_port[r.port].sc_child : NULL;
		mutex_exit(&sc->sc_lock);
		umoxa_g2_notify(sc, r.fault_mask);
		if (child != NULL && !r.error)
			ucom_transport_done(child, r.epoch, r.cookie, r.actual, 0);
	}
	if (umoxa_g2_read(sc, UMOXA_G2_PIPE_RX, &actual) == 0)
		(void)umoxa_g2_read(sc, UMOXA_G2_PIPE_EVENT, &actual);
out:
	mutex_enter(&sc->sc_lock);
	sc->sc_queued = false;
	for (p = 0; p < 4; p++)
		poll |= sc->sc_txq.ports[p].state == UMOXA_OPEN;
	if (sc->sc_ready && !sc->sc_dying && !sc->sc_gate && (poll || sc->sc_flow_dirty))
		callout_schedule(&sc->sc_poll, MAX(1, hz / 50));
	mutex_exit(&sc->sc_lock);
}

int
umoxa_g2_io_init(struct umoxa_g2_softc *sc)
{
	uint8_t ep[3] = {sc->sc_layout.data_in, sc->sc_layout.out, sc->sc_layout.event_in};
	unsigned i;
	int error;

	callout_init(&sc->sc_poll, CALLOUT_MPSAFE);
	callout_setfunc(&sc->sc_poll, umoxa_g2_poll, sc);
	sc->sc_poll_initialized = true;
	for (i = 0; i < 3; i++) {
		error = usbd_open_pipe(sc->sc_iface, ep[i], USBD_EXCLUSIVE_USE, &sc->sc_pipe[i]);
		if (error != USBD_NORMAL_COMPLETION)
			return EIO;
		error = usbd_create_xfer(sc->sc_pipe[i], sc->sc_layout.packet, 0, 0, &sc->sc_xfer[i]);
		if (error)
			return error;
		sc->sc_buf[i] = usbd_get_buffer(sc->sc_xfer[i]);
	}
	return workqueue_create(&sc->sc_wq, device_xname(sc->sc_dev),
	    umoxa_g2_worker, sc, PRI_NONE, IPL_SOFTUSB, WQ_MPSAFE);
}

void
umoxa_g2_io_fini(struct umoxa_g2_softc *sc)
{
	unsigned i;

	mutex_enter(&sc->sc_ctl_lock);
	mutex_enter(&sc->sc_lock);
	sc->sc_dying = sc->sc_gate = true;
	sc->sc_ready = false;
	mutex_exit(&sc->sc_lock);
	if (sc->sc_poll_initialized)
		callout_halt(&sc->sc_poll, NULL);
	for (i = 0; i < 3; i++)
		if (sc->sc_pipe[i] != NULL)
			usbd_abort_pipe(sc->sc_pipe[i]);
	if (sc->sc_wq != NULL) {
		workqueue_wait(sc->sc_wq, &sc->sc_work);
		workqueue_destroy(sc->sc_wq);
		sc->sc_wq = NULL;
	}
	for (i = 0; i < 3; i++) {
		if (sc->sc_xfer[i] != NULL) {
			usbd_destroy_xfer(sc->sc_xfer[i]);
			sc->sc_xfer[i] = NULL;
			sc->sc_buf[i] = NULL;
		}
		if (sc->sc_pipe[i] != NULL) {
			usbd_close_pipe(sc->sc_pipe[i]);
			sc->sc_pipe[i] = NULL;
		}
	}
	if (sc->sc_poll_initialized) {
		callout_destroy(&sc->sc_poll);
		sc->sc_poll_initialized = false;
	}
	mutex_exit(&sc->sc_ctl_lock);
}
