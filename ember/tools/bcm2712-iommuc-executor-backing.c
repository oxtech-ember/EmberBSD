/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Reuse complete existing service doubles and actual backing/helper TUs. */
#define main backing_reference_main
#include "bcmv3d-mmu4-backing-contract.c"
#undef main

void *icac_backing_create(struct bcm2712_mmu4_config *, bool);
int icac_backing_prepare(void *, const struct bcm2712_mmu4_config *);
void icac_backing_retained(void *);

void *
icac_backing_create(struct bcm2712_mmu4_config *out, bool fail_prepare)
{
	struct bcmv3d_mmu4_backing *b;

	reset();
	b = create();
	current = b;
	CHECK(bcmv3d_mmu4_backing_get_config(b, out) == 0);
	CHECK(bcmv3d_mmu4_backing_seal(b) == 0);
	if (fail_prepare)
		fail_pmap = pmap_calls + 1;
	return b;
}

int
icac_backing_prepare(void *arg, const struct bcm2712_mmu4_config *cfg)
{
	unsigned before = service_calls;
	int error;

	CHECK(arg == current && !preparing);
	preparing = true;
	error = bcmv3d_mmu4_backing_prepare(arg, cfg);
	preparing = false;
	CHECK(service_calls == before); /* No allocation in the locked path. */
	CHECK(syncs == (error == 0 ? 5U : 0U));
	if (error == 0)
		image_check();
	return error;
}

void
icac_backing_retained(void *arg)
{
	struct bcmv3d_mmu4_backing_status status;
	unsigned before = nfrees;

	CHECK(bcmv3d_mmu4_backing_status(arg, &status) == 0 && status.sealed);
	CHECK(bcmv3d_mmu4_backing_destroy_unexposed(arg) == EBUSY);
	CHECK(nfrees == before && bo != NULL && maps == 5);
}
