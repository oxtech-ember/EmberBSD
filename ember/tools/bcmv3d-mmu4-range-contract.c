/* SPDX-License-Identifier: BSD-2-Clause */
/* Complete actual native/provider/cache bodies and actual GEM backing TU. */
#define WSDISPLAY_BOOTFB_HANDOFF 1
#define BCMV3D_MMU4_RANGE_CONTRACT
#include "bcmv3d-mmu4-native-contract.c"
#undef main

void *range_bridge_bo(unsigned int);
unsigned int range_bridge_refs(void *);
void range_bridge_close(void *);
void range_bridge_begin(unsigned int, unsigned int);
void range_bridge_flushed(void);
void range_bridge_verify(unsigned int, bool);
void range_bridge_retained(unsigned int, void *);
unsigned int range_bridge_allocated(void);
void range_bridge_consumer_counts(unsigned int, unsigned int);

static unsigned int range_case, range_local_writes, range_local_polls;
void *range_bridge_retirement(unsigned int, void *, int);
unsigned int range_bridge_retirement_calls(void);

void *
range_native_owner(void)
{

	return owner;
}

int
range_icac_owned(void)
{

	return bcm2712_iommuc_mutex.held;
}

int
range_native_owned(void)
{

	return owner != NULL && owner->lock.held;
}

int
range_exposure_latched(void)
{

	return owner->status.range.mutation_attempted;
}

/* Called only by the controlled ICAC write service, after the real callback. */
void
range_cache_written(unsigned int count)
{

	if (!range_test_active)
		return;
	CHECK(count > 1 && owner->cache == &storage.core);
	range_bridge_flushed();
	if (range_case == 2)
		poll_timeout = true;
	if (range_case == 5)
		storage.core.status.reserved |= 1;
}

static uint32_t
range_local_read(size_t offset)
{

	CHECK(owner->active == NULL && owner->cache == &storage.core);
	CHECK(owner->executing && owner->status.fault == 0);
	if (offset == 0x18)
		return 0;
	CHECK(offset == 0);
	if (owner->status.range.polls != 0) {
		if (range_case == 3 || range_case == 8 || range_case == 9)
			return 0x04090883;
		if (range_case == 4)
			return 0x04190803;
	}
	return 0x04090803;
}

static void
range_local_write(size_t offset, uint32_t value)
{

	CHECK(offset == 0 && value == 0x04090807);
	CHECK(owner->active == NULL && owner->cache == &storage.core);
	CHECK(owner->status.range.mutation_attempted);
	CHECK(storage.core.status.flush_complete && writes == barriers);
	range_bridge_flushed();
	range_local_writes++;
	range_local_polls = 1;
	local_writes++;
	pending = true;
}

static int
range_call(unsigned int slot, enum bcmv3d_mmu4_range_operation op)
{
	unsigned int before = writes, local_before = range_local_writes;
	int error;

	range_bridge_begin(slot, op);
	error = op == BCMV3D_MMU4_RANGE_PUBLISH ?
	    bcmv3d_mmu4_native_publish_staged_gem(owner, slot) :
	    bcmv3d_mmu4_native_unmap_gem(owner, slot);
	CHECK(owner->active == NULL && owner->cache == NULL && !owner->executing);
	CHECK(!owner->lock.held && !bcm2712_iommuc_mutex.held);
	if (error == 0) {
		CHECK(writes == before + 1 && range_local_writes == local_before + 1);
		CHECK(owner->status.range.complete && owner->status.range.first_error == 0);
		CHECK(owner->status.range.mutation_attempted);
		CHECK(owner->status.range.polls != 0 && owner->status.range.polls <= 1024);
		range_bridge_verify(slot, op == BCMV3D_MMU4_RANGE_PUBLISH);
	}
	return error;
}

static unsigned int range_gate_callbacks;

static int
range_gate_callback(void *arg, struct bcm2712_iommuc_core *cache)
{

	(void)arg;
	CHECK(cache == &storage.core && bcm2712_iommuc_mutex.held);
	range_gate_callbacks++;
	return 0;
}

