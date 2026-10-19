/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual sources; kernel services doubled; cache init wrapper injects failure. */
#define main descriptor_contract_main
#define aprint_normal_dev descriptor_aprint_normal_dev
#define bcm2712_iommuc_acpi_resources descriptor_resources
#include "bcm2712-iommuc-contract.c"
#undef bcm2712_iommuc_acpi_resources
#undef aprint_normal_dev
#undef main

#include "core-types.h"
#include "native-types.h"
#include "executor-types.h"
#include "mmu4-types.h"
#include <unistd.h>
#include <sys/wait.h>

typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
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
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2

static unsigned int decoder_calls, lock_calls, unlock_calls, node_calls;
static unsigned int print_calls, error_prints, once_calls;
static int once_error;
static struct acpi_devnode namespace_node;
static bool missing_node;
static int tag_token, foreign_token;
static unsigned int map_calls, read_calls, operational_reads, writes, barriers;
static unsigned int core_init_calls;
static int core_init_error;
static uint32_t operational_value;
static bool poll_timeout;
static int map_error;
static uint32_t control_value;
static int bus_space_map(bus_space_tag_t, uint64_t, uint64_t, int,
    bus_space_handle_t *);
static uint32_t bus_space_read_4(bus_space_tag_t, bus_space_handle_t, size_t);
static void bus_space_write_4(bus_space_tag_t, bus_space_handle_t, size_t, uint32_t);
static void bus_space_barrier(bus_space_tag_t, bus_space_handle_t, size_t, size_t, int);

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
static bool mutex_owned(const kmutex_t *lock)
{ return lock->initialized && lock->held; }
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
#define bcm2712_iommuc_core_init actual_cache_init
#include "core.h"
#undef bcm2712_iommuc_core_init
#include "mmu4.h"
#include "native.h"

int
bcm2712_iommuc_core_init(struct bcm2712_iommuc_core *core,
    const struct bcm2712_iommuc_io *io)
{
	if (bcm2712_iommuc_owner != NULL && io != NULL &&
	    io->arg == bcm2712_iommuc_owner) {
		CHECK(core == &bcm2712_iommuc_owner->core);
		CHECK(bcm2712_iommuc_mutex.held && bcm2712_iommuc_owner->mmu4.attempted);
		core_init_calls++;
		if (core_init_error != 0)
			return core_init_error;
	}
	return actual_cache_init(core, io);
}


static int
bus_space_map(bus_space_tag_t tag, uint64_t address, uint64_t length,
    int flags, bus_space_handle_t *out)
{
	CHECK(bcm2712_iommuc_mutex.held && bcm2712_iommuc_owner != NULL);
	CHECK(tag == &tag_token && address == UINT64_C(0x1000005b00));
	CHECK(length == UINT64_C(0x80) && flags == 0 && out != NULL);
	CHECK(!bcm2712_iommuc_owner->core.initialized);
	CHECK(bcm2712_iommuc_owner->observation.started);
	CHECK(bcm2712_iommuc_owner->observation.stage == BCM2712_IOMMUC_OBSERVE_MAP);
	CHECK(!bcm2712_iommuc_owner->observation.mapped);
	CHECK(!bcm2712_iommuc_owner->observation.raw_valid);
	CHECK(bcm2712_iommuc_owner->observation.reads_attempted == 0);
	CHECK(++map_calls == 1);
	if (map_error != 0)
		return map_error;
	*out = 0x1234;
	return 0;
}
static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, size_t offset)
{
	CHECK(bcm2712_iommuc_mutex.held && bcm2712_iommuc_owner != NULL);
	CHECK(tag == &tag_token && handle == 0x1234 && offset == 0);
	if (bcm2712_iommuc_owner->core.initialized) {
		CHECK(bcm2712_iommuc_owner->executing);
		CHECK(bcm2712_iommuc_owner->mmu4.attempted && core_init_calls == 1);
		CHECK(bcm2712_iommuc_owner->mmu4.fault == 0);
		CHECK(bcm2712_iommuc_owner->core.status.fault == 0);
		CHECK(writes == barriers);
		operational_reads++;
		return poll_timeout && writes ? 4U : operational_value == 0 && writes ? 1U :
		    operational_value;
	}
	CHECK(print_calls == 2);
	CHECK(bcm2712_iommuc_owner->observation.started);
	CHECK(bcm2712_iommuc_owner->observation.mapped);
	CHECK(bcm2712_iommuc_owner->observation.stage == BCM2712_IOMMUC_OBSERVE_READ);
	CHECK(bcm2712_iommuc_owner->observation.reads_attempted == 1);
	CHECK(bcm2712_iommuc_owner->observation.reads_completed == 0);
	CHECK(!bcm2712_iommuc_owner->observation.raw_valid);
	CHECK(map_calls == 1 && ++read_calls == 1);
	return control_value;
}

