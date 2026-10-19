/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD 800 Series PCH-S GPIO contract, AI-assisted. */
/*
 * Host contract for the unmodified igpio(4) core and its ACPI front end.
 * Five fake communities model the Meteor Point-S (INTC1084) register
 * layout used by Linux pinctrl-meteorpoint.c: PADBAR 0x700, four PADCFG
 * registers per pad, HOSTSW_OWN 0x150, GPI_IS 0x200 (write one to clear)
 * and GPI_IE 0x220.  ACPI services are modeled; ACPICA supplies the types.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioccom.h>
#include <sys/queue.h>
#define ACPI_APPLICATION
#include <acpi.h>

static unsigned int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

typedef struct test_device *device_t;
typedef unsigned long bus_addr_t;	/* x86_64 u_long */
typedef unsigned long bus_size_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef struct { bool held; } kmutex_t;

#define __BIT(n)	(UINT32_C(1) << (n))
#define __BITS(m, n)	((UINT32_C(0xffffffff) >> (31 - (m))) & \
			    ~(__BIT(n) - 1))
#define __LOWEST_SET_BIT(m)	((m) & -(m))
#define __SHIFTIN(v, m)	((uint32_t)(v) * __LOWEST_SET_BIT(m))
#define __arraycount(x)	(sizeof(x) / sizeof((x)[0]))
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define KASSERT(c)	assert(c)
#define KM_SLEEP	0
#define MUTEX_DEFAULT	0
#define IPL_VM		6
#define CFARGS_NONE	0
#define GPIOMAXNAME	64
#define NGPIO		1

static int
ffs32(uint32_t n)
{
	return ffs((int)n);
}

static void mutex_init(kmutex_t *m, int t, int i)
{ (void)t; (void)i; m->held = false; }
static void mutex_enter(kmutex_t *m) { CHECK(!m->held); m->held = true; }
static void mutex_exit(kmutex_t *m) { CHECK(m->held); m->held = false; }
static bool mutex_owned(kmutex_t *m) { return m->held; }

static size_t live_bytes;
static void *
kmem_zalloc(size_t n, int f)
{
	void *p = calloc(1, n);

	(void)f;
	if (p == NULL)
		abort();
	live_bytes += n;
	return p;
}

static void
kmem_free(void *p, size_t n)
{
	CHECK(live_bytes >= n);
	live_bytes -= n;
	free(p);
}

static unsigned int errors;
static void
aprint_error_dev(device_t d, const char *fmt, ...)
{
	(void)d; (void)fmt;
	errors++;
}

/* Real NetBSD headers with their kernel includes removed by the runner. */
#include "sys-gpio.h"
#include "gpiovar.h"
#include "igpiovar.h"
#include "igpioreg.h"

#define NBAR		5
#define NBAR_MAX	6
#define BAR_SIZE	0x1000
#define PADBAR		0x700
static uint32_t regs[NBAR_MAX][BAR_SIZE / 4];
static unsigned int reg_writes;

static int
bus_space_map(bus_space_tag_t t, bus_addr_t a, bus_size_t n, int f,
    bus_space_handle_t *h)
{
	(void)t; (void)f;
	CHECK(n == BAR_SIZE && a % 0x100000 == 0 && a / 0x100000 >= 1 &&
	    a / 0x100000 <= NBAR_MAX);
	*h = a / 0x100000 - 1;
	return 0;
}

static uint32_t
bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	(void)t;
	CHECK(h < NBAR_MAX && o < BAR_SIZE && o % 4 == 0);
	if (h >= NBAR_MAX || o >= BAR_SIZE)
		return 0;
	return regs[h][o / 4];
}

static void
bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint32_t v)
{
	(void)t;
	CHECK(h < NBAR_MAX && o < BAR_SIZE && o % 4 == 0);
	if (h >= NBAR_MAX || o >= BAR_SIZE)
		return;
	reg_writes++;
	if (o >= 0x200 && o < 0x220)	/* GPI_IS is write one to clear */
		regs[h][o / 4] &= ~v;
	else
		regs[h][o / 4] = v;
}

static const char *
device_xname(device_t d)
{
	(void)d;
	return "igpio0";
}

struct test_device { int unit; };
static struct test_device gpio_dev, acpi_dev;
static int gpio_softc_token;
static union { char b[4096]; long double align; } acpi_softc_storage;
static unsigned int gpiobus_found;

