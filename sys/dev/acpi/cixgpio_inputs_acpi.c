/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded CIX P1 fixed GPI5 inputs, AI-assisted. */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/gpio.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/gpio/gpiovar.h>

#define CIXGPIOIN_GPIO_BASE	0x16005000
#define CIXGPIOIN_MUX_BASE	0x16007000
#define CIXGPIOIN_SIZE		0x1000
#define CIXGPIOIN_ENABLE	1

#define CIXGPIOIN_PINS 3
#define CIXGPIOIN_MASK 7U

struct cixgpioin_softc {
	device_t dev;
	bus_space_tag_t bst;
	bus_space_handle_t gpio;
	kmutex_t lock;
	struct gpio_chipset_tag gc;
	gpio_pin_t pins[CIXGPIOIN_PINS];
};

struct cixgpioin_resources {
	bool mux;
	unsigned int memory, irq, groups, end;
};

static const struct device_compatible_entry cixgpioin_compat[] = {
	{ .compat = "CIXH1003" }, DEVICE_COMPAT_EOL
};

/*
 * CIX S5 pads0..65 use big-endian vendor offset/config pairs.
 * Validate metadata only. Never apply a PinGroup or its vendor settings.
 * The 32-group/pin limits bound this observation, not the general pinctrl ABI.
 */
static bool
cixgpioin_group(const ACPI_RESOURCE_PIN_GROUP *group)
{
	unsigned int i, j;
	const char *label = group->ResourceLabel.StringPtr;

	if (group->RevisionId != 1 || group->ProducerConsumer != ACPI_PRODUCER ||
	    group->PinTableLength == 0 || group->PinTableLength > 32 ||
	    group->PinTable == NULL || group->VendorData == NULL ||
	    group->VendorLength != 4 * group->PinTableLength ||
	    group->ResourceLabel.StringLength < 2 ||
	    group->ResourceLabel.StringLength > 64 || label == NULL ||
	    label[group->ResourceLabel.StringLength - 1] != '\0')
		return false;
	for (i = 0; i + 1 < group->ResourceLabel.StringLength; i++)
		if ((unsigned char)label[i] < 0x20 || (unsigned char)label[i] > 0x7e)
			return false;
	for (i = 0; i < group->PinTableLength; i++) {
		unsigned int offset, config;

		if (group->PinTable[i] > 65)
			return false;
		for (j = 0; j < i; j++)
			if (group->PinTable[j] == group->PinTable[i])
				return false;
		offset = (unsigned int)group->VendorData[4 * i] << 8 |
		    group->VendorData[4 * i + 1];
		config = (unsigned int)group->VendorData[4 * i + 2] << 8 |
		    group->VendorData[4 * i + 3];
		if (offset != 4 * group->PinTable[i] || (config & ~0x1ffU) != 0)
			return false;
	}
	return true;
}

static ACPI_STATUS
cixgpioin_crs(ACPI_RESOURCE *res, void *arg)
{
	struct cixgpioin_resources *p = arg;
	const ACPI_RESOURCE_FIXED_MEMORY32 *memory;
	const ACPI_RESOURCE_EXTENDED_IRQ *irq;

	if (p->end != 0)
		return AE_BAD_DATA;
	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		memory = &res->Data.FixedMemory32;
		if (res->Length < offsetof(ACPI_RESOURCE, Data) + sizeof(*memory) ||
		    ++p->memory != 1 ||
		    memory->WriteProtect != ACPI_READ_WRITE_MEMORY ||
		    memory->Address != (p->mux ? CIXGPIOIN_MUX_BASE :
		    CIXGPIOIN_GPIO_BASE) || memory->AddressLength != CIXGPIOIN_SIZE)
			return AE_BAD_DATA;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		irq = &res->Data.ExtendedIrq;
		if (p->mux ||
		    res->Length < offsetof(ACPI_RESOURCE, Data) + sizeof(*irq) ||
		    ++p->irq != 1 || irq->ProducerConsumer != ACPI_CONSUMER ||
		    irq->InterruptCount != 1 || irq->Interrupts[0] != 405 ||
		    irq->Triggering != ACPI_LEVEL_SENSITIVE ||
		    irq->Polarity != ACPI_ACTIVE_HIGH ||
		    irq->Shareable != ACPI_EXCLUSIVE || irq->WakeCapable != 0 ||
		    irq->ResourceSource.Index != 0 ||
		    irq->ResourceSource.StringLength != 0)
			return AE_BAD_DATA;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_PIN_GROUP:
		if (!p->mux ||
		    res->Length < offsetof(ACPI_RESOURCE, Data) +
		    sizeof(res->Data.PinGroup) || ++p->groups > 32 ||
		    !cixgpioin_group(&res->Data.PinGroup))
			return AE_BAD_DATA;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_END_TAG:
		if (res->Length < offsetof(ACPI_RESOURCE, Data) +
		    sizeof(res->Data.EndTag))
			return AE_BAD_DATA;
		p->end++;
		return AE_OK;
	default:
		return AE_BAD_DATA;
	}
}

