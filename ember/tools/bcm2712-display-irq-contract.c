/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the production L2 core and native adapter with controlled services.
 */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#ifdef NDEBUG
#error This contract requires assertions
#endif
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32 OSSwapHostToLittleInt32
#define le32toh OSSwapLittleToHostInt32
#elif defined(__linux__)
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#define _KERNEL 1
#define KASSERT assert
#define KM_SLEEP 0
#define MUTEX_DEFAULT 0
#define IPL_VM 5
#define PRI_NONE 0
#define WQ_MPSAFE 1
#define ACPI_LEVEL_SENSITIVE 0
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define ASSERT_SLEEPABLE() assert(!in_irq)
typedef void *device_t;
typedef unsigned int bus_space_tag_t;
typedef unsigned int bus_space_handle_t;
typedef uint64_t bus_size_t;
typedef struct {
	bool held;
} kmutex_t;
struct lwp {
	int id;
};
static struct lwp main_lwp, work_lwp;
static struct lwp *curlwp = &main_lwp;
struct acpi_irq {
	unsigned int ar_irq, ar_type;
};
struct work {
	void *dummy;
};
struct workqueue {
	void (*function)(struct work *, void *);
	void *arg;
	struct work *pending;
	bool running;
};
static bool in_irq, queue_fail, establish_fail, parent_masked, barrier_done;
static int lock_count, allocations, queues, established, enqueues, waits;
static unsigned int reads, writes, clears, callbacks, expected_gsi;
static uint32_t status_bits, mask_bits, owned_bits, delivered;
static unsigned int mask_status, mask_set, mask_clear;
static bool has_clear, service_fail, service_self_destroy, service_enqueue;
static bool service_hold_level;
static int callback_error, self_result;
static int (*irq_function)(void *);
static void *irq_arg;
static struct workqueue *queue;
static struct bcm2712_l2_provider *instance;
static void run_work(void);
static uint32_t bus_space_read_4(
    bus_space_tag_t, bus_space_handle_t, unsigned int);
static void bus_space_write_4(
    bus_space_tag_t, bus_space_handle_t, unsigned int, uint32_t);
static void bus_space_barrier(
    bus_space_tag_t, bus_space_handle_t, bus_size_t, bus_size_t, int);
static void *kmem_zalloc(size_t, int);
static void kmem_free(void *, size_t);
static void mutex_init(kmutex_t *, int, int);
static void mutex_enter(kmutex_t *);
static void mutex_exit(kmutex_t *);
static void mutex_destroy(kmutex_t *);
static int workqueue_create(struct workqueue **, const char *,
    void (*)(struct work *, void *), void *, int, int, int);
static void workqueue_enqueue(struct workqueue *, struct work *, void *);
static void workqueue_wait(struct workqueue *, struct work *);
static void workqueue_destroy(struct workqueue *);
static void *acpi_intr_establish_irq(device_t, struct acpi_irq *, int, bool,
    int (*)(void *), void *, const char *);
static void acpi_intr_mask(void *);
static void acpi_intr_disestablish(void *);
static void xc_barrier(unsigned int);
#include "display.h"
#include "properties.h"
#include "display-core.h"
#include "irq.h"
#include "irq-core.h"
#include "irq-adapter.h"

static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int offset)
{
	assert(tag == 1 && handle == 2);
	reads++;
	if (offset == 0)
		return status_bits;
	assert(offset == mask_status);
	return mask_bits;
}

static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int offset, uint32_t value)
{
	assert(tag == 1 && handle == 2);
	assert(value != 0 && (value & ~owned_bits) == 0);
	writes++;
	if (offset == mask_set)
		mask_bits |= value;
	else if (offset == mask_clear)
		mask_bits &= ~value;
	else {
		assert(has_clear && offset == 8);
		status_bits &= ~value;
		clears++;
	}
}

static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, bus_size_t length, int flags)
{
	assert(tag == 1 && handle == 2 && offset == 0);
	assert(length == (has_clear ? 0x30 : 0x10));
	assert(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE));
}

