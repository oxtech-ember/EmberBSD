/* Origin: EmberBSD experimental G2 ucom child/lifecycle controls, 2026-10-14. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/termios.h>
#include <sys/tty.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/ucomvar.h>
#include <dev/usb/umoxa_g2io.h>
#include <dev/usb/umoxa_g2var.h>

static int
umoxag_uct_attach(void *arg, int p, struct ucom_softc *child)
{
	struct umoxa_g2_softc *sc = arg;

	if (p < 0 || p >= 4 || child == NULL || sc->sc_port[p].sc_child != NULL)
		return EINVAL;
	sc->sc_port[p].sc_child = child;
	return 0;
}

static void
umoxag_uct_detach(void *arg, int p)
{
	struct umoxa_g2_softc *sc = arg;

	if (p < 0 || p >= 4)
		return;
	mutex_enter(&sc->sc_ctl_lock);
	umoxa_g2_gate(sc);
	mutex_enter(&sc->sc_lock);
	sc->sc_port[p].sc_child = NULL;
	mutex_exit(&sc->sc_lock);
	umoxa_g2_ungate(sc);
	mutex_exit(&sc->sc_ctl_lock);
}

static int
umoxag_uct_start(void *arg, int p, uint64_t epoch)
{
	struct umoxa_g2_softc *sc = arg;
	uint8_t msr = 0;
	int error;

	if (p < 0 || p >= 4 || epoch == 0)
		return EINVAL;
	mutex_enter(&sc->sc_ctl_lock);
	umoxa_g2_gate(sc);
	mutex_enter(&sc->sc_lock);
	error = !sc->sc_ready || sc->sc_dying ? ENXIO :
	    sc->sc_port[p].sc_open ? EBUSY : 0;
	mutex_exit(&sc->sc_lock);
	if (error)
		goto out;
	error = umoxa_g2_quiesce(&sc->sc_io, p);
	if (error == 0)
		error = umoxa_g2_drain(sc);
	if (error == 0)
		error = umoxa_g2_prepare(&sc->sc_io, p, &sc->sc_port[p].sc_settings);
	if (error == 0)
		error = umoxa_g2_enable(&sc->sc_io, p);
	if (error == 0)
		error = sc->sc_io.control(sc, 1, 0x86, 0, p, &msr, 1);
	mutex_enter(&sc->sc_lock);
	if (error == 0)
		error = umoxa_g2_txq_open(&sc->sc_txq, p, epoch);
	if (error == 0) {
		sc->sc_port[p].sc_epoch = epoch;
		sc->sc_port[p].sc_open = true;
		sc->sc_port[p].sc_paused = false;
		sc->sc_port[p].sc_submitted = false;
		sc->sc_port[p].sc_fault = 0;
		sc->sc_port[p].sc_lsr = 0;
		sc->sc_port[p].sc_msr = msr & 0xf0;
	}
	mutex_exit(&sc->sc_lock);
	if (error)
		(void)umoxa_g2_quiesce(&sc->sc_io, p);
	else
		ucom_status_change(sc->sc_port[p].sc_child);
out:
	umoxa_g2_ungate(sc);
	mutex_exit(&sc->sc_ctl_lock);
	return error;
}

static void
umoxag_uct_stop(void *arg, int p, uint64_t epoch)
{
	struct umoxa_g2_softc *sc = arg;
	int error = 0;
	bool close;

	if (p < 0 || p >= 4)
		return;
	mutex_enter(&sc->sc_ctl_lock);
	mutex_enter(&sc->sc_lock);
	close = sc->sc_port[p].sc_open && sc->sc_port[p].sc_epoch == epoch;
	if (close) {
		(void)umoxa_g2_txq_close(&sc->sc_txq, p);
		sc->sc_port[p].sc_open = false;
		sc->sc_flow_dirty &= ~(1U << p);
	}
	mutex_exit(&sc->sc_lock);
	umoxa_g2_gate(sc);
	if (close && !sc->sc_dying)
		error = umoxa_g2_quiesce(&sc->sc_io, p);
	if (error) {
		mutex_enter(&sc->sc_lock);
		sc->sc_port[p].sc_fault = error;
		mutex_exit(&sc->sc_lock);
		aprint_error_dev(sc->sc_dev, "port %d close failed (%d)\n", p, error);
	}
	umoxa_g2_ungate(sc);
	mutex_exit(&sc->sc_ctl_lock);
}

static int
umoxag_uct_submit(void *arg, int p, uint64_t epoch, uint64_t cookie,
    const uint8_t *data, size_t size)
{
	struct umoxa_g2_softc *sc = arg;
	int error;

	if (p < 0 || p >= 4)
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	error = !sc->sc_ready || sc->sc_dying ? ENXIO :
	    umoxa_g2_txq_submit(&sc->sc_txq, p, epoch, cookie, data, size);
	if (error == 0) {
		sc->sc_port[p].sc_submitted = true;
		umoxa_g2_kick(sc);
	}
	mutex_exit(&sc->sc_lock);
	return error;
}

static void
umoxag_uct_flow(void *arg, int p, uint64_t epoch, int paused)
{
	struct umoxa_g2_softc *sc = arg;

	if (p < 0 || p >= 4)
		return;
	mutex_enter(&sc->sc_lock);
	if (!sc->sc_dying && sc->sc_port[p].sc_open &&
	    sc->sc_port[p].sc_epoch == epoch) {
		sc->sc_port[p].sc_paused = paused != 0;
		sc->sc_flow_dirty |= 1U << p;
		umoxa_g2_kick(sc);
	}
	mutex_exit(&sc->sc_lock);
}

static int
umoxag_uct_set(void *arg, int p, int which, int on)
{
	struct umoxa_g2_softc *sc = arg;
	int error;

	if (p < 0 || p >= 4)
		return EINVAL;
	mutex_enter(&sc->sc_ctl_lock);
	umoxa_g2_gate(sc);
	/* ucom sets logical modem state before start, including non-RS232 opens. */
	if (!sc->sc_dying && !sc->sc_port[p].sc_open &&
	    (which == UCOM_SET_DTR || which == UCOM_SET_RTS) && (on == 0 || on == 1))
		error = 0;
	else
		error = sc->sc_dying ? ENXIO : umoxa_g2_set(&sc->sc_io, p,
		    which, on, &sc->sc_port[p].sc_settings);
	if (error == 0 && which != UCOM_SET_BREAK) {
		mutex_enter(&sc->sc_lock);
		if (on)
			sc->sc_port[p].sc_settings.mcr |= which == UCOM_SET_DTR ? 1 : 2;
		else
			sc->sc_port[p].sc_settings.mcr &= ~(which == UCOM_SET_DTR ? 1 : 2);
		mutex_exit(&sc->sc_lock);
	}
	if (error == EIO)
		umoxa_g2_fault_port(sc, p, error);
	umoxa_g2_ungate(sc);
	mutex_exit(&sc->sc_ctl_lock);
	return error;
}

