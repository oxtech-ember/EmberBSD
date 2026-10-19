/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual backing/helper/encoder; reuse the existing native service fixture. */
#include "bcm2712-iommuc-executor-backing.c"

int native_backing_create(uint32_t, void **, unsigned int);
int native_backing_prepare(void *, const struct bcm2712_mmu4_config *, bool);
void native_backing_check(void *);

int
native_backing_create(uint32_t debug, void **out, unsigned int failure)
{
	struct bcmv3d_mmu4_backing *b = NULL;
	int error;

	reset();
	fail_service = failure;
	error = bcmv3d_mmu4_backing_create(&device, debug, &b);
	if (error == 0) {
		current = b;
		*out = b;
	}
	return error;
}

int
native_backing_prepare(void *arg, const struct bcm2712_mmu4_config *cfg,
    bool failure)
{
	unsigned int before = service_calls;
	int error;

	CHECK(arg == current && !preparing);
	if (failure)
		fail_pmap = pmap_calls + 1;
	preparing = true;
	error = bcmv3d_mmu4_backing_prepare(arg, cfg);
	preparing = false;
	CHECK(service_calls == before);
	CHECK(syncs == (error == 0 ? 5U : 0U));
	if (error == 0)
		image_check();
	return error;
}

void
native_backing_check(void *arg)
{
	struct bcmv3d_mmu4_backing_status status;
	unsigned int before = nfrees;

	CHECK(bcmv3d_mmu4_backing_status(arg, &status) == 0 && status.sealed);
	CHECK(bcmv3d_mmu4_backing_destroy_unexposed(arg) == EBUSY);
	CHECK(nfrees == before && bo != NULL && maps == 5);
}
