/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Execute the real genfb_pci panel PWM functions: the panel table, the
 * level scale, the register access, and the level across suspend.
 */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define __BIT(n) (UINT32_C(1) << (n))
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
typedef uint32_t pcireg_t;
#define PCI_ID_CODE(vid, pid) (((pcireg_t)(pid) << 16) | (pcireg_t)(vid))
#define PCI_VENDOR_INTEL 0x8086
#define PCI_VENDOR_MSI 0x1462

/* MMIO of the two PWM controllers, from the mapped base. */
#define NREG 128
struct mmio {
	uint32_t reg[NREG];
	uint32_t latched[NREG];	/* double-buffered duty: value of reads */
	bool double_buffer;
	int reads, writes;
	int last_write;
};
typedef struct mmio *bus_space_tag_t;
typedef size_t bus_space_handle_t;
typedef size_t bus_size_t;

static size_t
slot(bus_space_handle_t h, bus_size_t o)
{
	assert(h == 0 && o % 4 == 0 && o / 4 < NREG);
	return o / 4;
}

static uint32_t
bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	size_t i = slot(h, o);

	t->reads++;
	return t->double_buffer ? t->latched[i] : t->reg[i];
}

static void
bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint32_t v)
{
	size_t i = slot(h, o);

	t->writes++;
	t->last_write = (int)o;
	t->reg[i] = v;
	if (!t->double_buffer)
		t->latched[i] = v;
}

typedef int pmf_qual_t;
struct pci_genfb_softc;
typedef struct pci_genfb_softc *device_t;
struct genfb_pmf_callback {
	bool (*gpc_suspend)(device_t, const pmf_qual_t *);
	bool (*gpc_resume)(device_t, const pmf_qual_t *);
};
struct pci_genfb_softc {
	struct {
		struct genfb_pmf_callback *sc_pmfcb;
	} sc_gen;
	bus_space_tag_t sc_memt;
	const struct pci_genfb_panel *sc_panel;
	bus_space_handle_t sc_panel_pwm;
	int sc_panel_level;
};
#define device_private(dev) (dev)

static int errors_printed;

static void
aprint_error_dev(device_t dev, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
	errors_printed++;
}

#include "genfb-pci-pwm.h"

static const char *calls[8];
static int ncalls;
static bool md_result = true;

static bool
md_suspend(device_t dev, const pmf_qual_t *qual)
{
	(void)dev;
	(void)qual;
	calls[ncalls++] = "md_suspend";
	return md_result;
}

static bool
md_resume(device_t dev, const pmf_qual_t *qual)
{
	(void)dev;
	(void)qual;
	calls[ncalls++] = "md_resume";
	return md_result;
}

static const pcireg_t board_id = PCI_ID_CODE(PCI_VENDOR_INTEL, 0x7d67);
static const pcireg_t board_subsys = PCI_ID_CODE(PCI_VENDOR_MSI, 0x149c);

static void
check_table(void)
{
	const struct pci_genfb_panel *pp;
	size_t i;

	for (i = 0; i < __arraycount(pci_genfb_panels); i++) {
		pp = &pci_genfb_panels[i];
		assert(pp->pp_controller <= 1);
		assert(pp->pp_min < 255);
		assert(pp->pp_vendor[0] != '\0' && pp->pp_product[0] != '\0');
	}
	pp = pci_genfb_panel_lookup(board_id, board_subsys,
	    "Micro-Star International Co., Ltd.", "Vector 17 HX AI A2XWJG");
	assert(pp == &pci_genfb_panels[0]);
	assert(pp->pp_controller == 0 && pp->pp_min == 6);
	/* The same model prefix as the libopeninput hwdb. */
	assert(pci_genfb_panel_lookup(board_id, board_subsys, "Micro-Star",
	    "Vector 17 HX AI") == pp);
	assert(pci_genfb_panel_lookup(board_id, board_subsys,
	    "Micro-Star International Co., Ltd.", "Vector 17 HX") == NULL);
	assert(pci_genfb_panel_lookup(board_id, board_subsys,
	    "MICRO-STAR", "Vector 17 HX AI A2XWJG") == NULL);
	assert(pci_genfb_panel_lookup(board_id, board_subsys, NULL,
	    "Vector 17 HX AI A2XWJG") == NULL);
	assert(pci_genfb_panel_lookup(board_id, board_subsys, "Micro-Star",
	    NULL) == NULL);
	assert(pci_genfb_panel_lookup(board_id,
	    PCI_ID_CODE(PCI_VENDOR_MSI, 0x149d), "Micro-Star",
	    "Vector 17 HX AI A2XWJG") == NULL);
	assert(pci_genfb_panel_lookup(PCI_ID_CODE(PCI_VENDOR_INTEL, 0x7d68),
	    board_subsys, "Micro-Star", "Vector 17 HX AI A2XWJG") == NULL);
}

