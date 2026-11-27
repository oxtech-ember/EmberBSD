/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Origin: EmberBSD (AI-assisted).
 * Host contract for the second address attempt on a root hub port.  After a
 * timed-out Address Device command, xhci(4) restarts its command ring and
 * returns USBD_REENUMERATE.  The hub resets the port and addresses the device
 * once more, but only when the same high-speed device is still connected.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#ifndef __CTASSERT
#define __CTASSERT(e) _Static_assert(e, #e)
#endif
#include "usb.h"

typedef enum {
	USBD_NORMAL_COMPLETION, USBD_IOERROR, USBD_TIMEOUT, USBD_REENUMERATE
} usbd_status;

#define USB_SPEED_FULL 2
#define USB_SPEED_HIGH 3
#define USB_ROOTHUB_INDEX 1
#define USBD_RESTART_MAX 5
#define aprint_normal_dev(dev, ...) ((void)(dev))
#define aprint_error_dev(dev, ...) ((void)(dev))
#define usbd_errstr(e) ((void)(e), "error")

struct usbd_bus;
struct usbd_device {
	int ud_depth;
	struct usbd_bus *ud_bus;
};
struct usbd_bus {
	struct usbd_device *ub_devices[2];
};
struct usbd_port {
	struct usbd_device *up_dev;
	int up_reattach;
	unsigned up_restartcnt;
};
struct uhub_softc {
	int sc_dev;
};

/* Model of one root hub port and the request log. */
static struct {
	int port;
	unsigned status, change;	/* status and change words */
	unsigned status_after_reset, change_after_reset;
	bool reset_fails;
	usbd_status second;		/* result of the second address */
	int resets, addresses, drops, explores, warm_clears;
} m;
static struct usbd_device child;

static usbd_status
usbd_get_port_status(struct usbd_device *hub, int port, usb_port_status_t *ps)
{
	(void)hub;
	assert(port == m.port);
	USETW(ps->wPortStatus, m.status);
	USETW(ps->wPortChange, m.change);
	return USBD_NORMAL_COMPLETION;
}

static usbd_status
usbd_reset_port(struct usbd_device *hub, int port, usb_port_status_t *ps)
{
	assert(port == m.port);
	m.resets++;
	if (m.reset_fails)
		return USBD_IOERROR;
	m.status = m.status_after_reset;
	m.change = m.change_after_reset;
	return usbd_get_port_status(hub, port, ps);
}

static usbd_status
usbd_clear_port_feature(struct usbd_device *hub, int port, int feature)
{
	(void)hub;
	assert(port == m.port && feature == UHF_C_BH_PORT_RESET);
	assert(m.change & UPS_C_BH_PORT_RESET);
	m.change &= ~UPS_C_BH_PORT_RESET;
	m.warm_clears++;
	return USBD_NORMAL_COMPLETION;
}

/* The production routine is checked by usb-hub-reset-changes.sh. */
static bool
uhub_drop_reset_changes(struct usbd_device *hub, int port, int change)
{
	(void)hub;
	assert(port == m.port && (unsigned)change == m.change);
	m.drops++;
	m.change = 0;
	return (m.status & UPS_CURRENT_CONNECT_STATUS) != 0;
}

static void
usb_needs_explore(struct usbd_device *hub)
{
	(void)hub;
	m.explores++;
}

static usbd_status
usbd_new_device(int parent, struct usbd_bus *bus, int depth, int speed,
    int port, struct usbd_port *up)
{
	(void)parent; (void)bus;
	assert(depth == 1 && speed == USB_SPEED_HIGH && port == m.port);
	m.addresses++;
	if (m.second == USBD_NORMAL_COMPLETION)
		up->up_dev = &child;
	return m.second;
}

#include "uhub_retry.inc"

#define ATTACHED (UPS_CURRENT_CONNECT_STATUS | UPS_PORT_POWER | UPS_HIGH_SPEED)
#define ENABLED (ATTACHED | UPS_PORT_ENABLED)

static struct usbd_bus bus;
static struct usbd_device root = { .ud_depth = 0, .ud_bus = &bus };
static struct uhub_softc sc;
static struct usbd_port up;

static bool
run(const char *name, int depth, int speed, usbd_status *result)
{
	bool final;

	root.ud_depth = depth;
	bus.ub_devices[USB_ROOTHUB_INDEX] = &root;
	*result = USBD_REENUMERATE;
	final = uhub_retry_address(&sc, &root, &up, m.port, speed, result);
	/* The caller never sees USBD_REENUMERATE as a final status. */
	assert(!final || *result != USBD_REENUMERATE);
	printf("PASS %s\n", name);
	return final;
}