static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle, size_t offset,
    uint32_t value)
{
	CHECK(bcm2712_iommuc_mutex.held && bcm2712_iommuc_owner != NULL);
	CHECK(tag == &tag_token && handle == 0x1234 && offset == 0 && value == 3);
	CHECK(bcm2712_iommuc_owner->executing && bcm2712_iommuc_owner->mmu4.attempted);
	CHECK(bcm2712_iommuc_owner->core.status.mutation_attempted);
	CHECK(bcm2712_iommuc_owner->core.status.fault == 0);
	CHECK(bcm2712_iommuc_owner->mmu4.fault == 0 && ++writes == 1);
}
static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle, size_t offset,
    size_t length, int flags)
{
	CHECK(bcm2712_iommuc_mutex.held && bcm2712_iommuc_owner != NULL);
	CHECK(tag == &tag_token && handle == 0x1234 && offset == 0 && length == 4);
	CHECK(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE));
	CHECK(bcm2712_iommuc_owner->executing && ++barriers == writes);
}

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
	map_calls = read_calls = operational_reads = writes = barriers = 0;
	core_init_calls = 0; core_init_error = 0; operational_value = 0;
	poll_timeout = false;
	map_error = 0; control_value = 0;
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
		expected.observation = storage.observation;
		if (storage.core.initialized)
			expected.core = storage.core.status;
		CHECK(out.data.software_attached == expected.software_attached);
		CHECK(out.data.core_initialized == expected.core_initialized);
		compare_core_status(&out.data.core, &expected.core);
		CHECK(out.data.mmu4.attempted == storage.mmu4.attempted);
		CHECK(out.data.mmu4.first_error == storage.mmu4.first_error);
		CHECK(out.data.mmu4.fault == storage.mmu4.fault);
		CHECK(out.data.observation.stage == expected.observation.stage);
		CHECK(out.data.observation.raw_control == expected.observation.raw_control);
		CHECK(out.data.observation.first_error == expected.observation.first_error);
		CHECK(out.data.observation.started == expected.observation.started);
		CHECK(out.data.observation.mapped == expected.observation.mapped);
		CHECK(out.data.observation.raw_valid == expected.observation.raw_valid);
		CHECK(out.data.observation.reads_attempted == expected.observation.reads_attempted);
		CHECK(out.data.observation.reads_completed == expected.observation.reads_completed);
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

