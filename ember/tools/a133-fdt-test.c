/* Origin: EmberBSD - exercise the early A133 resource translation contract. */
/* Exercise the actual early FDT adapter without accessing hardware. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

int sun50i_a133_fdt_fixup(void *);

static void
fixture(void *fdt, int size, const char *compat)
{
	int soc, pio, mmc, twi, gic, uart, touch, rpio;
	fdt32_t gpio[7];

	assert(fdt_create_empty_tree(fdt, size) == 0);
	assert(fdt_setprop_string(fdt, 0, "compatible", compat) == 0);
	/* Subnodes are prepended: keep the GIC after the UART in the blob. */
	gic = fdt_add_subnode(fdt, 0, "interrupt-controller@03020000");
	assert(gic >= 0);
	assert(fdt_setprop_u32(fdt, gic, "phandle", 42) == 0);
	soc = fdt_add_subnode(fdt, 0, "soc@03000000");
	assert(soc >= 0);
	assert(fdt_setprop_u32(fdt, soc, "#address-cells", 2) == 0);
	assert(fdt_setprop_u32(fdt, soc, "#size-cells", 2) == 0);
	uart = fdt_add_subnode(fdt, soc, "uart@05000000");
	assert(uart >= 0);
	pio = fdt_add_subnode(fdt, soc, "pinctrl@0300b000");
	assert(pio >= 0);
	assert(fdt_setprop_string(fdt, pio, "compatible",
	    "allwinner,sun50i-pinctrl") == 0);
	assert(fdt_setprop_u32(fdt, pio, "phandle", 99) == 0);
	soc = fdt_path_offset(fdt, "/soc@03000000");
	rpio = fdt_add_subnode(fdt, soc, "pinctrl@07022000");
	assert(rpio >= 0);
	assert(fdt_setprop_u32(fdt, rpio, "phandle", 104) == 0);
	soc = fdt_path_offset(fdt, "/soc@03000000");
	mmc = fdt_add_subnode(fdt, soc, "sdmmc@04022000");
	assert(mmc >= 0);
	assert(fdt_setprop_string(fdt, mmc, "compatible",
	    "allwinner,sunxi-mmc-v4p6x") == 0);
	assert(fdt_setprop_string(fdt, mmc, "status", "disabled") == 0);
	assert(fdt_setprop(fdt, mmc, "mmc-hs400-1_8v", NULL, 0) == 0);
	assert(fdt_setprop(fdt, mmc, "mmc-hs200-1_8v", NULL, 0) == 0);
	assert(fdt_setprop(fdt, mmc, "mmc-ddr-1_8v", NULL, 0) == 0);
	soc = fdt_path_offset(fdt, "/soc@03000000");
	twi = fdt_add_subnode(fdt, soc, "s_twi@0x07081400");
	assert(twi >= 0);
	assert(fdt_setprop_string(fdt, twi, "compatible",
	    "allwinner,sun50i-twi") == 0);
	soc = fdt_path_offset(fdt, "/soc@03000000");
	twi = fdt_add_subnode(fdt, soc, "twi@0x05002c00");
	assert(twi >= 0);
	assert(fdt_setprop_string(fdt, twi, "compatible",
	    "allwinner,sun50i-twi") == 0);
	touch = fdt_add_subnode(fdt, twi, "goodix_ts@5d");
	assert(touch >= 0);
	assert(fdt_setprop_string(fdt, touch, "compatible", "goodix,gt9xx") == 0);
	assert(fdt_setprop_u32(fdt, touch, "reg", 0x5d) == 0);
	memset(gpio, 0, sizeof(gpio));
	gpio[0] = cpu_to_fdt32(99); gpio[1] = cpu_to_fdt32(7);
	gpio[2] = cpu_to_fdt32(14);
	assert(fdt_setprop(fdt, touch, "goodix,rst-gpio", gpio, sizeof(gpio)) == 0);
	gpio[0] = cpu_to_fdt32(104); gpio[1] = cpu_to_fdt32(11);
	gpio[2] = cpu_to_fdt32(10);
	assert(fdt_setprop(fdt, touch, "goodix,irq-gpio", gpio, sizeof(gpio)) == 0);
}

