/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; validate vendor PPU resources without device access. */
#define main existing_fdt_contract
#include "a133-fdt-test.c"
#undef main

#define PPU "/gpu-power-domain"
#define GPU "/gpu"
#define SRAM "/sram_ctrl"
#define RCCU "/soc@03000000/clock-controller@07010000"
#define CCU "/soc@03000000/clock-controller@03001000"
#define MARK "ember,a133-vendor-ppu"
#define VGPU "/soc@03000000/s_twi@0x07081400/pmu@34/virtual-dcdc4"

int sun50i_a133_gpu_fdt_fixup(void *);

static void
gpu_fixture(void *fdt)
{
	fdt32_t cells[4];
	int node, clocks;

	fixture(fdt, 16384, "allwinner,a133");
	assert(fdt_setprop_u32(fdt, 0, "#address-cells", 2) == 0);
	assert(fdt_setprop_u32(fdt, 0, "#size-cells", 2) == 0);
	node = fdt_path_offset(fdt, "/soc@03000000");
	assert(fdt_setprop(fdt, node, "ranges", NULL, 0) == 0);
	node = fdt_path_offset(fdt, "/interrupt-controller@03020000");
	assert(fdt_setprop_u32(fdt, node, "#interrupt-cells", 3) == 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "arm,cortex-a15-gic") == 0);
	assert(fdt_setprop(fdt, node, "interrupt-controller", NULL, 0) == 0);
	clocks = fdt_add_subnode(fdt, 0, "clocks");
	assert(clocks >= 0);
	node = fdt_add_subnode(fdt, clocks, "ppu");
	assert(node >= 0);
	assert(fdt_setprop_string(fdt, node, "compatible",
	    "allwinner,periph-cpus-clock") == 0);
	assert(fdt_setprop_string(fdt, node, "clock-output-names", "ppu") == 0);
	assert(fdt_setprop_u32(fdt, node, "#clock-cells", 0) == 0);
	assert(fdt_setprop_u32(fdt, node, "phandle", 266) == 0);
	node = fdt_add_subnode(fdt, clocks, "pll_gpu");
	assert(node >= 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "allwinner,pll-clock") == 0);
	assert(fdt_setprop_string(fdt, node, "clock-output-names", "pll_gpu") == 0);
	assert(fdt_setprop_u32(fdt, node, "#clock-cells", 0) == 0);
	assert(fdt_setprop_u32(fdt, node, "phandle", 267) == 0);
	node = fdt_add_subnode(fdt, clocks, "gpu");
	assert(node >= 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "allwinner,periph-clock") == 0);
	assert(fdt_setprop_string(fdt, node, "clock-output-names", "gpu") == 0);
	assert(fdt_setprop_u32(fdt, node, "#clock-cells", 0) == 0);
	assert(fdt_setprop_u32(fdt, node, "phandle", 268) == 0);
	node = fdt_add_subnode(fdt, 0, "sram_ctrl");
	assert(node >= 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "allwinner,sram_ctrl") == 0);
	cells[0] = cells[2] = 0;
	cells[1] = cpu_to_fdt32(0x03000000);
	cells[3] = cpu_to_fdt32(0x100);
	assert(fdt_setprop(fdt, node, "reg", cells, sizeof(cells)) == 0);
	node = fdt_add_subnode(fdt, 0, "gpu-power-domain");
	assert(node >= 0);
	assert(fdt_setprop(fdt, node, "compatible", "allwinner,gpu-pd\0syscon",
	    sizeof("allwinner,gpu-pd\0syscon")) == 0);
	assert(fdt_setprop_u32(fdt, node, "phandle", 274) == 0);
	assert(fdt_setprop_u32(fdt, node, "#power-domain-cells", 0) == 0);
	cells[1] = cpu_to_fdt32(0x07001000);
	cells[3] = cpu_to_fdt32(0x40);
	assert(fdt_setprop(fdt, node, "reg", cells, sizeof(cells)) == 0);
	cells[0] = 0; cells[1] = cpu_to_fdt32(119); cells[2] = cpu_to_fdt32(4);
	assert(fdt_setprop(fdt, node, "interrupts", cells, 12) == 0);
	assert(fdt_setprop_u32(fdt, node, "clocks", 266) == 0);
	assert(fdt_setprop_string(fdt, node, "clock-names", "ppu") == 0);
	node = fdt_add_subnode(fdt, fdt_path_offset(fdt,
	    "/soc@03000000/s_twi@0x07081400"), "pmu@34");
	assert(node >= 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "x-powers,axp803") == 0);
	assert(fdt_setprop_u32(fdt, node, "reg", 0x34) == 0);
	node = fdt_add_subnode(fdt, node, "regulators");
	assert(node >= 0);
	node = fdt_add_subnode(fdt, node, "dcdc4");
	assert(node >= 0);
	assert(fdt_setprop_u32(fdt, node, "phandle", 275) == 0);
	assert(fdt_setprop_string(fdt, node, "regulator-name", "axp803-dcdc4") == 0);
	assert(fdt_setprop(fdt, node, "regulator-always-on", NULL, 0) == 0);
	assert(fdt_setprop(fdt, node, "regulator-boot-on", NULL, 0) == 0);
	node = fdt_add_subnode(fdt, fdt_path_offset(fdt,
	    "/soc@03000000/s_twi@0x07081400/pmu@34"), "virtual-dcdc4");
	assert(node >= 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "xpower-vregulator,dcdc4") == 0);
	assert(fdt_setprop_u32(fdt, node, "dcdc4-supply", 275) == 0);
	node = fdt_add_subnode(fdt, 0, "gpu");
	assert(node >= 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "img,gpu") == 0);
	cells[0] = cpu_to_fdt32(267); cells[1] = cpu_to_fdt32(268);
	assert(fdt_setprop(fdt, node, "clocks", cells, 8) == 0);
	assert(fdt_setprop(fdt, node, "clock-names", "clk_parent\0clk_mali",
	    sizeof("clk_parent\0clk_mali")) == 0);
	assert(fdt_setprop_u32(fdt, node, "power-domains", 274) == 0);
	assert(fdt_setprop_u32(fdt, node, "gpu-supply", 275) == 0);
	cells[0] = cells[2] = 0;
	cells[1] = cpu_to_fdt32(0x01800000); cells[3] = cpu_to_fdt32(0x80000);
	assert(fdt_setprop(fdt, node, "reg", cells, sizeof(cells)) == 0);
	cells[0] = 0; cells[1] = cpu_to_fdt32(97); cells[2] = cpu_to_fdt32(4);
	assert(fdt_setprop(fdt, node, "interrupts", cells, 12) == 0);
	/* Opaque vendor payload stands for panel, PMIC and MCU properties. */
	node = fdt_add_subnode(fdt, 0, "vendor-board-data");
	assert(node >= 0);
	assert(fdt_setprop(fdt, node, "panel-pmic-mcu", "\1\0\377\3\0\4", 6) == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
}

