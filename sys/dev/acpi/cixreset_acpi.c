/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded native CIX P1 reset provider, AI-assisted. */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/cixreset_acpi.h>

#define CIXRESET_BASE	0x16000000
#define CIXRESET_SIZE	0x1000

struct cixreset {
	bus_size_t cr_offset;
	uint32_t cr_mask;
	enum {
		CIXRESET_UNTOUCHED,
		CIXRESET_ASSERTED,
		CIXRESET_DEASSERTED,
		CIXRESET_UNCERTAIN
	} cr_state;
	bool cr_claimed;
};

struct cixreset_acpi_softc {
	device_t sc_dev;
	ACPI_HANDLE sc_handle;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	struct cixreset sc_resets[2];
};

struct cixreset_profile {
	unsigned int memory, end;
};

static const struct device_compatible_entry cixreset_compat[] = {
	{ .compat = "CIXHA020" }, DEVICE_COMPAT_EOL
};

/* One exact RST0 profile; publication and all register operations share a lock. */
static struct cixreset_acpi_softc *cixreset_provider;
static kmutex_t cixreset_lock;
static ONCE_DECL(cixreset_once);

static int
cixreset_init(void)
{

	mutex_init(&cixreset_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static int
cixreset_enter(void)
{
	int error;

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	error = RUN_ONCE(&cixreset_once, cixreset_init);
	if (error != 0)
		return error;
	mutex_enter(&cixreset_lock);
	return 0;
}

static ACPI_STATUS
cixreset_crs(ACPI_RESOURCE *res, void *arg)
{
	struct cixreset_profile *p = arg;
	const ACPI_RESOURCE_FIXED_MEMORY32 *mem;

	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		mem = &res->Data.FixedMemory32;
		if (p->end != 0 || ++p->memory != 1 ||
		    mem->WriteProtect != ACPI_READ_WRITE_MEMORY ||
		    mem->Address != CIXRESET_BASE ||
		    mem->AddressLength != CIXRESET_SIZE)
			return AE_BAD_DATA;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_END_TAG:
		return ++p->end == 1 && p->memory == 1 ? AE_OK : AE_BAD_DATA;
	default:
		return AE_BAD_DATA;
	}
}

static bool
cixreset_profile(ACPI_HANDLE handle)
{
	struct cixreset_profile p = { 0 };
	ACPI_INTEGER value;
	ACPI_STATUS rv;

	if (ACPI_FAILURE(acpi_eval_integer(handle, "_UID", &value)) || value != 0 ||
	    ACPI_FAILURE(acpi_eval_integer(handle, "_CCA", &value)) || value != 0)
		return false;
	/* ACPICA owns the decoded buffer and frees it after this walk. */
	rv = AcpiWalkResources(handle, "_CRS", cixreset_crs, &p);
	return ACPI_SUCCESS(rv) && p.memory == 1 && p.end == 1;
}

static int
cixreset_acpi_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct acpi_attach_args *aa = aux;
	int match;

	(void)parent;
	(void)cf;
	match = acpi_compatible_match(aa, cixreset_compat);
	if (match == 0 || !cixreset_profile(aa->aa_node->ad_handle))
		return 0;
	return match;
}

static void
cixreset_acpi_attach(device_t parent, device_t self, void *aux)
{
	struct cixreset_acpi_softc *sc = device_private(self);
	const struct acpi_attach_args *aa = aux;

	(void)parent;
	aprint_naive(": CIX P1 reset provider\n");
	aprint_normal(": CIX P1 bounded reset provider\n");
	/* Revalidate before mapping; attach never reads or changes a register. */
	if (acpi_compatible_match(aa, cixreset_compat) == 0 ||
	    !cixreset_profile(aa->aa_node->ad_handle)) {
		aprint_error_dev(self, "unsupported RST0 resource profile\n");
		return;
	}
	sc->sc_dev = self;
	sc->sc_handle = aa->aa_node->ad_handle;
	sc->sc_bst = aa->aa_memt;
	/* Hardware facts only: IDs, register offsets and active-low bit masks. */
	sc->sc_resets[0] = (struct cixreset){
		.cr_offset = 0x400, .cr_mask = __BIT(11)
	};
	sc->sc_resets[1] = (struct cixreset){
		.cr_offset = 0x804, .cr_mask = __BIT(19)
	};
	if (bus_space_map(sc->sc_bst, CIXRESET_BASE, CIXRESET_SIZE, 0,
	    &sc->sc_bsh) != 0) {
		aprint_error_dev(self, "cannot map reset registers\n");
		return;
	}
	if (cixreset_enter() != 0) {
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, CIXRESET_SIZE);
		aprint_error_dev(self, "cannot register reset provider\n");
		return;
	}
	if (cixreset_provider != NULL) {
		mutex_exit(&cixreset_lock);
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, CIXRESET_SIZE);
		aprint_error_dev(self, "RST0 provider already registered\n");
		return;
	}
	cixreset_provider = sc;
	mutex_exit(&cixreset_lock);
}

