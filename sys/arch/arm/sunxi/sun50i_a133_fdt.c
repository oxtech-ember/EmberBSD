/* Origin: EmberBSD A133 FDT; warm path adapted from ef145f701, AI-assisted. */
/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE.
 */

/* Translate the vendor A133 MMC resources before FDT node offsets are cached. */
#include <sys/types.h>
#include <libfdt.h>
#ifdef _KERNEL
#include "opt_a133_wlan_warm.h"
#include <sys/systm.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "sun50i_a100_ccu.h"
#include "sun50i_a100_r_ccu.h"

#define SOC "/soc@03000000"
/* Three 800x1280 XRGB8888 frames with slack; see A133_BO_BYTES. */
#define A133_SCANOUT_POOL_BYTES (3 * (4096000 + 65536))
#define PIO SOC "/pinctrl@0300b000"
#define MMC SOC "/sdmmc@04022000"
#define CCU SOC "/clock-controller@03001000"
#define PINS PIO "/ember-mmc2-pins"
#define TOUCH SOC "/twi@0x05002c00/goodix_ts@5d"
#define CODEC SOC "/codec@0x05096000"
#define SOUND SOC "/sound@0"
#define SDIO SOC "/sdmmc@04021000"
#define SDIO_GROUP PIO "/sdc1@0"
#define SDIO_PINS PIO "/ember-mmc1-pins"
#define WLAN SOC "/wlan@0"
#define RPIO SOC "/pinctrl@07022000"
#define SDIO_GIC "/interrupt-controller@03020000"
#define SDIO_MARK "ember,ys-m33-sdio-host"
#define SDIO_NATIVE_OPT "ember,ys-m33-wlan-warm"
#define SDIO_NATIVE_MARK "ember,ys-m33-wlan-warm-host"
#define SDIO_PRESERVE_OPT "ember,ys-m33-wlan-keep-power"
#define SDIO_PRESERVE "/ember-wlan-preserved-power"

int sun50i_a133_fdt_fixup(void *);
int sun50i_a133_gpu_fdt_fixup(void *);

static int
a133_node(void *fdt, const char *parent, const char *name, uint32_t *phandle)
{
	int node, error;

	node = fdt_path_offset(fdt, parent);
	if (node < 0)
		return node;
	error = fdt_add_subnode(fdt, node, name);
	if (error == -FDT_ERR_EXISTS)
		node = fdt_subnode_offset(fdt, node, name);
	else
		node = error;
	if (node < 0)
		return node;
	*phandle = fdt_get_phandle(fdt, node);
	if (*phandle == 0) {
		error = fdt_generate_phandle(fdt, phandle);
		if (error != 0)
			return error;
		error = fdt_setprop_u32(fdt, node, "phandle", *phandle);
		if (error != 0)
			return error;
	}
	return node;
}

static int
a133_prop(void *fdt, const char *path, const char *name,
    const void *data, int len)
{
	int node;

	node = fdt_path_offset(fdt, path);
	if (node < 0)
		return node;
	return fdt_setprop(fdt, node, name, data, len);
}

static int
a133_clock(void *fdt, const char *name, uint32_t rate, uint32_t *phandle)
{
	int node, error;

	node = a133_node(fdt, "/", name, phandle);
	if (node < 0)
		return node;
	error = fdt_setprop_string(fdt, node, "compatible", "fixed-clock");
	if (error != 0)
		return error;
	error = fdt_setprop_u32(fdt, node, "#clock-cells", 0);
	if (error != 0)
		return error;
	return fdt_setprop_u32(fdt, node, "clock-frequency", rate);
}

/* Every property update can move other node offsets; resolve each path again. */
#define A133_SET(path, name, data, len) do { \
	error = a133_prop(fdt, path, name, data, len); \
	if (error != 0) return error; \
} while (0)
#define A133_STRING(path, name, value) A133_SET(path, name, value, sizeof(value))
#define A133_CELL(path, name, value) do { \
	cell = cpu_to_fdt32(value); \
	A133_SET(path, name, &cell, sizeof(cell)); \
} while (0)

static bool
a133_audio_okay(const void *fdt, const char *path)
{
	const char *status;
	int node, len;

	node = fdt_path_offset(fdt, path);
	if (node < 0)
		return false;
	status = fdt_getprop(fdt, node, "status", &len);
	return status != NULL && len == 5 && memcmp(status, "okay", 5) == 0;
}

/* The six-cell GPIO binding remains vendor-owned; translate only this IRQ. */
static int
a133_audio(void *fdt)
{
	const fdt32_t *p;
	fdt32_t irq[3], cell;
	uint32_t pio, codec, gic;
	int node, len, error, i;

	node = fdt_path_offset(fdt, CODEC);
	if (node < 0)
		return 0;
	error = fdt_delprop(fdt, node, "ember,ys-m33-audio");
	if (error != 0 && error != -FDT_ERR_NOTFOUND)
		return error;
	if (!a133_audio_okay(fdt, CODEC) || !a133_audio_okay(fdt, SOUND))
		return 0;
	node = fdt_path_offset(fdt, CODEC);
	if (fdt_node_check_compatible(fdt, node, "allwinner,sunxi-internal-codec") != 0)
		return 0;
	codec = fdt_get_phandle(fdt, node);
	p = fdt_getprop(fdt, node, "reg", &len);
	if (codec == 0 || p == NULL || len != 16 || p[0] != 0 || p[2] != 0 ||
	    fdt32_to_cpu(p[1]) != 0x05096000 || fdt32_to_cpu(p[3]) != 0x32c)
		return 0;
	p = fdt_getprop(fdt, node, "pa_level", &len);
	if (p == NULL || len != 4 || fdt32_to_cpu(*p) != 0)
		return 0;
	p = fdt_getprop(fdt, node, "pa_msleep_time", &len);
	if (p == NULL || len != 4 || fdt32_to_cpu(*p) != 120)
		return 0;
	node = fdt_path_offset(fdt, PIO);
	pio = fdt_get_phandle(fdt, node);
	p = fdt_getprop(fdt, node, "#gpio-cells", &len);
	if (pio == 0 || p == NULL || len != 4 || fdt32_to_cpu(*p) != 6)
		return 0;
	node = fdt_path_offset(fdt, SOUND);
	if (fdt_node_check_compatible(fdt, node, "allwinner,sunxi-codec-machine") != 0)
		return 0;
	p = fdt_getprop(fdt, node, "sunxi,audio-codec", &len);
	if (p == NULL || len != 4 || fdt32_to_cpu(*p) != codec)
		return 0;
	p = fdt_getprop(fdt, node, "spk-gpio", &len);
	if (p == NULL || len != 28 || fdt32_to_cpu(p[0]) != pio ||
	    fdt32_to_cpu(p[1]) != 5 || fdt32_to_cpu(p[2]) != 6)
		return 0;
	for (i = 3; i < 7; i++)
		if (fdt32_to_cpu(p[i]) != 1)
			return 0;
	p = fdt_getprop(fdt, node, "interrupts", &len);
	if (p == NULL || len != 12 || p[0] != 0 ||
	    fdt32_to_cpu(p[1]) != 25 || fdt32_to_cpu(p[2]) != 4)
		return 0;
	memcpy(irq, p, sizeof(irq));
	node = fdt_path_offset(fdt, "/interrupt-controller@03020000");
	if (node < 0 || (gic = fdt_get_phandle(fdt, node)) == 0)
		return 0;
	A133_SET(CODEC, "interrupts", irq, sizeof(irq));
	A133_CELL(CODEC, "interrupt-parent", gic);
	return a133_prop(fdt, CODEC, "ember,ys-m33-audio", NULL, 0);
}

static int
a133_touch_reset(void *fdt)
{
	const fdt32_t *gpio;
	uint32_t pio, rpio;
	int node, len, error;

	node = fdt_path_offset(fdt, TOUCH);
	if (node < 0)
		return 0;
	error = fdt_delprop(fdt, node, "ember,ys-m33-reset");
	if (error != 0 && error != -FDT_ERR_NOTFOUND)
		return error;
	node = fdt_path_offset(fdt, SOC "/pinctrl@07022000");
	if (node < 0)
		return 0;
	rpio = fdt_get_phandle(fdt, node);
	node = fdt_path_offset(fdt, PIO);
	pio = fdt_get_phandle(fdt, node);
	if (pio == 0 || rpio == 0)
		return 0;
	node = fdt_path_offset(fdt, TOUCH);
	gpio = fdt_getprop(fdt, node, "goodix,rst-gpio", &len);
	if (gpio == NULL || len != 28 || fdt32_to_cpu(gpio[0]) != pio ||
	    fdt32_to_cpu(gpio[1]) != 7 || fdt32_to_cpu(gpio[2]) != 14)
		return 0;
	gpio = fdt_getprop(fdt, node, "goodix,irq-gpio", &len);
	if (gpio == NULL || len != 28 || fdt32_to_cpu(gpio[0]) != rpio ||
	    fdt32_to_cpu(gpio[1]) != 11 || fdt32_to_cpu(gpio[2]) != 10)
		return 0;
	return fdt_setprop(fdt, node, "ember,ys-m33-reset", NULL, 0);
}

