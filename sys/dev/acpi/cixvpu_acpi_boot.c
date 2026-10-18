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
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
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
	bool rpc_enabled, frame_enabled;
	uint8_t *frame_result;
	struct sysctllog *frame_log;
	uint8_t frame_status[96];
	volatile unsigned int frame_frozen;
	struct cixvpu_boot_report report;
};

static volatile unsigned int cixvpu_frame_claimed;

static int
cixvpu_frame_parent(SYSCTLFN_ARGS)
{
	struct sysctlnode request, children[2];
	int error;

	if (name == NULL || namelen != 1 || rnode == NULL)
		return EINVAL;
	if (name[0] == CTL_QUERY) {
		unsigned int i;

		/* Copy one request; a second user read could change its version. */
		if (newp == NULL || newlen != sizeof(request))
			return EINVAL;
		error = sysctl_copyin(l, newp, &request, sizeof(request));
		if (error != 0)
			return error;
		if (SYSCTL_VERS(request.sysctl_flags) != SYSCTL_VERSION)
			return EINVAL;
		/* libc discovery uses zero. Do not enter native versioned-query. */
		if (request.sysctl_ver != 0)
			return ENOTSUP;

		if (rnode->sysctl_clen > __arraycount(children) ||
		    (rnode->sysctl_clen != 0 && rnode->sysctl_child == NULL))
			return EIO;
		/* An overlay could replace this sanitized list with foreign nodes. */
		if (l != NULL && l->l_proc->p_emul->e_sysctlovly != NULL)
			return ENOTSUP;
		memset(children, 0, sizeof(children));
		for (i = 0; i < rnode->sysctl_clen; i++) {
			const struct sysctlnode *source = &rnode->sysctl_child[i];

			children[i].sysctl_flags = source->sysctl_flags;
			children[i].sysctl_num = source->sysctl_num;
			memcpy(children[i].sysctl_name, source->sysctl_name,
			    sizeof(children[i].sysctl_name));
			children[i].sysctl_ver = source->sysctl_ver;
			children[i].sysctl_size = source->sysctl_size;
		}
		/* Match the native full-record short-buffer rule, never a prefix. */
		for (i = 0; i < rnode->sysctl_clen; i++) {
			if (oldp != NULL && *oldlenp >= (i + 1) * sizeof(children[0])) {
				error = sysctl_copyout(l, &children[i],
				    (uint8_t *)oldp + i * sizeof(children[0]),
				    sizeof(children[0]));
				if (error != 0)
					return error;
			}
		}
		*oldlenp = rnode->sysctl_clen * sizeof(children[0]);
		return 0;

	}
	if (newp != NULL)
		return EPERM;
	if (name[0] == CTL_DESCRIBE)
		return sysctl_describe(SYSCTLFN_CALL(rnode));
	return EINVAL;

}

