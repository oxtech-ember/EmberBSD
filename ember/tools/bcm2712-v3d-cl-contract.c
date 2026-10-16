/* Origin: EmberBSD actual-source native CL contract, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define BCMV3D_CL_CONTRACT
#include "bcm2712-v3d-sync-contract.c"

static unsigned cl_waits;
static bool cl_close_on_wait;
static void
cl_inspect_wait(void)
{

	CHECK(active_job->lock == 0 && old_fence->refs > 0);
	if (active_file->syncs[1]) CHECK(active_file->syncs[1]->fence == old_fence);
	cl_waits++;
	if (cl_close_on_wait && cl_waits == 1) {
		drm_syncobj_put(active_file->syncs[1]);
		active_file->syncs[1] = NULL;
		for (unsigned h = 1; h < 16; h++)
			if (active_file->handles[h])
				close_handle(active_file, h);
	}
}

static struct drm_v3d_submit_cl
cl_setup(struct bcmv3d_device *c, struct drm_device *d, struct drm_file *f,
    uint32_t handles[2])
{

	(void)csd_setup(c, d, f, handles);
	cl_model = true;
	uint32_t src = bcmv3d_to_bo(f->handles[handles[0]])->gpu;
	uint32_t dst = bcmv3d_to_bo(f->handles[handles[1]])->gpu;
	return (struct drm_v3d_submit_cl){
	    .bcl_start = src, .bcl_end = src + 64,
	    .rcl_start = src + 4096, .rcl_end = src + 4224,
	    .qma = dst, .qms = 8192, .qts = dst + 8192,
	    .bo_handles = (uintptr_t)handles, .bo_handle_count = 2};
}

int
main(void)
{
	struct bcmv3d_device c;
	struct drm_device d;
	struct drm_file f, foreign = {0};
	struct drm_v3d_submit_cl a, b;
	uint32_t handles[2], saved;
	unsigned calls;

	a = cl_setup(&c, &d, &f, handles);
	for (unsigned n = 0; n < 14; n++) {
		b = a;
		switch (n) {
		case 0: b.flags = 2; break;
		case 1: b.pad = 1; break;
		case 2: b.perfmon_id = 1; break;
		case 3: b.extensions = 8; break;
		case 4: b.bo_handle_count = 0; break;
		case 5: b.bo_handle_count = 65; break;
		case 6: b.bcl_start = 0; break;
		case 7: b.bcl_end = 0; break;
		case 8: b.rcl_start = b.rcl_end; break;
		case 9: b.rcl_end = UINT32_MAX; break;
		case 10: b.qma++; break;
		case 11: b.qms = 32768; break;
		case 12: b.qts = 0; break;
		case 13: b.qts++; break;
		}
		calls = reg_calls;
		CHECK(bcmv3d_submit_cl(&d, &b, &f) == -EINVAL);
		CHECK(reg_calls == calls && !c.job->poisoned && c.job->nbo == 0);
	}
	b = a; b.bo_handles = 1;
	CHECK(bcmv3d_submit_cl(&d, &b, &f) == -EFAULT);
	CHECK(bcmv3d_submit_cl(&d, &a, &foreign) == -ENOENT);
	saved = handles[1]; handles[1] = handles[0];
	CHECK(bcmv3d_submit_cl(&d, &a, &f) == -EINVAL);
	handles[1] = saved;
	reg_calls = 0;
	CHECK(bcmv3d_submit_cl(&d, &a, &f) == 0);
	calls = reg_calls;
	CHECK(cl_bins == 1 && cl_renders == 1 && cache_phase == 3 && revoked);
	for (unsigned n = 0; n < 260; n++)
		CHECK(bcmv3d_submit_cl(&d, &a, &f) == 0);
	b = a; b.bcl_start = b.bcl_end; b.flags = DRM_V3D_SUBMIT_CL_FLUSH_CACHE;
	CHECK(bcmv3d_submit_cl(&d, &b, &f) == 0);
	CHECK(cl_bins == 261 && cl_renders == 262);
	/* A branch can finish in a different admitted BO, even below its start. */
	b = a; b.bcl_start = a.qma + 4096; b.bcl_end = a.bcl_end;
	b.rcl_end = a.qma + 128;
	CHECK(bcmv3d_submit_cl(&d, &b, &f) == 0);
	csd_cleanup(&c, &f, false);
	for (unsigned n = 0; n < 2; n++) {
		a = cl_setup(&c, &d, &f, handles);
		if (n == 0) core_regs[V3D_INT_STS/4] = 2;
		else regs[V3D_INT_STS/4] = 1;
		CHECK(bcmv3d_submit_cl(&d, &a, &f) != 0);
		CHECK(cl_bins == 0 && cl_renders == 0);
		csd_cleanup(&c, &f, true);
	}
	for (unsigned n = 1; n <= calls; n++) {
		a = cl_setup(&c, &d, &f, handles);
		reg_calls = 0; fail_reg = n;
		CHECK(bcmv3d_submit_cl(&d, &a, &f) != 0);
		unsigned before = cl_bins + cl_renders;
		CHECK(bcmv3d_submit_cl(&d, &a, &f) == -EIO);
		CHECK(before == cl_bins + cl_renders);
		csd_cleanup(&c, &f, true);
	}
	const unsigned failures[] = {1, 20, 21, 22, 31, 32, 33, 34, 35, 36};
	for (unsigned n = 0; n < __arraycount(failures); n++) {
		a = cl_setup(&c, &d, &f, handles); model_case = failures[n];
		CHECK(bcmv3d_submit_cl(&d, &a, &f) != 0);
		csd_cleanup(&c, &f, true);
	}
	a = cl_setup(&c, &d, &f, handles); model_case = 11;
	CHECK(bcmv3d_submit_cl(&d, &a, &f) == 0 && sleeps == 10);
	csd_cleanup(&c, &f, false);
	for (unsigned mode = 0; mode < 9; mode++) {
		a = cl_setup(&c, &d, &f, handles); setup_sync(&c, &f);
		a.in_sync_bcl = a.in_sync_rcl = a.out_sync = 1;
		cl_waits = 0; cl_close_on_wait = mode == 4; on_wait = cl_inspect_wait;
		if (mode == 1) { old_fence->status = 0; input_wait_result = 0; }
		if (mode == 2) old_fence->status = -EIO;
		if (mode == 3) fail_fence_alloc = true;
		if (mode == 5) model_case = 1;
		if (mode == 6) { old_fence->status = 0; input_wait_result = -EINTR; }
		if (mode == 7) a.in_sync_rcl = 3;
		if (mode == 8) a.out_sync = 3;
		const int expected[] = {0, -ETIME, -EIO, -ENOMEM, 0, -EIO,
		    -EINTR, -ENOENT, -ENOENT};
		CHECK(bcmv3d_submit_cl(&d, &a, &f) == expected[mode]);
		if (mode == 0 || mode == 4 || mode == 5)
			CHECK(cl_waits == 2 && signals == 1);
		else CHECK(signals == 0 && cl_bins == 0 &&
		    f.syncs[1]->fence == old_fence);
		fail_fence_alloc = false; cleanup_sync(&f); csd_cleanup(&c, &f, mode == 5);
	}
	a = cl_setup(&c, &d, &f, handles); close_during_submit = &f;
	CHECK(bcmv3d_submit_cl(&d, &a, &f) == 0 && gems == 2);
	csd_cleanup(&c, &f, false);
	/* A timeout must report PCS, not the distinct CORE_IDENT2 register. */
	a = cl_setup(&c, &d, &f, handles); model_case = 1;
	core_regs[0x08/4] = 0xc0078101;
	core_regs[0x130/4] = 0x13579bdf;
	core_regs[0x104/4] = 0x2468ace0;
	core_regs[0x114/4] = 0x12345000;
	core_regs[0x10c/4] = 0x12346000;
	CHECK(bcmv3d_submit_cl(&d, &a, &f) == -EIO);
	CHECK(strstr(diagnostic_log,"pcs=0x13579bdf ") != NULL);
	CHECK(strstr(diagnostic_log,"0xc0078101") == NULL);
	CHECK(strstr(diagnostic_log,"ct1cs=0x2468ace0 ") != NULL);
	CHECK(strstr(diagnostic_log,"ct1ca=0x12345000 ") != NULL);
	CHECK(strstr(diagnostic_log,"ct1ea=0x12346000 ") != NULL);
	CHECK(strstr(diagnostic_log,"mmu=0x60d0c01 read-failed=0 ") != NULL);
	CHECK(strstr(diagnostic_log,"2 buffers retained until reboot") != NULL);
	const char *const failed_field[] = {
	    "irq=0 ", "pcs=0 ", "ct1cs=0 ", "ct1ca=0 ", "ct1ea=0 ", "mmu=0 "};
	for (unsigned n = 0; n < 6; n++) {
		char expected_mask[32];
		unsigned before = reg_calls;
		fail_reg = before + n + 1;
		bcmv3d_cl_diagnose(&c, EIO, 2);
		CHECK(reg_calls == before + 6);
		snprintf(expected_mask,sizeof(expected_mask),"read-failed=%#x ",1U<<n);
		CHECK(strstr(diagnostic_log,expected_mask) != NULL);
		CHECK(strstr(diagnostic_log,failed_field[n]) != NULL);
		CHECK(c.job->poisoned && c.job->nbo == 2);
	}
	fail_reg = 0;
	csd_cleanup(&c, &f, true);
	printf("PASS: %u CL ownership/IRQ checks; %u injected register failures\n",
	    checks, calls);
	return 0;
}
