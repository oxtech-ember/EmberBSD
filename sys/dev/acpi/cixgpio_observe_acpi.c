/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded CIX P1 GPIO observation, AI-assisted. */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>

#define CIXGPIOOBS_GPIO_BASE	0x16005000
#define CIXGPIOOBS_MUX_BASE	0x16007000
#define CIXGPIOOBS_SIZE		0x1000
#define CIXGPIOOBS_ENABLE	1

struct cixgpioobs_resources {
	bool mux;
	unsigned int memory, irq, groups, end;
};

static const struct device_compatible_entry cixgpioobs_compat[] = {
	{ .compat = "CIXH1003" }, DEVICE_COMPAT_EOL
};

/*
 * CIX S5 pads0..65 use big-endian vendor offset/config pairs.
 * Validate metadata only. Never apply a PinGroup or its vendor settings.
 * The 32-group/pin limits bound this observation, not the general pinctrl ABI.
 */
static bool
cixgpioobs_group(const ACPI_RESOURCE_PIN_GROUP *group)
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
cixgpioobs_crs(ACPI_RESOURCE *res, void *arg)
{
	struct cixgpioobs_resources *p = arg;
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
		    memory->Address != (p->mux ? CIXGPIOOBS_MUX_BASE :
		    CIXGPIOOBS_GPIO_BASE) || memory->AddressLength != CIXGPIOOBS_SIZE)
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
		    !cixgpioobs_group(&res->Data.PinGroup))
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
cixgpioobs_identity(ACPI_HANDLE handle, const char *expected, ACPI_INTEGER uid)
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
cixgpioobs_profile(ACPI_HANDLE gpio, ACPI_HANDLE *mux)
{
	struct cixgpioobs_resources gp = { 0 }, mp = { .mux = true };
	ACPI_HANDLE expected;
	ACPI_INTEGER value;

	if (ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB.GPI5", &expected)) ||
	    expected != gpio || !cixgpioobs_identity(gpio, "CIXH1003", 5) ||
	    ACPI_FAILURE(acpi_dsd_integer(gpio, "id", &value)) || value != 1 ||
	    ACPI_FAILURE(acpi_dsd_integer(gpio, "ngpios", &value)) || value != 10 ||
	    ACPI_FAILURE(acpi_dsd_integer(gpio, "gpio-io-mask", &value)) ||
	    value != 0 ||
	    ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB.MUX1", mux)) ||
	    !cixgpioobs_identity(*mux, "CIXHA017", 1))
		return false;
	/* ACPICA owns the parsed buffers. Their pointer bounds remain trusted. */
	if (ACPI_FAILURE(AcpiWalkResources(gpio, "_CRS", cixgpioobs_crs, &gp)) ||
	    gp.memory != 1 || gp.irq != 1 || gp.groups != 0 || gp.end != 1)
		return false;
	return ACPI_SUCCESS(AcpiWalkResources(*mux, "_CRS", cixgpioobs_crs, &mp)) &&
	    mp.memory == 1 && mp.irq == 0 && mp.groups != 0 && mp.end == 1;
}

static int
cixgpioobs_acpi_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct acpi_attach_args *aa = aux;
	ACPI_HANDLE expected;
	int match;

	(void)parent;
	if (cf->cf_flags != CIXGPIOOBS_ENABLE)
		return 0;
	match = acpi_compatible_match(aa, cixgpioobs_compat);
	if (match == 0 ||
	    ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB.GPI5", &expected)) ||
	    expected != aa->aa_node->ad_handle)
		return 0;
	return match;
}

static void
cixgpioobs_acpi_attach(device_t parent, device_t self, void *aux)
{
	const struct acpi_attach_args *aa = aux;
	bus_space_handle_t gpio, mux;
	ACPI_HANDLE mux_handle;
	uint32_t mux80, mux84, mux88, bypass, direction, input;

	(void)parent;
	aprint_naive(": CIX GPIO observation\n");
	aprint_normal(": CIX GPI5/header mux observation\n");
	if (device_cfdata(self)->cf_flags != CIXGPIOOBS_ENABLE ||
	    acpi_compatible_match(aa, cixgpioobs_compat) == 0 ||
	    !cixgpioobs_profile(aa->aa_node->ad_handle, &mux_handle)) {
		aprint_error_dev(self, "unsupported GPI5/MUX1 observation profile\n");
		return;
	}
	if (bus_space_map(aa->aa_memt, CIXGPIOOBS_GPIO_BASE,
	    CIXGPIOOBS_SIZE, 0, &gpio) != 0) {
		aprint_error_dev(self, "could not map validated GPI5 resource\n");
		return;
	}
	if (bus_space_map(aa->aa_memt, CIXGPIOOBS_MUX_BASE,
	    CIXGPIOOBS_SIZE, 0, &mux) != 0) {
		bus_space_unmap(aa->aa_memt, gpio, CIXGPIOOBS_SIZE);
		aprint_error_dev(self, "could not map validated MUX1 resource\n");
		return;
	}
	/* Six nonclearing reads. Never read STATUS or touch IRQ/configuration. */
	mux80 = bus_space_read_4(aa->aa_memt, mux, 0x80);
	mux84 = bus_space_read_4(aa->aa_memt, mux, 0x84);
	mux88 = bus_space_read_4(aa->aa_memt, mux, 0x88);
	bypass = bus_space_read_4(aa->aa_memt, gpio, 0x00);
	direction = bus_space_read_4(aa->aa_memt, gpio, 0x04);
	input = bus_space_read_4(aa->aa_memt, gpio, 0x10);
	bus_space_unmap(aa->aa_memt, mux, CIXGPIOOBS_SIZE);
	bus_space_unmap(aa->aa_memt, gpio, CIXGPIOOBS_SIZE);
	aprint_normal_dev(self, "mux80=%#x mux84=%#x mux88=%#x\n",
	    mux80, mux84, mux88);
	aprint_normal_dev(self, "bypass=%#x direction=%#x input=%#x\n",
	    bypass, direction, input);
	aprint_normal_dev(self, "read-only snapshot; no GPIO/PPS qualification\n");
}

CFATTACH_DECL_NEW(cixgpioobs_acpi, 0,
    cixgpioobs_acpi_match, cixgpioobs_acpi_attach, NULL, NULL);