static void *
device_private(device_t d)
{
	if (d == &gpio_dev)
		return &gpio_softc_token;
	CHECK(d == &acpi_dev);
	return &acpi_softc_storage;
}

int
gpiobus_print(void *aux, const char *pnp)
{
	(void)aux; (void)pnp;
	return 0;
}

static device_t
config_found(device_t parent, void *aux, int (*print)(void *, const char *),
    int cfargs)
{
	const struct gpiobus_attach_args *gba = aux;

	(void)parent; (void)cfargs;
	CHECK(print == gpiobus_print);
	CHECK(gba->gba_npins > 0 && gba->gba_pins != NULL);
	gpiobus_found++;
	return &gpio_dev;
}

#include "igpio-source.inc"

/* ACPI front end model. */
#define __KERNEL_RCSID(a, b)
struct device_compatible_entry { const char *compat; };
#define DEVICE_COMPAT_EOL	{ NULL }
typedef struct test_cfdata *cfdata_t;
#define CFATTACH_DECL_NEW(n, s, m, a, d, r) \
    static const size_t test_softc_size = (s); \
    static int (*const test_match)(device_t, cfdata_t, void *) = (m); \
    static void (*const test_attach)(device_t, device_t, void *) = (a); \
    static int (*const test_detach)(device_t, int) = (d)
struct acpi_devnode {
	ACPI_HANDLE		ad_handle;
	ACPI_DEVICE_INFO	*ad_devinfo;
};
struct acpi_attach_args {
	struct acpi_devnode	*aa_node;
	bus_space_tag_t		aa_memt;
};
struct acpi_irq { uint32_t ar_irq; };
struct acpi_mem { bus_addr_t ar_base; bus_size_t ar_length; };
struct acpi_resources { int nmem; };
struct acpi_resource_parse_ops { int unused; };
struct acpi_event;
static const struct acpi_resource_parse_ops acpi_resource_parse_ops_default;
static struct acpi_irq model_irq = { 14 };
static struct acpi_mem model_mem[NBAR_MAX];
static int model_nmem;
static ACPI_STATUS model_aei = AE_NOT_FOUND;
static unsigned int intr_established, intr_disestablished, gpio_registered;
static int (*model_intr)(void *);
static void *model_intr_arg;
static int (*model_translate)(void *, ACPI_RESOURCE_GPIO *, void **);
static void *model_translate_priv;
static int intr_token;

static ACPI_STATUS
acpi_resource_parse(device_t d, ACPI_HANDLE h, const char *p,
    struct acpi_resources *res, const struct acpi_resource_parse_ops *ops)
{
	(void)d; (void)h; (void)ops;
	CHECK(strcmp(p, "_CRS") == 0);
	res->nmem = model_nmem;
	return AE_OK;
}

static struct acpi_irq *
acpi_res_irq(struct acpi_resources *res, int n)
{
	(void)res;
	return n == 0 ? &model_irq : NULL;
}

static struct acpi_mem *
acpi_res_mem(struct acpi_resources *res, int n)
{
	return n < res->nmem ? &model_mem[n] : NULL;
}

static void acpi_resource_cleanup(struct acpi_resources *res) { (void)res; }

static ACPI_STATUS
acpi_event_create_gpio(device_t d, ACPI_HANDLE h,
    void (*f)(void *, struct acpi_event *, ACPI_RESOURCE_GPIO *), void *a)
{
	(void)d; (void)h; (void)f; (void)a;
	return model_aei;
}

static void *
acpi_intr_establish(device_t d, uint64_t h, int ipl, bool mpsafe,
    int (*f)(void *), void *a, const char *name)
{
	(void)d; (void)h; (void)mpsafe; (void)name;
	CHECK(ipl == IPL_VM);
	intr_established++;
	model_intr = f;
	model_intr_arg = a;
	return &intr_token;
}

static void
acpi_intr_disestablish(void *c)
{
	CHECK(c == &intr_token);
	intr_disestablished++;
}

static ACPI_STATUS
acpi_gpio_register(struct acpi_devnode *ad, device_t d,
    int (*f)(void *, ACPI_RESOURCE_GPIO *, void **), void *p)
{
	(void)ad;
	CHECK(d == &acpi_dev);
	gpio_registered++;
	model_translate = f;
	model_translate_priv = p;
	return AE_OK;
}