static void
observation_tests(void)
{
	static const uint32_t samples[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, UINT32_MAX };
	static const int map_errors[] = { ENOMEM, EIO, EBUSY, EOPNOTSUPP };
	struct bcm2712_iommuc_observation *o;
	struct bcm2712_iommuc_io io = {
		forbidden_read, forbidden_write, forbidden_barrier, &foreign_token
	};
	unsigned int aml, decoded, locks;
	int error;

	native_setup();
	CHECK(bcm2712_iommuc_observe_control(NULL) == EINVAL);
	CHECK(bcm2712_iommuc_observe_control(&foreign_storage) == ENXIO);
	attach(); lookup(true); snapshot(&storage, true);
	CHECK(map_calls == 0 && read_calls == 0 && !storage.observation.started);
	for (unsigned int i = 0; i < 2; i++) {
		hard_irq = i == 0; soft_irq = i == 1;
		aml = calls; decoded = decoder_calls; locks = lock_calls;
		CHECK(bcm2712_iommuc_observe_control(&storage) == EWOULDBLOCK);
		CHECK(calls == aml && decoder_calls == decoded && lock_calls == locks);
		CHECK(map_calls == 0 && read_calls == 0 && !storage.observation.started);
	}
	for (unsigned int i = 0; i < __arraycount(samples); i++) {
		native_setup(); attach(); control_value = samples[i];
		o = &storage.observation;
		aml = calls; decoded = decoder_calls;
		error = i < 4 ? 0 : i < 8 ? EBUSY : EOPNOTSUPP;
		CHECK(bcm2712_iommuc_observe_control(&storage) == error);
		CHECK(calls == aml && decoder_calls == decoded);
		CHECK(o->started && o->mapped && o->raw_valid);
		CHECK(o->raw_control == samples[i] && o->first_error == error);
		CHECK(o->stage == (error == 0 ? BCM2712_IOMMUC_OBSERVE_COMPLETE :
		    BCM2712_IOMMUC_OBSERVE_READ));
		CHECK(o->reads_attempted == 1 && o->reads_completed == 1);
		CHECK(map_calls == 1 && read_calls == 1 && !storage.core.initialized);
		compare_core_status(&storage.core.status,
		    &(struct bcm2712_iommuc_status){ 0 });
		snapshot(&storage, true); snapshot(&storage, true);
		CHECK(bcm2712_iommuc_observe_control(&storage) == (error == 0 ? EALREADY : error));
		CHECK(map_calls == 1 && read_calls == 1);
		CHECK(bcm2712iommuc_acpi_ca.detach(&device, 0) == EBUSY);
		CHECK(bcm2712_iommuc_owner == &storage && storage.bsh == 0x1234);
	}
	for (unsigned int i = 0; i < __arraycount(map_errors); i++) {
		native_setup(); attach(); map_error = map_errors[i];
		o = &storage.observation;
		CHECK(bcm2712_iommuc_observe_control(&storage) == map_errors[i]);
		CHECK(o->started && !o->mapped && !o->raw_valid);
		CHECK(o->stage == BCM2712_IOMMUC_OBSERVE_MAP);
		CHECK(o->first_error == map_errors[i] && o->raw_control == 0);
		CHECK(o->reads_attempted == 0 && o->reads_completed == 0);
		CHECK(map_calls == 1 && read_calls == 0 && !storage.core.initialized);
		snapshot(&storage, true);
		CHECK(bcm2712_iommuc_observe_control(&storage) == map_errors[i]);
		CHECK(map_calls == 1 && read_calls == 0);
		CHECK(bcm2712iommuc_acpi_ca.detach(&device, 0) == EBUSY);
		CHECK(bcm2712_iommuc_owner == &storage);
	}
	for (unsigned int i = 0; i < 6; i++) {
		native_setup(); attach();
		switch (i) {
		case 0: storage.software_attached = false; break;
		case 1: storage.bst = NULL; break;
		case 2: storage.contract.version++; break;
		case 3: storage.contract.address++; break;
		case 4: storage.contract.length++; break;
		case 5: bcm2712_iommuc_owner = &foreign_storage; break;
		}
		CHECK(bcm2712_iommuc_observe_control(&storage) == ENXIO);
		CHECK(!storage.observation.started && map_calls == 0 && read_calls == 0);
	}
	native_setup(); attach();
	CHECK(bcm2712_iommuc_core_init(&storage.core, &io) == 0);
	CHECK(bcm2712_iommuc_observe_control(&storage) == EBUSY);
	CHECK(!storage.observation.started && map_calls == 0 && read_calls == 0);
	CHECK(lock_calls == unlock_calls && !bcm2712_iommuc_mutex.held);
}

void *icac_backing_create(struct bcm2712_mmu4_config *, bool);
int icac_backing_prepare(void *, const struct bcm2712_mmu4_config *);
void icac_backing_retained(void *);

