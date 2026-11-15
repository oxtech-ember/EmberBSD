/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side check of the memory limit of the GEM objects of the 6.18 core
 * (drm618/drm618_shmem.c): objects of 128 MiB are made and their pages taken
 * until the memory is used up.  The pin of an object that does not fit must
 * say ENOMEM and give back the pages it had taken (before the limit it waited
 * for ever and the system killed init); the memory kept free stays above half
 * of the reserve; what was taken comes back when the objects go.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <linux/err.h>
#include <linux/slab.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static const struct drm_driver t_driver = {
	.driver_features = DRIVER_GEM,
	.name = __UNCONST("t-fill"), .desc = __UNCONST("memory fill test"),
	.major = 1,
};

#define	CHUNK		(128u << 20)
#define	MAXOBJ		96

int	drm2_memfill_run(void);

int
drm2_memfill_run(void)
{
	struct drm_device *dev;
	struct drm_gem_shmem_object *o[MAXOBJ];
	unsigned int n = 0, i, avail0, avail_min, avail1;
	size_t total = 0, want;
	int r = 0;

	t_run = t_fail = 0;
	dev = drm_dev_alloc(&t_driver, root_device);
	CHECK(!IS_ERR(dev));
	if (IS_ERR(dev))
		goto out;
	avail0 = uvm_availmem(true);
	avail_min = avail0;
	/* more than all of the memory is asked for */
	want = (size_t)physmem * PAGE_SIZE + (size_t)physmem * PAGE_SIZE / 4;
	while (total < want && n < MAXOBJ) {
		struct drm_gem_shmem_object *s = drm_gem_shmem_create(dev, CHUNK);

		if (IS_ERR(s)) {
			r = PTR_ERR(s);
			break;
		}
		r = drm_gem_shmem_pin(s);
		if (r != 0) {
			drm_gem_object_put(&s->base);
			break;
		}
		o[n++] = s;
		total += CHUNK;
		if ((unsigned int)uvm_availmem(true) < avail_min)
			avail_min = uvm_availmem(true);
	}
	printf("drm2-memfill: %u objects of 128 MiB pinned, then %d; free %u of %u pages at the lowest\n",
	    n, r, avail_min, avail0);
	CHECK(r == -ENOMEM);			/* it ended by refusal, not by waiting or by luck */
	CHECK(n > 0 && total < want);
	CHECK(avail_min >= (u_int)(physmem / 16) / 2);	/* the reserve was kept, about */
	for (i = 0; i < n; i++) {
		drm_gem_shmem_unpin(o[i]);
		drm_gem_object_put(&o[i]->base);
	}
	avail1 = uvm_availmem(true);
	CHECK(avail1 + avail1 / 50 + 2048 >= avail0);	/* the pages came back, to 2% */
	drm_dev_put(dev);
out:
	if (t_fail != 0)
		printf("drm2-memfill: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-memfill: %d checks, 0 failed\n", t_run);
	return t_fail;
}
