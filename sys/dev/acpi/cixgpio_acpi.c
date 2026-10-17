/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded CIX S5 GPIO ACPI event driver, AI-assisted. */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpi_event.h>
#include <dev/acpi/acpi_intr.h>

/* Cadence r1p02 register facts. No GPL implementation is included here. */
#define CIXGPIO_BYPASS	0x00
#define CIXGPIO_DIRECTION	0x04
#define CIXGPIO_INPUT	0x10
#define CIXGPIO_MASK	0x14
#define CIXGPIO_ENABLE	0x18
#define CIXGPIO_DISABLE	0x1c
#define CIXGPIO_STATUS	0x20	/* Read clears the bank's pending bits. */
#define CIXGPIO_TYPE	0x24
#define CIXGPIO_VALUE	0x28
#define CIXGPIO_ANY_EDGE	0x2c
#define CIXGPIO_PIN	__BIT(6)
#define CIXGPIO_ARM	1
#define CIXGPIO_LOW_LIMIT	32	/* Service budget, not vendor timing. */

struct cixgpio_profile {
	ACPI_HANDLE handle;
	unsigned int memory, irq, event;
};

struct cixgpio_acpi_softc {
	device_t sc_dev;
	ACPI_HANDLE sc_handle;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	kmutex_t sc_lock;
	struct acpi_event *sc_event;
	void *sc_ih;
	unsigned int sc_low;
	unsigned int sc_interrupts, sc_completed, sc_errors;
	bool sc_pending, sc_fault, sc_armed;
};

static const struct device_compatible_entry cixgpio_compat[] = {
	{ .compat = "CIXH1003" }, DEVICE_COMPAT_EOL
};

static ACPI_STATUS
cixgpio_crs(ACPI_RESOURCE *res, void *arg)
{
	struct cixgpio_profile *p = arg;
	const ACPI_RESOURCE_FIXED_MEMORY32 *mem;
	const ACPI_RESOURCE_EXTENDED_IRQ *irq;

	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		mem = &res->Data.FixedMemory32;
		if (++p->memory != 1 || mem->WriteProtect != ACPI_READ_WRITE_MEMORY ||
		    mem->Address != 0x16004000 ||
		    mem->AddressLength != 0x1000)
			return AE_BAD_DATA;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		irq = &res->Data.ExtendedIrq;
		if (++p->irq != 1 || irq->InterruptCount != 1 ||
		    irq->Interrupts[0] != 404 ||
		    irq->ProducerConsumer != ACPI_CONSUMER ||
		    irq->Triggering != ACPI_LEVEL_SENSITIVE ||
		    irq->Polarity != ACPI_ACTIVE_HIGH ||
		    irq->Shareable != ACPI_EXCLUSIVE || irq->WakeCapable != 0 ||
		    irq->ResourceSource.StringLength != 0)
			return AE_BAD_DATA;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_END_TAG:
		return AE_OK;
	default:
		/* Do not silently drop descriptors unknown to a common parser. */
		return AE_BAD_DATA;
	}
}

static ACPI_STATUS
cixgpio_aei(ACPI_RESOURCE *res, void *arg)
{
	struct cixgpio_profile *p = arg;
	const ACPI_RESOURCE_GPIO *gpio;
	ACPI_HANDLE source;

	if (res->Type == ACPI_RESOURCE_TYPE_END_TAG)
		return AE_OK;
	if (res->Type != ACPI_RESOURCE_TYPE_GPIO)
		return AE_BAD_DATA;
	gpio = &res->Data.Gpio;
	if (++p->event != 1 || gpio->RevisionId != 1 ||
	    gpio->ConnectionType != ACPI_RESOURCE_GPIO_TYPE_INT ||
	    gpio->ProducerConsumer != ACPI_CONSUMER ||
	    gpio->PinTableLength != 1 || gpio->PinTable == NULL ||
	    gpio->PinTable[0] != 6 || gpio->Triggering != ACPI_LEVEL_SENSITIVE ||
	    gpio->Polarity != ACPI_ACTIVE_LOW || gpio->Shareable != ACPI_EXCLUSIVE ||
	    gpio->WakeCapable != 0 || gpio->PinConfig != ACPI_PIN_CONFIG_PULLUP ||
	    gpio->DriveStrength != 0 || gpio->DebounceTimeout != 0 ||
	    gpio->VendorLength != 0 || gpio->ResourceSource.Index != 0 ||
	    gpio->ResourceSource.StringLength != sizeof("\\_SB.GPI4") ||
	    gpio->ResourceSource.StringPtr == NULL ||
	    memcmp(gpio->ResourceSource.StringPtr, "\\_SB.GPI4",
	    sizeof("\\_SB.GPI4")) != 0)
		return AE_BAD_DATA;
	if (ACPI_FAILURE(AcpiGetHandle(NULL, gpio->ResourceSource.StringPtr,
	    &source)) || source != p->handle)
		return AE_BAD_DATA;
	return AE_OK;
}