/* Preserve the YS-M33 panel-108 scanout initialized by the vendor loader. */
static int
a133_framebuffer(void *fdt)
{
	const char *args, *p;
	char *end, *last, name[48], path[64];
	fdt32_t cells[4], cell;
	uint64_t start, limit, addr, size;
	unsigned long bytes, base;
	uint32_t phandle;
	int chosen, len, node, error, i;

	chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen < 0)
		return 0;
	args = fdt_getprop(fdt, chosen, "bootargs", &len);
	if (args == NULL || len <= 0 || memchr(args, 0, len) == NULL ||
	    strstr(args, "LCD/lcd_mipi_param=108;") == NULL)
		return 0;
	p = strstr(args, "disp_reserve=");
	if (p == NULL || (p != args && p[-1] != ' '))
		return 0;
	p += sizeof("disp_reserve=") - 1;
	bytes = strtoul(p, &end, 10);
	if (bytes != 4096000 || end == p || *end != ',')
		return 0;
	base = strtoul(end + 1, &last, 0);
	if (last == end + 1 || (*last != 0 && *last != ' ') ||
	    base < 0x40000000 || base > 0x80000000 - bytes)
		return 0;
	if (fdt_node_offset_by_compatible(fdt, -1, "simple-framebuffer") >= 0)
		return 0;

	/* The buffer is unaligned; exclude both partial pages from VM. */
	start = base & ~(uint64_t)4095;
	limit = ((uint64_t)base + bytes + 4095) & ~(uint64_t)4095;
	for (i = 0; i < fdt_num_mem_rsv(fdt); i++) {
		error = fdt_get_mem_rsv(fdt, i, &addr, &size);
		if (error != 0)
			return error;
		if (addr <= start && size >= limit - addr)
			break;
	}
	if (i == fdt_num_mem_rsv(fdt)) {
		error = fdt_add_mem_rsv(fdt, start, limit - start);
		if (error != 0)
			return error;
	}
	snprintf(name, sizeof(name), "framebuffer@%lx", base);
	snprintf(path, sizeof(path), "/chosen/%s", name);
	node = a133_node(fdt, "/chosen", name, &phandle);
	if (node < 0)
		return node;
	A133_CELL("/chosen", "#address-cells", 2);
	A133_CELL("/chosen", "#size-cells", 2);
	A133_STRING(path, "compatible", "simple-framebuffer");
	cells[0] = 0;
	cells[1] = cpu_to_fdt32(base);
	cells[2] = 0;
	cells[3] = cpu_to_fdt32(bytes);
	A133_SET(path, "reg", cells, sizeof(cells));
	A133_CELL(path, "width", 800);
	A133_CELL(path, "height", 1280);
	A133_CELL(path, "stride", 3200);
	A133_STRING(path, "format", "x8r8g8b8");
	A133_STRING(path, "status", "okay");

	/*
	 * Page-flip buffers for the DSI KMS driver: three frames directly
	 * below the boot frame, reserved before UVM. After boot, free pages
	 * are spread over all of memory and a contiguous frame cannot be
	 * allocated any more.
	 */
	if (fdt_path_offset(fdt, SOC "/disp@06000000") < 0)
		return 0;
	limit = start;
	start = limit - A133_SCANOUT_POOL_BYTES;
	for (i = 0; i < fdt_num_mem_rsv(fdt); i++) {
		error = fdt_get_mem_rsv(fdt, i, &addr, &size);
		if (error != 0)
			return error;
		if (addr < limit && addr + size > start)
			return 0;	/* occupied: no KMS buffers */
	}
	error = fdt_add_mem_rsv(fdt, start, limit - start);
	if (error != 0)
		return error;
	cells[0] = 0;
	cells[1] = cpu_to_fdt32(start);
	cells[2] = 0;
	cells[3] = cpu_to_fdt32(limit - start);
	A133_SET(SOC "/disp@06000000", "netbsd,scanout-pool", cells,
	    sizeof(cells));
	return 0;
}

static bool
a133_value(const void *fdt, const char *path, const char *name,
    const void *value, int size)
{
	const void *p;
	int node, len;

	node = fdt_path_offset(fdt, path);
	if (node < 0)
		return false;
	p = fdt_getprop(fdt, node, name, &len);
	return p != NULL && len == size && memcmp(p, value, size) == 0;
}

static bool
a133_cell(const void *fdt, const char *path, const char *name,
    uint32_t value)
{
	fdt32_t cell = cpu_to_fdt32(value);

	return a133_value(fdt, path, name, &cell, sizeof(cell));
}

/* USB1 is the fixed USB-A host; USB0 remains owned by the vendor USB-C path. */
static int
a133_usb1(void *fdt, uint32_t ccu)
{
	static const char *paths[] = {
	    SOC "/ehci1-controller@0x05200000",
	    SOC "/ohci1-controller@0x05200400"
	};
	static const char *legacy[] = {
	    "allwinner,sunxi-ehci1", "allwinner,sunxi-ohci1"
	};
	const char *phy_path = SOC "/ember-usb1-phy";
	const fdt32_t *p;
	fdt32_t cells[8], gpio[7], power[28], cell;
	uint32_t pio, vbus, phy;
	int node, len, error, i;

	/* Re-entry after our complete conversion is a no-op. */
	if (fdt_path_offset(fdt, phy_path) >= 0)
		return 0;
	node = fdt_path_offset(fdt, PIO);
	pio = node < 0 ? 0 : fdt_get_phandle(fdt, node);
	node = fdt_path_offset(fdt, "/usb1-vbus");
	vbus = node < 0 ? 0 : fdt_get_phandle(fdt, node);
	if (pio == 0 || vbus == 0 ||
	    !a133_audio_okay(fdt, "/usb1-vbus") ||
	    fdt_node_check_compatible(fdt, node, "regulator-fixed") != 0 ||
	    fdt_getprop(fdt, node, "enable-active-high", &len) == NULL || len != 0)
		return 0;
	for (i = 0; i < 2; i++) {
		node = fdt_path_offset(fdt, paths[i]);
		if (node < 0 || !a133_audio_okay(fdt, paths[i]) ||
		    fdt_node_check_compatible(fdt, node, legacy[i]) != 0 ||
		    !a133_cell(fdt, paths[i], "hci_ctrl_no", 1) ||
		    !a133_cell(fdt, paths[i], "drvvbus-supply", vbus))
			return 0;
		p = fdt_getprop(fdt, node, "reg", &len);
		if (p == NULL || len != 64 || fdt32_to_cpu(p[0]) != 0 ||
		    fdt32_to_cpu(p[1]) != 0x05200000 || p[2] != 0 ||
		    fdt32_to_cpu(p[3]) != 0xfff)
			return 0;
		cells[0] = 0; cells[1] = cpu_to_fdt32(33 + i);
		cells[2] = cpu_to_fdt32(4);
		if (!a133_value(fdt, paths[i], "interrupts", cells, 12))
			return 0;
	}
	node = fdt_path_offset(fdt, "/misc_power_en");
	if (node < 0 || !a133_cell(fdt, "/misc_power_en",
	    "vcc_host_drv1_gpio_level", 1))
		return 0;
	p = fdt_getprop(fdt, node, "vcc_host_drv1_gpio", &len);
	if (p == NULL || len != 28 || fdt32_to_cpu(p[0]) != pio ||
	    fdt32_to_cpu(p[1]) != 5 || fdt32_to_cpu(p[2]) != 2 ||
	    fdt32_to_cpu(p[3]) != 1 || fdt32_to_cpu(p[6]) != 1)
		return 0;
	memcpy(gpio, p, sizeof(gpio));
	{
		static const char *names[] = {"vcc_host_drv0", "vcc_host_drv2",
		    "vcc_host_drv3", "vcc_hub_drv0"};
		static const uint32_t pins[] = {0, 3, 4, 5};
		char name[48];
		for (i = 0; i < 4; i++) {
			snprintf(name, sizeof(name), "%s_gpio_level", names[i]);
			if (!a133_cell(fdt, "/misc_power_en", name, 1))
				return 0;
			snprintf(name, sizeof(name), "%s_gpio", names[i]);
			p = fdt_getprop(fdt, node, name, &len);
			if (p == NULL || len != 28 || fdt32_to_cpu(p[0]) != pio ||
			    fdt32_to_cpu(p[1]) != 5 || fdt32_to_cpu(p[2]) != pins[i] ||
			    fdt32_to_cpu(p[3]) != 1 || fdt32_to_cpu(p[6]) != 1)
				return 0;
			memcpy(power + i * 7, p, 28);
		}
	}
	A133_SET("/usb1-vbus", "gpio", gpio, sizeof(gpio));
	A133_CELL("/usb1-vbus", "startup-delay-us", 1000);
	A133_SET("/usb1-vbus", "regulator-boot-on", NULL, 0);
	node = a133_node(fdt, SOC, "ember-usb1-phy", &phy);
	if (node < 0)
		return node;
	A133_STRING(phy_path, "compatible", "allwinner,sun50i-a100-usb-phy");
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x05100400);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x100);
	cells[4] = 0; cells[5] = cpu_to_fdt32(0x05200800);
	cells[6] = 0; cells[7] = cpu_to_fdt32(0x100);
	A133_SET(phy_path, "reg", cells, sizeof(cells));
	A133_STRING(phy_path, "reg-names", "phy_ctrl\0pmu1");
	cells[0] = cpu_to_fdt32(ccu); cells[1] = cpu_to_fdt32(A100_CLK_USB_PHY1);
	A133_SET(phy_path, "clocks", cells, 8);
	cells[1] = cpu_to_fdt32(A100_RST_USB_PHY1);
	A133_SET(phy_path, "resets", cells, 8);
	A133_CELL(phy_path, "#phy-cells", 1);
	A133_CELL(phy_path, "usb1_vbus-supply", vbus);
	A133_SET(phy_path, "usb1-power-gpios", power, sizeof(power));
	A133_STRING(phy_path, "status", "okay");
	for (i = 0; i < 2; i++) {
		const char *path = paths[i];
		if (i == 0)
			A133_STRING(path, "compatible", "generic-ehci");
		else
			A133_STRING(path, "compatible", "generic-ohci");
		cells[0] = 0; cells[1] = cpu_to_fdt32(0x05200000 + i * 0x400);
		cells[2] = 0; cells[3] = cpu_to_fdt32(0x100);
		A133_SET(path, "reg", cells, 16);
		cells[0] = cells[2] = cells[4] = cpu_to_fdt32(ccu);
		cells[1] = cpu_to_fdt32(A100_CLK_BUS_OHCI1);
		cells[3] = cpu_to_fdt32(A100_CLK_USB_OHCI1);
		cells[5] = cpu_to_fdt32(A100_CLK_BUS_EHCI1);
		A133_SET(path, "clocks", cells, i == 0 ? 24 : 16);
		cells[1] = cpu_to_fdt32(A100_RST_BUS_OHCI1);
		cells[3] = cpu_to_fdt32(A100_RST_BUS_EHCI1);
		A133_SET(path, "resets", cells, i == 0 ? 16 : 8);
		cells[0] = cpu_to_fdt32(phy); cells[1] = cpu_to_fdt32(1);
		A133_SET(path, "phys", cells, 8);
		A133_STRING(path, "phy-names", "usb");
	}
	return 0;
}