static void
range_provider_gates(void)
{
	void *saved_arg = storage.core.io.arg;
	unsigned int before = writes + operational_reads;

	CHECK(bcm2712_iommuc_execute_mmu4_retained(
	    NULL, range_gate_callback, NULL) == EINVAL);
	CHECK(bcm2712_iommuc_execute_mmu4_retained(&storage, NULL, NULL) == EINVAL);
	CHECK(bcm2712_iommuc_execute_mmu4_retained(
	    &foreign_storage, range_gate_callback, NULL) == ENXIO);
	hard_irq = true;
	CHECK(bcm2712_iommuc_execute_mmu4_retained(
	    &storage, range_gate_callback, NULL) == EWOULDBLOCK);
	hard_irq = false;
	mutex_enter(&bcm2712_iommuc_mutex);
	CHECK(bcm2712_iommuc_execute_mmu4_retained(
	    &storage, range_gate_callback, NULL) == EDEADLK);
	mutex_exit(&bcm2712_iommuc_mutex);
	storage.core.status.reserved = 3;
	CHECK(bcm2712_iommuc_execute_mmu4_retained(
	    &storage, range_gate_callback, NULL) == EBUSY);
	storage.core.status.reserved = 2;
	storage.core.status.retained = 0;
	CHECK(bcm2712_iommuc_execute_mmu4_retained(
	    &storage, range_gate_callback, NULL) == EBUSY);
	storage.core.status.retained = 2;
	storage.mmu4.first_error = EAGAIN;
	CHECK(bcm2712_iommuc_execute_mmu4_retained(
	    &storage, range_gate_callback, NULL) == EAGAIN);
	storage.mmu4.first_error = 0;
	storage.core.io.arg = NULL;
	CHECK(bcm2712_iommuc_execute_mmu4_retained(
	    &storage, range_gate_callback, NULL) == EBUSY);
	storage.core.io.arg = saved_arg;
	CHECK(range_gate_callbacks == 0 && before == writes + operational_reads);
}

static void
range_consumer(void **bo)
{
	unsigned int before;

	range_bridge_consumer_counts(0, 0);
	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 0, NULL) == EINVAL);
	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 3, bo[0]) == EINVAL);
	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 0, bo[1]) == EXDEV);
	CHECK(bcmv3d_mmu4_native_expose_gem(owner, 0, bo[0]) == EBUSY);
	hard_irq = true;
	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 0, bo[0]) == EWOULDBLOCK);
	hard_irq = false;
	mutex_enter(&owner->lock);
	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 0, bo[0]) == EDEADLK);
	mutex_exit(&owner->lock);
	range_bridge_consumer_counts(0, 0);
	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 0, bo[0]) == 0);
	range_bridge_consumer_counts(1, 0);
	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 0, bo[0]) == EALREADY);
	/* Known never-published cancellation: POSTWRITE, unload, then BO put. */
	CHECK(bcmv3d_mmu4_native_discard_staged_gem(owner, 0) == 0);
	range_bridge_consumer_counts(1, 1);
	CHECK(range_bridge_refs(bo[0]) == 1);
	CHECK(bcmv3d_mmu4_native_stage_gem(owner, 0, bo[0]) == 0);
	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 0, bo[0]) == 0);
	CHECK(range_call(0, BCMV3D_MMU4_RANGE_PUBLISH) == 0);
	/* Known never-HVS-latched translation unmap has the same DMA pairing. */
	CHECK(range_call(0, BCMV3D_MMU4_RANGE_UNMAP) == 0);
	range_bridge_consumer_counts(2, 2);
	CHECK(range_bridge_refs(bo[0]) == 1);
	CHECK(range_call(1, BCMV3D_MMU4_RANGE_PUBLISH) == 0);
	CHECK(bcmv3d_mmu4_native_expose_gem(owner, 1, bo[1]) == EBUSY);
	CHECK(range_call(1, BCMV3D_MMU4_RANGE_UNMAP) == 0);
	CHECK(bcmv3d_mmu4_native_stage_gem(owner, 1, bo[1]) == 0);
	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 1, bo[1]) == 0);
	CHECK(range_call(1, BCMV3D_MMU4_RANGE_PUBLISH) == 0);
	CHECK(bcmv3d_mmu4_native_expose_gem(owner, 1, bo[2]) == EXDEV);
	CHECK(bcmv3d_mmu4_native_expose_gem(owner, 1, bo[1]) == 0);
	range_bridge_close(bo[1]);
	before = local_reads + local_writes + writes;
	CHECK(bcmv3d_mmu4_native_discard_staged_gem(owner, 1) == EBUSY);
	CHECK(bcmv3d_mmu4_native_unmap_gem(owner, 1) == EBUSY);
	CHECK(bcmv3d_mmu4_native_publish_staged_gem(owner, 1) == EBUSY);
	CHECK(bcmv3d_mmu4_native_expose_gem(owner, 1, bo[1]) == EBUSY);
	CHECK(bcmv3d_mmu4_native_stage_gem(owner, 1, bo[2]) == EBUSY);
	CHECK(local_reads + local_writes + writes == before);
	CHECK(range_bridge_refs(bo[1]) == 1 && owner->status.fault == 0);
	range_bridge_consumer_counts(3, 2);
}

