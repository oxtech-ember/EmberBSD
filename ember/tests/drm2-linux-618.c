/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the names that the Linux 6.18 DRM core needs from the
 * layer (drm618/include/drm618_prefix.h and what it pulls in; run by
 * sys/modules/drmkms_linux_test).  The file includes the prefix header the
 * way files.drmkms618 does for every 6.18 source.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>

#include <drm618_prefix.h>

#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/ww_mutex.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

/* The same two macro names, the NetBSD forms and the Linux forms, in one file. */

struct tfoo {
	int			v;
	LIST_ENTRY(tfoo)	link;
};
LIST_HEAD(, tfoo) bsd_anon_head = LIST_HEAD_INITIALIZER(bsd_anon_head);
LIST_HEAD(tfoo_head, tfoo);
LIST_HEAD(lnx_head);

struct tnode {
	int			v;
	struct list_head	node;
};

static void
test_macro_names(void)
{
	struct tfoo_head h = LIST_HEAD_INITIALIZER(h);
	struct tfoo a, b;
	LIST_HEAD(lnx_local);
	struct tnode n1, n2, n3;

	/* LIST_HEAD, NetBSD forms. */
	a.v = 1; b.v = 2;
	LIST_INSERT_HEAD(&h, &a, link);
	LIST_INSERT_HEAD(&h, &b, link);
	CHECK(LIST_FIRST(&h) == &b && LIST_NEXT(&b, link) == &a);
	CHECK(LIST_EMPTY(&bsd_anon_head));

	/* LIST_HEAD, Linux form: an empty list_head at file and block scope. */
	CHECK(list_empty(&lnx_head));
	CHECK(list_empty(&lnx_local));

	/* list_last_entry_or_null. */
	CHECK(list_last_entry_or_null(&lnx_local, struct tnode, node) == NULL);
	n1.v = 1; n2.v = 2; n3.v = 3;
	list_add_tail(&n1.node, &lnx_local);
	CHECK(list_last_entry_or_null(&lnx_local, struct tnode, node) == &n1);
	list_add_tail(&n2.node, &lnx_local);
	list_add_tail(&n3.node, &lnx_local);
	CHECK(list_last_entry_or_null(&lnx_local, struct tnode, node) == &n3);
	CHECK(list_first_entry_or_null(&lnx_local, struct tnode, node) == &n1);
	list_del(&n3.node);
	CHECK(list_last_entry_or_null(&lnx_local, struct tnode, node) == &n2);

	/* ALIGN: Linux (x, a), NetBSD (p). */
	CHECK(ALIGN(13, 8) == 16);
	CHECK(ALIGN(16, 8) == 16);
	CHECK(ALIGN(0, 64) == 0);
	CHECK(ALIGN(65ULL, 64) == 128);
	CHECK(ALIGN(1, 4096) == 4096);
	CHECK(ALIGN((uintptr_t)1) == ALIGNBYTES + 1);
	CHECK(ALIGN(ALIGNBYTES + 1) == ALIGNBYTES + 1);
}

static void
test_mutex_names(void)
{
	struct mutex m;
	kmutex_t k;

	/* Linux mutex_init(m), NetBSD mutex_init(k, type, ipl). */
	mutex_init(&m);
	mutex_lock(&m);
	CHECK(mutex_is_locked(&m));
	mutex_unlock(&m);
	CHECK(!mutex_is_locked(&m));
	mutex_destroy(&m);

	mutex_init(&k, MUTEX_DEFAULT, IPL_NONE);
	mutex_enter(&k);
	CHECK(mutex_owned(&k));
	mutex_exit(&k);
	mutex_destroy(&k);
}

static void
test_ww_trylock(void)
{
	DEFINE_WW_CLASS(cls);
	struct ww_mutex m;
	struct ww_acquire_ctx ctx;

	ww_mutex_init(&m, &cls);
	ww_acquire_init(&ctx, &cls);

	/* With a context: counted in the context, held by it. */
	CHECK(ww_mutex_trylock(&m, &ctx) == 1);
	CHECK(ctx.wwx_acquired == 1);
	CHECK(ww_mutex_locking_ctx(&m) == &ctx);
	CHECK(ww_mutex_is_locked(&m));
	CHECK(ww_mutex_trylock(&m, &ctx) == 0);
	CHECK(ww_mutex_trylock(&m, NULL) == 0);
	CHECK(ctx.wwx_acquired == 1);
	ww_mutex_unlock(&m);
	CHECK(ctx.wwx_acquired == 0);
	CHECK(!ww_mutex_is_locked(&m));

	/* Without: both spellings. */
	CHECK(ww_mutex_trylock(&m, NULL) == 1);
	CHECK(ww_mutex_locking_ctx(&m) == NULL);
	CHECK(ww_mutex_trylock(&m) == 0);
	ww_mutex_unlock(&m);
	CHECK(ww_mutex_trylock(&m) == 1);
	ww_mutex_unlock(&m);

	ww_acquire_fini(&ctx);
	ww_mutex_destroy(&m);
}

