/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted. */
/*
 * Execute the complete native ACPI frontend against modeled kernel services.
 * The model does not execute xhci_init, AML, interrupts, DMA or MMIO. Its types
 * model the frontend's service boundary, not native ACPICA or kernel layouts.
 * Zero children is only an attach result, not IRQ or controller qualification.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "queue-native.h"

static unsigned int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "%s:%u: %s\n", __func__, (unsigned)__LINE__, #c); \
} } while (0)
#define __BIT(n) (UINT32_C(1) << (n))
#define ACPI_FAILURE(s) ((s) != 0)
#define AE_OK 0
#define USBREV_2_0 4
#define USBREV_3_0 5
#define XHCI_32BIT_ACCESS __BIT(2)
#define XHCI_HCCPARAMS 0x10
#define XHCI_HCC_AC64(n) ((n) & 1U)
#define BUS_DMA_TAG_VALID(t) ((t) != NULL)
#define IPL_USB 7
#define CFARGS_NONE NULL
#define DEVICE_COMPAT_EOL { .compat = NULL }

typedef uint64_t ACPI_INTEGER;
typedef unsigned int ACPI_STATUS;
typedef void *ACPI_HANDLE;
typedef uintptr_t bus_addr_t;
typedef size_t bus_size_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef void *bus_dma_tag_t;
typedef void *cfdata_t;
struct model_device { void *private; const char *name; };
typedef struct model_device *device_t;
struct device_compatible_entry { const char *compat; uintptr_t value; };
struct usbd_bus {
	void *ub_hcpriv;
	int ub_revision;
	bus_dma_tag_t ub_dmatag;
};
struct xhci_softc {
	device_t sc_dev, sc_child, sc_child2;
	bus_size_t sc_ios;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	struct usbd_bus sc_bus, sc_bus2;
	unsigned int sc_quirks;
	void (*sc_vendor_init)(struct xhci_softc *);
	int sc_usb3nports, sc_usb2nports;
	int sc_rhportcount[2];
};
struct acpi_devnode {
	ACPI_HANDLE ad_handle;
	SIMPLEQ_ENTRY(acpi_devnode) ad_child_list;
	SIMPLEQ_HEAD(, acpi_devnode) ad_child_head;
};
struct acpi_attach_args {
	struct acpi_devnode *aa_node;
	bus_space_tag_t aa_memt;
	bus_dma_tag_t aa_dmat, aa_dmat64;
};
struct acpi_resources { unsigned int id; };
struct acpi_mem { bus_addr_t ar_base; bus_size_t ar_length; };
struct acpi_irq { unsigned int ar_irq; };
static const unsigned int acpi_resource_parse_ops_default = 1;
static const unsigned int acpi_resource_parse_ops_quiet = 2;
struct model_attachment {
	size_t size;
	int (*match)(device_t, cfdata_t, void *);
	void (*attach)(device_t, device_t, void *);
};
#define CFATTACH_DECL2_NEW(n, s, m, a, d, v, r, c) \
	static const struct model_attachment model_attachment = { (s), (m), (a) }

enum event { PARSE_MAIN, PARSE_URS, MAP, READ_CAP, ESTABLISH, INIT,
    POST_RESET, CHILD_SS, CHILD_HS, CLEAN_MAIN, CLEAN_URS };
static struct {
	struct model_device parent, self, ss, hs;
	struct acpi_devnode node, child[3];
	struct acpi_attach_args aa;
	struct acpi_mem mem;
	struct acpi_irq irq;
	const char *compatible;
	unsigned int parse_error, urs_error, map_error, init_error;
	bool missing_mem, missing_irq, missing_urs_irq, intr_error;
	bool child_null[2], ac64;
	int ports[2];
	unsigned int cleanup[2], resets, configs[2];
	unsigned int event_count;
	enum event events[32];
	char errors[512];
	unsigned int handles[6];
} model;

static void
event(enum event value)
{
	CHECK(model.event_count < sizeof(model.events) / sizeof(model.events[0]));
	if (model.event_count < sizeof(model.events) / sizeof(model.events[0]))
		model.events[model.event_count++] = value;
}

static void *
device_private(device_t self)
{
	CHECK(self == &model.self);
	return self->private;
}

static const char *
device_xname(device_t self)
{
	CHECK(self == &model.self);
	return self->name;
}

static const struct device_compatible_entry *
acpi_compatible_lookup(const struct acpi_attach_args *aa,
    const struct device_compatible_entry *table)
{
	CHECK(aa == &model.aa);
	for (; table->compat != NULL; table++)
		if (strcmp(table->compat, model.compatible) == 0)
			return table;
	/* Autoconfiguration calls attach only after a successful match. */
	abort();
}

