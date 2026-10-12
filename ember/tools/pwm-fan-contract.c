/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the production parser, hysteresis, sysctl and PWM error paths. */
#include <sys/types.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifndef __NetBSD__
typedef unsigned int u_int;
#endif
typedef void *device_t;
typedef void *pwm_tag_t;
typedef int kmutex_t;
typedef int kcondvar_t;
typedef int pmf_qual_t;
struct sysctllog;
struct pwm_config {
	u_int period;
	u_int duty_cycle;
	int polarity;
};
struct sysctlnode {
	const char *sysctl_name;
	void *sysctl_data;
};
#define KM_SLEEP 0
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define SYSCTLFN_ARGS const struct sysctlnode *rnode, const int *newp
#define SYSCTLFN_CALL(node) (node), newp
#define aprint_error_dev(...) ((void)0)
#ifdef __NetBSD__
#include <sys/endian.h>
#else
static uint32_t
be32toh(uint32_t value)
{
	const unsigned char *p = (const unsigned char *)&value;

	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | p[3];
}
#endif

#include "pwm-fan-types.h"

struct property {
	const char *name;
	uint32_t data[260];
	int len;
};
struct dt_node {
	const char *name;
	int parent;
	bool disabled;
	struct property props[8];
};
static struct dt_node nodes[64];
static int zone_temperature[64], zone_error[64];
static unsigned int checks, allocations, sample_count, pwm_sets, pwm_enables;
static unsigned int fail_get, fail_set, fail_enable;
static struct pwm_config config;
static int lookup_error, lookup_value;

#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void *
kmem_alloc(size_t size, int flags)
{
	void *p;

	(void)flags;
	p = malloc(size);
	assert(p != NULL);
	allocations++;
	return p;
}

static void *
kmem_zalloc(size_t size, int flags)
{
	void *p = kmem_alloc(size, flags);

	memset(p, 0, size);
	return p;
}

static void
mutex_enter(kmutex_t *lock)
{

	assert(*lock == 0);
	*lock = 1;
}

static void
mutex_exit(kmutex_t *lock)
{

	assert(*lock == 1);
	*lock = 0;
}

static void
cv_signal(kcondvar_t *cv)
{

	(void)cv;
}

static int
sysctl_lookup(SYSCTLFN_ARGS)
{

	if (lookup_error != 0)
		return lookup_error;
	if (newp != NULL)
		*(int *)rnode->sysctl_data = *newp;
	else
		lookup_value = *(int *)rnode->sysctl_data;
	return 0;
}

static struct property *
property(int node, const char *name)
{
	unsigned int i;
	struct property *p;

	assert(node > 0 && node < 64);
	for (i = 0; i < 8; i++) {
		p = &nodes[node].props[i];
		if (p->name == NULL || strcmp(p->name, name) == 0) {
			p->name = name;
			return p;
		}
	}
	abort();
}

static void
words(int node, const char *name, const uint32_t *values, size_t count)
{
	struct property *p = property(node, name);
	unsigned int i;
	unsigned char *b;

	assert(count <= 260);
	p->len = count * 4;
	for (i = 0; i < count; i++) {
		b = (unsigned char *)&p->data[i];
		b[0] = values[i] >> 24;
		b[1] = values[i] >> 16;
		b[2] = values[i] >> 8;
		b[3] = values[i];
	}
}

static void
word(int node, const char *name, uint32_t value)
{

	words(node, name, &value, 1);
}

static void
string_prop(int node, const char *name, const char *value)
{
	struct property *p = property(node, name);

	p->len = strlen(value) + 1;
	assert((size_t)p->len <= sizeof(p->data));
	memcpy(p->data, value, p->len);
}

static const void *
fdtbus_get_prop(int node, const char *name, int *len)
{
	unsigned int i;

	*len = -1;
	if (node <= 0 || node >= 64)
		return NULL;
	for (i = 0; i < 8; i++) {
		if (nodes[node].props[i].name != NULL &&
		    strcmp(nodes[node].props[i].name, name) == 0) {
			*len = nodes[node].props[i].len;
			return nodes[node].props[i].data;
		}
	}
	return NULL;
}

