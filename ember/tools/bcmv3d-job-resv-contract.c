/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual TFU/CSD/CL, BO and reservation helper; controlled DMA/IRQ services. */
#define BCMV3D_WAIT_CONTRACT
#include "bcm2712-v3d-cl-contract.c"
static struct drm_gem_object *submitted[2];
static unsigned producer_signals;
static void
producer_signal(struct dma_fence *f)
{
	CHECK(resv_locks == 0 && resv_contexts == 0 && resv_arrays == 0);
	CHECK(submitted[0]->resv->exclusive == f && submitted[1]->resv->exclusive == f);
	CHECK(f->refs >= 3 && f->context != 0 && f->seqno != 0);
	producer_signals++;
}
static struct dma_fence *
previous_fence(int status)
{
	static const struct dma_fence_ops ops = {.use_64bit_seqno = true};
	struct dma_fence *f = calloc(1, sizeof(*f));
	CHECK(f != NULL);
	dma_fence_init(f, &ops, NULL, 99, 1); f->status = status;
	return f;
}
static void
run(unsigned engine, unsigned mode)
{
	struct bcmv3d_device c;
	struct drm_device d;
	struct drm_file file;
	struct drm_v3d_submit_tfu tfu = {0};
	struct drm_v3d_submit_csd csd = {0};
	struct drm_v3d_submit_cl cl = {0};
	uint32_t handles[2];
	int error, expected = 0;
	unsigned before;
	struct dma_fence *prior = NULL, *old = NULL;

	if (engine == 0) {
		tfu = setup(&c, &d, &file);
		handles[0] = tfu.bo_handles[0]; handles[1] = tfu.bo_handles[1];
	} else if (engine == 1)
		csd = csd_setup(&c, &d, &file, handles);
	else
		cl = cl_setup(&c, &d, &file, handles);
	submitted[0] = file.handles[handles[0]]; submitted[1] = file.handles[handles[1]];
	producer_signals = 0; on_signal = producer_signal;
	/* Actual lookup order is source then destination for TFU reservations. */
	unsigned last = engine == 0 ? 0 : 1;
	if (mode >= 6 && mode <= 11) {
		prior = previous_fence(mode == 9 || mode == 11 ? -EIO : 0);
		if (mode >= 10) submitted[last]->resv->shared = dma_fence_get(prior);
		else submitted[last]->resv->exclusive = dma_fence_get(prior);
		resv_wait_fault = true;
		resv_wait_result = mode == 7 ? -EINTR : mode == 8 ? -ERESTARTSYS : 0;
		expected = mode == 7 ? -EINTR : mode == 8 ? -ERESTARTSYS : mode == 9 || mode == 11 ? -EIO : -ETIME;
	}
	if (mode == 2 || mode == 3) {
		resv_lock_error = EINTR; resv_fault_at = mode - 1; expected = -EINTR;
	}
	if (mode == 4 || mode == 5) {
		resv_snapshot_fail = true; resv_fault_at = mode - 3; expected = -ENOMEM;
	}
	if (mode == 12) { fail_fence_alloc = true; expected = -ENOMEM; }
	if (mode == 13) { c.job->fence_seqno = UINT64_MAX; expected = -EOVERFLOW; }
	if (mode == 14) {
		if (engine == 0) { tfu.bo_handles[0] = tfu.bo_handles[1]; tfu.ioa = tfu.iia; }
		else handles[1] = handles[0];
		expected = -EINVAL;
	}
	if (mode == 15) { model_case = 1; expected = -EIO; }
	/* Both prior reader and writer fences must be observed before replacement. */
	if (mode == 16) {
		old = previous_fence(1); prior = previous_fence(-EIO);
		submitted[last]->resv->exclusive = dma_fence_get(old);
		submitted[last]->resv->shared = dma_fence_get(prior);
		expected = -EIO;
	}
	before = reg_calls;
	if (engine == 0) error = bcmv3d_submit_tfu(&d, &tfu, &file);
	else if (engine == 1) error = bcmv3d_submit_csd(&d, &csd, &file);
	else error = bcmv3d_submit_cl(&d, &cl, &file);
	CHECK(error == expected);
	CHECK(resv_locks == 0 && resv_contexts == 0 && resv_arrays == 0);
	if (expected && mode != 15) {
		CHECK(reg_calls == before && producer_signals == 0 && resv_publications == 0);
		CHECK(!c.job->poisoned && submitted[0]->refs == 1 && submitted[1]->refs == 1);
	} else {
		CHECK(producer_signals == 1 && resv_publications == 2);
		struct dma_fence *f = submitted[0]->resv->exclusive;
		CHECK(f && f == submitted[1]->resv->exclusive && f->status == (mode == 15 ? -EIO : 1));
		CHECK(f->context == c.job->fence_context && f->seqno == 1);
		if (mode == 1) {
			/* Keep the old fence alive while both BOs receive a new producer. */
			old = dma_fence_get(f);
			if (engine == 0) error = bcmv3d_submit_tfu(&d, &tfu, &file);
			else if (engine == 1) error = bcmv3d_submit_csd(&d, &csd, &file);
			else error = bcmv3d_submit_cl(&d, &cl, &file);
			CHECK(error == 0 && producer_signals == 2 && resv_publications == 4);
			f = submitted[0]->resv->exclusive;
			CHECK(f != old && f == submitted[1]->resv->exclusive && f->status == 1);
			CHECK(f->seqno == 2 && old->refs == 1 && old->status == 1);
		}
	}
	if (prior) CHECK(prior->refs == 2);
	fail_fence_alloc = false; on_signal = NULL;
	dma_fence_put(prior); dma_fence_put(old);
	if (engine == 0) cleanup(&c, &file, mode == 15);
	else csd_cleanup(&c, &file, mode == 15);
	/* Cleanup requires every BO-owned fence reference to be released. */
	CHECK(fence_count == 0 && sync_count == 0 && resv_objects == 0);
}
int
main(void)
{
	for (unsigned engine = 0; engine < 3; engine++)
		for (unsigned mode = 0; mode <= 16; mode++)
			run(engine, mode);
	printf("PASS actual TFU/CSD/CL implicit BO fences: %u checks, 51 caller cases\n", checks);
	return 0;
}
