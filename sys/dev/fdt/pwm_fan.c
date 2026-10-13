/* $NetBSD: pwm_fan.c,v 1.2 2021/01/27 03:10:21 thorpej Exp $ */

/*-
 * Copyright (c) 2018 Jared McNeill <jmcneill@invisible.ca>
 * All rights reserved.
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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: pwm_fan.c,v 1.2 2021/01/27 03:10:21 thorpej Exp $");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/kthread.h>
#include <sys/mutex.h>
#include <sys/pmf.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <dev/pwm/pwmvar.h>
#include <dev/fdt/fdtvar.h>

struct pwm_fan_trip {
	struct pwm_fan_trip	*next;
	int			zone;
	int			temperature;
	int			hysteresis;
	int			sample;
	u_int			level;
	bool			active;
};

struct pwm_fan_softc {
	device_t		sc_dev;
	pwm_tag_t		sc_pwm;
	u_int			*sc_levels;
	u_int			sc_nlevels;
	struct pwm_fan_trip	*sc_trips;
	kmutex_t		sc_lock;
	kcondvar_t		sc_cv;
	struct sysctllog		*sc_log;
	int			sc_level;
	int			sc_min_level;
	int			sc_temperature;
	int			sc_error;
	int			sc_config_error;
	bool			sc_suspended;
};

static int	pwm_fan_match(device_t, cfdata_t, void *);
static void	pwm_fan_attach(device_t, device_t, void *);
static int	pwm_fan_detach(device_t, int);
static void	pwm_fan_init(device_t);
static bool	pwm_fan_suspend(device_t, const pmf_qual_t *);
static bool	pwm_fan_resume(device_t, const pmf_qual_t *);
static bool	pwm_fan_shutdown(device_t, int);
static int	pwm_fan_set(struct pwm_fan_softc *, u_int);
static void	pwm_fan_update(struct pwm_fan_softc *);
static void	pwm_fan_thread(void *);
static void	pwm_fan_sysctl_init(struct pwm_fan_softc *);
static int	pwm_fan_sysctl(SYSCTLFN_ARGS);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "pwm-fan" },
	DEVICE_COMPAT_EOL
};

CFATTACH_DECL_NEW(pwmfan, sizeof(struct pwm_fan_softc),
	pwm_fan_match, pwm_fan_attach, pwm_fan_detach, NULL);

static int
pwm_fan_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static int
pwm_fan_read_u32(int phandle, const char *name, uint32_t *value)
{
	const uint32_t *p;
	int len;

	p = fdtbus_get_prop(phandle, name, &len);
	if (p == NULL || len != sizeof(*p))
		return EINVAL;
	*value = be32toh(*p);
	return 0;
}

static int
pwm_fan_levels(struct pwm_fan_softc *sc, int phandle)
{
	const uint32_t *p;
	u_int n, count, value, previous = 0;
	int len;

	p = fdtbus_get_prop(phandle, "cooling-levels", &len);
	if (p == NULL || len < 2 * (int)sizeof(*p) ||
	    len > 256 * (int)sizeof(*p) || len % sizeof(*p) != 0)
		return EINVAL;
	count = len / sizeof(*p);
	for (n = 0; n < count; n++) {
		value = be32toh(p[n]);
		if (value > 255 || value < previous || (n == 0 && value != 0))
			return EINVAL;
		previous = value;
	}
	if (previous != 255)
		return EINVAL;
	sc->sc_levels = kmem_alloc(count * sizeof(*sc->sc_levels), KM_SLEEP);
	sc->sc_nlevels = count;
	for (n = 0; n < count; n++)
		sc->sc_levels[n] = be32toh(p[n]);
	return 0;
}

static int
pwm_fan_add_trip(struct pwm_fan_softc *sc, int zone, int map, u_int level)
{
	struct pwm_fan_trip *t;
	const char *type;
	uint32_t value, hysteresis;
	int trip, trips, len, temperature;

	if (level == 0 || level >= sc->sc_nlevels ||
	    pwm_fan_read_u32(map, "trip", &value) != 0)
		return EINVAL;
	trip = fdtbus_get_phandle_from_native(value);
	trips = of_find_firstchild_byname(zone, "trips");
	if (trip <= 0 || trips <= 0 || OF_parent(trip) != trips ||
	    !fdtbus_status_okay(trip))
		return EINVAL;
	type = fdtbus_get_prop(trip, "type", &len);
	if (type == NULL || len != sizeof("active") ||
	    memcmp(type, "active", sizeof("active")) != 0 ||
	    pwm_fan_read_u32(trip, "temperature", &value) != 0 ||
	    pwm_fan_read_u32(trip, "hysteresis", &hysteresis) != 0)
		return EINVAL;
	temperature = (int32_t)value;
	if (temperature < -273150 || hysteresis > INT_MAX ||
	    (int64_t)temperature - hysteresis < -273150)
		return EINVAL;
	t = kmem_zalloc(sizeof(*t), KM_SLEEP);
	t->zone = zone;
	t->temperature = temperature;
	t->hysteresis = hysteresis;
	t->level = level;
	t->next = sc->sc_trips;
	sc->sc_trips = t;
	return 0;
}

static int
pwm_fan_parse_map(struct pwm_fan_softc *sc, int fan, int zone, int map)
{
	const uint32_t *p;
	uint32_t cells, level;
	int len, target, error;
	u_int count;

	p = fdtbus_get_prop(map, "cooling-device", &len);
	if (p == NULL || len <= 0 || len % sizeof(*p) != 0)
		return EINVAL;
	count = len / sizeof(*p);
	while (count != 0) {
		target = fdtbus_get_phandle_from_native(be32toh(p[0]));
		if (target <= 0 || pwm_fan_read_u32(target,
		    "#cooling-cells", &cells) != 0 || cells >= count)
			return EINVAL;
		if (target == fan) {
			if (cells != 2 || p[1] != p[2])
				return EINVAL;
			level = be32toh(p[1]);
			error = pwm_fan_add_trip(sc, zone, map, level);
			if (error != 0)
				return error;
		}
		p += cells + 1;
		count -= cells + 1;
	}
	return 0;
}

static int
pwm_fan_parse(struct pwm_fan_softc *sc, int fan)
{
	const struct pwm_fan_trip *t, *other;
	int zones, zone, maps, map, error;
	bool highest;

	zones = OF_finddevice("/thermal-zones");
	if (zones <= 0)
		return 0;
	for (zone = OF_child(zones); zone > 0; zone = OF_peer(zone)) {
		if (!fdtbus_status_okay(zone))
			continue;
		maps = of_find_firstchild_byname(zone, "cooling-maps");
		if (maps <= 0)
			continue;
		for (map = OF_child(maps); map > 0; map = OF_peer(map)) {
			if (!fdtbus_status_okay(map))
				continue;
			error = pwm_fan_parse_map(sc, fan, zone, map);
			if (error != 0)
				return error;
		}
	}
	/* The hottest mapped trip must request full cooling in every zone. */
	for (t = sc->sc_trips; t != NULL; t = t->next) {
		highest = true;
		for (other = sc->sc_trips; other != NULL; other = other->next) {
			if (other->zone != t->zone)
				continue;
			if (other->temperature > t->temperature)
				highest = false;
			if ((other->temperature == t->temperature &&
			    other->level != t->level) ||
			    (other->temperature > t->temperature &&
			    other->level < t->level))
				return EINVAL;
		}
		if (highest && t->level != sc->sc_nlevels - 1)
			return EINVAL;
	}
	return 0;
}