static void
range_retirement(void **bo, unsigned int failure)
{
	struct bcmv3d_scanout_frame *proof;
	unsigned int before, calls;
	int error;

	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 0, bo[0]) == 0);
	CHECK(range_call(0, BCMV3D_MMU4_RANGE_PUBLISH) == 0);
	CHECK(bcmv3d_mmu4_native_expose_gem(owner, 0, bo[0]) == 0);
	before = local_reads + local_writes + writes;
	proof = range_bridge_retirement(0, bo[0], EAGAIN);
	CHECK(bcmv3d_mmu4_native_retire_gem(owner, 0, NULL) == EINVAL);
	CHECK(bcmv3d_mmu4_native_retire_gem(owner, 3, proof) == EINVAL);
	hard_irq = true;
	CHECK(bcmv3d_mmu4_native_retire_gem(owner, 0, proof) == EWOULDBLOCK);
	hard_irq = false;
	mutex_enter(&owner->lock);
	CHECK(bcmv3d_mmu4_native_retire_gem(owner, 0, proof) == EDEADLK);
	mutex_exit(&owner->lock);
	CHECK(bcmv3d_mmu4_native_retire_gem(owner, 0, (void *)&before) == EPERM);
	CHECK(bcmv3d_mmu4_native_retire_gem(owner, 0, proof) == EAGAIN);
	(void)range_bridge_retirement(1, bo[0], 0);
	CHECK(bcmv3d_mmu4_native_retire_gem(owner, 0, proof) == EPERM);
	(void)range_bridge_retirement(0, bo[1], 0);
	CHECK(bcmv3d_mmu4_native_retire_gem(owner, 0, proof) == EPERM);
	CHECK(local_reads + local_writes + writes == before);
	CHECK(owner->status.fault == 0 && range_bridge_refs(bo[0]) == 2);
	CHECK(range_bridge_retirement_calls() == 4);
	range_bridge_consumer_counts(1, 0);
	CHECK(bcmv3d_mmu4_native_unmap_gem(owner, 0) == EBUSY);
	proof = range_bridge_retirement(0, bo[0], 0);
	calls = range_bridge_retirement_calls();
	range_bridge_begin(0, BCMV3D_MMU4_RANGE_UNMAP);
	range_case = failure;
	error = bcmv3d_mmu4_native_retire_gem(owner, 0, proof);
	CHECK(range_bridge_retirement_calls() == calls + 1);
	CHECK(!owner->lock.held && !bcm2712_iommuc_mutex.held);
	if (failure != 0) {
		CHECK(error == ETIMEDOUT && owner->status.fault == ETIMEDOUT);
		range_bridge_consumer_counts(1, 0);
		range_bridge_retained(0, bo[0]);
		before = local_reads + local_writes + writes;
		CHECK(bcmv3d_mmu4_native_retire_gem(owner, 0, proof) == ETIMEDOUT);
		CHECK(before == local_reads + local_writes + writes);
		return;
	}
	CHECK(error == 0 && owner->status.range.complete);
	range_bridge_verify(0, false);
	range_bridge_consumer_counts(1, 1);
	CHECK(range_bridge_refs(bo[0]) == 1);
	CHECK(bcmv3d_mmu4_native_retire_gem(owner, 0, proof) == EBUSY);
	/* A fresh staged object cannot inherit the prior retirement token. */
	CHECK(bcmv3d_mmu4_native_stage_gem(owner, 0, bo[0]) == 0);
	CHECK(bcmv3d_mmu4_native_prepare_gem_read(owner, 0, bo[0]) == 0);
	CHECK(range_call(0, BCMV3D_MMU4_RANGE_PUBLISH) == 0);
	CHECK(bcmv3d_mmu4_native_expose_gem(owner, 0, bo[0]) == 0);
	CHECK(bcmv3d_mmu4_native_unmap_gem(owner, 0) == EBUSY);
	range_bridge_consumer_counts(2, 1);
}

