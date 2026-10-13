/* Origin: EmberBSD; AI-assisted tests of production FDT power attachment. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/queue.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <dt-bindings/power/rk3399-power.h>

typedef unsigned int u_int;
typedef void *device_t;
typedef void *cfdata_t;
typedef unsigned int devhandle_t;
typedef void *prop_dictionary_t;
struct fdt_attach_args {
	int faa_phandle;
	bool faa_quiet;
};
#define FDTCF_PASS 0
#define FDTCF_NLOCS 1
#define FDTCF_PASS_DEFAULT 9
#define FDT_MAX_PATH 256
#define KM_SLEEP 0
#define aprint_debug_dev(...) ((void)0)
#include "nodes.h"
#include "interface.h"
static TAILQ_HEAD(, fdt_node) fdt_nodes = TAILQ_HEAD_INITIALIZER(fdt_nodes);
static struct fdt_node node;
static struct fdt_softc bus;
static uint32_t specifier[4];
static uint32_t provider_cells[4];
static bool provider_cells_missing[4];
static bool property_present, pin_init, late_match, attach_fails;
static bool managed_power;
static int managed_length;
static bool match_available, match_requires_provider;
static int property_length, checked_error, pin_error, last_error;
static unsigned int checked_calls, legacy_calls, power_disables;
static unsigned int attach_calls, found_calls, post_calls, pin_calls;
static unsigned int init_calls, errors, diagnostics, checks;
static unsigned int search_calls;
static char event_log[128];
static size_t event_count;
static void
record(char e)
{

	assert(event_count + 1 < sizeof(event_log));
	event_log[event_count++] = e;
	event_log[event_count] = '\0';
}
static uint32_t
swap32(uint32_t val)
{
	const uint32_t test = 1;

	return *(const unsigned char *)&test == 1 ? __builtin_bswap32(val) : val;
}
#ifndef be32toh
#define be32toh(v) swap32(v)
#endif
static void *
kmem_alloc(size_t size, int flags)
{

	return calloc(1, size);
}

static int
of_getprop_uint32(int phandle, const char *name, uint32_t *value)
{

	assert(strcmp(name, "#power-domain-cells") == 0);
	if (phandle < 2 || phandle > 3 || provider_cells_missing[phandle])
		return -1;
	*value = provider_cells[phandle];
	return 0;
}
static const void *
fdtbus_get_prop(int phandle, const char *name, int *len)
{

	*len = property_present ? property_length : -1;
	return property_present ? specifier : NULL;
}
static bool
of_hasprop(int phandle, const char *name)
{

	if (strcmp(name, "netbsd,consumer-managed-power") == 0)
		return managed_power;
	assert(strcmp(name, "power-domains") == 0);
	return property_present;
}
static int
OF_getproplen(int phandle, const char *name)
{

	assert(strcmp(name, "netbsd,consumer-managed-power") == 0);
	return managed_power ? managed_length : -1;
}
static int
fdtbus_get_phandle_from_native(int phandle)
{

	return phandle >= 2 && phandle <= 3 ? phandle : -1;
}

#include "power.h"
static int
checked(device_t dev, const uint32_t *pd, bool enable)
{

	checked_calls++;
	power_disables += !enable;
	record('P');
	return checked_error;
}
static void
legacy(device_t dev, const uint32_t *pd, bool enable)
{

	legacy_calls++;
	power_disables += !enable;
	record('L');
}
static void
error_message(device_t dev, const char *fmt, const char *name, int e)
{

	assert(strstr(fmt, "failed to enable power domains") != NULL ||
	    strstr(fmt, "invalid consumer-managed power property") != NULL);
	assert(strcmp(name, "test-device") == 0);
	last_error = e;
	errors++;
	record('E');
}
#define aprint_error_dev error_message
static void
fdt_init_attach_args(struct fdt_attach_args *tmpl,
    struct fdt_node *n, bool quiet, struct fdt_attach_args *faa)
{

	*faa = *tmpl;
	faa->faa_phandle = n->n_phandle;
	faa->faa_quiet = quiet;
	init_calls++;
}
static bool
fdtbus_pinctrl_has_config(int phandle, const char *name)
{

	return pin_init;
}
static int
fdtbus_pinctrl_set_config(int phandle, const char *name)
{

	pin_calls++;
	record(strcmp(name, "init") == 0 ? 'I' : 'D');
	return pin_error;
}
static devhandle_t
device_handle(device_t dev)
{

	return 1;
}

static devhandle_t
devhandle_from_of(devhandle_t handle, int phandle)
{

	return phandle;
}

static int
fdtbus_print(void *aux, const char *name)
{

	return 0;
}

static int
fdt_scan_submatch(device_t dev, cfdata_t cf, const int *locs, void *aux)
{

	return node.n_cf != NULL || late_match;
}
struct fixture_cfargs {
	const int *locators;
	devhandle_t devhandle;
	int (*submatch)(device_t, cfdata_t, const int *, void *);
	const char *iattr;
};
#define CFARGS(...) (&(struct fixture_cfargs){ __VA_ARGS__ })
static cfdata_t
config_search(device_t parent, void *aux, struct fixture_cfargs *args)
{
	struct fdt_attach_args *faa = aux;

	assert(faa->faa_quiet);
	search_calls++;
	if (!match_available ||
	    (match_requires_provider && fdtbus_powerdomain_lookup(2) == NULL))
		return NULL;
	return &bus;
}

static int
config_match(device_t parent, cfdata_t cf, void *aux)
{

	return 1;
}

static device_t
config_attach(device_t parent, cfdata_t cf, void *aux,
    int (*print)(void *, const char *), struct fixture_cfargs *args)
{

	attach_calls++;
	record('A');
	return attach_fails ? NULL : (device_t)&bus;
}
static device_t
config_found(device_t parent, void *aux,
    int (*print)(void *, const char *), struct fixture_cfargs *args)
{
	struct fdt_attach_args *faa = aux;

	assert(!faa->faa_quiet);
	found_calls++;
	if (!args->submatch(parent, (cfdata_t)&bus, args->locators, aux)) {
		diagnostics++;
		record('N');
		return NULL;
	}
	record('F');
	return attach_fails ? NULL : (device_t)&bus;
}
static prop_dictionary_t
device_properties(device_t dev)
{

	post_calls++;
	record('O');
	return NULL;
}
static bool
fdtbus_get_path(int phandle, char *buf, size_t size)
{

	return false;
}

static void
prop_dictionary_set_string(prop_dictionary_t dict,
    const char *key, const char *value)
{
}
static int fdt_pre_attach(struct fdt_node *);
static void fdt_post_attach(struct fdt_node *);
#include "attach.h"
#define CHECK(c) do { assert(c); checks++; } while (0)

static void
reset(int pass)
{
	struct fdtbus_powerdomain_controller *pdc;

	while ((pdc = LIST_FIRST(&fdtbus_powerdomain_controllers)) != NULL) {
		LIST_REMOVE(pdc, pdc_next);
		free(pdc);
	}
	TAILQ_INIT(&fdt_nodes);
	memset(&node, 0, sizeof(node));
	node.n_bus = &bus;
	node.n_name = "test-device";
	node.n_phandle = 1;
	node.n_cf = &bus;
	node.n_cfpass = pass;
	TAILQ_INSERT_TAIL(&fdt_nodes, &node, n_nodes);
	property_present = pin_init = true;
	late_match = attach_fails = false;
	match_available = match_requires_provider = false;
	property_length = 8;
	specifier[0] = swap32(2);
	specifier[1] = swap32(4);
	specifier[2] = swap32(3);
	specifier[3] = swap32(5);
	provider_cells[2] = provider_cells[3] = 1;
	provider_cells_missing[2] = provider_cells_missing[3] = false;
	checked_error = pin_error = last_error = 0;
	managed_power = false;
	managed_length = 0;
	checked_calls = legacy_calls = power_disables = 0;
	attach_calls = found_calls = post_calls = pin_calls = 0;
	init_calls = errors = diagnostics = 0;
	search_calls = 0;
	event_count = 0;
	event_log[0] = '\0';
}
static void
add_provider(int phandle, bool old)
{
	static const struct fdtbus_powerdomain_controller_func checked_funcs = {
		.pdc_set = checked,
	};
	static const struct fdtbus_powerdomain_controller_func legacy_funcs = {
		.pdc_enable = legacy,
	};

	CHECK(fdtbus_register_powerdomain_controller(NULL, phandle,
	    old ? &legacy_funcs : &checked_funcs) == 0);
}
static void
unattached(int expected_error)
{

	CHECK(node.n_dev == NULL);
	CHECK(attach_calls == 0 && found_calls == 0 && post_calls == 0);
	CHECK(errors == 1 && last_error == expected_error);
	CHECK(power_disables == 0);
	CHECK(pin_calls == 1); /* Existing pre-attach pinctrl ordering. */
}

