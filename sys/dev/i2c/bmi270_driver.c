/*
 * Copyright (c) 2026 The EmberBSD Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES ARE DISCLAIMED.
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

#if !defined(__has_include) || !__has_include(<bmi270.h>)
#error "Missing prepared BMI270 headers; install devel/bmi270-sensorapi in EXTERNAL_TOOLCHAIN"
#endif
#include <bmi270.h>

#include "bmi270_math.h"

#define BMI270_I2C_READ_WRITE_LEN	32
#define BMI270_NSENSORS			6

enum bmi270_axis {
	BMI270_ACCEL_X,
	BMI270_ACCEL_Y,
	BMI270_ACCEL_Z,
	BMI270_GYRO_X,
	BMI270_GYRO_Y,
	BMI270_GYRO_Z
};

struct bmi270_softc {
	i2c_tag_t sc_i2c;
	i2c_addr_t sc_addr;
	struct bmi2_dev sc_dev;
	struct sysmon_envsys *sc_sme;
	envsys_data_t sc_sensor[BMI270_NSENSORS];
};

static const char * const bmi270_sensor_desc[BMI270_NSENSORS] = {
	"accel X (um/s^2)", "accel Y (um/s^2)", "accel Z (um/s^2)",
	"gyro X (mdps)", "gyro Y (mdps)", "gyro Z (mdps)"
};

static const struct device_compatible_entry bmi270_compat[] = {
	{ .compat = "bosch,bmi270" },
	DEVICE_COMPAT_EOL
};

static int	bmi270_match(device_t, cfdata_t, void *);
static void	bmi270_attach(device_t, device_t, void *);
static void	bmi270_refresh(struct sysmon_envsys *, envsys_data_t *);
static int8_t	bmi270_i2c_read(uint8_t, uint8_t *, uint32_t, void *);
static int8_t	bmi270_i2c_write(uint8_t, const uint8_t *, uint32_t, void *);
static void	bmi270_delay_us(uint32_t, void *);

CFATTACH_DECL_NEW(bmi270g, sizeof(struct bmi270_softc),
	bmi270_match, bmi270_attach, NULL, NULL);

static int
bmi270_match(device_t parent, cfdata_t match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return iic_compatible_match(ia, bmi270_compat);
}

static void
bmi270_attach(device_t parent, device_t self, void *aux)
{
	struct bmi270_softc *sc = device_private(self);
	struct i2c_attach_args *ia = aux;
	struct bmi2_sens_config config[2] = { 0 };
	const uint8_t sensors[] = { BMI2_ACCEL, BMI2_GYRO };
	size_t i;
	int error;
	int8_t result;

	sc->sc_i2c = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_dev.intf = BMI2_I2C_INTF;
	sc->sc_dev.intf_ptr = sc;
	sc->sc_dev.read = bmi270_i2c_read;
	sc->sc_dev.write = bmi270_i2c_write;
	sc->sc_dev.delay_us = bmi270_delay_us;
	sc->sc_dev.read_write_len = BMI270_I2C_READ_WRITE_LEN;

	/* The Bosch base initializer validates CHIP_ID, loads its public config,
	 * and reads the gyro cross-axis calibration used by the base profile. */
	error = iic_acquire_bus(sc->sc_i2c, 0);
	if (error != 0) {
		aprint_error_dev(self, "couldn't acquire I2C bus: %d\n", error);
		return;
	}
	result = bmi270_init(&sc->sc_dev);
	if (result == BMI2_OK) {
		config[0].type = BMI2_ACCEL;
		config[1].type = BMI2_GYRO;
		result = bmi270_get_sensor_config(config, __arraycount(config),
		    &sc->sc_dev);
	}
	if (result == BMI2_OK) {
		config[0].cfg.acc.odr = BMI2_ACC_ODR_100HZ;
		config[0].cfg.acc.range = BMI2_ACC_RANGE_2G;
		config[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
		config[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
		result = bmi270_set_sensor_config(config, __arraycount(config),
		    &sc->sc_dev);
	}
	if (result == BMI2_OK)
		result = bmi270_sensor_enable(sensors, __arraycount(sensors),
		    &sc->sc_dev);
	iic_release_bus(sc->sc_i2c, 0);
	if (result != BMI2_OK) {
		aprint_error_dev(self, "BMI270 initialization/configuration failed: %d\n",
		    result);
		return;
	}

	aprint_naive("\n");
	aprint_normal(": Bosch BMI270 accelerometer and gyroscope\n");

	sc->sc_sme = sysmon_envsys_create();
	if (sc->sc_sme == NULL) {
		aprint_error_dev(self, "couldn't create envsys device\n");
		return;
	}
	sc->sc_sme->sme_name = device_xname(self);
	sc->sc_sme->sme_cookie = sc;
	sc->sc_sme->sme_refresh = bmi270_refresh;
	sc->sc_sme->sme_flags = SME_INIT_REFRESH;

	for (i = 0; i < __arraycount(sc->sc_sensor); i++) {
		envsys_data_t *sensor = &sc->sc_sensor[i];

		sensor->private = i;
		sensor->units = ENVSYS_INTEGER;
		sensor->state = ENVSYS_SINVALID;
		strlcpy(sensor->desc, bmi270_sensor_desc[i],
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
bmi270_refresh(struct sysmon_envsys *sme, envsys_data_t *edata)
{
	struct bmi270_softc *sc = sme->sme_cookie;
	struct bmi2_sens_data data;
	int16_t raw;
	unsigned int which = (unsigned int)edata->private;
	int error;
	int8_t result;

	edata->state = ENVSYS_SINVALID;
	if (which >= BMI270_NSENSORS)
		return;

	error = iic_acquire_bus(sc->sc_i2c, 0);
	if (error != 0)
		return;
	result = bmi2_get_sensor_data(&data, &sc->sc_dev);
	iic_release_bus(sc->sc_i2c, 0);
	if (result != BMI2_OK)
		return;

	switch (which) {
	case BMI270_ACCEL_X:
		raw = data.acc.x;
		break;
	case BMI270_ACCEL_Y:
		raw = data.acc.y;
		break;
	case BMI270_ACCEL_Z:
		raw = data.acc.z;
		break;
	case BMI270_GYRO_X:
		raw = data.gyr.x;
		break;
	case BMI270_GYRO_Y:
		raw = data.gyr.y;
		break;
	case BMI270_GYRO_Z:
		raw = data.gyr.z;
		break;
	default: return;
	}
	edata->value_cur = which <= BMI270_ACCEL_Z ?
	    bmi270_accel_um_s2(raw) : bmi270_gyro_mdps(raw);
	edata->state = ENVSYS_SVALID;
}

static int8_t
bmi270_i2c_read(uint8_t reg, uint8_t *data, uint32_t len, void *arg)
{
	struct bmi270_softc *sc = arg;
	int error;

	if (data == NULL || len == 0)
		return BMI2_E_NULL_PTR;
	error = iic_exec(sc->sc_i2c, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &reg, sizeof(reg), data, len, 0);
	return error == 0 ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

static int8_t
bmi270_i2c_write(uint8_t reg, const uint8_t *data, uint32_t len, void *arg)
{
	struct bmi270_softc *sc = arg;
	int error;

	if (data == NULL || len == 0)
		return BMI2_E_NULL_PTR;
	error = iic_exec(sc->sc_i2c, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &reg, sizeof(reg), __UNCONST(data), len, 0);
	return error == 0 ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

static void
bmi270_delay_us(uint32_t period, void *arg)
{
	(void)arg;
	delay(period);
}
