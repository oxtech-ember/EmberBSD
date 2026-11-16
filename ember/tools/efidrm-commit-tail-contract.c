/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Origin: EmberBSD, AI-assisted.
 *
 * Contract for the nonblocking commits of efidrm on the DRM 5.6 core.
 *
 * efidrm-commit-tail-contract.py extracts the actual commit_tail(),
 * drm_atomic_helper_commit_tail(), efidrm_crtc_atomic_flush(),
 * efidrm_atomic_commit() and the atomic_commit_tail hook of efidrm, if any.
 * This file supplies the rest as a model with a virtual clock:
 *
 *  - NetBSD runs system_unbound_wq on one thread (alloc_ordered_workqueue in
 *    linux_work.c), so the work of a nonblocking commit starts only after
 *    the work of the previous commit returns;
 *  - drm_atomic_helper_setup_commit() (stall_checks) gives EBUSY to a
 *    nonblocking commit while flip_done of the last commit is not complete;
 *    it waits for cleanup_done of the commit before the last one;
 *  - drm_atomic_helper_swap_state(stall=true) waits for hw_done of the last
 *    commit; commit_tail() waits for its hw_done and flip_done;
 *  - the efidrm vblank ticks with a fixed period; wait_for_vblanks waits for
 *    the next tick; the plane update copies for COPY_NS.
 *
 * Requirement: with the default event mode (evmode 1, the event goes out
 * right after the copy), a nonblocking flip that comes at least one copy
 * time after the previous commit must not get EBUSY.  The KMS client does
 * exactly this after 300 opens of the node ("and flips, with events").
 *
 * The output is one "ok"/"FAIL" line per check; exit status 0 if all pass.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int64_t s64;
typedef int64_t ktime_t;
#define BIT(n)		(1u << (n))

#define MS		1000000LL
#define PERIOD_NS	(1000000000LL / 60)	/* efidrm at hz >= 600 */
#define COPY_NS		(MS * 3 / 10)
#define CHURN_NS	(MS * 104 / 10)		/* median of test_churn in the VM */
#define POOL		16
#define EVQ		16

static int nchecks, nfail;

#define CHECK(cond, ...) do {						\
	nchecks++;							\
	if (cond) {							\
		printf("ok   ");					\
	} else {							\
		printf("FAIL ");					\
		nfail++;						\
	}								\
	printf(__VA_ARGS__);						\
	printf("\n");							\
} while (0)

/* An invariant that is checked in every commit; only failures print. */
static int ninv_fail;
#define INVARIANT(cond, what) do {					\
	if (!(cond)) {							\
		if (ninv_fail++ < 10)					\
			printf("FAIL invariant: %s\n", what);		\
	}								\
} while (0)

/* ------------------------------------------------ minimal DRM objects */

struct mcommit;

struct drm_pending_event {
	struct mcommit	*completion;	/* flip_done of this commit */
	bool		user;		/* the client asked for the event */
	uint64_t	user_data;
	int		sent;
};

struct drm_pending_vblank_event {
	struct drm_pending_event base;
	unsigned int	pipe;
	struct {
		struct {
			uint32_t sequence, tv_sec, tv_usec;
		} vbl;
	} event;
};

struct drm_crtc_state {
	bool		active;
	bool		no_vblank;
	bool		self_refresh_active;
	struct drm_pending_vblank_event *event;
};

struct drm_atomic_state;

struct drm_mode_config_helper_funcs {
	void (*atomic_commit_tail)(struct drm_atomic_state *);
};

struct drm_device {
	void		*dev_private;
	int		event_lock;
	struct {
		const struct drm_mode_config_helper_funcs *helper_private;
	} mode_config;
};

struct drm_crtc {
	struct drm_device	*dev;
	struct drm_crtc_state	*state;
};

struct mcommit {
	int64_t		submitted;
	int64_t		flip_done;	/* time; valid if flip_set */
	int64_t		hw_done;	/* time; valid if hw_set */
	int64_t		cleanup_done;	/* time; valid if cleanup_set */
	bool		flip_set, hw_set, cleanup_set;
	bool		planes, cleaned;
	struct drm_pending_vblank_event ev;
};

struct drm_atomic_state {
	struct drm_device	*dev;
	struct drm_crtc		*crtc;
	struct drm_crtc_state	new_crtc_state;
	struct mcommit		c;
	struct mcommit		*old_commit;
};

/* commit_tail() sets crtc and does not read it: (void) marks it used. */
#define for_each_new_crtc_in_state(state, crtc_, ncs, i)		\
	for ((i) = 0; (i) < 1 &&					\
	    ((crtc_) = (state)->crtc, (void)(crtc_),			\
	    (ncs) = &(state)->new_crtc_state, 1);			\
	    (i)++)

