/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Origin: EmberBSD (AI-assisted).
 * Host contract for the change bits that a failed port reset leaves.  An xHCI
 * root hub port sends no new Port Status Change event while one of its change
 * bits stays set, so the hub driver must clear all of them.
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
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "usb.h"

typedef enum { USBD_NORMAL_COMPLETION, USBD_IOERROR } usbd_status;

/* One hub port: the status and change words and the request log. */
struct usbd_device {
	int port;
	int status, change;
	bool status_fails;
	bool connect_on_clear;
	int refused;		/* feature that the hub refuses */
	int cleared[32];
	int nclears, nreads;
};

static usbd_status
usbd_clear_port_feature(struct usbd_device *dev, int port, int feature)
{
	static const struct {
		int feature, bit;
	} map[] = {
		{ UHF_C_PORT_CONNECTION, 0x0001 },
		{ UHF_C_PORT_ENABLE, 0x0002 },
		{ UHF_C_PORT_SUSPEND, 0x0004 },
		{ UHF_C_PORT_OVER_CURRENT, 0x0008 },
		{ UHF_C_PORT_RESET, 0x0010 },
		{ UHF_C_BH_PORT_RESET, 0x0020 },
		{ UHF_C_PORT_LINK_STATE, 0x0040 },
		{ UHF_C_PORT_CONFIG_ERROR, 0x0080 },
	};
	size_t i;

	assert(port == dev->port);
	assert(dev->nclears < (int)__arraycount(dev->cleared));
	dev->cleared[dev->nclears++] = feature;
	if (feature == dev->refused)
		return USBD_IOERROR;
	for (i = 0; i < __arraycount(map); i++) {
		if (map[i].feature == feature) {
			/* Only a set change bit may be cleared. */
			assert(dev->change & map[i].bit);
			dev->change &= ~map[i].bit;
			if (dev->connect_on_clear) {
				dev->status |= UPS_CURRENT_CONNECT_STATUS;
				dev->change |= UPS_C_CONNECT_STATUS;
				dev->connect_on_clear = false;
			}
			return USBD_NORMAL_COMPLETION;
		}
	}
	assert(!"clear of a feature that is not a port change");
	return USBD_IOERROR;
}

static usbd_status
usbd_get_port_status(struct usbd_device *dev, int port, usb_port_status_t *ps)
{
	assert(port == dev->port);
	dev->nreads++;
	if (dev->status_fails)
		return USBD_IOERROR;
	USETW(ps->wPortStatus, dev->status);
	USETW(ps->wPortChange, dev->change);
	return USBD_NORMAL_COMPLETION;
}

#include "uhub_drop_reset_changes.inc"

static void
vanished_after_reset(void)
{
	/* The status that QBOOK15 port 1 reported after a port error. */
	struct usbd_device d = { .port = 1, .status = 0x0100,
	    .change = UPS_C_CONNECT_STATUS | UPS_C_PORT_ENABLED |
	    UPS_C_PORT_RESET };

	assert(!uhub_drop_reset_changes(&d, 1, d.change));
	assert(d.change == 0);
	assert(d.nclears == 3 && d.nreads == 1);
	assert(d.cleared[0] == UHF_C_PORT_CONNECTION);
	assert(d.cleared[1] == UHF_C_PORT_ENABLE);
	assert(d.cleared[2] == UHF_C_PORT_RESET);
}

static void
every_change_bit(void)
{
	struct usbd_device d = { .port = 3, .status = 0x0100, .change = 0x00ff };

	assert(!uhub_drop_reset_changes(&d, 3, d.change));
	assert(d.change == 0 && d.nclears == 8);
}

static void
device_back_during_clear(void)
{
	/*
	 * The device connects again while the driver clears the bits.  The
	 * new connect change stays set for the next exploration, and the
	 * routine reports the connected device.
	 */
	struct usbd_device d = { .port = 2, .status = 0x0100,
	    .change = UPS_C_PORT_ENABLED, .connect_on_clear = true };

	assert(uhub_drop_reset_changes(&d, 2, UPS_C_PORT_ENABLED));
	assert(d.change == UPS_C_CONNECT_STATUS);
	assert(d.nclears == 1 && d.cleared[0] == UHF_C_PORT_ENABLE);
}

static void
connected_but_disabled(void)
{
	struct usbd_device d = { .port = 4,
	    .status = 0x0100 | UPS_CURRENT_CONNECT_STATUS,
	    .change = UPS_C_PORT_RESET };

	assert(uhub_drop_reset_changes(&d, 4, d.change));
	assert(d.change == 0 && d.nclears == 1);
}

static void
status_read_fails(void)
{
	struct usbd_device d = { .port = 1,
	    .status = UPS_CURRENT_CONNECT_STATUS,
	    .change = UPS_C_PORT_RESET, .status_fails = true };

	assert(!uhub_drop_reset_changes(&d, 1, d.change));
	assert(d.change == 0 && d.nreads == 1);
}

static void
refused_clear(void)
{
	/*
	 * A hub refuses one request.  The routine still asks for the other
	 * clears, and the refused bit stays set.
	 */
	struct usbd_device d = { .port = 1, .status = 0x0100,
	    .change = UPS_C_CONNECT_STATUS | UPS_C_PORT_ENABLED |
	    UPS_C_PORT_RESET, .refused = UHF_C_PORT_ENABLE };

	assert(!uhub_drop_reset_changes(&d, 1, d.change));
	assert(d.nclears == 3 && d.change == UPS_C_PORT_ENABLED);
}

static void
no_change(void)
{
	struct usbd_device d = { .port = 1, .status = 0x0100 };

	assert(!uhub_drop_reset_changes(&d, 1, 0));
	assert(d.nclears == 0 && d.nreads == 1);
}

int
main(void)
{
	vanished_after_reset();
	every_change_bit();
	device_back_during_clear();
	connected_but_disabled();
	status_read_fails();
	refused_clear();
	no_change();
	puts("usb-hub-reset-changes: ok");
	return 0;
}
