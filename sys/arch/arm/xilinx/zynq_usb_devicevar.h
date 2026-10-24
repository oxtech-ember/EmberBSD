/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _ARM_XILINX_ZYNQ_USB_DEVICEVAR_H_
#define _ARM_XILINX_ZYNQ_USB_DEVICEVAR_H_
#include <sys/bus.h>
#include <dev/usb/usbperiph.h>
#include <arm/xilinx/zynq_usb_devicereg.h>
struct zudc_dma {
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	void *kva;
	size_t size;
};
struct zudc_request {
	struct zudc_dma data;
	uint64_t generation;
	uint32_t length, actual;
	int status;
	bool active, retained;
};
/* All core calls run under the attachment's interrupt lock. */
struct zynqudc {
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	bus_dma_tag_t dmat;
	struct zudc_dma descriptors;
	struct zudc_qh *qh;
	struct zudc_td *td;
	struct zudc_request req[ZUDC_SLOTS];
	struct usbperiph_config config;
	struct usbperiph_event event;
	uint64_t generation, sequence;
	uint64_t irq_count, setup_count, reset_count, disconnect_count;
	uint32_t last_irq_status;
	uint8_t last_setup[8];
	unsigned nep;
	uint8_t configuration, address, pending_address, ep0_phase;
	bool faulted, connected, suspended, valid_config, event_pending;
	bool vendor_pending;
	unsigned vendor_ticks;
};
int zynqudc_role(const char *);
int zynqudc_init(struct zynqudc *);
int zynqudc_configure(struct zynqudc *, const struct usbperiph_config *);
int zynqudc_connect(struct zynqudc *);
int zynqudc_disconnect(struct zynqudc *);
int zynqudc_submit(struct zynqudc *, unsigned, uint32_t, uint64_t);
int zynqudc_cancel(struct zynqudc *, unsigned, int);
int zynqudc_intr(struct zynqudc *);
int zynqudc_reply(struct zynqudc *, const struct usbperiph_reply *);
void zynqudc_tick(struct zynqudc *);
#endif
