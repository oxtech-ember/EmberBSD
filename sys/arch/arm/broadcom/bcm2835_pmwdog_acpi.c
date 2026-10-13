/* $NetBSD$ */

/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Modelled on bcm2835_mbox_acpi.c.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * The BCM2712 power management, reset and watchdog block described by
 * ACPI, as our build of the eotics-com rpi5-uefi firmware hands it over
 * (_HID PRP0001, _DSD compatible "brcm,bcm2712-pm").  The registers are
 * those of the BCM2835; bcm2835_pmwdog.c drives them.
 *
 * The same firmware copies the power source the Raspberry Pi boot
 * loader reports into \_SB.PSMC, PSRR, PSUH and PSOC (eotics names),
 * and into \_SB.BLRS the reset status register as the boot loader
 * found it: the register itself reads 0 by the time we run.  Together
 * they tell why the board last started.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include "opt_bcmv3d.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>

#include <arm/broadcom/bcm2835_pmwdogvar.h>

/* A cell the boot loader did not report. */
#define	PM_ACPI_UNREPORTED	0xffffffff

/* Origin: EmberBSD BCM2712 PM observation and opt-in reset owner, 2026-10-09. */
#define BCM2712_PM_GRAFX	0x304
static struct bcm2835pmwdog_softc *bcmpmwdog_v3d_sc;
#ifdef BCM2712_V3D_TAKEOVER
static void bcmpmwdog_v3d_register(struct bcm2835pmwdog_softc *,
    bus_addr_t, bus_size_t);
#endif

/*
 * Why the PMIC was reset, bit by bit, as the Raspberry Pi configuration
 * reference documents power_reset in /chosen/power.
 */
static const char * const bcmpmwdog_acpi_pmic_reasons[] = {
	"over-voltage", "under-voltage", "over-temperature",
	"enable signal", "watchdog",
};

static int	bcmpmwdog_acpi_match(device_t, cfdata_t, void *);
static void	bcmpmwdog_acpi_attach(device_t, device_t, void *);
static void	bcmpmwdog_acpi_power_source(device_t);

CFATTACH_DECL_NEW(bcmpmwdog_acpi, sizeof(struct bcm2835pmwdog_softc),
    bcmpmwdog_acpi_match, bcmpmwdog_acpi_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "brcm,bcm2712-pm" },
	DEVICE_COMPAT_EOL
};

static int
bcmpmwdog_acpi_match(device_t parent, cfdata_t cf, void *aux)
{
	struct acpi_attach_args *aa = aux;

	return acpi_compatible_match(aa, compat_data);
}

static void
bcmpmwdog_acpi_attach(device_t parent, device_t self, void *aux)
{
	struct bcm2835pmwdog_softc * const sc = device_private(self);
	struct acpi_attach_args *aa = aux;
	struct acpi_resources res;
	struct acpi_mem *mem;
	ACPI_STATUS rv;

	sc->sc_dev = self;
	sc->sc_iot = aa->aa_memt;

	rv = acpi_resource_parse(self, aa->aa_node->ad_handle, "_CRS",
	    &res, &acpi_resource_parse_ops_default);
	if (ACPI_FAILURE(rv))
		return;

	mem = acpi_res_mem(&res, 0);
	if (mem == NULL) {
		aprint_error_dev(self, "no registers\n");
		goto done;
	}
	if (bus_space_map(sc->sc_iot, mem->ar_base, mem->ar_length, 0,
	    &sc->sc_ioh) != 0) {
		aprint_error_dev(self, "couldn't map registers\n");
		goto done;
	}

	aprint_normal_dev(self, "power management, reset and watchdog\n");
	bcmpmwdog_attach_common(sc);
	if (mem->ar_length >= BCM2712_PM_GRAFX + sizeof(uint32_t) &&
	    bcmpmwdog_v3d_sc == NULL)
		bcmpmwdog_v3d_sc = sc;
#ifdef BCM2712_V3D_TAKEOVER
	bcmpmwdog_v3d_register(sc, mem->ar_base, mem->ar_length);
#endif
	bcmpmwdog_acpi_power_source(self);
done:
	acpi_resource_cleanup(&res);
}