static uint32_t
a133_phandle_node(const void *fdt, int node)
{
	uint32_t phandle;
	int other;

	if (node < 0)
		return 0;
	phandle = fdt_get_phandle(fdt, node);
	if (phandle == 0 || phandle == UINT32_MAX ||
	    fdt_node_offset_by_phandle(fdt, phandle) != node)
		return 0;
	other = -1;
	while ((other = fdt_next_node(fdt, other, NULL)) >= 0)
		if (other != node && fdt_get_phandle(fdt, other) == phandle)
			return 0;
	return phandle;
}

static uint32_t
a133_phandle(const void *fdt, const char *path)
{

	return a133_phandle_node(fdt, fdt_path_offset(fdt, path));
}

static bool
a133_gpu_active(const void *fdt, const char *path)
{
	const char *status;
	int node, len;

	node = fdt_path_offset(fdt, path);
	if (node < 0)
		return false;
	status = fdt_getprop(fdt, node, "status", &len);
	return (status == NULL && len == -FDT_ERR_NOTFOUND) ||
	    (status != NULL && ((len == 5 && memcmp(status, "okay", 5) == 0) ||
	    (len == 3 && memcmp(status, "ok", 3) == 0)));
}

static bool
a133_gpu_reg(const void *fdt, const char *path, uint32_t address,
    uint32_t size)
{
	fdt32_t cells[4] = {0, cpu_to_fdt32(address), 0, cpu_to_fdt32(size)};

	return a133_value(fdt, path, "reg", cells, sizeof(cells));
}

/* Preserve the vendor interrupt-controller node and all references to it. */
static int
a133_keys(void *fdt, uint32_t ccu)
{
	static const char nmi[] = "/intc-nmi@07010320";
	static const char pmu[] = SOC "/s_twi@0x07081400/pmu@34";
	static const char keys[] = SOC "/keyboard";
	const fdt32_t nmi_irq[] = { 0, cpu_to_fdt32(103), cpu_to_fdt32(4) };
	const fdt32_t pmu_irq[] = { 0, cpu_to_fdt32(8) };
	const fdt32_t key_irq[] = { 0, cpu_to_fdt32(22), 0 };
	fdt32_t cells[2], cell;
	uint32_t controller = a133_phandle(fdt, nmi);
	uint32_t gic = a133_phandle(fdt, SDIO_GIC);
	int error;

	if (gic == 0)
		return 0;
	if (controller != 0 && a133_gpu_active(fdt, nmi) &&
	    a133_gpu_reg(fdt, nmi, 0x07010320, 0x0c) &&
	    a133_value(fdt, nmi, "compatible", "allwinner,sun8i-nmi",
	        sizeof("allwinner,sun8i-nmi")) &&
	    a133_cell(fdt, nmi, "#interrupt-cells", 2) &&
	    a133_cell(fdt, nmi, "interrupt-parent", gic) &&
	    a133_value(fdt, nmi, "interrupts", nmi_irq, sizeof(nmi_irq)) &&
	    a133_gpu_active(fdt, pmu) && a133_cell(fdt, pmu, "reg", 0x34) &&
	    a133_value(fdt, pmu, "compatible", "x-powers,axp803",
	        sizeof("x-powers,axp803")) &&
	    a133_cell(fdt, pmu, "interrupt-parent", controller) &&
	    a133_value(fdt, pmu, "interrupts", pmu_irq, sizeof(pmu_irq))) {
		A133_STRING(nmi, "compatible",
		    "allwinner,sun8i-nmi\0allwinner,sun9i-a80-nmi");
	}
	if (!a133_gpu_active(fdt, keys) ||
	    !a133_gpu_reg(fdt, keys, 0x05070800, 0x400) ||
	    !a133_value(fdt, keys, "compatible", "allwinner,keyboard_1350mv",
	        sizeof("allwinner,keyboard_1350mv")) ||
	    !a133_value(fdt, keys, "interrupts", key_irq, sizeof(key_irq)))
		return 0;
	cells[0] = cpu_to_fdt32(ccu);
	cells[1] = cpu_to_fdt32(A100_CLK_BUS_LRADC);
	A133_SET(keys, "clocks", cells, sizeof(cells));
	cells[1] = cpu_to_fdt32(A100_RST_BUS_LRADC);
	A133_SET(keys, "resets", cells, sizeof(cells));
	A133_CELL(keys, "interrupt-parent", gic);
	return 0;
}

static bool
a133_gpu_unique(const void *fdt, const char *compat, int expected)
{
	int node;

	node = fdt_node_offset_by_compatible(fdt, -1, compat);
	return node == expected && (node < 0 ||
	    fdt_node_offset_by_compatible(fdt, node, compat) == -FDT_ERR_NOTFOUND);
}

/* Resource checks precede every mutation, including an owned re-entry. */
static bool
a133_gpu_node_active(const void *fdt, int node)
{
	const char *status;
	int len;

	for (; node >= 0; node = fdt_parent_offset(fdt, node)) {
		status = fdt_getprop(fdt, node, "status", &len);
		if (status != NULL && !((len == 5 && memcmp(status, "okay", 5) == 0) ||
		    (len == 3 && memcmp(status, "ok", 3) == 0)))
			return false;
		if (status == NULL && len != -FDT_ERR_NOTFOUND)
			return false;
	}
	return true;
}

static bool
a133_gpu_refs(const void *fdt, const fdt32_t *v, int len,
    const char *cells_name, uint32_t target, uint32_t id1, uint32_t id2,
    uint32_t id3)
{
	int i, p, n, plen;
	const fdt32_t *pc;
	uint32_t ph, count, id;
	bool relevant = false;

	if (len < 0 || len % 4 != 0)
		return false;
	n = len / 4;
	for (i = 0; i < n; i++)
		if (fdt32_to_cpu(v[i]) == target)
			relevant = true;
	if (!relevant)
		return true;
	for (i = 0; i < n;) {
		ph = fdt32_to_cpu(v[i++]);
		p = fdt_node_offset_by_phandle(fdt, ph);
		if (p < 0 || a133_phandle_node(fdt, p) != ph)
			return false;
		pc = fdt_getprop(fdt, p, cells_name, &plen);
		if (pc == NULL || plen != 4 || (count = fdt32_to_cpu(*pc)) > 8 ||
		    count > (uint32_t)(n - i))
			return false;
		if (ph == target) {
			if (count == 0)
				return false;
			id = fdt32_to_cpu(v[i]);
			if (id == id1 || id == id2 || id == id3)
				return false;
		}
		i += count;
	}
	return true;
}