static void
pwm_fan_attach(device_t parent, device_t self, void *aux)
{
	struct pwm_fan_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	int error;

	sc->sc_dev = self;
	sc->sc_level = -1;
	sc->sc_temperature = INT_MIN;
	sc->sc_pwm = fdtbus_pwm_acquire(faa->faa_phandle, "pwms");
	if (sc->sc_pwm == NULL) {
		aprint_error(": couldn't acquire pwm\n");
		return;
	}
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&sc->sc_cv, "pwmfan");
	aprint_naive("\n");
	aprint_normal(": PWM Fan\n");

	error = pwm_fan_levels(sc, faa->faa_phandle);
	if (error == 0)
		error = pwm_fan_parse(sc, faa->faa_phandle);
	else {
		/* A malformed table must not leave the fan without power. */
		sc->sc_nlevels = 2;
		sc->sc_levels = kmem_zalloc(2 * sizeof(*sc->sc_levels), KM_SLEEP);
		sc->sc_levels[1] = 255;
	}
	sc->sc_config_error = error;
	if (error != 0)
		aprint_error_dev(self, "invalid thermal configuration: %d\n", error);

	pwm_fan_sysctl_init(sc);
	if (!pmf_device_register1(self, pwm_fan_suspend, pwm_fan_resume,
	    pwm_fan_shutdown))
		aprint_error_dev(self, "couldn't register power handlers\n");
	config_interrupts(self, pwm_fan_init);
}