static void
umoxag_status(void *arg, int p, u_char *lsr, u_char *msr)
{
	struct umoxa_g2_softc *sc = arg;

	*lsr = *msr = 0;
	if (p < 0 || p >= 4)
		return;
	mutex_enter(&sc->sc_lock);
	*lsr = sc->sc_port[p].sc_lsr;
	*msr = sc->sc_port[p].sc_msr;
	mutex_exit(&sc->sc_lock);
}

static int
umoxag_param(void *arg, int p, struct termios *t)
{
	struct umoxa_g2_softc *sc = arg;
	struct umoxa_g2_settings s;
	uint8_t line[4], baud[4];
	int error;

	if (p < 0 || p >= 4 || t->c_ospeed == 0 ||
	    (t->c_ispeed != 0 && t->c_ispeed != t->c_ospeed) ||
	    (t->c_cflag & (CDTRCTS | MDMBUF)))
		return EINVAL;
	mutex_enter(&sc->sc_ctl_lock);
	s = sc->sc_port[p].sc_settings;
	s.baud = t->c_ospeed;
	s.bits = (t->c_cflag & CSIZE) == CS5 ? 5 :
	    (t->c_cflag & CSIZE) == CS6 ? 6 : (t->c_cflag & CSIZE) == CS7 ? 7 : 8;
	s.parity = !(t->c_cflag & PARENB) ? 0 : t->c_cflag & PARODD ? 1 : 2;
	s.stops = t->c_cflag & CSTOPB ? 2 : 1;
	s.rtscts = (t->c_cflag & CRTSCTS) != 0;
	error = umoxa_g2_config(&s, line, baud);
	if (error == 0) {
		umoxa_g2_gate(sc);
		error = sc->sc_dying ? ENXIO : umoxa_g2_prepare(&sc->sc_io, p, &s);
		if (error == 0) {
			mutex_enter(&sc->sc_lock);
			sc->sc_port[p].sc_settings = s;
			mutex_exit(&sc->sc_lock);
		} else if (error == EIO) {
			umoxa_g2_fault_port(sc, p, error);
		}
		umoxa_g2_ungate(sc);
	}
	mutex_exit(&sc->sc_ctl_lock);
	return error;
}