static bool
cixgpioin_identity(ACPI_HANDLE handle, const char *expected, ACPI_INTEGER uid)
{
	ACPI_INTEGER value;
	char *hid;
	bool match;

	if (ACPI_FAILURE(acpi_eval_string(handle, "_HID", &hid)))
		return false;
	match = strcmp(hid, expected) == 0;
	ACPI_FREE(hid);
	return match && ACPI_SUCCESS(acpi_eval_integer(handle, "_UID", &value)) &&
	    value == uid;
}

static bool
cixgpioin_profile(ACPI_HANDLE gpio, ACPI_HANDLE *mux)
{
	struct cixgpioin_resources gp = { 0 }, mp = { .mux = true };
	ACPI_HANDLE expected;
	ACPI_INTEGER value;

	if (ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB.GPI5", &expected)) ||
	    expected != gpio || !cixgpioin_identity(gpio, "CIXH1003", 5) ||
	    ACPI_FAILURE(acpi_dsd_integer(gpio, "id", &value)) || value != 1 ||
	    ACPI_FAILURE(acpi_dsd_integer(gpio, "ngpios", &value)) || value != 10 ||
	    ACPI_FAILURE(acpi_dsd_integer(gpio, "gpio-io-mask", &value)) ||
	    value != 0 ||
	    ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB.MUX1", mux)) ||
	    !cixgpioin_identity(*mux, "CIXHA017", 1))
		return false;
	/* ACPICA owns the parsed buffers. Their pointer bounds remain trusted. */
	if (ACPI_FAILURE(AcpiWalkResources(gpio, "_CRS", cixgpioin_crs, &gp)) ||
	    gp.memory != 1 || gp.irq != 1 || gp.groups != 0 || gp.end != 1)
		return false;
	return ACPI_SUCCESS(AcpiWalkResources(*mux, "_CRS", cixgpioin_crs, &mp)) &&
	    mp.memory == 1 && mp.irq == 0 && mp.groups != 0 && mp.end == 1;
}

static int
cixgpioin_acpi_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct acpi_attach_args *aa = aux;
	ACPI_HANDLE expected;
	int match;

	(void)parent;
	if (cf->cf_flags != CIXGPIOIN_ENABLE)
		return 0;
	match = acpi_compatible_match(aa, cixgpioin_compat);
	if (match == 0 ||
	    ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB.GPI5", &expected)) ||
	    expected != aa->aa_node->ad_handle)
		return 0;
	return match;
}

/* The three fixed inputs are reserved, not a general pin configuration API. */
static int
cixgpioin_pin_read(void *cookie, int pin)
{
	struct cixgpioin_softc *sc = cookie;

	if ((unsigned int)pin >= CIXGPIOIN_PINS)
		return GPIO_PIN_LOW;
	return (bus_space_read_4(sc->bst, sc->gpio, 0x10) >> pin) & 1;
}

/* gpio_resume invokes this even on input-only pins. Never drive an output. */
static void
cixgpioin_pin_write(void *cookie, int pin, int value)
{

	(void)cookie;
	(void)pin;
	(void)value;
}

/* Direction is fixed at attach. No later configuration or resume write. */
static void
cixgpioin_pin_ctl(void *cookie, int pin, int flags)
{

	(void)cookie;
	(void)pin;
	(void)flags;
}

