/* SPDX-License-Identifier: BSD-2-Clause */
/* Run the real driver functions with controlled ACPI providers. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define ACPI_TYPE_PACKAGE 1
#define ACPI_FAILURE(rv) ((rv) != 0)
#define ACPI_DEBUG_PRINT(args) ((void)0)
#define ENVSYS_SINVALID 0
#define ENVSYS_SVALID 1
#define ENVSYS_SCRITOVER 2
#define OSL_NOTIFY_HANDLER 0
#define atomic_load_relaxed(p) (*(p))
#define atomic_store_relaxed(p, v) (*(p) = (v))
#define aprint_normal_dev(...) ((void)0)
#define aprint_verbose_dev(...) ((void)0)
#define aprint_verbose(...) do { if (false) printf(__VA_ARGS__); } while (0)
#define aprint_debug_dev(...) ((void)0)
#define SYSCTLFN_ARGS const struct sysctlnode *rnode, const int *newp
#define SYSCTLFN_CALL(node) (node), newp

typedef void *ACPI_HANDLE;
typedef int ACPI_STATUS;
typedef struct acpi_object ACPI_OBJECT;
struct acpi_object {
	int Type;
	struct {
		unsigned int Count;
		ACPI_OBJECT *Elements;
	} Package;
	ACPI_HANDLE reference;
};
typedef struct {
	void *Pointer;
} ACPI_BUFFER;
typedef struct {
	int state;
	uint64_t value_cur;
} envsys_data_t;
struct acpi_devnode {
	ACPI_HANDLE ad_handle;
};
struct callout { int unused; };
struct sysmon_envsys;
struct sysctllog;
struct cpu_info;
struct acpitz_softc;
typedef struct acpitz_softc *device_t;
struct sysctlnode {
	const char *sysctl_name;
	void *sysctl_data;
};

#include "acpitz-types.h"

static unsigned int checks, allocations, power_on, power_off;
static unsigned int bad_list, bad_trip;
static bool failed_sample, failed_queue, changed_resource;
static uint32_t sample;
static int devices[ATZ_NLEVELS + 1];
static const uint32_t trips[] = { 3482, 3407, 3332, 3232 };
static struct acpi_devnode node;

#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void
acpi_free(void *p)
{
	assert(p != NULL && allocations > 0);
	allocations--;
	free(p);
}
#define ACPI_FREE(p) acpi_free(p)

static struct acpitz_softc *
device_private(device_t dv)
{
	return dv;
}

static int
acpitz_get_integer(device_t dv, const char *name, uint32_t *value)
{
	(void)dv;
	*value = ATZ_TMP_INVALID;
	if (strcmp(name, "_TMP") == 0) {
		if (failed_sample)
			return 1;
		*value = sample;
	} else if (strncmp(name, "_AC", 3) == 0) {
		unsigned int i = (unsigned int)(name[3] - '0');
		if (i >= 4 || (bad_trip & (1U << i)) != 0)
			return 1;
		*value = trips[i];
	} else if (strcmp(name, "_CRT") == 0) {
		*value = 3832;
	} else if (strcmp(name, "_PSV") == 0) {
		*value = 3532;
	} else if (strcmp(name, "_TC1") == 0 || strcmp(name, "_TC2") == 0 ||
	    strcmp(name, "_RTV") == 0) {
		*value = 0;
	} else {
		return 1;
	}
	return 0;
}

static int
acpitz_get_fanspeed(device_t dv, uint32_t *a, uint32_t *b, uint32_t *c)
{
	(void)dv;
	(void)a;
	(void)b;
	(void)c;
	return 1;
}

static ACPI_STATUS
acpi_eval_struct(ACPI_HANDLE handle, const char *name, ACPI_BUFFER *buffer)
{
	ACPI_OBJECT *obj;
	unsigned int i = (unsigned int)(name[3] - '0');

	(void)handle;
	assert(strncmp(name, "_AL", 3) == 0 && i < 4);
	if ((bad_list & (1U << i)) != 0)
		return 1;
	obj = calloc(2, sizeof(*obj));
	assert(obj != NULL);
	allocations++;
	obj->Type = ACPI_TYPE_PACKAGE;
	obj->Package.Count = 1;
	obj->Package.Elements = obj + 1;
	obj[1].reference = &devices[changed_resource && i == 0 ? 10 : i];
	buffer->Pointer = obj;
	return 0;
}

static ACPI_STATUS
acpi_eval_reference_handle(ACPI_OBJECT *obj, ACPI_HANDLE *handle)
{
	*handle = obj->reference;
	return *handle == NULL ? 1 : 0;
}

static void
acpitz_power_zone(struct acpitz_softc *sc, int i, int on)
{
	assert(i >= 0 && i < ATZ_NLEVELS);
	assert(sc->sc_zone.al[i].Pointer != NULL);
	if (on)
		power_on++;
	else
		power_off++;
}

static int
sysctl_lookup(SYSCTLFN_ARGS)
{
	if (newp != NULL)
		*(int *)rnode->sysctl_data = *newp;
	return 0;
}

static ACPI_STATUS
AcpiOsExecute(int type, void (*func)(void *), void *arg)
{
	(void)type;
	if (failed_queue)
		return 1;
	func(arg);
	return 0;
}

static const char *
acpitz_celcius_string(int value)
{
	(void)value;
	return "temperature";
}

static void acpitz_get_status(void *);
static void acpitz_get_zone(void *, int);
static void acpitz_set_active(struct acpitz_softc *, int);
static void acpitz_sane_temp(uint32_t *);
static void acpitz_power_off(struct acpitz_softc *);

#include "acpitz-control.h"

static void
at(struct acpitz_softc *sc, unsigned int tenth_c, int active)
{
	sample = ATZ_ZEROC + tenth_c;
	acpitz_get_status(sc);
	CHECK(sc->sc_active == active);
}

static int
policy(struct acpitz_softc *sc, const char *name, int value)
{
	struct sysctlnode ctl = { name, sc };

	return acpitz_sysctl_fan(&ctl, &value);
}

int
main(void)
{
	struct acpitz_softc sc;
	unsigned int on, off;
	int i;

	memset(&sc, 0, sizeof(sc));
	sc.sc_node = &node;
	sc.sc_first = true;
	sc.sc_active = ATZ_ACTIVE_NONE;
	sc.sc_zone.tzp = 10;
	sc.sc_zone_expire = ATZ_ZONE_EXPIRE / 10;
	sample = ATZ_ZEROC + 400;
	acpitz_get_zone(&sc, 1);
	CHECK(allocations == 4);
	at(&sc, 499, ATZ_ACTIVE_NONE);
	at(&sc, 500, 3);
	at(&sc, 450, 3);
	at(&sc, 449, ATZ_ACTIVE_NONE);
	at(&sc, 600, 2);
	at(&sc, 675, 1);
	at(&sc, 750, 0);
	at(&sc, 400, ATZ_ACTIVE_NONE);

	CHECK(policy(&sc, "fan_max_only", -1) == EINVAL);
	CHECK(policy(&sc, "fan_max_only", 2) == EINVAL);
	CHECK(sc.sc_fan_max_only == 0);
	failed_queue = true;
	CHECK(policy(&sc, "fan_max_only", 1) == EIO);
	CHECK(sc.sc_fan_max_only == 0);
	failed_queue = false;
	CHECK(policy(&sc, "fan_max_only", 1) == 0);
	at(&sc, 499, ATZ_ACTIVE_NONE);
	at(&sc, 500, 0);
	at(&sc, 490, 0);
	at(&sc, 450, 0);

	/* Fresh AML allocations for the same devices must not stop the fan. */
	on = power_on;
	off = power_off;
	acpitz_get_zone_quiet(&sc);
	CHECK(sc.sc_active == 0 && sc.sc_fan_latched);
	CHECK(power_on == on && power_off == off && allocations == 4);
	sc.sc_zone_expire = 1;
	at(&sc, 470, 0);
	CHECK(power_on == on && power_off == off && allocations == 4);
	at(&sc, 449, ATZ_ACTIVE_NONE);
	CHECK(!sc.sc_fan_latched);
	at(&sc, 480, ATZ_ACTIVE_NONE);
	at(&sc, 500, 0);

	/* A replacement device must retain the cooling latch below 50 C. */
	at(&sc, 480, 0);
	changed_resource = true;
	acpitz_get_zone_quiet(&sc);
	CHECK(sc.sc_active == 0 && sc.sc_fan_latched);
	changed_resource = false;
	acpitz_get_zone_quiet(&sc);
	CHECK(sc.sc_active == 0 && sc.sc_fan_latched);

	/* Failed refreshes retain the previous cooling resources and trips. */
	off = power_off;
	bad_list = 1;
	acpitz_get_zone_quiet(&sc);
	CHECK(sc.sc_active == 0 && power_off == off && allocations == 4);
	bad_list = 0;
	bad_trip = 1U << 3;
	acpitz_get_zone_quiet(&sc);
	CHECK(sc.sc_active == 0 && power_off == off && allocations == 4);
	CHECK(sc.sc_zone.ac[3] == trips[3]);
	bad_trip = 0;
	at(&sc, 400, ATZ_ACTIVE_NONE);
	failed_sample = true;
	acpitz_get_status(&sc);
	CHECK(sc.sc_active == 0 && sc.sc_fan_latched);
	CHECK(sc.sc_temp_sensor.state == ENVSYS_SINVALID);
	bad_list = 1;
	off = power_off;
	acpitz_get_zone_quiet(&sc);
	CHECK(sc.sc_active == 0 && power_off == off && allocations == 4);
	failed_sample = false;
	bad_list = 0;
	at(&sc, 470, 0);
	at(&sc, 440, ATZ_ACTIVE_NONE);
	sample = ATZ_TMP_INVALID;
	acpitz_get_status(&sc);
	CHECK(sc.sc_active == 0 && sc.sc_temp_sensor.state == ENVSYS_SINVALID);

	at(&sc, 1100, 0);
	CHECK((sc.sc_flags & (ATZ_F_CRITICAL | ATZ_F_PASSIVE)) ==
	    (ATZ_F_CRITICAL | ATZ_F_PASSIVE));
	CHECK(sc.sc_temp_sensor.state == ENVSYS_SCRITOVER);
	at(&sc, 400, ATZ_ACTIVE_NONE);
	CHECK(policy(&sc, "force_fan", 1) == 0);
	at(&sc, 400, 0);
	on = power_on;
	off = power_off;
	acpitz_get_zone_quiet(&sc);
	CHECK(power_on == on && power_off == off && sc.sc_active == 0);
	CHECK(policy(&sc, "fan_max_only", 0) == 0);
	CHECK(sc.sc_force_fan == 1 && sc.sc_active == 0);
	on = power_on;
	off = power_off;
	acpitz_get_zone_quiet(&sc);
	CHECK(power_on == on && power_off == off && sc.sc_active == 0);
	changed_resource = true;
	acpitz_get_zone_quiet(&sc);
	CHECK(power_off > off && power_on == on + 1 && sc.sc_active == 0);
	CHECK(policy(&sc, "force_fan", -1) == EINVAL);
	CHECK(policy(&sc, "force_fan", 2) == EINVAL);
	CHECK(policy(&sc, "force_fan", 0) == 0);
	at(&sc, 500, 3);
	at(&sc, 400, ATZ_ACTIVE_NONE);
	for (i = 0; i < ATZ_NLEVELS; i++) {
		if (sc.sc_zone.al[i].Pointer != NULL)
			ACPI_FREE(sc.sc_zone.al[i].Pointer);
	}
	CHECK(allocations == 0);
	printf("PASS: %u thermal policy and refresh checks\n", checks);
	return 0;
}