static void
pwm_fan_init(device_t self)
{
	struct pwm_fan_softc *sc = device_private(self);
	int error;

	/* FDT sensor providers have attached before cold autoconfiguration ends. */
	mutex_enter(&sc->sc_lock);
	pwm_fan_update(sc);
	mutex_exit(&sc->sc_lock);
	error = kthread_create(PRI_NONE, KTHREAD_MPSAFE,
	    NULL, pwm_fan_thread, sc, NULL, "%s", device_xname(self));
	if (error != 0) {
		mutex_enter(&sc->sc_lock);
		sc->sc_config_error = error;
		pwm_fan_update(sc);
		mutex_exit(&sc->sc_lock);
		aprint_error_dev(self, "couldn't start thermal worker: %d\n", error);
	}
}

static int
pwm_fan_set(struct pwm_fan_softc *sc, u_int level)
{
	struct pwm_config conf;
	int error;

	if (level >= sc->sc_nlevels)
		return EINVAL;
	error = pwm_get_config(sc->sc_pwm, &conf);
	if (error != 0)
		return error;
	if (conf.period == 0)
		return EINVAL;
	conf.duty_cycle = ((uint64_t)conf.period * sc->sc_levels[level]) / 255;
	/* The provider updates a running PWM without an off/on pulse. */
	error = pwm_set_config(sc->sc_pwm, &conf);
	if (error == 0)
		error = pwm_enable(sc->sc_pwm);
	if (error == 0)
		sc->sc_level = level;
	return error;
}

static int
pwm_fan_demand(struct pwm_fan_softc *sc, u_int *level)
{
	struct pwm_fan_trip *t, *previous;
	int error, hottest = INT_MIN;

	*level = sc->sc_nlevels - 1;
	if (sc->sc_config_error != 0)
		return sc->sc_config_error;
	if (sc->sc_trips == NULL || sc->sc_suspended)
		return 0;
	/* Sample each zone once before changing any trip's hysteresis state. */
	for (t = sc->sc_trips; t != NULL; t = t->next) {
		for (previous = sc->sc_trips; previous != t;
		    previous = previous->next) {
			if (previous->zone == t->zone)
				break;
		}
		if (previous != t) {
			t->sample = previous->sample;
			continue;
		}
		error = fdtbus_thermal_get_temp(t->zone, "thermal-sensors",
		    &t->sample);
		if (error != 0 || t->sample < -273150) {
			sc->sc_temperature = INT_MIN;
			return error != 0 ? error : ERANGE;
		}
		hottest = MAX(hottest, t->sample);
	}
	*level = sc->sc_min_level;
	for (t = sc->sc_trips; t != NULL; t = t->next) {
		if (t->sample >= t->temperature)
			t->active = true;
		else if ((int64_t)t->sample <=
		    (int64_t)t->temperature - t->hysteresis)
			t->active = false;
		if (t->active)
			*level = MAX(*level, t->level);
	}
	sc->sc_temperature = hottest;
	return 0;
}

static void
pwm_fan_update(struct pwm_fan_softc *sc)
{
	u_int level;
	int error, pwm_error;

	error = pwm_fan_demand(sc, &level);
	if (error != 0)
		level = sc->sc_nlevels - 1;
	/* Retry a failed provider transaction even if the old level was full. */
	if ((int)level != sc->sc_level || sc->sc_error != 0) {
		pwm_error = pwm_fan_set(sc, level);
		if (pwm_error != 0) {
			error = pwm_error;
			(void)pwm_fan_set(sc, sc->sc_nlevels - 1);
		}
	}
	if (error != sc->sc_error && error != 0)
		aprint_error_dev(sc->sc_dev, "thermal control failed: %d; request full cooling\n",
		    error);
	sc->sc_error = error;
}

