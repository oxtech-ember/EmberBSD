/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded manual firmware boot experiment, AI-assisted. */
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <arm/pic/picvar.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpi_power.h>
#include <dev/acpi/cixvpu_acpi_boot.h>
#include <dev/acpi/cixvpu_acpi_control_private.h>
#include <dev/ic/cixvpu_prepare.h>
#include <dev/ic/cixvpu_resources.h>

struct cixvpu_acpi_boot {
	device_t dev;
	struct acpi_attach_args aa;
	struct cixvpu_resources resources;
	struct cixvpu_acpi_control control;
	struct cixvpu_prepared *prepared;
	ACPI_HANDLE cores[4], powers[4];
	void *irq_cookie;
	volatile unsigned int irq_ready, irq_masked, irq_seen, irq_unexpected;
	bool rpc_enabled;
	struct cixvpu_boot_report report;
};

static uint64_t
cixvpu_boot_now(void)
{
	struct timeval now;

	getmicrouptime(&now);
	return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_usec;
}

static void
cixvpu_boot_mask(struct cixvpu_acpi_boot *b)
{

	if (b->irq_cookie != NULL && atomic_cas_uint(&b->irq_masked, 0, 1) == 0)
		intr_mask(b->irq_cookie);
}

static int
cixvpu_boot_interrupt(void *arg)
{
	struct cixvpu_acpi_boot *b = arg;
	const struct cixvpu_acpi_control *c = &b->control;
	uint32_t pending;

	if (atomic_load_acquire(&b->irq_ready) == 0)
		return 0;
	pending = bus_space_read_4(c->bst, c->bsh, 0x18);
	if (pending == 0)
		return 0;
	if ((pending & 1) != 0) {
		bus_space_write_4(c->bst, c->bsh, 0x21c, 0);
		bus_space_barrier(c->bst, c->bsh, 0x21c, 4,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		atomic_or_uint(&b->irq_seen, 1);
	}
	if ((pending & ~1U) != 0) {
		atomic_or_uint(&b->irq_unexpected, pending & ~1U);
		cixvpu_boot_mask(b);
	}
	return 1;
}

static uint32_t
cixvpu_boot_read(struct cixvpu_acpi_boot *b, bool rcsu, bus_size_t offset)
{
	const struct cixvpu_acpi_control *c = &b->control;

	return bus_space_read_4(c->bst, rcsu ? c->profile_bsh : c->bsh, offset);
}

static void
cixvpu_boot_write(struct cixvpu_acpi_boot *b, bool rcsu, bus_size_t offset,
    uint32_t value)
{
	const struct cixvpu_acpi_control *c = &b->control;

	b->report.last_offset = offset;
	b->report.last_value = value;
	bus_space_write_4(c->bst, rcsu ? c->profile_bsh : c->bsh, offset, value);
	bus_space_barrier(c->bst, rcsu ? c->profile_bsh : c->bsh, offset, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

/* This is an owned literal profile, not global firmware exclusion. */
static int
cixvpu_boot_idle(struct cixvpu_acpi_boot *b)
{
	const struct cixvpu_acpi_control *c = &b->control;
	uint32_t queue;
	unsigned int i;

	if (!c->mapped || !c->profile_mapped || c->span < 0x240 ||
	    c->profile_span < 0x30c || c->profile.usable_core_mask != 0xf)
		return ENOTSUP;
	if ((cixvpu_boot_read(b, false, 4) & ~1U) != 0 ||
	    (cixvpu_boot_read(b, false, 0x10) & 0xffff) != 0xffff ||
	    cixvpu_boot_read(b, false, 0x18) != 0 ||
	    cixvpu_boot_read(b, false, 0x20c) != 0 ||
	    cixvpu_boot_read(b, false, 0x214) != 0 ||
	    cixvpu_boot_read(b, false, 0x218) != 0)
		return EBUSY;
	queue = cixvpu_boot_read(b, false, 0x14);
	for (i = 0; i < 4; i++)
		if (((queue >> (i * 8)) & 0xf) != 0xf)
			return EBUSY;
	return 0;
}

static int
cixvpu_boot_terminate(struct cixvpu_acpi_boot *b)
{
	uint64_t start = cixvpu_boot_now();
	unsigned int i;

	for (i = 0; i < 10001; i++) {
		uint64_t now;

		b->report.terminate_reads++;
		if (cixvpu_boot_read(b, false, 0x218) == 0)
			return 0;
		now = cixvpu_boot_now();
		if (now < start)
			return EIO;
		if (now - start >= 100000)
			return ETIMEDOUT;
		if (i != 10000)
			delay(10);
	}
	return ETIMEDOUT;
}

static int
cixvpu_boot_setup(struct cixvpu_acpi_boot *b)
{
	unsigned int i;
	uint32_t value;
	int error;

	if ((error = cixvpu_boot_idle(b)) != 0)
		return error;
	value = cixvpu_boot_read(b, true, 0x21c);
	cixvpu_boot_write(b, true, 0x21c, value | 1);
	b->report.stage = CIXVPU_BOOT_REPAIR;
	for (i = 0; i < 4; i++) {
		ACPI_STATUS rv;

		b->report.core_attempted |= 1U << i;
		rv = acpi_power_res(b->powers[i], b->cores[i], true);
		b->report.core_status[i] = rv;
		if (ACPI_FAILURE(rv))
			return EIO;
	}
	/* Advisory: cached ON can skip _ON and AML can return after timeout. */
	b->report.repair_pass = cixvpu_boot_read(b, true, 0x10);
	b->report.repair_enable = cixvpu_boot_read(b, true, 0x14);
	b->report.repair_busy = cixvpu_boot_read(b, true, 0x18);
	value = cixvpu_boot_read(b, true, 0x300);
	value = (value & 0x0fffffffU) |
	    ((~b->control.profile.usable_core_mask & 0xfU) << 28);
	cixvpu_boot_write(b, true, 0x300, value);
	cixvpu_boot_write(b, false, 0x50, 1);
	cixvpu_boot_write(b, false, 0x24, 0);
	cixvpu_boot_write(b, false, 0x44, 2);
	b->report.stage = CIXVPU_BOOT_SETUP;
	cixvpu_boot_write(b, false, 0x20c, 1);
	cixvpu_boot_write(b, false, 0x218, 1);
	if ((error = cixvpu_boot_terminate(b)) != 0)
		return error;
	cixvpu_boot_write(b, false, 0x200, 0x1fe);
	return 0;
}

static int
cixvpu_boot_start(struct cixvpu_acpi_boot *b, uint32_t root)
{
	uint32_t queue;
	unsigned int i;

	cixvpu_boot_write(b, false, 0x204, root);
	cixvpu_boot_write(b, false, 0x210, 0);
	cixvpu_boot_write(b, false, 0x230, 0x33);
	cixvpu_boot_write(b, false, 0x234, 0x22);
	cixvpu_boot_write(b, false, 0x238, 0x33);
	cixvpu_boot_write(b, false, 0x23c, 0x33);
	cixvpu_boot_write(b, false, 0x21c, 0);
	cixvpu_boot_write(b, false, 0x220, 1);
	cixvpu_boot_write(b, false, 0x214, 1);
	cixvpu_boot_write(b, false, 4, 0);
	queue = cixvpu_boot_read(b, false, 0x14);
	for (i = 0; i < 4; i++) {
		if (((queue >> (i * 8)) & 0xf) == 0xf) {
			queue &= ~(0xffU << (i * 8)); /* LSID0, one core. */
			cixvpu_boot_write(b, false, 0x14, queue);
			cixvpu_boot_write(b, false, 4, 1);
			return 0;
		}
	}
	return EBUSY;
}

static void
cixvpu_boot_observe_rpc(struct cixvpu_acpi_boot *b)
{
	struct cixvpu_boot_rpc_header header = { 0 };

	b->report.rpc_header_error = cixvpu_prepared_boot_rpc_header(b->prepared,
	    &header);
	b->report.rpc_header_valid = header.valid;
	b->report.rpc_state = header.state;
	b->report.rpc_callid = header.callid;
	b->report.rpc_size = header.size;
	b->report.rpc_alloc_valid = header.alloc_valid;
	b->report.rpc_alloc_size = header.alloc_size;
	b->report.rpc_alloc_max_size = header.alloc_max_size;
	b->report.rpc_alloc_region = header.alloc_region;
	b->report.rpc_alloc_log2_alignment = header.alloc_log2_alignment;
}

static int
cixvpu_boot_receive(struct cixvpu_acpi_boot *b)
{
	struct cixvpu_boot_reply reply;
	uint64_t start = cixvpu_boot_now();
	unsigned int visit, i;

	b->report.stage = CIXVPU_BOOT_RECEIVE;
	for (visit = 0; visit < 1001; visit++) {
		unsigned int bytes = 0;
		uint64_t now;

		b->report.visits++;
		if (atomic_load_acquire(&b->irq_unexpected) != 0)
			return EIO;
		for (i = 0; i < 16 && bytes < 4096; i++) {
			int error;

			if (b->rpc_enabled) {
				enum cixvpu_boot_rpc_result result;

				error = cixvpu_prepared_boot_rpc_step(b->prepared,
				    &result);
				cixvpu_boot_observe_rpc(b);
				if (error == EAGAIN)
					break;
				if (error != 0)
					return error;
				if (result == CIXVPU_BOOT_RPC_ALLOC_REPLIED ||
				    result == CIXVPU_BOOT_RPC_DEBUG_REPLIED) {
					/* The closed step has already committed RETURN. */
					b->report.rpc_commits++;
					if (result == CIXVPU_BOOT_RPC_ALLOC_REPLIED)
						b->report.rpc_alloc_replied = true;
					if (b->report.rpc_commits > 17)
						return EPROTO;
					if (atomic_load_acquire(&b->irq_unexpected) != 0)
						return EIO;
					cixvpu_boot_write(b, false, 0x220, 1);
					b->report.rpc_notifications++;
					if (atomic_load_acquire(&b->irq_unexpected) != 0)
						return EIO;
					continue;
				}
				if (result != CIXVPU_BOOT_RPC_IDLE &&
				    result != CIXVPU_BOOT_RPC_PENDING)
					return EPROTO;
				error = cixvpu_prepared_boot_receive_rpc(b->prepared,
				    &reply);
			} else {
				error = cixvpu_prepared_boot_receive(b->prepared, &reply);
			}

			cixvpu_boot_observe_rpc(b);
			if (error == EAGAIN)
				break;
			if (error != 0)
				return error;
			b->report.messages++;
			if (reply.code == 2001 && reply.core == 0) {
				b->report.switched_in = true;
				bytes += 8;
			} else if (reply.code == 2009) {
				b->report.pong = true;
				bytes += 4;
			} else {
				return EPROTO;
			}
			if (b->report.switched_in && b->report.pong)
				return 0;
		}
		now = cixvpu_boot_now();
		if (now < start)
			return EIO;
		if (now - start >= 1000000)
			return ETIMEDOUT;
		if (visit != 1000)
			delay(1000);
	}
	return ETIMEDOUT;
}

/* Never call this after even an unused root capability was returned. */
static int
cixvpu_boot_unwind(struct cixvpu_acpi_boot *b)
{
	ACPI_STATUS rv;
	int error, i;

	if (b->report.exposed)
		return EBUSY;
	/* A failed reset must still leave our terminal IRQ disabled. */
	cixvpu_boot_mask(b);
	atomic_store_release(&b->irq_ready, 0);
	if (b->control.started) {
		if ((error = cixvpu_control_assert_stop(&b->control)) != 0)
			return error;
	}
	if (b->irq_cookie != NULL) {
		intr_disestablish(b->irq_cookie);
		b->irq_cookie = NULL;
	}
	if (b->control.started) {
		if ((error = cixvpu_control_clock_stop(&b->control)) != 0)
			return error;
	}
	for (i = 3; i >= 0; i--) {
		if ((b->report.core_attempted & (1U << i)) == 0 ||
		    (b->report.core_released & (1U << i)) != 0)
			continue;
		rv = acpi_power_res(b->powers[i], b->cores[i], false);
		b->report.core_cleanup[i] = rv;
		if (ACPI_FAILURE(rv))
			return EIO;
		b->report.core_released |= 1U << i;
	}
	if (b->control.started) {
		error = cixvpu_acpi_control_cleanup(&b->control);
		if (error != 0)
			return error;
	}
	if (b->prepared != NULL)
		return cixvpu_prepared_free(&b->prepared);
	return 0;
}

static bool
cixvpu_boot_overlap(const struct cixvpu_acpi_boot *b, const void *p,
    size_t size)
{
	uintptr_t base = (uintptr_t)b, address = (uintptr_t)p;

	return address >= base ? address - base < sizeof(*b) :
	    base - address < size;
}

static int
cixvpu_boot_create(device_t dev, const struct acpi_attach_args *aa,
    const struct cixvpu_resources *resources, struct cixvpu_acpi_boot **out,
    bool rpc_enabled)
{
	struct cixvpu_acpi_boot *b;

	if (dev == NULL || aa == NULL || aa->aa_node == NULL ||
	    aa->aa_node->ad_handle == NULL || resources == NULL || out == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (*out != NULL)
		return EBUSY;
	b = kmem_zalloc(sizeof(*b), KM_NOSLEEP);
	if (b == NULL)
		return ENOMEM;
	b->dev = dev;
	b->aa = *aa;
	b->resources = *resources;
	b->rpc_enabled = rpc_enabled;
	*out = b;
	return 0;
}

int
cixvpu_acpi_boot_create(device_t dev, const struct acpi_attach_args *aa,
    const struct cixvpu_resources *resources, struct cixvpu_acpi_boot **out)
{

	return cixvpu_boot_create(dev, aa, resources, out, false);
}

int
cixvpu_acpi_boot_create_rpc(device_t dev, const struct acpi_attach_args *aa,
    const struct cixvpu_resources *resources, struct cixvpu_acpi_boot **out)
{

	return cixvpu_boot_create(dev, aa, resources, out, true);
}

int
cixvpu_acpi_boot_once(struct cixvpu_acpi_boot *b,
    struct cixvpu_prepared **owned)
{
	uint32_t root;
	int error;

	if (b == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (b->report.started)
		return EALREADY;
	if (owned == NULL || *owned == NULL ||
	    cixvpu_boot_overlap(b, owned, sizeof(*owned)))
		return EINVAL;
	b->report.started = true;
	b->report.stage = CIXVPU_BOOT_SYNC;
	error = cixvpu_prepared_sync_initial(*owned);
	if (error != 0) {
		b->report.error = error;
		b->report.last_stage = b->report.stage;
		return error;
	}
	b->prepared = *owned;
	*owned = NULL;
	b->report.moved = true;
	b->report.stage = CIXVPU_BOOT_CLAIM;
	error = cixvpu_control_claim_native(&b->control, b->dev,
	    &b->aa, &b->resources);
	b->control.report.error = error;
	if (error != 0)
		goto failed;
	error = cixvpu_control_core_resources(&b->control, b->cores, b->powers);
	if (error != 0)
		goto failed;
	b->report.stage = CIXVPU_BOOT_IRQ;
	b->irq_cookie = intr_establish_xname_masked(358, IPL_VM,
	    IST_LEVEL | IST_MPSAFE, cixvpu_boot_interrupt, b, device_xname(b->dev));
	if (b->irq_cookie == NULL) {
		error = EBUSY;
		goto failed;
	}
	atomic_store_release(&b->irq_masked, 1);
	b->report.stage = CIXVPU_BOOT_IDENTITY;
	error = cixvpu_control_activate(&b->control, &b->resources);
	b->control.report.error = error;
	if (error != 0)
		goto failed;
	error = cixvpu_control_boot_profile(&b->control, &b->resources);
	b->control.report.error = error;
	if (error != 0)
		goto failed;
	if ((error = cixvpu_boot_setup(b)) != 0)
		goto failed;
	atomic_store_release(&b->irq_ready, 1);
	atomic_store_release(&b->irq_masked, 0);
	intr_unmask(b->irq_cookie);
	if (atomic_load_acquire(&b->irq_unexpected) != 0) {
		error = EIO;
		goto failed;
	}
	b->report.stage = CIXVPU_BOOT_EXPOSE;
	error = cixvpu_prepared_publish_root(b->prepared, &root);
	if (error != 0)
		goto failed;
	b->report.exposed = true;
	if (atomic_load_acquire(&b->irq_unexpected) != 0) {
		error = EIO;
		goto held;
	}
	b->report.stage = CIXVPU_BOOT_START;
	if ((error = cixvpu_boot_start(b, root)) == 0)
		error = cixvpu_boot_receive(b);
held:
	cixvpu_boot_mask(b);
	/* A final receive/ACK can observe a fault after the visit check. */
	if (error == 0 && atomic_load_acquire(&b->irq_unexpected) != 0)
		error = EIO;
	atomic_store_release(&b->irq_ready, 0);
	b->report.held = true;
	b->report.last_stage = b->report.stage;
	b->report.stage = CIXVPU_BOOT_HELD;
	b->report.error = error;
	return error;
failed:
	b->report.error = error;
	b->report.last_stage = b->report.stage;
	b->report.cleanup_error = cixvpu_boot_unwind(b);
	b->report.held = b->report.cleanup_error != 0;
	return error;
}

int
cixvpu_acpi_boot_report(const struct cixvpu_acpi_boot *b,
    struct cixvpu_boot_report *out)
{
	struct cixvpu_boot_report report;

	if (b == NULL || out == NULL || cixvpu_boot_overlap(b, out, sizeof(*out)))
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	report = b->report;
	report.control = b->control.report;
	report.profile = b->control.profile;
	report.prepared_retained = b->prepared != NULL;
	report.irq_registered = b->irq_cookie != NULL;
	report.irq_masked = atomic_load_acquire(&b->irq_masked) != 0;
	report.irq_seen = atomic_load_acquire(&b->irq_seen);
	report.irq_unexpected = atomic_load_acquire(&b->irq_unexpected);
	*out = report;
	return 0;
}
