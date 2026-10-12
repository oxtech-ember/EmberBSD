/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
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

/* Native FDT temperature providers. Values use millidegrees Celsius. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/queue.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <dev/fdt/fdtvar.h>

struct fdtbus_thermal_controller {
	device_t tc_dev;
	int tc_phandle;
	const struct fdtbus_thermal_controller_func *tc_funcs;
	LIST_ENTRY(fdtbus_thermal_controller) tc_next;
};

/* Providers remain attached. Publish new entries under the registry lock. */
static LIST_HEAD(, fdtbus_thermal_controller) fdtbus_thermal_controllers =
    LIST_HEAD_INITIALIZER(fdtbus_thermal_controllers);

static kmutex_t fdtbus_thermal_lock;
static ONCE_DECL(fdtbus_thermal_once);

static int
fdtbus_thermal_init(void)
{

	mutex_init(&fdtbus_thermal_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

int
fdtbus_register_thermal_controller(device_t dev, int phandle,
    const struct fdtbus_thermal_controller_func *funcs)
{
	struct fdtbus_thermal_controller *tc, *newtc;

	if (phandle <= 0 || funcs == NULL || funcs->get_temp == NULL)
		return EINVAL;
	(void)RUN_ONCE(&fdtbus_thermal_once, fdtbus_thermal_init);
	newtc = kmem_alloc(sizeof(*newtc), KM_SLEEP);
	newtc->tc_dev = dev;
	newtc->tc_phandle = phandle;
	newtc->tc_funcs = funcs;
	mutex_enter(&fdtbus_thermal_lock);
	LIST_FOREACH(tc, &fdtbus_thermal_controllers, tc_next) {
		if (tc->tc_phandle == phandle) {
			mutex_exit(&fdtbus_thermal_lock);
			kmem_free(newtc, sizeof(*newtc));
			return EEXIST;
		}
	}
	LIST_INSERT_HEAD(&fdtbus_thermal_controllers, newtc, tc_next);
	mutex_exit(&fdtbus_thermal_lock);
	return 0;
}

int
fdtbus_thermal_get_temp(int phandle, const char *property, int *mc)
{
	struct fdtbus_thermal_controller *tc;
	const uint32_t *spec;
	uint32_t cells;
	int len, sensor, temperature, error;

	if (mc == NULL)
		return EINVAL;
	spec = fdtbus_get_prop(phandle, property, &len);
	if (spec == NULL || len < 4 || len % 4 != 0)
		return EINVAL;
	sensor = fdtbus_get_phandle_from_native(be32toh(spec[0]));
	if (sensor <= 0 || of_getprop_uint32(sensor,
	    "#thermal-sensor-cells", &cells) != 0)
		return EINVAL;
	/* One complete sensor specifier, with no length arithmetic overflow. */
	if (cells != (u_int)len / 4 - 1)
		return EINVAL;
	(void)RUN_ONCE(&fdtbus_thermal_once, fdtbus_thermal_init);
	mutex_enter(&fdtbus_thermal_lock);
	LIST_FOREACH(tc, &fdtbus_thermal_controllers, tc_next) {
		if (tc->tc_phandle == sensor)
			break;
	}
	mutex_exit(&fdtbus_thermal_lock);
	if (tc == NULL)
		return ENXIO;
	/* Providers cannot detach. Their callback runs outside the list lock. */
	error = tc->tc_funcs->get_temp(tc->tc_dev, spec + 1,
	    (size_t)len - 4, &temperature);
	if (error != 0)
		return error;
	*mc = temperature;
	return 0;
}
