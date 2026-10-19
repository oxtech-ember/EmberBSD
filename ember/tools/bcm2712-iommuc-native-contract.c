/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual native/query/core sources. Controlled kernel services only. */
#define main descriptor_contract_main
#define aprint_normal_dev descriptor_aprint_normal_dev
#define bcm2712_iommuc_acpi_resources descriptor_resources
#include "bcm2712-iommuc-contract.c"
#undef bcm2712_iommuc_acpi_resources
#undef aprint_normal_dev
#undef main

#include "core-types.h"
#include "native-types.h"

typedef void *bus_space_tag_t;
typedef void *cfdata_t;
typedef struct { bool initialized, held; } kmutex_t;
struct fixture_device { void *private; const char *driver; };
struct acpi_devnode {
	ACPI_HANDLE ad_handle;
	ACPI_OBJECT_TYPE ad_type;
	device_t ad_device;
};
struct acpi_attach_args {
	struct acpi_devnode *aa_node;
	bus_space_tag_t aa_memt;
};
struct fixture_attachment {
	size_t size;
	int (*match)(device_t, cfdata_t, void *);
	void (*attach)(device_t, device_t, void *);
	int (*detach)(device_t, int);
};
#define CFATTACH_DECL_NEW(n, s, m, a, d, act) \
	static const struct fixture_attachment n##_ca = { s, m, a, d }
#define ONCE_DECL(n) int n
#define RUN_ONCE(o, f) fixture_once(o, f)
#define MUTEX_DEFAULT 0
#define IPL_NONE 0

static unsigned int decoder_calls, lock_calls, unlock_calls, node_calls;
static unsigned int print_calls, error_prints, once_calls;
static int once_error;
static struct acpi_devnode namespace_node;
static bool missing_node;
static int tag_token, foreign_token;

static int bcm2712_iommuc_acpi_resources(ACPI_HANDLE handle,
    struct bcm2712_iommuc_contract *out)
{
	decoder_calls++;
	return descriptor_resources(handle, out);
}
static int fixture_once(int *once, int (*init)(void))
{
	once_calls++;
	if (once_error != 0)
		return once_error;
	if (*once == 0) {
		int error = init();
		if (error != 0)
			return error;
		*once = 1;
	}
	return 0;
}
static void mutex_init(kmutex_t *lock, int type, int ipl)
{
	CHECK(type == MUTEX_DEFAULT && ipl == IPL_NONE && !lock->initialized);
	lock->initialized = true;
}
static void mutex_enter(kmutex_t *lock)
{
	CHECK(lock->initialized && !lock->held && !hard_irq && !soft_irq);
	lock->held = true;
	lock_calls++;
}
static void mutex_exit(kmutex_t *lock)
{
	CHECK(lock->initialized && lock->held);
	lock->held = false;
	unlock_calls++;
}
static void *device_private(device_t dev)
{
	return dev == NULL ? NULL : ((struct fixture_device *)dev)->private;
}
static bool device_is_a(device_t dev, const char *name)
{
	CHECK(dev != NULL);
	return strcmp(((struct fixture_device *)dev)->driver, name) == 0;
}
static struct acpi_devnode *acpi_match_node(ACPI_HANDLE handle)
{
	node_calls++;
	CHECK(handle == CACHE);
	return missing_node ? NULL : &namespace_node;
}
static void aprint_normal_dev(device_t dev, const char *format, ...)
{
	CHECK(dev != NULL && format != NULL);
	print_calls++;
}
static void aprint_error_dev(device_t dev, const char *format, ...)
{
	CHECK(dev != NULL && format != NULL);
	error_prints++;
}
#include "core.h"
#include "native.h"

static struct bcm2712_iommuc_provider storage, duplicate, foreign_storage;
static struct fixture_device device = { &storage, "bcm2712iommuc" };
static struct fixture_device other_device = { &duplicate, "bcm2712iommuc" };
static struct acpi_attach_args attach_args;

