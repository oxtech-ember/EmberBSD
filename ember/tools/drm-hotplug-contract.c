/* Origin: EmberBSD actual-source native DRM event contract. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

struct device {
	const char *name;
};
struct drm_minor {
	int index;
};
struct drm_device {
	bool registered;
	struct drm_minor *primary, *render;
	struct device *dev;
};
struct drm_connector {
	struct drm_device *dev;
};
struct drm_property {
	int unused;
};
struct dictionary {
	char device[32], parent[64];
};
typedef struct dictionary *prop_dictionary_t;
static unsigned checks, allocations, releases, calls, sets, assertions;
static unsigned fail_set;
static bool fail_create, backend_error, no_listener, hard_irq, soft_irq;
static prop_dictionary_t live;
static char last_device[32], last_parent[64];

static void
check(bool value, const char *expression, int line)
{
	checks++;
	if (!value) {
		fprintf(stderr, "drm-hotplug:%d: %s\n", line, expression);
		exit(1);
	}
}
#define CHECK(x) check((x), #x, __LINE__)
#define KASSERTMSG(x, ...) do { if (!(x)) assertions++; } while (0)

static bool
cpu_intr_p(void)
{
	return hard_irq;
}

static bool
cpu_softintr_p(void)
{
	return soft_irq;
}

static const char *
device_xname(const struct device *dev)
{
	CHECK(dev != NULL);
	return dev->name;
}

static prop_dictionary_t
prop_dictionary_create(void)
{
	CHECK(live == NULL);
	if (fail_create)
		return NULL;
	live = calloc(1, sizeof(*live));
	CHECK(live != NULL);
	allocations++;
	return live;
}

static bool
prop_dictionary_set_string(prop_dictionary_t dict, const char *key,
    const char *value)
{
	char *dst;
	size_t size;

	CHECK(dict != NULL && dict == live && value != NULL);
	sets++;
	if (sets == fail_set)
		return false;
	if (strcmp(key, "device") == 0) {
		dst = dict->device;
		size = sizeof(dict->device);
	} else {
		CHECK(strcmp(key, "parent") == 0);
		dst = dict->parent;
		size = sizeof(dict->parent);
	}
	CHECK(strlen(value) < size);
	memcpy(dst, value, strlen(value) + 1);
	return true;
}

static void
prop_object_release(prop_dictionary_t dict)
{
	CHECK(dict != NULL && dict == live);
	free(dict);
	live = NULL;
	releases++;
}

static int
insert_event(const char *event, prop_dictionary_t dict)
{
	CHECK(dict != NULL && dict == live);
	CHECK(strcmp(event, "device-change") == 0);
	CHECK(dict->device[0] != '\0' && dict->parent[0] != '\0');
	calls++;
	/* The default no_devmon_insert rejects without consuming the object. */
	if (backend_error)
		return ENODEV;
	if (!no_listener) {
		memcpy(last_device, dict->device, sizeof(last_device));
		memcpy(last_parent, dict->parent, sizeof(last_parent));
	}
	/* The drvctl backend consumes even when no listeners are present. */
	prop_object_release(dict);
	return 0;
}
static int (*devmon_insert_vec)(const char *, prop_dictionary_t) = insert_event;

#include "drm-hotplug-body.h"

static struct device parent = { .name = "display0" };
static struct drm_minor primary = { .index = 2 }, render = { .index = 128 };
static struct drm_device drm;

static void
reset(void)
{
	CHECK(live == NULL);
	allocations = releases = calls = sets = assertions = fail_set = 0;
	fail_create = backend_error = no_listener = hard_irq = soft_irq = false;
	memset(last_device, 0, sizeof(last_device));
	memset(last_parent, 0, sizeof(last_parent));
	primary.index = 2;
	drm = (struct drm_device){ .registered = true, .primary = &primary,
	    .render = &render, .dev = &parent };
}

static void
check_balanced(unsigned expected_calls, unsigned expected_allocations)
{
	CHECK(calls == expected_calls);
	CHECK(allocations == expected_allocations);
	CHECK(releases == expected_allocations && live == NULL);
}

int
main(void)
{
	struct drm_connector connector = { .dev = &drm };
	struct drm_property property = { 0 };

	CHECK(devmon_insert_vec == insert_event);
	reset();
	drm_sysfs_hotplug_event(&drm);
	check_balanced(1, 1);
	CHECK(strcmp(last_device, "dri/card2") == 0);
	CHECK(strcmp(last_parent, "display0") == 0);
	reset();
	drm_sysfs_connector_status_event(&connector, &property);
	check_balanced(1, 1);
	CHECK(strcmp(last_device, "dri/card2") == 0);
	reset();
	drm_sysfs_connector_status_event(&connector, NULL);
	check_balanced(1, 1);
	reset();
	drm_sysfs_connector_status_event(NULL, NULL);
	drm_sysfs_hotplug_event(NULL);
	check_balanced(0, 0);
	reset();
	drm.registered = false;
	drm_sysfs_hotplug_event(&drm);
	check_balanced(0, 0);
	reset();
	drm.primary = NULL;
	drm_sysfs_hotplug_event(&drm);
	check_balanced(0, 0);
	reset();
	drm.dev = NULL;
	drm_sysfs_hotplug_event(&drm);
	check_balanced(0, 0);
	reset();
	primary.index = -1;
	drm_sysfs_hotplug_event(&drm);
	check_balanced(0, 0);
	reset();
	primary.index = INT_MAX;
	drm_sysfs_hotplug_event(&drm);
	check_balanced(1, 1);
	CHECK(strcmp(last_device, "dri/card2147483647") == 0);
	reset();
	fail_create = true;
	drm_sysfs_hotplug_event(&drm);
	check_balanced(0, 0);
	for (unsigned i = 1; i <= 2; i++) {
		reset();
		fail_set = i;
		drm_sysfs_hotplug_event(&drm);
		check_balanced(0, 1);
	}
	reset();
	backend_error = true;
	drm_sysfs_hotplug_event(&drm);
	check_balanced(1, 1);
	reset();
	no_listener = true;
	drm_sysfs_hotplug_event(&drm);
	check_balanced(1, 1);
	CHECK(last_device[0] == '\0');
	reset();
	hard_irq = true;
	drm_sysfs_hotplug_event(&drm);
	check_balanced(0, 0);
	CHECK(assertions == 1);
	reset();
	soft_irq = true;
	drm_sysfs_hotplug_event(&drm);
	check_balanced(0, 0);
	CHECK(assertions == 1);
	printf("PASS: %u native DRM publication and ownership checks\n", checks);
	return 0;
}
