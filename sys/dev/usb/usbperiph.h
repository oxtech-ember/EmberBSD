/* SPDX-License-Identifier: BSD-2-Clause */
/* Native peripheral function ABI. No user buffer is mapped for device DMA. */
#ifndef _DEV_USB_USBPERIPH_H_
#define _DEV_USB_USBPERIPH_H_
#include <sys/types.h>
#include <sys/ioccom.h>
#define USBPERIPH_ABI 1
#define USBPERIPH_MAX_XFER 16384
#define USBPERIPH_MAX_CONFIG 512
#define USBPERIPH_MAX_STRINGS 512
#define USBPERIPH_MAX_CONTROL 512
struct usbperiph_config {
	uint32_t version;
	uint32_t config_length;
	uint32_t strings_length;
	uint32_t reserved;
	uint8_t device[18];
	uint8_t config[USBPERIPH_MAX_CONFIG];
	uint8_t strings[USBPERIPH_MAX_STRINGS]; /* Packed USB string descriptors. */
};
/*
 * Initialize a fresh argument for each XFER. Outputs are valid only on success.
 * On any ioctl error, discard the payload and ignore actual and generation.
 */
struct usbperiph_xfer {
	uint64_t buffer;
	uint64_t generation; /* Zero accepts current generation; returned only on success. */
	uint32_t length;
	uint32_t actual;
	uint32_t timeout_ms;
	uint32_t endpoint; /* USB address: 0x01 OUT, 0x81 IN. */
};
struct usbperiph_event {
	uint64_t generation;
	uint64_t sequence;
	uint32_t type;
	uint32_t configured;
	uint8_t setup[8];
};
#define USBPERIPH_EVENT_SETUP 1
#define USBPERIPH_EVENT_RESET 2
#define USBPERIPH_EVENT_DISCONNECT 3
#define USBPERIPH_EVENT_CONFIGURE 4
#define USBPERIPH_EVENT_SUSPEND 5
struct usbperiph_reply {
	uint64_t generation;
	uint64_t sequence;
	uint32_t length;
	uint32_t stall;
	uint8_t data[USBPERIPH_MAX_CONTROL];
};
/* Zero generation accepts the current bus generation; nonzero must match. */
struct usbperiph_endpoint_gate {
	uint64_t generation;
	uint32_t endpoint;
	uint32_t enable;
};
#define USBPERIPH_IOC_ENDPOINT_GATE _IOW('P', 7, struct usbperiph_endpoint_gate)
/* Read-only snapshot; PHY fields are cached attach-time values, never live. */
#define USBPERIPH_SNAPSHOT_VERSION 1
#define USBPERIPH_SNAPSHOT_CONNECTED 0x01U
#define USBPERIPH_SNAPSHOT_FAULTED 0x02U
#define USBPERIPH_SNAPSHOT_SUSPENDED 0x04U
#define USBPERIPH_SNAPSHOT_STOPPING 0x08U
#define USBPERIPH_SNAPSHOT_PHY_INITIAL_VALID 0x10U
struct usbperiph_snapshot {
	uint32_t version;
	uint32_t reserved0;
	uint64_t generation;
	uint64_t irq_count;
	uint64_t setup_count;
	uint64_t reset_count;
	uint64_t disconnect_count;
	uint32_t flags;
	uint32_t configuration;
	uint32_t address;
	uint32_t ep0_phase;
	uint32_t active_slots;
	uint32_t retained_slots;
	uint32_t last_irq_status;
	uint32_t reserved1;
	uint8_t last_setup[8];
	uint8_t phy_function_initial;
	uint8_t phy_otg_initial;
	uint8_t reserved[6];
	uint32_t command, status, interrupt_enable, port, otg, mode;
	uint32_t setup_status, prime, flush, endpoint_status, complete, epctrl0;
};
#define USBPERIPH_IOC_SNAPSHOT _IOR('P', 8, struct usbperiph_snapshot)
#define USBPERIPH_IOC_CONFIGURE _IOW('P', 0, struct usbperiph_config)
#define USBPERIPH_IOC_CONNECT _IO('P', 1)
#define USBPERIPH_IOC_DISCONNECT _IO('P', 2)
#define USBPERIPH_IOC_XFER _IOWR('P', 3, struct usbperiph_xfer)
#define USBPERIPH_IOC_CANCEL _IOW('P', 4, uint32_t)
#define USBPERIPH_IOC_GET_EVENT _IOR('P', 5, struct usbperiph_event)
#define USBPERIPH_IOC_REPLY_SETUP _IOW('P', 6, struct usbperiph_reply)
#endif
