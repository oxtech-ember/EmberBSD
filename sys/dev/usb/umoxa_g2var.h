/* Origin: EmberBSD experimental G2 USB-parent private state, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_USB_UMOXA_G2VAR_H_
#define _DEV_USB_UMOXA_G2VAR_H_
#include <sys/callout.h>
#include <sys/workqueue.h>
#include <dev/usb/umoxa_g2_protocol.h>
#include <dev/usb/umoxa_g2_txq.h>
#define UMOXA_G2_PIPE_RX 0
#define UMOXA_G2_PIPE_TX 1
#define UMOXA_G2_PIPE_EVENT 2
struct umoxa_g2_uart {
	device_t sc_subdev;
	struct ucom_softc *sc_child;
	struct umoxa_g2_settings sc_settings;
	uint64_t sc_epoch;
	u_char sc_lsr, sc_msr;
	bool sc_open, sc_paused;
	int sc_fault;
};
struct umoxa_g2_softc {
	device_t sc_dev;
	struct usbd_device *sc_udev;
	struct usbd_interface *sc_iface;
	struct umoxa_g2_layout sc_layout;
	struct umoxa_g2_io sc_io;
	struct umoxa_g2_txq sc_txq;
	struct umoxa_g2_uart sc_port[4];
	struct usbd_pipe *sc_pipe[3];
	struct usbd_xfer *sc_xfer[3];
	uint8_t *sc_buf[3];
	struct workqueue *sc_wq;
	struct work sc_work;
	callout_t sc_poll;
	kmutex_t sc_lock, sc_ctl_lock;
	uint32_t sc_flow_dirty;
	bool sc_gate, sc_queued, sc_ready, sc_dying;
	bool sc_locks_initialized, sc_poll_initialized;
};
extern const struct ucom_methods umoxag_methods;
int umoxa_g2_io_init(struct umoxa_g2_softc *);
void umoxa_g2_io_fini(struct umoxa_g2_softc *);
/* Gate/ungate require the process control lock and never hold the state lock. */
void umoxa_g2_gate(struct umoxa_g2_softc *);
void umoxa_g2_ungate(struct umoxa_g2_softc *);
/* kick requires the state lock; it neither sleeps nor calls child methods. */
void umoxa_g2_kick(struct umoxa_g2_softc *);
int umoxa_g2_drain(struct umoxa_g2_softc *);
void umoxa_g2_fault_port(struct umoxa_g2_softc *, unsigned, int);
#endif
