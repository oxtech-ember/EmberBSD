/*-
 * Copyright (c) 2026 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Kernel services for the radio code of the ESP32-S31 (esp_wifi_os.h).
 *
 * The vendor's libraries were written for FreeRTOS: tasks, queues, counting
 * semaphores, timers that run in a task.  Each is built here from a kernel
 * thread, one spin mutex and condition variables.  The lock is a spin mutex
 * because the interrupt handler of the radio posts to the same queues.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include "opt_multiprocessor.h"
#include "opt_espwifi_net80211.h"
#include "opt_espwifi_wpa2.h"

#ifdef MULTIPROCESSOR
#error "ESPWIFI critical sections currently require a single CPU"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/cprng.h>
#include <sys/cpu.h>
#include <sys/entropy.h>
#include <sys/intr.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/vmem.h>

#include <lib/libkern/libkern.h>

#include <dev/ofw/openfirm.h>
#include <dev/fdt/fdtvar.h>

#include <riscv/espressif/esp_intmtx.h>
#include <riscv/espressif/esp_wifi_os.h>
#include <riscv/espressif/esp_wifi_var.h>

/* Internal RAM left free by the firmware: the pool for DMA memory. */
#define	ESPWIFI_POOL_BASE	0x2f030000
#define	ESPWIFI_POOL_SIZE	0x00040000

#define	ESPWIFI_STACK_SIZE	(16 * 1024)
#define	ESPWIFI_NTHREADS	8

struct espwifi_fp {
	uint32_t	fp_reg[32];
	uint32_t	fp_fcsr;
	uint32_t	fp_fs;
};

void	espwifi_fp_save(struct espwifi_fp *);
void	espwifi_fp_restore(const struct espwifi_fp *);
void	espwifi_call_on_stack(void (*)(void *), void *, void *);

struct espwifi_hdr {
	uint32_t	h_size;		/* whole block, header included */
	uint32_t	h_pool;
	uint32_t	h_pad[2];
};

struct espwifi_sem {
	kcondvar_t	s_cv;
	u_int		s_count;
	u_int		s_max;
};

struct espwifi_mutex {
	kcondvar_t	m_cv;
	lwp_t		*m_owner;
	u_int		m_depth;
};

struct espwifi_queue {
	kcondvar_t	q_cv;
	u_int		q_len;
	u_int		q_size;
	u_int		q_head;
	u_int		q_count;
	uint8_t		*q_data;
};

struct espwifi_event {
	kcondvar_t	e_cv;
	u_int		e_bits;
};

struct espwifi_task {
	lwp_t		*t_lwp;
	void		(*t_func)(void *);
	void		*t_arg;
	void		*t_stack;
	struct espwifi_sem *t_sem;
	int		t_fpu_cookie;
	label_t		t_return;
};

struct espwifi_timer {
	callout_t	t_co;
	void		(*t_func)(void *);
	void		*t_arg;
	u_int		t_ticks;
	bool		t_repeat;
	bool		t_armed;
	bool		t_queued;
	bool		t_dead;
	TAILQ_ENTRY(espwifi_timer) t_link;
};

struct espwifi_intr {
	void		(*i_func)(void *);
	void		*i_arg;
	u_int		i_src;
};

static kmutex_t espwifi_mtx;
static vmem_t *espwifi_pool;
static u_long espwifi_pool_free;
static int espwifi_crit_depth;
static int espwifi_crit_spl;
static struct espwifi_task *espwifi_tasks[ESPWIFI_NTHREADS];
static TAILQ_HEAD(, espwifi_timer) espwifi_fired =
    TAILQ_HEAD_INITIALIZER(espwifi_fired);
static kcondvar_t espwifi_timer_cv;

/*
 * Sleep on a condition, at most `ticks` of the radio's clock.  The F
 * registers of the sleeper are kept across the sleep: other radio threads
 * run meanwhile.  Returns false when the time ran out.
 */