struct efidrm_softc {
	uint32_t	sc_evseq;
};

static struct {
	int	shadow, evmode, sync;
} efidrm_tun;

static struct {
	uint64_t ev_immediate, ev_tick;
} efidrm_cnt;

#define spin_lock_irqsave(lock, flags)		((void)(lock), (flags) = 0)
#define spin_unlock_irqrestore(lock, flags)	((void)(lock), (void)(flags))

/* -------------------------------------------------------------- model */

static int64_t phase;		/* time of the first vblank tick */
static int64_t vnow;		/* clock of the thread that runs commit_tail */
static int64_t tnow;		/* clock of the client */
static int64_t worker_free;	/* the ordered work queue is free from here */
static struct mcommit *last, *before_last;
static struct drm_atomic_state pool[POOL];
static int npool;
static int nwaits, ncommits;

static struct { int64_t t; uint64_t data; } evq[EVQ];
static int nev;

static struct efidrm_softc softc;
static struct drm_device drmdev = { .dev_private = &softc };
static struct drm_crtc crtc = { .dev = &drmdev };

/* First vblank tick strictly after t. */
static int64_t
next_tick(int64_t t)
{
	if (t < phase)
		return phase;
	return phase + ((t - phase) / PERIOD_NS + 1) * PERIOD_NS;
}

static void
deliver(struct drm_pending_event *e, int64_t t)
{
	struct mcommit *c = e->completion;

	INVARIANT(e->sent == 0, "every event goes out once");
	e->sent++;
	c->flip_done = t;
	c->flip_set = true;
	if (e->user && nev < EVQ) {
		evq[nev].t = t;
		evq[nev].data = e->user_data;
		nev++;
	}
}

static uint64_t
efidrm_now_ns(void)
{
	return (uint64_t)vnow;
}

static unsigned int
drm_crtc_index(struct drm_crtc *c)
{
	(void)c;
	return 0;
}

static void
drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e)
{
	(void)dev;
	deliver(e, vnow);
}

static int
drm_crtc_vblank_get(struct drm_crtc *c)
{
	return c->state->active ? 0 : -EINVAL;
}

static void
drm_crtc_arm_vblank_event(struct drm_crtc *c, struct drm_pending_vblank_event *e)
{
	(void)c;
	deliver(&e->base, next_tick(vnow));
}

static void
drm_crtc_send_vblank_event(struct drm_crtc *c, struct drm_pending_vblank_event *e)
{
	(void)c;
	deliver(&e->base, vnow);
}

static void efidrm_crtc_atomic_flush(struct drm_crtc *, struct drm_crtc_state *);

static void
drm_atomic_helper_commit_modeset_disables(struct drm_device *dev,
    struct drm_atomic_state *s)
{
	INVARIANT(!s->c.planes, "modeset disables before the planes");
}

static void
drm_atomic_helper_commit_planes(struct drm_device *dev,
    struct drm_atomic_state *s, uint32_t flags)
{
	vnow += COPY_NS;		/* efidrm_plane_atomic_update copies */
	s->c.planes = true;
	efidrm_crtc_atomic_flush(s->crtc, NULL);
}

static void
drm_atomic_helper_commit_modeset_enables(struct drm_device *dev,
    struct drm_atomic_state *s)
{
	INVARIANT(s->c.planes, "modeset enables after the planes");
}

static void
drm_atomic_helper_fake_vblank(struct drm_atomic_state *s)
{
	if (s->new_crtc_state.no_vblank && s->new_crtc_state.event) {
		drm_crtc_send_vblank_event(s->crtc, s->new_crtc_state.event);
		s->new_crtc_state.event = NULL;
	}
}

static void
drm_atomic_helper_commit_hw_done(struct drm_atomic_state *s)
{
	INVARIANT(s->new_crtc_state.event == NULL,
	    "the event is consumed before hw_done");
	INVARIANT(s->c.planes, "the planes are committed before hw_done");
	s->c.hw_done = vnow;
	s->c.hw_set = true;
}

static void
drm_atomic_helper_wait_for_vblanks(struct drm_device *dev,
    struct drm_atomic_state *s)
{
	INVARIANT(s->c.hw_set, "the vblank wait comes after hw_done");
	nwaits++;
	if (s->new_crtc_state.active)
		vnow = next_tick(vnow);
}

static void
drm_atomic_helper_cleanup_planes(struct drm_device *dev,
    struct drm_atomic_state *s)
{
	INVARIANT(s->c.hw_set, "the planes are cleaned up after hw_done");
	s->c.cleaned = true;
}

