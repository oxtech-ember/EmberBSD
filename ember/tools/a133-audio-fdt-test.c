/* Origin: EmberBSD - test verified audio resources before driver attachment. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define main existing_fdt_contract
#include "a133-fdt-test.c"
#undef main
#define CODEC "/soc@03000000/codec@0x05096000"
#define SOUND "/soc@03000000/sound@0"

static void
audio_fixture(void *fdt, const char *root)
{
	fdt32_t cells[7];
	int soc, node;

	fixture(fdt, 16384, root);
	node = fdt_path_offset(fdt, "/soc@03000000/pinctrl@0300b000");
	assert(fdt_setprop_u32(fdt, node, "#gpio-cells", 6) == 0);
	soc = fdt_path_offset(fdt, "/soc@03000000");
	node = fdt_add_subnode(fdt, soc, "codec@0x05096000");
	assert(fdt_setprop_string(fdt, node, "compatible", "allwinner,sunxi-internal-codec") == 0);
	assert(fdt_setprop_string(fdt, node, "status", "okay") == 0);
	assert(fdt_setprop_u32(fdt, node, "phandle", 128) == 0);
	assert(fdt_setprop_u32(fdt, node, "pa_level", 0) == 0);
	assert(fdt_setprop_u32(fdt, node, "pa_msleep_time", 120) == 0);
	cells[0] = 0; cells[1] = cpu_to_fdt32(0x05096000);
	cells[2] = 0; cells[3] = cpu_to_fdt32(0x32c);
	assert(fdt_setprop(fdt, node, "reg", cells, 16) == 0);
	soc = fdt_path_offset(fdt, "/soc@03000000");
	node = fdt_add_subnode(fdt, soc, "sound@0");
	assert(fdt_setprop_string(fdt, node, "compatible", "allwinner,sunxi-codec-machine") == 0);
	assert(fdt_setprop_string(fdt, node, "status", "okay") == 0);
	assert(fdt_setprop_u32(fdt, node, "sunxi,audio-codec", 128) == 0);
	cells[0] = 0; cells[1] = cpu_to_fdt32(25); cells[2] = cpu_to_fdt32(4);
	assert(fdt_setprop(fdt, node, "interrupts", cells, 12) == 0);
	cells[0] = cpu_to_fdt32(99); cells[1] = cpu_to_fdt32(5);
	cells[2] = cpu_to_fdt32(6); cells[3] = cpu_to_fdt32(1);
	cells[4] = cells[5] = cells[6] = cpu_to_fdt32(1);
	assert(fdt_setprop(fdt, node, "spk-gpio", cells, 28) == 0);
}

int
main(void)
{
	unsigned char fdt[16384], before[16384];
	const fdt32_t *cells;
	fdt32_t modified[7];
	int node, len, which;

	audio_fixture(fdt, "allwinner,a133");
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	node = fdt_path_offset(fdt, CODEC);
	assert(fdt_getprop(fdt, node, "ember,ys-m33-audio", &len) != NULL && len == 0);
	cells = fdt_getprop(fdt, node, "interrupts", &len);
	assert(cells != NULL && len == 12 && fdt32_to_cpu(cells[0]) == 0 &&
	    fdt32_to_cpu(cells[1]) == 25 && fdt32_to_cpu(cells[2]) == 4);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	node = fdt_path_offset(fdt, CODEC);
	assert(fdt_getprop(fdt, node, "ember,ys-m33-audio", &len) != NULL && len == 0);

	for (which = 0; which < 8; which++) {
		audio_fixture(fdt, "allwinner,a133");
		node = fdt_path_offset(fdt, CODEC);
		assert(fdt_setprop(fdt, node, "ember,ys-m33-audio", NULL, 0) == 0);
		if (which == 0 || which == 1) {
			node = fdt_path_offset(fdt, which == 0 ? CODEC : SOUND);
			assert(fdt_setprop_string(fdt, node, "status", "disabled") == 0);
		} else if (which == 2 || which == 3) {
			node = fdt_path_offset(fdt, CODEC);
			assert(fdt_setprop_u32(fdt, node,
			    which == 2 ? "pa_level" : "pa_msleep_time", 1) == 0);
		} else if (which == 4) {
			node = fdt_path_offset(fdt, SOUND);
			assert(fdt_setprop_u32(fdt, node, "sunxi,audio-codec", 99) == 0);
		} else if (which == 5) {
			node = fdt_path_offset(fdt, SOUND);
			cells = fdt_getprop(fdt, node, "spk-gpio", &len);
			memcpy(modified, cells, 28); modified[2] = cpu_to_fdt32(7);
			assert(fdt_setprop(fdt, node, "spk-gpio", modified, 28) == 0);
		} else if (which == 6) {
			node = fdt_path_offset(fdt, SOUND);
			cells = fdt_getprop(fdt, node, "interrupts", &len);
			memcpy(modified, cells, 12); modified[1] = cpu_to_fdt32(26);
			assert(fdt_setprop(fdt, node, "interrupts", modified, 12) == 0);
		} else {
			node = fdt_path_offset(fdt, CODEC);
			cells = fdt_getprop(fdt, node, "reg", &len);
			memcpy(modified, cells, 16); modified[1] = cpu_to_fdt32(0x05097000);
			assert(fdt_setprop(fdt, node, "reg", modified, 16) == 0);
		}
		assert(sun50i_a133_fdt_fixup(fdt) == 0);
		node = fdt_path_offset(fdt, CODEC);
		assert(fdt_getprop(fdt, node, "ember,ys-m33-audio", &len) == NULL);
	}
	audio_fixture(fdt, "allwinner,sun50i-a64");
	memcpy(before, fdt, sizeof(fdt));
	assert(sun50i_a133_fdt_fixup(fdt) == 0 && memcmp(before, fdt, sizeof(fdt)) == 0);
	audio_fixture(fdt, "allwinner,a133");
	assert(fdt_pack(fdt) == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == -FDT_ERR_NOSPACE);
	puts("A133 audio FDT: resource guards, IRQ, isolation and stale marker passed");
	return 0;
}