static int
acpi_compatible_match(const struct acpi_attach_args *aa,
    const struct device_compatible_entry *e)
{
	for (; e->compat != NULL; e++) {
		if (strcmp(e->compat, aa->aa_node->ad_devinfo->HardwareId.String)
		    == 0)
			return 1;
	}
	return 0;
}

static bool pmf_device_register(device_t d, void *a, void *b)
{ (void)d; (void)a; (void)b; return true; }
static void pmf_device_deregister(device_t d) { (void)d; }
const char *AcpiFormatException(ACPI_STATUS s) { (void)s; return "test"; }

#include "igpio-acpi-source.inc"

#define __SHIFTOUT_RXEV(v) \
    (((v) & IGPIO_PADCFG0_RXEVCFG_MASK) >> IGPIO_PADCFG0_RXEVCFG_SHIFT)

static bus_addr_t bases[NBAR_MAX];
static bus_size_t lengths[NBAR_MAX];
static bus_space_handle_t handles[NBAR_MAX];
static struct igpio_softc sc;

static void
reset_hw(void)
{
	int i;

	memset(regs, 0, sizeof(regs));
	for (i = 0; i < NBAR_MAX; i++) {
		regs[i][0] = 0x01000000;	/* REVID 0x100: 4 PADCFG regs */
		regs[i][IGPIO_PADBAR / 4] = PADBAR;
		/* Firmware left stale enables and status behind. */
		regs[i][0x220 / 4] = 0xffffffff;
		regs[i][0x200 / 4] = 0x00000005;
		bases[i] = (bus_addr_t)(i + 1) * 0x100000;
		lengths[i] = BAR_SIZE;
	}
}

static void
attach(const char *hid, int nbar)
{
	memset(&sc, 0, sizeof(sc));
	sc.sc_acpi_hid = hid;
	sc.sc_nbar = nbar;
	sc.sc_base = bases;
	sc.sc_length = lengths;
	sc.sc_bsh = handles;
	igpio_attach(&sc);
}

static uint32_t
padcfg0(int bar, int pad)
{
	return regs[bar][(PADBAR + pad * 16) / 4];
}

static unsigned int handled;
static void *handler_ih;

static int
handler(void *arg)
{
	CHECK(arg == &handled);
	handled++;
	/* ihidev masks a level interrupt from inside its handler. */
	if (handler_ih != NULL) {
		sc.sc_gc.gp_intr_mask(sc.sc_gc.gp_cookie, handler_ih);
		sc.sc_gc.gp_intr_unmask(sc.sc_gc.gp_cookie, handler_ih);
	}
	return 1;
}

static void
test_mtps_numbering(void)
{
	int pad;
	struct igpio_bank *ib;
	const struct igpio_gpp *gg;
	int i;

	reset_hw();
	attach("INTC1084", NBAR);
	CHECK(sc.sc_banks != NULL);
	CHECK(sc.sc_gpp_device != NULL);
	/* JTAG_CPU, the last group, starts at ACPI GPIO 576 with 16 pads. */
	CHECK(sc.sc_npins == 592);
	if (sc.sc_banks == NULL || sc.sc_npins != 592)
		return;

	/* Attach masks and clears all GPI interrupts left by firmware. */
	for (i = 0; i < NBAR; i++) {
		CHECK(regs[i][0x220 / 4] != 0xffffffff);
		CHECK(regs[i][0x200 / 4] == 0);
	}

	/* GPP_D ends at 24, GPP_R starts at ACPI 32 with pad 25. */
	CHECK(sc.sc_pins[24].pin_caps != 0);
	CHECK(sc.sc_pins[25].pin_caps == 0);
	CHECK(sc.sc_pins[31].pin_caps == 0);
	CHECK(sc.sc_pins[32].pin_caps != 0);
	ib = igpio_pin_bank(&sc, 32, &pad, &gg);
	CHECK(ib == &sc.sc_banks[0] && pad == 25 && gg->igg_regno == 1);

	/* GPP_B pin 3: ACPI 195, pad 118, community 1 index 30. */
	ib = igpio_pin_bank(&sc, 195, &pad, &gg);
	CHECK(ib == &sc.sc_banks[1] && pad == 30 && gg->igg_regno == 2);

	/* GPP_C pin 0: ACPI 256, pad 146, community 2 index 9. */
	ib = igpio_pin_bank(&sc, 256, &pad, &gg);
	CHECK(ib == &sc.sc_banks[2] && pad == 9 && gg->igg_regno == 1);

	/* JTAG_CPU last pin: ACPI 591, pad 338, community 4 index 36. */
	ib = igpio_pin_bank(&sc, 591, &pad, &gg);
	CHECK(ib == &sc.sc_banks[4] && pad == 36 && gg->igg_regno == 1);

	/* A hole has no pad and is never touched. */
	i = reg_writes;
	CHECK(igpio_pin_bank(&sc, 57, &pad, NULL) == NULL); /* past GPP_J */
	CHECK(igpio_pin_read(&sc, 25) == 0);
	igpio_pin_write(&sc, 25, 1);
	igpio_pin_ctl(&sc, 25, GPIO_PIN_OUTPUT);
	CHECK(reg_writes == (unsigned int)i);
	CHECK(igpio_intr_establish(&sc, 25, IPL_VM, GPIO_INTR_LOW_LEVEL,
	    handler, &handled) == NULL);

	/* Pin reads use the translated pad. */
	regs[1][(PADBAR + 30 * 16) / 4] = IGPIO_PADCFG0_GPIOTXDIS |
	    IGPIO_PADCFG0_GPIORXSTATE;
	CHECK(igpio_pin_read(&sc, 195) == 1);
	CHECK(igpio_pin_read(&sc, 115 + 3) == 0);	/* linear pad number */

	CHECK(sc.sc_gc.gp_intr_mask != NULL);
	CHECK(sc.sc_gc.gp_intr_unmask != NULL);
}

