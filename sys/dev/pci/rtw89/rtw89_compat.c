/*	$NetBSD$	*/

/*-
 * Copyright (c) 2026 The EmberBSD Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Implementation of the Linux kernel API subset declared in rtw89_compat.h.
 */

#include "rtw89_compat.h"

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/intr.h>
#include <sys/kthread.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/time.h>

#include <uvm/uvm_extern.h>

#include <dev/firmload.h>

#include "core.h"
#include "debug.h"
#include "rtw89_nb.h"

/* ------------------------------------------------------------------ */
/* Context helpers                                                     */
/* ------------------------------------------------------------------ */

static bool
rtw89_can_sleep(void)
{

	if (cold || cpu_intr_p() || cpu_softintr_p())
		return false;
	if (curcpu()->ci_mtx_count != 0)	/* holding a spin mutex */
		return false;
	return true;
}

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

void *
rtw89_kmalloc(size_t size, gfp_t gfp)
{
	int flags;

	if (size == 0)
		size = 1;
	flags = (gfp & __GFP_ZERO) ? M_ZERO : 0;
	if ((gfp & GFP_ATOMIC) || !rtw89_can_sleep())
		flags |= M_NOWAIT;
	else
		flags |= M_WAITOK;
	return malloc(size, M_DEVBUF, flags);
}

void
rtw89_kfree(const void *p)
{

	if (p != NULL)
		free(__UNCONST(p), M_DEVBUF);
}

/* ------------------------------------------------------------------ */
/* printf                                                              */
/* ------------------------------------------------------------------ */

unsigned int rtw89_debug_mask = 0;

static const char rtw89_hexdigits[] = "0123456789abcdef";

/*
 * vsnprintf() understanding the Linux %pM, %*ph[N] and %pe extensions;
 * other conversions are passed to the kernel snprintf() one by one.
 */
static void
rtw89_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	char spec[32];
	size_t o = 0;
	const char *p;

#define PUTC(c) do { if (o + 1 < size) buf[o++] = (c); } while (0)
	for (p = fmt; *p != '\0'; p++) {
		const char *start;
		size_t sl;
		int width = -1, lflag = 0, hflag = 0, zflag = 0;

		if (*p != '%') {
			PUTC(*p);
			continue;
		}
		start = p++;
		if (*p == '%') {
			PUTC('%');
			continue;
		}
		while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' ||
		    *p == '0')
			p++;
		if (*p == '*') {
			width = va_arg(ap, int);
			p++;
		} else {
			while (*p >= '0' && *p <= '9')
				p++;
		}
		if (*p == '.') {
			p++;
			if (*p == '*') {
				(void)va_arg(ap, int);
				p++;
			} else {
				while (*p >= '0' && *p <= '9')
					p++;
			}
		}
		for (;; p++) {
			if (*p == 'l')
				lflag++;
			else if (*p == 'h')
				hflag++;
			else if (*p == 'z' || *p == 't' || *p == 'j')
				zflag++;
			else
				break;
		}
		if (*p == 'p') {
			const u8 *ptr = va_arg(ap, const u8 *);
			int i, n;

			if (p[1] == 'M' || p[1] == 'm') {
				p++;
				for (i = 0; i < ETH_ALEN; i++) {
					if (i && *p == 'M')
						PUTC(':');
					PUTC(rtw89_hexdigits[ptr[i] >> 4]);
					PUTC(rtw89_hexdigits[ptr[i] & 0xf]);
				}
				continue;
			}
			if (p[1] == 'h') {
				bool sep = true;

				p++;
				if (p[1] == 'N' || p[1] == 'C' || p[1] == 'D') {
					sep = p[1] != 'N';
					p++;
				}
				n = width < 0 ? 1 : MIN(width, 64);
				for (i = 0; i < n; i++) {
					if (i && sep)
						PUTC(' ');
					PUTC(rtw89_hexdigits[ptr[i] >> 4]);
					PUTC(rtw89_hexdigits[ptr[i] & 0xf]);
				}
				continue;
			}
			if (p[1] == 'e') {
				p++;
				o += snprintf(buf + o, o < size ? size - o : 0,
				    "%ld", (long)(intptr_t)ptr);
				if (o >= size)
					o = size - 1;
				continue;
			}
			while (p[1] == 'S' || p[1] == 's' || p[1] == 'B' ||
			    p[1] == 'x' || p[1] == 'K')
				p++;
			o += snprintf(buf + o, o < size ? size - o : 0, "%p",
			    ptr);
			if (o >= size)
				o = size - 1;
			continue;
		}
		sl = p - start + 1;
		if (sl >= sizeof(spec))
			break;
		memcpy(spec, start, sl);
		spec[sl] = '\0';
