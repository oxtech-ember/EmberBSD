/*
 * Copyright (c) 2026 The EmberBSD Project
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

#if !defined(__has_include) || !__has_include(<bmm150.h>)
#error "Missing prepared BMM150 headers; install devel/bmm150-sensorapi in EXTERNAL_TOOLCHAIN"
#endif
#include <bmm150.h>

#include "bmm150_math.h"

enum bmm150_axis {
	BMM150_AXIS_X,
	BMM150_AXIS_Y,
	BMM150_AXIS_Z,
	BMM150_NAXES
};

struct bmm150_softc {
	i2c_tag_t sc_i2c;
	i2c_addr_t sc_addr;
	struct bmm150_dev sc_dev;
	struct sysmon_envsys *sc_sme;
	envsys_data_t sc_sensor[BMM150_NAXES];
};

static const char * const bmm150_axis_desc[BMM150_NAXES] = {
	"magnetic field X (nT)",
	"magnetic field Y (nT)",
	"magnetic field Z (nT)",
};

static const struct device_compatible_entry bmm150_compat[] = {
	{ .compat = "bosch,bmm150" },
	DEVICE_COMPAT_EOL
};

static int	bmm150_match(device_t, cfdata_t, void *);
static void	bmm150_attach(device_t, device_t, void *);
static void	bmm150_refresh(struct sysmon_envsys *, envsys_data_t *);
static int8_t	bmm150_i2c_read(uint8_t, uint8_t *, uint32_t, void *);
static int8_t	bmm150_i2c_write(uint8_t, const uint8_t *, uint32_t, void *);
static void	bmm150_delay_us(uint32_t, void *);

CFATTACH_DECL_NEW(bmm150g, sizeof(struct bmm150_softc),
	bmm150_match, bmm150_attach, NULL, NULL);

static int
bmm150_match(device_t parent, cfdata_t match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return iic_compatible_match(ia, bmm150_compat);
}

static void
bmm150_attach(device_t parent, device_t self, void *aux)
{
	struct bmm150_softc *sc = device_private(self);
	struct i2c_attach_args *ia = aux;
	struct bmm150_settings settings = { 0 };
	size_t i;
	int error;
	int8_t result;

	sc->sc_i2c = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_dev.intf = BMM150_I2C_INTF;
	sc->sc_dev.intf_ptr = sc;
	sc->sc_dev.read = bmm150_i2c_read;
	sc->sc_dev.write = bmm150_i2c_write;
	sc->sc_dev.delay_us = bmm150_delay_us;

	/*
	 * Verify the device, load its factory trim registers, and select the
	 * Bosch regular preset at 10 Hz.  The preset/configuration writes are
	 * sensor setup; no calibration or non-volatile memory is modified.
	 */
	settings.pwr_mode = BMM150_POWERMODE_NORMAL;
	settings.preset_mode = BMM150_PRESETMODE_REGULAR;
	error = iic_acquire_bus(sc->sc_i2c, 0);
	if (error != 0) {
		aprint_error_dev(self, "couldn't acquire I2C bus: %d\n", error);
		return;
	}
	result = bmm150_init(&sc->sc_dev);
	if (result == BMM150_OK && sc->sc_dev.chip_id != BMM150_CHIP_ID)
		result = BMM150_E_DEV_NOT_FOUND;
	if (result == BMM150_OK)
		result = bmm150_set_op_mode(&settings, &sc->sc_dev);
	if (result == BMM150_OK)
		result = bmm150_set_presetmode(&settings, &sc->sc_dev);
	iic_release_bus(sc->sc_i2c, 0);
	if (result != BMM150_OK) {
		aprint_error_dev(self, "BMM150 initialization/configuration failed: %d\n",
		    result);
		return;
	}

	aprint_naive("\n");
	aprint_normal(": Bosch BMM150 magnetometer\n");

	sc->sc_sme = sysmon_envsys_create();
	if (sc->sc_sme == NULL) {
		aprint_error_dev(self, "couldn't create envsys device\n");
		return;
	}
	sc->sc_sme->sme_name = device_xname(self);
	sc->sc_sme->sme_cookie = sc;
	sc->sc_sme->sme_refresh = bmm150_refresh;
	sc->sc_sme->sme_flags = SME_INIT_REFRESH;

	for (i = 0; i < __arraycount(sc->sc_sensor); i++) {
		envsys_data_t *sensor = &sc->sc_sensor[i];

		sensor->private = i;
		sensor->units = ENVSYS_INTEGER;
		sensor->state = ENVSYS_SINVALID;
		strlcpy(sensor->desc, bmm150_axis_desc[i],
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
bmm150_refresh(struct sysmon_envsys *sme, envsys_data_t *edata)
{
	struct bmm150_softc *sc = sme->sme_cookie;
	struct bmm150_mag_data mag;
	int16_t raw;
	int16_t *axis;
	unsigned int which = (unsigned int)edata->private;
	int error;
	int8_t result;

	edata->state = ENVSYS_SINVALID;
	if (which >= BMM150_NAXES)
		return;

	error = iic_acquire_bus(sc->sc_i2c, 0);
	if (error != 0)
		return;
	result = bmm150_read_mag_data(&mag, &sc->sc_dev);
	iic_release_bus(sc->sc_i2c, 0);
	if (result != BMM150_OK)
		return;

	axis = which == BMM150_AXIS_X ? &mag.x :
	    which == BMM150_AXIS_Y ? &mag.y : &mag.z;
	raw = *axis;
	if (raw == BMM150_OVERFLOW_OUTPUT)
		return;

	/* Bosch reports 16 LSB per microtesla; preserve this resolution in nT. */
	edata->value_cur = bmm150_raw_to_nanotesla(raw);
	edata->state = ENVSYS_SVALID;
}

static int8_t
bmm150_i2c_read(uint8_t reg, uint8_t *data, uint32_t len, void *arg)
{
	struct bmm150_softc *sc = arg;
	int error;

	if (data == NULL || len == 0)
		return -1;
	error = iic_exec(sc->sc_i2c, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &reg, sizeof(reg), data, len, 0);
	return error == 0 ? BMM150_INTF_RET_SUCCESS : -1;
}

static int8_t
bmm150_i2c_write(uint8_t reg, const uint8_t *data, uint32_t len, void *arg)
{
	struct bmm150_softc *sc = arg;
	int error;

	if (data == NULL || len == 0)
		return -1;
	error = iic_exec(sc->sc_i2c, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &reg, sizeof(reg), __UNCONST(data), len, 0);
	return error == 0 ? BMM150_INTF_RET_SUCCESS : -1;
}

static void
bmm150_delay_us(uint32_t period, void *arg)
{
	(void)arg;
	delay(period);
}
