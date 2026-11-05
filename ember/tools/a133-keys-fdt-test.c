/* Origin: EmberBSD - A133 interrupt routing and volume-key binding contract. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define main existing_fdt_contract
#include "a133-fdt-test.c"
#undef main
#include <stdbool.h>
#include <arm/sunxi/sunxi_lradc.h>
#include <arm/sunxi/sun50i_a100_ccu.h>
#define NMI "/intc-nmi@07010320"
#define PMU "/soc@03000000/s_twi@0x07081400/pmu@34"
#define KEYS "/soc@03000000/keyboard"

/* Keep OF access at the host boundary; run the production match function. */
typedef void *device_t;
typedef void *cfdata_t;
struct fdt_attach_args { int faa_phandle; };
struct device_compatible_entry { const char *compat; };
#define DEVICE_COMPAT_EOL { .compat = NULL }
static const void *match_fdt;
#define of_hasprop(node, prop) (fdt_getprop(match_fdt, node, prop, NULL) != NULL)

static int
of_compatible_match(int node, const struct device_compatible_entry *table)
{
	for (; table->compat != NULL; table++) {
		if (fdt_node_check_compatible(match_fdt, node, table->compat) == 0)
			return 1;
	}
	return 0;
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "keys-match.h"
#pragma GCC diagnostic pop

static void
key_match(void)
{
	unsigned char fdt[1024];
	struct fdt_attach_args faa;
	static const struct { const char *compat; int match; } cases[] = {
		{ "allwinner,keyboard_1350mv", 1 },
		{ "allwinner,sun4i-a10-lradc-keys", 1 },
		{ "allwinner,unsupported-keyboard", 0 }
	};

	match_fdt = fdt;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		for (int marked = 0; marked < 2; marked++) {
			assert(fdt_create_empty_tree(fdt, sizeof(fdt)) == 0);
			assert(fdt_setprop_string(fdt, 0, "compatible",
			    "example,other-board") == 0);
			faa.faa_phandle = fdt_add_subnode(fdt, 0, "keyboard");
			assert(faa.faa_phandle >= 0);
			assert(fdt_setprop_string(fdt, faa.faa_phandle,
			    "compatible", cases[i].compat) == 0);
			if (marked)
				assert(fdt_setprop(fdt, faa.faa_phandle,
				    "ember,ys-m33-volume-keys", NULL, 0) == 0);
			assert(sunxi_lradc_match(NULL, NULL, &faa) == cases[i].match);
		}
	}
}

static void
make_keys(void *fdt, int bad_irq)
{
	fdt32_t irq[] = { 0, cpu_to_fdt32(bad_irq ? 104 : 103), cpu_to_fdt32(4) };
	fdt32_t reg[] = { 0, cpu_to_fdt32(0x07010320), 0, cpu_to_fdt32(12) };
	int n;

	fixture(fdt, 32768, "allwinner,a133");
	n = fdt_add_subnode(fdt, 0, &NMI[1]);
	assert(fdt_setprop_string(fdt, n, "compatible", "allwinner,sun8i-nmi") == 0);
	assert(fdt_setprop(fdt, n, "reg", reg, sizeof(reg)) == 0);
	assert(fdt_setprop(fdt, n, "interrupts", irq, sizeof(irq)) == 0);
	assert(fdt_setprop_u32(fdt, n, "#interrupt-cells", 2) == 0);
	assert(fdt_setprop(fdt, n, "interrupt-controller", NULL, 0) == 0);
	assert(fdt_setprop_u32(fdt, n, "phandle", 103) == 0);
	assert(fdt_setprop_u32(fdt, n, "interrupt-parent", 42) == 0);
	n = fdt_add_subnode(fdt, fdt_path_offset(fdt, "/soc@03000000/s_twi@0x07081400"), "pmu@34");
	assert(fdt_setprop_string(fdt, n, "compatible", "x-powers,axp803") == 0);
	assert(fdt_setprop_u32(fdt, n, "reg", 0x34) == 0);
	assert(fdt_setprop_u32(fdt, n, "interrupt-parent", 103) == 0);
	irq[1] = cpu_to_fdt32(8);
	assert(fdt_setprop(fdt, n, "interrupts", irq, 8) == 0);
	n = fdt_add_subnode(fdt, fdt_path_offset(fdt, "/soc@03000000"), "keyboard");
	assert(fdt_setprop_string(fdt, n, "compatible", "allwinner,keyboard_1350mv") == 0);
	reg[1] = cpu_to_fdt32(0x05070800); reg[3] = cpu_to_fdt32(0x400);
	assert(fdt_setprop(fdt, n, "reg", reg, sizeof(reg)) == 0);
	irq[1] = cpu_to_fdt32(22); irq[2] = 0;
	assert(fdt_setprop(fdt, n, "interrupts", irq, sizeof(irq)) == 0);
	assert(fdt_setprop_u32(fdt, n, "key_cnt", 2) == 0);
	reg[0] = cpu_to_fdt32(210); reg[1] = cpu_to_fdt32(115);
	assert(fdt_setprop(fdt, n, "key0", reg, 8) == 0);
	reg[0] = cpu_to_fdt32(410); reg[1] = cpu_to_fdt32(114);
	assert(fdt_setprop(fdt, n, "key1", reg, 8) == 0);
}

/* Use the selected descriptors from the production CCU table. */
#define SUNXI_CCU_GATE(id, name, parent, reg, bit) {id, reg, bit, parent}
#define SUNXI_CCU_RESET(id, reg, bit) {id, reg, bit, NULL}
static const struct { unsigned id, reg, bit; const char *parent; } clocks[] = {
#include "keys-ccu.h"
};