#define EMIT(v) do {							\
	if (width >= 0) {						\
		/* replace '*' by the literal width */			\
		char spec2[48], *s = spec2, *q;				\
		for (q = spec; *q && *q != '*'; q++)			\
			*s++ = *q;					\
		s += snprintf(s, 12, "%d", width);			\
		for (q++; *q; q++)					\
			*s++ = *q;					\
		*s = '\0';						\
		o += snprintf(buf + o, o < size ? size - o : 0, spec2, v); \
	} else								\
		o += snprintf(buf + o, o < size ? size - o : 0, spec, v); \
	if (o >= size)							\
		o = size ? size - 1 : 0;				\
} while (0)
		switch (*p) {
		case 'd': case 'i':
			if (lflag >= 2)
				EMIT(va_arg(ap, long long));
			else if (lflag || zflag)
				EMIT(va_arg(ap, long));
			else
				EMIT(va_arg(ap, int));
			break;
		case 'u': case 'x': case 'X': case 'o':
			if (lflag >= 2)
				EMIT(va_arg(ap, unsigned long long));
			else if (lflag || zflag)
				EMIT(va_arg(ap, unsigned long));
			else
				EMIT(va_arg(ap, unsigned int));
			break;
		case 'c':
			EMIT(va_arg(ap, int));
			break;
		case 's':
			EMIT(va_arg(ap, const char *));
			break;
		default:
			/* unknown conversion: copy verbatim */
			for (; start <= p; start++)
				PUTC(*start);
			break;
		}
#undef EMIT
		(void)hflag;
	}
	if (size)
		buf[o < size ? o : size - 1] = '\0';
#undef PUTC
}

void
rtw89_dev_printf(const struct device *dev, const char *prefix,
    const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	rtw89_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (dev != NULL)
		printf("%s: %s%s", device_xname(__UNCONST(dev)), prefix, buf);
	else
		printf("rtw89: %s%s", prefix, buf);
}

void
rtw89_printk(const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	rtw89_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	printf("%s", buf);
}

void
__rtw89_debug(struct rtw89_dev *rtwdev, enum rtw89_debug_mask mask,
    const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	if ((rtw89_debug_mask & mask) == 0)
		return;
	va_start(ap, fmt);
	rtw89_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	printf("%s: %s", device_xname(rtwdev->dev), buf);
}

void
rtw89_hexdump(const char *prefix, const void *buf, size_t len)
{
	const u8 *b = buf;
	size_t i;

	for (i = 0; i < len; i++) {
		if ((i % 16) == 0)
			printf("%s%s%04zx:", i ? "\n" : "", prefix, i);
		printf(" %02x", b[i]);
	}
	printf("\n");
}

const char *
rtw89_dev_name(const struct device *dev)
{

	return device_xname(__UNCONST(dev));
}

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */

ktime_t
rtw89_ktime_get(void)
{
	struct timespec ts;

	nanouptime(&ts);
	return (ktime_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void
rtw89_usleep(unsigned long us)
{

	if (us < 2000 || !rtw89_can_sleep()) {
		DELAY(us);
		return;
	}
	kpause("rtw89us", false, MAX(1, mstohz(howmany(us, 1000))), NULL);
}

void
rtw89_msleep(unsigned int ms)
{

	rtw89_usleep((unsigned long)ms * 1000);
}

/* ------------------------------------------------------------------ */
/* Work queues                                                         */
/* ------------------------------------------------------------------ */

struct workqueue_struct {
	kmutex_t			 wq_lock;
	kcondvar_t			 wq_cv;
	kcondvar_t			 wq_done_cv;
	TAILQ_HEAD(, work_struct)	 wq_queue;
	struct work_struct		*wq_running;
	lwp_t				*wq_lwp;
	bool				 wq_dying;
	char				 wq_name[16];
};

struct workqueue_struct *rtw89_system_wq;

static void
rtw89_wq_thread(void *arg)
{
	struct workqueue_struct *wq = arg;
	struct work_struct *w;

	mutex_enter(&wq->wq_lock);
	for (;;) {
		w = TAILQ_FIRST(&wq->wq_queue);
		if (w == NULL) {
			if (wq->wq_dying)
				break;
			cv_wait(&wq->wq_cv, &wq->wq_lock);
			continue;
		}
		TAILQ_REMOVE(&wq->wq_queue, w, w_entry);
		w->w_state = (w->w_state & ~RTW89_WORK_QUEUED) |
		    RTW89_WORK_RUNNING;
		wq->wq_running = w;
		mutex_exit(&wq->wq_lock);

		(*w->func)(w);

		mutex_enter(&wq->wq_lock);
		w->w_state &= ~RTW89_WORK_RUNNING;
		wq->wq_running = NULL;
		cv_broadcast(&wq->wq_done_cv);
	}
	mutex_exit(&wq->wq_lock);
	kthread_exit(0);
}

struct workqueue_struct *
rtw89_alloc_workqueue(const char *name)
{
	struct workqueue_struct *wq;
	int error;

	wq = kmem_zalloc(sizeof(*wq), KM_SLEEP);
	rtw89_kmutex_init(&wq->wq_lock, IPL_NET);
	cv_init(&wq->wq_cv, "rtw89wq");
	cv_init(&wq->wq_done_cv, "rtw89wqd");
	TAILQ_INIT(&wq->wq_queue);
	strlcpy(wq->wq_name, name, sizeof(wq->wq_name));
	/* Not MPSAFE: work items may call into net80211. */
	error = kthread_create(PRI_NONE, KTHREAD_MUSTJOIN, NULL,
	    rtw89_wq_thread, wq, &wq->wq_lwp, "%s", wq->wq_name);
	if (error) {
		cv_destroy(&wq->wq_done_cv);
		cv_destroy(&wq->wq_cv);
		rtw89_kmutex_destroy(&wq->wq_lock);
		kmem_free(wq, sizeof(*wq));
		return NULL;
	}
	return wq;
}

void
rtw89_destroy_workqueue(struct workqueue_struct *wq)
{

	if (wq == NULL)
		return;
	mutex_enter(&wq->wq_lock);
	wq->wq_dying = true;
	cv_broadcast(&wq->wq_cv);
	mutex_exit(&wq->wq_lock);
	kthread_join(wq->wq_lwp);
	cv_destroy(&wq->wq_done_cv);
	cv_destroy(&wq->wq_cv);
	rtw89_kmutex_destroy(&wq->wq_lock);
	kmem_free(wq, sizeof(*wq));
}

void
rtw89_flush_workqueue(struct workqueue_struct *wq)
{

	mutex_enter(&wq->wq_lock);
	while (!TAILQ_EMPTY(&wq->wq_queue) || wq->wq_running != NULL)
		cv_wait(&wq->wq_done_cv, &wq->wq_lock);
	mutex_exit(&wq->wq_lock);
}

void
rtw89_init_work(struct work_struct *w, work_func_t func)
{

	memset(w, 0, sizeof(*w));
	w->func = func;
}

static void
rtw89_dw_timeout(void *arg)
{
	struct delayed_work *dw = arg;

	dw->dw_armed = 0;
	rtw89_queue_work(dw->dw_wq, &dw->work);
}

void
rtw89_init_delayed_work(struct delayed_work *dw, work_func_t func)
{

	rtw89_init_work(&dw->work, func);
	callout_init(&dw->dw_callout, CALLOUT_MPSAFE);
	callout_setfunc(&dw->dw_callout, rtw89_dw_timeout, dw);
	dw->dw_wq = NULL;
	dw->dw_armed = 0;
}

bool
rtw89_queue_work(struct workqueue_struct *wq, struct work_struct *w)
{
	bool queued = false;

	if (wq == NULL)
		wq = rtw89_system_wq;
	if (w->w_wq != NULL && w->w_wq != wq) {
		/* moving between queues: only if idle */
		mutex_enter(&w->w_wq->wq_lock);
		if (w->w_state & RTW89_WORK_QUEUED) {
			mutex_exit(&w->w_wq->wq_lock);
			return false;
		}
		mutex_exit(&w->w_wq->wq_lock);
	}
	mutex_enter(&wq->wq_lock);
	if ((w->w_state & RTW89_WORK_QUEUED) == 0 && !wq->wq_dying) {
		w->w_wq = wq;
		w->w_state |= RTW89_WORK_QUEUED;
		TAILQ_INSERT_TAIL(&wq->wq_queue, w, w_entry);
		cv_signal(&wq->wq_cv);
		queued = true;
	}
	mutex_exit(&wq->wq_lock);
	return queued;
}

bool
rtw89_queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dw,
    unsigned long delay)
{

	if (wq == NULL)
		wq = rtw89_system_wq;
	if (delay == 0)
		return rtw89_queue_work(wq, &dw->work);
	if (dw->dw_armed || work_pending(&dw->work))
		return false;
	dw->dw_wq = wq;
	dw->dw_armed = 1;
	callout_schedule(&dw->dw_callout, MAX(1, (int)delay));
	return true;
}

bool
rtw89_cancel_work(struct work_struct *w, bool sync)
{
	struct workqueue_struct *wq = w->w_wq;
	bool cancelled = false;

	if (wq == NULL)
		return false;
	mutex_enter(&wq->wq_lock);
	if (w->w_state & RTW89_WORK_QUEUED) {
		TAILQ_REMOVE(&wq->wq_queue, w, w_entry);
		w->w_state &= ~RTW89_WORK_QUEUED;
		cancelled = true;
	}
	if (sync && wq->wq_lwp != curlwp) {
		while (w->w_state & RTW89_WORK_RUNNING)
			cv_wait(&wq->wq_done_cv, &wq->wq_lock);
	}
	mutex_exit(&wq->wq_lock);
	return cancelled;
}

bool
rtw89_cancel_delayed_work(struct delayed_work *dw, bool sync)
{
	bool cancelled = false;

	if (sync)
		callout_halt(&dw->dw_callout, NULL);
	else
		callout_stop(&dw->dw_callout);
	if (dw->dw_armed) {
		dw->dw_armed = 0;
		cancelled = true;
	}
	if (rtw89_cancel_work(&dw->work, sync))
		cancelled = true;
	return cancelled;
}

void
rtw89_flush_work(struct work_struct *w)
{
	struct workqueue_struct *wq = w->w_wq;

	if (wq == NULL || wq->wq_lwp == curlwp)
		return;
	mutex_enter(&wq->wq_lock);
	while (w->w_state & (RTW89_WORK_QUEUED | RTW89_WORK_RUNNING))
		cv_wait(&wq->wq_done_cv, &wq->wq_lock);
	mutex_exit(&wq->wq_lock);
}

/* ------------------------------------------------------------------ */
/* Completions                                                         */
/* ------------------------------------------------------------------ */

void
init_completion(struct completion *c)
{

	rtw89_kmutex_init(&c->c_lock, IPL_NET);
	cv_init(&c->c_cv, "rtw89cmp");
	c->c_done = 0;
}

void
reinit_completion(struct completion *c)
{

	mutex_enter(&c->c_lock);
	c->c_done = 0;
	mutex_exit(&c->c_lock);
}

void
complete(struct completion *c)
{

	mutex_enter(&c->c_lock);
	if (c->c_done != UINT_MAX)
		c->c_done++;
	cv_signal(&c->c_cv);
	mutex_exit(&c->c_lock);
}

void
complete_all(struct completion *c)
{

	mutex_enter(&c->c_lock);
	c->c_done = UINT_MAX;
	cv_broadcast(&c->c_cv);
	mutex_exit(&c->c_lock);
}

void
wait_for_completion(struct completion *c)
{

	mutex_enter(&c->c_lock);
	while (c->c_done == 0)
		cv_wait(&c->c_cv, &c->c_lock);
	if (c->c_done != UINT_MAX)
		c->c_done--;
	mutex_exit(&c->c_lock);
}

unsigned long
wait_for_completion_timeout(struct completion *c, unsigned long timeout)
{
	int start = getticks(), now, left;
	unsigned long ret;

	mutex_enter(&c->c_lock);
	for (;;) {
		if (c->c_done != 0)
			break;
		now = getticks();
		left = (int)timeout - (now - start);
		if (left <= 0)
			break;
		if (cold) {
			mutex_exit(&c->c_lock);
			DELAY(1000000 / hz);
			mutex_enter(&c->c_lock);
			continue;
		}
		(void)cv_timedwait(&c->c_cv, &c->c_lock, left);
	}
	if (c->c_done == 0) {
		ret = 0;
	} else {
		if (c->c_done != UINT_MAX)
			c->c_done--;
		now = getticks();
		left = (int)timeout - (now - start);
		ret = left > 0 ? (unsigned long)left : 1;
	}
	mutex_exit(&c->c_lock);
	return ret;
}

bool
completion_done(struct completion *c)
{
	bool done;

	mutex_enter(&c->c_lock);
	done = c->c_done != 0;
	mutex_exit(&c->c_lock);
	return done;
}

/* ------------------------------------------------------------------ */
/* DMA-able buffers and socket buffers                                 */
/* ------------------------------------------------------------------ */

/*
 * Every sk_buff lives at the start of a physically contiguous buffer
 * allocated below 4GB (the RTL8852BE DMA engine uses 32-bit addresses).
 * The data area follows the header, so rtw89_dma_map_single() can map any
 * pointer into it.
 */
#define RTW89_SKB_SMALL		PAGE_SIZE
#define RTW89_SKB_LARGE		(3 * PAGE_SIZE)
#define RTW89_SKB_HDRSZ		roundup(sizeof(struct sk_buff), 64)
#define RTW89_SKB_NSMALL	512
#define RTW89_SKB_NLARGE	(2 * 256 + 48)

struct rtw89_dmabuf {
	SLIST_ENTRY(rtw89_dmabuf) db_next;
	LIST_ENTRY(rtw89_dmabuf) db_all;
	bus_dma_segment_t	 db_seg;
	void			*db_kva;
	size_t			 db_size;
};

static bus_dma_tag_t rtw89_dmat;
static kmutex_t rtw89_dmabuf_lock;
static SLIST_HEAD(, rtw89_dmabuf) rtw89_free_small =
    SLIST_HEAD_INITIALIZER(rtw89_free_small);
static SLIST_HEAD(, rtw89_dmabuf) rtw89_free_large =
    SLIST_HEAD_INITIALIZER(rtw89_free_large);
static LIST_HEAD(, rtw89_dmabuf) rtw89_all_dmabufs =
    LIST_HEAD_INITIALIZER(rtw89_all_dmabufs);
static unsigned int rtw89_skb_nalloc, rtw89_skb_fail;

static struct rtw89_dmabuf *
rtw89_dmabuf_alloc(size_t size, bool canwait)
{
	struct rtw89_dmabuf *db;
	int rseg, error;
	int flags = canwait ? BUS_DMA_WAITOK : BUS_DMA_NOWAIT;

	db = kmem_intr_zalloc(sizeof(*db), canwait ? KM_SLEEP : KM_NOSLEEP);
	if (db == NULL)
		return NULL;
	size = round_page(size);
	error = bus_dmamem_alloc(rtw89_dmat, size, PAGE_SIZE, 0, &db->db_seg,
	    1, &rseg, flags);
	if (error)
		goto fail;
	if (db->db_seg.ds_addr + size > 0x100000000ULL) {
		printf("rtw89: DMA buffer above 4GB (0x%" PRIxPADDR ")\n",
		    (paddr_t)db->db_seg.ds_addr);
		bus_dmamem_free(rtw89_dmat, &db->db_seg, 1);
		goto fail;
	}
	error = bus_dmamem_map(rtw89_dmat, &db->db_seg, 1, size, &db->db_kva,
	    flags | BUS_DMA_COHERENT);
	if (error) {
		bus_dmamem_free(rtw89_dmat, &db->db_seg, 1);
		goto fail;
	}
	db->db_size = size;
	mutex_enter(&rtw89_dmabuf_lock);
	LIST_INSERT_HEAD(&rtw89_all_dmabufs, db, db_all);
	mutex_exit(&rtw89_dmabuf_lock);
	return db;
fail:
	kmem_intr_free(db, sizeof(*db));
	return NULL;
}

static void
rtw89_dmabuf_free(struct rtw89_dmabuf *db)
{

	mutex_enter(&rtw89_dmabuf_lock);
	LIST_REMOVE(db, db_all);
	mutex_exit(&rtw89_dmabuf_lock);
	bus_dmamem_unmap(rtw89_dmat, db->db_kva, db->db_size);
	bus_dmamem_free(rtw89_dmat, &db->db_seg, 1);
	kmem_intr_free(db, sizeof(*db));
}

int
rtw89_compat_init(bus_dma_tag_t dmat)
{
	struct rtw89_dmabuf *db;
	int i;

	if (rtw89_dmat != NULL)
		return 0;
	rtw89_dmat = dmat;
	rtw89_kmutex_init(&rtw89_dmabuf_lock, IPL_NET);
	for (i = 0; i < RTW89_SKB_NSMALL; i++) {
		db = rtw89_dmabuf_alloc(RTW89_SKB_SMALL, true);
		if (db == NULL)
			goto fail;
		SLIST_INSERT_HEAD(&rtw89_free_small, db, db_next);
	}
	for (i = 0; i < RTW89_SKB_NLARGE; i++) {
		db = rtw89_dmabuf_alloc(RTW89_SKB_LARGE, true);
		if (db == NULL)
			goto fail;
		SLIST_INSERT_HEAD(&rtw89_free_large, db, db_next);
	}
	rtw89_system_wq = rtw89_alloc_workqueue("rtw89sys");
	if (rtw89_system_wq == NULL)
		goto fail;
	return 0;
fail:
	rtw89_compat_fini();
	return ENOMEM;
}

void
rtw89_compat_fini(void)
{
	struct rtw89_dmabuf *db;

	if (rtw89_dmat == NULL)
		return;
	if (rtw89_system_wq != NULL) {
		rtw89_destroy_workqueue(rtw89_system_wq);
		rtw89_system_wq = NULL;
	}
	while ((db = SLIST_FIRST(&rtw89_free_small)) != NULL) {
		SLIST_REMOVE_HEAD(&rtw89_free_small, db_next);
		rtw89_dmabuf_free(db);
	}
	while ((db = SLIST_FIRST(&rtw89_free_large)) != NULL) {
		SLIST_REMOVE_HEAD(&rtw89_free_large, db_next);
		rtw89_dmabuf_free(db);
	}
	/* coherent allocations must have been released by the driver */
	if (!LIST_EMPTY(&rtw89_all_dmabufs))
		printf("rtw89: leaking DMA buffers on unload\n");
	rtw89_kmutex_destroy(&rtw89_dmabuf_lock);
	rtw89_dmat = NULL;
}

struct sk_buff *
rtw89_alloc_skb(unsigned int size, gfp_t gfp)
{
	struct rtw89_dmabuf *db = NULL;
	struct sk_buff *skb;
	size_t need = RTW89_SKB_HDRSZ + NET_SKB_PAD + size;

	if (need <= RTW89_SKB_SMALL) {
		mutex_enter(&rtw89_dmabuf_lock);
		db = SLIST_FIRST(&rtw89_free_small);
		if (db != NULL)
			SLIST_REMOVE_HEAD(&rtw89_free_small, db_next);
		mutex_exit(&rtw89_dmabuf_lock);
	}
	if (db == NULL && need <= RTW89_SKB_LARGE) {
		mutex_enter(&rtw89_dmabuf_lock);
		db = SLIST_FIRST(&rtw89_free_large);
		if (db != NULL)
			SLIST_REMOVE_HEAD(&rtw89_free_large, db_next);
		mutex_exit(&rtw89_dmabuf_lock);
	}
	if (db == NULL) {
		bool canwait = (gfp & GFP_ATOMIC) == 0 && rtw89_can_sleep();

		db = rtw89_dmabuf_alloc(MAX(need, RTW89_SKB_SMALL), canwait);
		if (db == NULL) {
			if (rtw89_skb_fail++ < 10)
				printf("rtw89: out of sk_buff memory (%u)\n",
				    size);
			return NULL;
		}
	}
	skb = db->db_kva;
	memset(skb, 0, sizeof(*skb));
	skb->skb_buf = db;
	skb->head = (unsigned char *)db->db_kva + RTW89_SKB_HDRSZ;
	skb->end = (unsigned char *)db->db_kva + db->db_size;
	skb->data = skb->tail = skb->head + NET_SKB_PAD;
	atomic_inc_uint(&rtw89_skb_nalloc);
	return skb;
}

void
rtw89_free_skb(struct sk_buff *skb)
{
	struct rtw89_dmabuf *db;

	if (skb == NULL)
		return;
	db = skb->skb_buf;
	KASSERT(db->db_kva == (void *)skb);
	atomic_dec_uint(&rtw89_skb_nalloc);
	mutex_enter(&rtw89_dmabuf_lock);
	if (db->db_size == RTW89_SKB_SMALL) {
		SLIST_INSERT_HEAD(&rtw89_free_small, db, db_next);
		db = NULL;
	} else if (db->db_size == RTW89_SKB_LARGE) {
		SLIST_INSERT_HEAD(&rtw89_free_large, db, db_next);
		db = NULL;
	}
	mutex_exit(&rtw89_dmabuf_lock);
	if (db != NULL)
		rtw89_dmabuf_free(db);
}

struct sk_buff *
rtw89_skb_copy(const struct sk_buff *skb, gfp_t gfp)
{
	struct sk_buff *n;
	unsigned int headroom = skb->data - skb->head;

	n = rtw89_alloc_skb(skb->end - skb->head, gfp);
	if (n == NULL)
		return NULL;
	n->data = n->head + headroom;
	n->tail = n->data;
	memcpy(skb_put(n, skb->len), skb->data, skb->len);
	n->priority = skb->priority;
	n->queue_mapping = skb->queue_mapping;
	n->protocol = skb->protocol;
	n->network_header = skb->network_header;
	n->transport_header = skb->transport_header;
	memcpy(n->cb, skb->cb, sizeof(n->cb));
	return n;
}

int
rtw89_pskb_expand_head(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp)
{

	/* The header lives inside the buffer; growing is not supported. */
	if (skb_headroom(skb) >= (unsigned)nhead && skb_tailroom(skb) >= ntail)
		return 0;
	return -ENOMEM;
}

dma_addr_t
rtw89_dma_map_single(void *ptr, size_t size)
{
	vaddr_t va = (vaddr_t)ptr, pg;
	paddr_t pa, pa2;

	if (!pmap_extract(pmap_kernel(), va, &pa))
		return DMA_MAPPING_ERROR;
	for (pg = trunc_page(va) + PAGE_SIZE; pg < va + size; pg += PAGE_SIZE) {
		if (!pmap_extract(pmap_kernel(), pg, &pa2) ||
		    pa2 != trunc_page(pa) + (pg - trunc_page(va)))
			return DMA_MAPPING_ERROR;
	}
	if (pa + size > 0x100000000ULL)
		return DMA_MAPPING_ERROR;
	membar_sync();
	return pa;
}

void *
rtw89_dma_alloc_coherent(size_t size, dma_addr_t *dma)
{
	struct rtw89_dmabuf *db;

	db = rtw89_dmabuf_alloc(size, rtw89_can_sleep());
	if (db == NULL)
		return NULL;
	memset(db->db_kva, 0, db->db_size);
	*dma = db->db_seg.ds_addr;
	return db->db_kva;
}

void
rtw89_dma_free_coherent(size_t size, void *kva, dma_addr_t dma)
{
	struct rtw89_dmabuf *db;

	mutex_enter(&rtw89_dmabuf_lock);
	LIST_FOREACH(db, &rtw89_all_dmabufs, db_all)
		if (db->db_kva == kva)
			break;
	mutex_exit(&rtw89_dmabuf_lock);
	if (db == NULL) {
		printf("rtw89: freeing unknown coherent buffer %p\n", kva);
		return;
	}
	rtw89_dmabuf_free(db);
}

/* ------------------------------------------------------------------ */
/* NAPI on top of a softint                                            */
/* ------------------------------------------------------------------ */

static void
rtw89_napi_softint(void *arg)
{
	struct napi_struct *napi = arg;
	int done;

	if (!napi->enabled) {
		clear_bit(NAPI_STATE_SCHED, &napi->state);
		return;
	}
	done = (*napi->poll)(napi, NAPI_POLL_WEIGHT);
	if (done >= NAPI_POLL_WEIGHT && test_bit(NAPI_STATE_SCHED, &napi->state))
		softint_schedule(napi->nb_sih);
}

void
rtw89_netif_napi_add(struct net_device *dev, struct napi_struct *napi,
    int (*poll)(struct napi_struct *, int))
{

	napi->dev = dev;
	napi->poll = poll;
	napi->state = 0;
	napi->enabled = false;
	/* Not MPSAFE: the RX path ends in net80211. */
	napi->nb_sih = softint_establish(SOFTINT_NET, rtw89_napi_softint, napi);
}

void
rtw89_netif_napi_del(struct napi_struct *napi)
{

	if (napi->nb_sih != NULL) {
		softint_disestablish(napi->nb_sih);
		napi->nb_sih = NULL;
	}
}

void
rtw89_napi_schedule(struct napi_struct *napi)
{

	if (!test_and_set_bit(NAPI_STATE_SCHED, &napi->state))
		softint_schedule(napi->nb_sih);
}

bool
rtw89_napi_complete_done(struct napi_struct *napi, int work_done)
{

	clear_bit(NAPI_STATE_SCHED, &napi->state);
	return true;
}

void
rtw89_napi_synchronize(struct napi_struct *napi)
{
	int i;

	for (i = 0; i < 1000 && test_bit(NAPI_STATE_SCHED, &napi->state); i++)
		rtw89_usleep(1000);
}

/* ------------------------------------------------------------------ */
/* Firmware                                                            */
/* ------------------------------------------------------------------ */

int
rtw89_request_firmware(const struct firmware **fwp, const char *name,
    struct device *dev)
{
	struct firmware *fw;
	firmware_handle_t fh;
	const char *base;
	size_t size;
	u8 *data;
	int error;

	*fwp = NULL;
	base = strrchr(name, '/');
	base = base ? base + 1 : name;
	error = firmware_open("if_rtw89", base, &fh);
	if (error)
		return -error;
	size = firmware_get_size(fh);
	if (size == 0 || size > 8 * 1024 * 1024) {
		firmware_close(fh);
		return -EFBIG;
	}
	data = firmware_malloc(size);
	error = firmware_read(fh, 0, data, size);
	firmware_close(fh);
	if (error) {
		firmware_free(data, size);
		return -error;
	}
	fw = kmem_zalloc(sizeof(*fw), KM_SLEEP);
	fw->data = data;
	fw->size = size;
	*fwp = fw;
	return 0;
}

void
rtw89_release_firmware(const struct firmware *fw)
{

	if (fw == NULL)
		return;
	firmware_free(__UNCONST(fw->data), fw->size);
	kmem_free(__UNCONST(fw), sizeof(*fw));
}