static void *
kmem_zalloc(size_t size, int flags)
{
	assert(flags == KM_SLEEP && !in_irq);
	allocations++;
	return calloc(1, size);
}

static void
kmem_free(void *ptr, size_t size)
{
	assert(ptr != NULL && size != 0 && !in_irq);
	allocations--;
	free(ptr);
}

static void
mutex_init(kmutex_t *lock, int type, int ipl)
{
	assert(type == MUTEX_DEFAULT && ipl == IPL_VM);
	lock->held = false;
}

static void
mutex_enter(kmutex_t *lock)
{
	assert(!lock->held);
	lock->held = true;
	lock_count++;
}

static void
mutex_exit(kmutex_t *lock)
{
	assert(lock->held);
	lock->held = false;
	lock_count--;
}

static void
mutex_destroy(kmutex_t *lock)
{
	assert(!lock->held);
}

static int
workqueue_create(struct workqueue **out, const char *name,
    void (*function)(struct work *, void *), void *arg, int priority, int ipl,
    int flags)
{
	assert(!in_irq && lock_count == 0 && name != NULL);
	assert(priority == PRI_NONE && ipl == IPL_VM && flags == WQ_MPSAFE);
	if (queue_fail)
		return ENOMEM;
	queue = calloc(1, sizeof(*queue));
	assert(queue != NULL);
	queues++;
	queue->function = function;
	queue->arg = arg;
	*out = queue;
	return 0;
}

static void
workqueue_enqueue(struct workqueue *wq, struct work *work, void *cpu)
{
	assert(wq == queue && wq->pending == NULL && cpu == NULL);
	wq->pending = work;
	enqueues++;
}

static void
run_work(void)
{
	struct work *work = queue->pending;
	assert(!in_irq && lock_count == 0 && work != NULL && !queue->running);
	queue->pending = NULL;
	queue->running = true;
	curlwp = &work_lwp;
	queue->function(work, queue->arg);
	curlwp = &main_lwp;
	queue->running = false;
}

static void
workqueue_wait(struct workqueue *wq, struct work *work)
{
	assert(wq == queue && lock_count == 0 && !in_irq && !wq->running);
	assert(
	    established == 0 && parent_masked && barrier_done && work != NULL);
	waits++;
	if (wq->pending != NULL)
		run_work();
}

static void
workqueue_destroy(struct workqueue *wq)
{
	assert(wq == queue && wq->pending == NULL && !wq->running &&
	       lock_count == 0);
	free(wq);
	queue = NULL;
	queues--;
}

static void *
acpi_intr_establish_irq(device_t dev, struct acpi_irq *irq, int ipl,
    bool mpsafe, int (*function)(void *), void *arg, const char *name)
{
	assert(dev != NULL && irq->ar_irq == expected_gsi &&
	       irq->ar_type == ACPI_LEVEL_SENSITIVE);
	assert(ipl == IPL_VM && mpsafe && name != NULL && lock_count == 0);
	assert((mask_bits & owned_bits) == owned_bits);
	if (establish_fail)
		return NULL;
	irq_function = function;
	irq_arg = arg;
	established++;
	return &established;
}

static void
acpi_intr_mask(void *ih)
{
	assert(ih == &established && established == 1);
	parent_masked = true;
}

static void
xc_barrier(unsigned int flags)
{
	assert(flags == 0 && lock_count == 0 && parent_masked);
	barrier_done = true;
}

static void
acpi_intr_disestablish(void *ih)
{
	assert(ih == &established && parent_masked && barrier_done &&
	       lock_count == 0);
	established--;
}

static int
trigger(void)
{
	int result;
	assert(!parent_masked && established == 1 && !in_irq);
	in_irq = true;
	result = irq_function(irq_arg);
	in_irq = false;
	return result;
}

static int
service(void *arg, uint32_t bits, int error)
{
	assert(arg == &callbacks && lock_count == 0 && !in_irq &&
	       curlwp == &work_lwp);
	callbacks++;
	delivered |= bits;
	callback_error = error;
	if (service_self_destroy)
		self_result = bcm2712_display_irq_destroy(instance);
	if (service_enqueue && callbacks == 1) {
		/* A second leaf can queue while this work item is running. */
		status_bits |= __BIT(8);
		assert(trigger() == 1);
	}
	if (!has_clear && !service_hold_level)
		status_bits &= ~bits;
	return service_fail ? EIO : 0;
}