int
main(void)
{
	const int failures[] = { EINVAL, ENXIO, EIO, ETIMEDOUT };
	const int bad_lengths[] = { 0, 3, 4, 7 };
	const int rk3399_domains[] = { RK3399_PD_SD, RK3399_PD_EMMC };

	for (int mode = 0; mode < 2; mode++) {
		const int pass = mode ? FDTCF_PASS_DEFAULT : 3;

		for (u_int i = 0; i < sizeof(failures) / sizeof(int); i++) {
			reset(pass);
			managed_power = true;
			add_provider(2, false);
			checked_error = failures[i];
			fdt_scan(&bus, pass);
			CHECK(node.n_dev != NULL && post_calls == 1);
			CHECK(checked_calls == 0 && legacy_calls == 0 && errors == 0);
			CHECK(strcmp(event_log, mode ? "IFOD" : "IAOD") == 0);
		}
		reset(pass);
		managed_power = true;
		managed_length = 4;
		add_provider(2, true);
		fdt_scan(&bus, pass);
		unattached(EINVAL);
		CHECK(checked_calls == 0 && legacy_calls == 0);
		/* Removing the explicit opt-in restores the legacy operation. */
		managed_power = false;
		fdt_scan(&bus, pass);
		CHECK(node.n_dev != NULL && legacy_calls == 1 && post_calls == 1);

		reset(pass);
		property_present = false;
		fdt_scan(&bus, pass);
		CHECK(node.n_dev != NULL && post_calls == 1);
		CHECK(checked_calls == 0 && legacy_calls == 0 && errors == 0);
		CHECK(attach_calls == (mode ? 0U : 1U));
		CHECK(found_calls == (mode ? 1U : 0U));
		CHECK(strcmp(event_log, mode ? "IFOD" : "IAOD") == 0);

		for (u_int i = 0; i < sizeof(bad_lengths) / sizeof(int); i++) {
			reset(pass);
			property_length = bad_lengths[i];
			add_provider(2, false);
			fdt_scan(&bus, pass);
			unattached(EINVAL);
			CHECK(checked_calls == 0);
		}
		/* rk3399.dtsi SD/eMMC use a one-cell, unsupported provider. */
		for (u_int i = 0; i < sizeof(rk3399_domains) / sizeof(int); i++) {
			reset(pass);
			specifier[1] = swap32(rk3399_domains[i]);
			CHECK(fdtbus_powerdomain_enable(1) == ENXIO);
			CHECK(fdtbus_powerdomain_enable_index(1, 0) == ENXIO);
			CHECK(fdtbus_powerdomain_disable(1) == ENXIO);
			CHECK(fdtbus_powerdomain_disable_index(1, 0) == ENXIO);
			fdt_scan(&bus, pass);
			CHECK(node.n_dev != NULL && post_calls == 1 && errors == 0);
			CHECK(checked_calls == 0 && legacy_calls == 0);
			CHECK(strcmp(event_log, mode ? "IFOD" : "IAOD") == 0);
			/* Registration does not reattach a successful firmware user. */
			add_provider(2, false);
			fdt_scan(&bus, pass);
			CHECK(post_calls == 1 && checked_calls == 0 && pin_calls == 2);
			/* A subsequent explicit request now reaches the provider. */
			CHECK(fdtbus_powerdomain_enable(1) == 0);
			CHECK(checked_calls == 1);
		}

		/* Unsupported providers still require a well-formed DT record. */
		for (u_int i = 0; i < sizeof(bad_lengths) / sizeof(int); i++) {
			reset(pass);
			property_length = bad_lengths[i];
			fdt_scan(&bus, pass);
			unattached(EINVAL);
			CHECK(checked_calls == 0 && legacy_calls == 0);
		}
		for (int invalid = 0; invalid < 3; invalid++) {
			reset(pass);
			if (invalid == 0)
				provider_cells_missing[2] = true;
			else if (invalid == 1)
				provider_cells[2] = UINT32_MAX;
			else
				specifier[0] = swap32(99); /* Invalid phandle. */
			fdt_scan(&bus, pass);
			unattached(EINVAL);
		}
		reset(pass);
		provider_cells[2] = 0;
		property_length = 4;
		fdt_scan(&bus, pass);
		CHECK(node.n_dev != NULL && post_calls == 1 && errors == 0);
		CHECK(checked_calls == 0 && legacy_calls == 0);

		for (u_int i = 0; i < sizeof(failures) / sizeof(int); i++) {
			reset(pass);
			add_provider(2, false);
			checked_error = failures[i];
			fdt_scan(&bus, pass);
			unattached(failures[i]);
			CHECK(checked_calls == 1 && strcmp(event_log, "IPE") == 0);
			checked_error = 0;
			fdt_scan(&bus, pass);
			CHECK(node.n_dev != NULL && post_calls == 1);
			CHECK(checked_calls == 2 && errors == 1 && power_disables == 0);
		}
		reset(pass);
		add_provider(2, true);
		fdt_scan(&bus, pass);
		CHECK(node.n_dev != NULL && legacy_calls == 1 && checked_calls == 0);
		CHECK(strcmp(event_log, mode ? "ILFOD" : "ILAOD") == 0);

		reset(pass);
		add_provider(2, true);
		add_provider(3, false);
		property_length = 16;
		checked_error = ETIMEDOUT;
		fdt_scan(&bus, pass);
		unattached(ETIMEDOUT);
		CHECK(legacy_calls == 1 && checked_calls == 1);
		CHECK(strcmp(event_log, "ILPE") == 0); /* No speculative rollback. */

		reset(pass);
		add_provider(2, true);
		add_provider(3, false);
		property_length = 12; /* First domain valid, second truncated. */
		fdt_scan(&bus, pass);
		unattached(EINVAL);
		CHECK(legacy_calls == 1 && checked_calls == 0);
		CHECK(strcmp(event_log, "ILE") == 0);

		/* Missing first provider must not hide a checked second error. */
		for (u_int i = 0; i < sizeof(failures) / sizeof(int); i++) {
			reset(pass);
			add_provider(3, false);
			property_length = 16;
			checked_error = failures[i];
			fdt_scan(&bus, pass);
			unattached(failures[i]);
			CHECK(checked_calls == 1 && strcmp(event_log, "IPE") == 0);
			checked_error = 0;
			add_provider(2, true); /* Late registration participates. */
			fdt_scan(&bus, pass);
			CHECK(node.n_dev != NULL && post_calls == 1);
			CHECK(legacy_calls == 1 && checked_calls == 2);
			CHECK(power_disables == 0);
		}

		/* A trailing unsupported domain preserves earlier transitions. */
		reset(pass);
		add_provider(2, true);
		property_length = 16;
		fdt_scan(&bus, pass);
		CHECK(node.n_dev != NULL && post_calls == 1 && legacy_calls == 1);
		CHECK(checked_calls == 0 && power_disables == 0);
		reset(pass);
		add_provider(2, true);
		property_length = 12; /* Unsupported second record is truncated. */
		fdt_scan(&bus, pass);
		unattached(EINVAL);
		CHECK(legacy_calls == 1 && checked_calls == 0);
		CHECK(strcmp(event_log, "ILE") == 0);

		/* Skipping one provider must not skip later malformed records. */
		reset(pass);
		property_length = 12;
		fdt_scan(&bus, pass);
		unattached(EINVAL);
		CHECK(legacy_calls == 0 && checked_calls == 0);

		reset(pass);
		add_provider(3, false);
		provider_cells[2] = 0;
		specifier[1] = swap32(3);
		specifier[2] = swap32(5);
		property_length = 12; /* Zero-cell fallback, then one-cell provider. */
		fdt_scan(&bus, pass);
		CHECK(node.n_dev != NULL && post_calls == 1 && checked_calls == 1);
		CHECK(strcmp(event_log, mode ? "IPFOD" : "IPAOD") == 0);

		reset(pass);
		add_provider(2, false);
		pin_init = false;
		pin_error = ENOENT;
		fdt_scan(&bus, pass);
		CHECK(node.n_dev != NULL && post_calls == 1 && pin_calls == 1);
		CHECK(errors == 0); /* No pinctrl configuration remains nonfatal. */

		reset(pass);
		add_provider(2, false);
		attach_fails = true;
		fdt_scan(&bus, pass);
		CHECK(node.n_dev == NULL && post_calls == 0 && checked_calls == 1);
		CHECK(power_disables == 0 && pin_calls == 1);

		reset(pass);
		node.n_cf = NULL;
		late_match = true; /* config_found must not bypass pre-attach. */
		fdt_scan(&bus, pass);
		CHECK(node.n_dev == NULL && post_calls == 0 && pin_calls == 0);
		CHECK(checked_calls == 0 && legacy_calls == 0 && errors == 0);
		CHECK(found_calls == (mode ? 1U : 0U));
		CHECK(diagnostics == (mode ? 1U : 0U));
		node.n_cf = &bus; /* A later rescan selects a real match. */
		add_provider(2, false);
		fdt_scan(&bus, pass);
		CHECK(node.n_dev != NULL && checked_calls == 1 && post_calls == 1);
	}

	/* A provider attaches after the initial match but before default pass. */
	reset(FDTCF_PASS_DEFAULT);
	match_available = match_requires_provider = true;
	fdt_scan_best(&bus, &node);
	CHECK(node.n_cf == NULL && node.n_cfpass == FDTCF_PASS_DEFAULT);
	CHECK(search_calls == FDTCF_PASS_DEFAULT + 1);
	add_provider(2, false);
	fdt_scan(&bus, FDTCF_PASS_DEFAULT);
	CHECK(node.n_dev != NULL && node.n_cf != NULL && post_calls == 1);
	CHECK(search_calls == FDTCF_PASS_DEFAULT + 2);
	CHECK(found_calls == 1 && checked_calls == 1 && diagnostics == 0);
	CHECK(strcmp(event_log, "IPFOD") == 0);
	CHECK(node.n_cfpass == FDTCF_PASS_DEFAULT);

	/* A newly matched device cannot bypass the power-domain error gate. */
	for (u_int i = 0; i < sizeof(failures) / sizeof(int); i++) {
		reset(FDTCF_PASS_DEFAULT);
		match_available = match_requires_provider = true;
		fdt_scan_best(&bus, &node);
		CHECK(node.n_cf == NULL);
		add_provider(2, false);
		checked_error = failures[i];
		fdt_scan(&bus, FDTCF_PASS_DEFAULT);
		unattached(failures[i]);
		CHECK(node.n_cf != NULL && node.n_cfpass == FDTCF_PASS_DEFAULT);
		CHECK(strcmp(event_log, "IPE") == 0);
		checked_error = 0;
		fdt_scan(&bus, FDTCF_PASS_DEFAULT);
		CHECK(node.n_dev != NULL && post_calls == 1 && checked_calls == 2);
		CHECK(search_calls == FDTCF_PASS_DEFAULT + 2);
	}

	reset(3); /* Release the fixture providers for sanitizer checking. */
	printf("FDT power attach: %u production checks passed\n", checks);
	return 0;
}