static void
bcmpmwdog_acpi_power_source(device_t self)
{
	ACPI_INTEGER rsts, reset, current, high, overcurrent;
	const char *sep = " (";
	u_int i;

	if (ACPI_SUCCESS(acpi_eval_integer(NULL, "\\_SB.BLRS", &rsts)) &&
	    rsts != PM_ACPI_UNREPORTED)
		aprint_normal_dev(self, "reset status at boot %#jx\n",
		    (uintmax_t)rsts);

	if (ACPI_FAILURE(acpi_eval_integer(NULL, "\\_SB.PSRR", &reset)) ||
	    ACPI_FAILURE(acpi_eval_integer(NULL, "\\_SB.PSMC", &current)) ||
	    ACPI_FAILURE(acpi_eval_integer(NULL, "\\_SB.PSUH", &high)) ||
	    ACPI_FAILURE(acpi_eval_integer(NULL, "\\_SB.PSOC", &overcurrent)))
		return;

	aprint_normal_dev(self, "PMIC reset event");
	if (reset == PM_ACPI_UNREPORTED)
		aprint_normal(" not reported");
	else {
		aprint_normal(" %#jx", (uintmax_t)reset);
		for (i = 0; i < __arraycount(bcmpmwdog_acpi_pmic_reasons); i++) {
			if (!ISSET(reset, __BIT(i)))
				continue;
			aprint_normal("%s%s", sep,
			    bcmpmwdog_acpi_pmic_reasons[i]);
			sep = ", ";
		}
		if (sep[0] == ',')
			aprint_normal(")");
	}
	if (current != PM_ACPI_UNREPORTED)
		aprint_normal(", supply %ju mA", (uintmax_t)current);
	if (high != PM_ACPI_UNREPORTED)
		aprint_normal(", USB current limit %s", high ? "high" : "low");
	if (overcurrent != PM_ACPI_UNREPORTED && overcurrent != 0)
		aprint_normal(", USB over-current at boot");
	aprint_normal("\n");
}

int
bcmpmwdog_v3d_status(uint32_t *value)
{
	struct bcm2835pmwdog_softc *sc = bcmpmwdog_v3d_sc;

	if (sc == NULL)
		return ENXIO;
	return bus_space_peek_4(sc->sc_iot, sc->sc_ioh,
	    BCM2712_PM_GRAFX, value);
}

#ifdef BCM2712_V3D_TAKEOVER
#define BCM2712_PM_BASE		UINT64_C(0x107d200000)
#define BCM2712_PM_SIZE		0x308
#define BCM2712_PM_PASSWORD	0x5a000000
#define BCM2712_PM_RESET_N	__BIT(6)
#define BCM2712_PM_ENABLE		__BIT(12)

static struct {
	kmutex_t lock;
	bool ready, sealed, attempted;
	device_t owner;
	uint32_t baseline;
} bcmpmwdog_v3d_claimed;

static void
bcmpmwdog_v3d_register(struct bcm2835pmwdog_softc *sc, bus_addr_t base,
    bus_size_t size)
{
	/* Only the original, exact ACPI PM owner can offer this interface. */
	if (sc != bcmpmwdog_v3d_sc || base != BCM2712_PM_BASE ||
	    size != BCM2712_PM_SIZE || bcmpmwdog_v3d_claimed.ready)
		return;
	mutex_init(&bcmpmwdog_v3d_claimed.lock, MUTEX_DEFAULT, IPL_NONE);
	bcmpmwdog_v3d_claimed.ready = true;
}