static int
cixvpu_frame_lookup(bool status, SYSCTLFN_ARGS)
{
	struct sysctlnode node;
	const struct cixvpu_acpi_boot *b;

	if (newp != NULL)
		return EPERM;
	if (namelen != 0 || rnode == NULL || rnode->sysctl_data == NULL)
		return EINVAL;
	b = rnode->sysctl_data;
	if (atomic_load_acquire(&b->frame_frozen) == 0)
		return EAGAIN;
	if (!status && b->report.frame_output_bytes == 0)
		return ENODATA;
	node = *rnode;
	node.sysctl_data = status ? __UNCONST(b->frame_status) : b->frame_result;
	node.sysctl_size = status ? sizeof(b->frame_status) :
	    b->report.frame_output_bytes;
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
cixvpu_frame_bytes(SYSCTLFN_ARGS)
{

	return cixvpu_frame_lookup(false, SYSCTLFN_CALL(rnode));
}

static int
cixvpu_frame_status(SYSCTLFN_ARGS)
{

	return cixvpu_frame_lookup(true, SYSCTLFN_CALL(rnode));
}

static int
cixvpu_frame_register(struct cixvpu_acpi_boot *b)
{
	const struct sysctlnode *parent = NULL;
	int error;

	error = sysctl_createv(&b->frame_log, 0, NULL, &parent,
	    CTLFLAG_READONLY | CTLFLAG_PRIVATE, CTLTYPE_NODE, device_xname(b->dev),
	    SYSCTL_DESCR("Closed CIX VPU frame trial"), cixvpu_frame_parent,
	    0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL);
	if (error == 0)
		error = sysctl_createv(&b->frame_log, 0, &parent, NULL,
		    CTLFLAG_READONLY, CTLTYPE_STRUCT, "hevc_frame",
		    SYSCTL_DESCR("Immutable host copy; inspect frame_status first"),
		    cixvpu_frame_bytes, 0, b, CIXVPU_BOOT_FRAME_BYTES,
		    CTL_CREATE, CTL_EOL);
	if (error == 0)
		error = sysctl_createv(&b->frame_log, 0, &parent, NULL,
		    CTLFLAG_READONLY, CTLTYPE_STRUCT, "frame_status",
		    SYSCTL_DESCR("Version1, twenty-four LE32 numeric words"),
		    cixvpu_frame_status, 0, b, sizeof(b->frame_status),
		    CTL_CREATE, CTL_EOL);
	return error;
}

static void
cixvpu_frame_freeze(struct cixvpu_acpi_boot *b)
{
	const struct cixvpu_boot_report *r = &b->report;
	const uint32_t words[24] = {
		1, 96, (uint32_t)r->error, (uint32_t)r->cleanup_error,
		(uint32_t)r->last_stage, r->frame_action, r->frame_option,
		r->frame_complete ? 1 : 0, r->frame_output_bytes, r->frame_output_returns,
		r->frame_input_flags, r->frame_output_flags,
		r->frame_visits, r->frame_messages, r->frame_received_bytes,
		r->frame_publications, r->frame_notifications,
		r->rpc_commits, r->rpc_notifications,
		atomic_load_acquire(&b->irq_seen), atomic_load_acquire(&b->irq_unexpected),
		r->rpc_state, r->rpc_callid, r->rpc_size
	};
	unsigned int i;

	if (!b->frame_enabled)
		return;
	for (i = 0; i < __arraycount(words); i++) {
		b->frame_status[4 * i] = (uint8_t)words[i];
		b->frame_status[4 * i + 1] = (uint8_t)(words[i] >> 8);
		b->frame_status[4 * i + 2] = (uint8_t)(words[i] >> 16);
		b->frame_status[4 * i + 3] = (uint8_t)(words[i] >> 24);
	}
	atomic_store_release(&b->frame_frozen, 1);
}

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
	b->report.rpc_resize_valid = header.resize_valid;
	b->report.rpc_resize_address = header.resize_address;
	b->report.rpc_resize_new_size = header.resize_new_size;
}