static bool
espwifi_wait(kcondvar_t *cv, u_int ticks, int *left)
{
	struct espwifi_fp fp;
	bool ok = true;

	if (ticks == 0)
		return false;
	espwifi_fp_save(&fp);
	if (ticks == ESPWIFI_WAIT_FOREVER) {
		cv_wait(cv, &espwifi_mtx);
	} else {
		if (*left < 0)
			*left = MAX(1, mstohz(ticks * ESPWIFI_TICK_MS));
		const int start = getticks();
		if (cv_timedwait(cv, &espwifi_mtx, *left) == EWOULDBLOCK)
			ok = false;
		*left -= getticks() - start;
		if (*left <= 0)
			ok = false;
	}
	espwifi_fp_restore(&fp);
	return ok;
}

/*
 * Memory.
 */

static void *
espwifi_alloc(size_t size, bool internal)
{
	const size_t total = roundup2(size + sizeof(struct espwifi_hdr), 16);
	struct espwifi_hdr *h = NULL;
	vmem_addr_t addr;

	if (internal && espwifi_pool != NULL &&
	    vmem_alloc(espwifi_pool, total, VM_INSTANTFIT | VM_NOSLEEP,
	    &addr) == 0) {
		h = (void *)addr;
		h->h_pool = 1;
		atomic_add_long(&espwifi_pool_free, -(long)total);
	} else {
		h = malloc(total, M_DEVBUF, M_NOWAIT);
		if (h == NULL)
			return NULL;
		h->h_pool = 0;
	}
	h->h_size = total;
	return h + 1;
}

void *
espwifi_os_malloc(unsigned long size)
{
	return espwifi_alloc(size, false);
}

void *
espwifi_os_malloc_internal(unsigned long size)
{
	return espwifi_alloc(size, true);
}

void
espwifi_os_free(void *p)
{
	if (p == NULL)
		return;

	struct espwifi_hdr *h = (struct espwifi_hdr *)p - 1;
	if (h->h_pool) {
		const size_t total = h->h_size;

		vmem_free(espwifi_pool, (vmem_addr_t)h, total);
		atomic_add_long(&espwifi_pool_free, total);
	} else {
		free(h, M_DEVBUF);
	}
}

void *
espwifi_os_realloc(void *p, unsigned long size)
{
	void *n = espwifi_alloc(size, true);

	if (n != NULL && p != NULL) {
		const struct espwifi_hdr *h = (struct espwifi_hdr *)p - 1;

		memcpy(n, p, MIN(size, h->h_size - sizeof(*h)));
		espwifi_os_free(p);
	}
	return n;
}

unsigned long
espwifi_os_free_heap(void)
{
	return espwifi_pool_free;
}

/*
 * Critical sections: one processor, so raising the priority level is
 * enough.  They nest.
 */

unsigned int
espwifi_os_critical_enter(void)
{
	const int s = splhigh();

	if (espwifi_crit_depth++ == 0)
		espwifi_crit_spl = s;
	return 0;
}

void
espwifi_os_critical_exit(unsigned int token)
{
	KASSERT(espwifi_crit_depth > 0);
	if (--espwifi_crit_depth == 0)
		splx(espwifi_crit_spl);
}

int
espwifi_os_in_isr(void)
{
	return cpu_intr_p();
}

/*
 * Counting semaphores.
 */

void *
espwifi_os_sem_create(unsigned int max, unsigned int init)
{
	struct espwifi_sem *s = malloc(sizeof(*s), M_DEVBUF, M_NOWAIT);

	if (s == NULL)
		return NULL;
	cv_init(&s->s_cv, "espwsem");
	s->s_count = init;
	s->s_max = max;
	return s;
}

void
espwifi_os_sem_delete(void *v)
{
	struct espwifi_sem *s = v;

	cv_destroy(&s->s_cv);
	free(s, M_DEVBUF);
}

int
espwifi_os_sem_take(void *v, unsigned int ticks)
{
	struct espwifi_sem *s = v;
	int left = -1;
	int ok = 1;

	mutex_spin_enter(&espwifi_mtx);
	while (s->s_count == 0) {
		if (!espwifi_wait(&s->s_cv, ticks, &left) &&
		    s->s_count == 0) {
			ok = 0;
			break;
		}
	}
	if (ok)
		s->s_count--;
	mutex_spin_exit(&espwifi_mtx);
	return ok;
}

