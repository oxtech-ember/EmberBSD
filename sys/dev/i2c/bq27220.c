/*
 * Copyright (c) 2026 EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/bus.h>

#include <dev/i2c/i2cvar.h>
#include <dev/sysmon/sysmonvar.h>

#include "bq27220_logic.h"

#define BQ27220_REG_TEMPERATURE	0x06
#define BQ27220_REG_VOLTAGE	0x08
#define BQ27220_REG_CURRENT	0x0c
#define BQ27220_REG_SOC		0x2c

struct bq27220_softc {
	i2c_tag_t sc_i2c;
	i2c_addr_t sc_addr;
	struct sysmon_envsys *sc_sme;
	envsys_data_t sc_sensor[BQ27220_NSENSORS];
};

struct bq27220_sensor_desc {
	uint8_t reg;
	uint32_t units;
	uint32_t flags;
	const char *desc;
};

static const struct bq27220_sensor_desc bq27220_sensors[] = {
	[BQ27220_SENSOR_VOLTAGE] = {
		BQ27220_REG_VOLTAGE, ENVSYS_SVOLTS_DC, 0, "battery voltage"
	},
	[BQ27220_SENSOR_CURRENT] = {
		BQ27220_REG_CURRENT, ENVSYS_SAMPS, 0, "battery current"
	},
	[BQ27220_SENSOR_TEMPERATURE] = {
		BQ27220_REG_TEMPERATURE, ENVSYS_STEMP, 0, "gauge temperature"
	},
	[BQ27220_SENSOR_SOC] = {
		BQ27220_REG_SOC, ENVSYS_INTEGER,
		ENVSYS_FPERCENT | ENVSYS_FVALID_MAX, "battery charge"
	},
};

static const struct device_compatible_entry bq27220_compat[] = {
	{ .compat = "ti,bq27220" },
	DEVICE_COMPAT_EOL
};

static int	bq27220_match(device_t, cfdata_t, void *);
static void	bq27220_attach(device_t, device_t, void *);
static void	bq27220_refresh(struct sysmon_envsys *, envsys_data_t *);
static int	bq27220_read_word(struct bq27220_softc *, uint8_t, uint16_t *);

CFATTACH_DECL_NEW(bq27220g, sizeof(struct bq27220_softc),
	bq27220_match, bq27220_attach, NULL, NULL);

static int
bq27220_match(device_t parent, cfdata_t match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return iic_compatible_match(ia, bq27220_compat);
}

static void
bq27220_attach(device_t parent, device_t self, void *aux)
{
	struct bq27220_softc *sc = device_private(self);
	struct i2c_attach_args *ia = aux;
	size_t i;
	int error;

	sc->sc_i2c = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	aprint_naive("\n");
	aprint_normal(": TI BQ27220 fuel gauge\n");

	sc->sc_sme = sysmon_envsys_create();
	if (sc->sc_sme == NULL) {
		aprint_error_dev(self, "couldn't create envsys device\n");
		return;
	}
	sc->sc_sme->sme_name = device_xname(self);
	sc->sc_sme->sme_cookie = sc;
	sc->sc_sme->sme_refresh = bq27220_refresh;
	sc->sc_sme->sme_flags = SME_INIT_REFRESH;

	for (i = 0; i < __arraycount(bq27220_sensors); i++) {
		envsys_data_t *sensor = &sc->sc_sensor[i];

		sensor->private = i;
		sensor->units = bq27220_sensors[i].units;
		sensor->flags = bq27220_sensors[i].flags;
		if (i == BQ27220_SENSOR_SOC)
			sensor->value_max = 100;
		sensor->state = ENVSYS_SINVALID;
		strlcpy(sensor->desc, bq27220_sensors[i].desc,
		    sizeof(sensor->desc));
		error = sysmon_envsys_sensor_attach(sc->sc_sme, sensor);
		if (error != 0) {
			aprint_error_dev(self,
			    "couldn't attach envsys sensor: %d\n", error);
			sysmon_envsys_destroy(sc->sc_sme);
			sc->sc_sme = NULL;
			return;
		}
	}

	error = sysmon_envsys_register(sc->sc_sme);
	if (error != 0) {
		aprint_error_dev(self, "couldn't register envsys device: %d\n",
		    error);
		sysmon_envsys_destroy(sc->sc_sme);
		sc->sc_sme = NULL;
	}
}

static void
bq27220_refresh(struct sysmon_envsys *sme, envsys_data_t *edata)
{
	struct bq27220_softc *sc = sme->sme_cookie;
	enum bq27220_sensor sensor = edata->private;
	uint16_t raw;
	int32_t value;
	int error;

	edata->state = ENVSYS_SINVALID;
	if ((unsigned)sensor >= BQ27220_NSENSORS)
		return;

	error = iic_acquire_bus(sc->sc_i2c, 0);
	if (error != 0)
		return;
	error = bq27220_read_word(sc, bq27220_sensors[sensor].reg, &raw);
	iic_release_bus(sc->sc_i2c, 0);
	if (error != 0 || !bq27220_sensor_value(sensor, raw, &value))
		return;

	edata->value_cur = value;
	edata->state = ENVSYS_SVALID;
}

static int
bq27220_read_word(struct bq27220_softc *sc, uint8_t command, uint16_t *value)
{
	uint8_t bytes[2];
	int error;

	error = iic_exec(sc->sc_i2c, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &command, sizeof(command), bytes, sizeof(bytes), 0);
	if (error != 0)
		return error;

	*value = bq27220_decode_word(bytes);
	return 0;
}