static bool
a133_gpu_inventory(const void *fdt, int gpu, uint32_t ccu, uint32_t domain,
    uint32_t supply, uint32_t pll, uint32_t legacy_gpu, int virtual_gpu)
{
	const struct fdt_property *prop;
	const fdt32_t *v;
	const char *name;
	uint64_t base, size, end;
	int node = -1, parent, soc, off, len, n, i;
	size_t namelen;

	soc = fdt_path_offset(fdt, SOC);
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		if (node == gpu || !a133_gpu_node_active(fdt, node))
			continue;
		parent = fdt_parent_offset(fdt, node);
		if (parent == 0 || parent == soc) {
			v = fdt_getprop(fdt, node, "reg", &len);
			if (v != NULL) {
				if (len <= 0 || len % 16 != 0)
					return false;
				for (i = 0; i < len / 4; i += 4) {
					base = ((uint64_t)fdt32_to_cpu(v[i]) << 32) |
					    fdt32_to_cpu(v[i + 1]);
					size = ((uint64_t)fdt32_to_cpu(v[i + 2]) << 32) |
					    fdt32_to_cpu(v[i + 3]);
					if (size == 0 || base > UINT64_MAX - size)
						return false;
					end = base + size;
					if ((base < 0x01880000 && end > 0x01800000) ||
					    (base < 0x01880024 && end > 0x01880020))
						return false;
				}
			}
		}
		fdt_for_each_property_offset(off, fdt, node) {
			prop = fdt_get_property_by_offset(fdt, off, &len);
			if (prop == NULL || (name = fdt_string(fdt,
			    fdt32_to_cpu(prop->nameoff))) == NULL)
				return false;
			v = (const fdt32_t *)prop->data;
			n = fdt32_to_cpu(prop->len);
			namelen = strlen(name);
			if (namelen >= 7 && strcmp(name + namelen - 7, "-supply") == 0) {
				if (n >= 4 && fdt32_to_cpu(*v) == supply &&
				    !(node == virtual_gpu && strcmp(name, "dcdc4-supply") == 0))
					return false;
			} else if (strcmp(name, "clocks") == 0 ||
			    strcmp(name, "assigned-clocks") == 0 ||
			    strcmp(name, "assigned-clock-parents") == 0) {
				if (!a133_gpu_refs(fdt, v, n, "#clock-cells", ccu,
				    A100_CLK_GPU, A100_CLK_BUS_GPU, A100_CLK_PLL_GPU) ||
				    !a133_gpu_refs(fdt, v, n, "#clock-cells", pll, 0, 0, 0) ||
				    !a133_gpu_refs(fdt, v, n, "#clock-cells", legacy_gpu, 0, 0, 0))
					return false;
			} else if (strcmp(name, "resets") == 0) {
				if (!a133_gpu_refs(fdt, v, n, "#reset-cells", ccu,
				    A100_RST_BUS_GPU, UINT32_MAX, UINT32_MAX))
					return false;
			} else if (strcmp(name, "power-domains") == 0) {
				if (!a133_gpu_refs(fdt, v, n, "#power-domain-cells", domain,
				    0, 0, 0))
					return false;
			}
		}
	}
	return node == -FDT_ERR_NOTFOUND;
}

