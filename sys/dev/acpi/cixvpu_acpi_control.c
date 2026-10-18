/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded native VPU resource probe, AI-assisted. */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpi_power.h>
#include <dev/acpi/cixclk_acpi.h>
#include <dev/acpi/cixreset_acpi.h>
#include <dev/acpi/cixvpu_acpi_control.h>
#include <dev/ic/cixvpu_resources.h>

struct cixvpu_object {
	ACPI_BUFFER buffer;
	const ACPI_OBJECT *root;
};

/* Subtract addresses first. Never form an unchecked allocation end pointer. */
static bool
cixvpu_range(const ACPI_BUFFER *b, const void *p, size_t n)
{
	uintptr_t base = (uintptr_t)b->Pointer;
	uintptr_t address = (uintptr_t)p;

	return p != NULL && address >= base && address - base <= b->Length &&
	    n <= b->Length - (address - base);
}

static const ACPI_OBJECT *
cixvpu_package(const struct cixvpu_object *o, const ACPI_OBJECT *p,
    unsigned int count)
{
	const ACPI_OBJECT *elements;

	if (p->Type != ACPI_TYPE_PACKAGE || p->Package.Count != count)
		return NULL;
	elements = p->Package.Elements;
	if ((uintptr_t)elements % __alignof__(ACPI_OBJECT) != 0 ||
	    !cixvpu_range(&o->buffer, elements, count * sizeof(*elements)))
		return NULL;
	return elements;
}

static bool
cixvpu_string(const struct cixvpu_object *o, const ACPI_OBJECT *p,
    const char *expected)
{
	size_t length = strlen(expected);

	return p->Type == ACPI_TYPE_STRING && p->String.Length == length &&
	    cixvpu_range(&o->buffer, p->String.Pointer, length + 1) &&
	    memcmp(p->String.Pointer, expected, length + 1) == 0;
}

static bool
cixvpu_reference(const ACPI_OBJECT *p, ACPI_HANDLE expected)
{

	return p->Type == ACPI_TYPE_LOCAL_REFERENCE &&
	    p->Reference.Handle != NULL && p->Reference.Handle == expected;
}

static int
cixvpu_evaluate(struct cixvpu_acpi_control *c, const char *name,
    struct cixvpu_object *o)
{
	ACPI_HANDLE table;
	ACPI_OBJECT_TYPE type;
	ACPI_STATUS rv;

	o->buffer = (ACPI_BUFFER){ ACPI_ALLOCATE_BUFFER, NULL };
	o->root = NULL;
	/* Reject a method before evaluating a table that must be static data. */
	rv = AcpiGetHandle(c->consumer, name, &table);
	if (ACPI_SUCCESS(rv))
		rv = AcpiGetType(table, &type);
	if (ACPI_FAILURE(rv)) {
		c->report.acpi_status = rv;
		return EIO;
	}
	if (type != ACPI_TYPE_PACKAGE)
		return EINVAL;
	rv = AcpiEvaluateObject(c->consumer, name, NULL, &o->buffer);
	if (ACPI_FAILURE(rv)) {
		c->report.acpi_status = rv;
		return EIO;
	}
	if ((uintptr_t)o->buffer.Pointer % __alignof__(ACPI_OBJECT) != 0 ||
	    !cixvpu_range(&o->buffer, o->buffer.Pointer, sizeof(ACPI_OBJECT)))
		return EINVAL;
	o->root = o->buffer.Pointer;
	return 0;
}

static int
cixvpu_clock_table(struct cixvpu_acpi_control *c)
{
	struct cixvpu_object o;
	const ACPI_OBJECT *entries, *p;
	int error;

	error = cixvpu_evaluate(c, "CLKT", &o);
	if (error != 0)
		goto out;
	entries = cixvpu_package(&o, o.root, 1);
	if (entries == NULL || (p = cixvpu_package(&o, entries, 3)) == NULL ||
	    p[0].Type != ACPI_TYPE_INTEGER || p[0].Integer.Value != 0x43 ||
	    !cixvpu_string(&o, &p[1], "vpu_clk") ||
	    !cixvpu_reference(&p[2], c->consumer))
		error = EINVAL;
out:
	AcpiOsFree(o.buffer.Pointer);
	return error;
}