static int
acpi_compatible_match(const struct acpi_attach_args *aa,
    const struct device_compatible_entry *table)
{
	CHECK(aa == &model.aa);
	for (; table->compat != NULL; table++)
		if (strcmp(table->compat, model.compatible) == 0)
			return 100;
	return 0;
}

static ACPI_STATUS
acpi_resource_parse(device_t self, ACPI_HANDLE handle, const char *name,
    struct acpi_resources *res, const unsigned int *ops)
{
	CHECK(self == &model.self && strcmp(name, "_CRS") == 0);
	if (handle == model.node.ad_handle) {
		event(PARSE_MAIN);
		CHECK(ops == &acpi_resource_parse_ops_default);
		res->id = 0;
		return model.parse_error;
	}
	event(PARSE_URS);
	CHECK(handle == model.child[1].ad_handle);
	CHECK(ops == &acpi_resource_parse_ops_quiet);
	res->id = 1;
	return model.urs_error;
}

static void
acpi_resource_cleanup(struct acpi_resources *res)
{
	CHECK(res->id < 2);
	if (res->id < 2) {
		model.cleanup[res->id]++;
		event(res->id ? CLEAN_URS : CLEAN_MAIN);
	}
}

static struct acpi_mem *
acpi_res_mem(struct acpi_resources *res, int index)
{
	CHECK(res->id == 0 && index == 0);
	return model.missing_mem ? NULL : &model.mem;
}

static struct acpi_irq *
acpi_res_irq(struct acpi_resources *res, int index)
{
	CHECK(res->id < 2 && index == 0);
	return (res->id ? model.missing_urs_irq : model.missing_irq) ?
	    NULL : &model.irq;
}

static ACPI_STATUS
acpi_eval_integer(ACPI_HANDLE handle, const char *name, ACPI_INTEGER *adr)
{
	CHECK(strcmp(name, "_ADR") == 0);
	if (handle == model.child[0].ad_handle)
		return 1;
	*adr = handle == model.child[1].ad_handle ? 0 : 1;
	return AE_OK;
}

static const char *
AcpiFormatException(ACPI_STATUS status)
{
	CHECK(status == model.urs_error);
	return "modeled failure";
}

static void
aprint_error_dev(device_t self, const char *format, ...)
{
	va_list args;
	size_t used = strlen(model.errors);
	CHECK(self == &model.self);
	va_start(args, format);
	(void)vsnprintf(model.errors + used, sizeof(model.errors) - used,
	    format, args);
	va_end(args);
}

/* Verbose printing has no resource or attachment side effects. */
#define aprint_verbose_dev(...) ((void)0)
#define aprint_verbose(...) ((void)0)

static int
bus_space_map(bus_space_tag_t tag, bus_addr_t base, bus_size_t size,
    int flags, bus_space_handle_t *out)
{
	event(MAP);
	CHECK(tag == model.aa.aa_memt && base == model.mem.ar_base);
	CHECK(size == model.mem.ar_length && flags == 0);
	if (model.map_error)
		return (int)model.map_error;
	*out = 0xfeed0000;
	return 0;
}

static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	event(READ_CAP);
	CHECK(tag == model.aa.aa_memt && handle == 0xfeed0000);
	CHECK(offset == 0x10);
	return model.ac64 ? 1 : 0;
}

static int
xhci_intr(void *cookie)
{
	(void)cookie;
	/* Never called: this contract does not model a hardware interrupt. */
	abort();
}