static void
native_setup(void)
{
	setup();
	CHECK(lock_calls == unlock_calls);
	memset(&storage, 0, sizeof(storage));
	memset(&duplicate, 0, sizeof(duplicate));
	memset(&foreign_storage, 0, sizeof(foreign_storage));
	memset(&bcm2712_iommuc_mutex, 0, sizeof(bcm2712_iommuc_mutex));
	bcm2712_iommuc_once = 0;
	bcm2712_iommuc_owner = NULL;
	decoder_calls = lock_calls = unlock_calls = node_calls = 0;
	print_calls = error_prints = once_calls = 0;
	once_error = 0;
	missing_node = false;
	device.private = &storage;
	device.driver = "bcm2712iommuc";
	namespace_node.ad_handle = CACHE;
	namespace_node.ad_type = ACPI_TYPE_DEVICE;
	namespace_node.ad_device = NULL;
	attach_args.aa_node = &namespace_node;
	attach_args.aa_memt = &tag_token;
}
static void
attach(void)
{
	CHECK(bcm2712iommuc_acpi_ca.size == sizeof(storage));
	CHECK(bcm2712iommuc_acpi_ca.match(NULL, NULL, &attach_args) == 1);
	bcm2712iommuc_acpi_ca.attach(NULL, &device, &attach_args);
	/* config_found assigns ad_device only after the attach callback returns. */
	namespace_node.ad_device = &device;
}
static int
lookup(bool accepted)
{
	struct bcm2712_iommuc_provider *out = &foreign_storage;
	int error = bcm2712_iommuc_lookup(CACHE, &out);

	CHECK((error == 0) == accepted);
	CHECK(out == (accepted ? &storage : &foreign_storage));
	CHECK(lock_calls == unlock_calls && !bcm2712_iommuc_mutex.held);
	CHECK(allocations == frees && mailbox_calls == 0);
	return error;
}
static void
compare_core_status(const struct bcm2712_iommuc_status *a,
    const struct bcm2712_iommuc_status *b)
{
	CHECK(a->reserved == b->reserved && a->retained == b->retained);
	CHECK(a->mmu == b->mmu && a->stage == b->stage && a->poll == b->poll);
	CHECK(a->reads_attempted == b->reads_attempted);
	CHECK(a->reads_completed == b->reads_completed);
	CHECK(a->writes_attempted == b->writes_attempted);
	CHECK(a->writes_completed == b->writes_completed);
	CHECK(a->raw_control == b->raw_control && a->fault == b->fault);
	CHECK(a->raw_valid == b->raw_valid);
	CHECK(a->mutation_attempted == b->mutation_attempted);
	CHECK(a->flush_complete == b->flush_complete);
}

static int
snapshot(struct bcm2712_iommuc_provider *owner, bool accepted)
{
	struct { uint64_t before; struct bcm2712_iommuc_native_status data;
	    uint64_t after; } out, saved;
	struct bcm2712_iommuc_native_status expected;
	unsigned int aml = calls, decoded = decoder_calls;
	int error;

	memset(&out, 0xa5, sizeof(out));
	memcpy(&saved, &out, sizeof(saved));
	error = bcm2712_iommuc_cached_status(owner, &out.data);
	CHECK((error == 0) == accepted);
	if (accepted) {
		memset(&expected, 0, sizeof(expected));
		expected.software_attached = true;
		expected.core_initialized = storage.core.initialized;
		if (storage.core.initialized)
			expected.core = storage.core.status;
		CHECK(out.data.software_attached == expected.software_attached);
		CHECK(out.data.core_initialized == expected.core_initialized);
		compare_core_status(&out.data.core, &expected.core);
	} else {
		CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
	}
	CHECK(out.before == saved.before && out.after == saved.after);
	CHECK(calls == aml && decoder_calls == decoded);
	CHECK(lock_calls == unlock_calls && !bcm2712_iommuc_mutex.held);
	return error;
}
static int forbidden_read(void *arg, uint32_t *out)
{
	(void)arg; (void)out; abort();
}
static int forbidden_write(void *arg, uint32_t value)
{
	(void)arg; (void)value; abort();
}
static void forbidden_barrier(void *arg)
{
	(void)arg; abort();
}