#ifndef __BIT
#define __BIT(n) (1U << (n))
#endif
struct sunxi_lradc_softc {
	int sc_lastlevel[2], sc_previous_sample;
	int *sc_switches[2];
	uint8_t sc_level[2][32];
	bool sc_volume_sampling;
	uint32_t regs[5];
};
#define ADC_READ(sc, reg) ((sc)->regs[(reg)/4])
#define ADC_WRITE(sc, reg, value) ((sc)->regs[(reg)/4] = (value))
#define mutex_enter(lock) ((void)0)
#define mutex_exit(lock) ((void)0)
#define aprint_error_dev(...) abort()
static int events[4], nevents;
static void sunxi_lradc_volume_press(void *p) { events[nevents++] = *(int *)p; }
static void sunxi_lradc_volume_release(void *p) { events[nevents++] = -*(int *)p; }
static int
sysmon_task_queue_sched(unsigned priority, void (*event)(void *), void *arg)
{
	assert(priority == 0 && nevents < 4);
	event(arg);
	return 0;
}
#include "keys-intr.h"

static void
key_events(const uint8_t upper[2])
{
	int buttons[] = { 115, 114 };
	struct sunxi_lradc_softc sc = { .sc_lastlevel = {-1, -1},
	    .sc_switches = {buttons, NULL} };
	const uint32_t edges = AWIN_LRADC_INT_KEY0 | AWIN_LRADC_INT_KEYUP0;

	memcpy(sc.sc_level[0], upper, 2);
	for (int k = 0; k < 2; k++) {
		sc.regs[2] = AWIN_LRADC_INT_KEY0;
		assert(sunxi_lradc_volume_intr(&sc) && sc.regs[1] == (edges | AWIN_LRADC_INT_DATA0));
		sc.regs[2] = AWIN_LRADC_INT_DATA0;
		sc.regs[3] = 63;
		sunxi_lradc_volume_intr(&sc); sunxi_lradc_volume_intr(&sc);
		sc.regs[3] = k == 0 ? 10 : 20;
		sunxi_lradc_volume_intr(&sc);
		assert(nevents == 2*k);
		sunxi_lradc_volume_intr(&sc);
		sunxi_lradc_volume_intr(&sc);
		assert(nevents == 2*k+1 && events[2*k] == buttons[k] && sc.regs[1] == edges);
		sc.regs[2] = AWIN_LRADC_INT_KEYUP0; sc.regs[3] = 63;
		sunxi_lradc_volume_intr(&sc); sunxi_lradc_volume_intr(&sc);
		assert(nevents == 2*k+2 && events[2*k+1] == -buttons[k]);
	}
}

static void
key_ladder(void)
{
	uint32_t keys[2][2] = { {210, 115}, {410, 114} };
	uint8_t upper[2];
	const char *names[2];

	assert(sizeof(clocks) / sizeof(clocks[0]) == 2);
	assert(clocks[0].id == 49 && clocks[0].reg == 0xa9c && clocks[0].bit == 16);
	assert(clocks[1].id == 117 && clocks[1].reg == 0xa9c && clocks[1].bit == 0 &&
	    strcmp(clocks[1].parent, "ahb3") == 0);
	assert(sunxi_lradc_volume_levels(keys, upper, names));
	key_events(upper);
	assert(strcmp(names[0], "volume-up") == 0 && strcmp(names[1], "volume-down") == 0);
	for (int i = 0; i < 64; i++)
		assert(sunxi_lradc_volume_index(upper, i) == (i <= 14 ? 0 : i <= 41 ? 1 : -1));
	keys[1][1] = 115;
	assert(!sunxi_lradc_volume_levels(keys, upper, names));
	keys[1][1] = 114; keys[1][0] = 209;
	assert(!sunxi_lradc_volume_levels(keys, upper, names));
}

int
main(void)
{
	unsigned char fdt[32768];
	const fdt32_t *p;
	struct fdt_attach_args faa;
	int n, len, pass;
	key_match();
	key_ladder();
	make_keys(fdt, 0);
	for (pass = 0; pass < 2; pass++) {
		assert(sun50i_a133_fdt_fixup(fdt) == 0);
		n = fdt_path_offset(fdt, NMI);
		assert(fdt_node_check_compatible(fdt, n, "allwinner,sun9i-a80-nmi") == 0);
		assert(fdt_get_phandle(fdt, n) == 103);
		n = fdt_path_offset(fdt, KEYS);
		match_fdt = fdt;
		faa.faa_phandle = n;
		assert(sunxi_lradc_match(NULL, NULL, &faa) == 1);
		p = fdt_getprop(fdt, n, "clocks", &len);
		assert(p && len == 8 && fdt32_to_cpu(p[1]) == 117);
		assert(fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(p[0])) ==
		    fdt_path_offset(fdt, "/soc@03000000/clock-controller@03001000"));
		p = fdt_getprop(fdt, n, "resets", &len);
		assert(p && len == 8 && fdt32_to_cpu(p[1]) == 49);
	}
	make_keys(fdt, 1);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	assert(fdt_node_check_compatible(fdt, fdt_path_offset(fdt, NMI),
	    "allwinner,sun9i-a80-nmi") != 0);
	puts("A133 keys: hardware match, NMI, LRADC resources, ladder and IRQ passed");
	return 0;
}
