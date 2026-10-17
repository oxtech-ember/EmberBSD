/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original checked CIX ACPI clock provider, AI-assisted. */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/intr.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/cixclk_acpi.h>

struct cixclk_acpi_softc;
struct cixclk {
	struct cixclk_acpi_softc *provider;
	bool claimed;
	enum { CIXCLK_IDLE, CIXCLK_ENABLED, CIXCLK_UNCERTAIN } state;
};

struct cixclk_acpi_softc {
	device_t sc_dev;
	ACPI_HANDLE sc_handle;
	kmutex_t sc_lock;
	struct cixclk sc_clock;
};

/* UID 0 is the only supported provider. Autoconfiguration publishes it once. */
static struct cixclk_acpi_softc *cixclk_provider;
static const struct device_compatible_entry cixclk_compat[] = {
	{ .compat = "CIXHA010" }, DEVICE_COMPAT_EOL
};

static int
cixclk_acpi_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct acpi_attach_args *aa = aux;
	ACPI_INTEGER uid;
	int match;

	(void)parent;
	(void)cf;
	match = acpi_compatible_match(aa, cixclk_compat);
	if (match == 0 || ACPI_FAILURE(acpi_eval_integer(
	    aa->aa_node->ad_handle, "_UID", &uid)) || uid != 0)
		return 0;
	return match;
}

static void
cixclk_acpi_attach(device_t parent, device_t self, void *aux)
{
	struct cixclk_acpi_softc *sc = device_private(self);
	const struct acpi_attach_args *aa = aux;

	(void)parent;
	aprint_naive("\n");
	aprint_normal(": CIX ACPI clock provider\n");
	if (cixclk_provider != NULL) {
		aprint_error_dev(self, "duplicate provider is not supported\n");
		return;
	}
	sc->sc_dev = self;
	sc->sc_handle = aa->aa_node->ad_handle;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->sc_clock.provider = sc;
	sc->sc_clock.claimed = false;
	sc->sc_clock.state = CIXCLK_IDLE;
	cixclk_provider = sc;
	/* No GCLK or CLKC evaluation is permitted during attachment. */
}

CFATTACH_DECL_NEW(cixclk_acpi, sizeof(struct cixclk_acpi_softc),
    cixclk_acpi_match, cixclk_acpi_attach, NULL, NULL);

static bool
cixclk_interrupt(void)
{

	return cpu_intr_p() || cpu_softintr_p();
}

int
cixclk_acpi_get(ACPI_HANDLE provider, uint32_t id, struct cixclk **out)
{
	struct cixclk_acpi_softc *sc = cixclk_provider;
	int error = 0;

	if (out == NULL || provider == NULL)
		return EINVAL;
	if (cixclk_interrupt())
		return EWOULDBLOCK;
	if (sc == NULL || sc->sc_handle != provider)
		return ENXIO;
	if (id != 0x43)
		return ENOTSUP;
	mutex_enter(&sc->sc_lock);
	if (sc->sc_clock.claimed)
		error = EBUSY;
	else {
		sc->sc_clock.claimed = true;
		*out = &sc->sc_clock;
	}
	mutex_exit(&sc->sc_lock);
	return error;
}