int
espwifi_os_sem_give(void *v)
{
	struct espwifi_sem *s = v;
	int ok = 0;

	mutex_spin_enter(&espwifi_mtx);
	if (s->s_count < s->s_max) {
		s->s_count++;
		cv_signal(&s->s_cv);
		ok = 1;
	}
	mutex_spin_exit(&espwifi_mtx);
	return ok;
}

/*
 * Mutexes; all of them may be taken again by their owner.
 */

void *
espwifi_os_mutex_create(void)
{
	struct espwifi_mutex *m = malloc(sizeof(*m), M_DEVBUF, M_NOWAIT);

	if (m == NULL)
		return NULL;
	cv_init(&m->m_cv, "espwmtx");
	m->m_owner = NULL;
	m->m_depth = 0;
	return m;
}

void
espwifi_os_mutex_delete(void *v)
{
	struct espwifi_mutex *m = v;

	cv_destroy(&m->m_cv);
	free(m, M_DEVBUF);
}

int
espwifi_os_mutex_lock(void *v)
{
	struct espwifi_mutex *m = v;
	int left = -1;

	mutex_spin_enter(&espwifi_mtx);
	while (m->m_owner != NULL && m->m_owner != curlwp)
		espwifi_wait(&m->m_cv, ESPWIFI_WAIT_FOREVER, &left);
	m->m_owner = curlwp;
	m->m_depth++;
	mutex_spin_exit(&espwifi_mtx);
	return 1;
}

int
espwifi_os_mutex_unlock(void *v)
{
	struct espwifi_mutex *m = v;

	mutex_spin_enter(&espwifi_mtx);
	if (m->m_owner == curlwp && --m->m_depth == 0) {
		m->m_owner = NULL;
		cv_signal(&m->m_cv);
	}
	mutex_spin_exit(&espwifi_mtx);
	return 1;
}

void
espwifi_os_lock_acquire(void **lock)
{
	if (*lock == NULL)
		*lock = espwifi_os_mutex_create();
	espwifi_os_mutex_lock(*lock);
}

void
espwifi_os_lock_release(void **lock)
{
	espwifi_os_mutex_unlock(*lock);
}

/*
 * Queues of fixed-size items.
 */

void *
espwifi_os_queue_create(unsigned int len, unsigned int size)
{
	struct espwifi_queue *q;

	q = malloc(sizeof(*q) + len * size, M_DEVBUF, M_NOWAIT);
	if (q == NULL)
		return NULL;
	cv_init(&q->q_cv, "espwq");
	q->q_len = len;
	q->q_size = size;
	q->q_head = 0;
	q->q_count = 0;
	q->q_data = (uint8_t *)(q + 1);
	return q;
}

void
espwifi_os_queue_delete(void *v)
{
	struct espwifi_queue *q = v;

	cv_destroy(&q->q_cv);
	free(q, M_DEVBUF);
}

int
espwifi_os_queue_send(void *v, const void *item, unsigned int ticks,
    int front)
{
	struct espwifi_queue *q = v;
	int left = -1;
	int ok = 1;

	mutex_spin_enter(&espwifi_mtx);
	while (q->q_count == q->q_len) {
		if (cpu_intr_p() ||
		    (!espwifi_wait(&q->q_cv, ticks, &left) &&
		    q->q_count == q->q_len)) {
			ok = 0;
			break;
		}
	}
	if (ok) {
		u_int slot;

		if (front) {
			q->q_head = (q->q_head + q->q_len - 1) % q->q_len;
			slot = q->q_head;
		} else {
			slot = (q->q_head + q->q_count) % q->q_len;
		}
		memcpy(q->q_data + slot * q->q_size, item, q->q_size);
		q->q_count++;
		cv_broadcast(&q->q_cv);
	}
	mutex_spin_exit(&espwifi_mtx);
	return ok;
}