static int
fdtbus_get_phandle_from_native(uint32_t phandle)
{

	return phandle > 0 && phandle < 64 && nodes[phandle].name != NULL ?
	    (int)phandle : -1;
}

static int
OF_parent(int node)
{

	return nodes[node].parent;
}

static int
OF_child(int node)
{
	int i;

	for (i = 1; i < 64; i++) {
		if (nodes[i].name != NULL && nodes[i].parent == node)
			return i;
	}
	return 0;
}

static int
OF_peer(int node)
{
	int i;

	for (i = node + 1; i < 64; i++) {
		if (nodes[i].name != NULL && nodes[i].parent == nodes[node].parent)
			return i;
	}
	return 0;
}

static int
of_find_firstchild_byname(int node, const char *name)
{
	int i;

	for (i = OF_child(node); i != 0; i = OF_peer(i)) {
		if (strcmp(nodes[i].name, name) == 0)
			return i;
	}
	return 0;
}

static int
OF_finddevice(const char *path)
{

	assert(strcmp(path, "/thermal-zones") == 0);
	return nodes[2].name != NULL ? 2 : -1;
}

static bool
fdtbus_status_okay(int node)
{

	return !nodes[node].disabled;
}

static int
fdtbus_thermal_get_temp(int zone, const char *name, int *temperature)
{

	assert(strcmp(name, "thermal-sensors") == 0);
	sample_count++;
	if (zone_error[zone] != 0)
		return zone_error[zone];
	*temperature = zone_temperature[zone];
	return 0;
}

static int
pwm_get_config(pwm_tag_t pwm, struct pwm_config *out)
{

	(void)pwm;
	if (fail_get != 0) {
		fail_get--;
		return EIO;
	}
	*out = config;
	return 0;
}

static int
pwm_set_config(pwm_tag_t pwm, const struct pwm_config *in)
{

	(void)pwm;
	pwm_sets++;
	if (fail_set != 0) {
		fail_set--;
		return EIO;
	}
	config = *in;
	return 0;
}

static int
pwm_enable(pwm_tag_t pwm)
{

	(void)pwm;
	pwm_enables++;
	if (fail_enable != 0) {
		fail_enable--;
		return EIO;
	}
	return 0;
}

static void *
device_private(device_t device)
{

	return device;
}

#include "pwm-fan-control.h"


static void
cleanup(struct pwm_fan_softc *sc)
{
	struct pwm_fan_trip *t;

	while ((t = sc->sc_trips) != NULL) {
		sc->sc_trips = t->next;
		free(t);
		allocations--;
	}
	if (sc->sc_levels != NULL) {
		free(sc->sc_levels);
		sc->sc_levels = NULL;
		allocations--;
	}
	CHECK(allocations == 0);
}

static void
fixture(struct pwm_fan_softc *sc)
{
	static const uint32_t levels[] = { 0, 50, 102, 170, 255 };
	uint32_t map[3];
	int i;

	memset(sc, 0, sizeof(*sc));
	memset(nodes, 0, sizeof(nodes));
	memset(zone_temperature, 0, sizeof(zone_temperature));
	memset(zone_error, 0, sizeof(zone_error));
	sample_count = pwm_sets = pwm_enables = 0;
	fail_get = fail_set = fail_enable = 0;
	lookup_error = 0;
	sc->sc_level = -1;
	sc->sc_temperature = INT_MIN;
	config = (struct pwm_config){ .period = 40000, .polarity = 1 };
	nodes[1].name = "fan";
	nodes[2].name = "thermal-zones";
	nodes[3] = (struct dt_node){ .name = "cpu-thermal", .parent = 2 };
	nodes[4] = (struct dt_node){ .name = "trips", .parent = 3 };
	nodes[5] = (struct dt_node){ .name = "cooling-maps", .parent = 3 };
	word(1, "#cooling-cells", 2);
	words(1, "cooling-levels", levels, 5);
	for (i = 0; i < 4; i++) {
		nodes[10 + i] = (struct dt_node){ .name = "trip", .parent = 4 };
		nodes[20 + i] = (struct dt_node){ .name = "map", .parent = 5 };
		word(10 + i, "temperature", 55000 + i * 10000);
		word(10 + i, "hysteresis", 5000);
		string_prop(10 + i, "type", "active");
		word(20 + i, "trip", 10 + i);
		map[0] = 1;
		map[1] = map[2] = i + 1;
		words(20 + i, "cooling-device", map, 3);
	}
	zone_temperature[3] = 40000;
}