static ACPI_STATUS
cixgpio_profile(ACPI_HANDLE handle)
{
	struct cixgpio_profile p = { .handle = handle };
	ACPI_HANDLE method;
	ACPI_INTEGER value;
	ACPI_STATUS rv;

	if (ACPI_FAILURE(acpi_eval_integer(handle, "_UID", &value)) || value != 4 ||
	    ACPI_FAILURE(acpi_dsd_integer(handle, "id", &value)) || value != 0 ||
	    ACPI_FAILURE(acpi_dsd_integer(handle, "ngpios", &value)) || value != 32 ||
	    ACPI_FAILURE(acpi_dsd_integer(handle, "gpio-io-mask", &value)) || value != 0)
		return AE_BAD_DATA;
	/* AcpiWalkResources owns and frees its parsed resource buffer. */
	rv = AcpiWalkResources(handle, "_CRS", cixgpio_crs, &p);
	if (ACPI_FAILURE(rv) || p.memory != 1 || p.irq != 1)
		return AE_BAD_DATA;
	rv = AcpiWalkResources(handle, "_AEI", cixgpio_aei, &p);
	if (ACPI_FAILURE(rv) || p.event != 1)
		return AE_BAD_DATA;
	return AcpiGetHandle(handle, "_L06", &method);
}

static int
cixgpio_acpi_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct acpi_attach_args *aa = aux;
	int match;

	(void)parent;
	(void)cf;
	match = acpi_compatible_match(aa, cixgpio_compat);
	if (match == 0 || ACPI_FAILURE(cixgpio_profile(aa->aa_node->ad_handle)))
		return 0;
	return match;
}

static uint32_t
cixgpio_read(struct cixgpio_acpi_softc *sc, bus_size_t off)
{

	return bus_space_read_4(sc->sc_bst, sc->sc_bsh, off);
}

static void
cixgpio_write(struct cixgpio_acpi_softc *sc, bus_size_t off, uint32_t value)
{

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, off, value);
}

/* May run in hard interrupt on queue failure. Never sleep or invoke AML. */
static void
cixgpio_completed(void *arg, ACPI_STATUS rv)
{
	struct cixgpio_acpi_softc *sc = arg;
	bool report = false;

	mutex_enter(&sc->sc_lock);
	KASSERT(sc->sc_pending);
	sc->sc_completed++;
	if (ACPI_FAILURE(rv)) {
		sc->sc_fault = true;
		sc->sc_errors++;
		report = true;
	} else if ((cixgpio_read(sc, CIXGPIO_INPUT) & CIXGPIO_PIN) != 0) {
		sc->sc_low = 0;
	} else if (++sc->sc_low >= CIXGPIO_LOW_LIMIT) {
		sc->sc_fault = true;
		sc->sc_errors++;
		report = true;
	}
	sc->sc_pending = false;
	/* Rearm under the same lock; no late unmask can overtake a new IRQ. */
	if (!sc->sc_fault)
		acpi_intr_unmask(sc->sc_ih);
	mutex_exit(&sc->sc_lock);
	if (report)
		aprint_error_dev(sc->sc_dev,
		    "event fault: %s, level budget %u; IRQ remains masked\n",
		    AcpiFormatException(rv), sc->sc_low);
}

static int
cixgpio_acpi_intr(void *arg)
{
	struct cixgpio_acpi_softc *sc = arg;
	uint32_t status;

	mutex_enter(&sc->sc_lock);
	/* Includes an IRQ that entered on another CPU before GSI masking. */
	if (!sc->sc_armed || sc->sc_pending || sc->sc_fault) {
		mutex_exit(&sc->sc_lock);
		return 1;
	}
	status = cixgpio_read(sc, CIXGPIO_STATUS);
	status &= ~cixgpio_read(sc, CIXGPIO_MASK) & CIXGPIO_PIN;
	if (status == 0) {
		mutex_exit(&sc->sc_lock);
		return 0;
	}
	sc->sc_pending = true;
	sc->sc_interrupts++;
	mutex_exit(&sc->sc_lock);
	/* Completion may execute synchronously; do not hold sc_lock here. */
	(void)acpi_event_notify(sc->sc_event);
	return 1;
}

static void
cixgpio_register_event(void *arg, struct acpi_event *ev, ACPI_RESOURCE_GPIO *gpio)
{
	struct cixgpio_acpi_softc *sc = arg;
	struct cixgpio_profile p = { .handle = sc->sc_handle };
	ACPI_RESOURCE res = { .Type = ACPI_RESOURCE_TYPE_GPIO };

	/* The factory evaluates _AEI again. Reject changed or duplicate data. */
	res.Data.Gpio = *gpio;
	if (sc->sc_event != NULL || ACPI_FAILURE(cixgpio_aei(&res, &p))) {
		sc->sc_fault = true;
		acpi_event_destroy(ev);
		return;
	}
	sc->sc_event = ev;
}