int
main(void)
{
	struct bcm2712_iommuc_provider *out;
	struct bcm2712_iommuc_native_status status;
	struct bcm2712_iommuc_io io = {
		forbidden_read, forbidden_write, forbidden_barrier, &foreign_token
	};
	unsigned int i, count, aml, decoded;

	/* The unchanged descriptor/dependency causal fixture still runs. */
	CHECK(descriptor_contract_main() == 0);
	native_setup();
	CHECK(lookup(false) == ENXIO);
	snapshot(&foreign_storage, false);
	attach();
	CHECK(bcm2712_iommuc_owner == &storage && print_calls == 1);
	CHECK(storage.dev == &device && storage.handle == CACHE);
	CHECK(storage.bst == &tag_token && storage.software_attached);
	CHECK(!storage.core.initialized);
	CHECK(storage.core.io.read_control == NULL);
	CHECK(storage.core.io.write_control == NULL);
	CHECK(storage.core.io.barrier == NULL && storage.core.io.arg == NULL);
	compare_core_status(&storage.core.status,
	    &(struct bcm2712_iommuc_status){ 0 });
	CHECK(lookup(true) == 0); CHECK(lookup(true) == 0);
	snapshot(&storage, true);
	CHECK(bcm2712iommuc_acpi_ca.detach(&device, 0) == EBUSY);
	CHECK(bcm2712_iommuc_owner == &storage);
	bcm2712iommuc_acpi_ca.attach(NULL, &other_device, &attach_args);
	CHECK(bcm2712_iommuc_owner == &storage && !duplicate.software_attached);
	CHECK(error_prints == 1);

	/* Future initialized cached state uses the real pure core, never I/O. */
	CHECK(bcm2712_iommuc_core_init(&storage.core, &io) == 0);
	CHECK(bcm2712_iommuc_core_reserve(&storage.core, BCM2712_IOMMUC_MMU2) == 0);
	CHECK(bcm2712_iommuc_core_reserve(&storage.core, BCM2712_IOMMUC_MMU4) == 0);
	CHECK(bcm2712_iommuc_core_retain(&storage.core, BCM2712_IOMMUC_MMU4) == 0);
	storage.core.status.fault = EIO;
	snapshot(&storage, true); snapshot(&storage, true);
	CHECK(bcm2712iommuc_acpi_ca.detach(&device, 0) == EBUSY);
	CHECK(storage.core.status.retained == 2 && storage.core.status.fault == EIO);

	for (i = 0; i < 15; i++) {
		native_setup(); attach();
		switch (i) {
		case 0: missing_node = true; break;
		case 1: namespace_node.ad_device = NULL; break;
		case 2: namespace_node.ad_type = ACPI_TYPE_INTEGER; break;
		case 3: namespace_node.ad_handle = ALIAS; break;
		case 4: device.driver = "unrelated"; break;
		case 5: device.private = NULL; break;
		case 6: namespace_node.ad_device = &other_device; break;
		case 7: storage.software_attached = false; break;
		case 8: storage.dev = &other_device; break;
		case 9: storage.handle = ALIAS; break;
		case 10: storage.bst = NULL; break;
		case 11: storage.contract.version++; break;
		case 12: storage.contract.address++; break;
		case 13: storage.contract.length++; break;
		case 14: bcm2712_iommuc_owner = NULL; break;
		}
		CHECK(lookup(false) == ENXIO);
	}
	native_setup();
	bcm2712iommuc_acpi_ca.attach(NULL, &device, &attach_args);
	count = calls;
	/* Every fallible decoder call, before lock/publication. */
	for (i = 1; i <= count; i++) {
		native_setup(); fail_at = i;
		bcm2712iommuc_acpi_ca.attach(NULL, &device, &attach_args);
		CHECK(bcm2712_iommuc_owner == NULL && !storage.software_attached);
		CHECK(allocations == frees && lock_calls == unlock_calls);
	}
	native_setup(); attach(); calls = 0; lookup(true); count = calls;
	for (i = 1; i <= count; i++) {
		native_setup(); attach(); calls = 0; fail_at = i;
		lookup(false);
	}
	for (i = 0; i < 7; i++) {
		native_setup();
		switch (i) {
		case 0: attach_args.aa_node = NULL; break;
		case 1: namespace_node.ad_type = ACPI_TYPE_INTEGER; break;
		case 2: attach_args.aa_memt = NULL; break;
		case 3: namespace_node.ad_handle = ALIAS; break;
		case 4: input.version = 2; break;
		case 5: input.sta = 0; break;
		case 6: input.resource[0].Data.Address64.Address.Minimum++; break;
		}
		CHECK(bcm2712iommuc_acpi_ca.match(NULL, NULL, &attach_args) == 0);
		bcm2712iommuc_acpi_ca.attach(NULL, &device, &attach_args);
		CHECK(bcm2712_iommuc_owner == NULL && !storage.software_attached);
	}
	native_setup();
	CHECK(bcm2712iommuc_acpi_ca.match(NULL, NULL, NULL) == 0);
	bcm2712iommuc_acpi_ca.attach(NULL, &device, NULL);
	out = &foreign_storage;
	CHECK(bcm2712_iommuc_lookup(NULL, &out) == EINVAL);
	CHECK(bcm2712_iommuc_lookup(CACHE, NULL) == EINVAL);
	CHECK(bcm2712_iommuc_cached_status(NULL, &status) == EINVAL);
	CHECK(bcm2712_iommuc_cached_status(&foreign_storage, NULL) == EINVAL);
	CHECK(out == &foreign_storage && calls == 0 && decoder_calls == 0);
	native_setup(); once_error = ENOMEM;
	bcm2712iommuc_acpi_ca.attach(NULL, &device, &attach_args);
	CHECK(bcm2712_iommuc_owner == NULL);
	CHECK(lookup(false) == ENOMEM);
	CHECK(snapshot(&foreign_storage, false) == ENOMEM);

	for (i = 0; i < 2; i++) {
		native_setup(); attach();
		aml = calls; decoded = decoder_calls;
		hard_irq = i == 0; soft_irq = i == 1;
		CHECK(lookup(false) == EWOULDBLOCK);
		CHECK(snapshot(&storage, false) == EWOULDBLOCK);
		CHECK(calls == aml && decoder_calls == decoded);
	}
	printf("BCM2712 ICAC native contract: %u checks passed\n", checks);
	return 0;
}
