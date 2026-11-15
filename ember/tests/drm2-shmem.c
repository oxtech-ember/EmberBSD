/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the shmem GEM helper of the 6.18 core
 * (drm618/drm618_gem_shmem_helper.c), run inside drmkms618_test with the real
 * core loaded: creation and its limits, the pages and their holders, the
 * kernel mapping, the tables, the user mapping flags, the release of the
 * object.  A device that is allocated and never registered carries the objects.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/shmem_fs.h>
#include <linux/scatterlist.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm618_page.h>

static int t_run, t_fail, t_freed;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

struct t_obj {
	struct drm_gem_shmem_object	shmem;	/* first */
	unsigned int			magic;
};

static void
t_free(struct drm_gem_object *obj)
{

	t_freed++;
	drm_gem_shmem_object_free(obj);
}

static const struct drm_gem_object_funcs t_funcs = {
	.free		= t_free,
	.pin		= drm_gem_shmem_object_pin,
	.unpin		= drm_gem_shmem_object_unpin,
	.get_sg_table	= drm_gem_shmem_object_get_sg_table,
	.vmap		= drm_gem_shmem_object_vmap,
	.vunmap		= drm_gem_shmem_object_vunmap,
	.mmap		= drm_gem_shmem_object_mmap,
	.vm_ops		= &drm_gem_shmem_vm_ops,
};

static struct drm_gem_object *
t_create_object(struct drm_device *dev __unused, size_t size __unused)
{
	struct t_obj *o = kzalloc(sizeof(*o), GFP_KERNEL);

	if (o == NULL)
		return ERR_PTR(-ENOMEM);
	o->magic = 0x1234;
	o->shmem.base.funcs = &t_funcs;
	return &o->shmem.base;
}

static const struct drm_driver t_driver_cb = {
	.driver_features = DRIVER_GEM,
	.gem_create_object = t_create_object,
	.name = __UNCONST("t-shmem"), .desc = __UNCONST("shmem helper test"), .major = 1,
};

static const struct drm_driver t_driver_plain = {
	.driver_features = DRIVER_GEM,
	.name = __UNCONST("t-shmem2"), .desc = __UNCONST("shmem helper test"), .major = 1,
};

/* drm618_gem_shmem_helper.c, drm618_shmem.c (drmkms618) */
void	drm618_gem_set_limit(device_t, size_t);
extern u_int drm618_shmem_reserve;

int	drm2_shmem_run(void);