static void
port(unsigned status, unsigned change, unsigned after, unsigned after_change,
    usbd_status second)
{
	m.port = 4;
	m.status = status;
	m.change = change;
	m.status_after_reset = after;
	m.change_after_reset = after_change;
	m.reset_fails = false;
	m.second = second;
	m.resets = m.addresses = m.drops = m.explores = m.warm_clears = 0;
	up.up_dev = NULL;
	up.up_reattach = 0;
	up.up_restartcnt = 0;
}

int
main(void)
{
	usbd_status r;
	unsigned i;

	/* The same device stays: one reset, one second address. */
	port(ATTACHED, 0, ENABLED, UPS_C_BH_PORT_RESET, USBD_NORMAL_COMPLETION);
	assert(run("same device is addressed once more", 0, USB_SPEED_HIGH,
	    &r) && r == USBD_NORMAL_COMPLETION);
	assert(m.resets == 1 && m.addresses == 1 && m.warm_clears == 1);
	assert(m.drops == 0 && m.explores == 0);

	/* The second address fails: the caller disables the port. */
	port(ATTACHED, 0, ENABLED, 0, USBD_TIMEOUT);
	assert(run("failed second address goes to the disable path", 0,
	    USB_SPEED_HIGH, &r) && r == USBD_TIMEOUT);
	assert(m.resets == 1 && m.addresses == 1);

	/* A second USBD_REENUMERATE also ends in the disable path. */
	port(ATTACHED, 0, ENABLED, 0, USBD_REENUMERATE);
	assert(run("second release goes to the disable path", 0,
	    USB_SPEED_HIGH, &r) && r == USBD_TIMEOUT);
	assert(m.addresses == 1);

	/* An unplug and replug before the retry: explore the port again. */
	port(ATTACHED, UPS_C_CONNECT_STATUS, ENABLED, 0,
	    USBD_NORMAL_COMPLETION);
	assert(!run("connect change explores again", 0, USB_SPEED_HIGH, &r));
	assert(m.resets == 0 && m.addresses == 0);
	assert(up.up_reattach == 1 && m.explores == 1);

	/* Over-current: no second address, explore again. */
	port(ATTACHED | UPS_OVERCURRENT_INDICATOR, 0, ENABLED, 0,
	    USBD_NORMAL_COMPLETION);
	assert(!run("over-current explores again", 0, USB_SPEED_HIGH, &r));
	assert(m.resets == 0 && m.addresses == 0 && m.explores == 1);

	/* The device is gone after the reset: clear the changes only. */
	port(ATTACHED, 0, UPS_PORT_POWER, UPS_C_CONNECT_STATUS,
	    USBD_NORMAL_COMPLETION);
	assert(!run("device gone after reset clears the changes", 0,
	    USB_SPEED_HIGH, &r));
	assert(m.resets == 1 && m.addresses == 0);
	assert(m.drops == 1 && m.explores == 0 && up.up_reattach == 0);

	/* Connected, but the port is not enabled: clear and explore again. */
	port(ATTACHED, 0, ATTACHED, UPS_C_PORT_ENABLED, USBD_NORMAL_COMPLETION);
	assert(!run("disabled port after reset explores again", 0,
	    USB_SPEED_HIGH, &r));
	assert(m.addresses == 0 && m.drops == 1 && m.explores == 1);

	/* The restart limit bounds the later explorations. */
	port(ATTACHED, UPS_C_CONNECT_STATUS, ENABLED, 0,
	    USBD_NORMAL_COMPLETION);
	for (i = 0; i < USBD_RESTART_MAX + 2; i++)
		assert(!run("restart limit", 0, USB_SPEED_HIGH, &r));
	assert(m.explores == USBD_RESTART_MAX);

	/* Other ports: xhci(4) does not release them; disable as usual. */
	port(ATTACHED, 0, ENABLED, 0, USBD_NORMAL_COMPLETION);
	assert(run("external hub port goes to the disable path", 1,
	    USB_SPEED_HIGH, &r) && r == USBD_TIMEOUT);
	assert(run("full-speed device goes to the disable path", 0,
	    USB_SPEED_FULL, &r) && r == USBD_TIMEOUT);
	assert(m.resets == 0 && m.addresses == 0 && m.explores == 0);

	/* A failed reset goes to the disable path. */
	port(ATTACHED, 0, ENABLED, 0, USBD_NORMAL_COMPLETION);
	m.reset_fails = true;
	assert(run("failed reset goes to the disable path", 0, USB_SPEED_HIGH,
	    &r) && r == USBD_TIMEOUT);
	assert(m.resets == 1 && m.addresses == 0);
	return 0;
}