int
main(void)
{
	unsigned char *fdt = malloc(16384), *before = malloc(16384);
	const fdt32_t *cells;
	uint64_t addr, size;
	int mmc, provider, len, twi, chosen, fb;

	assert(fdt != NULL && before != NULL);
	fixture(fdt, 16384, "allwinner,a133");
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	cells = fdt_getprop(fdt, 0, "interrupt-parent", &len);
	assert(cells != NULL && len == 4 && fdt32_to_cpu(*cells) == 42);
	provider = fdt_path_offset(fdt, "/soc@03000000/uart@05000000");
	cells = fdt_getprop(fdt, provider, "interrupt-parent", &len);
	assert(cells != NULL && len == 4 && fdt32_to_cpu(*cells) == 42);
	assert(fdt_node_offset_by_phandle(fdt, 42) ==
	    fdt_path_offset(fdt, "/interrupt-controller@03020000"));
	twi = fdt_path_offset(fdt, "/soc@03000000/s_twi@0x07081400");
	assert(fdt_node_check_compatible(fdt, twi,
	    "allwinner,sun6i-a31-i2c") == 0);
	cells = fdt_getprop(fdt, twi, "clocks", &len);
	assert(cells != NULL && len == 4);
	provider = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*cells));
	assert(provider >= 0);
	cells = fdt_getprop(fdt, provider, "clock-frequency", &len);
	assert(cells != NULL && len == 4 && fdt32_to_cpu(*cells) == 24000000);
	twi = fdt_path_offset(fdt, "/soc@03000000/twi@0x05002c00");
	assert(fdt_node_check_compatible(fdt, twi,
	    "allwinner,sun6i-a31-i2c") == 0);
	cells = fdt_getprop(fdt, twi, "clocks", &len);
	assert(cells != NULL && len == 8 && fdt32_to_cpu(cells[1]) == 77);
	provider = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(cells[0]));
	assert(provider >= 0 && fdt_node_check_compatible(fdt, provider,
	    "allwinner,sun50i-a100-ccu") == 0);
	cells = fdt_getprop(fdt, twi, "resets", &len);
	assert(cells != NULL && len == 8 && fdt32_to_cpu(cells[1]) == 26);
	cells = fdt_getprop(fdt, twi, "pinctrl-0", &len);
	assert(cells != NULL && len == 4);
	provider = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*cells));
	assert(provider >= 0);
	assert(strcmp(fdt_getprop(fdt, provider, "function", NULL), "i2c3") == 0);
	assert(memcmp(fdt_getprop(fdt, provider, "pins", &len),
	    "PH12\0PH13", 10) == 0 && len == 10);
	provider = fdt_path_offset(fdt,
	    "/soc@03000000/twi@0x05002c00/goodix_ts@5d");
	cells = fdt_getprop(fdt, provider, "reg", &len);
	assert(cells != NULL && len == 4 && fdt32_to_cpu(*cells) == 0x5d);
	assert(fdt_getprop(fdt, provider, "ember,ys-m33-reset", &len) != NULL &&
	    len == 0);
	mmc = fdt_path_offset(fdt, "/soc@03000000/sdmmc@04022000");
	assert(fdt_node_check_compatible(fdt, mmc,
	    "allwinner,sun50i-a100-emmc") == 0);
	assert(strcmp(fdt_getprop(fdt, mmc, "status", NULL), "okay") == 0);
	cells = fdt_getprop(fdt, mmc, "clocks", &len);
	assert(cells != NULL && len == 16);
	provider = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(cells[0]));
	assert(provider >= 0 && fdt32_to_cpu(cells[2]) ==
	    fdt32_to_cpu(cells[0]));
	assert(fdt_node_check_compatible(fdt, provider,
	    "allwinner,sun50i-a100-ccu") == 0);
	assert(fdt32_to_cpu(cells[1]) == 68 && fdt32_to_cpu(cells[3]) == 64);
	cells = fdt_getprop(fdt, provider, "clocks", &len);
	assert(cells != NULL && len == 12);
	for (int i = 0; i < 3; i++) {
		int osc = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(cells[i]));
		assert(osc >= 0 && fdt_node_check_compatible(fdt, osc,
		    "fixed-clock") == 0);
	}
	cells = fdt_getprop(fdt, mmc, "max-frequency", NULL);
	assert(cells != NULL && fdt32_to_cpu(*cells) == 25000000);
	assert(fdt_getprop(fdt, mmc, "mmc-hs400-1_8v", NULL) == NULL);
	assert(fdt_getprop(fdt, mmc, "mmc-hs200-1_8v", NULL) == NULL);
	assert(fdt_getprop(fdt, mmc, "mmc-ddr-1_8v", NULL) == NULL);
	cells = fdt_getprop(fdt, mmc, "pinctrl-0", &len);
	assert(cells != NULL && len == 4);
	assert(fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*cells)) >= 0);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	/* The touch controller is optional; other resources must still attach. */
	/* A changed reset route must revoke a previously valid board marker. */
	provider = fdt_path_offset(fdt,
	    "/soc@03000000/twi@0x05002c00/goodix_ts@5d");
	{
		fdt32_t invalid[7] = {cpu_to_fdt32(99), cpu_to_fdt32(7),
		    cpu_to_fdt32(15), 0, 0, 0, 0};
		assert(fdt_setprop(fdt, provider, "goodix,rst-gpio",
		    invalid, sizeof(invalid)) == 0);
	}
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	provider = fdt_path_offset(fdt,
	    "/soc@03000000/twi@0x05002c00/goodix_ts@5d");
	assert(fdt_getprop(fdt, provider, "ember,ys-m33-reset", NULL) == NULL);
	fixture(fdt, 16384, "allwinner,a133");
	twi = fdt_path_offset(fdt, "/soc@03000000/twi@0x05002c00");
	assert(fdt_del_node(fdt, twi) == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);

	/* A live vendor scanout must never remain allocatable kernel RAM. */
	fixture(fdt, 16384, "allwinner,a133");
	chosen = fdt_add_subnode(fdt, 0, "chosen");
	assert(chosen >= 0);
	assert(fdt_setprop_string(fdt, chosen, "bootargs",
	    "disp_reserve=4096000,0x7bf46100 "
	    "LCD/lcd_mipi_param=108;screen_type=7;/DCL") == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	fb = fdt_node_offset_by_compatible(fdt, -1, "simple-framebuffer");
	assert(fb >= 0);
	cells = fdt_getprop(fdt, fb, "width", NULL);
	assert(cells != NULL && fdt32_to_cpu(*cells) == 800);
	cells = fdt_getprop(fdt, fb, "height", NULL);
	assert(cells != NULL && fdt32_to_cpu(*cells) == 1280);
	cells = fdt_getprop(fdt, fb, "reg", &len);
	assert(cells != NULL && len == 16);
	assert(fdt32_to_cpu(cells[1]) == 0x7bf46100);
	assert(fdt_num_mem_rsv(fdt) == 1);
	assert(fdt_get_mem_rsv(fdt, 0, &addr, &size) == 0);
	assert(addr <= 0x7bf46100 && addr + size >= 0x7bf46100 + 4096000);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	assert(fdt_num_mem_rsv(fdt) == 1);

	fixture(fdt, 16384, "allwinner,a133");
	chosen = fdt_add_subnode(fdt, 0, "chosen");
	assert(fdt_setprop_string(fdt, chosen, "bootargs",
	    "disp_reserve=4096000,0xffffff00 LCD/lcd_mipi_param=108;") == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	assert(fdt_node_offset_by_compatible(fdt, -1,
	    "simple-framebuffer") == -FDT_ERR_NOTFOUND);

	fixture(fdt, 16384, "allwinner,sun50i-a64");
	memcpy(before, fdt, 16384);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	assert(memcmp(before, fdt, 16384) == 0);

	fixture(fdt, 16384, "allwinner,a133");
	assert(fdt_pack(fdt) == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == -FDT_ERR_NOSPACE);
	fixture(fdt, 16384, "allwinner,a133");
	mmc = fdt_path_offset(fdt, "/soc@03000000/sdmmc@04022000");
	assert(fdt_del_node(fdt, mmc) == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == -FDT_ERR_NOTFOUND);
	free(before);
	free(fdt);
	puts("A133 FDT adapter: MMC, I2C, scanout reservation, isolation and errors passed");
	return 0;
}
