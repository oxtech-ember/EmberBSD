/* Origin: EmberBSD - validate the YS-M33 USB-A host resource translation. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define main existing_fdt_contract
#include "a133-fdt-test.c"
#undef main
#define SOC "/soc@03000000"
#define EHCI SOC "/ehci1-controller@0x05200000"
#define OHCI SOC "/ohci1-controller@0x05200400"
#define PHY SOC "/ember-usb1-phy"

static void
usb_fixture(void *fdt)
{
	fdt32_t cells[16] = {0};
	int soc, node, i;

	fixture(fdt, 32768, "allwinner,a133");
	node = fdt_add_subnode(fdt, 0, "usb1-vbus");
	assert(node >= 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "regulator-fixed") == 0);
	assert(fdt_setprop_u32(fdt, node, "phandle", 122) == 0);
	assert(fdt_setprop(fdt, node, "enable-active-high", NULL, 0) == 0);
	assert(fdt_setprop_string(fdt, node, "status", "okay") == 0);
	node = fdt_add_subnode(fdt, 0, "misc_power_en");
	assert(node >= 0);
	cells[0] = cpu_to_fdt32(99); cells[1] = cpu_to_fdt32(5);
	cells[2] = cpu_to_fdt32(2); cells[3] = cpu_to_fdt32(1);
	cells[4] = cpu_to_fdt32(1); cells[5] = 0; cells[6] = cpu_to_fdt32(1);
	assert(fdt_setprop(fdt, node, "vcc_host_drv1_gpio", cells, 28) == 0);
	assert(fdt_setprop_u32(fdt, node, "vcc_host_drv1_gpio_level", 1) == 0);
	{
		static const char *names[] = {"vcc_host_drv0", "vcc_host_drv2",
		    "vcc_host_drv3", "vcc_hub_drv0"};
		static const uint32_t pins[] = {0, 3, 4, 5};
		char name[48];
		for (i = 0; i < 4; i++) {
			cells[2] = cpu_to_fdt32(pins[i]);
			snprintf(name, sizeof(name), "%s_gpio", names[i]);
			assert(fdt_setprop(fdt, node, name, cells, 28) == 0);
			snprintf(name, sizeof(name), "%s_gpio_level", names[i]);
			assert(fdt_setprop_u32(fdt, node, name, 1) == 0);
		}
	}
	for (i = 0; i < 2; i++) {
		soc = fdt_path_offset(fdt, SOC);
		node = fdt_add_subnode(fdt, soc, i == 0 ?
		    "ehci1-controller@0x05200000" : "ohci1-controller@0x05200400");
		assert(node >= 0);
		assert(fdt_setprop_string(fdt, node, "compatible", i == 0 ?
		    "allwinner,sunxi-ehci1" : "allwinner,sunxi-ohci1") == 0);
		assert(fdt_setprop_string(fdt, node, "status", "okay") == 0);
		memset(cells, 0, sizeof(cells)); cells[1] = cpu_to_fdt32(0x05200000);
		cells[3] = cpu_to_fdt32(0xfff);
		assert(fdt_setprop(fdt, node, "reg", cells, sizeof(cells)) == 0);
		cells[0] = 0; cells[1] = cpu_to_fdt32(33 + i); cells[2] = cpu_to_fdt32(4);
		assert(fdt_setprop(fdt, node, "interrupts", cells, 12) == 0);
		assert(fdt_setprop_u32(fdt, node, "hci_ctrl_no", 1) == 0);
		assert(fdt_setprop_u32(fdt, node, "drvvbus-supply", 122) == 0);
	}
}

int
main(void)
{
	unsigned char fdt[32768];
	const fdt32_t *cells;
	int node, len, which;

	usb_fixture(fdt);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	node = fdt_path_offset(fdt, EHCI);
	assert(fdt_node_check_compatible(fdt, node, "generic-ehci") == 0);
	node = fdt_path_offset(fdt, OHCI);
	assert(fdt_node_check_compatible(fdt, node, "generic-ohci") == 0);
	cells = fdt_getprop(fdt, node, "reg", &len);
	assert(cells != NULL && len == 16 && fdt32_to_cpu(cells[1]) == 0x05200400);
	node = fdt_path_offset(fdt, PHY);
	assert(fdt_node_check_compatible(fdt, node, "allwinner,sun50i-a100-usb-phy") == 0);
	cells = fdt_getprop(fdt, node, "clocks", &len);
	assert(cells != NULL && len == 8 && fdt32_to_cpu(cells[1]) == 111);
	cells = fdt_getprop(fdt, node, "usb1-power-gpios", &len);
	assert(cells != NULL && len == 112 && fdt32_to_cpu(cells[23]) == 5);
	node = fdt_path_offset(fdt, "/usb1-vbus");
	cells = fdt_getprop(fdt, node, "gpio", &len);
	assert(cells != NULL && len == 28 && fdt32_to_cpu(cells[2]) == 2);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	for (which = 0; which < 5; which++) {
		usb_fixture(fdt);
		node = fdt_path_offset(fdt, which == 2 ? "/misc_power_en" : EHCI);
		if (which == 0)
			assert(fdt_setprop_string(fdt, node, "status", "disabled") == 0);
		else if (which == 1)
			assert(fdt_setprop_u32(fdt, node, "hci_ctrl_no", 0) == 0);
		else if (which == 2)
			assert(fdt_setprop_u32(fdt, node, "vcc_host_drv1_gpio_level", 0) == 0);
		else if (which == 3)
			assert(fdt_setprop_u32(fdt, node, "drvvbus-supply", 999) == 0);
		else {
			fdt32_t irq[3] = {0, cpu_to_fdt32(30), cpu_to_fdt32(4)};
			assert(fdt_setprop(fdt, node, "interrupts", irq, 12) == 0);
		}
		assert(sun50i_a133_fdt_fixup(fdt) == 0);
		assert(fdt_path_offset(fdt, PHY) == -FDT_ERR_NOTFOUND);
	}
	puts("A133 USB-A FDT: host resources, OHCI base, VBUS, guards and repeat passed");
	return 0;
}