static void
test_mtps_interrupts(void)
{
	void *ih, *ih2;
	uint32_t v;

	reset_hw();
	errors = 0;
	attach("INTC1084", NBAR);
	if (sc.sc_banks == NULL || sc.sc_gpp_intr == NULL)
		return;

	/* ACPI-owned pads signal GPEs; they must not become GPI interrupts. */
	CHECK(igpio_intr_establish(&sc, 195, IPL_VM, GPIO_INTR_LOW_LEVEL,
	    handler, &handled) == NULL);
	CHECK(errors == 1);

	/* GPP_B: HOSTSW_OWN register 2, bit 3 belongs to the driver. */
	regs[1][(0x150 + 2 * 4) / 4] = __BIT(3);
	regs[1][(PADBAR + 30 * 16) / 4] = IGPIO_PADCFG0_GPIORXDIS |
	    IGPIO_PADCFG0_GPIROUTIOXAPIC | IGPIO_PADCFG0_GPIROUTSCI |
	    __SHIFTIN(1, IGPIO_PADCFG0_PMODE_MASK) |
	    __SHIFTIN(IGPIO_PADCFG0_RXEVCFG_DISABLED,
	    IGPIO_PADCFG0_RXEVCFG_MASK);
	ih = igpio_intr_establish(&sc, 195, IPL_VM, GPIO_INTR_LOW_LEVEL,
	    handler, &handled);
	CHECK(ih != NULL);
	v = padcfg0(1, 30);
	CHECK(__SHIFTOUT_RXEV(v) == IGPIO_PADCFG0_RXEVCFG_LEVEL);
	CHECK(v & IGPIO_PADCFG0_RXINV);
	CHECK((v & IGPIO_PADCFG0_PMODE_MASK) == 0);
	CHECK((v & IGPIO_PADCFG0_GPIORXDIS) == 0);
	CHECK(v & IGPIO_PADCFG0_GPIOTXDIS);
	CHECK((v & (IGPIO_PADCFG0_GPIROUTIOXAPIC | IGPIO_PADCFG0_GPIROUTSCI |
	    IGPIO_PADCFG0_GPIROUTSMI | IGPIO_PADCFG0_GPIROUTNMI)) == 0);
	CHECK((v & IGPIO_PADCFG0_GPIOTXSTATE) == 0);
	CHECK(regs[1][(0x220 + 2 * 4) / 4] == __BIT(3));
	/* A second handler for the same pin is refused. */
	CHECK(igpio_intr_establish(&sc, 195, IPL_VM, GPIO_INTR_LOW_LEVEL,
	    handler, &handled) == NULL);

	/* Status in GPP_B register 2 dispatches and is acknowledged. */
	handler_ih = ih;
	regs[1][(0x200 + 2 * 4) / 4] = __BIT(3) | __BIT(4);
	CHECK(igpio_intr(&sc) == 1);
	CHECK(handled == 1);
	CHECK(regs[1][(0x200 + 2 * 4) / 4] == __BIT(4));
	CHECK(regs[1][(0x220 + 2 * 4) / 4] == __BIT(3));
	handler_ih = NULL;

	/* The same bit in another group belongs to another pin. */
	regs[0][(0x200 + 2 * 4) / 4] = __BIT(3);
	CHECK(igpio_intr(&sc) == 0);
	CHECK(handled == 1);

	sc.sc_gc.gp_intr_mask(&sc, ih);
	CHECK(regs[1][(0x220 + 2 * 4) / 4] == 0);
	regs[1][(0x200 + 2 * 4) / 4] = __BIT(3);
	CHECK(igpio_intr(&sc) == 0);
	sc.sc_gc.gp_intr_unmask(&sc, ih);
	CHECK(igpio_intr(&sc) == 1);
	CHECK(handled == 2);

	/* Falling edge on the last JTAG_CPU pad. */
	regs[4][(0x150 + 1 * 4) / 4] = __BIT(15);
	ih2 = igpio_intr_establish(&sc, 591, IPL_VM, GPIO_INTR_NEG_EDGE,
	    handler, &handled);
	CHECK(ih2 != NULL);
	v = padcfg0(4, 36);
	CHECK(__SHIFTOUT_RXEV(v) == IGPIO_PADCFG0_RXEVCFG_EDGE);
	CHECK(v & IGPIO_PADCFG0_RXINV);
	CHECK(regs[4][(0x220 + 1 * 4) / 4] == __BIT(15));
	CHECK(igpio_intr_establish(&sc, 32, IPL_VM, 0, handler,
	    &handled) == NULL);

	igpio_intr_disestablish(&sc, ih);
	CHECK(regs[1][(0x220 + 2 * 4) / 4] == 0);
	regs[1][(0x200 + 2 * 4) / 4] = __BIT(3);
	CHECK(igpio_intr(&sc) == 0);
	igpio_intr_disestablish(&sc, ih2);
	CHECK(regs[4][(0x220 + 1 * 4) / 4] == 0);

	igpio_detach(&sc);
	CHECK(sc.sc_gpp_intr == NULL && sc.sc_pins == NULL);
}