static uint32_t
cixclk_le32(const uint8_t *p)
{

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Caller holds the sleep mutex across AML, including mailbox waits. */
static int
cixclk_evaluate(struct cixclk_acpi_softc *sc, bool rate, bool enable,
    uint64_t *value)
{
	ACPI_OBJECT args[2] = { { .Type = ACPI_TYPE_INTEGER },
	    { .Type = ACPI_TYPE_INTEGER } };
	ACPI_OBJECT_LIST list = { rate ? 1 : 2, args };
	ACPI_BUFFER result = { ACPI_ALLOCATE_BUFFER, NULL };
	const ACPI_OBJECT *obj;
	const uint8_t *bytes;
	uintptr_t base, payload;
	ACPI_STATUS status;
	const char *method = rate ? "GCLK" : "CLKC";
	uint32_t clock_status;
	int error = EIO;

	args[0].Integer.Value = 0x43;
	args[1].Integer.Value = enable ? 1 : 0;
	status = AcpiEvaluateObject(sc->sc_handle, method,
	    &list, &result);
	if (ACPI_FAILURE(status)) {
		aprint_error_dev(sc->sc_dev, "%s evaluation failed: %s\n",
		    method, AcpiFormatException(status));
		goto out;
	}
	if (result.Pointer == NULL || result.Length < sizeof(*obj))
		goto bad_response;
	obj = result.Pointer;
	if (obj->Type != ACPI_TYPE_BUFFER || obj->Buffer.Pointer == NULL ||
	    obj->Buffer.Length < 4)
		goto bad_response;
	/* ACPICA places the returned object and payload in one allocation. */
	base = (uintptr_t)result.Pointer;
	payload = (uintptr_t)obj->Buffer.Pointer;
	if (payload < base || payload - base < sizeof(*obj) ||
	    payload - base > result.Length ||
	    obj->Buffer.Length > result.Length - (payload - base))
		goto bad_response;
	bytes = obj->Buffer.Pointer;
	clock_status = cixclk_le32(bytes);
	if (clock_status != 0) {
		aprint_error_dev(sc->sc_dev, "%s returned clock status %u\n",
		    method, (unsigned int)clock_status);
		goto out;
	}
	if (rate && obj->Buffer.Length < 12)
		goto bad_response;
	if (rate)
		*value = (uint64_t)cixclk_le32(bytes + 4) |
		    (uint64_t)cixclk_le32(bytes + 8) << 32;
	error = 0;
	goto out;
bad_response:
	aprint_error_dev(sc->sc_dev, "%s returned an invalid clock response\n",
	    method);
out:
	if (result.Pointer != NULL)
		AcpiOsFree(result.Pointer);
	return error;
}

static int
cixclk_control(struct cixclk *clk, bool enable)
{
	struct cixclk_acpi_softc *sc;
	int error;

	if (clk == NULL)
		return EINVAL;
	if (cixclk_interrupt())
		return EWOULDBLOCK;
	sc = clk->provider;
	mutex_enter(&sc->sc_lock);
	if (!clk->claimed)
		error = EINVAL;
	else if (enable && clk->state == CIXCLK_UNCERTAIN)
		error = EIO;
	else if ((enable && clk->state == CIXCLK_ENABLED) ||
	    (!enable && clk->state == CIXCLK_IDLE))
		error = EALREADY;
	else {
		/* An AML error can follow a hardware change. Keep ownership. */
		clk->state = CIXCLK_UNCERTAIN;
		error = cixclk_evaluate(sc, false, enable, NULL);
		if (error == 0)
			clk->state = enable ? CIXCLK_ENABLED : CIXCLK_IDLE;
	}
	mutex_exit(&sc->sc_lock);
	return error;
}

int
cixclk_acpi_enable(struct cixclk *clk)
{

	return cixclk_control(clk, true);
}

int
cixclk_acpi_disable(struct cixclk *clk)
{

	return cixclk_control(clk, false);
}

int
cixclk_acpi_get_rate(struct cixclk *clk, uint64_t *rate)
{
	struct cixclk_acpi_softc *sc;
	int error;

	if (clk == NULL || rate == NULL)
		return EINVAL;
	if (cixclk_interrupt())
		return EWOULDBLOCK;
	sc = clk->provider;
	mutex_enter(&sc->sc_lock);
	error = clk->claimed ? cixclk_evaluate(sc, true, false, rate) : EINVAL;
	mutex_exit(&sc->sc_lock);
	return error;
}

int
cixclk_acpi_put(struct cixclk *clk)
{
	struct cixclk_acpi_softc *sc;
	int error = 0;

	if (clk == NULL)
		return EINVAL;
	if (cixclk_interrupt())
		return EWOULDBLOCK;
	sc = clk->provider;
	mutex_enter(&sc->sc_lock);
	if (!clk->claimed)
		error = EINVAL;
	else if (clk->state != CIXCLK_IDLE)
		error = EBUSY;
	else
		clk->claimed = false;
	mutex_exit(&sc->sc_lock);
	return error;
}