static void
ready(struct pwm_fan_softc *sc)
{

	fixture(sc);
	CHECK(pwm_fan_levels(sc, 1) == 0);
	CHECK(pwm_fan_parse(sc, 1) == 0);
	CHECK(pwm_fan_set(sc, 4) == 0);
}

static void
sample(struct pwm_fan_softc *sc, int temperature, int level)
{
	unsigned int before = sample_count;

	zone_temperature[3] = temperature;
	pwm_fan_update(sc);
	CHECK(sc->sc_level == level);
	CHECK(sc->sc_temperature == temperature);
	CHECK(sc->sc_error == 0);
	CHECK(sample_count == before + 1);
	CHECK(config.duty_cycle == (uint64_t)config.period * sc->sc_levels[level] / 255);
	CHECK(config.polarity == 1);
}

static void
policy_tests(void)
{
	struct pwm_fan_softc sc;
	struct sysctlnode node;
	unsigned int before;
	int value;

	ready(&sc);
	CHECK(config.duty_cycle == config.period);
	sample(&sc, 40000, 0);
	sample(&sc, 54999, 0);
	sample(&sc, 55000, 1);
	sample(&sc, 50001, 1);
	sample(&sc, 50000, 0);
	sample(&sc, 64999, 1);
	sample(&sc, 65000, 2);
	sample(&sc, 60001, 2);
	sample(&sc, 60000, 1);
	sample(&sc, 75000, 3);
	sample(&sc, 70001, 3);
	sample(&sc, 70000, 2);
	sample(&sc, 85000, 4);
	sample(&sc, 80001, 4);
	sample(&sc, 80000, 3);
	sample(&sc, INT_MAX, 4);
	sample(&sc, -273150, 0);
	before = pwm_sets;
	sample(&sc, 40000, 0);
	CHECK(pwm_sets == before);

	node = (struct sysctlnode){ .sysctl_name = "min_level", .sysctl_data = &sc };
	value = 2;
	CHECK(pwm_fan_sysctl(&node, &value) == 0);
	CHECK(sc.sc_min_level == 2 && sc.sc_level == 2);
	sample(&sc, 85000, 4);
	value = 0;
	CHECK(pwm_fan_sysctl(&node, &value) == 0);
	CHECK(sc.sc_min_level == 0 && sc.sc_level == 4);
	sample(&sc, 40000, 0);
	value = -1;
	CHECK(pwm_fan_sysctl(&node, &value) == EINVAL);
	value = 5;
	CHECK(pwm_fan_sysctl(&node, &value) == EINVAL);
	value = INT_MAX;
	CHECK(pwm_fan_sysctl(&node, &value) == EINVAL);
	CHECK(sc.sc_min_level == 0);
	CHECK(pwm_fan_sysctl(&node, NULL) == 0 && lookup_value == 0);
	node.sysctl_name = "level";
	CHECK(pwm_fan_sysctl(&node, &value) == EINVAL);
	CHECK(pwm_fan_sysctl(&node, NULL) == 0 && lookup_value == 0);
	node.sysctl_name = "temperature";
	CHECK(pwm_fan_sysctl(&node, NULL) == 0 && lookup_value == 40000);
	node.sysctl_name = "error";
	CHECK(pwm_fan_sysctl(&node, NULL) == 0 && lookup_value == 0);
	lookup_error = EFAULT;
	CHECK(pwm_fan_sysctl(&node, NULL) == EFAULT);
	lookup_error = 0;

	sc.sc_suspended = true;
	before = sample_count;
	pwm_fan_update(&sc);
	CHECK(sc.sc_level == 4 && sample_count == before);
	sc.sc_suspended = false;
	sample(&sc, 40000, 0);
	cleanup(&sc);
}