/* Translate only the measured vendor binding in an explicit GPU kernel. */
int
sun50i_a133_gpu_fdt_fixup(void *fdt)
{
	static const char ppu[] = "/gpu-power-domain";
	static const char gpu[] = "/gpu";
	static const char sram[] = "/sram_ctrl";
	static const char rccu[] = SOC "/clock-controller@07010000";
	static const char marker[] = "ember,a133-vendor-ppu";
	static const char vgpu[] = SOC "/s_twi@0x07081400/pmu@34/virtual-dcdc4";
	static const char pmu[] = SOC "/s_twi@0x07081400/pmu@34";
	static const char rail[] = SOC "/s_twi@0x07081400/pmu@34/regulators/dcdc4";
	fdt32_t cells[8], cell;
	uint32_t domain, gic, ccu, hosc, losc, iosc, syscon, rclock, legacy;
	uint32_t pll_gpu, clk_gpu, supply;
	uint32_t total, strings, size;
	const void *value;
	bool owned;
	int node, gpu_node, virtual_gpu, len, error;

	error = fdt_check_header(fdt);
	if (error != 0)
		return error;
	if (fdt_node_check_compatible(fdt, 0, "allwinner,a133") != 0)
		return 0;
	node = fdt_path_offset(fdt, ppu);
	if (node == -FDT_ERR_NOTFOUND)
		return 0;
	if (node < 0)
		return node;
	value = fdt_getprop(fdt, node, marker, &len);
	owned = value != NULL;
	if ((owned && len != 0) ||
	    !a133_gpu_unique(fdt, owned ? "allwinner,sun50i-a133-ppu" :
	    "allwinner,gpu-pd", node) ||
	    !a133_gpu_unique(fdt, owned ? "allwinner,gpu-pd" :
	    "allwinner,sun50i-a133-ppu", -FDT_ERR_NOTFOUND))
		return -FDT_ERR_BADVALUE;
	gpu_node = fdt_path_offset(fdt, gpu);
	if (gpu_node < 0 || !a133_gpu_unique(fdt, owned ? "allwinner,sun50i-a133-powervr" : "img,gpu", gpu_node) ||
	    !a133_gpu_unique(fdt, owned ? "img,gpu" : "allwinner,sun50i-a133-powervr", -FDT_ERR_NOTFOUND) ||
	    !a133_gpu_active(fdt, ppu) || !a133_gpu_active(fdt, gpu) ||
	    !a133_cell(fdt, "/", "#address-cells", 2) ||
	    !a133_cell(fdt, "/", "#size-cells", 2) ||
	    !a133_cell(fdt, SOC, "#address-cells", 2) ||
	    !a133_cell(fdt, SOC, "#size-cells", 2) ||
	    !a133_value(fdt, SOC, "ranges", "", 0) ||
	    !a133_gpu_reg(fdt, ppu, 0x07001000, 0x40) ||
	    (!owned && !a133_gpu_reg(fdt, gpu, 0x01800000, 0x80000)) ||
	    !a133_gpu_reg(fdt, sram, 0x03000000, 0x100) ||
	    (domain = a133_phandle(fdt, ppu)) == 0 ||
	    !a133_cell(fdt, ppu, "#power-domain-cells", 0) ||
	    !a133_cell(fdt, gpu, "power-domains", domain) ||
	    (gic = a133_phandle(fdt, SDIO_GIC)) == 0 ||
	    fdt_node_check_compatible(fdt, fdt_path_offset(fdt, SDIO_GIC),
	    "arm,cortex-a15-gic") != 0 ||
	    !a133_cell(fdt, SDIO_GIC, "#interrupt-cells", 3) ||
	    !a133_value(fdt, SDIO_GIC, "interrupt-controller", "", 0) ||
	    (ccu = a133_phandle(fdt, CCU)) == 0 ||
	    !a133_cell(fdt, CCU, "#clock-cells", 1) ||
	    !a133_cell(fdt, CCU, "#reset-cells", 1) ||
	    (hosc = a133_phandle(fdt, "/ember-osc24m")) == 0 ||
	    (losc = a133_phandle(fdt, "/ember-osc32k")) == 0 ||
	    (iosc = a133_phandle(fdt, "/ember-iosc")) == 0)
		return -FDT_ERR_BADVALUE;
	pll_gpu = a133_phandle(fdt, "/clocks/pll_gpu");
	clk_gpu = a133_phandle(fdt, "/clocks/gpu");
	value = fdt_getprop(fdt, gpu_node, "gpu-supply", &len);
	if (pll_gpu == 0 || clk_gpu == 0 || value == NULL || len != 4)
		return -FDT_ERR_BADVALUE;
	supply = fdt32_to_cpu(*(const fdt32_t *)value);
	node = fdt_node_offset_by_phandle(fdt, supply);
	if (node < 0 || node != fdt_path_offset(fdt, rail) ||
	    a133_phandle_node(fdt, node) != supply ||
	    !a133_gpu_node_active(fdt, node) ||
	    !a133_value(fdt, pmu, "compatible", "x-powers,axp803",
	    sizeof("x-powers,axp803")) || !a133_cell(fdt, pmu, "reg", 0x34) ||
	    (value = fdt_getprop(fdt, node, "regulator-name", &len)) == NULL ||
	    len != sizeof("axp803-dcdc4") ||
	    memcmp(value, "axp803-dcdc4", sizeof("axp803-dcdc4")) != 0 ||
	    fdt_getprop(fdt, node, "regulator-always-on", &len) == NULL || len != 0 ||
	    fdt_getprop(fdt, node, "regulator-boot-on", &len) == NULL || len != 0 ||
	    !a133_value(fdt, "/clocks/pll_gpu", "compatible", "allwinner,pll-clock", sizeof("allwinner,pll-clock")) ||
	    !a133_value(fdt, "/clocks/pll_gpu", "clock-output-names", "pll_gpu", sizeof("pll_gpu")) ||
	    !a133_cell(fdt, "/clocks/pll_gpu", "#clock-cells", 0) ||
	    !a133_value(fdt, "/clocks/gpu", "compatible", "allwinner,periph-clock", sizeof("allwinner,periph-clock")) ||
	    !a133_value(fdt, "/clocks/gpu", "clock-output-names", "gpu", sizeof("gpu")) ||
	    !a133_cell(fdt, "/clocks/gpu", "#clock-cells", 0))
		return -FDT_ERR_BADVALUE;
	virtual_gpu = fdt_path_offset(fdt, vgpu);
	if (virtual_gpu >= 0) {
		if (!a133_value(fdt, vgpu, "compatible", "xpower-vregulator,dcdc4",
		    sizeof("xpower-vregulator,dcdc4")) ||
		    !a133_cell(fdt, vgpu, "dcdc4-supply", supply) ||
		    fdt_first_subnode(fdt, virtual_gpu) != -FDT_ERR_NOTFOUND ||
		    (owned && !a133_value(fdt, vgpu, "status", "disabled", sizeof("disabled"))) ||
		    (!owned && !a133_gpu_node_active(fdt, virtual_gpu)))
			return -FDT_ERR_BADVALUE;
	} else if (virtual_gpu != -FDT_ERR_NOTFOUND)
		return -FDT_ERR_BADVALUE;
	if (!a133_gpu_inventory(fdt, gpu_node, ccu, domain, supply, pll_gpu,
	    clk_gpu, virtual_gpu))
		return -FDT_ERR_BADVALUE;
	node = fdt_path_offset(fdt, ppu);
	if (!owned) {
		cells[0] = cpu_to_fdt32(pll_gpu); cells[1] = cpu_to_fdt32(clk_gpu);
		if (!a133_value(fdt, gpu, "clocks", cells, 8) ||
		    !a133_value(fdt, gpu, "clock-names", "clk_parent\0clk_mali", sizeof("clk_parent\0clk_mali")) ||
		    fdt_getprop(fdt, gpu_node, "resets", &len) != NULL ||
		    fdt_getprop(fdt, gpu_node, "ember,a133-gpu-exclusive", &len) != NULL)
			return -FDT_ERR_BADVALUE;
	} else {
		cells[0] = 0; cells[1] = cpu_to_fdt32(0x01800000);
		cells[2] = 0; cells[3] = cpu_to_fdt32(0x80000);
		cells[4] = 0; cells[5] = cpu_to_fdt32(0x01880020);
		cells[6] = 0; cells[7] = cpu_to_fdt32(4);
		if (!a133_value(fdt, gpu, "reg", cells, 32) ||
		    !a133_value(fdt, gpu, "reg-names", "gpu\0dfs", sizeof("gpu\0dfs")) ||
		    !a133_value(fdt, gpu, "ember,a133-gpu-exclusive", "", 0) ||
		    !a133_cell(fdt, gpu, "interrupt-parent", gic))
			return -FDT_ERR_BADVALUE;
		cells[0] = cpu_to_fdt32(ccu); cells[1] = cpu_to_fdt32(A100_CLK_GPU);
		cells[2] = cpu_to_fdt32(ccu); cells[3] = cpu_to_fdt32(A100_CLK_BUS_GPU);
		if (!a133_value(fdt, gpu, "clocks", cells, 16) ||
		    !a133_value(fdt, gpu, "clock-names", "core\0bus", sizeof("core\0bus")))
			return -FDT_ERR_BADVALUE;
		cells[1] = cpu_to_fdt32(A100_RST_BUS_GPU);
		if (!a133_value(fdt, gpu, "resets", cells, 8) ||
		    !a133_value(fdt, gpu, "reset-names", "bus", sizeof("bus")))
			return -FDT_ERR_BADVALUE;
	}
	cells[0] = 0; cells[1] = cpu_to_fdt32(119); cells[2] = cpu_to_fdt32(4);
	if (!a133_value(fdt, ppu, "interrupts", cells, 12))
		return -FDT_ERR_BADVALUE;
	cells[1] = cpu_to_fdt32(97);
	if (!a133_value(fdt, gpu, "interrupts", cells, 12))
		return -FDT_ERR_BADVALUE;
	value = fdt_getprop(fdt, node, "interrupt-parent", &len);
	if ((value != NULL && !a133_cell(fdt, ppu, "interrupt-parent", gic)) ||
	    fdt_getprop(fdt, node, "interrupts-extended", &len) != NULL)
		return -FDT_ERR_BADVALUE;
	value = fdt_getprop(fdt, gpu_node, "interrupt-parent", &len);
	if ((value != NULL && !a133_cell(fdt, gpu, "interrupt-parent", gic)) ||
	    fdt_getprop(fdt, gpu_node, "interrupts-extended", &len) != NULL)
		return -FDT_ERR_BADVALUE;
	if (owned) {
		rclock = a133_phandle(fdt, rccu);
		syscon = a133_phandle(fdt, sram);
		cells[0] = cpu_to_fdt32(rclock);
		cells[1] = cpu_to_fdt32(A100_R_CLK_APB1_PPU);
		if (rclock == 0 || syscon == 0 ||
		    !a133_value(fdt, ppu, "clocks", cells, 8) ||
		    !a133_value(fdt, gpu, "netbsd,consumer-managed-power", "", 0) ||
		    !a133_cell(fdt, ppu, "syscon", syscon) ||
		    !a133_cell(fdt, ppu, "interrupt-parent", gic) ||
		    !a133_gpu_reg(fdt, rccu, 0x07010000, 0x300) ||
		    !a133_gpu_active(fdt, rccu) ||
		    !a133_cell(fdt, rccu, "#clock-cells", 1) ||
		    !a133_cell(fdt, rccu, "#reset-cells", 1) ||
		    !a133_value(fdt, rccu, "compatible",
		    "allwinner,sun50i-a100-r-ccu", sizeof("allwinner,sun50i-a100-r-ccu")) ||
		    !a133_value(fdt, sram, "compatible", "allwinner,sram_ctrl\0syscon",
		    sizeof("allwinner,sram_ctrl\0syscon")) ||
		    !a133_value(fdt, ppu, "clock-names", "ppu", sizeof("ppu")) ||
		    !a133_value(fdt, ppu, "reset-names", "ppu", sizeof("ppu")))
			return -FDT_ERR_BADVALUE;
		cells[1] = cpu_to_fdt32(A100_R_RST_APB1_PPU);
		if (!a133_value(fdt, ppu, "resets", cells, 8))
			return -FDT_ERR_BADVALUE;
		cells[0] = cpu_to_fdt32(hosc); cells[1] = cpu_to_fdt32(losc);
		cells[2] = cpu_to_fdt32(iosc); cells[3] = cpu_to_fdt32(ccu);
		cells[4] = cpu_to_fdt32(A100_CLK_PLL_PERIPH0);
		if (!a133_value(fdt, rccu, "clocks", cells, 5 * sizeof(cells[0])) ||
		    !a133_value(fdt, rccu, "clock-names", "hosc\0losc\0iosc\0pll-periph",
		    sizeof("hosc\0losc\0iosc\0pll-periph")))
			return -FDT_ERR_BADVALUE;
		return 0;
	}
	legacy = a133_phandle(fdt, "/clocks/ppu");
	if (legacy == 0 || !a133_cell(fdt, ppu, "clocks", legacy) ||
	    !a133_cell(fdt, "/clocks/ppu", "#clock-cells", 0) ||
	    !a133_value(fdt, "/clocks/ppu", "compatible",
	    "allwinner,periph-cpus-clock", sizeof("allwinner,periph-cpus-clock")) ||
	    !a133_value(fdt, sram, "compatible", "allwinner,sram_ctrl",
	    sizeof("allwinner,sram_ctrl")) || fdt_path_offset(fdt, rccu) >= 0 ||
	    fdt_node_offset_by_compatible(fdt, -1,
	    "allwinner,sun50i-a100-r-ccu") >= 0 ||
	    fdt_getprop(fdt, node, "resets", &len) != NULL ||
	    fdt_getprop(fdt, node, "syscon", &len) != NULL ||
	    fdt_getprop(fdt, gpu_node, "netbsd,consumer-managed-power", &len) != NULL)
		return -FDT_ERR_BADVALUE;
	/* Reserve more than the complete new node and property payload. */
	total = fdt_totalsize(fdt);
	strings = fdt_off_dt_strings(fdt);
	size = fdt_size_dt_strings(fdt);
	if (strings > total || size > total - strings ||
	    total - strings - size < 2048)
		return -FDT_ERR_NOSPACE;

	/* Vendor virtual regulator is a test-only consumer; keep the rail. */
	if (virtual_gpu >= 0)
		A133_STRING(vgpu, "status", "disabled");
	node = a133_node(fdt, SOC, "clock-controller@07010000", &rclock);
	if (node < 0)
		return node;
	A133_STRING(rccu, "compatible", "allwinner,sun50i-a100-r-ccu");
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x07010000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x300);
	A133_SET(rccu, "reg", cells, 16);
	cells[0] = cpu_to_fdt32(hosc); cells[1] = cpu_to_fdt32(losc);
	cells[2] = cpu_to_fdt32(iosc); cells[3] = cpu_to_fdt32(ccu);
	cells[4] = cpu_to_fdt32(A100_CLK_PLL_PERIPH0);
	A133_SET(rccu, "clocks", cells, 5 * sizeof(cells[0]));
	A133_STRING(rccu, "clock-names", "hosc\0losc\0iosc\0pll-periph");
	A133_CELL(rccu, "#clock-cells", 1);
	A133_CELL(rccu, "#reset-cells", 1);
	node = a133_node(fdt, "/", "sram_ctrl", &syscon);
	if (node < 0)
		return node;
	A133_STRING(sram, "compatible", "allwinner,sram_ctrl\0syscon");
	cells[0] = cpu_to_fdt32(rclock);
	cells[1] = cpu_to_fdt32(A100_R_CLK_APB1_PPU);
	A133_SET(ppu, "clocks", cells, 8);
	A133_STRING(ppu, "clock-names", "ppu");
	cells[1] = cpu_to_fdt32(A100_R_RST_APB1_PPU);
	A133_SET(ppu, "resets", cells, 8);
	A133_STRING(ppu, "reset-names", "ppu");
	A133_CELL(ppu, "syscon", syscon);
	A133_CELL(ppu, "interrupt-parent", gic);
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x01800000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x80000);
	cells[4] = 0; cells[5] = cpu_to_fdt32(0x01880020);
	cells[6] = 0; cells[7] = cpu_to_fdt32(4);
	A133_SET(gpu, "reg", cells, 32);
	A133_STRING(gpu, "reg-names", "gpu\0dfs");
	cells[0] = cpu_to_fdt32(ccu); cells[1] = cpu_to_fdt32(A100_CLK_GPU);
	cells[2] = cpu_to_fdt32(ccu); cells[3] = cpu_to_fdt32(A100_CLK_BUS_GPU);
	A133_SET(gpu, "clocks", cells, 16);
	A133_STRING(gpu, "clock-names", "core\0bus");
	cells[1] = cpu_to_fdt32(A100_RST_BUS_GPU);
	A133_SET(gpu, "resets", cells, 8);
	A133_STRING(gpu, "reset-names", "bus");
	A133_CELL(gpu, "interrupt-parent", gic);
	A133_SET(gpu, "ember,a133-gpu-exclusive", NULL, 0);
	A133_STRING(gpu, "compatible", "allwinner,sun50i-a133-powervr");
	A133_SET(gpu, "netbsd,consumer-managed-power", NULL, 0);
	A133_STRING(ppu, "compatible", "allwinner,sun50i-a133-ppu");
	return a133_prop(fdt, ppu, marker, NULL, 0);
}