static void
test_missing_community(void)
{
	reset_hw();
	attach("INTC1084", NBAR - 1);
	CHECK(sc.sc_banks == NULL);
}

static void
test_unknown_community(void)
{
	/* A sixth BAR has no setup; the failed attach must unwind. */
	reset_hw();
	attach("INTC1084", NBAR_MAX);
	CHECK(sc.sc_banks == NULL);
	CHECK(live_bytes == 0);
}

static void
test_legacy_linear(void)
{
	void *ih;

	int pad;

	/* Alder Lake-S keeps the historical linear pad numbering. */
	reset_hw();
	attach("INTC1056", NBAR);
	CHECK(sc.sc_banks != NULL);
	CHECK(sc.sc_gpp_device == NULL);
	CHECK(sc.sc_npins == 304);
	CHECK(sc.sc_gc.gp_intr_mask == NULL);
	if (sc.sc_banks == NULL)
		return;
	CHECK(igpio_pin_bank(&sc, 95, &pad, NULL) == &sc.sc_banks[1] &&
	    pad == 0);

	/* Disestablish must undo the pad it configured, not bank 0 pad 0. */
	ih = igpio_intr_establish(&sc, 95, IPL_VM, GPIO_INTR_LOW_LEVEL,
	    handler, &handled);
	CHECK(ih != NULL);
	regs[0][PADBAR / 4] = IGPIO_PADCFG0_GPIROUTIOXAPIC;
	CHECK(padcfg0(1, 0) & IGPIO_PADCFG0_GPIROUTIOXAPIC);
	igpio_intr_disestablish(&sc, ih);
	CHECK((padcfg0(1, 0) & IGPIO_PADCFG0_GPIROUTIOXAPIC) == 0);
	CHECK(padcfg0(0, 0) == IGPIO_PADCFG0_GPIROUTIOXAPIC);
	igpio_detach(&sc);
}

static ACPI_DEVICE_INFO devinfo;
static struct acpi_devnode node;
static struct acpi_attach_args aa;