static void *
acpi_intr_establish_irq(device_t self, struct acpi_irq *irq, int level,
    bool mpsafe, int (*handler)(void *), void *cookie, const char *name)
{
	event(ESTABLISH);
	CHECK(self == &model.self && irq == &model.irq && level == IPL_USB);
	CHECK(mpsafe && handler == xhci_intr && cookie == self->private);
	CHECK(strcmp(name, "xhci-test") == 0);
	return model.intr_error ? NULL : &model.handles[5];
}

static void
acpi_usb_post_reset(ACPI_HANDLE handle)
{
	event(POST_RESET);
	CHECK(handle == model.node.ad_handle);
	model.resets++;
}

static int
xhci_init(struct xhci_softc *sc)
{
	event(INIT);
	CHECK(sc == model.self.private && sc->sc_dev == &model.self);
	CHECK(sc->sc_bus.ub_hcpriv == sc && sc->sc_bus.ub_revision == 5);
	CHECK(sc->sc_quirks == 4 && sc->sc_vendor_init != NULL);
	if (model.init_error)
		return (int)model.init_error;
	/* Native xhci_init supplies these independently of config_found. */
	sc->sc_rhportcount[0] = model.ports[0];
	sc->sc_rhportcount[1] = model.ports[1];
	/* Legacy last-port sums are not root-hub descriptor port counts. */
	sc->sc_usb3nports = 19;
	sc->sc_usb2nports = 23;
	sc->sc_bus2.ub_hcpriv = sc;
	sc->sc_bus2.ub_revision = USBREV_2_0;
	sc->sc_bus2.ub_dmatag = sc->sc_bus.ub_dmatag;
	if (sc->sc_vendor_init != NULL)
		sc->sc_vendor_init(sc);
	return 0;
}

static int
usbctlprint(void *aux, const char *name)
{
	(void)aux;
	(void)name;
	abort();
}

static device_t
config_found(device_t self, void *aux, int (*print)(void *, const char *),
    const void *args)
{
	struct xhci_softc *sc = self->private;
	unsigned int bus = aux == &sc->sc_bus ? 0 : 1;
	CHECK(self == &model.self && print == usbctlprint && args == NULL);
	CHECK(aux == &sc->sc_bus || aux == &sc->sc_bus2);
	event(bus ? CHILD_HS : CHILD_SS);
	model.configs[bus]++;
	return model.child_null[bus] ? NULL : bus ? &model.hs : &model.ss;
}

#include "xhci-acpi-native.inc"

static struct xhci_acpi_softc softc;

static void
reset_model(void)
{
	memset(&model, 0, sizeof(model));
	memset(&softc, 0, sizeof(softc));
	model.self.private = &softc;
	model.self.name = "xhci-test";
	model.aa.aa_node = &model.node;
	model.node.ad_handle = &model.handles[0];
	SIMPLEQ_INIT(&model.node.ad_child_head);
	for (size_t i = 0; i < 3; i++) {
		model.child[i].ad_handle = &model.handles[i + 1];
		SIMPLEQ_INSERT_TAIL(&model.node.ad_child_head, &model.child[i],
		    ad_child_list);
	}
	model.aa.aa_memt = &model.handles[0];
	model.aa.aa_dmat = &model.handles[1];
	model.aa.aa_dmat64 = &model.handles[2];
	model.compatible = "PNP0D10";
	model.mem.ar_base = 0x12340000;
	model.mem.ar_length = 0x8000;
	model.irq.ar_irq = 272;
	model.ports[0] = 2;
	model.ports[1] = 3;
}

static void
attach(void)
{
	model_attachment.attach(&model.parent, &model.self, &model.aa);
}