static void
test_strscpy_pad(void)
{
	char buf[8];
	unsigned i;

	memset(buf, 'x', sizeof(buf));
	CHECK(strscpy_pad(buf, "ab", sizeof(buf)) == 2);
	CHECK(buf[0] == 'a' && buf[1] == 'b');
	for (i = 2; i < sizeof(buf); i++)
		CHECK(buf[i] == '\0');

	memset(buf, 'x', sizeof(buf));
	CHECK(strscpy_pad(buf, "abcdefghij", sizeof(buf)) == -E2BIG);
	CHECK(memcmp(buf, "abcdefg", 8) == 0);	/* cut, terminated */

	memset(buf, 'x', sizeof(buf));
	CHECK(strscpy_pad(buf, "abcdefg", sizeof(buf)) == 7);	/* exact fit */
	CHECK(memcmp(buf, "abcdefg", 8) == 0);

	memset(buf, 'x', sizeof(buf));
	CHECK(strscpy_pad(buf, "", sizeof(buf)) == 0);
	for (i = 0; i < sizeof(buf); i++)
		CHECK(buf[i] == '\0');

	CHECK(strscpy_pad(buf, "a", 0) == -E2BIG);
}

static void
test_dev_driver_string(void)
{
	const char *n;

	CHECK(strcmp(dev_driver_string(NULL), "") == 0);
	/* any attached device: the root device */
	CHECK(root_device != NULL);
	if (root_device != NULL) {
		n = dev_driver_string(root_device);
		CHECK(n != NULL && n[0] != '\0');
		CHECK(strcmp(n, device_cfdriver(root_device)->cd_name) == 0);
		CHECK(strncmp(device_xname(root_device), n, strlen(n)) == 0);
	}
}

static int
fall(int v)
{
	int r = 0;

	switch (v) {
	case 1:
		r += 1;
		fallthrough;
	case 2:
		r += 10;
		break;
	default:
		r = -1;
	}
	return r;
}

static void
test_helpers(void)
{
	char *p, *q;
	const char *s = "a=b,c";
	unsigned char z[16];
	void *v;

	/* krealloc_array: grows, keeps the data, refuses an overflow. */
	p = krealloc_array(NULL, 4, sizeof(int), GFP_KERNEL);
	CHECK(p != NULL);
	if (p != NULL) {
		memset(p, 7, 4 * sizeof(int));
		q = krealloc_array(p, 64, sizeof(int), GFP_KERNEL);
		CHECK(q != NULL && q[0] == 7 && q[4 * sizeof(int) - 1] == 7);
		p = q != NULL ? q : p;
		CHECK(krealloc_array(p, SIZE_MAX / 2, 4, GFP_KERNEL) == NULL);
		q = kvrealloc(p, 8, GFP_KERNEL);
		CHECK(q != NULL && q[0] == 7);
		kfree(q != NULL ? q : p);
	}

	/* mem_is_zero */
	memset(z, 0, sizeof(z));
	CHECK(mem_is_zero(z, sizeof(z)));
	CHECK(mem_is_zero(z, 0));
	z[15] = 1;
	CHECK(!mem_is_zero(z, sizeof(z)));
	CHECK(mem_is_zero(z, 15));
	z[0] = 1;
	CHECK(!mem_is_zero(z, 1));

	/* strnchr: bounded by the count and by the NUL */
	CHECK(strnchr(s, 5, '=') == s + 1);
	CHECK(strnchr(s, 5, 'c') == s + 4);
	CHECK(strnchr(s, 4, 'c') == NULL);
	CHECK(strnchr(s, 100, 'x') == NULL);
	CHECK(strnchr(s, 0, 'a') == NULL);
	CHECK(strnchr("", 4, '\0') == NULL);	/* stops at the NUL itself */

	/* range_overflows */
	CHECK(!range_overflows(0, 10, 10));
	CHECK(!range_overflows(4u, 6u, 10u));
	CHECK(range_overflows(4u, 7u, 10u));
	CHECK(range_overflows(10u, 0u, 10u));	/* start at the end */
	CHECK(range_overflows(11u, 0u, 10u));
	CHECK(range_overflows(1ULL, ~0ULL, 10ULL));	/* no wrap-around */
	CHECK(!range_overflows_t(unsigned int, 0, 0, 1));

	/* timer_container_of */
	{
		struct tt { int a; struct timer_list t; } x;
		struct tt *y;

		y = timer_container_of(y, &x.t, t);
		CHECK(y == &x);
	}

	/* fallthrough */
	CHECK(fall(1) == 11 && fall(2) == 10 && fall(3) == -1);

	/* memdup_user_nul: a kernel address is not a user address */
	v = memdup_user_nul(s, 3);
	CHECK(IS_ERR(v) && PTR_ERR(v) == -EFAULT);
	if (!IS_ERR(v))
		kfree(v);
}

int	drm2_linux_618_run(void);

int
drm2_linux_618_run(void)
{

	t_run = t_fail = 0;
	test_macro_names();
	test_mutex_names();
	test_ww_trylock();
	test_strscpy_pad();
	test_dev_driver_string();
	test_helpers();
	printf("drm2-linux-618: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