/* Host discovery only: preserve radio power and all legacy GPIO providers. */
static int
a133_sdio(void *fdt, uint32_t ccu, bool native)
{
	static const char pins[] = "PG0\0PG1\0PG2\0PG3\0PG4\0PG5";
	static const char *remove[] = {
		"vmmc-supply", "vqmmc-supply", "sd-uhs-sdr12",
		"sd-uhs-sdr25", "sd-uhs-sdr50", "sd-uhs-sdr104",
		"sd-uhs-ddr50", "pinctrl-1"
	};
	static const char gic_compat[] = "arm,cortex-a15-gic\0arm,cortex-a9-gic";
	static const uint32_t gic_reg[] = {
		0, 0x03021000, 0, 0x1000, 0, 0x03022000, 0, 0x2000,
		0, 0x03024000, 0, 0x2000, 0, 0x03026000, 0, 0x2000
	};
	static const char *reject[] = {
		"mmc-pwrseq", "interrupts-extended", "cd-gpios", "wp-gpios"
	};
	fdt32_t cells[16], cell;
	uint32_t rpio, legacy, pinhandle, gic, pwrseq = 0, channels = 0, supply = 0;
	const char *marker = native ? SDIO_NATIVE_MARK : SDIO_MARK;
	const fdt32_t *policy;
	const char *rail;
	bool owned, valid_marker, okay;
	int node, len, error, supply_node, pn;

	node = fdt_path_offset(fdt, SDIO);
	if (node < 0)
		return 0;
	if (native && fdt_getprop(fdt, node, SDIO_MARK, NULL) != NULL) {
		/* Withdraw the old diagnostic owner before a different policy. */
		A133_STRING(SDIO, "status", "disabled");
		node = fdt_path_offset(fdt, SDIO);
		error = fdt_delprop(fdt, node, SDIO_MARK);
		return error == -FDT_ERR_NOTFOUND ? 0 : error;
	}
	owned = fdt_getprop(fdt, node, marker, &len) != NULL;
	valid_marker = owned && len == 0;
	okay = a133_audio_okay(fdt, SDIO);
	if (owned) {
		/* Withdraw attachment before checking opt-in or altered resources. */
		A133_STRING(SDIO, "status", "disabled");
		node = fdt_path_offset(fdt, SDIO);
		error = fdt_delprop(fdt, node, marker);
		if (error != 0)
			return error;
	}
	if (!okay || (owned && !valid_marker) || !a133_audio_okay(fdt, WLAN))
		return 0;
	if (native) {
		if (fdt_getprop(fdt, 0, SDIO_NATIVE_OPT, &len) == NULL || len != 0 ||
		    fdt_getprop(fdt, 0, SDIO_PRESERVE_OPT, &len) == NULL || len != 0 ||
		    fdt_getprop(fdt, 0, "ember,ys-m33-sdio-probe", NULL) != NULL ||
		    fdt_getprop(fdt, 0, "ember,ys-m33-rtl8723ds-read-probe", NULL) != NULL ||
		    fdt_getprop(fdt, node, SDIO_MARK, NULL) != NULL ||
		    !a133_value(fdt, "/", "ember,rtw88-regdomain",
		    "world-min", sizeof("world-min")) ||
		    !a133_cell(fdt, "/", "ember,rtw8723d-phy-package", 0))
			return 0;
		policy = fdt_getprop(fdt, 0, "ember,rtw88-channels-2ghz", &len);
		if (policy == NULL || len != 4 ||
		    (channels = fdt32_to_cpu(*policy)) == 0 || (channels & ~0x1fffU) != 0)
			return 0;
		/* The factory tree names the always-on DLDO1 rail for WLAN and
		 * MMC1 I/O. Preserve its identity without taking regulator ownership. */
		if (!a133_value(fdt, WLAN, "wlan_power", "axp803-dldo1",
		    sizeof("axp803-dldo1")) ||
		    !a133_value(fdt, WLAN, "wlan_io_regulator", "", 0))
			return 0;
		node = fdt_path_offset(fdt, SDIO);
		if (!owned && fdt_getprop(fdt, node,
		    "ember,wlan-original-vqmmc", NULL) != NULL)
			return 0;
		policy = fdt_getprop(fdt, node, owned ?
		    "ember,wlan-original-vqmmc" : "vqmmc-supply", &len);
		if (policy == NULL || len != 4 ||
		    (supply = fdt32_to_cpu(*policy)) == 0 || supply == UINT32_MAX)
			return 0;
		supply_node = fdt_node_offset_by_phandle(fdt, supply);
		if (supply_node < 0 || a133_phandle_node(fdt, supply_node) != supply)
			return 0;
		rail = fdt_getprop(fdt, supply_node, "regulator-name", &len);
		if (rail == NULL || len != sizeof("axp803-dldo1") ||
		    memcmp(rail, "axp803-dldo1", sizeof("axp803-dldo1")) != 0)
			return 0;
		if (fdt_getprop(fdt, supply_node, "regulator-always-on", &len) == NULL ||
		    len != 0 || fdt_getprop(fdt, supply_node,
		    "regulator-boot-on", &len) == NULL || len != 0)
			return 0;
		if (owned) {
			pwrseq = a133_phandle(fdt, SDIO_PRESERVE);
			if (pwrseq == 0 ||
			    !a133_cell(fdt, SDIO, "mmc-pwrseq", pwrseq) ||
			    !a133_value(fdt, SDIO_PRESERVE, "compatible",
			    "mmc-pwrseq-simple", sizeof("mmc-pwrseq-simple")) ||
			    !a133_value(fdt, SDIO_PRESERVE,
			    "ember,boot-power-preserved", "", 0))
				return 0;
			pn = fdt_path_offset(fdt, SDIO_PRESERVE);
			if (fdt_getprop(fdt, pn, "reset-gpios", NULL) != NULL ||
			    fdt_getprop(fdt, pn, "clocks", NULL) != NULL ||
			    fdt_getprop(fdt, pn, "post-power-on-delay-ms", NULL) != NULL ||
			    fdt_getprop(fdt, pn, "power-off-delay-us", NULL) != NULL)
				return 0;
		} else if (fdt_path_offset(fdt, SDIO_PRESERVE) >= 0) {
			return 0; /* Never overwrite an unrelated provider. */
		}
	} else if (fdt_getprop(fdt, 0, "ember,ys-m33-sdio-probe", &len) == NULL ||
	    len != 0 || fdt_getprop(fdt, 0, SDIO_NATIVE_OPT, NULL) != NULL ||
	    fdt_getprop(fdt, node, SDIO_NATIVE_MARK, NULL) != NULL) {
		return 0;
	}
	if (fdt_stringlist_count(fdt, 0, "compatible") < 1 ||
	    !a133_cell(fdt, "/", "#address-cells", 2) ||
	    !a133_cell(fdt, "/", "#size-cells", 2) ||
	    !a133_cell(fdt, SOC, "#address-cells", 2) ||
	    !a133_cell(fdt, SOC, "#size-cells", 2) ||
	    !a133_value(fdt, SOC, "ranges", "", 0))
		return 0;
	if (!a133_value(fdt, SDIO, "compatible",
	    "allwinner,sunxi-mmc-v5p3x", sizeof("allwinner,sunxi-mmc-v5p3x")) &&
	    (!valid_marker || !a133_value(fdt, SDIO, "compatible",
	    "allwinner,sun50i-a100-mmc", sizeof("allwinner,sun50i-a100-mmc"))))
		return 0;
	node = fdt_path_offset(fdt, SDIO);
	for (unsigned int i = 0; i < sizeof(reject) / sizeof(reject[0]); i++)
		if (fdt_getprop(fdt, node, reject[i], &len) != NULL &&
		    !(native && owned && i == 0))
			return 0;
	node = fdt_path_offset(fdt, PIO);
	if (fdt_getprop(fdt, node, "vcc-pg-supply", &len) != NULL)
		return 0;
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x0300b000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x400);
	if (!a133_value(fdt, PIO, "reg", cells, 16))
		return 0;
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x04021000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x1000);
	if (!a133_value(fdt, SDIO, "reg", cells, 16) ||
	    !a133_cell(fdt, SDIO, "bus-width", 4) ||
	    !a133_value(fdt, SDIO, "device_type", "sdc1", 5))
		return 0;
	cells[0] = 0; cells[1] = cpu_to_fdt32(40); cells[2] = cpu_to_fdt32(4);
	if (!a133_value(fdt, SDIO, "interrupts", cells, 12) ||
	    !a133_value(fdt, SDIO_GROUP, "allwinner,pins",
	    pins, sizeof(pins)) ||
	    !a133_value(fdt, SDIO_GROUP, "allwinner,function", "sdc1", 5) ||
	    !a133_cell(fdt, SDIO_GROUP, "allwinner,muxsel", 2) ||
	    !a133_cell(fdt, SDIO_GROUP, "allwinner,drive", 3) ||
	    !a133_cell(fdt, SDIO_GROUP, "allwinner,pull", 1))
		return 0;
	legacy = a133_phandle(fdt, SDIO_GROUP);
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x07022000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x400);
	if (!a133_value(fdt, RPIO, "reg", cells, 16) ||
	    !a133_value(fdt, RPIO, "compatible",
	    "allwinner,sun50iw10p1-r-pinctrl",
	    sizeof("allwinner,sun50iw10p1-r-pinctrl")) ||
	    !a133_value(fdt, RPIO, "gpio-controller", "", 0) ||
	    (rpio = a133_phandle(fdt, RPIO)) == 0 ||
	    !a133_cell(fdt, RPIO, "#gpio-cells", 6) ||
	    !a133_cell(fdt, WLAN, "wlan_busnum", 1) || legacy == 0)
		return 0;
	if (!a133_value(fdt, WLAN, "compatible", "allwinner,sunxi-wlan",
	    sizeof("allwinner,sunxi-wlan")))
		return 0;
	cells[0] = cpu_to_fdt32(rpio); cells[1] = cpu_to_fdt32(11);
	cells[2] = cpu_to_fdt32(5); cells[3] = cpu_to_fdt32(1);
	cells[4] = cells[5] = cpu_to_fdt32(UINT32_MAX); cells[6] = 0;
	if (!a133_value(fdt, WLAN, "wlan_regon", cells, 28))
		return 0;
	cells[2] = cpu_to_fdt32(6); cells[3] = cpu_to_fdt32(6);
	if (!a133_value(fdt, WLAN, "wlan_hostwake", cells, 28))
		return 0;
	node = fdt_path_offset(fdt, SDIO_PINS);
	if (owned) {
		if (node < 0 || (pinhandle = a133_phandle(fdt, SDIO_PINS)) == 0 ||
		    !a133_cell(fdt, SDIO, "pinctrl-0", pinhandle) ||
		    !a133_value(fdt, SDIO_PINS, "pins", pins, sizeof(pins)) ||
		    !a133_value(fdt, SDIO_PINS, "function", "mmc1", 5))
			return 0;
	} else if (node >= 0 || !a133_cell(fdt, SDIO, "pinctrl-0", legacy))
		return 0;
	for (unsigned int i = 0; i < sizeof(gic_reg) / sizeof(gic_reg[0]); i++)
		cells[i] = cpu_to_fdt32(gic_reg[i]);
	if ((gic = a133_phandle(fdt, SDIO_GIC)) == 0 ||
	    !a133_value(fdt, SDIO_GIC, "reg",
	    cells, sizeof(cells)) ||
	    !a133_value(fdt, SDIO_GIC, "compatible",
	    gic_compat, sizeof(gic_compat)) ||
	    !a133_cell(fdt, SDIO_GIC, "#interrupt-cells", 3) ||
	    !a133_value(fdt, SDIO_GIC,
	    "interrupt-controller", "", 0))
		return 0;

	/* Keep the host disabled until all resource updates have succeeded. */
	A133_STRING(SDIO, "status", "disabled");
	node = a133_node(fdt, PIO, "ember-mmc1-pins", &pinhandle);
	if (node < 0)
		return node;
	A133_SET(SDIO_PINS, "pins", pins, sizeof(pins));
	A133_STRING(SDIO_PINS, "function", "mmc1");
	A133_CELL(SDIO_PINS, "drive-strength", 30);
	A133_SET(SDIO_PINS, "bias-pull-up", NULL, 0);
	cells[0] = cpu_to_fdt32(ccu); cells[1] = cpu_to_fdt32(A100_CLK_BUS_MMC1);
	cells[2] = cpu_to_fdt32(ccu); cells[3] = cpu_to_fdt32(A100_CLK_MMC1);
	A133_SET(SDIO, "clocks", cells, 16);
	A133_STRING(SDIO, "clock-names", "ahb\0mmc");
	cells[1] = cpu_to_fdt32(A100_RST_BUS_MMC1);
	A133_SET(SDIO, "resets", cells, 8);
	A133_STRING(SDIO, "reset-names", "ahb");
	A133_CELL(SDIO, "interrupt-parent", gic);
	A133_CELL(SDIO, "pinctrl-0", pinhandle);
	A133_STRING(SDIO, "pinctrl-names", "default");
	A133_CELL(SDIO, "max-frequency", 25000000);
	A133_SET(SDIO, "non-removable", NULL, 0);
	for (unsigned int i = 0; i < sizeof(remove) / sizeof(remove[0]); i++) {
		node = fdt_path_offset(fdt, SDIO);
		error = fdt_delprop(fdt, node, remove[i]);
		if (error != 0 && error != -FDT_ERR_NOTFOUND)
			return error;
	}
	if (native) {
		node = a133_node(fdt, "/", "ember-wlan-preserved-power", &pwrseq);
		if (node < 0)
			return node;
		A133_STRING(SDIO_PRESERVE, "compatible", "mmc-pwrseq-simple");
		A133_SET(SDIO_PRESERVE, "ember,boot-power-preserved", NULL, 0);
		A133_CELL(SDIO, "mmc-pwrseq", pwrseq);
		A133_CELL(SDIO, "ember,wlan-original-vqmmc", supply);
		A133_STRING(SDIO, "ember,rtw88-regdomain", "world-min");
		A133_CELL(SDIO, "ember,rtw88-channels-2ghz", channels);
		/* Package-neutral selector for the selected 8723D table image,
		 * not a measured physical package. No package-dependent rows. */
		A133_CELL(SDIO, "ember,rtw8723d-phy-package", 0);
	}
	A133_SET(SDIO, marker, NULL, 0);
	A133_STRING(SDIO, "compatible", "allwinner,sun50i-a100-mmc");
	A133_STRING(SDIO, "status", "okay");
	return 0;
}