static void
test_ports(void)
{
	/* Literal counts catch unconditional attachment and a swapped bus guard. */
	static const int counts[][2] = { { 2, 3 }, { 0, 1 }, { 1, 0 }, { 0, 0 } };
	static const unsigned int children[][2] = {
	    { 1, 1 }, { 0, 1 }, { 1, 0 }, { 0, 0 } };
	for (size_t i = 0; i < 4; i++) {
		reset_model();
		model.ports[0] = counts[i][0];
		model.ports[1] = counts[i][1];
		attach();
		CHECK(model.configs[0] == children[i][0]);
		CHECK(model.configs[1] == children[i][1]);
		CHECK(softc.sc_xhci.sc_child == (children[i][0] ? &model.ss : NULL));
		CHECK(softc.sc_xhci.sc_child2 == (children[i][1] ? &model.hs : NULL));
		CHECK(model.cleanup[0] == 1 && model.cleanup[1] == 0);
		CHECK(model.resets == 1);
		CHECK(i == 3 ? model.errors[0] != '\0' : model.errors[0] == '\0');
	}
	for (unsigned int mask = 1; mask <= 3; mask++) {
		reset_model();
		model.child_null[0] = (mask & 1) != 0;
		model.child_null[1] = (mask & 2) != 0;
		attach();
		CHECK(model.configs[0] == 1 && model.configs[1] == 1);
		CHECK(softc.sc_xhci.sc_child == (mask & 1 ? NULL : &model.ss));
		CHECK(softc.sc_xhci.sc_child2 == (mask & 2 ? NULL : &model.hs));
		CHECK(model.cleanup[0] == 1);
	}
}

static void
test_frontend_controls(void)
{
	static const char *const ids[] = {
	    "PNP0D10", "PNP0D15", "808622B7", "PNP0CA1", "UNKNOWN" };
	for (size_t i = 0; i < 5; i++) {
		reset_model();
		model.compatible = ids[i];
		CHECK(model_attachment.match(&model.parent, NULL, &model.aa) ==
		    (i == 4 ? 0 : 100));
	}
	CHECK(model_attachment.size == sizeof(softc));
	for (unsigned int mode = 0; mode < 3; mode++) {
		reset_model();
		model.ac64 = mode != 0;
		if (mode == 2)
			model.aa.aa_dmat64 = NULL;
		attach();
		CHECK(softc.sc_xhci.sc_bus.ub_dmatag ==
		    (mode == 1 ? &model.handles[2] : &model.handles[1]));
		CHECK(softc.sc_xhci.sc_bus2.ub_dmatag == softc.sc_xhci.sc_bus.ub_dmatag);
	}
	reset_model();
	model.compatible = "PNP0CA1";
	model.missing_irq = true; /* URS must use the child's IRQ, not this one. */
	attach();
	CHECK(model.cleanup[0] == 1 && model.cleanup[1] == 1);
	CHECK(model.configs[0] == 1 && model.configs[1] == 1);
	CHECK(model.event_count == 11);
	static const enum event sequence[] = { PARSE_MAIN, PARSE_URS, MAP, READ_CAP,
	    ESTABLISH, INIT, POST_RESET, CHILD_SS, CHILD_HS, CLEAN_MAIN, CLEAN_URS };
	CHECK(memcmp(model.events, sequence, sizeof(sequence)) == 0);
}

static void
test_failures(void)
{
	/* Characterize the existing returns; do not claim they clean up leaks. */
	for (unsigned int stage = 0; stage < 9; stage++) {
		reset_model();
		switch (stage) {
		case 0: model.parse_error = 1; break;
		case 1: model.missing_mem = true; break;
		case 2: model.missing_irq = true; break;
		case 3: model.map_error = ENOMEM; break;
		case 4: model.intr_error = true; break;
		case 5: model.init_error = EIO; break;
		case 6: model.compatible = "PNP0CA1"; model.urs_error = 1; break;
		case 7: model.compatible = "PNP0CA1"; model.missing_urs_irq = true; break;
		case 8: model.compatible = "PNP0CA1";
			SIMPLEQ_INIT(&model.node.ad_child_head); break;
		}
		attach();
		CHECK(model.configs[0] == 0 && model.configs[1] == 0);
		CHECK(softc.sc_xhci.sc_child == NULL && softc.sc_xhci.sc_child2 == NULL);
		CHECK(model.resets == 0);
		CHECK(model.cleanup[0] ==
		    ((stage == 1 || stage == 2 || stage >= 6) ? 1U : 0U));
		CHECK(model.cleanup[1] == (stage == 7 ? 1U : 0U));
		CHECK(stage == 0 ? model.errors[0] == '\0' : model.errors[0] != '\0');
	}
}

int
main(void)
{
	test_ports();
	test_frontend_controls();
	test_failures();
	printf("ACPI xHCI root-bus contract: %u checks, %u failures\n",
	    checks, failures);
	return failures ? 1 : 0;
}