static void
drm_atomic_helper_wait_for_fences(struct drm_device *dev,
    struct drm_atomic_state *s, bool pre_swap)
{
}

static void
drm_atomic_helper_wait_for_dependencies(struct drm_atomic_state *s)
{
	struct mcommit *o = s->old_commit;

	if (o == NULL)
		return;
	if (vnow < o->hw_done)
		vnow = o->hw_done;
	if (vnow < o->flip_done)
		vnow = o->flip_done;
}

static ktime_t
ktime_get(void)
{
	return vnow;
}

static s64
ktime_ms_delta(ktime_t later, ktime_t earlier)
{
	return (later - earlier) / MS;
}

static void
drm_self_refresh_helper_update_avg_times(struct drm_atomic_state *s,
    unsigned long commit_time_ms, unsigned int mask)
{
}

static void
drm_atomic_helper_commit_cleanup_done(struct drm_atomic_state *s)
{
	INVARIANT(s->c.cleaned, "cleanup_done after the planes are cleaned up");
	INVARIANT(s->c.flip_set && s->c.hw_set,
	    "flip_done and hw_done are set before cleanup_done");
	s->c.cleanup_done = vnow;
	s->c.cleanup_set = true;
}

static void
drm_atomic_state_put(struct drm_atomic_state *s)
{
}

static void commit_tail(struct drm_atomic_state *);

/*
 * drm_atomic_helper_commit() of the 5.6 core, reduced to the parts that
 * order the commits.  The work of a nonblocking commit runs here at once
 * in virtual time, from the moment the ordered queue is free.
 */
static int
drm_atomic_helper_commit(struct drm_device *dev, struct drm_atomic_state *s,
    bool nonblock)
{
	/* drm_atomic_helper_setup_commit(), stall_checks() */
	if (nonblock && last != NULL && last->flip_done > tnow)
		return -EBUSY;
	if (before_last != NULL && tnow < before_last->cleanup_done)
		tnow = before_last->cleanup_done;
	/* drm_atomic_helper_swap_state(state, true) */
	if (last != NULL && tnow < last->hw_done)
		tnow = last->hw_done;
	s->old_commit = last;
	before_last = last;
	last = &s->c;
	s->c.submitted = tnow;
	crtc.state = &s->new_crtc_state;
	ncommits++;
	if (nonblock) {
		vnow = tnow > worker_free ? tnow : worker_free;
		commit_tail(s);
		worker_free = vnow;
	} else {
		vnow = tnow;
		commit_tail(s);
		tnow = vnow;
	}
	INVARIANT(s->c.cleanup_set, "every commit reaches cleanup_done");
	INVARIANT(s->c.flip_done >= s->c.submitted, "flip_done after the submit");
	return 0;
}

#include "efidrm-commit-tail-extract.c"

/* ------------------------------------------------------------- client */

static void
reset(int64_t ph)
{
	memset(pool, 0, sizeof pool);
	npool = 0;
	nev = 0;
	phase = ph;
	vnow = tnow = worker_free = 0;
	last = before_last = NULL;
	crtc.state = NULL;
	model_set_helpers(&drmdev);
}

/* DRM_IOCTL_MODE_PAGE_FLIP: drm_atomic_helper_page_flip, nonblocking. */
static int
flip(bool with_event, uint64_t data)
{
	struct drm_atomic_state *s;
	int r;

	if (npool >= POOL)
		return -ENOMEM;
	s = &pool[npool];
	memset(s, 0, sizeof *s);
	s->dev = &drmdev;
	s->crtc = &crtc;
	s->new_crtc_state.active = true;
	s->c.ev.base.completion = &s->c;
	s->c.ev.base.user = with_event;
	s->c.ev.base.user_data = data;
	s->new_crtc_state.event = &s->c.ev;
	r = efidrm_atomic_commit(&drmdev, s, true);
	if (r == 0)
		npool++;
	return r;
}

/* read(2) of the DRM descriptor: wait for the earliest event. */
static int
read_event(uint64_t *data)
{
	int i, k = 0;

	if (nev == 0)
		return -ETIMEDOUT;
	for (i = 1; i < nev; i++)
		if (evq[i].t < evq[k].t)
			k = i;
	if (tnow < evq[k].t)
		tnow = evq[k].t;
	*data = evq[k].data;
	evq[k] = evq[--nev];
	return 0;
}

/*
 * The probe of task 074: a flip with an event, read it, a flip without an
 * event, pause, then a flip with an event.  Returns the number of EBUSY.
 */
