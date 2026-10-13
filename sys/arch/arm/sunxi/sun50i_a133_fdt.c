/* Origin: EmberBSD - adapt verified A133 vendor boot resources. */
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
#include <sys/systm.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "sun50i_a100_ccu.h"

#define SOC "/soc@03000000"
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

int sun50i_a133_fdt_fixup(void *);

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
	return 0;
}

static bool
a133_sdio_value(const void *fdt, const char *path, const char *name,
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
a133_sdio_cell(const void *fdt, const char *path, const char *name,
    uint32_t value)
{
	fdt32_t cell = cpu_to_fdt32(value);

	return a133_sdio_value(fdt, path, name, &cell, sizeof(cell));
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
		    !a133_sdio_cell(fdt, paths[i], "hci_ctrl_no", 1) ||
		    !a133_sdio_cell(fdt, paths[i], "drvvbus-supply", vbus))
			return 0;
		p = fdt_getprop(fdt, node, "reg", &len);
		if (p == NULL || len != 64 || fdt32_to_cpu(p[0]) != 0 ||
		    fdt32_to_cpu(p[1]) != 0x05200000 || p[2] != 0 ||
		    fdt32_to_cpu(p[3]) != 0xfff)
			return 0;
		cells[0] = 0; cells[1] = cpu_to_fdt32(33 + i);
		cells[2] = cpu_to_fdt32(4);
		if (!a133_sdio_value(fdt, paths[i], "interrupts", cells, 12))
			return 0;
	}
	node = fdt_path_offset(fdt, "/misc_power_en");
	if (node < 0 || !a133_sdio_cell(fdt, "/misc_power_en",
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
			if (!a133_sdio_cell(fdt, "/misc_power_en", name, 1))
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
a133_sdio_phandle(const void *fdt, const char *path)
{
	uint32_t phandle;
	int node;

	node = fdt_path_offset(fdt, path);
	if (node < 0)
		return 0;
	phandle = fdt_get_phandle(fdt, node);
	if (phandle == 0 || phandle == UINT32_MAX ||
	    fdt_node_offset_by_phandle(fdt, phandle) != node)
		return 0;
	return phandle;
}

/* Host discovery only: preserve radio power and all legacy GPIO providers. */
static int
a133_sdio(void *fdt, uint32_t ccu)
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
	uint32_t rpio, legacy, pinhandle, gic;
	bool owned, valid_marker, okay;
	int node, len, error;

	node = fdt_path_offset(fdt, SDIO);
	if (node < 0)
		return 0;
	owned = fdt_getprop(fdt, node, SDIO_MARK, &len) != NULL;
	valid_marker = owned && len == 0;
	okay = a133_audio_okay(fdt, SDIO);
	if (owned) {
		/* Withdraw attachment before checking opt-in or altered resources. */
		A133_STRING(SDIO, "status", "disabled");
		node = fdt_path_offset(fdt, SDIO);
		error = fdt_delprop(fdt, node, SDIO_MARK);
		if (error != 0)
			return error;
	}
	if (fdt_getprop(fdt, 0, "ember,ys-m33-sdio-probe", &len) == NULL ||
	    len != 0 || !okay || (owned && !valid_marker) ||
	    !a133_audio_okay(fdt, WLAN))
		return 0;
	if (fdt_stringlist_count(fdt, 0, "compatible") < 1 ||
	    !a133_sdio_cell(fdt, "/", "#address-cells", 2) ||
	    !a133_sdio_cell(fdt, "/", "#size-cells", 2) ||
	    !a133_sdio_cell(fdt, SOC, "#address-cells", 2) ||
	    !a133_sdio_cell(fdt, SOC, "#size-cells", 2) ||
	    !a133_sdio_value(fdt, SOC, "ranges", "", 0))
		return 0;
	if (!a133_sdio_value(fdt, SDIO, "compatible",
	    "allwinner,sunxi-mmc-v5p3x", sizeof("allwinner,sunxi-mmc-v5p3x")) &&
	    (!valid_marker || !a133_sdio_value(fdt, SDIO, "compatible",
	    "allwinner,sun50i-a100-mmc", sizeof("allwinner,sun50i-a100-mmc"))))
		return 0;
	node = fdt_path_offset(fdt, SDIO);
	for (unsigned int i = 0; i < sizeof(reject) / sizeof(reject[0]); i++)
		if (fdt_getprop(fdt, node, reject[i], &len) != NULL)
			return 0;
	node = fdt_path_offset(fdt, PIO);
	if (fdt_getprop(fdt, node, "vcc-pg-supply", &len) != NULL)
		return 0;
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x0300b000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x400);
	if (!a133_sdio_value(fdt, PIO, "reg", cells, 16))
		return 0;
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x04021000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x1000);
	if (!a133_sdio_value(fdt, SDIO, "reg", cells, 16) ||
	    !a133_sdio_cell(fdt, SDIO, "bus-width", 4) ||
	    !a133_sdio_value(fdt, SDIO, "device_type", "sdc1", 5))
		return 0;
	cells[0] = 0; cells[1] = cpu_to_fdt32(40); cells[2] = cpu_to_fdt32(4);
	if (!a133_sdio_value(fdt, SDIO, "interrupts", cells, 12) ||
	    !a133_sdio_value(fdt, SDIO_GROUP, "allwinner,pins",
	    pins, sizeof(pins)) ||
	    !a133_sdio_value(fdt, SDIO_GROUP, "allwinner,function", "sdc1", 5) ||
	    !a133_sdio_cell(fdt, SDIO_GROUP, "allwinner,muxsel", 2) ||
	    !a133_sdio_cell(fdt, SDIO_GROUP, "allwinner,drive", 3) ||
	    !a133_sdio_cell(fdt, SDIO_GROUP, "allwinner,pull", 1))
		return 0;
	legacy = a133_sdio_phandle(fdt, SDIO_GROUP);
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x07022000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x400);
	if (!a133_sdio_value(fdt, RPIO, "reg", cells, 16) ||
	    !a133_sdio_value(fdt, RPIO, "compatible",
	    "allwinner,sun50iw10p1-r-pinctrl",
	    sizeof("allwinner,sun50iw10p1-r-pinctrl")) ||
	    !a133_sdio_value(fdt, RPIO, "gpio-controller", "", 0) ||
	    (rpio = a133_sdio_phandle(fdt, RPIO)) == 0 ||
	    !a133_sdio_cell(fdt, RPIO, "#gpio-cells", 6) ||
	    !a133_sdio_cell(fdt, WLAN, "wlan_busnum", 1) || legacy == 0)
		return 0;
	if (!a133_sdio_value(fdt, WLAN, "compatible", "allwinner,sunxi-wlan",
	    sizeof("allwinner,sunxi-wlan")))
		return 0;
	cells[0] = cpu_to_fdt32(rpio); cells[1] = cpu_to_fdt32(11);
	cells[2] = cpu_to_fdt32(5); cells[3] = cpu_to_fdt32(1);
	cells[4] = cells[5] = cpu_to_fdt32(UINT32_MAX); cells[6] = 0;
	if (!a133_sdio_value(fdt, WLAN, "wlan_regon", cells, 28))
		return 0;
	cells[2] = cpu_to_fdt32(6); cells[3] = cpu_to_fdt32(6);
	if (!a133_sdio_value(fdt, WLAN, "wlan_hostwake", cells, 28))
		return 0;
	node = fdt_path_offset(fdt, SDIO_PINS);
	if (owned) {
		if (node < 0 || (pinhandle = a133_sdio_phandle(fdt, SDIO_PINS)) == 0 ||
		    !a133_sdio_cell(fdt, SDIO, "pinctrl-0", pinhandle) ||
		    !a133_sdio_value(fdt, SDIO_PINS, "pins", pins, sizeof(pins)) ||
		    !a133_sdio_value(fdt, SDIO_PINS, "function", "mmc1", 5))
			return 0;
	} else if (node >= 0 || !a133_sdio_cell(fdt, SDIO, "pinctrl-0", legacy))
		return 0;
	for (unsigned int i = 0; i < sizeof(gic_reg) / sizeof(gic_reg[0]); i++)
		cells[i] = cpu_to_fdt32(gic_reg[i]);
	if ((gic = a133_sdio_phandle(fdt, SDIO_GIC)) == 0 ||
	    !a133_sdio_value(fdt, SDIO_GIC, "reg",
	    cells, sizeof(cells)) ||
	    !a133_sdio_value(fdt, SDIO_GIC, "compatible",
	    gic_compat, sizeof(gic_compat)) ||
	    !a133_sdio_cell(fdt, SDIO_GIC, "#interrupt-cells", 3) ||
	    !a133_sdio_value(fdt, SDIO_GIC,
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
	A133_SET(SDIO, SDIO_MARK, NULL, 0);
	A133_STRING(SDIO, "compatible", "allwinner,sun50i-a100-mmc");
	A133_STRING(SDIO, "status", "okay");
	return 0;
}

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
	A133_STRING(CCU, "compatible", "allwinner,sun50i-a100-ccu");
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
	error = a133_sdio(fdt, ccu);
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