struct operation {
	struct bcm2712_mmu4_config cfg;
	struct bcm2712_mmu4_status preclose;
	struct bcm2712_iommuc_core *borrowed;
	void *backing;
	unsigned int callbacks, calls, reads, writes, barriers, fail_at;
	int primary, close_error;
	bool local_locked, snapshot_valid, close_attempted, wrong_debug, reenter;
	bool close_fault;
};
static void
held(struct operation *op)
{
	CHECK(op->local_locked && bcm2712_iommuc_mutex.held);
	CHECK(storage.executing && storage.mmu4.attempted);
	CHECK(op->borrowed == &storage.core);
}
static int
local_prepare(void *arg, const struct bcm2712_mmu4_config *cfg)
{
	struct operation *op = arg;

	held(op);
	CHECK(cfg->top_pa == op->cfg.top_pa);
	return op->backing != NULL ? icac_backing_prepare(op->backing, cfg) : 0;
}
static int
local_read(void *arg, enum bcm2712_mmu4_register reg, uint32_t *out)
{
	struct operation *op = arg;

	held(op); op->reads++;
	if (++op->calls == op->fail_at)
		return EAGAIN;
	switch (reg) {
	case BCM2712_MMU4_DEBUG: *out = op->cfg.debug + op->wrong_debug; break;
	case BCM2712_MMU4_CONTROL: *out = op->writes == 8 ? 0x04090803U : 0; break;
	case BCM2712_MMU4_SHOOT_DOWN: *out = 0; break;
	case BCM2712_MMU4_MISC: *out = 0x80000000U; break;
	default: CHECK(false); return EINVAL;
	}
	return 0;
}
static int
local_write(void *arg, enum bcm2712_mmu4_register reg, uint32_t value)
{
	static const enum bcm2712_mmu4_register regs[] = {
		BCM2712_MMU4_CONTROL, BCM2712_MMU4_MISC, BCM2712_MMU4_CAP,
		BCM2712_MMU4_BYPASS_START, BCM2712_MMU4_BYPASS_END,
		BCM2712_MMU4_PT_BASE, BCM2712_MMU4_ILLEGAL_ADR, BCM2712_MMU4_CONTROL
	};
	struct operation *op = arg;
	uint32_t values[] = { 0x0810100cU, 0, 0x80000000U, 0, 0,
	    (uint32_t)(op->cfg.top_pa >> 12),
	    0x80000000U | (uint32_t)(op->cfg.default_pa >> 12), 0x04090803U };

	held(op);
	if (++op->calls == op->fail_at)
		return EAGAIN;
	CHECK(op->writes < 8 && reg == regs[op->writes] && value == values[op->writes]);
	CHECK((storage.core.status.retained & 2U) != 0);
	op->writes++;
	return 0;
}
static void
local_barrier(void *arg)
{
	struct operation *op = arg;

	held(op); CHECK(++op->barriers == op->writes);
}
static int
operation(void *arg, struct bcm2712_iommuc_core *cache)
{
	struct operation *op = arg;
	struct bcm2712_mmu4_core local = { 0 };
	const struct bcm2712_mmu4_io io = {
		local_prepare, local_read, local_write, local_barrier, op
	};
	struct bcm2712_iommuc_provider *out = &foreign_storage;
	struct bcm2712_iommuc_native_status status;
	unsigned int aml = calls, decoded = decoder_calls;

	op->callbacks++;
	op->borrowed = cache;
	held(op);
	if (op->reenter) {
		CHECK(bcm2712_iommuc_lookup(CACHE, &out) == EDEADLK);
		CHECK(out == &foreign_storage);
		CHECK(bcm2712_iommuc_cached_status(&storage, &status) == EDEADLK);
		CHECK(bcm2712_iommuc_observe_control(&storage) == EDEADLK);
		CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, op) == EDEADLK);
		CHECK(calls == aml && decoder_calls == decoded);
	}
	op->primary = bcm2712_mmu4_core_init(&local, &op->cfg, &io, cache);
	if (op->primary == 0) {
		op->primary = bcm2712_mmu4_core_program(&local);
		CHECK(bcm2712_mmu4_core_status(&local, &op->preclose) == 0);
		op->snapshot_valid = true; /* Value snapshot BEFORE any close. */
		if (op->primary != 0 && !op->preclose.retained &&
		    !op->preclose.mutation_attempted && op->preclose.fault == 0) {
			if (op->close_fault)
				cache->status.fault = EIO;
			op->close_attempted = true;
			op->close_error = bcm2712_mmu4_core_fini(&local);
		}
	}
	held(op);
	op->borrowed = NULL; /* The local sequencer/cache pointer never escapes. */
	return op->primary != 0 ? op->primary : op->close_error;
}
static struct operation
new_operation(void)
{
	struct operation op;

	memset(&op, 0, sizeof(op));
	op.cfg = (struct bcm2712_mmu4_config){ .debug = 0x20804774,
	    .top_pa = 0x20000000, .l2_pa = { 0x20100000, 0x20200000 },
	    .default_pa = 0x20300000 };
	op.local_locked = true;
	return op;
}
static void
executor_setup(void)
{
	native_setup(); attach();
	CHECK(bcm2712_iommuc_observe_control(&storage) == 0);
	CHECK(!storage.core.initialized && map_calls == 1 && read_calls == 1);
}
static int
misuse(void *arg, struct bcm2712_iommuc_core *cache)
{
	unsigned int mode = *(unsigned int *)arg;
	uint32_t value = 0;
	unsigned int before_reads = operational_reads;

	CHECK(bcm2712_iommuc_mutex.held && storage.executing && storage.mmu4.attempted);
	switch (mode) {
	case 0:
		CHECK(cache->io.write_control(cache->io.arg, 2) == EINVAL);
		CHECK(storage.mmu4.fault == EINVAL);
		CHECK(cache->io.read_control(cache->io.arg, &value) == EIO);
		CHECK(cache->io.write_control(cache->io.arg, 3) == EIO);
		cache->io.barrier(cache->io.arg);
		CHECK(storage.mmu4.fault == EINVAL); break;
	case 1:
		CHECK(bcm2712_iommuc_core_reserve(cache, BCM2712_IOMMUC_MMU4) == 0);
		CHECK(bcm2712_iommuc_core_retain(cache, BCM2712_IOMMUC_MMU4) == 0);
		CHECK(bcm2712_iommuc_core_enable_flush(cache, BCM2712_IOMMUC_MMU4) == EBUSY);
		CHECK(cache->status.fault == EBUSY);
		CHECK(cache->io.read_control(cache->io.arg, &value) == EIO);
		CHECK(cache->io.write_control(cache->io.arg, 3) == EIO);
		cache->io.barrier(cache->io.arg);
		CHECK(storage.mmu4.fault == EBUSY);
		CHECK(operational_reads == before_reads + 1); return EAGAIN;
	case 2: CHECK(cache->io.read_control(cache->io.arg, NULL) == EINVAL); break;
	case 3: cache->io.arg = &foreign_token; break;
	case 4: cache->initialized = false; break;
	case 5: CHECK(bcm2712_iommuc_core_fini(cache) == 0); break;
	case 6: CHECK(bcm2712_iommuc_core_reserve(cache, BCM2712_IOMMUC_MMU2) == 0); break;
	case 7: return -EIO;
	case 8: CHECK(bcm2712_iommuc_core_reserve(cache, BCM2712_IOMMUC_MMU4) == 0); return EAGAIN;
	default: CHECK(false);
	}
	CHECK(operational_reads == before_reads && writes == 0 && barriers == 0);
	return 0;
}
static void
real_backing_case(bool fail_prepare, bool wrong_debug)
{
	pid_t child = fork();
	int status;

	CHECK(child != -1);
	if (child == 0) {
		struct operation op = new_operation(), fresh = new_operation();
		int expected = fail_prepare ? EFAULT : wrong_debug ? EAGAIN : 0;
		executor_setup();
		op.backing = icac_backing_create(&op.cfg, fail_prepare);
		op.wrong_debug = wrong_debug;
		CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &op) == expected);
		CHECK(op.borrowed == NULL && op.snapshot_valid && op.callbacks == 1);
		CHECK(op.close_attempted == (expected != 0));
		CHECK(op.close_error == 0 && storage.mmu4.fault == 0);
		CHECK(storage.core.status.reserved == (expected == 0 ? 2U : 0U));
		CHECK(storage.core.status.retained == (expected == 0 ? 2U : 0U));
		CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &fresh) ==
		    (expected == 0 ? EALREADY : expected));
		CHECK(fresh.callbacks == 0 && core_init_calls == 1);
		icac_backing_retained(op.backing);
		CHECK(bcm2712iommuc_acpi_ca.detach(&device, 0) == EBUSY);
		_exit(0); /* Retained sealed backing belongs to this simulated boot. */
	}
	CHECK(waitpid(child, &status, 0) == child);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void