static void
power_tests(void)
{
	struct pwm_fan_softc sc;
	struct sysctlnode node;
	unsigned int before;
	int value;

	ready(&sc);
	sample(&sc, 40000, 0);
	CHECK(pwm_fan_suspend(&sc, NULL));
	CHECK(sc.sc_suspended && sc.sc_level == 4);
	node = (struct sysctlnode){ .sysctl_name = "min_level", .sysctl_data = &sc };
	value = 1;
	before = pwm_sets;
	CHECK(pwm_fan_sysctl(&node, &value) == 0);
	CHECK(pwm_sets == before && sc.sc_level == 4 && sc.sc_min_level == 1);
	CHECK(pwm_fan_resume(&sc, NULL));
	CHECK(!sc.sc_suspended && sc.sc_level == 1);
	fail_set = 1;
	CHECK(!pwm_fan_suspend(&sc, NULL));
	CHECK(!sc.sc_suspended && sc.sc_error == EIO);
	pwm_fan_update(&sc);
	CHECK(sc.sc_level == 1 && sc.sc_error == 0);
	CHECK(pwm_fan_suspend(&sc, NULL));
	fail_set = 1;
	CHECK(!pwm_fan_suspend(&sc, NULL));
	CHECK(sc.sc_suspended);
	CHECK(pwm_fan_resume(&sc, NULL));
	CHECK(pwm_fan_shutdown(&sc, 0));
	CHECK(sc.sc_suspended && sc.sc_level == 4);
	CHECK(pwm_fan_resume(&sc, NULL));
	before = pwm_sets;
	CHECK(pwm_fan_detach(&sc, 0) == EOPNOTSUPP);
	CHECK(pwm_sets == before && sc.sc_level == 1 && !sc.sc_suspended);
	CHECK(sc.sc_trips != NULL && sc.sc_levels != NULL);
	value = 2;
	CHECK(pwm_fan_sysctl(&node, &value) == 0 && sc.sc_min_level == 2);
	sample(&sc, 85000, 4);
	cleanup(&sc);

}

static void
failure_tests(void)

{
	struct pwm_fan_softc sc;
	unsigned int before;
	int i;

	ready(&sc);
	sample(&sc, 40000, 0);
	zone_error[3] = EIO;
	pwm_fan_update(&sc);
	CHECK(sc.sc_level == 4 && sc.sc_error == EIO);
	CHECK(sc.sc_temperature == INT_MIN);
	zone_error[3] = 0;
	sample(&sc, 40000, 0);
	zone_temperature[3] = INT_MIN;
	pwm_fan_update(&sc);
	CHECK(sc.sc_level == 4 && sc.sc_error == ERANGE);
	sample(&sc, 40000, 0);
	for (i = 0; i < 3; i++) {
		sample(&sc, 40000, 0);
		zone_temperature[3] = 65000;
		if (i == 0)
			fail_get = 1;
		else if (i == 1)
			fail_set = 1;
		else
			fail_enable = 1;
		pwm_fan_update(&sc);
		CHECK(sc.sc_error == EIO && sc.sc_level == 4);
		CHECK(config.duty_cycle == config.period);
		sample(&sc, 65000, 2);
	}
	sample(&sc, 40000, 0);
	zone_temperature[3] = 85000;
	fail_set = 2;
	pwm_fan_update(&sc);
	CHECK(sc.sc_error == EIO && sc.sc_level == 0);
	sample(&sc, 85000, 4);
	zone_temperature[3] = 40000;
	fail_set = 1;
	pwm_fan_update(&sc);
	CHECK(sc.sc_error == EIO && sc.sc_level == 4);
	before = pwm_sets;
	sample(&sc, 85000, 4);
	CHECK(pwm_sets == before + 1);

	config.period = UINT_MAX;
	CHECK(pwm_fan_set(&sc, 4) == 0);
	CHECK(config.duty_cycle == UINT_MAX);
	CHECK(pwm_fan_set(&sc, 2) == 0);
	CHECK(config.duty_cycle == (uint64_t)UINT_MAX * 102 / 255);
	CHECK(pwm_fan_set(&sc, 5) == EINVAL);
	config.period = 0;
	CHECK(pwm_fan_set(&sc, 1) == EINVAL && sc.sc_level == 2);
	config.period = 40000;
	sc.sc_config_error = EINVAL;
	pwm_fan_update(&sc);
	CHECK(sc.sc_error == EINVAL && sc.sc_level == 4);
	cleanup(&sc);
}