static int
umoxag_ioctl(void *arg, int p, u_long cmd, void *data, int flags, proc_t *proc)
{
	struct umoxa_g2_softc *sc = arg;
	struct umoxa_g2_settings settings;
	bool open;
	int mode, error = 0;

	if (p < 0 || p >= 4)
		return EINVAL;
	if (cmd == UMOXA_G2_GET_FAULT) {
		mutex_enter(&sc->sc_lock);
		*(int *)data = sc->sc_port[p].sc_fault;
		mutex_exit(&sc->sc_lock);
		return 0;
	}
	if (cmd != UMOXA_G2_SET_MODE)
		return EPASSTHROUGH;
	mode = *(int *)data;
	if (mode < 0 || mode > 3)
		return EINVAL;
	mutex_enter(&sc->sc_ctl_lock);
	umoxa_g2_gate(sc);
	mutex_enter(&sc->sc_lock);
	open = sc->sc_port[p].sc_open;
	settings = sc->sc_port[p].sc_settings;
	if (sc->sc_dying)
		error = ENXIO;
	else if (open && (sc->sc_txq.ports[p].state != UMOXA_OPEN ||
	    sc->sc_txq.ports[p].job != UMOXA_EMPTY || sc->sc_port[p].sc_submitted))
		error = EBUSY;
	mutex_exit(&sc->sc_lock);
	settings.mode = mode;
	if (!error && open && !ucom_transport_idle(sc->sc_port[p].sc_child))
		error = EBUSY;
	if (!error && mode != 0 && settings.rtscts)
		error = EINVAL;
	if (!error && open)
		error = umoxa_g2_prepare(&sc->sc_io, p, &settings);
	if (!error) {
		mutex_enter(&sc->sc_lock);
		sc->sc_port[p].sc_settings = settings;
		mutex_exit(&sc->sc_lock);
	} else if (error == EIO) {
		umoxa_g2_fault_port(sc, p, error);
	}
	umoxa_g2_ungate(sc);
	mutex_exit(&sc->sc_ctl_lock);
	return error;
}

static int
umoxag_fault_ioctl(void *arg, int p, u_long cmd, void *data, int flags, proc_t *proc)
{
	struct umoxa_g2_softc *sc = arg;
	bool dying;

	if (cmd != UMOXA_G2_GET_FAULT)
		return EPASSTHROUGH;
	mutex_enter(&sc->sc_lock);
	dying = sc->sc_dying;
	mutex_exit(&sc->sc_lock);
	return dying ? ENXIO : umoxag_ioctl(arg, p, cmd, data, flags, proc);
}

static const struct ucom_transport_methods umoxag_transport = {
	.uct_attach = umoxag_uct_attach, .uct_start = umoxag_uct_start,
	.uct_stop = umoxag_uct_stop, .uct_submit = umoxag_uct_submit,
	.uct_rx_flow = umoxag_uct_flow, .uct_set = umoxag_uct_set,
	.uct_detach = umoxag_uct_detach
};
const struct ucom_methods umoxag_methods = {
	.ucom_get_status = umoxag_status, .ucom_param = umoxag_param,
	.ucom_ioctl = umoxag_ioctl, .ucom_transport = &umoxag_transport,
	.ucom_param_strict = true, .ucom_fault_ioctl = umoxag_fault_ioctl
};
