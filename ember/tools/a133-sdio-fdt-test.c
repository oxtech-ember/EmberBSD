/* Origin: EmberBSD - exercise opt-in YS-M33 SDIO resource translation. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define main existing_fdt_contract
#include "a133-fdt-test.c"
#undef main

#define HOST "/soc@03000000/sdmmc@04021000"
#define GROUP "/soc@03000000/pinctrl@0300b000/sdc1@0"
#define WLAN "/soc@03000000/wlan@0"
#define RPIO "/soc@03000000/pinctrl@07022000"
#define MARK "ember,ys-m33-sdio-host"
#define OPT "ember,ys-m33-sdio-probe"

static void
sdio_fixture(void *fdt, const char *root)
{
	static const char pins[] = "PG0\0PG1\0PG2\0PG3\0PG4\0PG5";
	fdt32_t cells[16];
	int node, parent;

	fixture(fdt, 16384, root);
	assert(fdt_setprop_u32(fdt, 0, "#address-cells", 2) == 0);
	assert(fdt_setprop_u32(fdt, 0, "#size-cells", 2) == 0);
	node = fdt_path_offset(fdt, "/soc@03000000");
	assert(fdt_setprop(fdt, node, "ranges", NULL, 0) == 0);
	node = fdt_path_offset(fdt, "/soc@03000000/pinctrl@0300b000");
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x0300b000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x400);
	assert(fdt_setprop(fdt, node, "reg", cells, 16) == 0);
	node = fdt_path_offset(fdt, "/interrupt-controller@03020000");
	assert(fdt_setprop(fdt, node, "compatible",
	    "arm,cortex-a15-gic\0arm,cortex-a9-gic", sizeof("arm,cortex-a15-gic\0arm,cortex-a9-gic")) == 0);
	assert(fdt_setprop_u32(fdt, node, "#interrupt-cells", 3) == 0);
	assert(fdt_setprop(fdt, node, "interrupt-controller", NULL, 0) == 0);
	memset(cells, 0, sizeof(cells));
	cells[1] = cpu_to_fdt32(0x03021000); cells[3] = cpu_to_fdt32(0x1000);
	cells[5] = cpu_to_fdt32(0x03022000); cells[7] = cpu_to_fdt32(0x2000);
	cells[9] = cpu_to_fdt32(0x03024000); cells[11] = cpu_to_fdt32(0x2000);
	cells[13] = cpu_to_fdt32(0x03026000); cells[15] = cpu_to_fdt32(0x2000);
	assert(fdt_setprop(fdt, node, "reg", cells, sizeof(cells)) == 0);
	node = fdt_path_offset(fdt, RPIO);
	assert(fdt_setprop_string(fdt, node, "compatible",
	    "allwinner,sun50iw10p1-r-pinctrl") == 0);
	assert(fdt_setprop(fdt, node, "gpio-controller", NULL, 0) == 0);
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x07022000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x400);
	assert(fdt_setprop(fdt, node, "reg", cells, 16) == 0);
	assert(fdt_setprop(fdt, 0, OPT, NULL, 0) == 0);
	node = fdt_path_offset(fdt, RPIO);
	assert(fdt_setprop_u32(fdt, node, "#gpio-cells", 6) == 0);
	parent = fdt_path_offset(fdt, "/soc@03000000/pinctrl@0300b000");
	node = fdt_add_subnode(fdt, parent, "sdc1@0");
	assert(node >= 0);
	assert(fdt_setprop_u32(fdt, node, "phandle", 120) == 0);
	assert(fdt_setprop(fdt, node, "allwinner,pins", pins, sizeof(pins)) == 0);
	assert(fdt_setprop_string(fdt, node, "allwinner,function", "sdc1") == 0);
	assert(fdt_setprop_u32(fdt, node, "allwinner,muxsel", 2) == 0);
	assert(fdt_setprop_u32(fdt, node, "allwinner,drive", 3) == 0);
	assert(fdt_setprop_u32(fdt, node, "allwinner,pull", 1) == 0);
	parent = fdt_path_offset(fdt, "/soc@03000000");
	node = fdt_add_subnode(fdt, parent, "sdmmc@04021000");
	assert(node >= 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "allwinner,sunxi-mmc-v5p3x") == 0);
	assert(fdt_setprop_string(fdt, node, "device_type", "sdc1") == 0);
	assert(fdt_setprop_string(fdt, node, "status", "okay") == 0);
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x04021000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x1000);
	assert(fdt_setprop(fdt, node, "reg", cells, 16) == 0);
	cells[0] = 0; cells[1] = cpu_to_fdt32(40); cells[2] = cpu_to_fdt32(4);
	assert(fdt_setprop(fdt, node, "interrupts", cells, 12) == 0);
	assert(fdt_setprop_u32(fdt, node, "bus-width", 4) == 0);
	assert(fdt_setprop_u32(fdt, node, "pinctrl-0", 120) == 0);
	assert(fdt_setprop_u32(fdt, node, "vqmmc-supply", 199) == 0);
	assert(fdt_setprop(fdt, node, "sd-uhs-sdr104", NULL, 0) == 0);
	parent = fdt_path_offset(fdt, "/soc@03000000");
	node = fdt_add_subnode(fdt, parent, "wlan@0");
	assert(node >= 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "allwinner,sunxi-wlan") == 0);
	assert(fdt_setprop_string(fdt, node, "status", "okay") == 0);
	assert(fdt_setprop_u32(fdt, node, "wlan_busnum", 1) == 0);
	cells[0] = cpu_to_fdt32(104); cells[1] = cpu_to_fdt32(11);
	cells[2] = cpu_to_fdt32(5); cells[3] = cpu_to_fdt32(1);
	cells[4] = cells[5] = cpu_to_fdt32(UINT32_MAX); cells[6] = 0;
	assert(fdt_setprop(fdt, node, "wlan_regon", cells, 28) == 0);
	cells[2] = cpu_to_fdt32(6); cells[3] = cpu_to_fdt32(6);
	assert(fdt_setprop(fdt, node, "wlan_hostwake", cells, 28) == 0);
}

static void
check_host(void *fdt)
{
	const fdt32_t *cells;
	int node, provider, len;

	node = fdt_path_offset(fdt, HOST);
	assert(fdt_getprop(fdt, node, MARK, &len) != NULL && len == 0);
	assert(fdt_node_check_compatible(fdt, node, "allwinner,sun50i-a100-mmc") == 0);
	assert(fdt_getprop(fdt, node, "non-removable", &len) != NULL);
	assert(fdt_getprop(fdt, node, "vqmmc-supply", &len) == NULL);
	assert(fdt_getprop(fdt, node, "sd-uhs-sdr104", &len) == NULL);
	cells = fdt_getprop(fdt, node, "max-frequency", &len);
	assert(cells != NULL && len == 4 && fdt32_to_cpu(*cells) == 25000000);
	cells = fdt_getprop(fdt, node, "clocks", &len);
	assert(cells != NULL && len == 16 && fdt32_to_cpu(cells[1]) == 67 && fdt32_to_cpu(cells[3]) == 63);
	provider = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(cells[0]));
	assert(provider == fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(cells[2])));
	assert(provider == fdt_path_offset(fdt, "/soc@03000000/clock-controller@03001000"));
	cells = fdt_getprop(fdt, node, "resets", &len);
	assert(cells != NULL && len == 8 && fdt32_to_cpu(cells[1]) == 16);
	cells = fdt_getprop(fdt, node, "interrupt-parent", &len);
	assert(cells != NULL && len == 4 && fdt32_to_cpu(*cells) == 42);
	cells = fdt_getprop(fdt, node, "pinctrl-0", &len);
	assert(cells != NULL && len == 4);
	provider = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*cells));
	assert(provider == fdt_path_offset(fdt, "/soc@03000000/pinctrl@0300b000/ember-mmc1-pins"));
	assert(strcmp(fdt_getprop(fdt, provider, "function", &len), "mmc1") == 0);
	node = fdt_path_offset(fdt, "/soc@03000000/sdmmc@04022000");
	cells = fdt_getprop(fdt, node, "clocks", &len);
	assert(cells != NULL && len == 16 && fdt32_to_cpu(cells[1]) == 68 && fdt32_to_cpu(cells[3]) == 64);
	node = fdt_path_offset(fdt, WLAN);
	cells = fdt_getprop(fdt, node, "wlan_regon", &len);
	assert(cells != NULL && len == 28 && fdt32_to_cpu(cells[0]) == 104 && fdt32_to_cpu(cells[2]) == 5);
	node = fdt_path_offset(fdt, RPIO);
	cells = fdt_getprop(fdt, node, "#gpio-cells", &len);
	assert(cells != NULL && len == 4 && fdt32_to_cpu(*cells) == 6);
}

int
main(void)
{
	unsigned char fdt[16384], before[16384];
	fdt32_t cells[7];
	const void *p;
	int node, len, which;

	sdio_fixture(fdt, "allwinner,a133");
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	check_host(fdt);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	check_host(fdt);
	assert(fdt_delprop(fdt, 0, OPT) == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	node = fdt_path_offset(fdt, HOST);
	assert(fdt_getprop(fdt, node, MARK, &len) == NULL);
	assert(strcmp(fdt_getprop(fdt, node, "status", &len), "disabled") == 0);
	for (which = 0; which < 6; which++) {
		sdio_fixture(fdt, "allwinner,a133");
		assert(sun50i_a133_fdt_fixup(fdt) == 0);
		node = fdt_path_offset(fdt, which == 0 || which == 5 ? HOST : WLAN);
		if (which == 0) assert(fdt_setprop_u32(fdt, node, MARK, 1) == 0);
		if (which == 1) assert(fdt_setprop_u32(fdt, node, "wlan_busnum", 0) == 0);
		if (which == 2) assert(fdt_setprop_string(fdt, node, "status", "disabled") == 0);
		if (which == 3) assert(fdt_setprop_u32(fdt, 0, OPT, 1) == 0);
		if (which == 4) {
			node = fdt_path_offset(fdt, "/soc@03000000/pinctrl@0300b000");
			assert(fdt_setprop_u32(fdt, node, "vcc-pg-supply", 199) == 0);
		}
		if (which == 5) assert(fdt_setprop_u32(fdt, node, "interrupts-extended", 99) == 0);
		assert(sun50i_a133_fdt_fixup(fdt) == 0);
		node = fdt_path_offset(fdt, HOST);
		assert(fdt_getprop(fdt, node, MARK, &len) == NULL);
		assert(strcmp(fdt_getprop(fdt, node, "status", &len), "disabled") == 0);
	}

	for (which = 0; which < 28; which++) {
		sdio_fixture(fdt, "allwinner,a133");
		node = fdt_path_offset(fdt, HOST);
		if (which == 0) assert(fdt_delprop(fdt, 0, OPT) == 0);
		if (which == 1) assert(fdt_setprop_u32(fdt, 0, OPT, 1) == 0);
		if (which == 2) assert(fdt_setprop_string(fdt, node, "status", "disabled") == 0);
		if (which == 3) assert(fdt_setprop_u32(fdt, node, "bus-width", 8) == 0);
		if (which == 4) assert(fdt_setprop_u32(fdt, node, "pinctrl-0", 99) == 0);
		if (which == 5) {
			p = fdt_getprop(fdt, node, "reg", &len); memcpy(cells, p, 16);
			cells[1] = cpu_to_fdt32(0x04022000);
			assert(fdt_setprop(fdt, node, "reg", cells, 16) == 0);
		}
		if (which == 6) {
			p = fdt_getprop(fdt, node, "interrupts", &len); memcpy(cells, p, 12);
			cells[1] = cpu_to_fdt32(41);
			assert(fdt_setprop(fdt, node, "interrupts", cells, 12) == 0);
		}
		if (which == 7 || which == 8 || which == 9 || which == 10) {
			node = fdt_path_offset(fdt, WLAN);
			if (which == 7) assert(fdt_setprop_string(fdt, node, "status", "disabled") == 0);
			if (which == 8) assert(fdt_setprop_u32(fdt, node, "wlan_busnum", 2) == 0);
			if (which == 9 || which == 10) {
				p = fdt_getprop(fdt, node, "wlan_regon", &len); memcpy(cells, p, 28);
				cells[which == 9 ? 0 : 2] = cpu_to_fdt32(which == 9 ? 99 : 0);
				assert(fdt_setprop(fdt, node, "wlan_regon", cells, 28) == 0);
			}
		}
		if (which == 11) {
			node = fdt_path_offset(fdt, GROUP);
			assert(fdt_setprop_string(fdt, node, "allwinner,pins", "PG6") == 0);
		}
		if (which == 12) {
			node = fdt_path_offset(fdt, RPIO);
			assert(fdt_setprop_u32(fdt, node, "#gpio-cells", 3) == 0);
		}
		if (which == 13) {
			node = fdt_path_offset(fdt, "/soc@03000000/pinctrl@0300b000");
			assert(fdt_setprop_u32(fdt, node, "vcc-pg-supply", 199) == 0);
		}
		if (which == 14) assert(fdt_setprop_u32(fdt, node, "interrupts-extended", 99) == 0);
		if (which == 15 || which == 16 || which == 17) {
			node = fdt_path_offset(fdt, "/soc@03000000");
			if (which == 15) assert(fdt_setprop_u32(fdt, node, "#address-cells", 1) == 0);
			if (which == 16) assert(fdt_setprop_u32(fdt, node, "#size-cells", 1) == 0);
			if (which == 17) assert(fdt_setprop_u32(fdt, node, "ranges", 1) == 0);
		}
		if (which == 18 || which == 19) {
			node = fdt_path_offset(fdt, RPIO);
			if (which == 18) assert(fdt_setprop_string(fdt, node, "compatible", "foreign,rpio") == 0);
			if (which == 19) assert(fdt_setprop_u32(fdt, node, "reg", 1) == 0);
		}
		if (which == 20 || which == 21 || which == 22) {
			node = fdt_path_offset(fdt, "/interrupt-controller@03020000");
			if (which == 20) assert(fdt_setprop_string(fdt, node, "compatible", "foreign,gic") == 0);
			if (which == 21) assert(fdt_setprop_u32(fdt, node, "#interrupt-cells", 1) == 0);
			if (which == 22) assert(fdt_setprop_u32(fdt, node, "reg", 1) == 0);
		}
		if (which == 23) assert(fdt_setprop(fdt, node, "compatible",
		    "allwinner,sunxi-mmc-v5p3x\0x", sizeof("allwinner,sunxi-mmc-v5p3x\0x") - 1) == 0);
		if (which == 25) {
			node = fdt_path_offset(fdt, WLAN);
			assert(fdt_setprop(fdt, node, "compatible", "allwinner,sunxi-wlan\0x",
			    sizeof("allwinner,sunxi-wlan\0x") - 1) == 0);
		}
		if (which == 26) {
			node = fdt_path_offset(fdt, RPIO);
			assert(fdt_setprop_u32(fdt, node, "phandle", 42) == 0);
		}
		if (which == 27) assert(fdt_setprop(fdt, 0, "compatible", "allwinner,a133\0x",
		    sizeof("allwinner,a133\0x") - 1) == 0);
		if (which == 24) {
			node = fdt_path_offset(fdt, "/soc@03000000/pinctrl@0300b000");
			assert(fdt_setprop_u32(fdt, node, "reg", 1) == 0);
		}
		assert(sun50i_a133_fdt_fixup(fdt) == 0);
		node = fdt_path_offset(fdt, HOST);
		assert(fdt_getprop(fdt, node, MARK, &len) == NULL);
		assert(fdt_node_check_compatible(fdt, node, "allwinner,sunxi-mmc-v5p3x") == 0);
	}
	sdio_fixture(fdt, "brcm,bcm2712");
	memcpy(before, fdt, sizeof(fdt));
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	assert(memcmp(before, fdt, sizeof(fdt)) == 0);
	/* An unowned canonical host is not disabled by missing probe opt-in. */
	sdio_fixture(fdt, "allwinner,a133");
	assert(fdt_delprop(fdt, 0, OPT) == 0);
	node = fdt_path_offset(fdt, HOST);
	assert(fdt_setprop_string(fdt, node, "compatible", "allwinner,sun50i-a100-mmc") == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	node = fdt_path_offset(fdt, HOST);
	assert(strcmp(fdt_getprop(fdt, node, "status", &len), "okay") == 0);
	/* Partial resource updates may never expose an enabled canonical host. */
	for (which = 0; which <= 256; which += 16) {
		int error;

		sdio_fixture(fdt, "allwinner,a133");
		assert(fdt_delprop(fdt, 0, OPT) == 0);
		assert(sun50i_a133_fdt_fixup(fdt) == 0);
		assert(fdt_setprop(fdt, 0, OPT, NULL, 0) == 0);
		assert(fdt_pack(fdt) == 0);
		assert(fdt_open_into(fdt, before, fdt_totalsize(fdt) + which) == 0);
		error = sun50i_a133_fdt_fixup(before);
		assert(error == 0 || error == -FDT_ERR_NOSPACE);
		node = fdt_path_offset(before, HOST);
		if (error == 0) check_host(before);
		else assert(fdt_node_check_compatible(before, node,
		    "allwinner,sun50i-a100-mmc") != 0 ||
		    strcmp(fdt_getprop(before, node, "status", &len), "disabled") == 0);
	}
	sdio_fixture(fdt, "allwinner,a133");
	assert(fdt_pack(fdt) == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == -FDT_ERR_NOSPACE);
	puts("A133 opt-in SDIO FDT contract passed");
	return 0;
}