static void
check_period(uint32_t period, unsigned min)
{
	uint32_t lo = pci_genfb_pwm_floor(period, min);
	uint32_t duty, prev = 0;
	int v;

	assert(pci_genfb_pwm_usable(period, min));
	for (v = 0; v <= 255; v++) {
		duty = pci_genfb_pwm_duty(period, min, v);
		assert(duty >= lo && duty <= period);
		assert(v == 0 || duty > prev);
		if (pci_genfb_pwm_level(period, min, duty) != v) {
			fprintf(stderr, "period %u min %u value %d: duty %u "
			    "reads back as %d\n", period, min, v, duty,
			    pci_genfb_pwm_level(period, min, duty));
			assert(0);
		}
		prev = duty;
	}
	assert(pci_genfb_pwm_duty(period, min, 0) == lo);
	assert(pci_genfb_pwm_duty(period, min, 255) == period);
	/* Firmware can leave a duty cycle below the lowest level. */
	assert(pci_genfb_pwm_level(period, min, 0) == 0);
	assert(pci_genfb_pwm_level(period, min, lo) == 0);
}

static void
check_scale(void)
{
	static const unsigned mins[] = { 0, 6, 128, 254 };
	uint32_t period;
	size_t i;

	for (i = 0; i < __arraycount(mins); i++) {
		for (period = 1; !pci_genfb_pwm_usable(period, mins[i]);
		    period++)
			assert(period < 1U << 20);
		/* The shortest usable period and the next ones. */
		for (; period < 70000; period += period < 4096 ? 1 : 997)
			check_period(period, mins[i]);
		check_period(UINT32_MAX - 1, mins[i]);
	}
	/* The level 6 of 255 had no own value before: 0..5 all gave 6. */
	assert(pci_genfb_pwm_duty(48000, 6, 0) <
	    pci_genfb_pwm_duty(48000, 6, 1));
	assert(!pci_genfb_pwm_usable(260, 6));
	assert(!pci_genfb_pwm_usable(0, 6));
	assert(!pci_genfb_pwm_usable(UINT32_MAX, 6));
}

static void
setup(struct pci_genfb_softc *sc, struct mmio *io,
    const struct pci_genfb_panel *pp, uint32_t period, uint32_t duty)
{
	size_t base = pp->pp_controller * ARL_PWM_STRIDE;

	memset(sc, 0, sizeof(*sc));
	memset(io, 0, sizeof(*io));
	sc->sc_memt = io;
	sc->sc_panel = pp;
	sc->sc_panel_level = -1;
	io->reg[base / 4] = ARL_PWM_ENABLE;
	io->reg[(base + ARL_PWM_PERIOD) / 4] = period;
	io->reg[(base + ARL_PWM_DUTY) / 4] = duty;
	memcpy(io->latched, io->reg, sizeof(io->reg));
}

static void
check_registers(void)
{
	static const struct pci_genfb_panel second = {
		0, 0, "", "", 1, 6
	};
	const struct pci_genfb_panel *panels[] = {
		&pci_genfb_panels[0], &second
	};
	struct pci_genfb_softc sc;
	struct mmio io;
	size_t i, base, other;
	int value;

	assert(ARL_PWM_MAPSIZE == ARL_PWM_STRIDE + ARL_PWM_DUTY + 4);
	for (i = 0; i < __arraycount(panels); i++) {
		base = panels[i]->pp_controller * ARL_PWM_STRIDE;
		other = (1 - panels[i]->pp_controller) * ARL_PWM_STRIDE;

		setup(&sc, &io, panels[i], 48000, 48000);
		assert(pci_genfb_pwm_get(&sc, &value) == 0 && value == 255);
		assert(pci_genfb_pwm_set(&sc, 100) == 0);
		assert(io.writes == 1 &&
		    io.last_write == (int)(base + ARL_PWM_DUTY));
		assert(io.reg[(base + ARL_PWM_DUTY) / 4] ==
		    pci_genfb_pwm_duty(48000, 6, 100));
		assert(pci_genfb_pwm_get(&sc, &value) == 0 && value == 100);
		assert(pci_genfb_pwm_set(&sc, -1) == EINVAL);
		assert(pci_genfb_pwm_set(&sc, 256) == EINVAL);
		assert(io.writes == 1);

		/* The new duty cycle is read only after the next period. */
		setup(&sc, &io, panels[i], 48000, 48000);
		io.double_buffer = true;
		assert(pci_genfb_pwm_set(&sc, 10) == 0);
		assert(io.writes == 1);

		/* Firmware owns the period, polarity and enable bits. */
		setup(&sc, &io, panels[i], 48000, 1000);
		io.reg[base / 4] = 0;
		assert(pci_genfb_pwm_get(&sc, &value) == ENXIO);
		assert(pci_genfb_pwm_set(&sc, 10) == ENXIO);
		io.reg[base / 4] = ARL_PWM_ENABLE | ARL_PWM_INVERT;
		assert(pci_genfb_pwm_get(&sc, &value) == ENXIO);
		io.reg[base / 4] = ARL_PWM_ENABLE;
		io.reg[other / 4] = ARL_PWM_ENABLE;
		assert(pci_genfb_pwm_get(&sc, &value) == ENXIO);
		io.reg[other / 4] = 0;
		io.reg[(base + ARL_PWM_DUTY) / 4] = 48001;
		assert(pci_genfb_pwm_get(&sc, &value) == ENXIO);
		io.reg[(base + ARL_PWM_DUTY) / 4] = 0;
		io.reg[(base + ARL_PWM_PERIOD) / 4] = 0;
		assert(pci_genfb_pwm_get(&sc, &value) == ENXIO);
		io.reg[(base + ARL_PWM_PERIOD) / 4] = UINT32_MAX;
		assert(pci_genfb_pwm_get(&sc, &value) == ENXIO);
		io.reg[(base + ARL_PWM_PERIOD) / 4] = 260;
		assert(pci_genfb_pwm_get(&sc, &value) == ENXIO);
		assert(io.writes == 0);
	}
}