static uint32_t
cell(const void *fdt, const char *path, const char *name)
{
	const fdt32_t *value;
	int len;

	value = fdt_getprop(fdt, fdt_path_offset(fdt, path), name, &len);
	assert(value != NULL && len == 4);
	return fdt32_to_cpu(*value);
}

int
main(void)
{
	unsigned char fdt[16384], before[16384];
	const fdt32_t *cells;
	const void *data;
	uint32_t rccu, syscon;
	int node, len;

	gpu_fixture(fdt);
	assert(sun50i_a133_gpu_fdt_fixup(fdt) == 0);
	node = fdt_path_offset(fdt, GPU);
	assert(fdt_node_check_compatible(fdt, node,
	    "allwinner,sun50i-a133-powervr") == 0);
	data = fdt_getprop(fdt, node, "ember,a133-gpu-exclusive", &len);
	assert(data != NULL && len == 0);
	data = fdt_getprop(fdt, node, "reg-names", &len);
	assert(data != NULL && len == sizeof("gpu\0dfs") &&
	    memcmp(data, "gpu\0dfs", sizeof("gpu\0dfs")) == 0);
	cells = fdt_getprop(fdt, node, "reg", &len);
	assert(cells != NULL && len == 32 && fdt32_to_cpu(cells[1]) == 0x01800000 &&
	    fdt32_to_cpu(cells[3]) == 0x80000 && fdt32_to_cpu(cells[5]) == 0x01880020 &&
	    fdt32_to_cpu(cells[7]) == 4);
	cells = fdt_getprop(fdt, node, "clocks", &len);
	assert(cells != NULL && len == 16 && fdt32_to_cpu(cells[1]) == 36 &&
	    fdt32_to_cpu(cells[3]) == 37 && cells[0] == cells[2]);
	assert(fdt32_to_cpu(cells[0]) == fdt_get_phandle(fdt, fdt_path_offset(fdt, CCU)));
	cells = fdt_getprop(fdt, node, "resets", &len);
	assert(cells != NULL && len == 8 && fdt32_to_cpu(cells[1]) == 3);
	node = fdt_path_offset(fdt, PPU);
	assert(fdt_get_phandle(fdt, node) == 274);
	assert(fdt_stringlist_search(fdt, node, "compatible", "allwinner,sun50i-a133-ppu") == 0);
	assert(fdt_node_check_compatible(fdt, node, "syscon") != 0);
	assert(cell(fdt, PPU, "#power-domain-cells") == 0);
	rccu = fdt_get_phandle(fdt, fdt_path_offset(fdt, RCCU));
	assert(rccu != 0);
	cells = fdt_getprop(fdt, node, "clocks", &len);
	assert(cells != NULL && len == 8 && fdt32_to_cpu(cells[0]) == rccu &&
	    fdt32_to_cpu(cells[1]) == 8);
	cells = fdt_getprop(fdt, node, "resets", &len);
	assert(cells != NULL && len == 8 && fdt32_to_cpu(cells[0]) == rccu &&
	    fdt32_to_cpu(cells[1]) == 2);
	syscon = cell(fdt, PPU, "syscon");
	assert(syscon != 0 && fdt_node_offset_by_phandle(fdt, syscon) ==
	    fdt_path_offset(fdt, SRAM));
	assert(fdt_stringlist_search(fdt, fdt_path_offset(fdt, VGPU),
	    "status", "disabled") == 0);
	assert(cell(fdt, GPU, "power-domains") == 274);
	data = fdt_getprop(fdt, fdt_path_offset(fdt, GPU),
	    "netbsd,consumer-managed-power", &len);
	assert(data != NULL && len == 0);
	data = fdt_getprop(fdt, fdt_path_offset(fdt, "/vendor-board-data"),
	    "panel-pmic-mcu", &len);
	assert(data != NULL && len == 6 && memcmp(data, "\1\0\377\3\0\4", 6) == 0);
	memcpy(before, fdt, sizeof(fdt));
	assert(sun50i_a133_gpu_fdt_fixup(fdt) == 0);
	assert(memcmp(before, fdt, sizeof(fdt)) == 0);
	/* Re-entry must validate providers, not trust an old ownership marker. */
	for (unsigned i = 0; i < 7; i++) {
		gpu_fixture(fdt);
		assert(sun50i_a133_gpu_fdt_fixup(fdt) == 0);
		if (i == 0)
			assert(fdt_setprop_string(fdt, fdt_path_offset(fdt, RCCU),
			    "compatible", "unknown,clock-controller") == 0);
		else if (i == 1)
			assert(fdt_setprop_u32(fdt, fdt_path_offset(fdt, RCCU),
			    "clocks", 99) == 0);
		else if (i == 2)
			assert(fdt_setprop_string(fdt, fdt_path_offset(fdt, RCCU),
			    "clock-names", "other") == 0);
		else if (i == 3)
			assert(fdt_setprop_string(fdt, fdt_path_offset(fdt, PPU),
			    "clock-names", "other") == 0);
		else if (i == 4)
			assert(fdt_setprop_string(fdt, fdt_path_offset(fdt, SRAM),
			    "compatible", "unknown,syscon") == 0);
		else if (i == 5)
			assert(fdt_setprop_string(fdt, fdt_path_offset(fdt,
			    "/interrupt-controller@03020000"), "compatible",
			    "unknown,interrupt-controller") == 0);
		else {
			int duplicate = fdt_add_subnode(fdt, 0, "duplicate-phandle");
			assert(duplicate >= 0);
			assert(fdt_setprop_u32(fdt, duplicate, "phandle", 274) == 0);
		}
		memcpy(before, fdt, sizeof(fdt));
		assert(sun50i_a133_gpu_fdt_fixup(fdt) == -FDT_ERR_BADVALUE);
		assert(memcmp(before, fdt, sizeof(fdt)) == 0);
	}
	/* Reject unknown resources before modifying any property. */
	for (unsigned i = 0; i < 7; i++) {
		gpu_fixture(fdt);
		node = fdt_path_offset(fdt, PPU);
		if (i == 0) {
			fdt32_t bad[4] = {0, cpu_to_fdt32(0x07002000), 0, cpu_to_fdt32(0x40)};
			assert(fdt_setprop(fdt, node, "reg", bad, sizeof(bad)) == 0);
		} else if (i == 1) {
			fdt32_t bad[3] = {0, cpu_to_fdt32(118), cpu_to_fdt32(4)};
			assert(fdt_setprop(fdt, node, "interrupts", bad, sizeof(bad)) == 0);
		} else if (i == 2) {
			assert(fdt_setprop_u32(fdt, node, "clocks", 99) == 0);
		} else if (i == 3) {
			assert(fdt_setprop_u32(fdt, fdt_path_offset(fdt, GPU), "power-domains", 99) == 0);
		} else if (i == 4) {
			assert(fdt_add_subnode(fdt, fdt_path_offset(fdt, "/soc@03000000"),
			    "clock-controller@07010000") >= 0);
		} else if (i == 5) {
			int duplicate = fdt_add_subnode(fdt, 0, "other-ppu");
			assert(duplicate >= 0);
			assert(fdt_setprop_string(fdt, duplicate, "compatible", "allwinner,gpu-pd") == 0);
		} else {
			int duplicate = fdt_add_subnode(fdt, 0, "duplicate-phandle");
			assert(duplicate >= 0);
			assert(fdt_setprop_u32(fdt, duplicate, "phandle", 274) == 0);
		}
		memcpy(before, fdt, sizeof(fdt));
		assert(sun50i_a133_gpu_fdt_fixup(fdt) == -FDT_ERR_BADVALUE);
		assert(memcmp(before, fdt, sizeof(fdt)) == 0);
	}
	/* The vendor test consumer and each legacy clock are exact bindings. */
	for (unsigned i = 0; i < 6; i++) {
		gpu_fixture(fdt);
		node = fdt_path_offset(fdt, VGPU);
		if (i == 0)
			assert(fdt_setprop_string(fdt, node, "compatible", "unknown") == 0);
		else if (i == 1)
			assert(fdt_setprop_u32(fdt, node, "dcdc4-supply", 274) == 0);
		else if (i == 2)
			assert(fdt_setprop_u32(fdt, node, "extra-supply", 275) == 0);
		else if (i == 3)
			assert(fdt_add_subnode(fdt, node, "unexpected") >= 0);
		else if (i == 4)
			assert(fdt_setprop_string(fdt, fdt_path_offset(fdt, "/clocks/pll_gpu"),
			    "clock-output-names", "bad") == 0);
		else
			assert(fdt_setprop_u32(fdt, fdt_path_offset(fdt, "/clocks/gpu"),
			    "#clock-cells", 1) == 0);
		memcpy(before, fdt, sizeof(fdt));
		assert(sun50i_a133_gpu_fdt_fixup(fdt) == -FDT_ERR_BADVALUE);
		assert(memcmp(before, fdt, sizeof(fdt)) == 0);
	}
	/* A forged supply parent must not reach an unguarded regulator setter. */
	for (unsigned i = 0; i < 2; i++) {
		gpu_fixture(fdt);
		node = fdt_path_offset(fdt, "/soc@03000000/s_twi@0x07081400/pmu@34");
		if (i == 0)
			assert(fdt_setprop_string(fdt, node, "compatible", "unknown,pmic") == 0);
		else
			assert(fdt_setprop_u32(fdt, node, "reg", 0x35) == 0);
		memcpy(before, fdt, sizeof(fdt));
		assert(sun50i_a133_gpu_fdt_fixup(fdt) == -FDT_ERR_BADVALUE);
		assert(memcmp(before, fdt, sizeof(fdt)) == 0);
	}
	/* Every conflicting active claimant refuses before any mutation. */
	for (unsigned i = 0; i < 10; i++) {
		fdt32_t claim[4];
		int other;

		gpu_fixture(fdt);
		other = fdt_add_subnode(fdt, 0, "other-claimant");
		assert(other >= 0);
		if (i < 2) {
			claim[0] = claim[2] = 0;
			claim[1] = cpu_to_fdt32(i == 0 ? 0x01800000 : 0x01880020);
			claim[3] = cpu_to_fdt32(4);
			assert(fdt_setprop(fdt, other, "reg", claim, 16) == 0);
		} else if (i < 7) {
			claim[0] = cpu_to_fdt32(cell(fdt, CCU, "phandle"));
			claim[1] = cpu_to_fdt32(i == 2 || i == 6 ? 36 : i == 3 ? 37 : i == 4 ? 7 : 3);
			assert(fdt_setprop(fdt, other, i == 5 ? "resets" :
			    i == 6 ? "assigned-clocks" : "clocks", claim, 8) == 0);
		} else if (i == 7) {
			assert(fdt_setprop_u32(fdt, other, "power-domains", 274) == 0);
		} else if (i == 8) {
			assert(fdt_setprop_u32(fdt, other, "vdd-supply", 275) == 0);
		} else {
			assert(fdt_setprop_u32(fdt, other, "clocks", 267) == 0);
		}
		memcpy(before, fdt, sizeof(fdt));
		assert(sun50i_a133_gpu_fdt_fixup(fdt) == -FDT_ERR_BADVALUE);
		assert(memcmp(before, fdt, sizeof(fdt)) == 0);
	}
	puts("PASS: vendor PPU resources, ownership, preservation and idempotence");
	return 0;
}
