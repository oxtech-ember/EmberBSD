/*	EmberBSD: OpenBSD kernel API shims for the iwx(4) port.	*/

/*
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _IF_IWX_COMPAT_H_
#define _IF_IWX_COMPAT_H_

#include <sys/atomic.h>
#include <sys/callout.h>
#include <sys/kmem.h>
#include <sys/rwlock.h>
#include <sys/workqueue.h>
#include <dev/firmload.h>

#define nitems(_a)		__arraycount(_a)
#define letoh16(_x)		le16toh(_x)
#define letoh32(_x)		le32toh(_x)
#define letoh64(_x)		le64toh(_x)

#define USEC_TO_NSEC(_u)	((uint64_t)(_u) * 1000ULL)
#define MSEC_TO_NSEC(_m)	((uint64_t)(_m) * 1000000ULL)
#define SEC_TO_NSEC(_s)		((uint64_t)(_s) * 1000000000ULL)
#define INFSLP			((uint64_t)-1)

static inline int
iwx_nsec2ticks(uint64_t nsec)
{
	uint64_t t;

	if (nsec == INFSLP)
		return 0;
	t = nsec / (1000000000ULL / hz);
	return t == 0 ? 1 : (t > INT_MAX ? INT_MAX : (int)t);
}

#define tsleep_nsec(_id, _pri, _msg, _ns) \
	tsleep((_id), (_pri), (_msg), iwx_nsec2ticks(_ns))

/* malloc(9)/free(9) with the OpenBSD size argument dropped. */
#define iwx_free(_p, _t, _s)	free((_p), (_t))
#define mallocarray(_n, _s, _t, _f)	malloc((size_t)(_n) * (_s), (_t), (_f))

/* OpenBSD task(9) on top of NetBSD workqueue(9). */
struct task {
	struct work	t_work;
	void		(*t_func)(void *);
	void		*t_arg;
	volatile unsigned int t_pending;
};
struct taskq {
	struct workqueue *tq_wq;
};

static void
iwx_task_worker(struct work *wk, void *arg __unused)
{
	struct task *t = container_of(wk, struct task, t_work);

	atomic_swap_uint(&t->t_pending, 0);
	t->t_func(t->t_arg);
}

static inline struct taskq *
taskq_create(const char *name, unsigned int nthreads __unused, int ipl,
    unsigned int flags __unused)
{
	struct taskq *tq = kmem_zalloc(sizeof(*tq), KM_SLEEP);

	if (workqueue_create(&tq->tq_wq, name, iwx_task_worker, NULL,
	    PRI_NONE, ipl, WQ_MPSAFE) != 0) {
		kmem_free(tq, sizeof(*tq));
		return NULL;
	}
	return tq;
}

static inline void
taskq_destroy(struct taskq *tq)
{
	workqueue_destroy(tq->tq_wq);
	kmem_free(tq, sizeof(*tq));
}

static inline void
task_set(struct task *t, void (*fn)(void *), void *arg)
{
	t->t_func = fn;
	t->t_arg = arg;
	t->t_pending = 0;
}

static inline int
task_add(struct taskq *tq, struct task *t)
{
	if (atomic_swap_uint(&t->t_pending, 1) != 0)
		return 0;
	workqueue_enqueue(tq->tq_wq, &t->t_work, NULL);
	return 1;
}

/*
 * A queued work item cannot be removed from a NetBSD workqueue.  Callers
 * of task_del() in iwx hold a reference which the task drops itself when
 * it runs, so report "not removed" and let the task run to completion.
 */
static inline int
task_del(struct taskq *tq __unused, struct task *t __unused)
{
	return 0;
}

#define task_pending(_t)	((_t)->t_pending != 0)

/* OpenBSD timeout(9) on top of NetBSD callout(9). */
struct timeout {
	callout_t	to_ch;
};

static inline void
timeout_set(struct timeout *to, void (*fn)(void *), void *arg)
{
	callout_init(&to->to_ch, 0);
	callout_setfunc(&to->to_ch, fn, arg);
}

static inline void
timeout_add_usec(struct timeout *to, uint64_t usec)
{
	int t = (int)((usec * hz) / 1000000ULL);

	callout_schedule(&to->to_ch, t > 0 ? t : 1);
}

#define timeout_add_msec(_to, _ms)	timeout_add_usec((_to), (uint64_t)(_ms) * 1000)
#define timeout_add_sec(_to, _s)	timeout_add_usec((_to), (uint64_t)(_s) * 1000000)
#define timeout_del(_to)		callout_stop(&(_to)->to_ch)
#define timeout_pending(_to)		callout_pending(&(_to)->to_ch)

/* OpenBSD rwlock(9) names. */
#define rw_enter_write(_l)	rw_enter((_l), RW_WRITER)
#define rw_assert_wrlock(_l)	KASSERT(rw_write_held(_l))
#define iwx_rw_init(_l, _n)	rw_init(_l)

/* OpenBSD refcnt(9), used to wait for tasks on stop. */
struct refcnt {
	volatile unsigned int r_refs;
};

static inline void
refcnt_init(struct refcnt *r)
{
	r->r_refs = 1;
}

static inline void
refcnt_take(struct refcnt *r)
{
	atomic_inc_uint(&r->r_refs);
}

static inline int
refcnt_rele(struct refcnt *r)
{
	return atomic_dec_uint_nv(&r->r_refs) == 0;
}

static inline void
refcnt_rele_wake(struct refcnt *r)
{
	if (refcnt_rele(r))
		wakeup(r);
}

static inline void
refcnt_finalize(struct refcnt *r, const char *wmesg)
{
	if (!refcnt_rele(r)) {
		while (r->r_refs != 0)
			tsleep(r, 0, wmesg, hz / 10);
	}
}

/* OpenBSD loadfirmware(9) on top of NetBSD firmload(9). */
static inline int
loadfirmware(const char *name, u_char **bufp, size_t *buflen)
{
	firmware_handle_t fwh;
	size_t size;
	u_char *buf;
	int err;

	err = firmware_open("if_iwx", name, &fwh);
	if (err)
		return err;
	size = firmware_get_size(fwh);
	if (size == 0) {
		firmware_close(fwh);
		return EINVAL;
	}
	buf = firmware_malloc(size);
	if (buf == NULL) {
		firmware_close(fwh);
		return ENOMEM;
	}
	err = firmware_read(fwh, 0, buf, size);
	firmware_close(fwh);
	if (err) {
		firmware_free(buf, size);
		return err;
	}
	*bufp = buf;
	*buflen = size;
	return 0;
}

#define iwx_firmware_free(_b, _l)	firmware_free((_b), (_l))

#endif /* _IF_IWX_COMPAT_H_ */
