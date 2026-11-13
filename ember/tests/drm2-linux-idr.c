/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the idr part of the drm2 Linux layer (run by
 * sys/modules/drmkms_linux_test).  Linux semantics: an entry replaced
 * by NULL stays allocated (idr_find returns NULL, the id is not reused)
 * but idr_for_each and idr_get_next skip it.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/idr.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static int
t_alloc(struct idr *idr, void *p)
{
	int id;

	idr_preload(GFP_KERNEL);
	id = idr_alloc(idr, p, 1, 0, GFP_NOWAIT);
	idr_preload_end();
	return id;
}

struct visit {
	int	ids[8];
	void	*ptrs[8];
	unsigned n;
	unsigned stop_after;
};

static int
visit_cb(int id, void *p, void *arg)
{
	struct visit *v = arg;

	if (v->n < 8) {
		v->ids[v->n] = id;
		v->ptrs[v->n] = p;
	}
	v->n++;
	if (v->stop_after != 0 && v->n == v->stop_after)
		return 42;
	return 0;
}

static void
test_idr_for_each_null(void)
{
	struct idr idr;
	struct visit v;
	static int o[4];
	int id0, id1, id2, id3, error, id;
	void *p;

	idr_init(&idr);
	id0 = t_alloc(&idr, &o[0]);
	id1 = t_alloc(&idr, &o[1]);
	id2 = t_alloc(&idr, &o[2]);
	id3 = t_alloc(&idr, &o[3]);
	CHECK(id0 == 1 && id1 == 2 && id2 == 3 && id3 == 4);

	/* All entries are visited in id order. */
	memset(&v, 0, sizeof(v));
	error = idr_for_each(&idr, visit_cb, &v);
	CHECK(error == 0 && v.n == 4);
	CHECK(v.ids[0] == 1 && v.ids[1] == 2 && v.ids[2] == 3 && v.ids[3] == 4);

	/* Entries replaced by NULL are skipped, the others are not. */
	CHECK(idr_replace(&idr, NULL, id1) == &o[1]);
	CHECK(idr_replace(&idr, NULL, id3) == &o[3]);
	CHECK(idr_find(&idr, id1) == NULL);
	memset(&v, 0, sizeof(v));
	error = idr_for_each(&idr, visit_cb, &v);
	CHECK(error == 0 && v.n == 2);
	CHECK(v.ids[0] == id0 && v.ptrs[0] == &o[0]);
	CHECK(v.ids[1] == id2 && v.ptrs[1] == &o[2]);

	/* The callback's nonzero result stops the walk and is returned. */
	memset(&v, 0, sizeof(v));
	v.stop_after = 1;
	error = idr_for_each(&idr, visit_cb, &v);
	CHECK(error == 42 && v.n == 1 && v.ids[0] == id0);

	/* idr_get_next steps over NULL entries too (idr_for_each_entry). */
	id = 0;
	p = idr_get_next(&idr, &id);
	CHECK(p == &o[0] && id == id0);
	id++;
	p = idr_get_next(&idr, &id);
	CHECK(p == &o[2] && id == id2);
	id++;
	p = idr_get_next(&idr, &id);
	CHECK(p == NULL);
	v.n = 0;
	idr_for_each_entry(&idr, p, id)
		v.n++;
	CHECK(v.n == 2);

	/* A NULL slot still holds its id: it is not reused. */
	CHECK(t_alloc(&idr, &o[1]) == 5);

	/* Nothing but NULL entries: no visits. */
	idr_replace(&idr, NULL, id0);
	idr_replace(&idr, NULL, id2);
	idr_replace(&idr, NULL, 5);
	memset(&v, 0, sizeof(v));
	error = idr_for_each(&idr, visit_cb, &v);
	CHECK(error == 0 && v.n == 0);
	id = 0;
	CHECK(idr_get_next(&idr, &id) == NULL);

	idr_destroy(&idr);
}

static void
test_idr_remove_missing(void)
{
	struct idr idr;
	static int o[2];
	int id0, id1;

	idr_init(&idr);
	id0 = t_alloc(&idr, &o[0]);
	id1 = t_alloc(&idr, &o[1]);
	CHECK(id0 == 1 && id1 == 2);

	/* An id that was never allocated: nothing found, nothing freed. */
	CHECK(idr_remove(&idr, 99) == NULL);
	CHECK(idr_find(&idr, id0) == &o[0] && idr_find(&idr, id1) == &o[1]);

	/* Removal returns the entry once; the second time the id is gone. */
	CHECK(idr_remove(&idr, id0) == &o[0]);
	CHECK(idr_remove(&idr, id0) == NULL);
	CHECK(idr_find(&idr, id0) == NULL && idr_find(&idr, id1) == &o[1]);

	/* An entry replaced by NULL still holds its id and is removable. */
	CHECK(idr_replace(&idr, NULL, id1) == &o[1]);
	CHECK(idr_remove(&idr, id1) == NULL);
	CHECK(idr_remove(&idr, id1) == NULL);

	idr_destroy(&idr);
}

int	drm2_linux_idr_run(void);

int
drm2_linux_idr_run(void)
{

	t_run = t_fail = 0;
	test_idr_for_each_null();
	test_idr_remove_missing();
	printf("drm2-linux-idr: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
