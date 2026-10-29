/*
 * Run the production polled admin path of sys/dev/ic/nvme.c (nvme_poll,
 * nvme_poll_fill, nvme_poll_done, nvme_q_complete and struct
 * nvme_poll_state) against a fake controller and queue.  The completion
 * interrupt may finish a polled command on another CPU while nvme_poll()
 * spins: as soon as the phase bit is in its state, nvme_poll() returns and
 * its stack, where the state lives, is reused.  The fake publishes that
 * moment, poisons the state as the reused stack would and checks that
 * nothing touches it any more; it also times a command out while another
 * CPU has claimed its completion.  The shell driver ember/tests/nvme-poll.sh
 * extracts the functions.  AI-assisted (Claude Code, Claude Opus 5.5).
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __BIT(n)		(1u << (n))
#define __BITS(lo, hi)		((~0u >> (31 - (hi))) & ~((1u << (lo)) - 1))
#define __packed		__attribute__((__packed__))
#define __aligned(x)		__attribute__((__aligned__(x)))
#include "nvme-poll-cqe.h"

#define htole16(x)		(x)
#define lemtoh16(p)		(*(p))
#define ISSET(t, f)		(((t) & (f)) != 0)
#define CLR(t, f)		((t) &= ~(f))
#define SET(t, f)		fake_set16(&(t), (f))
#define atomic_load_relaxed(p)	(*(volatile uint16_t *)(p))
#define atomic_store_relaxed(p, v) fake_store16((p), (v))
#define NVME_DMA_KVA(m)		((m)->kva)
#define BUS_DMASYNC_PREREAD	1
#define BUS_DMASYNC_POSTREAD	2

#define QENTRIES	4
struct nvme_queue;
struct nvme_softc;
struct nvme_sqe { uint8_t opcode; uint8_t rest[63]; };
struct nvme_ccb {
	void		*ccb_cookie;
	void		(*ccb_done)(struct nvme_queue *, struct nvme_ccb *,
			    struct nvme_cqe *);
};
struct nvme_dmamem { struct nvme_cqe *kva; };
struct nvme_queue {
	int		q_cq_mtx;
	struct nvme_dmamem *q_cq_dmamem;
	uint32_t	q_cq_head;
	uint16_t	q_cq_phase;
	uint32_t	q_entries;
	uint32_t	q_cqhdbl;
	struct nvme_ccb	*q_ccbs;
};
struct nvme_ops {
	void		(*op_cq_done)(struct nvme_softc *, struct nvme_queue *,
			    struct nvme_ccb *);
};
struct nvme_softc {
	const char	*sc_dev;
	const struct nvme_ops *sc_ops;
};

#include "nvme-poll-state.h"

static void nvme_empty_done(struct nvme_queue *, struct nvme_ccb *,
    struct nvme_cqe *);
static void nvme_poll_fill(struct nvme_queue *, struct nvme_ccb *, void *);
static void nvme_poll_done(struct nvme_queue *, struct nvme_ccb *,
    struct nvme_cqe *);
static int nvme_q_complete(struct nvme_softc *, struct nvme_queue *);

static int failures;
#define CHECK(cond, what) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, what); \
		failures++;						\
	}								\
} while (0)

/* What the fake watches. */
static struct {
	struct nvme_queue *q;
	struct nvme_ccb	*ccb;
	void		*cookie;	/* the caller's ccb_cookie */
	struct nvme_poll_state *state;	/* the poller's, while it waits */
	uint16_t	*phase;		/* &state->c.flags */
	bool		poison;		/* poller returns as soon as it sees phase */
	unsigned char	poisoned[sizeof(struct nvme_poll_state)];
	int		done_calls;
	struct nvme_cqe	done_cqe;
	int		published;
	bool		released_after_done;
	bool		acquired_after_publish;
	int		released, acquired;
	int		timed_out_msgs;
	int		delays;
	int		post_at;	/* controller posts the completion */
	bool		irq_at_post;	/* ...and the interrupt handles it */
	int		claim_at;	/* another CPU claims it, unfinished */
	int		finish_at;	/* ...and finishes it */
	bool		claimed_seen;
	int		doorbells;
	struct nvme_cqe	cqe;
	struct nvme_cqe	ring[QENTRIES];
	struct nvme_dmamem cqmem;
	struct nvme_ccb	ccbs[QENTRIES];
} f;

static void
caller_done(struct nvme_queue *q, struct nvme_ccb *ccb, struct nvme_cqe *cqe)
{
	CHECK(ccb == f.ccb, "callback with another ccb");
	CHECK(ccb->ccb_cookie == f.cookie, "caller's cookie not restored for the callback");
	CHECK(f.published == 0, "callback after the poller saw the phase bit");
	CHECK(q->q_cq_mtx == 0, "callback under the completion lock");
	f.done_calls++;
	f.done_cqe = *cqe;
}