static int
probe(int64_t ph, int64_t pause)
{
	uint64_t d;
	int r, busy = 0;

	reset(ph);
	r = flip(true, 1);
	INVARIANT(r == 0 && read_event(&d) == 0 && d == 1,
	    "the first flip and its event");
	r = flip(false, 0);
	INVARIANT(r == 0, "the flip without an event");
	tnow += pause;
	r = flip(true, 2);
	if (r == -EBUSY)
		busy++;
	else
		INVARIANT(r == 0 && read_event(&d) == 0 && d == 2,
		    "the last flip and its event");
	return busy;
}

/* The tail of test_churn in ember/tests/drm2-kms-client.c. */
static int
client_churn(int64_t ph)
{
	uint64_t d;
	int r;

	reset(ph);
	r = flip(true, 1);
	INVARIANT(r == 0 && read_event(&d) == 0, "the flip before the churn");
	r = flip(false, 0);
	INVARIANT(r == 0, "the flip without an event before the churn");
	tnow += CHURN_NS;
	if (flip(true, 21) != 0 || read_event(&d) != 0)
		return 1;
	if (flip(true, 22) != 0 || read_event(&d) != 0 || d != 22)
		return 1;
	return 0;
}

struct sweep {
	int	trials, busy;
	int64_t	busy_max_pause;
};

static struct sweep
sweep_probe(int64_t from)
{
	struct sweep sw = { 0, 0, -1 };
	int64_t ph, pause;

	for (ph = 0; ph < PERIOD_NS; ph += MS / 4)
		for (pause = from; pause <= 20 * MS; pause += MS / 2) {
			sw.trials++;
			if (probe(ph, pause)) {
				sw.busy++;
				if (pause > sw.busy_max_pause)
					sw.busy_max_pause = pause;
			}
		}
	return sw;
}

static struct sweep
sweep_client(void)
{
	struct sweep sw = { 0, 0, -1 };
	int64_t ph;

	for (ph = 0; ph < PERIOD_NS; ph += MS / 4) {
		sw.trials++;
		sw.busy += client_churn(ph);
	}
	return sw;
}

static void
set_mode(int evmode, int sync)
{
	efidrm_tun.evmode = evmode;
	efidrm_tun.sync = sync;
	nwaits = ncommits = 0;
}

int
main(void)
{
	struct sweep sw;

	reset(0);
	printf("note: efidrm sets %s atomic_commit_tail\n",
	    drmdev.mode_config.helper_private != NULL &&
	    drmdev.mode_config.helper_private->atomic_commit_tail != NULL ?
	    "its own" : "no");

	/* The default of efidrm: event after the copy, nonblocking commits. */
	set_mode(1, 0);
	sw = sweep_probe(2 * COPY_NS);
	printf("note: evmode 1: probe EBUSY %d of %d", sw.busy, sw.trials);
	if (sw.busy > 0)
		printf(", longest pause with EBUSY %.1f ms",
		    sw.busy_max_pause / 1e6);
	printf("; vblank waits %d in %d commits\n", nwaits, ncommits);
	CHECK(sw.busy == 0, "evmode 1: no EBUSY for a flip %.1f ms or more "
	    "after a flip without an event", 2 * COPY_NS / 1e6);
	sw = sweep_client();
	printf("note: evmode 1: client churn EBUSY %d of %d\n", sw.busy, sw.trials);
	CHECK(sw.busy == 0,
	    "evmode 1: \"and flips, with events\" after the churn, no EBUSY");

	/* sync=1: every commit blocks; the VM showed 0 failures in this mode. */
	set_mode(1, 1);
	sw = sweep_probe(0);
	CHECK(sw.busy == 0, "evmode 1, sync 1: blocking commits get no EBUSY");
	sw = sweep_client();
	CHECK(sw.busy == 0, "evmode 1, sync 1: the client sequence passes");

	/*
	 * evmode 0: the event comes on the vblank tick.  A flip before the
	 * tick of the previous flip gets EBUSY, as on hardware, so the counts
	 * are notes; the behaviour of this mode stays as it was.
	 */
	set_mode(0, 0);
	sw = sweep_probe(0);
	printf("note: evmode 0: probe EBUSY %d of %d\n", sw.busy, sw.trials);
	CHECK(ncommits > 0 && nwaits == ncommits,
	    "evmode 0: the commit tail waits for a vblank in every commit "
	    "(%d of %d)", nwaits, ncommits);

	CHECK(ninv_fail == 0, "commit order invariants hold in every commit "
	    "(%d failures)", ninv_fail);
	printf("%d checks, %d failed\n", nchecks, nfail);
	return nfail == 0 ? 0 : 1;
}