int
espwifi_os_queue_recv(void *v, void *item, unsigned int ticks)
{
	struct espwifi_queue *q = v;
	int left = -1;
	int ok = 1;

	mutex_spin_enter(&espwifi_mtx);
	while (q->q_count == 0) {
		if (!espwifi_wait(&q->q_cv, ticks, &left) &&
		    q->q_count == 0) {
			ok = 0;
			break;
		}
	}
	if (ok) {
		memcpy(item, q->q_data + q->q_head * q->q_size, q->q_size);
		q->q_head = (q->q_head + 1) % q->q_len;
		q->q_count--;
		cv_broadcast(&q->q_cv);
	}
	mutex_spin_exit(&espwifi_mtx);
	return ok;
}

unsigned int
espwifi_os_queue_waiting(void *v)
{
	struct espwifi_queue *q = v;

	return q->q_count;
}

/*
 * Event groups: a word of bits to wait on.
 */

void *
espwifi_os_event_create(void)
{
	struct espwifi_event *e = malloc(sizeof(*e), M_DEVBUF, M_NOWAIT);

	if (e == NULL)
		return NULL;
	cv_init(&e->e_cv, "espwev");
	e->e_bits = 0;
	return e;
}

void
espwifi_os_event_delete(void *v)
{
	struct espwifi_event *e = v;

	cv_destroy(&e->e_cv);
	free(e, M_DEVBUF);
}

unsigned int
espwifi_os_event_set(void *v, unsigned int bits)
{
	struct espwifi_event *e = v;
	u_int now;

	mutex_spin_enter(&espwifi_mtx);
	e->e_bits |= bits;
	now = e->e_bits;
	cv_broadcast(&e->e_cv);
	mutex_spin_exit(&espwifi_mtx);
	return now;
}

unsigned int
espwifi_os_event_clear(void *v, unsigned int bits)
{
	struct espwifi_event *e = v;
	u_int before;

	mutex_spin_enter(&espwifi_mtx);
	before = e->e_bits;
	e->e_bits &= ~bits;
	mutex_spin_exit(&espwifi_mtx);
	return before;
}

unsigned int
espwifi_os_event_wait(void *v, unsigned int bits, int clear, int all,
    unsigned int ticks)
{
	struct espwifi_event *e = v;
	int left = -1;
	u_int seen;

	mutex_spin_enter(&espwifi_mtx);
	for (;;) {
		seen = e->e_bits;
		const bool hit = all ? (seen & bits) == bits :
		    (seen & bits) != 0;
		if (hit) {
			if (clear)
				e->e_bits &= ~bits;
			break;
		}
		if (!espwifi_wait(&e->e_cv, ticks, &left)) {
			seen = e->e_bits;
			break;
		}
	}
	mutex_spin_exit(&espwifi_mtx);
	return seen;
}

/*
 * Tasks.  Each is a kernel thread that runs on a stack of its own with
 * floating point enabled.  They run above user processes: the radio
 * answers its peer within milliseconds.
 */

static void
espwifi_task_main(void *v)
{
	struct espwifi_task *t = v;

	mutex_spin_enter(&espwifi_mtx);
	t->t_lwp = curlwp;
	mutex_spin_exit(&espwifi_mtx);
	t->t_fpu_cookie = kthread_fpu_enter();
	if (setjmp(&t->t_return) == 0)
		espwifi_call_on_stack(t->t_func, t->t_arg,
		    (char *)t->t_stack + ESPWIFI_STACK_SIZE);
	mutex_spin_enter(&espwifi_mtx);
	for (u_int i = 0; i < ESPWIFI_NTHREADS; i++)
		if (espwifi_tasks[i] == t) {
			espwifi_tasks[i] = NULL;
			break;
		}
	mutex_spin_exit(&espwifi_mtx);
	kthread_fpu_exit(t->t_fpu_cookie);
	espwifi_os_sem_delete(t->t_sem);
	free(t->t_stack, M_DEVBUF);
	free(t, M_DEVBUF);
	kthread_exit(0);
}