/* The poller's reused stack: a callback pointer there is garbage. */
static void
trap_done(struct nvme_queue *q, struct nvme_ccb *ccb, struct nvme_cqe *cqe)
{
	(void)q; (void)ccb; (void)cqe;
	CHECK(0, "poller's state used after it returned");
}

static void
published(void)
{
	f.published++;
	CHECK(f.done_calls == 1, "phase bit published before the callback ran");
	CHECK(f.ccb->ccb_cookie == f.cookie, "phase bit published before the cookie was restored");
	if (f.poison) {
		/* nvme_poll() returned; its stack now holds anything. */
		memset(f.state, 0xa5, sizeof(*f.state));
		f.state->done = trap_done;
		f.state->cookie = (void *)0xdeadbeef;
		memcpy(f.poisoned, f.state, sizeof(*f.state));
	}
}

static void
fake_set16(uint16_t *p, uint16_t v)
{
	*p |= v;
	if (p == f.phase && ISSET(v, NVME_CQE_PHASE))
		published();
}

static void
fake_store16(uint16_t *p, uint16_t v)
{
	*p = v;
	if (p == f.phase && ISSET(v, NVME_CQE_PHASE)) {
		CHECK(f.released_after_done,
		    "phase bit published without membar_release() after the callback");
		published();
	}
}

static void
membar_release(void)
{
	f.released++;
	f.released_after_done = f.done_calls == 1;
}

static void
membar_acquire(void)
{
	f.acquired++;
	f.acquired_after_publish = f.published > 0;
}

static void
aprint_error_dev(const char *dev, const char *msg)
{
	(void)dev; (void)msg;
	f.timed_out_msgs++;
}

static void
mutex_enter(int *m)
{
	CHECK(*m == 0, "queue mutex entered twice");
	*m = 1;
}

static void
mutex_exit(int *m)
{
	CHECK(*m == 1, "queue mutex released unheld");
	*m = 0;
}

static void
nvme_dmamem_sync(struct nvme_softc *sc_, struct nvme_dmamem *m, int op)
{
	(void)sc_; (void)m; (void)op;
}

static void
nvme_write4(struct nvme_softc *sc_, uint32_t reg, uint32_t v)
{
	(void)sc_; (void)reg; (void)v;
	f.doorbells++;
}

static void
fake_cq_done(struct nvme_softc *sc_, struct nvme_queue *q, struct nvme_ccb *ccb)
{
	(void)sc_; (void)q; (void)ccb;
}

static const struct nvme_ops fake_ops = { fake_cq_done };
static struct nvme_softc sc = { "nvme0", &fake_ops };

static void
nvme_empty_done(struct nvme_queue *q, struct nvme_ccb *ccb, struct nvme_cqe *cqe)
{
	(void)q; (void)ccb; (void)cqe;
}

static void
nvme_q_submit(struct nvme_softc *sc_, struct nvme_queue *q, struct nvme_ccb *ccb,
    void (*fill)(struct nvme_queue *, struct nvme_ccb *, void *))
{
	struct nvme_sqe slot;

	(void)sc_;
	memset(&slot, 0, sizeof(slot));
	(*fill)(q, ccb, &slot);
	CHECK(slot.opcode == 0x42, "submitted entry is not the caller's");
	f.state = ccb->ccb_cookie;
	f.phase = &f.state->c.flags;
}

/* The controller writes the completion into the ring. */
static void
post(void)
{
	struct nvme_cqe *e = &f.ring[f.q->q_cq_head];

	*e = f.cqe;
	e->cid = (uint16_t)(f.ccb - f.q->q_ccbs);
	e->flags = (uint16_t)((e->flags & ~NVME_CQE_PHASE) | f.q->q_cq_phase);
}

/* Time passes in the poller; the scenario acts on other CPUs meanwhile. */
static void
delay(int us)
{
	(void)us;
	f.delays++;
	if (f.delays == f.post_at) {
		post();
		if (f.irq_at_post) {
			struct nvme_poll_state *st = f.state;

			/* The interrupt on another CPU completes it. */
			CHECK(nvme_q_complete(&sc, f.q) == 1, "interrupt completes the entry");
			f.claimed_seen = st->claimed;
		}
	}
	if (f.delays == f.claim_at)
		f.state->claimed = true;	/* another CPU took it */
	if (f.delays == f.finish_at)
		nvme_poll_done(f.q, f.ccb, &f.cqe);
}

static void
caller_fill(struct nvme_queue *q, struct nvme_ccb *ccb, void *slot)
{
	struct nvme_sqe *sqe = slot;

	(void)q; (void)ccb;
	sqe->opcode = 0x42;
}

#include "nvme-poll.h"