int
cixreset_acpi_get(ACPI_HANDLE provider, uint32_t id, struct cixreset **reset)
{
	struct cixreset *r;
	unsigned int slot;
	int error;

	if (provider == NULL || reset == NULL)
		return EINVAL;
	switch (id) {
	case 0x0e: slot = 0; break;
	case 0x8e: slot = 1; break;
	default: return EINVAL;
	}
	if ((error = cixreset_enter()) != 0)
		return error;
	if (cixreset_provider == NULL ||
	    cixreset_provider->sc_handle != provider) {
		error = ENXIO;
		goto out;
	}
	r = &cixreset_provider->sc_resets[slot];
	if (r->cr_claimed) {
		error = EBUSY;
		goto out;
	}
	r->cr_claimed = true;
	r->cr_state = CIXRESET_UNTOUCHED;
	*reset = r;
	error = 0;
out:
	mutex_exit(&cixreset_lock);
	return error;
}

/* Validate the opaque token by address before reading it. Lock must be held. */
static struct cixreset *
cixreset_claim(struct cixreset *r)
{
	unsigned int i;

	if (cixreset_provider == NULL)
		return NULL;
	for (i = 0; i < __arraycount(cixreset_provider->sc_resets); i++) {
		struct cixreset *candidate = &cixreset_provider->sc_resets[i];

		if (r == candidate && candidate->cr_claimed)
			return candidate;
	}
	return NULL;
}

int
cixreset_acpi_get_state(struct cixreset *reset, bool *asserted)
{
	struct cixreset_acpi_softc *sc;
	struct cixreset *r;
	uint32_t value;
	int error;

	if (reset == NULL || asserted == NULL)
		return EINVAL;
	if ((error = cixreset_enter()) != 0)
		return error;
	r = cixreset_claim(reset);
	if (r == NULL) {
		error = EINVAL;
		goto out;
	}
	sc = cixreset_provider;
	value = bus_space_read_4(sc->sc_bst, sc->sc_bsh, r->cr_offset);
	*asserted = (value & r->cr_mask) == 0;
	error = 0;
out:
	mutex_exit(&cixreset_lock);
	return error;
}

static int
cixreset_set(struct cixreset *reset, bool asserted)
{
	struct cixreset_acpi_softc *sc;
	struct cixreset *r;
	uint32_t value, observed;
	int error;

	if ((error = cixreset_enter()) != 0)
		return error;
	r = cixreset_claim(reset);
	if (r == NULL) {
		error = EINVAL;
		goto out;
	}
	sc = cixreset_provider;
	value = bus_space_read_4(sc->sc_bst, sc->sc_bsh, r->cr_offset);
	if (asserted)
		value &= ~r->cr_mask;
	else
		value |= r->cr_mask;
	/* Ownership remains held even if the write or readback cannot confirm it. */
	r->cr_state = CIXRESET_UNCERTAIN;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, r->cr_offset, value);
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, r->cr_offset, sizeof(value),
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	observed = bus_space_read_4(sc->sc_bst, sc->sc_bsh, r->cr_offset);
	if ((observed & r->cr_mask) != (value & r->cr_mask)) {
		aprint_error_dev(sc->sc_dev,
		    "reset register %#lx bit %#x readback failed\n",
		    (unsigned long)r->cr_offset, r->cr_mask);
		error = EIO;
		goto out;
	}
	r->cr_state = asserted ? CIXRESET_ASSERTED : CIXRESET_DEASSERTED;
	error = 0;
out:
	mutex_exit(&cixreset_lock);
	return error;
}

int
cixreset_acpi_assert(struct cixreset *reset)
{

	return cixreset_set(reset, true);
}

int
cixreset_acpi_deassert(struct cixreset *reset)
{

	return cixreset_set(reset, false);
}

int
cixreset_acpi_put(struct cixreset *reset)
{
	struct cixreset *r;
	int error;

	if ((error = cixreset_enter()) != 0)
		return error;
	r = cixreset_claim(reset);
	if (r == NULL)
		error = EINVAL;
	else if (r->cr_state != CIXRESET_UNTOUCHED &&
	    r->cr_state != CIXRESET_ASSERTED)
		error = EBUSY;
	else {
		r->cr_claimed = false;
		error = 0;
	}
	mutex_exit(&cixreset_lock);
	return error;
}

CFATTACH_DECL_NEW(cixreset_acpi, sizeof(struct cixreset_acpi_softc),
    cixreset_acpi_match, cixreset_acpi_attach, NULL, NULL);