static void
pwm_fan_thread(void *arg)
{
	struct pwm_fan_softc *sc = arg;

	mutex_enter(&sc->sc_lock);
	for (;;) {
		if (!sc->sc_suspended)
			pwm_fan_update(sc);
		(void)cv_timedwait(&sc->sc_cv, &sc->sc_lock, hz);
	}
}

static int
pwm_fan_sysctl(SYSCTLFN_ARGS)
{
	struct pwm_fan_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	int value, error;
	bool minimum;

	minimum = strcmp(rnode->sysctl_name, "min_level") == 0;
	mutex_enter(&sc->sc_lock);
	if (minimum)
		value = sc->sc_min_level;
	else if (strcmp(rnode->sysctl_name, "level") == 0)
		value = sc->sc_level;
	else if (strcmp(rnode->sysctl_name, "temperature") == 0)
		value = sc->sc_temperature;
	else
		value = sc->sc_error;
	mutex_exit(&sc->sc_lock);
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	if (!minimum || value < 0 || (u_int)value >= sc->sc_nlevels)
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	sc->sc_min_level = value;
	if (!sc->sc_suspended)
		pwm_fan_update(sc);
	error = sc->sc_error;
	cv_signal(&sc->sc_cv);
	mutex_exit(&sc->sc_lock);
	return error;
}

static void
pwm_fan_sysctl_init(struct pwm_fan_softc *sc)
{
	const struct sysctlnode *node;
	static const char * const names[] = {
		"min_level", "level", "temperature", "error"
	};
	static const char * const descriptions[] = {
		"Minimum automatic cooling state",
		"Last applied cooling state, or -1 if unknown",
		"Hottest assigned temperature in millidegrees C, or INT_MIN if unknown",
		"Last thermal or PWM error"
	};
	u_int n;
	int error;

	error = sysctl_createv(&sc->sc_log, 0, NULL, &node, 0, CTLTYPE_NODE,
	    device_xname(sc->sc_dev), NULL, NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL);
	for (n = 0; error == 0 && n < __arraycount(names); n++)
		error = sysctl_createv(&sc->sc_log, 0, &node, NULL,
		    n == 0 ? CTLFLAG_READWRITE : 0, CTLTYPE_INT,
		    names[n], SYSCTL_DESCR(descriptions[n]), pwm_fan_sysctl,
		    0, (void *)sc, 0, CTL_CREATE, CTL_EOL);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "couldn't create sysctl nodes: %d\n", error);
		sysctl_teardown(&sc->sc_log);
	}
}

static bool
pwm_fan_suspend(device_t self, const pmf_qual_t *qual)
{
	struct pwm_fan_softc *sc = device_private(self);
	int error;
	bool suspended;

	(void)qual;
	mutex_enter(&sc->sc_lock);
	suspended = sc->sc_suspended;
	sc->sc_suspended = true;
	error = pwm_fan_set(sc, sc->sc_nlevels - 1);
	sc->sc_error = error;
	if (error != 0)
		sc->sc_suspended = suspended;
	mutex_exit(&sc->sc_lock);
	return error == 0;
}

static bool
pwm_fan_resume(device_t self, const pmf_qual_t *qual)
{
	struct pwm_fan_softc *sc = device_private(self);

	(void)qual;
	mutex_enter(&sc->sc_lock);
	sc->sc_suspended = false;
	/* Reapply the PWM after providers restore their register state. */
	sc->sc_level = -1;
	pwm_fan_update(sc);
	cv_signal(&sc->sc_cv);
	mutex_exit(&sc->sc_lock);
	return true;
}

static bool
pwm_fan_shutdown(device_t self, int how)
{

	(void)how;
	return pwm_fan_suspend(self, NULL);
}

static int
pwm_fan_detach(device_t self, int flags)
{

	(void)self;
	(void)flags;
	/*
	 * The FDT PWM interface cannot release an acquired channel. Keep this
	 * consumer attached until a release contract also permits reacquisition.
	 */
	return EOPNOTSUPP;
}