static struct nvme_queue *
reset(void)
{
	static struct nvme_queue q;

	memset(&f, 0, sizeof(f));
	memset(&q, 0, sizeof(q));
	f.cqmem.kva = f.ring;
	q.q_cq_dmamem = &f.cqmem;
	q.q_cq_phase = NVME_CQE_PHASE;
	q.q_entries = QENTRIES;
	q.q_ccbs = f.ccbs;
	f.q = &q;
	f.ccb = &f.ccbs[2];
	f.cookie = &f;
	f.ccb->ccb_cookie = f.cookie;
	f.ccb->ccb_done = caller_done;
	f.cqe.cdw0 = 0x12345678;
	f.cqe.sqhd = 3;
	f.cqe.flags = htole16(0x0004);	/* a status, phase clear */
	return &q;
}

/* nvme_poll_done() run by the interrupt while the poller spins elsewhere. */
static void
check_done_on_another_cpu(void)
{
	struct nvme_poll_state *st = calloc(1, sizeof(*st));
	struct nvme_queue *q = reset();

	f.poison = true;
	f.state = st;
	f.phase = &st->c.flags;
	st->cookie = f.cookie;
	st->done = caller_done;
	f.ccb->ccb_cookie = st;
	f.ccb->ccb_done = nvme_poll_done;

	nvme_poll_done(q, f.ccb, &f.cqe);

	CHECK(f.published == 1, "completion published once");
	CHECK(memcmp(st, f.poisoned, sizeof(*st)) == 0, "poller's state written after it returned");
	CHECK(f.done_calls == 1, "callback runs once");
	CHECK(f.done_cqe.cdw0 == 0x12345678 && f.done_cqe.sqhd == 3,
	    "callback gets the completion entry");
	CHECK(f.done_cqe.flags == (0x0004 | NVME_CQE_PHASE), "callback sees the phase bit");
	CHECK(f.ccb->ccb_cookie == f.cookie, "caller's cookie restored");
	free(st);
}

static void
check_poll(const char *what, bool irq)
{
	struct nvme_queue *q = reset();
	int rv;

	f.post_at = 3;
	f.irq_at_post = irq;
	rv = nvme_poll(&sc, q, f.ccb, caller_fill, 1);
	CHECK(f.done_calls == 1, what);
	CHECK(rv == 0x0004, "status of the completion returned");
	CHECK(f.acquired == 1 && f.acquired_after_publish, "membar_acquire() after the phase bit");
	CHECK(f.ccb->ccb_cookie == f.cookie, "caller's cookie restored");
	CHECK(f.timed_out_msgs == 0, "no timeout");
	CHECK(q->q_cq_head == 1 && f.doorbells == 1, "entry consumed once");
	if (irq)
		CHECK(f.claimed_seen, "interrupt claims the poller's completion");
}

static void
check_poll_timeout(void)
{
	struct nvme_queue *q = reset();
	int rv;

	rv = nvme_poll(&sc, q, f.ccb, caller_fill, 1);
	CHECK(rv == 1, "timeout returns 1");
	CHECK(f.timed_out_msgs == 1, "timeout reported");
	CHECK(f.done_calls == 1, "callback cleans up once");
	CHECK(f.done_cqe.flags == 0, "cleanup gets an empty entry");
	CHECK(f.ccb->ccb_cookie == f.cookie, "caller's cookie restored");
	CHECK(f.ccb->ccb_done == nvme_empty_done, "late completion goes to a no-op");

	/* The command completes after all: nothing must run. */
	post();
	CHECK(nvme_q_complete(&sc, q) == 1, "late entry consumed");
	CHECK(f.done_calls == 1, "late completion runs nothing");
}

/* Timeout while the interrupt on another CPU is inside the callback. */
static void
check_poll_timeout_claimed(void)
{
	struct nvme_queue *q = reset();
	int rv;

	f.claim_at = 99999;		/* the poll loop gives up at 100000 */
	f.finish_at = 100005;
	rv = nvme_poll(&sc, q, f.ccb, caller_fill, 1);
	CHECK(rv == 0x0004, "claimed completion waited for and returned");
	CHECK(f.done_calls == 1, "callback runs once");
	CHECK(f.timed_out_msgs == 0, "no timeout for a claimed completion");
	CHECK(f.ccb->ccb_done == nvme_poll_done, "claimed completion keeps its handler");
	CHECK(f.ccb->ccb_cookie == f.cookie, "caller's cookie restored");
}

int
main(void)
{
	check_done_on_another_cpu();
	check_poll("poller completes the entry itself", false);
	check_poll("interrupt completes the entry on another CPU", true);
	check_poll_timeout();
	check_poll_timeout_claimed();
	if (failures != 0) {
		fprintf(stderr, "nvme poll: %d failures\n", failures);
		return 1;
	}
	printf("nvme poll: completion published last, claimed completion waited for, late one inert\n");
	return 0;
}