int
bcmpmwdog_v3d_claim(device_t owner)
{
	uint32_t value;
	int error;

	if (owner == NULL || !bcmpmwdog_v3d_claimed.ready)
		return ENXIO;
	mutex_enter(&bcmpmwdog_v3d_claimed.lock);
	if (bcmpmwdog_v3d_claimed.owner != NULL) {
		error = EBUSY;
		goto out;
	}
	error = bcmpmwdog_v3d_status(&value);
	if (error != 0)
		goto out;
	if ((value & 0xff000000) != 0 ||
	    (value & (BCM2712_PM_RESET_N | BCM2712_PM_ENABLE)) !=
	    (BCM2712_PM_RESET_N | BCM2712_PM_ENABLE)) {
		error = EBUSY;
		goto out;
	}
	bcmpmwdog_v3d_claimed.baseline = value;
	bcmpmwdog_v3d_claimed.owner = owner;
out:
	mutex_exit(&bcmpmwdog_v3d_claimed.lock);
	return error;
}

int
bcmpmwdog_v3d_seal(device_t owner)
{
	int error = 0;

	if (!bcmpmwdog_v3d_claimed.ready || owner == NULL)
		return ENXIO;
	mutex_enter(&bcmpmwdog_v3d_claimed.lock);
	if (bcmpmwdog_v3d_claimed.owner != owner)
		error = EPERM;
	else if (bcmpmwdog_v3d_claimed.sealed)
		error = EBUSY;
	else
		bcmpmwdog_v3d_claimed.sealed = true;
	mutex_exit(&bcmpmwdog_v3d_claimed.lock);
	return error;
}

int
bcmpmwdog_v3d_release(device_t owner)
{
	int error = 0;

	if (!bcmpmwdog_v3d_claimed.ready || owner == NULL)
		return ENXIO;
	mutex_enter(&bcmpmwdog_v3d_claimed.lock);
	if (bcmpmwdog_v3d_claimed.owner != owner)
		error = EPERM;
	else if (bcmpmwdog_v3d_claimed.sealed)
		error = EBUSY;
	else
		bcmpmwdog_v3d_claimed.owner = NULL;
	mutex_exit(&bcmpmwdog_v3d_claimed.lock);
	return error;
}

int
bcmpmwdog_v3d_reset(device_t owner)
{
	struct bcm2835pmwdog_softc *sc = bcmpmwdog_v3d_sc;
	uint32_t baseline, value;
	int error;

	if (!bcmpmwdog_v3d_claimed.ready || sc == NULL || owner == NULL)
		return ENXIO;
	mutex_enter(&bcmpmwdog_v3d_claimed.lock);
	if (bcmpmwdog_v3d_claimed.owner != owner ||
	    !bcmpmwdog_v3d_claimed.sealed) {
		error = EPERM;
		goto out;
	}
	if (bcmpmwdog_v3d_claimed.attempted) {
		error = EBUSY;
		goto out;
	}
	bcmpmwdog_v3d_claimed.attempted = true;
	baseline = bcmpmwdog_v3d_claimed.baseline;
	error = bcmpmwdog_v3d_status(&value);
	if (error != 0)
		goto out;
	if (value != baseline) {
		error = EIO;
		goto out;
	}
	error = bus_space_poke_4(sc->sc_iot, sc->sc_ioh, BCM2712_PM_GRAFX,
	    BCM2712_PM_PASSWORD | (baseline & ~BCM2712_PM_RESET_N));
	if (error != 0)
		goto out;
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, BCM2712_PM_GRAFX, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	error = bcmpmwdog_v3d_status(&value);
	if (error != 0)
		goto out;
	if (value != (baseline & ~BCM2712_PM_RESET_N)) {
		error = EIO;
		goto out;
	}
	/* BCM2712 has no ASB or domain clock; keep the enabled clock running. */
	delay(1);
	error = bus_space_poke_4(sc->sc_iot, sc->sc_ioh, BCM2712_PM_GRAFX,
	    BCM2712_PM_PASSWORD | baseline);
	if (error != 0)
		goto out;
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, BCM2712_PM_GRAFX, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	error = bcmpmwdog_v3d_status(&value);
	if (error == 0 && value != baseline)
		error = EIO;
out:
	mutex_exit(&bcmpmwdog_v3d_claimed.lock);
	return error;
}
#endif