static int
cixvpu_reset_table(struct cixvpu_acpi_control *c, ACPI_HANDLE *provider)
{
	static const uint32_t ids[] = { 0x0e, 0x8e };
	static const char * const names[] = { "vpu_reset", "vpu_rcsu_reset" };
	struct cixvpu_object o;
	const ACPI_OBJECT *entries, *p;
	ACPI_HANDLE handle = NULL;
	unsigned int i;
	int error;

	error = cixvpu_evaluate(c, "RSTL", &o);
	if (error != 0)
		goto out;
	entries = cixvpu_package(&o, o.root, 2);
	if (entries == NULL) {
		error = EINVAL;
		goto out;
	}
	for (i = 0; i < 2; i++) {
		p = cixvpu_package(&o, &entries[i], 4);
		if (p == NULL || p[0].Type != ACPI_TYPE_LOCAL_REFERENCE ||
		    p[0].Reference.Handle == NULL) {
			error = EINVAL;
			goto out;
		}
		if (i == 0)
			handle = p[0].Reference.Handle;
		if (!cixvpu_reference(&p[0], handle) ||
		    p[1].Type != ACPI_TYPE_INTEGER || p[1].Integer.Value != ids[i] ||
		    !cixvpu_reference(&p[2], c->consumer) ||
		    !cixvpu_string(&o, &p[3], names[i])) {
			error = EINVAL;
			goto out;
		}
	}
	*provider = handle;
out:
	AcpiOsFree(o.buffer.Pointer);
	return error;
}

static int
cixvpu_power_table(struct cixvpu_acpi_control *c, const char *name,
    ACPI_HANDLE power)
{
	struct cixvpu_object o;
	const ACPI_OBJECT *p;
	int error;

	error = cixvpu_evaluate(c, name, &o);
	if (error != 0)
		goto out;
	p = cixvpu_package(&o, o.root, 1);
	if (p == NULL || !cixvpu_reference(p, power))
		error = EINVAL;
out:
	AcpiOsFree(o.buffer.Pointer);
	return error;
}

static int
cixvpu_provider(struct cixvpu_acpi_control *c, ACPI_HANDLE handle,
    const char *hid)
{
	ACPI_DEVICE_INFO *info = NULL;
	ACPI_BUFFER buffer;
	ACPI_INTEGER uid;
	ACPI_STATUS rv;
	size_t length = strlen(hid) + 1;
	int error = EINVAL;

	rv = AcpiGetObjectInfo(handle, &info);
	if (ACPI_FAILURE(rv)) {
		c->report.acpi_status = rv;
		error = EIO;
		goto out;
	}
	if (info == NULL || info->InfoSize < sizeof(*info))
		goto out;
	buffer = (ACPI_BUFFER){ info->InfoSize, info };
	if (info->Type != ACPI_TYPE_DEVICE || !(info->Valid & ACPI_VALID_HID) ||
	    info->HardwareId.Length != length ||
	    !cixvpu_range(&buffer, info->HardwareId.String, length) ||
	    memcmp(info->HardwareId.String, hid, length) != 0)
		goto out;
	rv = acpi_eval_integer(handle, "_UID", &uid);
	if (ACPI_FAILURE(rv)) {
		c->report.acpi_status = rv;
		error = EIO;
		goto out;
	}
	error = uid == 0 ? 0 : EINVAL;
out:
	AcpiOsFree(info);
	return error;
}

static int
cixvpu_profile(struct cixvpu_acpi_control *c, ACPI_HANDLE *clock,
    ACPI_HANDLE *reset)
{
	ACPI_OBJECT_TYPE type;
	ACPI_STATUS rv;
	int error;

	if ((error = cixvpu_clock_table(c)) != 0 ||
	    (error = cixvpu_reset_table(c, reset)) != 0)
		return error;
	rv = AcpiGetHandle(NULL, "\\_SB.CCLK", clock);
	if (ACPI_FAILURE(rv))
		goto acpi_error;
	if ((error = cixvpu_provider(c, *clock, "CIXHA010")) != 0 ||
	    (error = cixvpu_provider(c, *reset, "CIXHA020")) != 0)
		return error;
	rv = AcpiGetHandle(c->consumer, "PPRS", &c->power);
	if (ACPI_FAILURE(rv))
		goto acpi_error;
	if ((error = cixvpu_power_table(c, "_PR0", c->power)) != 0 ||
	    (error = cixvpu_power_table(c, "_PR3", c->power)) != 0)
		return error;
	rv = AcpiGetType(c->power, &type);
	if (ACPI_FAILURE(rv))
		goto acpi_error;
	return type == ACPI_TYPE_POWER ? 0 : EINVAL;
acpi_error:
	c->report.acpi_status = rv;
	return EIO;
}

