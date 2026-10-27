/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the real board DTB; no register or GPIO access is performed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

int sun50i_a133_fdt_fixup(void *);

#define HOST "/soc/mmc@4021000"
#define OPT "ember,ys-m33-sdio-probe"
#define CAPACITY (1024 * 1024)

static void
check_string(const void *fdt, int node, const char *name, const char *want)
{
	const char *value;
	int len;

	value = fdt_getprop(fdt, node, name, &len);
	assert(value != NULL && len == (int)strlen(want) + 1);
	assert(memcmp(value, want, (size_t)len) == 0);
}

static void
check_cells(const void *fdt, int node, const char *name,
    const uint32_t *want, size_t count)
{
	const fdt32_t *value;
	size_t i;
	int len;

	value = fdt_getprop(fdt, node, name, &len);
	assert(value != NULL && len == (int)(count * sizeof(*value)));
	for (i = 0; i < count; i++)
		assert(fdt32_to_cpu(value[i]) == want[i]);
}

static void
check_disabled_resources(const void *fdt)
{
	static const uint32_t reg[] = { 0x04021000, 0x1000 };
	static const uint32_t irq[] = { 0, 40, 4 };
	static const uint32_t width[] = { 4 }, frequency[] = { 25000000 };
	static const uint32_t emmc_reg[] = { 0x04022000, 0x1000 };
	static const uint32_t emmc_irq[] = { 0, 41, 4 };
	static const uint32_t emmc_width[] = { 8 };
	static const char pins[] = "PG0\0PG1\0PG2\0PG3\0PG4\0PG5";
	const fdt32_t *value;
	const void *property;
	uint32_t clocks[4], resets[2];
	int node, provider, group, len;

	node = fdt_path_offset(fdt, HOST);
	assert(node >= 0); /* Missing native host is the baseline RED. */
	check_string(fdt, node, "status", "disabled");
	check_string(fdt, node, "compatible", "allwinner,sun50i-a100-mmc");
	check_cells(fdt, node, "reg", reg, 2);
	check_cells(fdt, node, "interrupts", irq, 3);
	check_cells(fdt, node, "bus-width", width, 1);
	check_cells(fdt, node, "max-frequency", frequency, 1);
	property = fdt_getprop(fdt, node, "non-removable", &len);
	assert(property != NULL && len == 0);
	assert(fdt_getprop(fdt, node, "ember,ys-m33-sdio-host", NULL) == NULL);
	provider = fdt_path_offset(fdt, "/soc/clock@3001000");
	assert(provider >= 0 && fdt_get_phandle(fdt, provider) != 0);
	clocks[0] = clocks[2] = resets[0] = fdt_get_phandle(fdt, provider);
	clocks[1] = 67; clocks[3] = 63; resets[1] = 16;
	check_cells(fdt, node, "clocks", clocks, 4);
	check_cells(fdt, node, "resets", resets, 2);
	value = fdt_getprop(fdt, node, "pinctrl-0", &len);
	assert(value != NULL && len == 4);
	group = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*value));
	assert(group >= 0);
	check_string(fdt, group, "function", "mmc1");
	property = fdt_getprop(fdt, group, "pins", &len);
	assert(property != NULL && len == sizeof(pins));
	assert(memcmp(property, pins, sizeof(pins)) == 0);
	node = fdt_path_offset(fdt, "/soc/mmc@4022000");
	assert(node >= 0);
	check_string(fdt, node, "status", "okay");
	check_string(fdt, node, "compatible", "allwinner,sun50i-a100-emmc");
	check_cells(fdt, node, "reg", emmc_reg, 2);
	check_cells(fdt, node, "interrupts", emmc_irq, 3);
	check_cells(fdt, node, "bus-width", emmc_width, 1);
}

static void
fixup_preserves_tree(void *fdt, void *before)
{
	size_t size = (size_t)fdt_totalsize(fdt);

	memcpy(before, fdt, size);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	assert(memcmp(before, fdt, size) == 0);
	check_disabled_resources(fdt);
}

int
main(int argc, char **argv)
{
	unsigned char *blob, *fdt, *before;
	fdt32_t gpio[4];
	FILE *stream;
	size_t length;
	int node, provider, read_error, close_error;

	assert(argc == 2);
	blob = malloc(CAPACITY);
	fdt = calloc(1, CAPACITY);
	before = malloc(CAPACITY);
	assert(blob != NULL && fdt != NULL && before != NULL);
	stream = fopen(argv[1], "rb");
	assert(stream != NULL);
	length = fread(blob, 1, CAPACITY, stream);
	read_error = ferror(stream);
	close_error = fclose(stream);
	assert(length > 0 && length < CAPACITY);
	assert(read_error == 0 && close_error == 0);
	assert(fdt_check_header(blob) == 0);
	assert((size_t)fdt_totalsize(blob) == length);
	assert(fdt_open_into(blob, fdt, CAPACITY) == 0);
	check_disabled_resources(fdt);
	node = fdt_path_offset(fdt, HOST);
	assert(fdt_getprop(fdt, node, "mmc-pwrseq", NULL) == NULL);
	assert(fdt_getprop(fdt, node, "vmmc-supply", NULL) == NULL);
	assert(fdt_getprop(fdt, node, "vqmmc-supply", NULL) == NULL);
	fixup_preserves_tree(fdt, before);
	puts("missing_profile_keeps_native_host_disabled: PASS");
	assert(fdt_setprop(fdt, 0, OPT, NULL, 0) == 0);
	fixup_preserves_tree(fdt, before);
	puts("diagnostic_opt_in_does_not_enable_native: PASS");
	/* A foreign GPIO reference is not authority to activate the host. */
	provider = fdt_path_offset(fdt, "/soc/pinctrl@300b000");
	assert(provider >= 0 && fdt_get_phandle(fdt, provider) != 0);
	gpio[0] = cpu_to_fdt32(fdt_get_phandle(fdt, provider));
	gpio[1] = cpu_to_fdt32(7); gpio[2] = cpu_to_fdt32(11);
	gpio[3] = cpu_to_fdt32(1);
	node = fdt_add_subnode(fdt, 0, "unverified-wlan-power");
	assert(node >= 0);
	assert(fdt_setprop_u32(fdt, node, "phandle", 0x7ffffffe) == 0);
	assert(fdt_setprop_string(fdt, node, "compatible", "mmc-pwrseq-simple") == 0);
	assert(fdt_setprop(fdt, node, "reset-gpios", gpio, sizeof(gpio)) == 0);
	node = fdt_path_offset(fdt, HOST);
	assert(fdt_setprop_u32(fdt, node, "mmc-pwrseq", 0x7ffffffe) == 0);
	fixup_preserves_tree(fdt, before);
	puts("foreign_profile_stays_disabled: PASS");
	puts("native_fixup_preserves_existing_resources: PASS");
	free(before);
	free(fdt);
	free(blob);
	return 0;
}