static bool
cixvpu_boot_bootstrap_complete(const struct cixvpu_acpi_boot *b)
{
	const struct cixvpu_boot_report *r = &b->report;

	return r->switched_in && r->pong && (!b->frame_enabled ||
	    (r->rpc_alloc_replied && r->rpc_resize_replied &&
	    r->rpc_second_alloc_replied));
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
				    result == CIXVPU_BOOT_RPC_DEBUG_REPLIED ||
				    result == CIXVPU_BOOT_RPC_RESIZE_REPLIED ||
				    result == CIXVPU_BOOT_RPC_SECOND_ALLOC_REPLIED) {
					/* The closed step has already committed RETURN. */
					b->report.rpc_commits++;
					if (result == CIXVPU_BOOT_RPC_ALLOC_REPLIED)
						b->report.rpc_alloc_replied = true;
					if (result == CIXVPU_BOOT_RPC_RESIZE_REPLIED)
						b->report.rpc_resize_replied = true;
					if (result == CIXVPU_BOOT_RPC_SECOND_ALLOC_REPLIED)
						b->report.rpc_second_alloc_replied = true;
					if (b->report.rpc_commits > 19)
						return EPROTO;
					if (atomic_load_acquire(&b->irq_unexpected) != 0)
						return EIO;
					cixvpu_boot_write(b, false, 0x220, 1);
					b->report.rpc_notifications++;
					if (atomic_load_acquire(&b->irq_unexpected) != 0)
						return EIO;
					if (cixvpu_boot_bootstrap_complete(b))
						return 0;
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
			if (cixvpu_boot_bootstrap_complete(b))
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
cixvpu_acpi_boot_create_frame(device_t dev, const struct acpi_attach_args *aa,
    const struct cixvpu_resources *resources, struct cixvpu_acpi_boot **out)
{
	int error;

	error = cixvpu_boot_create(dev, aa, resources, out, true);
	if (error != 0)
		return error;
	if (atomic_cas_uint(&cixvpu_frame_claimed, 0, 1) != 0) {
		kmem_free(*out, sizeof(**out));
		*out = NULL;
		return EBUSY;
	}
	(*out)->frame_result = kmem_zalloc(CIXVPU_BOOT_FRAME_BYTES, KM_NOSLEEP);
	if ((*out)->frame_result == NULL) {
		kmem_free(*out, sizeof(**out));
		*out = NULL;
		atomic_store_release(&cixvpu_frame_claimed, 0);
		return ENOMEM;
	}
	(*out)->frame_enabled = true;
	error = cixvpu_frame_register(*out);
	if (error != 0) {
		sysctl_teardown(&(*out)->frame_log);
		kmem_free((*out)->frame_result, CIXVPU_BOOT_FRAME_BYTES);
		kmem_free(*out, sizeof(**out));
		*out = NULL;
		atomic_store_release(&cixvpu_frame_claimed, 0);
	}
	return error;
}

/* Separate fixed one-frame policy. No caller can restart a held attempt. */
static int
cixvpu_frame_guard(const struct cixvpu_acpi_boot *b, uint64_t start)
{
	uint64_t now;

	if (atomic_load_acquire(&b->irq_unexpected) != 0)
		return EIO;
	now = cixvpu_boot_now();
	if (now < start)
		return EIO;
	return now - start >= 5000000 ? ETIMEDOUT : 0;
}

/* Called only after a successful commit. No retry of a partial notification. */
static int
cixvpu_frame_notify(struct cixvpu_acpi_boot *b, uint64_t start, bool rpc)
{
	int error;

	error = cixvpu_frame_guard(b, start);
	if (error != 0)
		return error;
	cixvpu_boot_write(b, false, 0x220, 1);
	b->report.frame_notifications++;
	if (rpc)
		b->report.rpc_notifications++;
	return cixvpu_frame_guard(b, start);
}

static void
cixvpu_frame_charge(struct cixvpu_acpi_boot *b, unsigned int *visit_bytes,
    unsigned int bytes)
{

	/* The maximum next shape was reserved before its consuming operation. */
	*visit_bytes += bytes;
	b->report.frame_received_bytes += bytes;
}

static int
cixvpu_frame_rpc(struct cixvpu_acpi_boot *b, uint64_t start,
    unsigned int *bytes, bool *progress)
{
	enum cixvpu_boot_rpc_result result;
	bool pending;
	int error;

	/* At a global limit, acquire a fresh read-only gate, never stale cache. */
	if (b->report.frame_publications == 32 ||
	    b->report.frame_received_bytes > 65536 - 1024 || *bytes > 4096 - 1024) {
		error = cixvpu_prepared_queue_rpc_pending(b->prepared, &pending);
		cixvpu_boot_observe_rpc(b);
		if (error != 0 || !pending)
			return error;
		return *bytes > 4096 - 1024 ? EWOULDBLOCK : E2BIG;
	}
	error = cixvpu_prepared_boot_rpc_step(b->prepared, &result);
	cixvpu_boot_observe_rpc(b);
	if (error != 0)
		return error;
	if (result == CIXVPU_BOOT_RPC_IDLE || result == CIXVPU_BOOT_RPC_PENDING)
		return 0;
	if (result != CIXVPU_BOOT_RPC_DEBUG_REPLIED &&
	    result != CIXVPU_BOOT_RPC_ALLOC_REPLIED &&
	    result != CIXVPU_BOOT_RPC_RESIZE_REPLIED &&
	    result != CIXVPU_BOOT_RPC_SECOND_ALLOC_REPLIED)
		return EPROTO;
	b->report.rpc_commits++;
	b->report.frame_publications++;
	cixvpu_frame_charge(b, bytes, 12 + b->report.rpc_size);
	*progress = true;
	return cixvpu_frame_notify(b, start, true);
}

static int
cixvpu_frame_publish(struct cixvpu_acpi_boot *b, unsigned int action,
    bool idle_ack, bool refill)
{
	struct cixvpu_queue_command command = { CIXVPU_Q_JOB_ONE, 0 };
	struct cixvpu_queue_input_param param;
	static const enum cixvpu_queue_command_kind options[] = {
		CIXVPU_Q_NALU_START_CODES, CIXVPU_Q_HEVC_MAIN,
		CIXVPU_Q_PFRAMES_ZERO, CIXVPU_Q_BFRAMES_ZERO, CIXVPU_Q_FIXED_QP
	};

	if (idle_ack) {
		command.kind = CIXVPU_Q_IDLE_ACK;
	} else if (refill || action == 11) {
		return cixvpu_prepared_queue_submit(b->prepared, CIXVPU_Q_OUTPUT_EMPTY);
	} else if (action == 10) {
		return cixvpu_prepared_queue_submit(b->prepared, CIXVPU_Q_INPUT_ONCE);
	} else if (action == 8 || action == 9) {
		param.kind = action == 8 ? CIXVPU_Q_FRAME_RATE : CIXVPU_Q_RC_OFF;
		param.value = action == 8 ? 30U << 16 : 0;
		return cixvpu_prepared_queue_input_param(b->prepared, &param);
	} else if (action >= 1 && action <= 5) {
		command.kind = options[action - 1];
		command.value = action == 2 ? 13 : action == 5 ? 26 : 0;
	} else if (action == 6 || action == 7) {
		command.kind = action == 6 ? CIXVPU_Q_GO : CIXVPU_Q_PING;
	} else if (action != 0) {
		return EINVAL;
	}
	return cixvpu_prepared_queue_command(b->prepared, &command);
}

static int
cixvpu_frame_event(struct cixvpu_acpi_boot *b,
    const struct cixvpu_queue_event *event, bool *waiting,
    unsigned int *pending,
    bool *idle_ack, unsigned int *size)
{
	const struct cixvpu_command_response *r = &event->response;

	*size = 4;
	switch (event->kind) {
	case CIXVPU_Q_INPUT_AVAILABLE:
		*pending = 1;
		return 0;
	case CIXVPU_Q_OUTPUT_AVAILABLE:
		*pending = 2;
		return 0;
	case CIXVPU_Q_PONG:
		return 0;
	case CIXVPU_Q_IDLE:
		if (*idle_ack)
			return EPROTO;
		*idle_ack = true;
		return 0;
	case CIXVPU_Q_RESPONSE:
		break;
	default:
		return EPROTO;
	}
	switch (r->kind) {
	case CIXVPU_COMMAND_CONFIRMED:
		*size += r->index_valid ? 4 : 0;
		if (!*waiting || (r->index_valid && r->index != b->report.frame_option))
			return EPROTO;
		*waiting = false;
		return 0;
	case CIXVPU_COMMAND_JOB_DEQUEUED:
	case CIXVPU_COMMAND_STATE:
		*size += 4;
		return 0;
	case CIXVPU_COMMAND_FATAL:
	case CIXVPU_COMMAND_OPTION_FAILED:
		*size += 132;
		return EIO;
	default:
		return EPROTO;
	}
}

static int
cixvpu_frame_run(struct cixvpu_acpi_boot *b)
{
	const struct cixvpu_queue_identity identity = { 1, 2, 3 };
	static const uint32_t indices[] = { 1, 3, 16, 17, 56 };
	uint64_t start = cixvpu_boot_now();
	unsigned int action = 0, pending = 0, visit;
	bool waiting = false, idle_ack = false, refill = false;
	int error;

	b->report.stage = CIXVPU_BOOT_FRAME_OPEN;
	error = cixvpu_frame_guard(b, start);
	if (error == 0)
		error = cixvpu_prepared_queue_open(b->prepared, &identity);
	if (error != 0)
		return error;
	for (visit = 0; visit < 5001; visit++) {
		unsigned int calls = 0, bytes = 0;
		bool progress = false;

		b->report.frame_visits++;
		while (calls < 16) {
			bool rpc_progress = false;

			error = cixvpu_frame_guard(b, start);
			if (error != 0)
				return error;
			calls++;
			error = cixvpu_frame_rpc(b, start, &bytes, &rpc_progress);
			if (error == EAGAIN || error == EWOULDBLOCK)
				break;
			if (error != 0)
				return error;
			if (rpc_progress) {
				progress = true;
				continue;
			}
			/* A read-only RPC observation can race a clock/IRQ fault too. */
			error = cixvpu_frame_guard(b, start);
			if (error != 0)
				return error;
			if (calls == 16)
				break;
			if (pending != 0) {
				unsigned int maximum = pending == 1 ? 76 : 44;

				if (bytes > 4096 - maximum)
					break;
				if (b->report.frame_received_bytes > 65536 - maximum)
					return E2BIG;
				calls++;
				b->report.stage = CIXVPU_BOOT_FRAME_COPY;
				if (pending == 1) {
					struct cixvpu_buffer_frame_result result;

					error = cixvpu_prepared_queue_input_return(b->prepared, &result);
					if (error == 0) {
						b->report.frame_input_flags = result.flags;
						if (result.rejected || result.corrupt)
							return EIO;
						b->report.frame_input_accepted = true;
					}
				} else {
					struct cixvpu_buffer_bitstream_result result;

					if (b->report.frame_output_returns == 4)
						return E2BIG;
					error = cixvpu_prepared_queue_output_copy(b->prepared,
					    b->frame_result + b->report.frame_output_bytes,
					    CIXVPU_BOOT_FRAME_BYTES - b->report.frame_output_bytes, &result);
					if (error == 0) {
						b->report.frame_output_returns++;
						b->report.frame_output_bytes += result.filled;
						b->report.frame_output_flags = result.flags;
						if ((result.flags & ~UINT32_C(0x1000b1)) != 0)
							return ENOTSUP;
						b->report.frame_output_eof = (result.flags & UINT32_C(0x100010)) != 0;
						refill = !b->report.frame_output_eof;
						if (refill && b->report.frame_output_returns == 4)
							return E2BIG;
					}
				}
				if (error == EAGAIN)
					break;
				if (error != 0)
					return error;
				cixvpu_frame_charge(b, &bytes, maximum);
				pending = 0;
				progress = true;
				if (b->report.frame_input_accepted && b->report.frame_output_eof) {
					if (b->report.frame_output_bytes == 0)
						return EPROTO;
					error = cixvpu_frame_guard(b, start);
					if (error != 0)
						return error;
					b->report.frame_complete = true;
					b->report.stage = CIXVPU_BOOT_FRAME_COMPLETE;
					return 0;
				}
				continue;
			}
			if (bytes > 4096 - 136)
				break;
			if (b->report.frame_received_bytes > 65536 - 136)
				return E2BIG;
			if (b->report.frame_messages == 128)
				return E2BIG;
			{
				struct cixvpu_queue_event event;
				unsigned int size;

				calls++;
				error = cixvpu_prepared_queue_receive(b->prepared, &event);
				if (error == 0) {
					b->report.frame_messages++;
					error = cixvpu_frame_event(b, &event, &waiting,
					    &pending, &idle_ack, &size);
					cixvpu_frame_charge(b, &bytes, size);
					if (error != 0)
						return error;
					progress = true;
					continue;
				}
			}
			if (error != EAGAIN)
				return error;
			if (calls == 16 || (waiting && !idle_ack) ||
			    (action == 12 && !refill && !idle_ack))
				break;
			if (b->report.frame_publications == 32)
				return E2BIG;
			error = cixvpu_frame_guard(b, start);
			if (error != 0)
				return error;
			calls++;
			b->report.frame_action = action;
			b->report.stage = action <= 5 ? CIXVPU_BOOT_FRAME_SETUP :
			    CIXVPU_BOOT_FRAME_RUN;
			error = cixvpu_frame_publish(b, action, idle_ack, refill);
			if (error == EAGAIN)
				break;
			if (error != 0)
				return error;
			b->report.frame_publications++;
			if (idle_ack) {
				idle_ack = false;
			} else if (refill) {
				refill = false;
			} else {
				if (action >= 1 && action <= 5) {
					b->report.frame_option = indices[action - 1];
					waiting = true;
				}
				action++;
			}
			error = cixvpu_frame_notify(b, start, false);
			if (error != 0)
				return error;
			progress = true;
		}
		if (!progress && visit != 5000)
			delay(1000);
	}
	return ETIMEDOUT;
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
		cixvpu_frame_freeze(b);
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
	if (error == 0 && b->frame_enabled)
		error = cixvpu_frame_run(b);
held:
	if (b->frame_enabled)
		(void)cixvpu_prepared_queue_hold(b->prepared, error);
	cixvpu_boot_mask(b);
	/* A final receive/ACK can observe a fault after the visit check. */
	if (error == 0 && atomic_load_acquire(&b->irq_unexpected) != 0)
		error = EIO;
	atomic_store_release(&b->irq_ready, 0);
	b->report.held = true;
	b->report.last_stage = b->report.stage;
	b->report.stage = CIXVPU_BOOT_HELD;
	b->report.error = error;
	cixvpu_frame_freeze(b);
	return error;
failed:
	b->report.error = error;
	b->report.last_stage = b->report.stage;
	b->report.cleanup_error = cixvpu_boot_unwind(b);
	b->report.held = b->report.cleanup_error != 0;
	cixvpu_frame_freeze(b);
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