static struct bcm2712_display_contract contract;

static void
reset(enum bcm2712_l2_domain domain)
{
	assert(allocations == 0 && queues == 0 && established == 0 &&
	       lock_count == 0);
	memset(&contract, 0, sizeof(contract));
	contract.version = 1;
	for (unsigned int i = 0; i < BCM2712_DISPLAY_NMEM; i++)
		contract.memory[i] = bcm2712_display_resources[i].memory;
	for (unsigned int i = 0; i < BCM2712_DISPLAY_NIRQ; i++)
		contract.irq[i] = bcm2712_display_irqs[i].gsi;
	has_clear = domain != BCM2712_L2_BSC;
	mask_status = has_clear ? 12 : 4;
	mask_set = has_clear ? 16 : 8;
	mask_clear = has_clear ? 20 : 12;
	owned_bits = domain == BCM2712_L2_AON ? __BIT(7) | __BIT(8) : __BIT(1);
	if (domain == BCM2712_L2_AON)
		expected_gsi = 271;
	else if (domain == BCM2712_L2_BSC)
		expected_gsi = 274;
	else
		expected_gsi = 129;
	status_bits = delivered = 0;
	mask_bits = UINT32_MAX;
	reads = writes = clears = callbacks = 0;
	enqueues = waits = 0;
	queue_fail = establish_fail = parent_masked = barrier_done = false;
	service_fail = service_self_destroy = service_enqueue =
	    service_hold_level = false;
	callback_error = self_result = 0;
	instance = NULL;
}

static int
create(enum bcm2712_l2_domain domain)
{
	return bcm2712_display_irq_create(&contract, &contract, domain,
	    &contract.memory[bcm2712_display_domains[domain].memory_index], 1,
	    2, owned_bits, service, &callbacks, &instance);
}

static void
destroy(void)
{
	int error = bcm2712_display_irq_destroy(instance);

	assert(error == 0 && allocations == 0 && queues == 0 &&
	    established == 0);
	assert(mask_bits == UINT32_MAX);
	instance = NULL;
}