static void
parser_tests(void)
{
	struct pwm_fan_softc sc;
	uint32_t values[6];
	struct pwm_fan_trip extra;
	int i;

	for (i = 0; i < 7; i++) {
		fixture(&sc);
		if (i == 0)
			property(1, "cooling-levels")->len = 3;
		else if (i == 1)
			property(1, "cooling-levels")->len = 0;
		else if (i == 2)
			property(1, "cooling-levels")->len = 257 * 4;
		else {
			values[0] = i == 3 ? 1 : 0;
			values[1] = i == 4 ? 256 : 128;
			values[2] = i == 5 ? 127 : (i == 6 ? 254 : 255);
			words(1, "cooling-levels", values, 3);
		}
		CHECK(pwm_fan_levels(&sc, 1) == EINVAL);
		cleanup(&sc);
	}
	for (i = 0; i < 14; i++) {
		fixture(&sc);
		CHECK(pwm_fan_levels(&sc, 1) == 0);
		switch (i) {
		case 0:
			property(20, "cooling-device")->len = 11;
			break;
		case 1:
			property(20, "cooling-device")->len = 8;
			break;
		case 2:
			word(1, "#cooling-cells", UINT_MAX);
			break;
		case 3:
			values[0] = 1;
			values[1] = 1;
			values[2] = 2;
			words(20, "cooling-device", values, 3);
			break;
		case 4:
			word(20, "trip", 63);
			break;
		case 5:
			nodes[10].parent = 3;
			break;
		case 6:
			string_prop(10, "type", "passive");
			break;
		case 7:
			word(10, "hysteresis", UINT_MAX);
			break;
		case 8:
			word(10, "temperature", (uint32_t)-273151);
			break;
		case 9:
			word(10, "hysteresis", 400000);
			break;
		case 10:
			values[0] = 1;
			values[1] = values[2] = 3;
			words(23, "cooling-device", values, 3);
			break;
		case 11:
			word(10, "temperature", 95000);
			break;
		case 12:
			word(10, "temperature", 65000);
			break;
		case 13:
			property(10, "type")->len--;
			break;
		}

		CHECK(pwm_fan_parse(&sc, 1) == EINVAL);
		cleanup(&sc);
	}
	fixture(&sc);
	nodes[2].name = NULL;
	CHECK(pwm_fan_levels(&sc, 1) == 0 && pwm_fan_parse(&sc, 1) == 0);
	pwm_fan_update(&sc);
	CHECK(sc.sc_level == 4 && sample_count == 0);
	cleanup(&sc);
	fixture(&sc);
	nodes[3].disabled = true;
	CHECK(pwm_fan_levels(&sc, 1) == 0 && pwm_fan_parse(&sc, 1) == 0);
	pwm_fan_update(&sc);
	CHECK(sc.sc_level == 4 && sample_count == 0);
	cleanup(&sc);
	fixture(&sc);
	nodes[30].name = "other-cooler";
	word(30, "#cooling-cells", 2);
	values[0] = 30; values[1] = 0; values[2] = UINT_MAX;
	values[3] = 1; values[4] = values[5] = 1;
	words(20, "cooling-device", values, 6);
	CHECK(pwm_fan_levels(&sc, 1) == 0 && pwm_fan_parse(&sc, 1) == 0);
	sample(&sc, 55000, 1);
	/* A hotter second zone overrides the first without lowering its demand. */
	extra = (struct pwm_fan_trip){ .next = sc.sc_trips, .zone = 31,
	    .temperature = 80000, .hysteresis = 5000, .level = 4 };
	sc.sc_trips = &extra;
	zone_temperature[31] = 85000;
	pwm_fan_update(&sc);
	CHECK(sc.sc_level == 4 && sc.sc_temperature == 85000);
	zone_error[3] = EIO;
	zone_temperature[31] = 40000;
	pwm_fan_update(&sc);
	CHECK(sc.sc_error == EIO && sc.sc_level == 4 && extra.active);
	zone_error[3] = 0;
	pwm_fan_update(&sc);
	CHECK(sc.sc_level == 1 && !extra.active);
	sc.sc_trips = extra.next;
	cleanup(&sc);
}

int
main(void)
{

	policy_tests();
	power_tests();
	failure_tests();
	parser_tests();
	printf("PASS: %u production PWM fan checks\n", checks);
	return 0;
}
