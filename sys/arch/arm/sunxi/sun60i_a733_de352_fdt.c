/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD, AI-assisted. Native DE3.52 single-plane consumer.
 * Register facts: BSP 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f and A733
 * User Manual v1.00. No GPL implementation is imported. See the native DE
 * contract. Attach does not read display MMIO or start a device.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/kmem.h>
#include <sys/kthread.h>
#include <sys/container_of.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/intr.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/workqueue.h>
#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <arm/sunxi/sun60i_a733_ccu.h>
#include <arm/sunxi/sun60i_a733_pck600.h>
#include <arm/sunxi/sun60i_a733_display_owner.h>
#include <arm/sunxi/sun60i_a733_de352_dma.h>
#include <arm/sunxi/sun60i_a733_output.h>
#include <arm/sunxi/sun60i_a733_hdmi_phy.h>
#include <arm/sunxi/sun60i_a733_de352_scanout.h>

#define DE352_BASE 0x05000000
#define DE352_SIZE 0x400000
#define DE352_IOMMU1_BASE 0x03910000
#define DE352_IOMMU1_SIZE 0xe40
#define DE352_NSI_BASE 0x02020000
#define DE352_NSI_SIZE 0x420
#define DE352_TIMEOUT_NS UINT64_C(100000000)
/*
 * DE2TCON mux value of TCON_TV0. The vendor DRM driver programs the DE0
 * output endpoint ID. That ID is 4 for TV0 and 5 for TV1; ID 3 is unused.
 */
#define DE352_TV0_MUX 4

CTASSERT(A733_DISPLAY_NMEMORY_REGS == 7);

/* Complete raw samples. Valid bits describe capture, not DMA acceptance. */
struct de352_memory_snapshot {
	uint32_t version, valid;
	uint32_t clocks[2][A733_DISPLAY_NMEMORY_REGS], iommu[2][5], nsi[2][3];
};

enum {
	CI_VERSION, CI_RESET, CI_ENABLE, CI_AUTOGATE, CI_TTB, CI_PREFETCH,
	CI_FLUSH, CI_TLB_MODE, CI_TLB_REQUEST, CI_PTW_MODE, CI_PTW_REQUEST,
	CI_IRQ_MASK, CI_IRQ_STATUS, CI_MIC_BASE, CI_COUNT = CI_MIC_BASE + 6 * 4
};
struct de352_cold_state { uint32_t words[2][CI_COUNT]; };

/* Cached diagnostics only. These values do not establish hardware readiness. */
enum {
	CT_NONE, CT_DE_MAP, CT_COLD_CHECK, CT_PEER_ACCESS, CT_IOMMU_MAP,
	CT_CAPTURE, CT_ADMIT, CT_GUARD, CT_PUBLISH, CT_STEP_CHECK,
	CT_STEP_CAPTURE, CT_STEP_COMPARE, CT_DELAY, CT_BEFORE, CT_WRITE,
	CT_AFTER, CT_FINAL_CHECK, CT_FINAL_CAPTURE, CT_FINAL_COMPARE, CT_DONE
};
enum { CT_REG = 1, CT_BEFORE_VALID = 2, CT_REQUESTED = 4, CT_AFTER_VALID = 8 };
struct de352_cold_trace {
	uint32_t version, phase, op, valid, error;
	uint32_t reg, mask, before, requested, after;
	uint32_t capture_attempt, capture_generation, capture_words;
	uint32_t capture_valid, capture_reg;
	struct de352_cold_state snapshot;
};
CTASSERT(sizeof(struct de352_cold_trace) == (15 + 2 * CI_COUNT) * 4);

static void
de352_trace_phase(struct de352_cold_trace *trace, uint32_t phase)
{
	trace->phase = phase;
	trace->valid = 0;
	trace->reg = UINT32_MAX;
	trace->mask = trace->before = trace->requested = trace->after = 0;
}

enum { RT_NONE, RT_OUTPUT, RT_CONFIGURE, RT_MIXER_ENABLE, RT_DE_ENABLE, RT_DONE };
struct de352_run_trace {
	uint32_t version, valid, phase, result, width, height, pixel_clock_hz;
	uint32_t field_valid, reg, mask, before, requested, after;
};
CTASSERT(sizeof(struct de352_run_trace) == 13 * sizeof(uint32_t));

/* Cached step 8 diagnostics. The record adds reads only after a failure. */
enum {
	ST_NONE, ST_START, ST_MEMORY, ST_PUBLISH, ST_QUEUE, ST_ACK,
	ST_REQUEST, ST_POLL, ST_COPY, ST_DONE
};
enum { ST_NREGS = 10 };
struct de352_rcq_trace {
	uint32_t request_samples, request_busy, request_first, request_last;
	uint32_t request_control, request_us;
	uint32_t poll_samples, poll_last, poll_us;
	uint32_t status_or, status_and;
};
CTASSERT(sizeof(struct de352_rcq_trace) == 11 * sizeof(uint32_t));

struct de352_submit_trace {
	uint32_t version, valid, phase, result;
	struct de352_rcq_trace rcq;
	uint32_t regs_valid, regs[ST_NREGS];
	uint32_t sample_error;
	struct sun60i_a733_output_sample sample;
};
CTASSERT(sizeof(struct de352_submit_trace) == 37 * sizeof(uint32_t));

/* Cached last external update. A failed handoff keeps its exact readback. */
enum {
	UT_NONE, UT_PREPARE, UT_START, UT_READY, UT_SUBMIT,
	UT_VBLANK, UT_MEMORY, UT_STOP, UT_DONE
};
enum { UO_NONE, UO_FRAME, UO_DISABLE, UO_STOP };
enum { UW_NONE, UW_BASELINE, UW_WAIT };
enum { RCQ_COPY_WORDS = 8, RCQ_TIMEOUT_REGS = 13 };
struct de352_rcq_timeout {
	uint32_t attempted, valid;
	uint32_t old_queue, new_queue;
	uint32_t old_words[RCQ_COPY_WORDS], new_words[RCQ_COPY_WORDS];
	uint32_t observed[RCQ_TIMEOUT_REGS], reserved;
};
struct de352_tv0_timeout {
	uint64_t raw_before, raw_after;
	uint32_t attempted, before_error, sample_error, after_error;
	struct sun60i_a733_output_sample sample;
};

struct de352_update_trace {
	uint32_t version, valid, phase, submit_phase, result;
	uint32_t reg, expected, observed;
	uint32_t operation, wait_phase, wait_samples, wait_enoent;
	uint64_t initial_baseline, baseline, last_sequence, last_validated;
	uint64_t elapsed_ns;
	struct sun60i_a733_output_tv0_trace tv0;
	struct de352_rcq_trace rcq;
	uint32_t reserved;
	struct de352_rcq_timeout timeout;
	struct de352_tv0_timeout tv0_timeout;
};
CTASSERT(sizeof(struct de352_update_trace) == 96 * sizeof(uint32_t));
static const uint32_t de352_submit_regs[ST_NREGS] = {
	0x8100, 0x8104, 0x8108, 0x810c, 0x8110, 0x8114, 0x8118, 0x811c,
	0x8010, 0x804c
};

struct sun60i_a733_de352_client {
	const void *owner;
	bool busy, attempted;
};

struct de352_frame {
	struct sun60i_a733_de352_frame info;
	struct sun60i_a733_de352_dma *dma;
	bool held, shadow;
};

#define DE352_SHADOW_RETIRE_MAX 3
struct de352_shadow_retire {
	struct de352_frame frame;
	uint64_t sequence;
	unsigned edges;
};

/* Read-only observation of the online DE at the physical TV0 interrupt. */
#define DE352_EDGE_ROWS 128
struct de352_edge_record {
	uint64_t ticket, sequence, irq_ns, posted_ns, verified_ns;
	uint32_t first_status, last_status, matches, sample_age_ns;
};
struct de352_edge_history {
	uint64_t count;
	struct de352_edge_record row[DE352_EDGE_ROWS];
};
enum {
	POST_PHASE_NO_IRQ = 1, POST_PHASE_TV0_READ, POST_PHASE_DE_READ,
	POST_PHASE_IRQ_RACE, POST_PHASE_TV0_STATUS, POST_PHASE_ACTIVE,
	POST_PHASE_DE_SYNC_RELEASED, POST_PHASE_DE_STATUS, POST_PHASE_SAFE,
	POST_PHASE_FIELD_TRANSITION
};
struct de352_post_phase {
	uint64_t ticket, sequence, irq_ns, posted_ns, verified_ns, sampled_ns;
	struct sun60i_a733_tv0_phase before, after;
	uint32_t first, last, matches, reason;
};
struct de352_post_history {
	uint64_t count;
	struct de352_post_phase row[DE352_EDGE_ROWS];
};
CTASSERT(sizeof(struct de352_post_phase) == 96);

struct de352_early_row {
	uint64_t ticket, sequence, irq_ns, blank_ns;
	uint32_t attempts, first_debug, last_debug, first_gint, last_gint;
	uint32_t first_us, last_us, valid, no_phase, no_line, no_access, changed;
};
struct de352_early_history {
	uint64_t count;
	struct de352_early_row row[DE352_EDGE_ROWS];
};

struct de352_edge {
	kmutex_t lock;
	sun60i_a733_vblank_fn consumer;
	void *arg;
	struct workqueue *worker;
	struct work work;
	bool scheduled;
	uint32_t address, queue, previous_address;
	uint64_t previous_idle_sequence;
	uint64_t blank_ns;
	uint64_t ticket, posted_ns, verified_ns;
	bool armed, irq_readback;
	struct de352_edge_history history;
	struct de352_post_history post;
	struct de352_early_history early;
};

struct sun60i_de352_softc {
	struct de352_edge edge;
	bool field_probe, bsp_vblank;
	int verify_each_flip;
	device_t dev;
	int phandle, power_phandle, output_phandle;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	bus_dma_tag_t dmat;
	struct clk *de0;
	kmutex_t lock;
	struct sysctllog *sysctllog;
	struct sun60i_a733_de352_dma *dma;
	struct sun60i_a733_output_mode mode;
	struct de352_memory_snapshot memory;
	bus_space_handle_t memory_iommu, memory_nsi;
	bool memory_mapped;
	struct sun60i_a733_de352_guard *guard;
	struct de352_cold_state cold;
	struct de352_cold_trace cold_trace;
	struct de352_run_trace run_trace;
	struct de352_submit_trace submit_trace;
	struct de352_update_trace update_trace;
	bool cold_initialized;
	bool output_stopped;
	bool discovery_pending, discovery_cold, discovery_managed;
	bool valid, mapped, power_reserved, reserved, retained;
	int stage, step, result, release_error;
	uint32_t last_status;
	uint64_t vblank;
	struct sun60i_a733_de352_client client;
	struct de352_frame active, pending;
	struct de352_shadow_retire shadow_retire[DE352_SHADOW_RETIRE_MAX];
	unsigned shadow_retire_count;

};

static ONCE_DECL(de352_once);
static kmutex_t de352_registry_lock;
static struct sun60i_de352_softc *de352_provider;