int
main(void)
{
	int error, result;
	unsigned int before_writes;

	for (enum bcm2712_l2_domain domain = BCM2712_L2_DISP;
	    domain <= BCM2712_L2_BSC; domain++) {
		reset(domain);
		error = create(domain);
		assert(error == 0);
		status_bits = owned_bits | __BIT(31);
		reads = 0;
		before_writes = writes;
		result = trigger();
		assert(result == 1 && reads == 2 && enqueues == 1);
		assert((mask_bits & owned_bits) == owned_bits &&
		       (status_bits & __BIT(31)) != 0);
		assert(clears == (has_clear ? 1U : 0U));
		assert(writes - before_writes == (has_clear ? 2U : 1U));
		result = trigger();
		assert(result == 0 && enqueues == 1);
		run_work();
		assert(callbacks == 1 && delivered == owned_bits &&
		       callback_error == 0);
		assert((mask_bits & owned_bits) == 0 &&
		       (status_bits & __BIT(31)) != 0);
		/* A second assertion after completion must not be lost. */
		status_bits |= owned_bits;
		result = trigger();
		assert(result == 1);
		run_work();
		assert(callbacks == 2);
		destroy();
	}
	reset(BCM2712_L2_AON);
	error = create(BCM2712_L2_AON);
	assert(error == 0);
	status_bits = __BIT(7);
	result = trigger();
	assert(result == 1);
	status_bits |= __BIT(8);
	result = trigger();
	assert(result == 1 && enqueues == 1);
	run_work();
	assert(delivered == owned_bits && callbacks == 1);
	destroy();
	/* An unmasked declared CEC leaf has a different owner. */
	reset(BCM2712_L2_AON);
	mask_bits &= ~__BIT(1);
	error = create(BCM2712_L2_AON);
	assert(error == EBUSY && writes == 0 && instance == NULL);
	/* Reserved mask bits may read zero without an asserted source. */
	reset(BCM2712_L2_AON);
	mask_bits &= ~__BIT(31);
	error = create(BCM2712_L2_AON);
	assert(error == 0 && (mask_bits & __BIT(31)) == 0);
	error = bcm2712_display_irq_destroy(instance);
	assert(error == 0 && mask_bits == (UINT32_MAX & ~__BIT(31)));
	/* An active unknown source cannot share this parent. */
	reset(BCM2712_L2_AON);
	mask_bits &= ~__BIT(31);
	status_bits = __BIT(31);
	error = create(BCM2712_L2_AON);
	assert(error == EBUSY && writes == 0 && instance == NULL);
	/* Deliver a persistent BSC level again after unmask. */
	reset(BCM2712_L2_BSC);
	error = create(BCM2712_L2_BSC);
	assert(error == 0);
	service_hold_level = true;
	status_bits = owned_bits;
	result = trigger();
	assert(result == 1);
	run_work();
	assert(status_bits == owned_bits && callbacks == 1 && clears == 0);
	service_hold_level = false;
	result = trigger();
	assert(result == 1);
	run_work();
	assert(status_bits == 0 && callbacks == 2 && clears == 0);
	destroy();
	reset(BCM2712_L2_AON);
	queue_fail = true;
	error = create(BCM2712_L2_AON);
	assert(error == ENOMEM && reads == 0 && writes == 0);
	reset(BCM2712_L2_AON);
	establish_fail = true;
	mask_bits &= ~__BIT(7);
	error = create(BCM2712_L2_AON);
	assert(error == ENXIO && mask_bits == (UINT32_MAX & ~__BIT(7)));
	reset(BCM2712_L2_AON);
	contract.version = 2;
	error = create(BCM2712_L2_AON);
	assert(error == EOPNOTSUPP && reads == 0 && writes == 0);
	reset(BCM2712_L2_AON);
	owned_bits = __BIT(31);
	error = create(BCM2712_L2_AON);
	assert(error == EINVAL && reads == 0 && writes == 0);
	reset(BCM2712_L2_AON);
	error = create(BCM2712_L2_AON);
	assert(error == 0);
	status_bits = __BIT(7);
	result = trigger();
	assert(result == 1);
	destroy();
	assert(callbacks == 0 && waits == 1);
	reset(BCM2712_L2_AON);
	error = create(BCM2712_L2_AON);
	assert(error == 0);
	mask_bits &= ~__BIT(31);
	status_bits = __BIT(31);
	result = trigger();
	assert(result == 1 && parent_masked && (mask_bits & __BIT(31)) == 0);
	run_work();
	assert(callback_error == EIO && delivered == 0 &&
	       bcm2712_display_irq_error(instance) == EIO);
	/* Undo only the external actor's change before teardown checks. */
	mask_bits |= __BIT(31);
	destroy();
	reset(BCM2712_L2_BSC);
	error = create(BCM2712_L2_BSC);
	assert(error == 0);
	service_fail = true;
	status_bits = owned_bits;
	result = trigger();
	assert(result == 1);
	run_work();
	assert(parent_masked && bcm2712_display_irq_error(instance) == EIO);
	destroy();
	reset(BCM2712_L2_AON);
	error = create(BCM2712_L2_AON);
	assert(error == 0);
	service_self_destroy = true;
	status_bits = __BIT(7);
	result = trigger();
	assert(result == 1);
	run_work();
	assert(self_result == EDEADLK && allocations == 1);
	destroy();
	reset(BCM2712_L2_AON);
	error = create(BCM2712_L2_AON);
	assert(error == 0);
	service_enqueue = true;
	status_bits = __BIT(7);
	result = trigger();
	assert(result == 1);
	run_work();
	assert(callbacks == 1 && enqueues == 2 && queue->pending != NULL);
	run_work();
	assert(callbacks == 2 && delivered == owned_bits);
	destroy();
	puts("BCM2712 L2 actual-source IRQ, ownership and teardown contract "
	     "PASS");
	return 0;
}