static void
cixgpio_acpi_attach(device_t parent, device_t self, void *aux)
{
	static const bus_size_t offsets[] = { 0, 4, 8, 12, 16, 20, 36, 40, 44 };
	const struct acpi_attach_args *aa = aux;
	const ACPI_HANDLE handle = aa->aa_node->ad_handle;
	struct cixgpio_acpi_softc *sc = device_private(self);
	struct acpi_irq irq = { .ar_irq = 404, .ar_type = ACPI_LEVEL_SENSITIVE };
	uint32_t snapshot[__arraycount(offsets)];
	ACPI_STATUS rv;
	unsigned int i;
	int flags = device_cfdata(self)->cf_flags;

	(void)parent;
	aprint_naive(": CIX S5 GPIO events\n");
	aprint_normal(": CIX S5 GPIO events (firmware-owned clock, S0 only)\n");
	sc->sc_dev = self;
	sc->sc_handle = handle;
	if ((flags & ~CIXGPIO_ARM) != 0 || ACPI_FAILURE(cixgpio_profile(handle))) {
		aprint_error_dev(self, "unsupported GPI4 resource/event profile\n");
		return;
	}
	sc->sc_bst = aa->aa_memt;
	if (bus_space_map(sc->sc_bst, 0x16004000, 0x1000, 0, &sc->sc_bsh) != 0) {
		aprint_error_dev(self, "could not map validated GPI4 resource\n");
		return;
	}
	for (i = 0; i < __arraycount(offsets); i++) {
		snapshot[i] = cixgpio_read(sc, offsets[i]);
		aprint_normal_dev(self, "snapshot reg %#x = %#x\n",
		    (unsigned int)offsets[i], snapshot[i]);
	}
	if ((flags & CIXGPIO_ARM) == 0) {
		aprint_normal_dev(self, "passive: IRQ not installed or enabled\n");
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, 0x1000);
		return;
	}
	if ((snapshot[0] & CIXGPIO_PIN) != 0 ||
	    (snapshot[1] & CIXGPIO_PIN) == 0 ||
	    (~snapshot[5] & ~CIXGPIO_PIN) != 0) {
		aprint_error_dev(self, "pin6 input/bypass or IRQ ownership rejected\n");
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, 0x1000);
		return;
	}
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_VM);
	/* Own only pin6. Do not change mux, pulls, direction, output or clocks. */
	cixgpio_write(sc, CIXGPIO_DISABLE, CIXGPIO_PIN);
	rv = acpi_event_create_gpio(self, handle, cixgpio_register_event, sc);
	if (ACPI_FAILURE(rv) || sc->sc_event == NULL || sc->sc_fault)
		goto fail;
	cixgpio_write(sc, CIXGPIO_VALUE, snapshot[7] & ~CIXGPIO_PIN);
	cixgpio_write(sc, CIXGPIO_TYPE, snapshot[6] | CIXGPIO_PIN);
	cixgpio_write(sc, CIXGPIO_ANY_EDGE, snapshot[8] & ~CIXGPIO_PIN);
	if ((cixgpio_read(sc, CIXGPIO_VALUE) & CIXGPIO_PIN) != 0 ||
	    (cixgpio_read(sc, CIXGPIO_TYPE) & CIXGPIO_PIN) == 0 ||
	    (cixgpio_read(sc, CIXGPIO_ANY_EDGE) & CIXGPIO_PIN) != 0) {
		goto fail;
	}
	sc->sc_ih = acpi_intr_establish_irq(self, &irq, IPL_VM, true,
	    cixgpio_acpi_intr, sc, device_xname(self));
	if (sc->sc_ih == NULL)
		goto fail;
	acpi_event_set_intrcookie(sc->sc_event, sc->sc_ih);
	acpi_event_set_completion(sc->sc_event, cixgpio_completed, sc);
	/* No STATUS read before the interrupt: its read clears pending bits. */
	mutex_enter(&sc->sc_lock);
	sc->sc_armed = true;
	cixgpio_write(sc, CIXGPIO_ENABLE, CIXGPIO_PIN);
	mutex_exit(&sc->sc_lock);
	aprint_normal_dev(self, "armed pin6 level-low, GSI404; budget %u\n",
	    CIXGPIO_LOW_LIMIT);
	return;
fail:
	if (sc->sc_event != NULL) {
		acpi_event_destroy(sc->sc_event);
		sc->sc_event = NULL;
	}
	mutex_destroy(&sc->sc_lock);
	bus_space_unmap(sc->sc_bst, sc->sc_bsh, 0x1000);
	aprint_error_dev(self, "event setup failed; pin6 remains disabled\n");
}

/* Built-in only: armed hot detach is not supported while AML can be queued. */
CFATTACH_DECL_NEW(cixgpio_acpi, sizeof(struct cixgpio_acpi_softc),
    cixgpio_acpi_match, cixgpio_acpi_attach, NULL, NULL);