static int
de352_registry_init(void)
{
	mutex_init(&de352_registry_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static struct sun60i_de352_softc *
de352_lookup(int phandle, const struct sun60i_a733_de352_client *client)
{
	struct sun60i_de352_softc *sc;

	RUN_ONCE(&de352_once, de352_registry_init);
	mutex_enter(&de352_registry_lock);
	sc = de352_provider;
	if (sc != NULL && (client != NULL ? client != &sc->client :
	    phandle != sc->phandle))
		sc = NULL;
	mutex_exit(&de352_registry_lock);
	/* This provider has no detach operation. Its lifetime is the whole boot. */
	return sc;
}

static const struct device_compatible_entry de352_compat[] = {
	{ .compat = "allwinner,display-engine-v352" }, DEVICE_COMPAT_EOL
};

static uint64_t de352_replacement_extra(const struct sun60i_a733_output_mode *);

static uint64_t
de352_now(void)
{
	struct timespec ts;
	nanouptime(&ts);
	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

/*
 * Bracket the DE reads with owner-checked scanline samples. Do not hold
 * edge.lock across provider calls (the TV0 IRQ nests intr_lock outside
 * edge.lock). The ticket and newest record must survive both samples.
 */
static struct de352_edge_record *
de352_edge_pending(struct de352_edge *e, uint64_t sequence)
{
	struct de352_edge_record *r;

	if (!e->armed || e->history.count == 0)
		return NULL;
	r = &e->history.row[(e->history.count - 1) % DE352_EDGE_ROWS];
	if (r->ticket != e->ticket || r->sequence != sequence ||
	    r->sample_age_ns != 0 || de352_now() - r->irq_ns >= e->blank_ns)
		return NULL;
	return r;
}

static void
de352_early_note(struct sun60i_de352_softc *sc, uint64_t sequence,
    const struct sun60i_a733_tv0_phase *phase, unsigned reason)
{
	struct de352_edge *e = &sc->edge;
	struct de352_early_row *r;
	uint64_t age;

	if (!sc->field_probe)
		return;
	mutex_spin_enter(&e->lock);
	if (!e->armed || e->early.count == 0)
		goto out;
	r = &e->early.row[(e->early.count - 1) % DE352_EDGE_ROWS];
	if (r->sequence != sequence || r->ticket != e->ticket)
		goto out;
	age = de352_now() - r->irq_ns;
	if (r->attempts++ == 0) {
		r->first_debug = phase->debug;
		r->first_gint = phase->gint0;
		r->first_us = age / 1000;
	}
	r->last_debug = phase->debug;
	r->last_gint = phase->gint0;
	r->last_us = age / 1000;
	switch (reason) {
	case 0: r->valid++; break;
	case 1: r->no_phase++; break;
	case 2: r->no_line++; break;
	case 3: r->no_access++; break;
	case 4: r->changed++; break;
	}
out:
	mutex_spin_exit(&e->lock);
}

struct de352_edge_sample {
	uint32_t first, queue, address, offline, last;
};

/* Sampling the online registers needs only the reserved and clocked DE. */
static bool
de352_sample_edge(struct sun60i_de352_softc *sc, struct de352_edge_sample *s)
{
	return bus_space_peek_4(sc->bst, sc->bsh, 0x8104, &s->first) == 0 &&
	    bus_space_peek_4(sc->bst, sc->bsh, 0x8114, &s->queue) == 0 &&
	    bus_space_peek_4(sc->bst, sc->bsh, 0x1c1010, &s->address) == 0 &&
	    bus_space_peek_4(sc->bst, sc->bsh, 0x80d0, &s->offline) == 0 &&
	    bus_space_peek_4(sc->bst, sc->bsh, 0x8104, &s->last) == 0;
}

static uint32_t
de352_edge_matches(const struct de352_edge_sample *s, uint32_t queue,
    uint32_t address)
{
	return (s->queue == queue ? 1U : 0U) |
	    (s->address == address ? 2U : 0U) |
	    ((s->offline & 1) == 0 ? 4U : 0U);
}

static void
de352_edge_measure(struct sun60i_de352_softc *sc, uint64_t sequence)
{
	struct de352_edge *e = &sc->edge;
	struct de352_edge_record *r;
	struct de352_edge_sample sample;
	uint32_t before, after;
	uint64_t now, ticket;
	bool valid;
	struct sun60i_a733_tv0_phase phase = {0};
	int phase_error = 0;

	if (sc->field_probe)
		phase_error = sun60i_a733_output_sample_tv0_phase(
		    sc->output_phandle, sc, sequence, &phase);
	if (sun60i_a733_output_sample_blank_line(sc->output_phandle, sc,
	    sequence, &before) != 0) {
		de352_early_note(sc, sequence, &phase, phase_error ? 1 : 2);
		return;
	}
	mutex_spin_enter(&e->lock);
	r = de352_edge_pending(e, sequence);
	ticket = e->ticket;
	valid = r != NULL && de352_sample_edge(sc, &sample);
	mutex_spin_exit(&e->lock);
	if (!valid || sun60i_a733_output_sample_blank_line(sc->output_phandle,
	    sc, sequence, &after) != 0 || after < before) {
		de352_early_note(sc, sequence, &phase, valid ? 4 : 3);
		return;
	}
	de352_early_note(sc, sequence, &phase, 0);
	mutex_spin_enter(&e->lock);
	r = de352_edge_pending(e, sequence);
	if (r != NULL && r->ticket == ticket) {
		now = de352_now();
		r->first_status = sample.first;
		r->last_status = sample.last;
		r->matches = de352_edge_matches(&sample, e->queue, e->address);
		r->sample_age_ns = (uint32_t)(now - r->irq_ns);
		if ((r->first_status & 0x14) == 0 && (r->last_status & 0x14) == 0 &&
		    r->matches == 5 && sample.address == e->previous_address)
			e->previous_idle_sequence = sequence;
	}
	mutex_spin_exit(&e->lock);
}

static void
de352_edge_work(struct work *work, void *cookie)
{
	struct sun60i_de352_softc *sc = cookie;
	struct de352_edge *e = &sc->edge;
	uint64_t sequence = 0;

	(void)work;
	mutex_spin_enter(&e->lock);
	e->scheduled = false;
	if (e->armed && e->history.count != 0)
		sequence = e->history.row[(e->history.count - 1) %
		    DE352_EDGE_ROWS].sequence;
	mutex_spin_exit(&e->lock);
	if (sequence != 0)
		de352_edge_measure(sc, sequence);
}

/*
 * A733 manual 15.2.7.3 starts line numbering at 1 on VSA. 15.2.10.10
 * puts output active after VBP+1 lines. The separate START_DELAY
 * releases DE_SYNC at the end of its named line (15.2.10.6).
 * Leave a whole line of margin at each cutoff.
 */
static unsigned
de352_post_phase_result(const struct de352_post_phase *p)
{
	const struct sun60i_a733_tv0_phase *before = &p->before, *after = &p->after;
	const uint32_t lo = before->debug & 0xffff;
	const uint32_t hi = after->debug & 0xffff;
	const uint32_t vbp = after->basic4 & 0x7fff;
	const uint32_t delay = (after->control >> 4) & 0xffff;
	const uint32_t irq_mask = 0x40004000;

	if (before->control != after->control || before->basic4 != after->basic4 ||
	    (after->control & 0x80100000) != 0x80000000 ||
	    ((before->debug | after->debug) & 0x60000000) != 0 ||
	    (before->gint0 & irq_mask) != 0x40000000 ||
	    (after->gint0 & irq_mask) != 0x40000000 || lo == 0 || hi < lo)
		return POST_PHASE_TV0_STATUS;
	if (hi > vbp)
		return POST_PHASE_ACTIVE;
	if (hi >= delay)
		return POST_PHASE_DE_SYNC_RELEASED;
	if ((p->first & 0x14) != 4 || (p->last & 0x14) != 4 ||
	    p->matches != 7)
		return POST_PHASE_DE_STATUS;
	return POST_PHASE_SAFE;
}

static void
de352_measure_post_phase(struct sun60i_de352_softc *sc)
{
	struct de352_edge *e = &sc->edge;
	struct de352_post_phase p = { .reason = POST_PHASE_NO_IRQ };
	const struct de352_edge_record *r;
	struct de352_edge_sample sample;
	uint32_t expected_queue, expected_addr;
	uint64_t now;

	KASSERT(mutex_owned(&sc->lock));
	mutex_spin_enter(&e->lock);
	if (!e->armed || e->verified_ns == 0) {
		mutex_spin_exit(&e->lock);
		return;
	}
	p.ticket = e->ticket;
	p.posted_ns = e->posted_ns;
	p.verified_ns = e->verified_ns;
	expected_queue = e->queue;
	expected_addr = e->address;
	if (e->history.count != 0) {
		r = &e->history.row[(e->history.count - 1) % DE352_EDGE_ROWS];
		if (r->ticket == p.ticket) {
			p.sequence = r->sequence;
			p.irq_ns = r->irq_ns;
		}
	}
	mutex_spin_exit(&e->lock);

	if (p.irq_ns == 0)
		goto record;
	p.reason = POST_PHASE_TV0_READ;
	if (sun60i_a733_output_sample_tv0_phase(sc->output_phandle, sc,
	    p.sequence, &p.before) != 0)
		goto record;

	/* sc->lock keeps this ticket and the display lease across the reads. */
	p.reason = POST_PHASE_DE_READ;
	if (!de352_sample_edge(sc, &sample))
		goto record;
	p.first = sample.first;
	p.last = sample.last;
	p.matches = de352_edge_matches(&sample, expected_queue, expected_addr);
	p.reason = POST_PHASE_IRQ_RACE;
	if (sun60i_a733_output_sample_tv0_phase(sc->output_phandle, sc,
	    p.sequence, &p.after) != 0)
		goto record;
	p.reason = de352_post_phase_result(&p);
record:
	now = de352_now();
	p.sampled_ns = now;
	mutex_spin_enter(&e->lock);
	/* Correlate a vendor idle vblank with the completed transfer.
	 * This observation alone never sends a page flip or releases storage. */
	if (sc->field_probe && p.reason == POST_PHASE_DE_SYNC_RELEASED &&
	    e->previous_idle_sequence == p.sequence && p.sequence != 0 &&
	    p.matches == 7 && (p.first & 0x14) == 0x14 &&
	    (p.last & 0x14) == 0x14 &&
	    (p.after.debug & 0xffff) < (p.after.basic4 & 0x7fff))
		p.reason = POST_PHASE_FIELD_TRANSITION;
	if (p.reason == POST_PHASE_SAFE || p.reason == POST_PHASE_FIELD_TRANSITION) {
		r = &e->history.row[(e->history.count - 1) % DE352_EDGE_ROWS];
		if (!e->armed || p.ticket != e->ticket ||
		    r->sequence != p.sequence || r->ticket != p.ticket ||
		    p.posted_ns > p.irq_ns || p.irq_ns > p.verified_ns ||
		    p.sampled_ns - p.irq_ns >= e->blank_ns)
			p.reason = POST_PHASE_IRQ_RACE;
	}
	e->post.row[e->post.count++ % DE352_EDGE_ROWS] = p;
	mutex_spin_exit(&e->lock);
}

/*
 * The worker already verified this immutable request before the interrupt.
 * Confirm the same online queue and pixel address at this field boundary.
 * The TV0 interrupt owns the outer spin lock. Do not call the output provider
 * or release any DMA reference here.
 */
static void
de352_confirm_irq(struct sun60i_de352_softc *sc, struct de352_edge_record *r,
    const struct sun60i_a733_irq_window *window)
{
	const struct de352_edge *e = &sc->edge;
	struct de352_edge_sample s;
	uint64_t age;
	uint32_t before, after;

	if (!e->irq_readback || r->verified_ns == 0 ||
	    r->posted_ns > r->verified_ns || r->verified_ns > r->irq_ns ||
	    e->blank_ns == 0 || window == NULL ||
	    !window->blank_line(window->cookie, &before))
		return;
	/* AArch64 peek uses the current LWP fault buffer. The IRQ uses only
	 * the live DE aperture already verified under this ticket. */
	s.first = bus_space_read_4(sc->bst, sc->bsh, 0x8104);
	if ((s.first & 0x14) != 4)
		return;
	s.queue = bus_space_read_4(sc->bst, sc->bsh, 0x8114);
	s.address = bus_space_read_4(sc->bst, sc->bsh, 0x1c1010);
	s.offline = bus_space_read_4(sc->bst, sc->bsh, 0x80d0);
	s.last = bus_space_read_4(sc->bst, sc->bsh, 0x8104);
	if ((s.last & 0x14) != 4 ||
	    de352_edge_matches(&s, e->queue, e->address) != 7 ||
	    !window->blank_line(window->cookie, &after) || after < before)
		return;
	age = de352_now() - r->irq_ns;
	if (age == 0 || age >= e->blank_ns || age > UINT32_MAX)
		return;
	r->matches = 7;
	r->first_status = s.first;
	r->last_status = s.last;
	r->sample_age_ns = (uint32_t)age;
}

static void
de352_edge_irq(void *arg, uint64_t sequence,
    const struct sun60i_a733_irq_window *window)
{
	struct sun60i_de352_softc *sc = arg;
	struct de352_edge *edge = &sc->edge;
	sun60i_a733_vblank_fn consumer;
	void *cookie;

	mutex_spin_enter(&edge->lock);
	if (edge->armed && edge->posted_ns != 0) {
		struct de352_edge_history *h = &edge->history;
		struct de352_edge_record *r = &h->row[h->count % DE352_EDGE_ROWS];

		r->ticket = edge->ticket;
		r->sequence = sequence;
		r->irq_ns = de352_now();
		r->posted_ns = edge->posted_ns;
		r->verified_ns = edge->verified_ns;
		r->first_status = r->last_status = r->matches = r->sample_age_ns = 0;
		h->count++;
		de352_confirm_irq(sc, r, window);
		if (sc->field_probe) {
			struct de352_early_history *early = &edge->early;
			early->row[early->count++ % DE352_EDGE_ROWS] =
			    (struct de352_early_row){
				.ticket = r->ticket, .sequence = sequence,
				.irq_ns = r->irq_ns, .blank_ns = edge->blank_ns
			    };
		}
		if (r->sample_age_ns == 0 && edge->worker != NULL && !edge->scheduled) {
			edge->scheduled = true;
			workqueue_enqueue(edge->worker, &edge->work, NULL);
		}
	}
	consumer = edge->consumer;
	cookie = edge->arg;
	mutex_spin_exit(&edge->lock);
	if (consumer != NULL)
		consumer(cookie, sequence);
}

static void
de352_edge_disarm(struct sun60i_de352_softc *sc)
{
	mutex_spin_enter(&sc->edge.lock);
	sc->edge.armed = false;
	mutex_spin_exit(&sc->edge.lock);
}

static int
de352_read(struct sun60i_de352_softc *sc, uint32_t reg, uint32_t *value)
{
	KASSERT(sc->retained && sc->mapped);
	if ((reg & 3) != 0 || reg > DE352_SIZE - 4)
		return EINVAL;
	/* A fault-aware read does not bound an unclocked bus transaction. */
	return bus_space_peek_4(sc->bst, sc->bsh, reg, value);
}

static int
de352_field(struct sun60i_de352_softc *sc, uint32_t reg,
    uint32_t mask, uint32_t value)
{
	struct de352_run_trace *trace = &sc->run_trace;
	const bool capture = sc->step == 7 && trace->valid == 1;
	uint32_t before, after;
	int error;

	if (capture) {
		trace->field_valid = 5;
		trace->reg = reg;
		trace->mask = mask;
		trace->requested = value;
		trace->before = trace->after = 0;
	}
	if ((value & ~mask) != 0)
		return EINVAL;
	error = de352_read(sc, reg, &before);
	if (error != 0)
		return error;
	if (capture) {
		trace->before = before;
		trace->field_valid |= 2;
	}
	error = bus_space_poke_4(sc->bst, sc->bsh, reg,
	    (before & ~mask) | value);
	if (error != 0)
		return error;
	bus_space_barrier(sc->bst, sc->bsh, reg, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	error = de352_read(sc, reg, &after);
	if (error != 0)
		return error;
	if (capture) {
		trace->after = after;
		trace->field_valid |= 8;
	}
	return (after & mask) == value ? 0 : EIO;
}

static int de352_cold_verify(struct sun60i_de352_softc *,
    bus_space_handle_t);

/* Caller holds the one display reservation. No IOMMU or NSI write is made. */
static int
de352_memory(struct sun60i_de352_softc *sc)
{
	static const uint32_t iregs[] = { 0, 0x10, 0x14, 0x804, 0x838 };
	static const uint32_t nregs[] = { 0x410, 0x414, 0x418 };
	uint32_t clocks[2][A733_DISPLAY_NMEMORY_REGS], iommu[2][5], nsi[2][3];
	bus_space_handle_t ih, nh;
	int error;

	memset(&sc->memory, 0, sizeof(sc->memory));
	sc->memory.version = 1;
	error = sun60i_a733_ccu_display_memory_state(sc->de0, sc, clocks);
	if (error != 0)
		return error;
	memcpy(sc->memory.clocks, clocks, sizeof(clocks));
	sc->memory.valid = 1;
	if (memcmp(clocks[0], clocks[1], sizeof(clocks[0])) != 0)
		return EBUSY;
	/* Fixed borrowed profile: static PERIPH0-600M for MBUS. No retune. */
	if ((clocks[0][A733_DISPLAY_MEMORY_IOMMU1] & 0x10007) != 0x10007 ||
	    (clocks[0][A733_DISPLAY_MEMORY_NSI_CFG] & 0x10001) != 0x10001 ||
	    (clocks[0][A733_DISPLAY_MEMORY_MBUS] & 0x9f000000) != 0x81000000 ||
	    (clocks[0][A733_DISPLAY_MEMORY_MBUS_MASTER] & 0x802) != 0x802 ||
	    (clocks[0][A733_DISPLAY_MEMORY_PERIPH_GATE] & 0x02000200) != 0x02000200 ||
	    (clocks[0][A733_DISPLAY_MEMORY_PERIPH_STATUS] & 0x02000000) == 0 ||
	    (clocks[0][A733_DISPLAY_MEMORY_NSI] & 0xdf000000) != 0xc3000000)
		return EOPNOTSUPP;
	if (sc->memory_mapped) {
		ih = sc->memory_iommu;
		nh = sc->memory_nsi;
	} else {
		error = bus_space_map(sc->bst, DE352_IOMMU1_BASE,
		    DE352_IOMMU1_SIZE, 0, &ih);
		if (error != 0)
			return error;
		error = bus_space_map(sc->bst, DE352_NSI_BASE, DE352_NSI_SIZE,
		    0, &nh);
		if (error != 0) {
			bus_space_unmap(sc->bst, ih, DE352_IOMMU1_SIZE);
			return error;
		}
	}
	for (u_int s = 0; s < 2 && error == 0; s++) {
		for (u_int i = 0; i < __arraycount(iregs) && error == 0; i++)
			error = bus_space_peek_4(sc->bst, ih, iregs[i], &iommu[s][i]);
		for (u_int i = 0; i < __arraycount(nregs) && error == 0; i++)
			error = bus_space_peek_4(sc->bst, nh, nregs[i], &nsi[s][i]);
	}
	if (error != 0)
		goto out;
	memcpy(sc->memory.iommu, iommu, sizeof(iommu));
	memcpy(sc->memory.nsi, nsi, sizeof(nsi));
	sc->memory.valid |= 2;
	if (memcmp(iommu[0], iommu[1], sizeof(iommu[0])) != 0 ||
	    memcmp(nsi[0], nsi[1], sizeof(nsi[0])) != 0) {
		error = EBUSY;
		goto out;
	}
	/* Unknown bypass/global-disable behavior is not an identity mapping. */
	if (iommu[0][0] != 0x16 || (iommu[0][1] & 4) == 0 ||
	    iommu[0][2] != 1 || iommu[0][3] != 1 || iommu[0][4] != 0 ||
	    (nsi[0][0] & 3) != 0 || (nsi[0][2] & 1) == 0) {
		error = EOPNOTSUPP;
		goto out;
	}
	if (sc->cold_initialized)
		error = de352_cold_verify(sc, ih);
out:
	if (!sc->memory_mapped) {
		if (error == 0 && sc->step == 10 && sc->retained) {
			/* The successful DE owner has a boot-long lease. */
			sc->memory_iommu = ih;
			sc->memory_nsi = nh;
			sc->memory_mapped = true;
		} else {
			bus_space_unmap(sc->bst, nh, DE352_NSI_SIZE);
			bus_space_unmap(sc->bst, ih, DE352_IOMMU1_SIZE);
		}
	}
	return error;
}

/* A complete stable capture must precede this restricted local profile. */
static bool
de352_memory_manageable(const struct de352_memory_snapshot *m)
{
	return m->valid == 3 && m->iommu[0][0] == 0x16 &&
	    m->iommu[0][2] == 1 && m->iommu[0][3] <= 1 &&
	    m->iommu[0][4] == 0;
}

/* No allocated DMA or released internal core may precede local preparation. */
static int
de352_memory_cold(struct sun60i_de352_softc *sc)
{
	uint32_t word;
	int error;

	if (!sc->reserved || !sc->power_reserved || !sc->retained ||
	    !sc->mapped || sc->dma != NULL)
		return EINVAL;
	error = sun60i_a733_ccu_display_ready(sc->de0, sc);
	if (error == 0) {
		uint32_t peers[2][A733_DISPLAY_NISOLATION_REGS];

		error = sun60i_a733_ccu_display_isolation_state(sc->de0, sc, peers);
	}
	if (error != 0)
		return error;
	error = de352_read(sc, 0x8004, &word);
	if (error != 0 || word != 0)
		return error != 0 ? error : EBUSY;
	error = de352_read(sc, 0x8000, &word);
	if (error != 0 || (word & 1) != 0)
		return error != 0 ? error : EBUSY;
	return 0;
}

/* Fixed v0x16 global and MIC controls. No error-clear register is included. */
static uint32_t
de352_cold_reg(u_int index)
{
	static const uint32_t global[] = {
		0, 0x10, 0x14, 0x1c, 0x20, 0x2c, 0x30, 0x40, 0x54,
		0x60, 0x6c, 0x80, 0x88
	};
	static const uint32_t micro[] = { 4, 0x18, 0x30, 0x38 };

	KASSERT(index < CI_COUNT);
	if (index < CI_MIC_BASE)
		return global[index];
	return 0x400 + ((index - CI_MIC_BASE) / 4) * 0x200 +
	    micro[(index - CI_MIC_BASE) % 4];
}

static int
de352_cold_read(struct sun60i_de352_softc *sc, bus_space_handle_t ih,
    struct de352_cold_state *out, struct de352_cold_trace *trace)
{
	struct de352_cold_state state;
	uint32_t peers[2][A733_DISPLAY_NISOLATION_REGS];
	int error;

	if (trace != NULL) {
		trace->capture_attempt++;
		trace->capture_words = 0;
		trace->capture_reg = UINT32_MAX;
	}
	error = sun60i_a733_ccu_display_ready(sc->de0, sc);
	if (error == 0)
		error = sun60i_a733_ccu_display_isolation_state(sc->de0, sc, peers);
	if (error != 0)
		return error;
	for (u_int sample = 0; sample < 2; sample++) {
		for (u_int i = 0; i < CI_COUNT; i++) {
			if (trace != NULL)
				trace->capture_reg = de352_cold_reg(i);
			error = bus_space_peek_4(sc->bst, ih, de352_cold_reg(i),
			    &state.words[sample][i]);
			if (error != 0)
				return error;
			if (trace != NULL)
				trace->capture_words++;
		}
	}
	if (trace != NULL) {
		trace->snapshot = state;
		trace->capture_generation = trace->capture_attempt;
		trace->capture_valid = 1;
	}
	if (memcmp(state.words[0], state.words[1], sizeof(state.words[0])) != 0)
		return EBUSY;
	if (trace != NULL)
		trace->capture_valid |= 2;
	error = sun60i_a733_ccu_display_ready(sc->de0, sc);
	if (error == 0)
		error = sun60i_a733_ccu_display_isolation_state(sc->de0, sc, peers);
	if (error != 0)
		return error;
	if (trace != NULL)
		trace->capture_valid |= 4;
	*out = state;
	return 0;
}

static bool
de352_cold_admit(const struct de352_cold_state *state)
{
	const uint32_t *w = state->words[0];

	if (w[CI_VERSION] != 0x16 || w[CI_ENABLE] != 0 ||
	    (w[CI_AUTOGATE] != 0 && w[CI_AUTOGATE] != 0x7fff0003) ||
	    w[CI_TTB] > 0x3ffff || (w[CI_PREFETCH] & ~0x3007fU) != 0 ||
	    w[CI_FLUSH] != 0 || w[CI_TLB_MODE] > 1 || w[CI_TLB_REQUEST] != 0 ||
	    w[CI_PTW_MODE] > 1 || w[CI_PTW_REQUEST] != 0 ||
	    (w[CI_IRQ_MASK] & ~3U) != 0 || w[CI_IRQ_STATUS] != 0)
		return false;
	for (u_int i = CI_MIC_BASE; i < CI_COUNT; i += 4) {
		if (w[i] > 1 || w[i + 1] != 0 || w[i + 2] > 1 || w[i + 3] != 0)
			return false;
	}
	return true;
}

/* Observed request states. Neither transition proves flush completion. */
static bool
de352_cold_request_cleared(u_int index, uint32_t previous, uint32_t actual)
{
	if (actual != 0)
		return false;
	if (index == CI_FLUSH)
		return previous == 3;
	return index >= CI_MIC_BASE && (index - CI_MIC_BASE) % 4 == 1 &&
	    previous == 1;
}

static int
de352_cold_same(struct de352_cold_state *expected,
    const struct de352_cold_state *actual)
{
	for (u_int i = 0; i < CI_COUNT; i++) {
		if (actual->words[0][i] == expected->words[0][i])
			continue;
		if (de352_cold_request_cleared(i, expected->words[0][i],
		    actual->words[0][i]))
			continue;
		return EBUSY;
	}
	*expected = *actual;
	return 0;
}

static int
de352_cold_verify(struct sun60i_de352_softc *sc, bus_space_handle_t ih)
{
	struct de352_cold_state current;
	int error;

	if (sc->guard == NULL || !sc->guard->published)
		return EINVAL;
	error = de352_cold_read(sc, ih, &current, NULL);
	if (error == 0)
		error = de352_cold_same(&sc->cold, &current);
	return error;
}

/* A bounded native request sequence. Physical RCQ remains the acceptance test. */
static int
de352_cold_prepare(struct sun60i_de352_softc *sc)
{
	struct { u_int index; uint32_t mask, value; } steps[32];
	struct de352_cold_state expected, current;
	struct de352_cold_trace *trace = &sc->cold_trace;
	bus_space_handle_t ih;
	uint32_t before, after, value;
	u_int count = 0;
	int error;

	de352_trace_phase(trace, CT_COLD_CHECK);
	error = de352_memory_cold(sc);
	if (error != 0)
		goto done;
	de352_trace_phase(trace, CT_PEER_ACCESS);
	error = sun60i_a733_ccu_display_begin_peer_access(sc->de0, sc);
	if (error != 0)
		goto done;
	/* Domain 2 is now retained ON. Only now may MIC0/1 be accessed. */
	de352_trace_phase(trace, CT_IOMMU_MAP);
	error = bus_space_map(sc->bst, DE352_IOMMU1_BASE, DE352_IOMMU1_SIZE, 0, &ih);
	if (error != 0)
		goto done;
	de352_trace_phase(trace, CT_CAPTURE);
	error = de352_cold_read(sc, ih, &expected, trace);
	if (error != 0)
		goto out;
	de352_trace_phase(trace, CT_ADMIT);
	if (!de352_cold_admit(&expected)) {
		error = EOPNOTSUPP;
		goto out;
	}
	de352_trace_phase(trace, CT_GUARD);
	error = sun60i_a733_de352_guard_prepare(sc->dmat, &sc->guard);
	if (error != 0)
		goto out;
#define CI_STEP(i, m, v) do { \
	KASSERT(count < __arraycount(steps)); \
	steps[count].index = (i); steps[count].mask = (m); \
	steps[count++].value = (v); \
} while (0)
	CI_STEP(CI_RESET, 0x3f, 0);
	CI_STEP(CI_IRQ_MASK, 3, 0);
	for (u_int i = CI_MIC_BASE; i < CI_COUNT; i += 4)
		CI_STEP(i + 2, 1, 0);
	for (u_int i = CI_MIC_BASE; i < CI_COUNT; i += 4)
		CI_STEP(i, 1, 1);
	CI_STEP(CI_TTB, 0x3ffff, (uint32_t)(sc->guard->directory.address >> 14));
	CI_STEP(CI_PREFETCH, 0x3007f, 0);
	CI_STEP(CI_TLB_MODE, 1, 1);
	CI_STEP(CI_PTW_MODE, 1, 1);
	CI_STEP(CI_RESET, 0x3f, 0x3f);
	CI_STEP(CI_FLUSH, 3, 3);
	for (u_int i = CI_MIC_BASE; i < CI_COUNT; i += 4)
		CI_STEP(i + 1, 1, 1);
	CI_STEP(CI_COUNT, 0, 10);
	CI_STEP(CI_FLUSH, 3, 0);
	CI_STEP(CI_ENABLE, 1, 1);
#undef CI_STEP
	/* No later uncertainty permits release of this guard directory. */
	de352_trace_phase(trace, CT_PUBLISH);
	error = sun60i_a733_de352_guard_publish(sc->guard);
	if (error != 0)
		goto out;
	for (u_int n = 0; n < count; n++) {
		const u_int index = steps[n].index;
		uint32_t reg;

		trace->op = n;
		de352_trace_phase(trace, CT_STEP_CHECK);
		error = de352_memory_cold(sc);
		if (error != 0)
			goto out;
		de352_trace_phase(trace, CT_STEP_CAPTURE);
		error = de352_cold_read(sc, ih, &current, trace);
		if (error != 0)
			goto out;
		de352_trace_phase(trace, CT_STEP_COMPARE);
		error = de352_cold_same(&expected, &current);
		if (error != 0)
			goto out;
		if (index == CI_COUNT) {
			/* Pinned BSP timing input, not observed completion. */
			de352_trace_phase(trace, CT_DELAY);
			trace->requested = steps[n].value;
			trace->valid = CT_REQUESTED;
			delay(steps[n].value);
			continue;
		}
		reg = de352_cold_reg(index);
		de352_trace_phase(trace, CT_BEFORE);
		trace->reg = reg;
		trace->mask = steps[n].mask;
		trace->valid = CT_REG;
		error = bus_space_peek_4(sc->bst, ih, reg, &before);
		if (error != 0)
			goto out;
		trace->before = before;
		trace->valid |= CT_BEFORE_VALID;
		if (before != expected.words[0][index] &&
		    !de352_cold_request_cleared(index,
		    expected.words[0][index], before)) {
			error = EBUSY;
			goto out;
		}
		value = (before & ~steps[n].mask) | steps[n].value;
		trace->requested = value;
		trace->valid |= CT_REQUESTED;
		/* Deassert the global request even when its readback is zero. */
		if (value == before && !(index == CI_FLUSH && steps[n].value == 0))
			continue;
		trace->phase = CT_WRITE;
		error = bus_space_poke_4(sc->bst, ih, reg, value);
		if (error != 0)
			goto out;
		bus_space_barrier(sc->bst, ih, reg, 4,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		trace->phase = CT_AFTER;
		error = bus_space_peek_4(sc->bst, ih, reg, &after);
		if (error != 0)
			goto out;
		trace->after = after;
		trace->valid |= CT_AFTER_VALID;
		if (after != value &&
		    !de352_cold_request_cleared(index, value, after)) {
			error = EIO;
			goto out;
		}
		expected.words[0][index] = expected.words[1][index] = after;
	}
	trace->op = UINT32_MAX;
	de352_trace_phase(trace, CT_FINAL_CHECK);
	error = de352_memory_cold(sc);
	if (error != 0)
		goto out;
	de352_trace_phase(trace, CT_FINAL_CAPTURE);
	error = de352_cold_read(sc, ih, &current, trace);
	if (error != 0)
		goto out;
	de352_trace_phase(trace, CT_FINAL_COMPARE);
	error = de352_cold_same(&expected, &current);
	if (error == 0) {
		sc->cold = expected;
		sc->cold_initialized = true;
		de352_trace_phase(trace, CT_DONE);
	}
out:
	bus_space_unmap(sc->bst, ih, DE352_IOMMU1_SIZE);
	if (sc->guard != NULL && !sc->guard->published) {
		(void)sun60i_a733_de352_guard_destroy(sc->guard);
		sc->guard = NULL;
	}
done:
	trace->error = error;
	return error;
}

/* No global IOMMU, table, interrupt, shared clock or peer register is written. */
static int
de352_memory_prepare(struct sun60i_de352_softc *sc)
{
	static const struct {
		bool iommu;
		uint32_t reg, mask, value, sample;
	} updates[] = {
		{ true, 0x804, 1, 1, 3 },
		{ false, 0x410, 3, 0, 0 },
		{ false, 0x418, 1, 1, 2 },
		{ true, 0x10, 4, 4, 1 },
	};
	struct de352_memory_snapshot expected = sc->memory;
	bus_space_handle_t ih, nh;
	uint32_t before, after;
	int error;

	error = de352_memory_cold(sc);
	if (error != 0)
		return error;
	if (!de352_memory_manageable(&expected))
		return EOPNOTSUPP;
	error = bus_space_map(sc->bst, DE352_IOMMU1_BASE, DE352_IOMMU1_SIZE,
	    0, &ih);
	if (error != 0)
		return error;
	error = bus_space_map(sc->bst, DE352_NSI_BASE, DE352_NSI_SIZE, 0, &nh);
	if (error != 0) {
		bus_space_unmap(sc->bst, ih, DE352_IOMMU1_SIZE);
		return error;
	}
	for (u_int i = 0; i < __arraycount(updates); i++) {
		bus_space_handle_t handle;
		uint32_t value, *words;

		/* Repeat the retained owner, cold-core and exact snapshot proof. */
		error = de352_memory_cold(sc);
		if (error != 0)
			break;
		error = de352_memory(sc);
		if (error != 0 && error != EOPNOTSUPP)
			break;
		if (!de352_memory_manageable(&sc->memory) ||
		    memcmp(&expected, &sc->memory, sizeof(expected)) != 0) {
			error = EBUSY;
			break;
		}
		handle = updates[i].iommu ? ih : nh;
		words = updates[i].iommu ? expected.iommu[0] : expected.nsi[0];
		error = bus_space_peek_4(sc->bst, handle, updates[i].reg, &before);
		if (error != 0)
			break;
		if (before != words[updates[i].sample]) {
			error = EBUSY;
			break;
		}
		value = (before & ~updates[i].mask) | updates[i].value;
		if (value == before)
			continue;
		/* The retained lease also covers a faulting first write attempt. */
		error = bus_space_poke_4(sc->bst, handle, updates[i].reg, value);
		if (error != 0)
			break;
		bus_space_barrier(sc->bst, handle, updates[i].reg, 4,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		error = bus_space_peek_4(sc->bst, handle, updates[i].reg, &after);
		if (error != 0)
			break;
		if (after != value) {
			error = EIO;
			break;
		}
		for (u_int sample = 0; sample < 2; sample++) {
			words = updates[i].iommu ? expected.iommu[sample] :
			    expected.nsi[sample];
			words[updates[i].sample] = value;
		}
	}
	bus_space_unmap(sc->bst, nh, DE352_NSI_SIZE);
	bus_space_unmap(sc->bst, ih, DE352_IOMMU1_SIZE);
	if (error == 0)
		error = de352_memory_cold(sc);
	if (error == 0)
		error = de352_memory(sc);
	if (error == 0 && memcmp(&expected, &sc->memory, sizeof(expected)) != 0)
		error = EBUSY;
	/* The outer coordinator quarantines failure. This helper never releases. */
	return error;
}

/* Read only DE top until the internal core clock and reset are established. */
static int
de352_open_core(struct sun60i_de352_softc *sc)
{
	uint32_t value;
	int error;

	error = de352_read(sc, 0x8004, &value);
	if (error != 0)
		return error;
	/* No known or unknown module gate may be open before shared MBUS. */
	if (value != 0)
		return EBUSY;
	/* A closed gate alone must not revive a retained RTMX/DMA state. */
	error = de352_read(sc, 0x8000, &value);
	if (error != 0 || (value & 1) != 0)
		return error != 0 ? error : EBUSY;
	error = de352_read(sc, 0x8028, &value);
	if (error != 0 || (value & 0xfff) != 0)
		return error != 0 ? error : EOPNOTSUPP;
	error = de352_read(sc, 0x802c, &value);
	if (error != 0 || (value & 0xffff) != 0)
		return error != 0 ? error : EOPNOTSUPP;
	error = de352_read(sc, 0x8010, &value);
	if (error != 0)
		return error;
	for (u_int shift = 4; shift <= 12; shift += 4)
		if (((value >> shift) & 0xf) == DE352_TV0_MUX)
			return EBUSY;
	error = de352_field(sc, 0x8000, 1, 1);
	if (error == 0)
		error = de352_field(sc, 0x8004, 1, 1);
	if (error == 0)
		error = de352_field(sc, 0x8008, 0x10, 0x10);
	if (error == 0)
		error = de352_field(sc, 0x8008, 1, 1);
	return error;
}

/* No unowned active processing block is reset or silently overwritten. */
static int
de352_idle(struct sun60i_de352_softc *sc)
{
	static const struct { uint32_t reg, mask; } idle[] = {
		{ 0x8100, 1 }, { 0x8110, UINT32_MAX },
		{ 0x80d0, 1 }, { 0x8020, 1 },
		{ 0x811c, UINT32_MAX }, { 0x281000, UINT32_MAX },
		{ 0x2810b0, 0x11111 },
		{ 0x105000, 1 }, { 0x105100, 1 },
		{ 0x105400, 1 }, { 0x125400, 1 }, { 0x1e5400, 1 },
		{ 0x1c0800, 1 }, { 0x1c4000, 0x40000001 },
		{ 0x1c5400, 1 }, { 0x285000, 1 },
		{ 0x287000, 0x11111 }, { 0x288000, 1 },
		{ 0x289000, 1 }, { 0x289040, 0x1f },
		{ 0x289050, 1 }, { 0x289070, 1 }, { 0x28907c, 1 },
		{ 0x28b000, 0x80000313 }
	};
	uint32_t value;
	int error;

	for (u_int i = 0; i < __arraycount(idle); i++) {
		error = de352_read(sc, idle[i].reg, &value);
		if (error != 0)
			return error;
		if ((value & idle[i].mask) != 0)
			return EBUSY;
	}
	/* All present OVL layers are mapped to the now-clocked core0. */
	for (u_int channel = 0; channel < 7; channel++) {
		const uint32_t base = 0x101000 +
		    (channel < 3 ? channel : channel + 3) * 0x20000;
		const uint32_t stride = channel < 3 ? 0x30 : 0x20;
		for (u_int layer = 0; layer < 4; layer++) {
			error = de352_read(sc, base + layer * stride, &value);
			if (error != 0 || (value & 1) != 0)
				return error != 0 ? error : EBUSY;
		}
	}
	/* Unknown RTMX control fields are not admitted by the fixed profile. */
	error = de352_read(sc, 0x8100, &value);
	if (error != 0 || (value & ~UINT32_C(0x302d1)) != 0)
		return error != 0 ? error : EOPNOTSUPP;
	/* The RCQ layer block includes only these seven fully known words. */
	error = de352_read(sc, 0x1c1000, &value);
	if (error != 0)
		return error;
	if ((value & ~UINT32_C(0xffb31f17)) != 0)
		return EOPNOTSUPP;
	error = de352_read(sc, 0x1c1004, &value);
	if (error != 0)
		return error;
	return (value & ~UINT32_C(0x1fff1fff)) == 0 ? 0 : EOPNOTSUPP;
}

static int
de352_configure(struct sun60i_de352_softc *sc)
{
	struct sun60i_a733_de352_rgb_words *w = &sc->dma->layer;
	const struct { uint32_t reg, mask, value; } fields[] = {
		{ 0x8050, 0xffff, 0x2000 },
		{ 0x8080, 0xffff0000, 0x08000000 },
		{ 0x802c, 0xf, 0 }, { 0x8030, 0xf000, 0x8000 },
		{ 0x8010, 0xf, DE352_TV0_MUX },
		{ 0x8108, 0x1fff1fff, w->size },
		{ 0x8100, 0x302d0, 0 },
		{ 0x1c1080, 0xff, w->address_high_byte },
		{ 0x1c1084, 0xff, 0 },
		{ 0x1c1088, 0x1fff1fff, w->channel_size },
		{ 0x1c10f0, 0x3fff3fff, 0 }, { 0x1c10f8, 0x3fff3fff, 0 },
		{ 0x281004, UINT32_MAX, UINT32_C(0xff000000) },
		{ 0x281008, 0x1fff1fff, w->size },
		{ 0x28100c, UINT32_MAX, 0 },
		{ 0x281080, 0xf, 3 }, { 0x281084, 0x3f, 0 },
		{ 0x281088, 0xffffff, 0 },
		{ 0x28108c, 0x1fff1fff, w->size },
		{ 0x281090, 0x0f0f0f0f, 0x00010001 },
		{ 0x2810fc, 0x303, 0 }
	};
	int error;

	for (u_int i = 0; i < __arraycount(fields); i++) {
		error = de352_field(sc, fields[i].reg, fields[i].mask,
		    fields[i].value);
		if (error != 0)
			return error;
	}
	return 0;
}

static void
de352_trace_status(struct de352_rcq_trace *t, uint32_t status)
{
	t->status_or |= status;
	t->status_and &= status;
}

static uint32_t
de352_elapsed_us(uint64_t elapsed_ns)
{
	const uint64_t us = elapsed_ns / 1000;

	return us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

/* Record only the reads already needed to prove the queue update. */
static int
de352_poll(struct sun60i_de352_softc *sc, uint32_t mask,
    struct de352_rcq_trace *t)
{
	const uint64_t start = de352_now();
	uint64_t elapsed, tried = 0;
	int error;

	for (;;) {
		error = de352_read(sc, 0x8104, &sc->last_status);
		if (sc->field_probe && sc->pending.shadow && error == 0 &&
		    (sc->last_status & 0x14) == 0) {
			struct de352_edge *e = &sc->edge;
			uint64_t sequence = 0;

			mutex_spin_enter(&e->lock);
			if (e->armed && e->history.count != 0) {
				const struct de352_edge_record *r = &e->history.row[
				    (e->history.count - 1) % DE352_EDGE_ROWS];
				if (r->ticket == e->ticket)
					sequence = r->sequence;
			}
			mutex_spin_exit(&e->lock);
			if (sequence != 0 && sequence != tried) {
				de352_edge_measure(sc, sequence);
				/* TV0 can still report the final line at interrupt
				 * entry. Retry on later hardware polls while this
				 * exact ticket and blank are still current. */
				mutex_spin_enter(&e->lock);
				if (de352_edge_pending(e, sequence) == NULL)
					tried = sequence;
				mutex_spin_exit(&e->lock);
			}
		}
		elapsed = de352_now() - start;
		if (t != NULL) {
			t->poll_us = de352_elapsed_us(elapsed);
			if (error == 0) {
				t->poll_samples++;
				t->poll_last = sc->last_status;
				de352_trace_status(t, sc->last_status);
			}
		}
		if (error != 0 || (sc->last_status & mask) == mask)
			return error;
		/* Check the status even when the delay crossed the deadline. */
		if (elapsed >= DE352_TIMEOUT_NS)
			return ETIMEDOUT;
		delay(10);
	}
}

/* The request is a command. It need not remain set after the write. */
static int
de352_request(struct sun60i_de352_softc *sc, struct de352_rcq_trace *t,
    bool once)
{
	const uint64_t start = de352_now();
	uint64_t elapsed;
	uint32_t status, control;
	int error, level;
	bool submitted;

	for (;;) {
		submitted = false;
		level = splhigh();
		error = de352_read(sc, 0x8104, &status);
		if (error == 0)
			sc->last_status = status;
		if (error == 0 && t != NULL) {
			if (t->request_samples++ == 0)
				t->request_first = status;
			t->request_last = status;
			t->request_busy += (status & 0x10) != 0;
			de352_trace_status(t, status);
		}
		if (error == 0 && (status & 0x10) != 0) {
			error = de352_read(sc, 0x8110, &control);
			if (error == 0 && t != NULL)
				t->request_control = control;
			if (error == 0 && control != 0)
				error = EBUSY;
			if (error == 0) {
				mutex_spin_enter(&sc->edge.lock);
				error = bus_space_poke_4(sc->bst, sc->bsh,
				    0x8110, control | 1);
				bus_space_barrier(sc->bst, sc->bsh, 0x8110, 4,
				    BUS_SPACE_BARRIER_WRITE);
				submitted = error == 0;
				if (submitted && sc->edge.armed)
					sc->edge.posted_ns = de352_now();
				mutex_spin_exit(&sc->edge.lock);
			}
		}
		splx(level);
		elapsed = de352_now() - start;
		if (t != NULL)
			t->request_us = de352_elapsed_us(elapsed);
		if (error != 0 || submitted)
			return error;
		if (once)
			return EAGAIN;
		if (elapsed >= DE352_TIMEOUT_NS)
			return ETIMEDOUT;
		delay(10);
	}
}

static int
de352_vblank(struct sun60i_de352_softc *sc)
{
	const uint64_t start = de352_now();
	uint64_t first = 0, sequence;
	int error;
	bool have_first = false;

	/* Consume an old pending event, then require a distinct later event. */
	do {
		error = sun60i_a733_output_observed_vblank(sc->output_phandle,
		    sc, &sequence);
		if (error == 0) {
			if (have_first && sequence > first) {
				sc->vblank = sequence;
				return 0;
			}
			first = sequence;
			have_first = true;
		} else if (error != ENOENT) {
			return error;
		}
		delay(10);
	} while (de352_now() - start < DE352_TIMEOUT_NS);
	return ETIMEDOUT;
}

static void
de352_rcq_words(const struct sun60i_a733_de352_dma *dma,
    uint32_t words[RCQ_COPY_WORDS])
{
	const uint32_t list[RCQ_COPY_WORDS] = {
		dma->layer.attribute, dma->layer.size, dma->layer.coordinate,
		dma->layer.pitch, dma->layer.address_low, dma->layer.bottom_low,
		dma->layer.fill_color, dma->disabled ? 0x001 : 0x100
	};

	memcpy(words, list, sizeof(list));
}

/*
 * The queue changes only the pixel address on this active, owned output.
 * The first commit, a different plane setup and the drain path check every
 * register. A new latch needs a distinct pixel address.
 */
static bool
de352_address_flip(struct sun60i_de352_softc *sc,
    const struct sun60i_a733_de352_dma *dma)
{
	uint32_t old[RCQ_COPY_WORDS], next[RCQ_COPY_WORDS];

	if (!sc->bsp_vblank || !sc->field_probe || sc->verify_each_flip ||
	    sc->output_stopped || dma == NULL || dma->disabled ||
	    sc->active.dma == NULL || sc->active.dma->disabled ||
	    !sc->active.held || !sc->pending.held || !sc->active.shadow ||
	    !sc->pending.shadow || sc->pending.dma != dma ||
	    sc->shadow_retire_count >= DE352_SHADOW_RETIRE_MAX)
		return false;
	de352_rcq_words(sc->active.dma, old);
	de352_rcq_words(dma, next);
	if (old[4] == next[4])
		return false;
	old[4] = next[4];
	return memcmp(old, next, sizeof(old)) == 0;
}

/* The last poll read succeeded and the display resources are retained. */
static void
de352_rcq_timeout_sample(struct sun60i_de352_softc *sc,
    const struct sun60i_a733_de352_dma *dma,
    const uint32_t words[RCQ_COPY_WORDS])
{
	static const uint32_t regs[RCQ_TIMEOUT_REGS] = {
		0x8104, 0x8110, 0x8114, 0x8118, 0x811c,
		0x1c1000, 0x1c1004, 0x1c1008, 0x1c100c, 0x1c1010,
		0x1c1014, 0x1c1018, 0x281000
	};
	const struct sun60i_a733_de352_dma *old =
	    sc->active.dma != NULL ? sc->active.dma : sc->dma;
	struct de352_rcq_timeout *trace = &sc->update_trace.timeout;

	trace->attempted = 1;
	trace->old_queue = (uint32_t)old->queue.address;
	trace->new_queue = (uint32_t)dma->queue.address;
	de352_rcq_words(old, trace->old_words);
	memcpy(trace->new_words, words, sizeof(trace->new_words));
	for (u_int i = 0; i < __arraycount(regs); i++) {
		if (de352_read(sc, regs[i], &trace->observed[i]) != 0)
			break;
		trace->valid |= 1U << i;
	}
}

static int
de352_prepare_dma(struct sun60i_de352_softc *sc,
    struct sun60i_a733_de352_dma *dma, bool initial,
    struct de352_submit_trace *t)
{
	uint32_t value;
	int error;

	de352_edge_disarm(sc);
	if (t != NULL)
		t->phase = ST_MEMORY;
	if (!initial)
		sc->update_trace.submit_phase = ST_MEMORY;
	error = de352_memory(sc);
	if (error != 0)
		return error;
	if (t != NULL)
		t->phase = ST_PUBLISH;
	if (!initial)
		sc->update_trace.submit_phase = ST_PUBLISH;
	/* Retain both maps before even the first attempted pointer write. */
	error = sun60i_a733_de352_dma_publish(dma);
	if (error != 0)
		return error;
	if (t != NULL)
		t->phase = ST_QUEUE;
	if (!initial)
		sc->update_trace.submit_phase = ST_QUEUE;
	error = de352_field(sc, 0x8114, UINT32_MAX, (uint32_t)dma->queue.address);
	if (error == 0)
		error = de352_field(sc, 0x8118, 0xff, 0);
	if (error == 0)
		/* Admit only initial zero -> 32, or an owned 32-byte replacement. */
		error = de352_read(sc, 0x811c, &value);
	if (error == 0 && value != (initial ? 0 : 32))
		error = EOPNOTSUPP;
	if (error == 0)
		error = bus_space_poke_4(sc->bst, sc->bsh, 0x811c, 32);
	if (error != 0)
		return error;
	bus_space_barrier(sc->bst, sc->bsh, 0x8114, 12,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	error = de352_read(sc, 0x811c, &value);
	if (error != 0 || value != 32)
		return error != 0 ? error : EIO;
	if (t != NULL)
		t->phase = ST_ACK;
	if (!initial)
		sc->update_trace.submit_phase = ST_ACK;
	/* W1C: acknowledge only the prior RCQ finish and accept flags. */
	error = bus_space_poke_4(sc->bst, sc->bsh, 0x8104, 0x0c);
	if (error != 0)
		return error;
	bus_space_barrier(sc->bst, sc->bsh, 0x8104, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	error = de352_read(sc, 0x8104, &value);
	if (error != 0 || (value & 0x0c) != 0)
		return error != 0 ? error : EIO;
	if (!initial && !dma->disabled) {
		struct de352_edge *edge = &sc->edge;
		const bool irq_readback = de352_address_flip(sc, dma);

		mutex_spin_enter(&edge->lock);
		edge->irq_readback = irq_readback;
		edge->ticket++;
		/* The admitted single-plane modes fit this 64-bit calculation. */
		edge->blank_ns = sc->mode.vtotal > sc->mode.vdisplay &&
		    sc->mode.pixel_clock_hz != 0 ?
		    UINT64_C(1000000000) * sc->mode.htotal *
		    (sc->mode.vtotal - sc->mode.vdisplay) / sc->mode.pixel_clock_hz : 0;
		edge->posted_ns = edge->verified_ns = 0;
		edge->address = dma->layer.address_low;
		edge->previous_address = sc->active.dma != NULL ?
		    sc->active.dma->layer.address_low : sc->dma->layer.address_low;
		edge->previous_idle_sequence = 0;
		edge->queue = (uint32_t)dma->queue.address;
		edge->armed = true;
		mutex_spin_exit(&edge->lock);
	}
	return 0;
}

static int
de352_verify_dma(struct sun60i_de352_softc *sc,
    struct sun60i_a733_de352_dma *dma, bool initial,
    struct de352_submit_trace *t)
{
	uint32_t words[RCQ_COPY_WORDS], value;
	const bool address_only = !initial && de352_address_flip(sc, dma);
	const u_int first = address_only ? 4 : 0;
	const u_int end = address_only ? 5 : __arraycount(words);
	int error;

	de352_rcq_words(dma, words);
	if (t != NULL)
		t->phase = ST_COPY;
	if (!initial)
		sc->update_trace.submit_phase = ST_COPY;
	/* Confirm the changed address even when FINISH and TV0 are timely. */
	for (u_int i = first; i < end; i++) {
		const uint32_t reg = i == __arraycount(words) - 1 ?
		    0x281000 : 0x1c1000 + 4 * i;
		if (!initial) {
			sc->update_trace.reg = reg;
			sc->update_trace.expected = words[i];
		}
		error = de352_read(sc, reg, &value);
		if (!initial && error == 0)
			sc->update_trace.observed = value;
		if (error != 0 || value != words[i])
			return error != 0 ? error : EIO;
	}
	if (!initial)
		sc->update_trace.submit_phase = ST_DONE;
	mutex_spin_enter(&sc->edge.lock);
	if (sc->edge.armed)
		sc->edge.verified_ns = de352_now();
	mutex_spin_exit(&sc->edge.lock);
	if (sc->pending.shadow)
		de352_measure_post_phase(sc);
	return 0;
}

/*
 * Sleep through the transfer when the queue is still running. An IRQ which
 * races REQUEST can only wake this worker. FINISH and the field ticket
 * decide whether the buffer can be presented.
 */
static int
de352_wait_transfer(struct sun60i_de352_softc *sc, uint64_t generation)
{
	uint32_t status;
	uint64_t observed, budget;
	int error;

	if (generation == UINT64_MAX)
		return 0;
	error = de352_read(sc, 0x8104, &status);
	if (error != 0 || (status & 4) != 0)
		return error;
	budget = de352_replacement_extra(&sc->mode);
	if (budget == 0)
		return 0;
	error = sun60i_a733_output_wait_vblank_irq(sc->output_phandle, sc,
	    generation + 1, de352_now() + budget, &observed);
	if (error == ETIMEDOUT || error == EOPNOTSUPP)
		return 0;
	return error;
}

static int
de352_submit_dma(struct sun60i_de352_softc *sc,
    struct sun60i_a733_de352_dma *dma, bool initial)
{
	uint32_t words[RCQ_COPY_WORDS];
	struct de352_submit_trace *t = initial && sc->step == 8 &&
	    sc->submit_trace.valid == 1 ? &sc->submit_trace : NULL;
	struct de352_rcq_trace *rcq = initial ? (t != NULL ? &t->rcq : NULL) :
	    &sc->update_trace.rcq;
	uint64_t generation = UINT64_MAX;
	int error;

	error = de352_prepare_dma(sc, dma, initial, t);
	if (error != 0)
		return error;
	if (t != NULL)
		t->phase = ST_REQUEST;
	if (!initial)
		sc->update_trace.submit_phase = ST_REQUEST;
	if (!initial && de352_address_flip(sc, dma)) {
		error = sun60i_a733_output_sample_vblank_irq(sc->output_phandle,
		    sc, &generation);
		if (error == EOPNOTSUPP)
			generation = UINT64_MAX;
		else if (error != 0)
			return error;
	}
	error = de352_request(sc, rcq, false);
	if (error == 0)
		error = de352_wait_transfer(sc, generation);
	if (error == 0 && t != NULL)
		t->phase = ST_POLL;
	if (error == 0 && !initial)
		sc->update_trace.submit_phase = ST_POLL;
	if (error == 0) {
		error = de352_poll(sc, 4, rcq);
		if (!initial && error == ETIMEDOUT) {
			de352_rcq_words(dma, words);
			de352_rcq_timeout_sample(sc, dma, words);
		}
	}
	if (error != 0)
		return error;
	return de352_verify_dma(sc, dma, initial, t);
}

static int
de352_submit(struct sun60i_de352_softc *sc)
{
	return de352_submit_dma(sc, sc->dma, true);
}

/* Reads only. Capture state before the fault latches the output provider. */
static void
de352_submit_failure(struct sun60i_de352_softc *sc, int error)
{
	struct de352_submit_trace *t = &sc->submit_trace;

	t->result = error;
	for (u_int i = 0; i < ST_NREGS; i++)
		if (de352_read(sc, de352_submit_regs[i], &t->regs[i]) == 0)
			t->regs_valid |= 1U << i;
	t->sample_error = sun60i_a733_output_sample(sc->output_phandle, sc,
	    &t->sample);
	t->valid = 3;
}

static int
de352_run(struct sun60i_de352_softc *sc)
{
	bool held = false, managed = false, cold = false;
	int error;

	if (!sc->valid)
		return ENXIO;
	if (sc->discovery_pending) {
		if (!sc->retained || !sc->reserved || !sc->power_reserved)
			return EIO;
		cold = sc->discovery_cold;
		managed = sc->discovery_managed;
		goto discover;
	}
	if (sc->retained || sc->reserved || sc->power_reserved)
		return EBUSY;
	sc->step = 1;
	error = sun60i_a733_pck_display_reserve(sc->power_phandle, sc);
	if (error != 0)
		return error;
	sc->power_reserved = true;
	error = sun60i_a733_ccu_display_reserve(sc->de0, sc->power_phandle, sc);
	if (error != 0)
		goto fail;
	sc->reserved = true;
	sc->step = 2;
	/* This claim excludes native clock writers for the full DMA lifetime. */
	error = sun60i_a733_ccu_display_memory_reserve(sc->de0, sc);
	if (error != 0)
		goto fail;
	error = de352_memory(sc);
	if (error == EOPNOTSUPP && sc->memory.valid == 3 &&
	    sc->memory.iommu[0][0] == 0x16 && sc->memory.iommu[0][2] == 0 &&
	    sc->memory.iommu[0][3] <= 1 && sc->memory.iommu[0][4] == 0)
		cold = true;
	if (error == EOPNOTSUPP && (cold || de352_memory_manageable(&sc->memory))) {
		/* Reserve inactive peers before any retained local write. */
		error = sun60i_a733_ccu_display_isolation_reserve(sc->de0, sc);
		managed = error == 0;
	}
	if (error != 0)
		goto fail;
	sc->step = 3;
	error = sun60i_a733_ccu_display_begin_de0(sc->de0, sc, &held);
	sc->retained = held;
	if (error == 0 && !held)
		error = EPROTO;
	if (error != 0)
		goto fail;
	sc->discovery_pending = true;
	sc->discovery_cold = cold;
	sc->discovery_managed = managed;
discover:
	sc->step = 4;
	/* No DE map, cold-init, memory write or pixel DMA before a sink. */
	error = sun60i_a733_output_get_mode(sc->output_phandle, sc, &sc->mode);
	if (error == EAGAIN)
		return error;
	if (error != 0)
		goto fail;
	sc->discovery_pending = false;
	if (cold) {
		memset(&sc->cold_trace, 0, sizeof(sc->cold_trace));
		sc->cold_trace.version = 1;
		sc->cold_trace.op = UINT32_MAX;
		sc->cold_trace.capture_reg = UINT32_MAX;
		de352_trace_phase(&sc->cold_trace, CT_DE_MAP);
	}
	error = bus_space_map(sc->bst, DE352_BASE, DE352_SIZE, 0, &sc->bsh);
	if (error != 0) {
		if (cold)
			sc->cold_trace.error = error;
		goto fail;
	}
	sc->mapped = true;
	if (cold) {
		error = de352_cold_prepare(sc);
		if (error != 0)
			goto fail;
		error = de352_memory(sc);
		if (error != 0 && error != EOPNOTSUPP)
			goto fail;
	}
	if (managed) {
		error = de352_memory_prepare(sc);
		if (error != 0)
			goto fail;
	}
	error = de352_open_core(sc);
	if (error == 0)
		error = de352_idle(sc);
	if (error != 0)
		goto fail;
	sc->step = 6;
	error = sun60i_a733_de352_dma_prepare(sc->dmat, sc->mode.hdisplay,
	    sc->mode.vdisplay, &sc->dma);
	if (error != 0)
		goto fail;
	sc->step = 7;
	memset(&sc->run_trace, 0, sizeof(sc->run_trace));
	sc->run_trace.version = sc->run_trace.valid = 1;
	sc->run_trace.phase = RT_OUTPUT;
	sc->run_trace.width = sc->mode.hdisplay;
	sc->run_trace.height = sc->mode.vdisplay;
	sc->run_trace.pixel_clock_hz = sc->mode.pixel_clock_hz;
	error = sun60i_a733_output_prepare(sc->output_phandle, sc, &sc->mode);
	if (error != 0)
		goto fail;
	sc->run_trace.phase = RT_CONFIGURE;
	error = de352_configure(sc);
	if (error == 0) {
		sc->run_trace.phase = RT_MIXER_ENABLE;
		error = de352_field(sc, 0x810c, 1, 1);
	}
	if (error == 0) {
		sc->run_trace.phase = RT_DE_ENABLE;
		error = de352_field(sc, 0x8100, 1, 1);
	}
	if (error != 0)
		goto fail;
	sc->run_trace.valid = 3;
	sc->run_trace.phase = RT_DONE;
	sc->step = 8;
	memset(&sc->submit_trace, 0, sizeof(sc->submit_trace));
	sc->submit_trace.version = sc->submit_trace.valid = 1;
	sc->submit_trace.phase = ST_START;
	sc->submit_trace.rcq.status_and = UINT32_MAX;
	error = sun60i_a733_output_start(sc->output_phandle, sc);
	if (error == 0)
		error = de352_submit(sc);
	if (error != 0)
		goto fail;
	sc->submit_trace.phase = ST_DONE;
	sc->submit_trace.valid = 3;
	sc->step = 9;
	error = de352_vblank(sc);
	if (error == 0)
		error = de352_memory(sc);
	if (error == 0) {
		sc->step = 10;
		return 0;
	}
fail:
	if (sc->step == 7 && sc->run_trace.valid == 1) {
		sc->run_trace.result = error;
		sc->run_trace.valid = 3;
	}
	if (sc->step == 8 && sc->submit_trace.valid == 1)
		de352_submit_failure(sc, error);
	if (sc->retained) {
		(void)sun60i_a733_ccu_display_fault(sc->de0, sc, error);
		(void)sun60i_a733_output_fault(sc->output_phandle, sc, error);
		/* No reset, stop, pointer rewrite or DMA release after an error. */
	} else {
		if (sc->reserved) {
			sc->release_error = sun60i_a733_ccu_display_release(sc->de0, sc);
			if (sc->release_error == 0)
				sc->reserved = false;
		}
		/* Keep PCK if the dependent CCU claim did not release. */
		if (!sc->reserved && sc->power_reserved) {
			sc->release_error = sun60i_a733_pck_display_release(
			    sc->power_phandle, sc);
			if (sc->release_error == 0)
				sc->power_reserved = false;
		}
	}
	if (sc->dma != NULL && !sc->dma->published) {
		(void)sun60i_a733_de352_dma_destroy(sc->dma);
		sc->dma = NULL;
	}
	return error;
}

int
sun60i_a733_de352_start(int phandle)
{
	struct sun60i_de352_softc *sc = de352_lookup(phandle, NULL);
	int error;

	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->lock);
	if (sc->stage == 1) {
		error = sc->result;
	} else {
		error = de352_run(sc);
		if (error == EAGAIN && !sc->discovery_pending)
			error = EIO;
		sc->result = error;
		/* Only a proved absent sink may retry retained discovery. */
		if (error != EAGAIN)
			sc->stage = 1;
	}
	mutex_exit(&sc->lock);
	return error;
}

static int
de352_sysctl(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = container_of((int *)rnode->sysctl_data,
	    struct sun60i_de352_softc, stage);
	struct sysctlnode node = *rnode;
	int stage;
	int error;

	mutex_enter(&sc->lock);
	stage = sc->stage;
	mutex_exit(&sc->lock);
	node.sysctl_data = &stage;
	node.sysctl_size = sizeof(stage);
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	if (stage != 1)
		return EINVAL;
	return sun60i_a733_de352_start(sc->phandle);
}

static int
de352_sysctl_verify_flips(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = container_of((int *)rnode->sysctl_data,
	    struct sun60i_de352_softc, verify_each_flip);
	struct sysctlnode node = *rnode;
	int value, error;

	mutex_enter(&sc->lock);
	value = sc->verify_each_flip;
	mutex_exit(&sc->lock);
	node.sysctl_data = &value;
	node.sysctl_size = sizeof(value);
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	if (value != 0 && value != 1)
		return EINVAL;
	mutex_enter(&sc->lock);
	sc->verify_each_flip = value != 0;
	mutex_exit(&sc->lock);
	return 0;
}

static int
de352_sysctl_memory(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct de352_memory_snapshot snapshot;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	snapshot = sc->memory;
	mutex_exit(&sc->lock);
	node.sysctl_data = &snapshot;
	node.sysctl_size = sizeof(snapshot);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_cold_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct de352_cold_trace trace;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	trace = sc->cold_trace;
	mutex_exit(&sc->lock);
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_run_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct de352_run_trace trace;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	trace = sc->run_trace;
	mutex_exit(&sc->lock);
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_submit_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct de352_submit_trace trace;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	trace = sc->submit_trace;
	mutex_exit(&sc->lock);
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_update_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct de352_update_trace trace;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	trace = sc->update_trace;
	mutex_exit(&sc->lock);
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_output_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct sun60i_a733_output_trace trace;
	int error;

	if (newp != NULL)
		return EROFS;
	/* The normal run uses this same DE -> output lock order. */
	mutex_enter(&sc->lock);
	error = sun60i_a733_output_cached_trace(sc->output_phandle, &trace);
	mutex_exit(&sc->lock);
	if (error != 0)
		return error;
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_observe_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct sun60i_a733_output_observe_trace trace;
	int error;

	if (newp != NULL)
		return EROFS;
	/* The normal update uses this same DE -> output lock order. */
	mutex_enter(&sc->lock);
	error = sun60i_a733_output_cached_observe_trace(
	    sc->output_phandle, &trace);
	mutex_exit(&sc->lock);
	if (error != 0)
		return error;
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
de352_sysctl_phy_trace(SYSCTLFN_ARGS)
{
	struct sun60i_de352_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	struct sun60i_a733_hdmi_phy_trace trace;
	int error;

	if (newp != NULL)
		return EROFS;
	mutex_enter(&sc->lock);
	error = sun60i_a733_output_cached_phy_trace(sc->output_phandle, &trace);
	mutex_exit(&sc->lock);
	if (error != 0)
		return error;
	node.sysctl_data = &trace;
	node.sysctl_size = sizeof(trace);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

/* The initial profile permits no active IOMMU or NSI provider. */
static bool
de352_disabled_node(int node, const char *compatible, bus_addr_t base,
    bus_size_t length, const char *cell_name, uint32_t cell_count)
{
	const struct device_compatible_entry table[] = {
		{ .compat = compatible }, DEVICE_COMPAT_EOL
	};
	const char *status;
	bus_addr_t address;
	bus_size_t size;
	uint32_t cells;
	int len;

	if (node <= 0 || of_compatible_match(node, table) == 0 ||
	    fdtbus_get_reg(node, 0, &address, &size) != 0 ||
	    address != base || size != length ||
	    of_getprop_uint32(node, cell_name, &cells) != 0 || cells != cell_count)
		return false;
	status = fdtbus_get_prop(node, "status", &len);
	return status != NULL && len == 9 && memcmp(status, "disabled", 9) == 0;
}

static bool
de352_bindings(struct sun60i_de352_softc *sc)
{
	const uint32_t *p;
	int node, len;

	p = fdtbus_get_prop(sc->phandle, "iommus", &len);
	if (p == NULL || len != 12 || be32toh(p[1]) != 8 || be32toh(p[2]) != 1)
		return false;
	node = fdtbus_get_phandle_from_native(be32toh(p[0]));
	if (!de352_disabled_node(node, "allwinner,iommu-v20", 0x03900000,
	    0x20000, "#iommu-cells", 2))
		return false;
	p = fdtbus_get_prop(sc->phandle, "nsi", &len);
	if (p == NULL || len != 8 || be32toh(p[1]) != 2)
		return false;
	node = fdtbus_get_phandle_from_native(be32toh(p[0]));
	if (!de352_disabled_node(node, "allwinner,sunxi-nsi-v2", DE352_NSI_BASE,
	    0x10000, "#nsi-cells", 1))
		return false;
	/* No translated parent DMA window is accepted by this first profile. */
	for (node = sc->phandle; node > 0; node = OF_parent(node)) {
		if (OF_getproplen(node, "dma-ranges") > 0 ||
		    of_hasprop(node, "iommu-map") ||
		    (node != sc->phandle && of_hasprop(node, "iommus")))
			return false;
	}
	return true;
}

static int
de352_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct fdt_attach_args *faa = aux;
	(void)parent;
	(void)cf;
	return of_compatible_match(faa->faa_phandle, de352_compat);
}

static void
de352_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_de352_softc *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	const struct sysctlnode *node;
	const uint32_t *cells;
	bus_addr_t address;
	bus_size_t size;
	int len;

	(void)parent;
	sc->dev = self;
	sc->phandle = faa->faa_phandle;
	sc->bst = faa->faa_bst;
	sc->dmat = faa->faa_dmat;
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->edge.lock, MUTEX_DEFAULT, IPL_VM);
	aprint_naive("\n");
	aprint_normal(": A733 DE3.52, explicit single-plane probe only\n");
	if (fdtbus_get_reg(sc->phandle, 0, &address, &size) != 0 ||
	    address != DE352_BASE || size != DE352_SIZE)
		return;
	cells = fdtbus_get_prop(sc->phandle, "power-domains", &len);
	if (cells == NULL || len != 8 || be32toh(cells[1]) != 1)
		return;
	sc->power_phandle = fdtbus_get_phandle_from_native(be32toh(cells[0]));
	sc->de0 = fdtbus_clock_get_index(sc->phandle, 0);
	sc->output_phandle = fdtbus_get_phandle(sc->phandle, "output");
	if (sc->de0 == NULL || sc->power_phandle <= 0 || sc->output_phandle <= 0 ||
	    sc->dmat == NULL || !de352_bindings(sc)) {
		if (sc->de0 != NULL)
			clk_put(sc->de0);
		sc->de0 = NULL;
		return;
	}
	RUN_ONCE(&de352_once, de352_registry_init);
	mutex_enter(&de352_registry_lock);
	if (de352_provider != NULL) {
		mutex_exit(&de352_registry_lock);
		clk_put(sc->de0);
		sc->de0 = NULL;
		return;
	}
	de352_provider = sc;
	mutex_exit(&de352_registry_lock);
	sc->valid = true;
	sc->field_probe = of_hasprop(faa->faa_phandle, "emberbsd,scanout-field-probe");
	sc->bsp_vblank = sc->field_probe && of_hasprop(faa->faa_phandle,
	    "emberbsd,online-rcq-vblank");
	/* This diagnostic is optional. Normal display still works without it. */
	if (workqueue_create(&sc->edge.worker, "deedge", de352_edge_work, sc,
	    PRI_SOFTCLOCK, IPL_VM, WQ_MPSAFE) != 0)
		sc->edge.worker = NULL;
	if (sysctl_createv(&sc->sysctllog, 0, NULL, &node, 0, CTLTYPE_NODE,
	    device_xname(self), NULL, NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL) != 0)
		return;
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "stage", SYSCTL_DESCR("Run one retained native RGB plane"),
	    de352_sysctl, 0, &sc->stage, sizeof(sc->stage), CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "step", NULL, NULL, 0, &sc->step, 0,
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "result", NULL, NULL, 0, &sc->result, 0,
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "release_error", NULL, NULL, 0, &sc->release_error, 0,
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "memory", SYSCTL_DESCR("Complete raw memory samples"),
	    de352_sysctl_memory, 0, sc, sizeof(sc->memory), CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "cold_trace", SYSCTL_DESCR("Cached cold-init diagnostics"),
	    de352_sysctl_cold_trace, 0, sc, sizeof(sc->cold_trace),
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "run_trace", SYSCTL_DESCR("Cached step 7 diagnostics"),
	    de352_sysctl_run_trace, 0, sc, sizeof(sc->run_trace), CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "submit_trace", SYSCTL_DESCR("Cached step 8 diagnostics"),
	    de352_sysctl_submit_trace, 0, sc, sizeof(sc->submit_trace),
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "update_trace", SYSCTL_DESCR("Cached last external update"),
	    de352_sysctl_update_trace, 0, sc, sizeof(sc->update_trace),
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "output_trace", SYSCTL_DESCR("Cached output preparation"),
	    de352_sysctl_output_trace, 0, sc, sizeof(struct sun60i_a733_output_trace),
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "observe_trace",
	    SYSCTL_DESCR("Cached first failed HDMI vblank observation"),
	    de352_sysctl_observe_trace, 0, sc,
	    sizeof(struct sun60i_a733_output_observe_trace),
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_STRUCT, "phy_trace", SYSCTL_DESCR("Cached PHY I2C comparison"),
	    de352_sysctl_phy_trace, 0, sc, sizeof(struct sun60i_a733_hdmi_phy_trace),
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "verify_flips", SYSCTL_DESCR("Check all plane words and the PHY on each flip"),
	    de352_sysctl_verify_flips, 0, &sc->verify_each_flip, sizeof(int),
	    CTL_CREATE, CTL_EOL);

}

CFATTACH_DECL_NEW(sun60i_a733_de, sizeof(struct sun60i_de352_softc),
    de352_match, de352_attach, NULL, NULL);

static int
de352_state_locked(struct sun60i_de352_softc *sc,
    struct sun60i_a733_de352_state *state)
{
	KASSERT(mutex_owned(&sc->lock));
	if (!sc->valid)
		return ENXIO;
	if (sc->stage != 1)
		return EAGAIN;
	if (sc->result != 0)
		return sc->result;
	if (sc->step != 10 || !sc->retained || sc->dma == NULL ||
	    !sc->dma->published || !sc->dma->synchronized)
		return EIO;
	/* Bootstrap storage and its restricted native tag stay retained. */
	state->mode = sc->mode;
	state->dmat = sc->dma->tag;
	state->max_dma_address = UINT32_MAX;
	state->observed_sequence = sc->vblank;
	return 0;
}

int
sun60i_a733_de352_get_state(int phandle, struct sun60i_a733_de352_state *out)
{
	struct sun60i_a733_de352_state state = { 0 };
	struct sun60i_de352_softc *sc;
	int error;

	if (out == NULL)
		return EINVAL;
	sc = de352_lookup(phandle, NULL);
	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->lock);
	error = de352_state_locked(sc, &state);
	if (error == 0)
		*out = state;
	mutex_exit(&sc->lock);
	return error;
}

int
sun60i_a733_de352_acquire(int phandle, const void *owner,
    struct sun60i_a733_de352_client **out)
{
	struct sun60i_a733_de352_state state = { 0 };
	struct sun60i_de352_softc *sc;
	int error;

	if (out == NULL || owner == NULL)
		return EINVAL;
	sc = de352_lookup(phandle, NULL);
	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->lock);
	error = de352_state_locked(sc, &state);
	if (error == 0 && (sc->client.owner != NULL))
		error = EBUSY;
	if (error == 0) {
		sc->client.owner = owner;
		*out = &sc->client;
	}
	mutex_exit(&sc->lock);
	return error;
}

int
sun60i_a733_de352_release(struct sun60i_a733_de352_client *client)
{
	struct sun60i_de352_softc *sc;
	int error = 0;

	if (client == NULL || (sc = de352_lookup(0, client)) == NULL)
		return EINVAL;
	mutex_enter(&sc->lock);
	if (client->owner == NULL)
		error = EINVAL;
	else if (client->busy || client->attempted)
		error = EBUSY;
	else
		client->owner = NULL;
	mutex_exit(&sc->lock);
	return error;
}

int
sun60i_a733_de352_set_vblank_handler(struct sun60i_a733_de352_client *client,
    sun60i_a733_vblank_fn fn, void *arg)
{
	struct sun60i_a733_de352_state state = { 0 };
	struct sun60i_de352_softc *sc;
	int error;

	if (client == NULL || (fn != NULL && arg == NULL) ||
	    (sc = de352_lookup(0, client)) == NULL)
		return EINVAL;
	mutex_enter(&sc->lock);
	error = client->owner == NULL ? EINVAL : 0;
	/* A removal is allowed after a later DE failure. */
	if (error == 0 && fn != NULL)
		error = de352_state_locked(sc, &state);
	if (error == 0) {
		struct de352_edge *edge = &sc->edge;
		sun60i_a733_vblank_fn old_fn;
		void *old_arg;

		mutex_spin_enter(&edge->lock);
		old_fn = edge->consumer;
		old_arg = edge->arg;
		mutex_spin_exit(&edge->lock);
		if (old_fn == NULL && fn != NULL) {
			/* The first interrupt may arrive as soon as binding succeeds. */
			mutex_spin_enter(&edge->lock);
			edge->consumer = fn;
			edge->arg = arg;
			mutex_spin_exit(&edge->lock);
		}
		error = sun60i_a733_output_set_vblank_handler(sc->output_phandle,
		    sc, fn != NULL ? de352_edge_irq : NULL, fn != NULL ? sc : NULL);
		mutex_spin_enter(&edge->lock);
		edge->consumer = error == 0 ? fn : old_fn;
		edge->arg = error == 0 ? (fn != NULL ? arg : NULL) : old_arg;
		mutex_spin_exit(&edge->lock);
	}
	mutex_exit(&sc->lock);
	return error;
}

int
sun60i_a733_de352_detect(struct sun60i_a733_de352_client *client, bool *out)
{
	struct sun60i_de352_softc *sc;
	int error;

	if (client == NULL || out == NULL ||
	    (sc = de352_lookup(0, client)) == NULL)
		return EINVAL;
	mutex_enter(&sc->lock);
	error = client->owner == NULL ? EINVAL : 0;
	if (error == 0)
		error = sun60i_a733_output_detect(sc->output_phandle, sc, out);
	mutex_exit(&sc->lock);
	return error;
}

/* Check the owned old list before replacing any pointer. No writes here. */
static int
de352_replace_ready(struct sun60i_de352_softc *sc)
{
	const struct sun60i_a733_de352_dma *old =
	    sc->active.dma != NULL ? sc->active.dma : sc->dma;
	const struct { uint32_t reg, mask, value; } fields[] = {
		{ 0x8110, UINT32_MAX, 0 },
		{ 0x8114, UINT32_MAX, (uint32_t)old->queue.address },
		{ 0x8118, 0xff, 0 }, { 0x811c, UINT32_MAX, 32 },
		{ 0x1c1080, 0xff, 0 }, { 0x8100, 1, 1 }
	};
	uint32_t value;
	int error;

	error = sun60i_a733_ccu_display_ready(sc->de0, sc);
	if (error != 0)
		return error;
	for (u_int i = 0; i < __arraycount(fields); i++) {
		error = de352_read(sc, fields[i].reg, &value);
		if (error != 0)
			return error;
		if ((value & fields[i].mask) != fields[i].value)
			return EBUSY;
	}
	return 0;
}

static uint64_t
de352_replacement_extra(const struct sun60i_a733_output_mode *mode)
{
	const uint64_t second = UINT64_C(1000000000);
	const uint64_t cap = UINT64_C(50000000);
	uint64_t pixels, ns, quantum;

	pixels = (uint64_t)mode->htotal * mode->vtotal;
	if (pixels == 0 || pixels > UINT64_MAX / second ||
	    mode->pixel_clock_hz == 0 || hz <= 0)
		return 0;
	ns = pixels * second;
	ns = ns / mode->pixel_clock_hz + (ns % mode->pixel_clock_hz != 0);
	quantum = second / hz + (second % hz != 0);
	if (ns >= cap || quantum >= cap - ns)
		return cap;
	return ns + quantum;
}

/* Samples newer than the post-RCQ baseline retire the old storage. */
static int
de352_replacement_after(struct sun60i_de352_softc *sc,
    struct sun60i_a733_de352_completion *completion, uint64_t baseline,
    int error)
{
	struct de352_update_trace *trace = &sc->update_trace;
	uint64_t last, sequence, start, raw;
	uint64_t limit = DE352_TIMEOUT_NS, extra;
	bool irq, final_irq_sampled = false, extra_probe = false;
	bool final_extra_sampled = false;

	trace->wait_phase = UW_BASELINE;
	trace->wait_samples = 1;
	if (error != 0 && error != ENOENT)
		return error;
	trace->initial_baseline = trace->baseline =
	    trace->last_sequence = baseline;
	if (error == ENOENT)
		trace->wait_enoent = 1;
	else
		trace->last_validated = baseline;
	if (baseline < sc->vblank)
		return EPROTO;
	last = baseline;
	start = de352_now();
	trace->wait_phase = UW_WAIT;
	error = sun60i_a733_output_sample_vblank_irq(sc->output_phandle, sc,
	    &raw);
	if (error != 0 && error != EOPNOTSUPP)
		return error;
	irq = error == 0;
	extra = irq ? de352_replacement_extra(&sc->mode) : 0;
	for (;;) {
		if (extra_probe) {
			trace->elapsed_ns = de352_now() - start;
			if (trace->elapsed_ns >= DE352_TIMEOUT_NS + extra)
				return ETIMEDOUT;
		}
		if (irq) {
			if (raw < trace->last_sequence)
				return EPROTO;
			trace->last_sequence = raw;
			if (raw - baseline < 2 && !extra_probe)
				goto deadline;
		}
		trace->wait_samples++;
		error = sun60i_a733_output_observed_vblank(sc->output_phandle,
		    sc, &sequence);
		if (error != 0 && error != ENOENT)
			return error;
		if (irq && sequence < raw)
			return EPROTO;
		trace->last_sequence = sequence;
		if (error == ENOENT)
			trace->wait_enoent++;
		if (sequence < last || (error == 0 && sequence == last))
			return EPROTO;
		if (error == 0)
			trace->last_validated = sequence;
		if (error == 0 && sequence - baseline >= 2) {
			uint64_t now = de352_now();

			if (extra_probe || limit != DE352_TIMEOUT_NS)
				trace->elapsed_ns = now - start;
			completion->observed_sequence = sequence;
			completion->observed_time_ns = now;
			return 0;
		}
		if (extra_probe) {
			trace->elapsed_ns = de352_now() - start;
			if (error != 0 || sequence - baseline != 1 ||
			    trace->elapsed_ns >= DE352_TIMEOUT_NS + extra)
				return ETIMEDOUT;
			limit = DE352_TIMEOUT_NS + extra;
			extra_probe = false;
		}
		/* An unvalidated advance (for example underflow) starts a new epoch. */
		if (error == ENOENT && sequence > last) {
			if (limit != DE352_TIMEOUT_NS)
				return ETIMEDOUT;
			baseline = sequence;
			trace->baseline = baseline;
		}
		last = sequence;
deadline:
		/* Always check for accumulated IRQs before declaring expiry. */
		trace->elapsed_ns = de352_now() - start;
		if (trace->elapsed_ns >= limit) {
			if (!irq)
				return ETIMEDOUT;
			if (limit != DE352_TIMEOUT_NS) {
				if (final_extra_sampled)
					return ETIMEDOUT;
				/* One last raw sample, with no further wait. */
				final_extra_sampled = true;
			} else if (final_irq_sampled) {
				if (extra == 0 || raw - baseline != 1 ||
				    baseline > UINT64_MAX - 2 ||
				    baseline != trace->initial_baseline ||
				    trace->elapsed_ns >= DE352_TIMEOUT_NS + extra)
					return ETIMEDOUT;
				extra_probe = true;
				continue;
			} else {
				/* The worker may have been descheduled after sampling. */
				final_irq_sampled = true;
			}
		} else if (irq) {
			if (baseline <= UINT64_MAX - 2 &&
			    start <= UINT64_MAX - limit) {
				error = sun60i_a733_output_wait_vblank_irq(
				    sc->output_phandle, sc, baseline + 2,
				    start + limit, &raw);
				if (error == 0)
					continue;
				if (error != EOPNOTSUPP)
					return error;
			}
			kpause("a733vb", false, 1, NULL);
		} else {
			delay(10);
		}
		if (irq) {
			error = sun60i_a733_output_sample_vblank_irq(
			    sc->output_phandle, sc, &raw);
			if (error != 0)
				return error;
		}
	}
}

static int
de352_replacement_vblank(struct sun60i_de352_softc *sc,
    struct sun60i_a733_de352_completion *completion)
{
	uint64_t baseline = 0;
	int error;

	error = sun60i_a733_output_observed_vblank(sc->output_phandle, sc,
	    &baseline);
	return de352_replacement_after(sc, completion, baseline, error);
}

/* Sample after the wait has failed, before the owner latches the fault. */
static void
de352_sample_tv0_timeout(struct sun60i_de352_softc *sc)
{
	struct de352_tv0_timeout *t = &sc->update_trace.tv0_timeout;

	t->attempted = 1;
	t->before_error = sun60i_a733_output_sample_vblank_irq(
	    sc->output_phandle, sc, &t->raw_before);
	t->sample_error = sun60i_a733_output_sample(sc->output_phandle, sc,
	    &t->sample);
	t->after_error = sun60i_a733_output_sample_vblank_irq(
	    sc->output_phandle, sc, &t->raw_after);
}

static void
de352_frame_drop(struct de352_frame *frame, bool retired)
{
	if (frame->dma != NULL) {
		int error = retired ? sun60i_a733_de352_dma_retire(frame->dma) :
		    sun60i_a733_de352_dma_destroy(frame->dma);
		KASSERT(error == 0);
		if (error != 0)
			return; /* Preserve the pixel reference on an internal fault. */
	}
	if (frame->held)
		frame->info.release(frame->info.cookie);
}

/*
 * Require the IRQ-side observation of this RCQ ticket, independently of
 * worker scheduling and its later output check. Never infer missed edges.
 */
static bool
de352_shadow_edge(struct sun60i_de352_softc *sc, uint64_t sequence,
    struct de352_edge_record *record)
{
	struct de352_edge *edge = &sc->edge;
	const struct de352_edge_history *h = &edge->history;
	bool found = false, pending;

	mutex_spin_enter(&edge->lock);
	/* DE352 BSP prepares the event at the RCQ request and gates it on
	 * the next online idle vblank. For the explicit lab profile also
	 * require the old address at that IRQ, a fresh completed request,
	 * and all new words before the first active TV0 line. The private
	 * or fenced surface remains retained through the retirement FIFO.
	 */
	if (edge->post.count != 0) {
		const struct de352_post_phase *p = &edge->post.row[
		    (edge->post.count - 1) % DE352_EDGE_ROWS];

		if (p->ticket == edge->ticket && p->sequence == sequence &&
		    (p->reason == POST_PHASE_SAFE || (sc->bsp_vblank &&
		    p->reason == POST_PHASE_FIELD_TRANSITION &&
		    (p->first & 0x1c) == 0x1c && (p->last & 0x1c) == 0x1c &&
		    (p->before.debug & 0xffff) == ((p->before.control >> 4) & 0xffff) &&
		    (p->after.debug & 0xffff) == (p->before.debug & 0xffff)))) {
			*record = (struct de352_edge_record){
				.ticket = p->ticket, .sequence = p->sequence,
				.irq_ns = p->irq_ns, .posted_ns = p->posted_ns,
				.verified_ns = p->verified_ns,
				.first_status = p->first, .last_status = p->last,
				.matches = p->matches,
				.sample_age_ns = p->sampled_ns - p->irq_ns
			};
			mutex_spin_exit(&edge->lock);
			return true;
		}
	}
	pending = de352_edge_pending(edge, sequence) != NULL;
	mutex_spin_exit(&edge->lock);
	if (pending)
		de352_edge_measure(sc, sequence);

	mutex_spin_enter(&edge->lock);
	for (uint64_t i = 0; i < h->count && i < DE352_EDGE_ROWS; i++) {
		const struct de352_edge_record *r =
		    &h->row[(h->count - 1 - i) % DE352_EDGE_ROWS];

		if (r->ticket != edge->ticket || r->sequence < sequence)
			break;
		if (r->sequence != sequence)
			continue;
		if (r->sample_age_ns != 0 && r->matches == 7 &&
		    (r->first_status & 0x14) == 4 &&
		    (r->last_status & 0x14) == 4 && r->posted_ns != 0 &&
		    r->posted_ns <= r->verified_ns &&
		    r->verified_ns <= r->irq_ns) {
			*record = *r;
			found = true;
		}
		break;
	}
	mutex_spin_exit(&edge->lock);
	return found;
}

/* ENOENT means take the ordinary replacement barrier. */
static int
de352_shadow_vblank(struct sun60i_de352_softc *sc,
    struct sun60i_a733_de352_completion *completion, uint64_t *baseline,
    int *baseline_error, bool address_only)
{
	struct de352_edge_record record;
	uint64_t generation, validated, limit;
	int error;

	if (sc->edge.consumer == NULL)
		return ENOENT;
	error = sun60i_a733_output_sample_vblank_irq(sc->output_phandle, sc,
	    &generation);
	if (error != 0)
		return error == EOPNOTSUPP ? ENOENT : error;
	if (generation <= sc->vblank || !de352_shadow_edge(sc, generation,
	    &record)) {
		if (generation == UINT64_MAX)
			return EOVERFLOW;
		limit = de352_now() + de352_replacement_extra(&sc->mode);
		error = sun60i_a733_output_wait_vblank_irq(sc->output_phandle,
		    sc, generation + 1, limit, &generation);
		if (error != 0)
			return error == ETIMEDOUT || error == EOPNOTSUPP ?
			    ENOENT : error;
		if (generation <= sc->vblank ||
		    !de352_shadow_edge(sc, generation, &record))
			return ENOENT;
	}
	/* The ticket, current online queue, copied address and TV0 phase
	 * identify this field. Its timestamp comes from the real interrupt. */
	if (address_only || (generation == *baseline && *baseline_error == 0)) {
		validated = generation;
	} else {
		error = sun60i_a733_output_observed_vblank(sc->output_phandle,
		    sc, &validated);
		if (error == ENOENT && validated > *baseline) {
			*baseline = validated;
			*baseline_error = error;
		}
		if (error != 0)
			return error;
	}
	if (validated < generation)
		return EPROTO;
	if (validated != generation &&
	    !de352_shadow_edge(sc, validated, &record))
		return ENOENT;
	completion->observed_sequence = record.sequence;
	completion->observed_time_ns = record.irq_ns;
	return 0;
}

/*
 * Called after successful DE memory validation. Two distinctly verified
 * successor edges retire a shadow. The ordinary post-replacement barrier
 * is newer than every queued entry and also covers them.
 */
static unsigned
de352_shadow_collect(struct sun60i_de352_softc *sc, uint64_t sequence,
    bool fast, struct de352_frame *retired)
{
	unsigned kept = 0, n = 0;

	for (unsigned i = 0; i < sc->shadow_retire_count; i++) {
		struct de352_shadow_retire entry = sc->shadow_retire[i];

		if (fast && sequence > entry.sequence) {
			entry.sequence = sequence;
			entry.edges++;
		}
		if (!fast || entry.edges >= 2)
			retired[n++] = entry.frame;
		else
			sc->shadow_retire[kept++] = entry;
	}
	for (unsigned i = kept; i < sc->shadow_retire_count; i++)
		memset(&sc->shadow_retire[i], 0, sizeof(sc->shadow_retire[i]));
	sc->shadow_retire_count = kept;
	return n;
}

static bool
de352_shadow_alias(const struct de352_frame *a, const struct de352_frame *b)
{
	if (a->dma == NULL || b->dma == NULL || a->dma->disabled ||
	    b->dma->disabled)
		return false;
	return a->dma->pixels.address <
	    (uint64_t)b->dma->pixels.address + b->dma->pixels.bytes &&
	    b->dma->pixels.address <
	    (uint64_t)a->dma->pixels.address + a->dma->pixels.bytes;
}

static bool
de352_shadow_held(const struct sun60i_de352_softc *sc,
    const struct de352_frame *next)
{
	if ((next->shadow || sc->active.shadow) &&
	    de352_shadow_alias(next, &sc->active))
		return true;
	for (unsigned i = 0; i < sc->shadow_retire_count; i++)
		if (de352_shadow_alias(next, &sc->shadow_retire[i].frame))
			return true;
	return false;
}

static int
de352_update(struct sun60i_a733_de352_client *client,
    const struct sun60i_a733_de352_frame *frame,
    struct sun60i_a733_de352_completion *out, bool stop, bool shadow)
{
	struct sun60i_a733_de352_completion completion = { 0 };
	struct sun60i_a733_de352_state state = { 0 };
	struct de352_frame next = { .shadow = shadow };
	struct de352_frame retired[DE352_SHADOW_RETIRE_MAX + 1];
	unsigned retired_count = 0;
	bool fast = false, finish_releases;
	struct sun60i_de352_softc *sc;
	int error;

	if (client == NULL || out == NULL ||
	    (frame != NULL && (frame->retain == NULL ||
	    frame->release == NULL || frame->map == NULL)))
		return EINVAL;
	sc = de352_lookup(0, client);
	if (sc == NULL)
		return EINVAL;
	if (frame != NULL)
		next.info = *frame;
	mutex_enter(&sc->lock);
	error = de352_state_locked(sc, &state);
	if (error == 0 && (client->owner == NULL || client->busy))
		error = EBUSY;
	if (error == 0 && frame != NULL && (next.info.dmat != state.dmat ||
	    next.info.width != state.mode.hdisplay ||
	    next.info.height != state.mode.vdisplay))
		error = EINVAL;
	if (error != 0) {
		mutex_exit(&sc->lock);
		return error;
	}
	if (stop && sc->output_stopped) {
		*out = (struct sun60i_a733_de352_completion){
			.observed_sequence = sc->vblank,
			.observed_time_ns = de352_now()
		};
		mutex_exit(&sc->lock);
		return 0;
	}
	client->busy = true;
	sc->update_trace = (struct de352_update_trace){
		.version = 5, .valid = 1, .phase = UT_PREPARE,
		.rcq.status_and = UINT32_MAX,
		.reg = UINT32_MAX,
		.operation = stop ? UO_STOP : frame == NULL ? UO_DISABLE : UO_FRAME
	};
	mutex_exit(&sc->lock);

	/* Both callbacks, allocation and producer lifetime are outside our lock. */
	error = frame != NULL ? next.info.retain(next.info.cookie) : 0;
	if (error == 0 && frame != NULL) {
		next.held = true;
		error = sun60i_a733_de352_dma_prepare_map(state.dmat, next.info.map,
		    next.info.bytes, next.info.width, next.info.height,
		    next.info.pitch, &next.dma);
	} else if (frame == NULL)
		error = sun60i_a733_de352_dma_prepare_disabled(state.dmat, &next.dma);
	mutex_enter(&sc->lock);
	if (error == 0 && de352_shadow_held(sc, &next))
		error = EBUSY;
	if (error == 0) {
		if (sc->output_stopped) {
			sc->update_trace.phase = UT_START;
			error = sun60i_a733_output_start(sc->output_phandle, sc);
			if (error == 0)
				sc->output_stopped = false;
		}
		if (error == 0) {
			sc->update_trace.phase = UT_READY;
			error = de352_replace_ready(sc);
		}
		if (error == 0) {
			sc->pending = next;
			sc->update_trace.phase = UT_SUBMIT;
			error = de352_submit_dma(sc, next.dma, false);
		}
		if (next.dma->published)
			client->attempted = true;
		if (error == 0) {
			sc->update_trace.phase = UT_VBLANK;
			if (shadow && (sc->active.dma == NULL ||
			    sc->active.shadow) &&
			    sc->shadow_retire_count < DE352_SHADOW_RETIRE_MAX) {
				uint64_t baseline = 0;
				const bool address_only = de352_address_flip(sc, next.dma);
				int sample = ENOENT;

				/* A qualified address-only update uses the ticket
				 * observed at TV0. An uncertain edge still takes
				 * the full PHY check and two-field retirement. */
				error = 0;
				if (!address_only) {
					sample = sun60i_a733_output_observed_vblank(
					    sc->output_phandle, sc, &baseline);
					error = sample;
				}
				if (error == 0 || error == ENOENT)
					error = de352_shadow_vblank(sc, &completion,
					    &baseline, &sample, address_only);
				fast = error == 0;
				if (error == ENOENT && address_only)
					error = de352_replacement_vblank(sc, &completion);
				else if (error == ENOENT)
					error = de352_replacement_after(sc, &completion,
					    baseline, sample);
			} else {
				error = de352_replacement_vblank(sc, &completion);
			}
			if (error == ETIMEDOUT) {
				(void)sun60i_a733_output_cached_tv0_trace(
				    sc->output_phandle, &sc->update_trace.tv0);
				de352_sample_tv0_timeout(sc);
			}
		}
		if (error == 0) {
			sc->update_trace.phase = UT_MEMORY;
			error = de352_memory(sc);
		}
		if (error == 0 && stop) {
			sc->update_trace.phase = UT_STOP;
			error = sun60i_a733_output_stop(sc->output_phandle, sc);
			if (error == 0)
				sc->output_stopped = true;
		}
		if (error != 0) {
			de352_edge_disarm(sc);
			sc->result = error;
			(void)sun60i_a733_ccu_display_fault(sc->de0, sc, error);
			(void)sun60i_a733_output_fault(sc->output_phandle, sc, error);
		} else {
			retired_count = de352_shadow_collect(sc,
			    completion.observed_sequence, fast, retired);
			if (sc->active.dma != NULL) {
				if (fast) {
					struct de352_shadow_retire *entry;

					KASSERT(sc->active.shadow);
					KASSERT(sc->shadow_retire_count <
					    DE352_SHADOW_RETIRE_MAX);
					entry = &sc->shadow_retire[
					    sc->shadow_retire_count++];
					*entry = (struct de352_shadow_retire){
						.frame = sc->active,
						.sequence = completion.observed_sequence
					};
				} else {
					retired[retired_count++] = sc->active;
				}
			}
			sc->active = next;
			sc->vblank = completion.observed_sequence;
			completion.shadow_fast = fast;
			*out = completion;
		}
	}
	if (error == 0)
		sc->update_trace.phase = UT_DONE;
	sc->update_trace.result = error;
	sc->update_trace.valid = 3;
	if (error == 0 || next.dma == NULL || !next.dma->published)
		memset(&sc->pending, 0, sizeof(sc->pending));
	de352_edge_disarm(sc);
	finish_releases = shadow;
	for (unsigned i = 0; i < retired_count; i++)
		finish_releases |= retired[i].shadow;
	if (!finish_releases)
		client->busy = false;
	mutex_exit(&sc->lock);

	if (error != 0 && (next.dma == NULL || !next.dma->published))
		de352_frame_drop(&next, false);
	for (unsigned i = 0; i < retired_count; i++)
		de352_frame_drop(&retired[i], true);
	if (finish_releases) {
		mutex_enter(&sc->lock);
		client->busy = false;
		mutex_exit(&sc->lock);
	}
	return error;
}

int
sun60i_a733_de352_submit(struct sun60i_a733_de352_client *client,
    const struct sun60i_a733_de352_frame *frame,
    struct sun60i_a733_de352_completion *out)
{
	if (frame == NULL)
		return EINVAL;
	return de352_update(client, frame, out, false, false);
}

int
sun60i_a733_de352_submit_shadow(struct sun60i_a733_de352_client *client,
    const struct sun60i_a733_de352_frame *frame,
    struct sun60i_a733_de352_completion *out)
{
	if (frame == NULL)
		return EINVAL;
	return de352_update(client, frame, out, false, true);
}

int
sun60i_a733_de352_observe_current(struct sun60i_a733_de352_client *client,
    struct sun60i_a733_de352_completion *out)
{
	struct sun60i_a733_de352_completion done = { 0 };
	struct sun60i_a733_de352_state state;
	struct sun60i_de352_softc *sc;
	int error;

	if (client == NULL || out == NULL ||
	    (sc = de352_lookup(0, client)) == NULL)
		return EINVAL;
	mutex_enter(&sc->lock);
	error = de352_state_locked(sc, &state);
	if (error == 0 && (client->owner == NULL || client->busy ||
	    sc->output_stopped || sc->active.dma == NULL ||
	    sc->active.dma->disabled))
		error = EBUSY;
	if (error == 0)
		error = de352_replacement_vblank(sc, &done);
	if (error == 0)
		error = de352_memory(sc);
	if (error == 0) {
		sc->vblank = done.observed_sequence;
		*out = done;
	}
	mutex_exit(&sc->lock);
	return error;
}

int
sun60i_a733_de352_disable_plane(struct sun60i_a733_de352_client *client,
    struct sun60i_a733_de352_completion *out)
{
	return de352_update(client, NULL, out, false, false);
}

int
sun60i_a733_de352_stop(struct sun60i_a733_de352_client *client,
    struct sun60i_a733_de352_completion *out)
{
	return de352_update(client, NULL, out, true, false);
}