int
espwifi_os_task_create(void (*func)(void *), const char *name,
    unsigned int stack, void *arg, void **handle)
{
	struct espwifi_task *t;
	lwp_t *thread;
	u_int slot;

	/* ESP-IDF specifies stack depth in bytes. */
	if (stack > ESPWIFI_STACK_SIZE)
		return 0;
	t = malloc(sizeof(*t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (t == NULL)
		return 0;
	t->t_stack = malloc(ESPWIFI_STACK_SIZE, M_DEVBUF, M_NOWAIT);
	t->t_sem = espwifi_os_sem_create(1, 0);
	if (t->t_stack == NULL || t->t_sem == NULL)
		goto fail;
	t->t_func = func;
	t->t_arg = arg;

	mutex_spin_enter(&espwifi_mtx);
	for (slot = 0; slot < ESPWIFI_NTHREADS; slot++) {
		if (espwifi_tasks[slot] == NULL) {
			espwifi_tasks[slot] = t;
			break;
		}
	}
	mutex_spin_exit(&espwifi_mtx);
	if (slot == ESPWIFI_NTHREADS) {
		printf("espwifi: too many tasks\n");
		goto fail;
	}
	if (kthread_create(PRI_SOFTNET, KTHREAD_MPSAFE, NULL, espwifi_task_main,
	    t, &thread, "espwifi/%s", name) != 0) {
		mutex_spin_enter(&espwifi_mtx);
		espwifi_tasks[slot] = NULL;
		mutex_spin_exit(&espwifi_mtx);
		goto fail;
	}
	if (handle != NULL)
		*handle = thread;
	return 1;

fail:
	if (t->t_sem != NULL)
		espwifi_os_sem_delete(t->t_sem);
	if (t->t_stack != NULL)
		free(t->t_stack, M_DEVBUF);
	free(t, M_DEVBUF);
	return 0;
}

static struct espwifi_task *
espwifi_task_self(void)
{
	struct espwifi_task *found = NULL;

	mutex_spin_enter(&espwifi_mtx);
	for (u_int i = 0; i < ESPWIFI_NTHREADS; i++) {
		struct espwifi_task * const t = espwifi_tasks[i];

		if (t != NULL && t->t_lwp == curlwp) {
			found = t;
			break;
		}
	}
	mutex_spin_exit(&espwifi_mtx);
	return found;
}

void
espwifi_os_task_delete(void *handle)
{
	struct espwifi_task *task = NULL;

	if (cpu_intr_p() || cpu_softintr_p() ||
	    curcpu()->ci_cpl != IPL_NONE || espwifi_crit_depth != 0) {
		printf("espwifi: task deletion outside thread context ignored\n");
		return;
	}
	if (handle != NULL && handle != curlwp) {
		printf("espwifi: a task deletes another one: ignored\n");
		return;
	}
	task = espwifi_task_self();
	if (task != NULL)
		longjmp(&task->t_return);
	printf("espwifi: task deletion outside a radio thread ignored\n");
}

void
espwifi_os_task_delay(unsigned int ticks)
{
	struct espwifi_fp fp;

	espwifi_fp_save(&fp);
	kpause("espwdly", false, MAX(1, mstohz(ticks * ESPWIFI_TICK_MS)),
	    NULL);
	espwifi_fp_restore(&fp);
}

void *
espwifi_os_task_current(void)
{
	return curlwp;
}

void *
espwifi_os_thread_sem(void)
{
	struct espwifi_task * const t = espwifi_task_self();

	if (t == NULL)
		panic("espwifi: thread semaphore outside a radio thread");
	return t->t_sem;
}

/*
 * Timers.  A callout marks the timer fired; a thread runs the function,
 * because the radio code sleeps in its timer functions.
 */

static void
espwifi_timer_fire(void *v)
{
	struct espwifi_timer *t = v;

	mutex_spin_enter(&espwifi_mtx);
	if (t->t_armed && !t->t_queued) {
		t->t_queued = true;
		TAILQ_INSERT_TAIL(&espwifi_fired, t, t_link);
		cv_signal(&espwifi_timer_cv);
	}
	mutex_spin_exit(&espwifi_mtx);
}

static void
espwifi_timer_thread(void *v)
{
	struct espwifi_timer *t;
	int left = -1;

	mutex_spin_enter(&espwifi_mtx);
	for (;;) {
		while ((t = TAILQ_FIRST(&espwifi_fired)) == NULL)
			espwifi_wait(&espwifi_timer_cv, ESPWIFI_WAIT_FOREVER,
			    &left);
		TAILQ_REMOVE(&espwifi_fired, t, t_link);
		t->t_queued = false;
		if (t->t_dead) {
			mutex_spin_exit(&espwifi_mtx);
			callout_destroy(&t->t_co);
			free(t, M_DEVBUF);
			mutex_spin_enter(&espwifi_mtx);
			continue;
		}
		if (!t->t_armed)
			continue;
		if (t->t_repeat)
			callout_schedule(&t->t_co, t->t_ticks);
		else
			t->t_armed = false;
		mutex_spin_exit(&espwifi_mtx);
		(*t->t_func)(t->t_arg);
		mutex_spin_enter(&espwifi_mtx);
	}
}

void *
espwifi_os_timer_create(void (*func)(void *), void *arg)
{
	struct espwifi_timer *t;

	t = malloc(sizeof(*t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (t == NULL)
		return NULL;
	callout_init(&t->t_co, CALLOUT_MPSAFE);
	callout_setfunc(&t->t_co, espwifi_timer_fire, t);
	t->t_func = func;
	t->t_arg = arg;
	return t;
}

void
espwifi_os_timer_arm(void *v, unsigned int us, int repeat)
{
	struct espwifi_timer *t = v;

	mutex_spin_enter(&espwifi_mtx);
	t->t_ticks = MAX(1, (u_int)(((uint64_t)us * hz + 999999) / 1000000));
	t->t_repeat = repeat != 0;
	t->t_armed = true;
	callout_schedule(&t->t_co, t->t_ticks);
	mutex_spin_exit(&espwifi_mtx);
}

void
espwifi_os_timer_disarm(void *v)
{
	struct espwifi_timer *t = v;

	mutex_spin_enter(&espwifi_mtx);
	t->t_armed = false;
	callout_stop(&t->t_co);
	mutex_spin_exit(&espwifi_mtx);
}

void
espwifi_os_timer_delete(void *v)
{
	struct espwifi_timer *t = v;

	mutex_spin_enter(&espwifi_mtx);
	t->t_armed = false;
	t->t_dead = true;
	callout_stop(&t->t_co);
	if (!t->t_queued) {
		t->t_queued = true;
		TAILQ_INSERT_TAIL(&espwifi_fired, t, t_link);
		cv_signal(&espwifi_timer_cv);
	}
	mutex_spin_exit(&espwifi_mtx);
}

/*
 * Interrupts.  The handler of the radio runs in whatever context was
 * interrupted, so the F registers of that context are kept.
 */

static int
espwifi_intr(void *v)
{
	struct espwifi_intr * const i = v;
	struct espwifi_fp fp;

	espwifi_fp_save(&fp);
	(*i->i_func)(i->i_arg);
	espwifi_fp_restore(&fp);
	return 1;
}

void
espwifi_os_intr_establish(unsigned int src, void (*func)(void *), void *arg)
{
	struct espwifi_intr *i = malloc(sizeof(*i), M_DEVBUF, M_NOWAIT);

	if (i == NULL)
		panic("espwifi: no memory for an interrupt handler");
	i->i_func = func;
	i->i_arg = arg;
	i->i_src = src;
	if (espintmtx_establish_source(src, IPL_NET, espwifi_intr, i,
	    "espwifi") == NULL)
		panic("espwifi: cannot take interrupt source %u", src);
	/* The radio code enables the line when it is ready. */
	espintmtx_source_enable(src, false);
}

void
espwifi_os_intr_enable(unsigned int src, int on)
{
	espintmtx_source_enable(src, on != 0);
}

/*
 * Time, randomness, messages.
 */

long long
espwifi_os_time_us(void)
{
	struct timeval tv;

	microuptime(&tv);
	return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

void
espwifi_os_delay_us(unsigned int us)
{
	delay(us);
}

unsigned int
espwifi_os_random(void)
{
	return cprng_fast32();
}

int
espwifi_os_secret_random(void *buffer, unsigned long len)
{
	uint8_t *p = buffer;
	const unsigned long total = len;

	if (len == 0)
		return 0;
	/* Strong CPRNG may reseed, which requires a sleepable context. */
	if (cpu_intr_p() || curcpu()->ci_cpl != IPL_NONE ||
	    espwifi_crit_depth != 0 || !entropy_ready())
		return -1;
	while (len != 0) {
		const size_t n = MIN(len, 32);

		if (cprng_strong(kern_cprng, p, n, 0) != n) {
			explicit_memset(buffer, 0, total);
			return -1;
		}
		p += n;
		len -= n;
	}
	return 0;
}

int
espwifi_os_ct_equal(const void *a, const void *b, unsigned long len)
{
	return consttime_memequal(a, b, len);
}

void
espwifi_os_vlog(const char *fmt, va_list ap)
{
	vprintf(fmt, ap);
}

void
espwifi_os_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void
espwifi_os_panic(const char *what)
{
	panic("espwifi: %s", what);
}

/*
 * A frame from the radio, on the thread of the libraries.  The network
 * stack may sleep under it, so the F registers are kept.
 */
void
espwifi_os_rx(const void *buf, unsigned int len)
{
	struct espwifi_fp fp;

	espwifi_fp_save(&fp);
	espwifi_if_input(buf, len);
	espwifi_fp_restore(&fp);
}

void
espwifi_os_scan_frame(const void *buf, unsigned int len,
    unsigned int channel, int rssi, unsigned int timestamp)
{
	struct espwifi_fp fp;

	espwifi_fp_save(&fp);
	espwifi_if_scan_frame(buf, len, channel, rssi, timestamp);
	espwifi_fp_restore(&fp);
}

int
espwifi_os_rx_eapol(const unsigned char *source, const void *buf,
    unsigned int len)
{
	struct espwifi_fp fp;
	int error;

	espwifi_fp_save(&fp);
	error = espwifi_if_rx_eapol(source, buf, len);
	espwifi_fp_restore(&fp);
	return error;
}

void
espwifi_os_link(int up, const unsigned char *bssid)
{
	struct espwifi_fp fp;

	espwifi_fp_save(&fp);
	espwifi_if_link(up, bssid);
	espwifi_fp_restore(&fp);
}

int
espwifi_os_assoc_response(unsigned int subtype, const void *body,
    unsigned int len, const unsigned char *sender)
{
	struct espwifi_fp fp;
	int accepted;

	espwifi_fp_save(&fp);
	accepted = espwifi_if_assoc_response(subtype, body, len, sender);
	espwifi_fp_restore(&fp);
	return accepted;
}

int
espwifi_os_wpa_in_4way(void)
{
	struct espwifi_fp fp;
	int active;

	espwifi_fp_save(&fp);
	active = espwifi_if_wpa_in_4way() ? 1 : 0;
	espwifi_fp_restore(&fp);
	return active;
}

int
espwifi_os_sae_build(const unsigned char *bssid, unsigned int type,
	    unsigned char *body, unsigned int capacity, unsigned int *len)
{
#ifdef ESPWIFI_NET80211_WPA2
	struct espwifi_fp fp;
	int error;

	if (espwifi_os_in_isr())
		return EWOULDBLOCK;
	espwifi_fp_save(&fp);
	error = espwifi_if_sae_build(bssid, type, body, capacity, len);
	espwifi_fp_restore(&fp);
	return error;
#else
	(void)bssid;
	(void)type;
	(void)body;
	(void)capacity;
	(void)len;
	return EOPNOTSUPP;
#endif
}

int
espwifi_os_sae_parse(const unsigned char *body, unsigned int len,
	    unsigned int type, unsigned int status)
{
#ifdef ESPWIFI_NET80211_WPA2
	struct espwifi_fp fp;
	int error;

	if (espwifi_os_in_isr())
		return EWOULDBLOCK;
	espwifi_fp_save(&fp);
	error = espwifi_if_sae_parse(body, len, type, status);
	espwifi_fp_restore(&fp);
	return error;
#else
	(void)body;
	(void)len;
	(void)type;
	(void)status;
	return EOPNOTSUPP;
#endif
}

void
espwifi_os_radio_started(void)
{
	struct espwifi_fp fp;

	espwifi_fp_save(&fp);
	espwifi_if_radio_started();
	espwifi_fp_restore(&fp);
}

void
espwifi_os_radio_stopped(void)
{
	struct espwifi_fp fp;

	espwifi_fp_save(&fp);
	espwifi_if_radio_stopped();
	espwifi_fp_restore(&fp);
}

/*
 * Start: the pool, the timer thread and the thread that brings the radio
 * up once the system runs.
 */

/*
 * Join the first network of the list in /chosen that is on the air.  The
 * list holds name and key in turn, best network first.
 */
#ifndef ESPWIFI_NET80211
static bool
espwifi_join(void)
{
	const int chosen = OF_finddevice("/chosen");
	int len;
	const char *list = chosen < 0 ? NULL :
	    fdtbus_get_prop(chosen, "netbsd,wifi-networks", &len);

	if (espwifi_scan() <= 0 || list == NULL)
		return false;
	for (const char *p = list; p < list + len; ) {
		const char * const ssid = p;
		p += strlen(p) + 1;
		if (p >= list + len)
			break;
		const char * const key = p;
		p += strlen(p) + 1;

		if (!espwifi_seen(ssid))
			continue;
		printf("espwifi: joining %s\n", ssid);
		if (espwifi_connect(ssid, key) == 0) {
			printf("espwifi: joined %s\n", ssid);
			return true;
		}
		printf("espwifi: could not join %s\n", ssid);
	}
	return false;
}
#endif

static void
espwifi_main(void *v)
{
	/* OSI random callbacks can run in interrupt context and cannot fail. */
	if (cprng_fast_reseed_sync() != 0) {
		printf("espwifi: waiting for entropy\n");
		do {
			espwifi_os_task_delay(1000 / ESPWIFI_TICK_MS);
		} while (cprng_fast_reseed_sync() != 0);
	}
#ifdef ESPWIFI_NET80211_WPA2
	if (espwifi_start_wpa2() != 0)
#else
	if (espwifi_start() != 0)
#endif
		return;
	espwifi_if_radio_ready();
#ifdef ESPWIFI_NET80211
	return;
#else
	/*
	 * Join, and join again whenever the link is lost.  The pause grows
	 * with every failure: access points stop answering a station that
	 * knocks every few seconds.
	 */
	for (u_int pause = 3;;) {
		if (espwifi_linked()) {
			pause = 3;
		} else if (!espwifi_join()) {
			pause = MIN(pause * 2, 60);
		}
		espwifi_os_task_delay(pause * 1000 / ESPWIFI_TICK_MS);
	}
#endif
}

void
espwifi_attach(void)
{
	void *handle;

	mutex_init(&espwifi_mtx, MUTEX_DEFAULT, IPL_HIGH);
	cv_init(&espwifi_timer_cv, "espwtmr");
	espwifi_pool = vmem_create("espwifi", ESPWIFI_POOL_BASE,
	    ESPWIFI_POOL_SIZE, 16, NULL, NULL, NULL, 0, VM_NOSLEEP, IPL_HIGH);
	espwifi_pool_free = ESPWIFI_POOL_SIZE;

	if (!espwifi_if_attach()) {
		printf("espwifi: interface initialization failed\n");
		return;
	}
	if (!espwifi_os_task_create(espwifi_timer_thread, "timer", 0, NULL,
	    &handle) ||
	    !espwifi_os_task_create(espwifi_main, "main", 0, NULL, &handle))
		printf("espwifi: cannot create threads\n");
}