#ifdef A133_YS_M33_WLAN_WARM
/* This explicitly selected trial preserves bootloader-established power.
 * It does not establish a cold power sequence or a country qualification. */
static int
a133_sdio_warm_policy(void *fdt)
{
	static const char *const policy[] = {
		SDIO_NATIVE_OPT, SDIO_PRESERVE_OPT, "ember,rtw88-regdomain",
		"ember,rtw88-channels-2ghz", "ember,rtw8723d-phy-package"
	};
	fdt32_t cell;
	int error;

	if (fdt_getprop(fdt, 0, "ember,ys-m33-sdio-probe", NULL) != NULL ||
	    fdt_getprop(fdt, 0, "ember,ys-m33-rtl8723ds-read-probe", NULL) != NULL)
		return 0;
	for (size_t i = 0; i < sizeof(policy) / sizeof(policy[0]); i++) {
		if (fdt_getprop(fdt, 0, policy[i], NULL) != NULL)
			return 0;
	}
	A133_SET("/", SDIO_NATIVE_OPT, "", 0);
	A133_SET("/", SDIO_PRESERVE_OPT, "", 0);
	A133_STRING("/", "ember,rtw88-regdomain", "world-min");
	A133_CELL("/", "ember,rtw88-channels-2ghz", 0x07ff);
	A133_CELL("/", "ember,rtw8723d-phy-package", 0);
	return 0;
}
#endif