static void
range_scenario(unsigned int which)
{
	void *bo[3];
	unsigned int i, cycle, before;
	int error, expected;

	adapter_setup();
	make_owner();
	CHECK(bcmv3d_mmu4_native_publish_staged_gem(owner, 0) == EBUSY);
	CHECK(bcmv3d_mmu4_native_prepare(owner) == 0);
	CHECK(bcmv3d_mmu4_native_program(owner) == 0);
	CHECK(core_init_calls == 1 && writes == 1 && local_writes == 8);
	range_provider_gates();
	range_test_active = true;
	range_case = which;
	before = local_reads + local_writes + writes;
	CHECK(bcmv3d_mmu4_native_publish_staged_gem(owner, 3) == EINVAL);
	CHECK(bcmv3d_mmu4_native_publish_staged_gem(owner, 0) == ENOENT);
	hard_irq = true;
	CHECK(bcmv3d_mmu4_native_publish_staged_gem(owner, 0) == EWOULDBLOCK);
	hard_irq = false;
	mutex_enter(&owner->lock);
	CHECK(bcmv3d_mmu4_native_unmap_gem(owner, 0) == EDEADLK);
	mutex_exit(&owner->lock);
	CHECK(before == local_reads + local_writes + writes);
	for (i = 0; i < 3; i++) {
		bo[i] = range_bridge_bo(3);
		CHECK(bcmv3d_mmu4_native_stage_gem(owner, i, bo[i]) == 0);
		CHECK(range_bridge_refs(bo[i]) == 2);
	}
	CHECK(range_bridge_allocated() == 14);
	if (which >= 11) {
		range_case = 0;
		range_retirement(bo, which == 11 ? 0 : which == 12 ? 2 : 3);
		return;
	}
	if (which == 10) {
		range_case = 0;
		range_consumer(bo);
		return;
	}
	if (which == 0) {
		for (cycle = 0; cycle < 2; cycle++) {
			for (i = 0; i < 3; i++) {
				CHECK(range_call(i, BCMV3D_MMU4_RANGE_PUBLISH) == 0);
				CHECK(bcmv3d_mmu4_native_discard_staged_gem(owner, i) == EBUSY);
				CHECK(bcmv3d_mmu4_native_publish_staged_gem(owner, i) == EBUSY);
			}
			for (i = 0; i < 3; i++) {
				range_bridge_close(bo[i]);
				CHECK(range_bridge_refs(bo[i]) == 1);
				CHECK(range_call(i, BCMV3D_MMU4_RANGE_UNMAP) == 0);
			}
			CHECK(range_bridge_allocated() == 11);
			if (cycle == 0) {
				for (i = 0; i < 3; i++) {
					bo[i] = range_bridge_bo(5);
					CHECK(bcmv3d_mmu4_native_stage_gem(owner, i, bo[i]) == 0);
				}
				CHECK(range_bridge_allocated() == 14);
			}
		}
		CHECK(core_init_calls == 1 && storage.core.status.reserved == 2);
		CHECK(storage.core.status.retained == 2 && storage.mmu4.first_error == 0);
		CHECK(bcmv3d_mmu4_native_program(owner) == EALREADY);
		CHECK(bcmv3d_mmu4_native_prepare(owner) == EALREADY);
		return;
	}
	if (which == 7 || which == 8) {
		range_case = 0;
		CHECK(range_call(0, BCMV3D_MMU4_RANGE_PUBLISH) == 0);
		range_case = which;
	}
	if (which == 1 || which == 7)
		operational_value = 4;
	if (which == 6 || which == 9)
		snapshot_failure = snapshots + 1;
	error = range_call(0, which == 7 || which == 8 ?
	    BCMV3D_MMU4_RANGE_UNMAP : BCMV3D_MMU4_RANGE_PUBLISH);
	expected = which == 1 || which == 7 ? EBUSY :
	    which == 2 || which == 3 || which == 8 || which == 9 ? ETIMEDOUT :
	    which == 5 ? EPROTO : which == 6 ? EAGAIN : EIO;
	CHECK(error == expected && owner->status.range.first_error == expected);
	CHECK(!owner->status.range.complete && owner->status.fault == expected);
	if (which == 9)
		CHECK(owner->status.status_error == EAGAIN && error == ETIMEDOUT);
	range_bridge_retained(0, bo[0]);
	before = local_reads + local_writes + writes;
	CHECK(bcmv3d_mmu4_native_publish_staged_gem(owner, 0) == expected);
	CHECK(bcmv3d_mmu4_native_unmap_gem(owner, 0) == expected);
	CHECK(local_reads + local_writes + writes == before);
	CHECK(core_init_calls == 1 && storage.mmu4.first_error == 0);
}

int
main(void)
{
	unsigned int which;
	pid_t child;
	int status;
	const char *control = getenv("RANGE_HARNESS_CONTROL");

	if (control != NULL) {
		if (strcmp(control, "segv") == 0)
			raise(SIGSEGV);
		if (strcmp(control, "abort") == 0)
			abort();
		if (strcmp(control, "hang") == 0) {
			for (;;)
				pause();
		}
		CHECK(false);
	}
	for (which = 0; which < 14; which++) {
		child = fork();
		CHECK(child >= 0);
		if (child == 0) {
			range_scenario(which);
			_Exit(0);
		}
		CHECK(waitpid(child, &status, 0) == child);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			abort();
	}
	puts("PASS actual native/provider/BO range: two cycles, "
	    "fixed6L2,14 isolated boots, consumer DMA, exposure and retirement");
	return 0;
}
