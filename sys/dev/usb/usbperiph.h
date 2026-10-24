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
struct usbperiph_xfer {
	uint64_t buffer;
	uint64_t generation; /* Zero accepts current generation; returned on completion. */
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
#define USBPERIPH_IOC_CONFIGURE _IOW('P', 0, struct usbperiph_config)
#define USBPERIPH_IOC_CONNECT _IO('P', 1)
#define USBPERIPH_IOC_DISCONNECT _IO('P', 2)
#define USBPERIPH_IOC_XFER _IOWR('P', 3, struct usbperiph_xfer)
#define USBPERIPH_IOC_CANCEL _IOW('P', 4, uint32_t)
#define USBPERIPH_IOC_GET_EVENT _IOR('P', 5, struct usbperiph_event)
#define USBPERIPH_IOC_REPLY_SETUP _IOW('P', 6, struct usbperiph_reply)
#endif