executor_tests(void)
{
	struct operation op, fresh;
	unsigned int count, before_reads, before_writes, before_calls, aml, decoded;
	int expected;

	native_setup(); attach(); op = new_operation();
	CHECK(bcm2712_iommuc_execute_mmu4(NULL, operation, &op) == EINVAL);
	CHECK(bcm2712_iommuc_execute_mmu4(&storage, NULL, &op) == EINVAL);
	CHECK(bcm2712_iommuc_execute_mmu4(&foreign_storage, operation, &op) == ENXIO);
	CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &op) == EBUSY);
	CHECK(!storage.mmu4.attempted && op.callbacks == 0 && core_init_calls == 0);
	CHECK(map_calls == 0 && read_calls == 0);
	for (unsigned int i = 0; i < 2; i++) {
		hard_irq = i == 0; soft_irq = i == 1;
		CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &op) == EWOULDBLOCK);
		CHECK(!storage.mmu4.attempted && op.callbacks == 0);
	}
	executor_setup(); op = new_operation(); op.reenter = true;
	aml = calls; decoded = decoder_calls;
	CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &op) == 0);
	CHECK(calls == aml && decoder_calls == decoded && op.borrowed == NULL);
	CHECK(op.snapshot_valid && op.preclose.sequence_complete && op.preclose.retained);
	CHECK(!op.close_attempted && op.writes == 8 && op.barriers == 8);
	CHECK(writes == 1 && barriers == 1 && operational_reads == 2);
	CHECK(storage.mmu4.attempted && storage.mmu4.first_error == 0 && storage.mmu4.fault == 0);
	CHECK(storage.core.status.reserved == 2 && storage.core.status.retained == 2);
	CHECK(storage.core.status.flush_complete && !storage.executing);
	count = op.calls;
	before_reads = operational_reads; before_writes = writes;
	fresh = new_operation();
	CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &fresh) == EALREADY);
	CHECK(fresh.callbacks == 0 && core_init_calls == 1);
	CHECK(operational_reads == before_reads && writes == before_writes);
	snapshot(&storage, true);
	for (unsigned int i = 1; i <= count; i++) {
		executor_setup(); op = new_operation(); op.fail_at = i;
		CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &op) == EAGAIN);
		CHECK(op.primary == EAGAIN && op.snapshot_valid && op.borrowed == NULL);
		CHECK(op.close_attempted == !op.preclose.retained && op.close_error == 0);
		CHECK(storage.mmu4.fault == (op.preclose.retained ? EAGAIN : 0));
		CHECK(storage.mmu4.first_error == EAGAIN && storage.mmu4.attempted);
		before_reads = operational_reads; before_calls = op.calls;
		fresh = new_operation();
		CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &fresh) == EAGAIN);
		CHECK(fresh.callbacks == 0 && op.calls == before_calls);
		CHECK(operational_reads == before_reads && core_init_calls == 1);
	}
	executor_setup(); op = new_operation(); op.wrong_debug = true; op.close_fault = true;
	CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &op) == EAGAIN);
	CHECK(op.primary == EAGAIN && op.close_error == EIO && op.close_attempted);
	CHECK(op.preclose.first_error == EAGAIN && op.preclose.close_error == 0);
	CHECK(op.preclose.reservation_owned && !op.preclose.retained);
	CHECK(storage.mmu4.first_error == EAGAIN && storage.mmu4.fault == EIO);
	CHECK(storage.core.status.reserved == 2 && writes == 0);
	executor_setup(); op = new_operation(); core_init_error = ERANGE;
	CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &op) == ERANGE);
	CHECK(storage.mmu4.attempted && storage.mmu4.first_error == ERANGE);
	CHECK(storage.mmu4.fault == 0 && op.callbacks == 0 && core_init_calls == 1);
	core_init_error = 0; fresh = new_operation();
	CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &fresh) == ERANGE);
	CHECK(fresh.callbacks == 0 && core_init_calls == 1 && !storage.core.initialized);
	for (unsigned int i = 0; i < 3; i++) {
		executor_setup(); op = new_operation();
		operational_value = i == 0 ? 4U : i == 1 ? 8U : 0;
		poll_timeout = i == 2;
		expected = i == 0 ? EBUSY : i == 1 ? EOPNOTSUPP : ETIMEDOUT;
		CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &op) == expected);
		CHECK(op.preclose.retained && !op.close_attempted && op.borrowed == NULL);
		CHECK(storage.mmu4.fault == expected && storage.core.status.fault == expected);
	}
	for (unsigned int mode = 0; mode < 9; mode++) {
		executor_setup(); operational_value = mode == 1 ? 4U : 0;
		expected = mode == 0 || mode == 2 || mode == 7 ? EINVAL :
		    mode == 1 || mode == 8 ? EAGAIN : EPROTO;
		CHECK(bcm2712_iommuc_execute_mmu4(&storage, misuse, &mode) == expected);
		CHECK(storage.mmu4.fault != 0 && storage.mmu4.first_error == expected);
		before_reads = operational_reads; before_writes = writes;
		fresh = new_operation();
		CHECK(bcm2712_iommuc_execute_mmu4(&storage, operation, &fresh) == expected);
		CHECK(fresh.callbacks == 0 && operational_reads == before_reads && writes == before_writes);
		snapshot(&storage, true);
	}
	real_backing_case(false, false);
	real_backing_case(false, true);
	real_backing_case(true, false);
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
	observation_tests();
	executor_tests();
	printf("BCM2712 ICAC native contract: %u checks passed\n", checks);
	return 0;
}