int
sun50i_a133_fdt_fixup(void *fdt)
{
	static const char mmc_pins[] = "PC0\0PC1\0PC5\0PC6\0PC8\0PC9\0"
	    "PC10\0PC11\0PC13\0PC14\0PC15\0PC16";
	static const char *fast_modes[] = {
		"mmc-hs400-1_8v", "mmc-hs200-1_8v", "mmc-ddr-1_8v"
	};
	fdt32_t cells[6], cell;
	uint32_t hosc, losc, iosc, ccu, pins;
	int node, error;

	error = fdt_check_header(fdt);
	if (error != 0)
		return error;
	if (fdt_node_check_compatible(fdt, 0, "allwinner,a133") != 0)
		return 0;
	if (fdt_path_offset(fdt, MMC) < 0 || fdt_path_offset(fdt, PIO) < 0)
		return -FDT_ERR_NOTFOUND;

	error = a133_clock(fdt, "ember-osc24m", 24000000, &hosc);
	if (error != 0)
		return error;
	error = a133_clock(fdt, "ember-osc32k", 32768, &losc);
	if (error != 0)
		return error;
	error = a133_clock(fdt, "ember-iosc", 16000000, &iosc);
	if (error != 0)
		return error;
	node = a133_node(fdt, SOC, "clock-controller@03001000", &ccu);
	if (node < 0)
		return node;
	A133_STRING(CCU, "compatible",
	    "allwinner,sun50i-a133-ccu\0allwinner,sun50i-a100-ccu");
	cells[0] = 0;
	cells[1] = cpu_to_fdt32(0x03001000);
	cells[2] = 0;
	cells[3] = cpu_to_fdt32(0x1000);
	A133_SET(CCU, "reg", cells, 4 * sizeof(cells[0]));
	cells[0] = cpu_to_fdt32(hosc);
	cells[1] = cpu_to_fdt32(losc);
	cells[2] = cpu_to_fdt32(iosc);
	A133_SET(CCU, "clocks", cells, 3 * sizeof(cells[0]));
	A133_STRING(CCU, "clock-names", "hosc\0losc\0iosc");
	A133_CELL(CCU, "#clock-cells", 1);
	A133_CELL(CCU, "#reset-cells", 1);

	A133_STRING(PIO, "compatible", "allwinner,sun50i-a100-pinctrl");
	cells[0] = cpu_to_fdt32(ccu);
	cells[1] = cpu_to_fdt32(A100_CLK_APB1);
	A133_SET(PIO, "clocks", cells, 2 * sizeof(cells[0]));
	node = a133_node(fdt, PIO, "ember-mmc2-pins", &pins);
	if (node < 0)
		return node;
	A133_SET(PINS, "pins", mmc_pins, sizeof(mmc_pins));
	A133_STRING(PINS, "function", "mmc2");
	A133_CELL(PINS, "drive-strength", 30);
	A133_SET(PINS, "bias-pull-up", NULL, 0);

	A133_STRING(MMC, "compatible", "allwinner,sun50i-a100-emmc");
	cells[0] = cpu_to_fdt32(ccu);
	cells[1] = cpu_to_fdt32(A100_CLK_BUS_MMC2);
	cells[2] = cpu_to_fdt32(ccu);
	cells[3] = cpu_to_fdt32(A100_CLK_MMC2);
	A133_SET(MMC, "clocks", cells, 4 * sizeof(cells[0]));
	A133_STRING(MMC, "clock-names", "ahb\0mmc");
	cells[0] = cpu_to_fdt32(ccu);
	cells[1] = cpu_to_fdt32(A100_RST_BUS_MMC2);
	A133_SET(MMC, "resets", cells, 2 * sizeof(cells[0]));
	A133_STRING(MMC, "reset-names", "ahb");
	A133_CELL(MMC, "pinctrl-0", pins);
	A133_STRING(MMC, "pinctrl-names", "default");
	A133_CELL(MMC, "max-frequency", 25000000);
	for (unsigned int i = 0; i < sizeof(fast_modes) / sizeof(fast_modes[0]); i++) {
		node = fdt_path_offset(fdt, MMC);
		error = fdt_delprop(fdt, node, fast_modes[i]);
		if (error != 0 && error != -FDT_ERR_NOTFOUND)
			return error;
	}
	A133_STRING(MMC, "status", "okay");
	/*
	 * The vendor firmware leaves S_TWI0 on PL0/PL1 running from HOSC:
	 * R_APB2_CFG is zero and R_TWI_BGR is 0x00010001 on the YS-M33.
	 * Preserve that clock/reset/pin route while attaching the common
	 * A31-compatible TWI controller.  Its children include the board
	 * MCU7502 watchdog and the PCF8563 clock.
	 */
	if (fdt_path_offset(fdt, SOC "/s_twi@0x07081400") >= 0) {
		A133_STRING(SOC "/s_twi@0x07081400", "compatible",
		    "allwinner,sun6i-a31-i2c");
		cells[0] = cpu_to_fdt32(hosc);
		A133_SET(SOC "/s_twi@0x07081400", "clocks", cells,
		    sizeof(cells[0]));
	}
	/* Translate the optional touch bus without changing its children. */
	if (fdt_path_offset(fdt, SOC "/twi@0x05002c00") >= 0) {
		node = a133_node(fdt, PIO, "ember-i2c3-pins", &pins);
		if (node < 0)
			return node;
		A133_STRING(PIO "/ember-i2c3-pins", "pins", "PH12\0PH13");
		A133_STRING(PIO "/ember-i2c3-pins", "function", "i2c3");
		A133_CELL(PIO "/ember-i2c3-pins", "drive-strength", 20);
		A133_SET(PIO "/ember-i2c3-pins", "bias-pull-up", NULL, 0);
		A133_STRING(SOC "/twi@0x05002c00", "compatible",
		    "allwinner,sun6i-a31-i2c");
		cells[0] = cpu_to_fdt32(ccu);
		cells[1] = cpu_to_fdt32(A100_CLK_BUS_I2C3);
		A133_SET(SOC "/twi@0x05002c00", "clocks", cells,
		    2 * sizeof(cells[0]));
		cells[1] = cpu_to_fdt32(A100_RST_BUS_I2C3);
		A133_SET(SOC "/twi@0x05002c00", "resets", cells,
		    2 * sizeof(cells[0]));
		A133_CELL(SOC "/twi@0x05002c00", "pinctrl-0", pins);
		A133_STRING(SOC "/twi@0x05002c00", "pinctrl-names", "default");
	}
	/* Copy the provider value before any update can move its property. */
	node = fdt_path_offset(fdt, "/interrupt-controller@03020000");
	if (node >= 0) {
		uint32_t gic = fdt_get_phandle(fdt, node);

		if (gic != 0) {
			A133_CELL("/", "interrupt-parent", gic);
			if (fdt_path_offset(fdt, SOC "/uart@05000000") >= 0)
				A133_CELL(SOC "/uart@05000000",
				    "interrupt-parent", gic);
		}
	}
	error = a133_usb1(fdt, ccu);
	if (error != 0)
		return error;
#ifdef A133_YS_M33_WLAN_WARM
	error = a133_sdio_warm_policy(fdt);
	if (error != 0)
		return error;
#endif
	node = fdt_path_offset(fdt, SDIO);
	error = a133_sdio(fdt, ccu,
	    fdt_getprop(fdt, 0, SDIO_NATIVE_OPT, NULL) != NULL ||
	    (node >= 0 && fdt_getprop(fdt, node, SDIO_NATIVE_MARK, NULL) != NULL));
	if (error != 0)
		return error;
	error = a133_keys(fdt, ccu);
	if (error != 0)
		return error;
	error = a133_audio(fdt);
	if (error != 0)
		return error;
	error = a133_touch_reset(fdt);
	if (error != 0)
		return error;
	return a133_framebuffer(fdt);
}
