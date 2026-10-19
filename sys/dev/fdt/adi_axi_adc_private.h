/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_FDT_ADI_AXI_ADC_PRIVATE_H_
#define _DEV_FDT_ADI_AXI_ADC_PRIVATE_H_
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <dev/fdt/adi_axi_adcvar.h>

struct adi_axi_converter {
	device_t c_dev;
	int c_phandle;
	const struct adi_axi_converter_ops *c_ops;
	void *c_cookie;
	kmutex_t c_lock;
	struct adi_axi_adc_lease *c_lease;
	bool c_poisoned;
	bool c_bootstrap_used;
	LIST_ENTRY(adi_axi_converter) c_next;
};

LIST_HEAD(adi_axi_converter_head, adi_axi_converter);
extern struct adi_axi_converter_head adi_axi_converters;
extern kmutex_t adi_axi_registry_lock;
void adi_axi_registry_init(void);
#endif