static void
cixgpioin_acpi_attach(device_t parent, device_t self, void *aux)
{
	const struct acpi_attach_args *aa = aux;
	struct cixgpioin_softc *sc = device_private(self);
	struct gpiobus_attach_args gba = { 0 };
	bus_space_handle_t mux;
	ACPI_HANDLE mux_handle;
	uint32_t mux80, mux84, mux88, bypass, direction, input, observed;
	unsigned int i;
	static const char * const names[] = { "GPIO015", "GPIO016", "GPIO017" };

	(void)parent;
	aprint_naive(": CIX fixed GPIO inputs\n");
	aprint_normal(": CIX GPI5 inputs (S0, firmware-owned mux/clock)\n");
	if (device_cfdata(self)->cf_flags != CIXGPIOIN_ENABLE ||
	    acpi_compatible_match(aa, cixgpioin_compat) == 0 ||
	    !cixgpioin_profile(aa->aa_node->ad_handle, &mux_handle)) {
		aprint_error_dev(self, "unsupported GPI5/MUX1 input profile\n");
		return;
	}
	sc->dev = self;
	sc->bst = aa->aa_memt;
	if (bus_space_map(sc->bst, CIXGPIOIN_GPIO_BASE,
	    CIXGPIOIN_SIZE, 0, &sc->gpio) != 0) {
		aprint_error_dev(self, "could not map validated GPI5 resource\n");
		return;
	}
	if (bus_space_map(sc->bst, CIXGPIOIN_MUX_BASE,
	    CIXGPIOIN_SIZE, 0, &mux) != 0) {
		aprint_error_dev(self, "could not map validated MUX1 resource\n");
		goto fail_gpio;
	}
	/* Nonclearing preflight only. STATUS would clear the entire bank. */
	mux80 = bus_space_read_4(sc->bst, mux, 0x80);
	mux84 = bus_space_read_4(sc->bst, mux, 0x84);
	mux88 = bus_space_read_4(sc->bst, mux, 0x88);
	bypass = bus_space_read_4(sc->bst, sc->gpio, 0x00);
	direction = bus_space_read_4(sc->bst, sc->gpio, 0x04);
	input = bus_space_read_4(sc->bst, sc->gpio, 0x10);
	aprint_normal_dev(self, "initial direction %#x, input %#x\n",
	    direction, input);
	bus_space_unmap(sc->bst, mux, CIXGPIOIN_SIZE);
	if (((mux80 | mux84 | mux88) & 0x180) != 0 ||
	    (bypass & CIXGPIOIN_MASK) != 0) {
		aprint_error_dev(self, "input mux/bypass ownership rejected\n");
		goto fail_gpio;
	}
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_VM);
	mutex_enter(&sc->lock);
	/* Cadence direction1 means input. Preserve all unowned bank bits. */
	direction = bus_space_read_4(sc->bst, sc->gpio, 0x04);
	bus_space_write_4(sc->bst, sc->gpio, 0x04,
	    direction | CIXGPIOIN_MASK);
	bus_space_barrier(sc->bst, sc->gpio, 0x04, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	observed = bus_space_read_4(sc->bst, sc->gpio, 0x04);
	mutex_exit(&sc->lock);
	if (observed != (direction | CIXGPIOIN_MASK)) {
		aprint_error_dev(self, "input direction readback failed\n");
		goto fail_lock;
	}
	sc->gc.gp_cookie = sc;
	sc->gc.gp_pin_read = cixgpioin_pin_read;
	sc->gc.gp_pin_write = cixgpioin_pin_write;
	sc->gc.gp_pin_ctl = cixgpioin_pin_ctl;
	for (i = 0; i < CIXGPIOIN_PINS; i++) {
		sc->pins[i].pin_num = i;
		sc->pins[i].pin_caps = GPIO_PIN_INPUT;
		sc->pins[i].pin_flags = GPIO_PIN_INPUT;
		sc->pins[i].pin_state = (input >> i) & 1;
		/* GPIOWRITE ignores caps; real parent ownership forces EBUSY. */
		sc->pins[i].pin_mapped = 1;
		strlcpy(sc->pins[i].pin_defname, names[i], GPIOMAXNAME);
	}
	gba.gba_gc = &sc->gc;
	gba.gba_pins = sc->pins;
	gba.gba_npins = CIXGPIOIN_PINS;
	if (config_found(self, &gba, gpiobus_print, CFARGS_NONE) == NULL) {
		aprint_error_dev(self, "could not attach input GPIO bus\n");
		goto fail_lock;
	}
	aprint_normal_dev(self, "three fixed inputs; no IRQ/PPS support\n");
	return;
fail_lock:
	mutex_destroy(&sc->lock);
fail_gpio:
	/* Never restore outputs after an attempted transition to inputs. */
	bus_space_unmap(sc->bst, sc->gpio, CIXGPIOIN_SIZE);
}

/* Built-in S0 service. Hot detach and suspend/resume are not qualified. */
CFATTACH_DECL_NEW(cixgpioin_acpi, sizeof(struct cixgpioin_softc),
    cixgpioin_acpi_match, cixgpioin_acpi_attach, NULL, NULL);
