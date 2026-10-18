/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted production-source thermal contracts. */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#include <sys/queue.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define be32toh(x) OSSwapBigToHostInt32(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#elif defined(__linux__)
#include <endian.h>
#else
#include <sys/endian.h>
#endif

typedef unsigned int u_int;
typedef void *device_t;
#define device_private(d) (d)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define KM_SLEEP 0
static void *kmem_alloc(size_t n, int flags)
{
	(void)flags;
	void *p = malloc(n);
	assert(p != NULL);
	return p;
}
static void kmem_free(void *p, size_t n) { (void)n; free(p); }
typedef bool kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define ONCE_DECL(name) bool name
#define RUN_ONCE(once, fn) (*(once) ? 0 : (*(once) = true, fn()))
static void mutex_init(kmutex_t *m, int type, int level)
{ (void)type; (void)level; *m = false; }
static void mutex_enter(kmutex_t *m) { assert(!*m); *m = true; }
static void mutex_exit(kmutex_t *m) { assert(*m); *m = false; }
struct fdtbus_thermal_controller_func {

	int (*get_temp)(device_t, const void *, size_t, int *);
};
static uint32_t prop[8];
static int prop_len, cells_error;
static uint32_t sensor_cells;
static bool prop_missing;
static const void *fdtbus_get_prop(int node, const char *name, int *len)
{
	assert(node == 10 && strcmp(name, "thermal-sensors") == 0);
	*len = prop_len;
	return prop_missing ? NULL : prop;
}
static int fdtbus_get_phandle_from_native(int node)
{
	return node == 20 ? 20 : 0;
}
static int of_getprop_uint32(int node, const char *name, uint32_t *cells)
{
	assert(node == 20 && strcmp(name, "#thermal-sensor-cells") == 0);
	*cells = sensor_cells;
	return cells_error;
}
struct sun60i_a733_ths_softc {
	bool sc_calibrated;
	uint32_t data[5];
};
static uint32_t read_sensor(struct sun60i_a733_ths_softc *sc, u_int offset)
{
	assert(offset >= 0xc0 && offset <= 0xd0 && offset % 4 == 0);
	return sc->data[(offset - 0xc0) / 4];
}
#define RD4(sc, reg) read_sensor(sc, reg)
#include "sensor.h"
#include "provider.h"
static unsigned int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); abort(); \
} } while (0)

int main(void)
{
	struct sun60i_a733_ths_softc sensor = {
		.sc_calibrated = true,
		.data = { 2000, 2100, 1900, 1700, 2200 },
	};
	const struct fdtbus_thermal_controller_func funcs = {
		.get_temp = sun60i_a733_ths_get_temp,
	};
	uint32_t index;
	int value;

	CHECK(fdtbus_register_thermal_controller(&sensor, 0, &funcs) == EINVAL);
	CHECK(fdtbus_register_thermal_controller(&sensor, 20, NULL) == EINVAL);
	CHECK(fdtbus_register_thermal_controller(&sensor, 20, &funcs) == 0);
	CHECK(fdtbus_register_thermal_controller(&sensor, 20, &funcs) == EEXIST);
	prop[0] = htobe32(20); prop[1] = htobe32(5);
	prop_len = 8; sensor_cells = 1;
	CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == 0);
	CHECK(value == (2835 - 1700) * 59);
	for (u_int n = 0; n < 5; n++) {
		index = htobe32(n);
		CHECK(sun60i_a733_ths_get_temp(&sensor, &index, 4, &value) == 0);
		CHECK(value == sun60i_a733_ths_to_mc(sensor.data[n]));
	}
	CHECK(sun60i_a733_ths_to_mc(1769) == (2835 - 1769) * 59);
	CHECK(sun60i_a733_ths_to_mc(1770) == (2822 - 1770) * 62);
	index = htobe32(6); value = 123;
	CHECK(sun60i_a733_ths_get_temp(&sensor, &index, 4, &value) == EINVAL);
	CHECK(value == 123);
	index = htobe32(5);
	CHECK(sun60i_a733_ths_get_temp(&sensor, &index, 0, &value) == EINVAL);
	CHECK(sun60i_a733_ths_get_temp(&sensor, NULL, 4, &value) == EINVAL);
	CHECK(sun60i_a733_ths_get_temp(&sensor, &index, 4, NULL) == EINVAL);
	for (u_int n = 0; n < 5; n++) {
		uint32_t old = sensor.data[n];
		value = 123; sensor.data[n] = 0;
		CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == EIO);
		CHECK(value == 123);
		sensor.data[n] = 0xfff;
		CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == EIO);
		CHECK(value == 123);
		sensor.data[n] = old;
	}
	sensor.sc_calibrated = false;
	CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == EIO);
	CHECK(value == 123);
	sensor.sc_calibrated = true;
	for (int n = -1; n < 8; n++) {
		prop_len = n;
		CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == EINVAL);
		CHECK(value == 123);
	}
	prop_len = 12;
	CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == EINVAL);
	prop_len = 8; sensor_cells = UINT32_MAX;
	CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == EINVAL);
	sensor_cells = 1; cells_error = ENOENT;
	CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == EINVAL);
	cells_error = 0; prop[0] = htobe32(21);
	CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == EINVAL);
	prop[0] = htobe32(20); prop_missing = true;
	CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == EINVAL);
	prop_missing = false;
	CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", NULL) == EINVAL);
	struct fdtbus_thermal_controller *tc = LIST_FIRST(&fdtbus_thermal_controllers);
	LIST_REMOVE(tc, tc_next); free(tc);
	CHECK(fdtbus_thermal_get_temp(10, "thermal-sensors", &value) == ENXIO);
	CHECK(value == 123);
	printf("FDT thermal contracts: %u checks passed\n", checks);
	return 0;
}