static void
acpi_attach(const char *hid, int nmem, ACPI_STATUS aei)
{
	int i;

	reset_hw();
	memset(&acpi_softc_storage, 0, sizeof(acpi_softc_storage));
	devinfo.HardwareId.String = __UNCONST(hid);
	node.ad_devinfo = &devinfo;
	aa.aa_node = &node;
	for (i = 0; i < NBAR_MAX; i++) {
		model_mem[i].ar_base = bases[i];
		model_mem[i].ar_length = lengths[i];
	}
	model_nmem = nmem;
	model_aei = aei;
	intr_established = intr_disestablished = gpio_registered = 0;
	model_translate = NULL;
	CHECK(test_softc_size <= sizeof(acpi_softc_storage));
	test_attach(NULL, &acpi_dev, &aa);
}

static int
translate(uint16_t acpi_pin, void **gpiop)
{
	ACPI_RESOURCE_GPIO gpio;
	UINT16 table[1] = { acpi_pin };

	memset(&gpio, 0, sizeof(gpio));
	gpio.PinTable = table;
	gpio.PinTableLength = 1;
	return model_translate(model_translate_priv, &gpio, gpiop);
}

static void
test_acpi_front_end(void)
{
	struct igpio_acpi_softc *asc = (void *)&acpi_softc_storage;
	void *gpiop = NULL, *ih;

	devinfo.HardwareId.String = __UNCONST("INTC1084");
	node.ad_devinfo = &devinfo;
	aa.aa_node = &node;
	CHECK(test_match(NULL, NULL, &aa) != 0);
	devinfo.HardwareId.String = __UNCONST("INTC1082");
	CHECK(test_match(NULL, NULL, &aa) == 0);

	/* The laptop's PGPI has no _AEI; its GPI interrupt is still needed. */
	acpi_attach("INTC1084", NBAR, AE_NOT_FOUND);
	CHECK(asc->sc_isc.sc_banks != NULL);
	CHECK(intr_established == 1);
	CHECK(gpio_registered == 1 && model_translate != NULL);
	if (model_translate == NULL || asc->sc_isc.sc_banks == NULL)
		return;
	CHECK(translate(195, &gpiop) == 195);
	CHECK(gpiop == &gpio_softc_token);
	CHECK(translate(25, NULL) == -1);	/* between GPP_D and GPP_R */
	CHECK(translate(592, NULL) == -1);	/* past JTAG_CPU */

	/* A GpioInt established through gpio(4) fires through _CRS IRQ. */
	regs[1][(0x150 + 2 * 4) / 4] = __BIT(3);
	ih = asc->sc_isc.sc_gc.gp_intr_establish(&asc->sc_isc, 195, IPL_VM,
	    GPIO_INTR_LOW_LEVEL, handler, &handled);
	CHECK(ih != NULL);
	handled = 0;
	regs[1][(0x200 + 2 * 4) / 4] = __BIT(3);
	CHECK(model_intr(model_intr_arg) == 1);
	CHECK(handled == 1);
	asc->sc_isc.sc_gc.gp_intr_disestablish(&asc->sc_isc, ih);
	test_detach(&acpi_dev, 0);
	CHECK(intr_disestablished == 1);

	/* Other _AEI failures are reported but do not drop the interrupt. */
	errors = 0;
	acpi_attach("INTC1084", NBAR, AE_ERROR);
	CHECK(errors == 1 && intr_established == 1 && gpio_registered == 1);
	test_detach(&acpi_dev, 0);

	/* A failed attach unwinds before any interrupt was established. */
	acpi_attach("INTC1084", NBAR_MAX, AE_NOT_FOUND);
	CHECK(asc->sc_isc.sc_banks == NULL);
	CHECK(intr_established == 0 && intr_disestablished == 0);
	CHECK(gpio_registered == 0);

	/* Linear legacy numbering is not offered to ACPI GPIO consumers. */
	acpi_attach("INTC1056", NBAR, AE_NOT_FOUND);
	CHECK(asc->sc_isc.sc_banks != NULL);
	CHECK(intr_established == 1 && gpio_registered == 0);
	test_detach(&acpi_dev, 0);
	CHECK(live_bytes == 0);
}

int
main(void)
{
	test_mtps_numbering();
	if (sc.sc_banks != NULL)
		igpio_detach(&sc);
	test_mtps_interrupts();
	test_missing_community();
	test_unknown_community();
	test_legacy_linear();
	test_acpi_front_end();
	CHECK(live_bytes == 0);
	printf("igpio-mtps-contract: %u checks, %u failures\n", checks,
	    failures);
	return failures != 0;
}