static int
cixvpu_power_status(struct cixvpu_acpi_control *c, bool on, bool cleanup)
{
	ACPI_INTEGER value;
	ACPI_STATUS rv;

	rv = acpi_eval_integer(c->power, "_STA", &value);
	if (ACPI_SUCCESS(rv) && value != (on ? 1 : 0))
		rv = AE_BAD_DATA;
	if (cleanup)
		c->report.cleanup_acpi_status = rv;
	else
		c->report.acpi_status = rv;
	return ACPI_SUCCESS(rv) ? 0 : EIO;
}

/* Resolve cached identity only. B independently validates service ownership. */
static int
cixvpu_native_scmi(const struct acpi_attach_args *aa, ACPI_HANDLE *out)
{
	const struct acpi_softc *acpi;
	struct acpi_devnode *node;
	ACPI_HANDLE match = NULL;
	bool consumer_found = false;

	if (aa->aa_node->ad_root == NULL ||
	    aa->aa_node->ad_type != ACPI_TYPE_DEVICE)
		return EINVAL;
	acpi = device_private(aa->aa_node->ad_root);
	if (acpi == NULL)
		return ENXIO;
	SIMPLEQ_FOREACH(node, &acpi->sc_head, ad_list) {
		ACPI_DEVICE_INFO *info = node->ad_devinfo;
		ACPI_BUFFER b;

		if (node->ad_root != aa->aa_node->ad_root)
			continue;
		if (node == aa->aa_node)
			consumer_found = true;
		if (node->ad_type != ACPI_TYPE_DEVICE || node->ad_handle == NULL ||
		    info == NULL || info->InfoSize < sizeof(*info) ||
		    info->InfoSize > UINTPTR_MAX - (uintptr_t)info ||
		    info->Type != ACPI_TYPE_DEVICE ||
		    (info->Valid & (ACPI_VALID_HID | ACPI_VALID_UID)) !=
		    (ACPI_VALID_HID | ACPI_VALID_UID))
			continue;
		b = (ACPI_BUFFER){ info->InfoSize, info };
		/* Integer UID Length is allocation capacity, including unused tail. */
		if (info->HardwareId.Length != sizeof("CIXHA006") ||
		    info->UniqueId.Length < sizeof("0") ||
		    !cixvpu_range(&b, info->HardwareId.String, sizeof("CIXHA006")) ||
		    !cixvpu_range(&b, info->UniqueId.String, info->UniqueId.Length) ||
		    memcmp(info->HardwareId.String, "CIXHA006", sizeof("CIXHA006")) != 0 ||
		    memcmp(info->UniqueId.String, "0", sizeof("0")) != 0)
			continue;
		if (match != NULL)
			return EINVAL;
		match = node->ad_handle;
	}
	if (!consumer_found || match == NULL)
		return ENXIO;
	*out = match;
	return 0;
}

int
cixvpu_acpi_control_cleanup(struct cixvpu_acpi_control *c)
{
	ACPI_STATUS rv;
	int error;

	if (c == NULL || !c->started)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	/* No DMA has ever started. This order is not a DMA detach sequence. */
	if (c->mapped) {
		bus_space_unmap(c->bst, c->bsh, c->span);
		c->mapped = false;
	}
	if (c->power_released_fault) {
		error = EIO;
		goto out;
	}
	if ((c->power_attempted || c->clock_attempted) && !c->reset_safe) {
		error = cixreset_acpi_assert(c->reset);
		if (error != 0)
			goto out;
		c->reset_safe = true;
	}
	if (c->clock_attempted) {
		error = cixclk_acpi_disable(c->clock);
		if (error != 0)
			goto out;
		c->clock_attempted = false;
	}
	if (c->power_attempted) {
		rv = acpi_power_res(c->power, c->consumer, false);
		c->report.cleanup_acpi_status = rv;
		if (ACPI_FAILURE(rv)) {
			error = EIO;
			goto out;
		}
		/* The native lease is gone even if _STA remains invalid or on. */
		c->power_attempted = false;
		error = cixvpu_power_status(c, false, true);
		if (error != 0) {
			c->power_released_fault = true;
			goto out;
		}
	}
	if (c->rcsu != NULL) {
		error = cixreset_acpi_put(c->rcsu);
		if (error != 0)
			goto out;
		c->rcsu = NULL;
	}
	if (c->reset != NULL) {
		error = cixreset_acpi_put(c->reset);
		if (error != 0)
			goto out;
		c->reset = NULL;
	}
	if (c->clock != NULL) {
		error = cixclk_acpi_put(c->clock);
		if (error != 0)
			goto out;
		c->clock = NULL;
	}
	c->report.released = true;
	error = 0;
out:
	c->report.cleanup_error = error;
	if (error != 0)
		aprint_error_dev(c->dev, "VPU probe cleanup failed: %d\n", error);
	return error;
}