int
drm2_shmem_run(void)
{
	struct drm_device *dev, *dev2;
	struct drm_gem_shmem_object *s, *s2;
	struct sg_table *sgt, *sgt2, *tbl;
	struct scatterlist *sg;
	struct iosys_map map, map2;
	struct vm_area_struct vma;
	unsigned int i, j;
	size_t total;
	char *va;
	bool same;

	t_run = t_fail = t_freed = 0;
	dev = drm_dev_alloc(&t_driver_cb, root_device);
	dev2 = drm_dev_alloc(&t_driver_plain, root_device);
	CHECK(!IS_ERR(dev) && !IS_ERR(dev2));
	if (IS_ERR(dev) || IS_ERR(dev2))
		goto out;

	/* creation: sizes */
	CHECK(IS_ERR(drm_gem_shmem_create(dev, 0)));
	CHECK(PTR_ERR(drm_gem_shmem_create(dev, 0)) == -EINVAL);
	CHECK(IS_ERR(drm_gem_shmem_create(dev, SIZE_MAX - 100)));
	s = drm_gem_shmem_create(dev, 1);
	CHECK(!IS_ERR(s));
	CHECK(s->base.size == PAGE_SIZE);
	CHECK(((struct t_obj *)s)->magic == 0x1234);	/* the driver's object */
	CHECK(s->base.funcs == &t_funcs);
	CHECK(s->pages == NULL && s->pages_use_count == 0 && s->vaddr == NULL);
	drm_gem_object_put(&s->base);
	CHECK(t_freed == 1);

	/* the pages: holders */
	s = drm_gem_shmem_create(dev, 5 * PAGE_SIZE - 7);
	CHECK(!IS_ERR(s) && s->base.size == 5 * PAGE_SIZE);
	CHECK(drm_gem_shmem_pin(s) == 0);
	CHECK(s->pages != NULL && s->pages_use_count == 1);
	CHECK(drm_gem_shmem_pin(s) == 0 && s->pages_use_count == 2);
	drm_gem_shmem_unpin(s);
	CHECK(s->pages != NULL && s->pages_use_count == 1);
	/* a table of an object nobody holds is an error, not a guess */
	drm_gem_shmem_unpin(s);
	CHECK(s->pages == NULL && s->pages_use_count == 0);
	CHECK(IS_ERR(drm_gem_shmem_get_sg_table(s)));
	CHECK(PTR_ERR(drm_gem_shmem_get_sg_table(s)) == -EINVAL);

	/* the kernel mapping: the pages seen through it are the object's */
	CHECK(drm_gem_shmem_vmap(s, &map) == 0);
	CHECK(!map.is_iomem && map.vaddr != NULL);
	va = map.vaddr;
	CHECK(s->vaddr == va && s->vmap_use_count == 1 && s->pages_use_count == 1);
	for (i = 0; i < 5 * PAGE_SIZE; i++)
		va[i] = (char)(i * 7 + (i >> 12));
	same = true;
	for (i = 0; i < 5; i++) {
		const char *pa = page_address(s->pages[i]);

		for (j = 0; j < PAGE_SIZE; j++)
			if (pa[j] != (char)((i * PAGE_SIZE + j) * 7 +
			    ((i * PAGE_SIZE + j) >> 12)))
				same = false;
	}
	CHECK(same);
	CHECK(drm_gem_shmem_vmap(s, &map2) == 0 && map2.vaddr == va);
	CHECK(s->vmap_use_count == 2 && s->pages_use_count == 1);
	drm_gem_shmem_vunmap(s, &map2);
	CHECK(s->vmap_use_count == 1 && s->vaddr == va && map2.vaddr == NULL);
	drm_gem_shmem_vunmap(s, &map);
	CHECK(s->vmap_use_count == 0 && s->vaddr == NULL && s->pages_use_count == 0);

	/* the table of the object: made once, kept; the bus side is the physical one */
	sgt = drm_gem_shmem_get_pages_sgt(s);
	CHECK(!IS_ERR(sgt));
	CHECK(s->pages_use_count == 1 && s->pages != NULL);
	sgt2 = drm_gem_shmem_get_pages_sgt(s);
	CHECK(sgt2 == sgt && s->pages_use_count == 1);
	total = 0;
	same = true;
	for_each_sgtable_sg(sgt, sg, i) {
		total += sg->length;
		if (sg_dma_address(sg) != sg_phys(sg) ||
		    sg_dma_len(sg) != sg->length)
			same = false;
	}
	CHECK(total == 5 * PAGE_SIZE && same && sgt->nents == sgt->orig_nents);
	CHECK(sgt->orig_nents >= 1 && sgt->orig_nents <= 5);
	/* the content is the one written through the mapping */
	va = (char *)page_address(sg_page(sgt->sgl)) + sgt->sgl->offset;
	CHECK(va[1] == (char)7 && va[100] == (char)(700));
	/* a new table of a pinned object */
	CHECK(drm_gem_shmem_pin(s) == 0);
	tbl = drm_gem_shmem_get_sg_table(s);
	CHECK(!IS_ERR(tbl) && tbl != sgt && tbl->orig_nents == sgt->orig_nents);
	if (!IS_ERR(tbl)) {
		sg_free_table(tbl);
		kfree(tbl);
	}
	drm_gem_shmem_unpin(s);

	/* the user mapping: the flags and the use it takes, given back by the close */
	memset(&vma, 0, sizeof(vma));
	CHECK(drm_gem_shmem_mmap(s, &vma) == 0);
	CHECK((vma.vm_flags & VM_DONTEXPAND) && (vma.vm_flags & VM_PFNMAP));
	CHECK(s->pages_use_count == 2);
	drm_gem_shmem_unpin(s);			/* the mapping goes */
	CHECK(s->pages_use_count == 1);

	drm_gem_object_put(&s->base);		/* frees the table and the pages */
	CHECK(t_freed == 2);

	/* an object without gem_create_object: the default functions */
	s2 = drm_gem_shmem_create(dev2, 2 * PAGE_SIZE);
	CHECK(!IS_ERR(s2) && s2->base.funcs != NULL);
	CHECK(s2->base.funcs->free != NULL && s2->base.funcs->vm_ops != NULL);
	CHECK(drm_gem_shmem_vmap(s2, &map) == 0 && map.vaddr != NULL);
	((char *)map.vaddr)[PAGE_SIZE + 5] = 42;
	CHECK(((const char *)page_address(s2->pages[1]))[5] == 42);
	drm_gem_object_put(&s2->base);		/* the mapping and the pages go with it */

	/* limits: an object of the size of all memory, of 2^40 bytes, above the limit of the device */
	{
		struct file *f = shmem_file_setup("t", (loff_t)physmem * PAGE_SIZE, 0);

		CHECK(IS_ERR(f) && PTR_ERR(f) == -ENOMEM);
		if (!IS_ERR(f))
			fput(f);
	}
	CHECK(IS_ERR(drm_gem_shmem_create(dev, (size_t)1 << 40)));
	drm618_gem_set_limit(root_device, 3 << 20);
	CHECK(PTR_ERR(drm_gem_shmem_create(dev, 4 << 20)) == -EFBIG);
	s = drm_gem_shmem_create(dev, 2 << 20);
	CHECK(!IS_ERR(s));
	if (!IS_ERR(s))
		drm_gem_object_put(&s->base);
	drm618_gem_set_limit(root_device, 0);
	s = drm_gem_shmem_create(dev, 4 << 20);
	CHECK(!IS_ERR(s));
	if (!IS_ERR(s))
		drm_gem_object_put(&s->base);
	/* memory: pages are not taken from the last of it, the pin says no instead of waiting */
	s = drm_gem_shmem_create(dev, 64 * PAGE_SIZE);
	CHECK(!IS_ERR(s));
	if (!IS_ERR(s)) {
		drm618_shmem_reserve = physmem;		/* nothing counts as free */
		CHECK(drm_gem_shmem_pin(s) == -ENOMEM);
		CHECK(s->pages == NULL && s->pages_use_count == 0);
		drm618_shmem_reserve = 0;
		CHECK(drm_gem_shmem_pin(s) == 0 && s->pages != NULL);
		drm_gem_shmem_unpin(s);
		drm_gem_object_put(&s->base);
	}
out:
	if (!IS_ERR_OR_NULL(dev))
		drm_dev_put(dev);
	if (!IS_ERR_OR_NULL(dev2))
		drm_dev_put(dev2);
	if (t_fail != 0)
		printf("drm2-shmem: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-shmem: %d checks, 0 failed\n", t_run);
	return t_fail;
}