static void
check_suspend(void)
{
	struct genfb_pmf_callback md = { md_suspend, md_resume };
	struct genfb_pmf_callback none = { NULL, NULL };
	const struct pci_genfb_panel *pp = &pci_genfb_panels[0];
	struct pci_genfb_softc sc;
	struct mmio io;
	int value;

	/* Two S3 cycles keep the level; firmware resets the duty cycle. */
	setup(&sc, &io, pp, 48000, 48000);
	sc.sc_gen.sc_pmfcb = &md;
	assert(pci_genfb_pwm_set(&sc, 40) == 0);
	for (int cycle = 0; cycle < 2; cycle++) {
		ncalls = 0;
		assert(pci_genfb_suspend(&sc, NULL));
		assert(sc.sc_panel_level == 40);
		io.reg[ARL_PWM_DUTY / 4] = io.latched[ARL_PWM_DUTY / 4] =
		    48000;
		assert(pci_genfb_resume(&sc, NULL));
		assert(ncalls == 2 && strcmp(calls[0], "md_suspend") == 0 &&
		    strcmp(calls[1], "md_resume") == 0);
		assert(pci_genfb_pwm_get(&sc, &value) == 0 && value == 40);
	}

	/* A failed MD suspend or resume stops; the level stays. */
	md_result = false;
	assert(!pci_genfb_suspend(&sc, NULL));
	io.writes = 0;
	assert(!pci_genfb_resume(&sc, NULL));
	assert(io.writes == 0);
	md_result = true;

	/* A PWM that firmware did not start again is not touched. */
	setup(&sc, &io, pp, 48000, 20000);
	assert(pci_genfb_suspend(&sc, NULL) && sc.sc_panel_level >= 0);
	io.reg[0] = 0;
	errors_printed = 0;
	assert(pci_genfb_resume(&sc, NULL));
	assert(io.writes == 0 && errors_printed == 1);

	/* A PWM off at suspend gives no level to set on resume. */
	setup(&sc, &io, pp, 48000, 20000);
	sc.sc_panel_level = 77;
	io.reg[0] = 0;
	assert(pci_genfb_suspend(&sc, NULL) && sc.sc_panel_level == -1);
	io.reg[0] = ARL_PWM_ENABLE;
	assert(pci_genfb_resume(&sc, NULL) && io.writes == 0);

	/* Without a panel only the MD callbacks run, and no MMIO. */
	setup(&sc, &io, pp, 48000, 20000);
	sc.sc_panel = NULL;
	sc.sc_gen.sc_pmfcb = &md;
	ncalls = 0;
	assert(pci_genfb_suspend(&sc, NULL) && pci_genfb_resume(&sc, NULL));
	assert(ncalls == 2 && io.reads == 0 && io.writes == 0);
	sc.sc_gen.sc_pmfcb = &none;
	assert(pci_genfb_suspend(&sc, NULL) && pci_genfb_resume(&sc, NULL));
	sc.sc_gen.sc_pmfcb = NULL;
	assert(pci_genfb_suspend(&sc, NULL) && pci_genfb_resume(&sc, NULL));
	assert(io.reads == 0 && io.writes == 0);
}

int
main(void)
{
	check_table();
	check_scale();
	check_registers();
	check_suspend();
	puts("genfb-pci-pwm: ok");
	return 0;
}