static bool
cixvpu_identity(const struct cixvpu_acpi_report *r)
{

	if (r->ncores == 0 || r->ncores > 4 || r->nlsid == 0 || r->nlsid > 4)
		return false;
	/* Vendor register identities and maintained core/slot array limits only. */
	switch (r->id >> 16) {
	case 0x5650:
	case 0x5655:
	case 0x5660:
	case 0x5661:
	case 0x5662:
	case 0x5663:
	case 0x5664:
		return true;
	default:
		return false;
	}
}

static int
cixvpu_probe(struct cixvpu_acpi_control *c, device_t dev,
    const struct acpi_attach_args *aa, const struct cixvpu_resources *resources,
    bool activate, bool native)
{
	ACPI_HANDLE clock, reset, scmi = NULL;
	ACPI_STATUS rv;
	bool asserted;
	int error, cleanup_error;

	if (c == NULL || aa == NULL || aa->aa_node == NULL ||
	    aa->aa_node->ad_handle == NULL || resources == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (c->started)
		return EALREADY;
	c->started = true;
	c->dev = dev;
	c->consumer = aa->aa_node->ad_handle;
	c->bst = aa->aa_memt;
	if (cixvpu_resources_validate(resources) != CIXVPU_RESOURCE_OK) {
		error = EINVAL;
		goto out;
	}
	if (native && (error = cixvpu_native_scmi(aa, &scmi)) != 0)
		goto out;
	if ((error = cixvpu_profile(c, &clock, &reset)) != 0)
		goto out;
	error = native ? cixclk_acpi_get_native(clock, scmi, 0x43, &c->clock) :
	    cixclk_acpi_get(clock, 0x43, &c->clock);
	if (error != 0 ||
	    (error = cixreset_acpi_get(reset, 0x0e, &c->reset)) != 0 ||
	    (error = cixreset_acpi_get(reset, 0x8e, &c->rcsu)) != 0)
		goto out;
	if (!activate) {
		c->report.rate_query_attempted = true;
		error = cixclk_acpi_get_rate(c->clock, &c->report.rate);
		c->report.rate_valid = error == 0;
		goto out;
	}
	/* The vendor VPU sequence pulses only main reset. Do not invent RCSU control. */
	if ((error = cixreset_acpi_get_state(c->rcsu, &asserted)) != 0)
		goto out;
	if (asserted) {
		error = EBUSY;
		goto out;
	}
	/* ON can fail before lease insertion or after effects. Attempt rollback. */
	c->power_attempted = true;
	rv = acpi_power_res(c->power, c->consumer, true);
	c->report.acpi_status = rv;
	if (ACPI_FAILURE(rv)) {
		error = EIO;
		goto out;
	}
	if ((error = cixvpu_power_status(c, true, false)) != 0)
		goto out;
	c->clock_attempted = true;
	/* Runtime resume requires clock enable, not devfreq rate telemetry. */
	if ((error = cixclk_acpi_enable(c->clock)) != 0)
		goto out;
	if ((error = cixreset_acpi_assert(c->reset)) != 0)
		goto out;
	delay(20);
	if ((error = cixreset_acpi_deassert(c->reset)) != 0)
		goto out;
	delay(20);
	c->span = resources->mem[1].length;
	error = bus_space_map(c->bst, resources->mem[1].translated, c->span, 0,
	    &c->bsh);
	if (error != 0)
		goto out;
	c->mapped = true;
	c->report.id = bus_space_read_4(c->bst, c->bsh, 0x00);
	c->report.ncores = bus_space_read_4(c->bst, c->bsh, 0x08);
	c->report.nlsid = bus_space_read_4(c->bst, c->bsh, 0x0c);
	c->report.svn = bus_space_read_4(c->bst, c->bsh, 0x30);
	c->report.identity_valid = cixvpu_identity(&c->report);
	error = c->report.identity_valid ? 0 : ENODEV;
out:
	c->report.error = error;
	cleanup_error = cixvpu_acpi_control_cleanup(c);
	if (error != 0)
		aprint_error_dev(c->dev, "VPU resource probe failed: %d\n", error);
	return error != 0 ? error : cleanup_error;
}

int
cixvpu_acpi_control_probe(struct cixvpu_acpi_control *c, device_t dev,
    const struct acpi_attach_args *aa, const struct cixvpu_resources *resources,
    bool activate)
{

	return cixvpu_probe(c, dev, aa, resources, activate, false);
}

int
cixvpu_acpi_control_probe_native(struct cixvpu_acpi_control *c, device_t dev,
    const struct acpi_attach_args *aa, const struct cixvpu_resources *resources)
{

	return cixvpu_probe(c, dev, aa, resources, true, true);
}
