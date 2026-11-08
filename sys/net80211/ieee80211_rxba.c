/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Bounded adaptation of FreeBSD net80211 RX BA sequence/window semantics,
 * d2145e9b96448da34d3cfede24c509af6d830c92 ieee80211_ht.c,
 * BSD-2-Clause, Copyright (c) 2007-2008 Sam Leffler, Errno Consulting.
 * No vap, LinuxKPI, M_AMPDU, or hardware BlockAck claim is imported.
 * The originator (ieee80211_txba_*) is original: IEEE Std 802.11-2016
 * 10.24.2 ADDBA exchange and 9.6.5 Block Ack Action frames only. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mbuf.h>
#include <sys/stdint.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net80211/ieee80211_var.h>

/* Reserve the inline owner state as well as every retained allocation. */
#define RXBA_HEAP_LIMIT (IEEE80211_RXBA_BUDGET - sizeof(struct ieee80211_rxba_state))

struct rxba_slot {
	struct mbuf *m, *tail;
	uint64_t arrived, epoch, key_epoch;
	uint16_t seq;
	size_t charged;
};
/* Both owners use these window operations. Emission is a private continuation:
 * raw input resumes net80211; offload only appends to the caller's output. */
typedef void (*rxba_emit_fn)(void *, struct rxba_slot *);
struct rxba_raw_owner {
	struct ieee80211com *ic;
	struct ieee80211_node *ni;
};
struct ieee80211_rxba_tid {
	struct rxba_slot slot[IEEE80211_RXBA_WINDOW];
	uint64_t last_rx;
	uint16_t head, window, request_params, timeout, request_ssn;
	uint8_t token;
};
struct ieee80211_rxba_candidate {
	struct ieee80211_rxba_tid *fresh;
	uint64_t epoch, key_epoch, deadline, timer_token;
	uint32_t attempt;
	uint16_t params, timeout;
	uint8_t token, tid;
	int committed;
};

static uint16_t
rxba_le16(const uint8_t *p)
{
	return p[0] | (uint16_t)p[1] << 8;
}

static unsigned
rxba_delta(unsigned seq, unsigned head)
{
	return (seq - head) & 4095;
}

int
ieee80211_rxba_ready(struct ieee80211com *ic)
{
	const struct ieee80211_rxba_ops *ops = ic->ic_rxba_ops;

	return ops != NULL && ops->now_ms != NULL && ops->schedule != NULL &&
	    ops->cancel != NULL && ic->ic_rxba_serial != UINT64_MAX;
}

static uint64_t
rxba_serial(struct ieee80211com *ic)
{

	if (ic->ic_rxba_serial >= UINT64_MAX - 1) {
		ic->ic_rxba_serial = UINT64_MAX;
		return 0;
	}
	return ++ic->ic_rxba_serial;
}

/* Count retained kernel storage, not just payload. Shared external buffers are
 * conservatively charged for each reference. Bound both chain and tag walks. */
static size_t
rxba_cost(const struct mbuf *m)
{
	const struct mbuf *part;
	const struct m_tag *tag;
	size_t cost = 0, wire = 0, amount;
	unsigned segments = 0, tags = 0;

	if ((m->m_flags & M_PKTHDR) == 0 || m->m_pkthdr.len < 0)
		return 0;
	for (part = m; part != NULL; part = part->m_next) {
		if (++segments > IEEE80211_RXBA_CHAIN_MAX || part->m_len < 0 ||
		    (size_t)part->m_len > (size_t)m->m_pkthdr.len - wire)
			return 0;
		wire += part->m_len;
		amount = sizeof(*part);
		if ((part->m_flags & M_EXT) != 0) {
			if (part->m_ext.ext_size < (size_t)part->m_len ||
			    part->m_ext.ext_size > RXBA_HEAP_LIMIT - amount)
				return 0;
			amount += part->m_ext.ext_size;
		}
		if (amount > RXBA_HEAP_LIMIT - cost)
			return 0;
		cost += amount;
		if ((part->m_flags & M_PKTHDR) == 0)
			continue;
		SLIST_FOREACH(tag, &part->m_pkthdr.tags, m_tag_link) {
			if (++tags > IEEE80211_RXBA_TAG_MAX)
				return 0;
			amount = sizeof(*tag) + tag->m_tag_len;
			if (amount > RXBA_HEAP_LIMIT - cost)
				return 0;
			cost += amount;
		}
	}
	return wire == (size_t)m->m_pkthdr.len ? cost : 0;
}

static void
rxba_drop(struct ieee80211_rxba_state *ba, struct rxba_slot *slot)
{

	if (slot->m == NULL)
		return;
	KASSERT(ba->bytes >= slot->charged);
	ba->bytes -= slot->charged;
	m_freem(slot->m);
	memset(slot, 0, sizeof(*slot));
}

static void
rxba_free_tid(struct ieee80211_rxba_state *ba, unsigned tid)
{
	struct ieee80211_rxba_tid *r = ba->tid[tid];
	unsigned i;

	if (r == NULL)
		return;
	for (i = 0; i < IEEE80211_RXBA_WINDOW; i++)
		rxba_drop(ba, &r->slot[i]);
	ba->tid[tid] = NULL;
	KASSERT(ba->bytes >= sizeof(*r));
	ba->bytes -= sizeof(*r);
	kmem_intr_free(r, sizeof(*r));
}

void
ieee80211_rxba_reset(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;
	unsigned i;

	if (ic != NULL && ni == ic->ic_bss && ba->timer_token != 0 &&
	    ic->ic_rxba_ops != NULL && ic->ic_rxba_ops->cancel != NULL)
		ic->ic_rxba_ops->cancel(ic);
	ba->timer_token = 0;
	for (i = 0; i < IEEE80211_RXBA_TIDS; i++)
		rxba_free_tid(ba, i);
	ba->epoch = ba->key_epoch = 0;
	memset(ba->tx, 0, sizeof(ba->tx));
	ba->tx_attempt = 0;
	KASSERT(ba->bytes == 0);
}

static void
rxba_withdraw(struct ieee80211com *ic, struct ieee80211_node *ni)
{

	/* Preflight may have armed a token that is not yet published in ba. */
	if (ic->ic_rxba_ops != NULL && ic->ic_rxba_ops->cancel != NULL)
		ic->ic_rxba_ops->cancel(ic);
	ieee80211_ht20_reset(ni);
	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
}

static int
rxba_current(struct ieee80211com *ic, struct ieee80211_node *ni)
{

	return ieee80211_rxba_ready(ic) && ic->ic_state == IEEE80211_S_RUN &&
	    ni == ic->ic_bss && ni->ni_ht20.valid &&
	    ieee80211_node_is_authorized(ni) &&
	    ieee80211_ht20_state_request(ic, IEEE80211_S_RUN) == 0;
}

/*
 * A negotiated peer without any block-ack session that is not yet in RUN
 * or not yet authorized: an AP commonly requests block ack right after
 * association, before a deferred driver RUN or the key handshake. That is
 * premature, not a contract violation.
 */
static int
rxba_premature(struct ieee80211com *ic, struct ieee80211_node *ni)
{

	return ni == ic->ic_bss && ni->ni_ht20.valid && ni->ni_rxba.epoch == 0 &&
	    (ic->ic_state == IEEE80211_S_ASSOC ||
	     (ic->ic_state == IEEE80211_S_RUN &&
	      !ieee80211_node_is_authorized(ni)));
}

static int
rxba_require_current(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	int owned = ni == ic->ic_bss &&
	    (ni->ni_rxba.epoch != 0 || ni->ni_ht20.valid);

	if (rxba_current(ic, ni))
		return 1;
	/* The profile check can already have purged BA. Retain the prior owner
	 * decision so a rejected active peer cannot silently continue as legacy. */
	if (owned)
		rxba_withdraw(ic, ni);
	return 0;
}

unsigned
ieee80211_rxba_mask(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	unsigned tid, mask = 0;

	/* A query before RUN or authorization, with no session, is empty;
	 * the driver asks after every received frame. */
	if (rxba_premature(ic, ni) || !rxba_require_current(ic, ni))
		return 0;
	for (tid = 0; tid < IEEE80211_RXBA_TIDS; tid++)
		if (ni->ni_rxba.tid[tid] != NULL)
			mask |= 1U << tid;
	return mask;
}

/* Idle a TID for the retry interval; an unusable clock never retries. */
static void
txba_idle(struct ieee80211_txba_tid *t, uint64_t now)
{

	t->state = IEEE80211_TXBA_IDLE;
	t->window = 0;
	t->deadline = now == UINT64_MAX ||
	    now > UINT64_MAX - IEEE80211_TXBA_RETRY_MS ? UINT64_MAX :
	    now + IEEE80211_TXBA_RETRY_MS;
}

int
ieee80211_txba_request(struct ieee80211com *ic, struct ieee80211_node *ni,
    unsigned mask)
{
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;
	struct ieee80211_txba_tid *t;
	uint64_t now;
	unsigned tid, ssn;
	int error;

	if (ni != ic->ic_bss || !rxba_current(ic, ni))
		return EINVAL;
	now = ic->ic_rxba_ops->now_ms(ic);
	if (now == UINT64_MAX || now > UINT64_MAX - IEEE80211_TXBA_RETRY_MS)
		return EOVERFLOW;
	if (ba->tx_attempt != ni->ni_ht20.attempt) {
		memset(ba->tx, 0, sizeof(ba->tx));
		ba->tx_attempt = ni->ni_ht20.attempt;
	}
	for (tid = 0; tid < IEEE80211_RXBA_TIDS; tid++) {
		t = &ba->tx[tid];
		if (t->state == IEEE80211_TXBA_PENDING && now >= t->deadline) {
			ba->tx_diag.timeouts++;
			txba_idle(t, now);
		}
		if ((mask & (1U << tid)) == 0 || t->state != IEEE80211_TXBA_IDLE ||
		    t->attempts >= IEEE80211_TXBA_ATTEMPTS || now < t->deadline)
			continue;
		if (++ba->tx_token == 0)
			ba->tx_token = 1;
		/* The next sequence number encap assigns to this TID. */
		ssn = ni->ni_txseqs[tid + 1] & 4095;
		error = ieee80211_send_txba_request(ic, ni, ba->tx_token,
		    2 | tid << 2 | IEEE80211_TXBA_WINDOW << 6, 0, ssn << 4);
		if (error != 0)
			return error;
		t->state = IEEE80211_TXBA_PENDING;
		t->token = ba->tx_token;
		t->attempts++;
		t->deadline = now + IEEE80211_TXBA_RESPONSE_MS;
		ba->tx_diag.requests++;
	}
	return 0;
}

unsigned
ieee80211_txba_window(struct ieee80211com *ic, struct ieee80211_node *ni,
    unsigned tid)
{
	const struct ieee80211_txba_tid *t;

	if (ni == NULL || ni != ic->ic_bss || tid >= IEEE80211_RXBA_TIDS ||
	    ic->ic_state != IEEE80211_S_RUN || !ni->ni_ht20.valid ||
	    ni->ni_rxba.tx_attempt != ni->ni_ht20.attempt)
		return 0;
	t = &ni->ni_rxba.tx[tid];
	return t->state == IEEE80211_TXBA_ACTIVE ? t->window : 0;
}

/* The recipient's ADDBA Response to our outstanding request for its TID. */
static int
txba_response(struct ieee80211com *ic, struct ieee80211_node *ni,
    const uint8_t *p, size_t len)
{
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;
	struct ieee80211_txba_tid *t;
	unsigned status, params, tid, window;

	if (len != 9)
		return EINVAL;
	status = rxba_le16(p + 3);
	params = rxba_le16(p + 5);
	tid = (params >> 2) & 15;
	if (tid >= IEEE80211_RXBA_TIDS || ba->tx_attempt != ni->ni_ht20.attempt ||
	    ba->tx[tid].state != IEEE80211_TXBA_PENDING ||
	    ba->tx[tid].token != p[2]) {
		ba->tx_diag.invalid++;
		return EINVAL;
	}
	t = &ba->tx[tid];
	ba->tx_diag.last_status = (uint16_t)status;
	window = params >> 6;
	/* A recipient timeout needs no state here: it ends with its DELBA. */
	if (status != 0 || (params & 2) == 0 || window == 0) {
		ba->tx_diag.refusals++;
		txba_idle(t, ic->ic_rxba_ops->now_ms(ic));
		return 0;
	}
	t->window = (uint16_t)MIN(window, IEEE80211_TXBA_WINDOW);
	t->state = IEEE80211_TXBA_ACTIVE;
	ba->tx_diag.accepts++;
	return 0;
}

static void
rxba_deliver(void *arg, struct rxba_slot *slot)
{
	struct rxba_raw_owner *owner = arg;
	struct ieee80211com *ic = owner->ic;
	struct ieee80211_node *ni = owner->ni;
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;
	struct mbuf *m = slot->m;
	int current;

	if (m == NULL)
		return;
	current = slot->epoch == ba->epoch && slot->key_epoch == ba->key_epoch;
	KASSERT(ba->bytes >= slot->charged);
	ba->bytes -= slot->charged;
	memset(slot, 0, sizeof(*slot));
	if (current)
		ieee80211_input_ordered(ic, ni, m);
	else
		m_freem(m);
}

static void
rxba_contiguous(struct ieee80211_rxba_tid *r, rxba_emit_fn emit, void *arg)
{
	unsigned n;

	for (n = 0; n < r->window; n++) {
		struct rxba_slot *s = &r->slot[r->head % IEEE80211_RXBA_WINDOW];
		if (s->m == NULL || s->seq != r->head)
			break;
		emit(arg, s);
		r->head = (r->head + 1) & 4095;
	}
}

static void
rxba_advance(struct ieee80211_rxba_tid *r, unsigned count,
    rxba_emit_fn emit, void *arg)
{
	unsigned n, limit = MIN(count, r->window);

	/* Deliver only held slots. A hostile SSN cannot make an unbounded loop. */
	for (n = 0; n < limit; n++) {
		struct rxba_slot *s = &r->slot[r->head % IEEE80211_RXBA_WINDOW];
		if (s->m != NULL && s->seq == r->head)
			emit(arg, s);
		r->head = (r->head + 1) & 4095;
	}
	r->head = (r->head + count - limit) & 4095;
}

static int
rxba_deadline(struct ieee80211_rxba_tid *r, unsigned hole_ms,
    uint64_t *deadline)
{
	uint64_t oldest = UINT64_MAX, next = UINT64_MAX, span;
	unsigned i;

	for (i = 0; i < IEEE80211_RXBA_WINDOW; i++)
		if (r->slot[i].m != NULL && r->slot[i].arrived < oldest)
			oldest = r->slot[i].arrived;
	if (oldest != UINT64_MAX) {
		if (oldest >= UINT64_MAX - hole_ms)
			return EOVERFLOW;
		next = oldest + hole_ms;
	}
	if (r->timeout != 0) {
		span = ((uint64_t)r->timeout * 1024 + 999) / 1000;
		if (r->last_rx > UINT64_MAX - span)
			return EOVERFLOW;
		if (r->last_rx + span < next)
			next = r->last_rx + span;
	}
	*deadline = next;
	return 0;
}

/* Reject late/duplicate and exactly half-space sequence numbers. Retain the
 * complete incoming slot only after releasing any window displacement. */
static int
rxba_insert(struct ieee80211_rxba_tid *r, const struct rxba_slot *incoming,
    size_t *bytes, size_t limit, rxba_emit_fn emit, void *arg)
{
	struct rxba_slot *slot;
	unsigned delta = rxba_delta(incoming->seq, r->head);

	if (delta >= 2048)
		return EINVAL;
	if (delta >= r->window)
		rxba_advance(r, delta - r->window + 1, emit, arg);
	slot = &r->slot[incoming->seq % IEEE80211_RXBA_WINDOW];
	if (slot->m != NULL)
		return EALREADY;
	if (incoming->charged == 0 || *bytes > limit ||
	    incoming->charged > limit - *bytes)
		return ENOBUFS;
	*slot = *incoming;
	*bytes += slot->charged;
	rxba_contiguous(r, emit, arg);
	return 0;
}

static unsigned
rxba_expire(struct ieee80211_rxba_tid *r, uint64_t now, unsigned hole_ms,
    rxba_emit_fn emit, void *arg)
{
	uint64_t oldest;
	unsigned n, i, first, delta, gaps = 0;

	for (n = 0; n < IEEE80211_RXBA_WINDOW; n++) {
		oldest = UINT64_MAX;
		first = IEEE80211_RXBA_WINDOW;
		for (i = 0; i < IEEE80211_RXBA_WINDOW; i++) {
			struct rxba_slot *s = &r->slot[i];
			if (s->m == NULL)
				continue;
			if (s->arrived < oldest)
				oldest = s->arrived;
			delta = rxba_delta(s->seq, r->head);
			if (delta < first)
				first = delta;
		}
		if (oldest == UINT64_MAX || now < oldest || now - oldest < hole_ms)
			break;
		KASSERT(first < r->window);
		rxba_advance(r, first, emit, arg);
		rxba_contiguous(r, emit, arg);
		gaps++;
	}
	return gaps;
}

static int
rxba_reschedule(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;
	uint64_t next = UINT64_MAX, deadline;
	unsigned i;
	int error;

	if (!ieee80211_rxba_ready(ic))
		goto invalid;
	for (i = 0; i < IEEE80211_RXBA_TIDS; i++) {
		if (ba->tid[i] == NULL)
			continue;
		if (rxba_deadline(ba->tid[i], IEEE80211_RXBA_HOLE_MS, &deadline) != 0)
			goto invalid;
		if (deadline < next)
			next = deadline;
	}
	if (next == UINT64_MAX) {
		ba->timer_token = 0;
		ic->ic_rxba_ops->cancel(ic);
		return 0;
	}
	ba->timer_token = rxba_serial(ic);
	if (ba->timer_token == 0)
		goto invalid;
	error = ic->ic_rxba_ops->schedule(ic, next, ba->timer_token);
	if (error == 0)
		return 0;
invalid:
	rxba_withdraw(ic, ni);
	return EINVAL;
}

void
ieee80211_rxba_key_change(struct ieee80211com *ic, struct ieee80211_key *key)
{
	struct ieee80211_node *ni = ic->ic_bss;
	struct ieee80211_rxba_state *ba;
	unsigned tid, i;

	if (ic->ic_opmode != IEEE80211_M_STA || ni == NULL || key != &ni->ni_ucastkey)
		return;
	ba = &ni->ni_rxba;
	if (ba->epoch == 0)
		return; /* No queued generation exists on the legacy/no-BA path. */
	for (tid = 0; tid < IEEE80211_RXBA_TIDS; tid++)
		if (ba->tid[tid] != NULL)
			for (i = 0; i < IEEE80211_RXBA_WINDOW; i++)
				rxba_drop(ba, &ba->tid[tid]->slot[i]);
	ba->key_epoch = rxba_serial(ic);
	if (ba->key_epoch == 0)
		rxba_withdraw(ic, ni);
	else if (ba->epoch != 0)
		(void)rxba_reschedule(ic, ni);
}

int
ieee80211_rxba_input(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct mbuf *m)
{
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;
	struct ieee80211_rxba_tid *r;
	struct rxba_slot incoming = {0};
	struct rxba_raw_owner owner = { ic, ni };
	const uint8_t *p = mtod(m, const uint8_t *);
	uint64_t now;
	unsigned tid, seq;
	size_t cost;

	if (m->m_len < 26 || (p[0] & 0x8f) != 0x88 || (p[1] & 3) != 2 ||
	    IEEE80211_IS_MULTICAST(p + 4))
		return 0;
	tid = p[24] & 15;
	/* Standalone negotiated A-MSDU also owns a current HT/security capsule,
	 * even when the AP never requested a receive BA window. */
	if ((ba->epoch != 0 || ((p[24] & IEEE80211_QOS_AMSDU) != 0 &&
	    (ni->ni_flags & IEEE80211_NODE_AMSDU_RX) != 0)) &&
	    !rxba_require_current(ic, ni))
		goto drop;
	if (tid >= IEEE80211_RXBA_TIDS || ba->tid[tid] == NULL)
		return 0;
	if (!rxba_require_current(ic, ni)) {
		ieee80211_rxba_reset(ic, ni);
		goto drop;
	}
	r = ba->tid[tid];
	if ((p[1] & (IEEE80211_FC1_MORE_FRAG | IEEE80211_FC1_ORDER)) != 0 ||
	    (p[22] & 15) != 0 || m->m_pkthdr.len < 26 || m->m_pkthdr.len > 4096 ||
	    !IEEE80211_ADDR_EQ(p + 4, ic->ic_myaddr) ||
	    !IEEE80211_ADDR_EQ(p + 10, ni->ni_bssid))
		goto drop;
	now = ic->ic_rxba_ops->now_ms(ic);
	if (now == UINT64_MAX || now < r->last_rx) {
		rxba_withdraw(ic, ni);
		goto drop;
	}
	r->last_rx = now;
	seq = rxba_le16(p + 22) >> 4;
	cost = rxba_cost(m);
	if (cost == 0)
		goto drop;
	incoming.m = incoming.tail = m;
	incoming.charged = cost;
	incoming.seq = seq;
	incoming.arrived = now;
	incoming.epoch = ba->epoch;
	incoming.key_epoch = ba->key_epoch;
	if (rxba_insert(r, &incoming, &ba->bytes, RXBA_HEAP_LIMIT,
	    rxba_deliver, &owner) != 0)
		goto drop;
	(void)rxba_reschedule(ic, ni);
	return 1;
drop:
	m_freem(m);
	return 1;
}

int
ieee80211_rxba_bar(struct ieee80211com *ic, struct ieee80211_node *ni,
    const uint8_t *p, size_t len)
{
	struct ieee80211_rxba_tid *r;
	struct rxba_raw_owner owner = { ic, ni };
	uint64_t now;
	unsigned control, tid, seq, delta;

	if (p == NULL || len != 20 || p[0] != 0x84 || (p[1] & ~IEEE80211_FC1_RETRY) != 0 ||
	    !IEEE80211_ADDR_EQ(p + 4, ic->ic_myaddr) ||
	    !IEEE80211_ADDR_EQ(p + 10, ni->ni_bssid) || !rxba_require_current(ic, ni))
		return EINVAL;
	control = rxba_le16(p + 16);
	tid = control >> 12;
	if ((control & 0xfff) != 4 || tid >= IEEE80211_RXBA_TIDS ||
	    (p[18] & 15) != 0 || (r = ni->ni_rxba.tid[tid]) == NULL)
		return EOPNOTSUPP;
	now = ic->ic_rxba_ops->now_ms(ic);
	if (now == UINT64_MAX || now < r->last_rx) {
		rxba_withdraw(ic, ni);
		return EINVAL;
	}
	seq = rxba_le16(p + 18) >> 4;
	delta = rxba_delta(seq, r->head);
	if (delta >= 2048)
		return EINVAL;
	r->last_rx = now;
	rxba_advance(r, delta, rxba_deliver, &owner);
	rxba_contiguous(r, rxba_deliver, &owner);
	return rxba_reschedule(ic, ni);
}

void
ieee80211_rxba_age(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint64_t token)
{
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;
	struct ieee80211_rxba_tid *r;
	struct rxba_raw_owner owner = { ic, ni };
	uint64_t now, span;
	unsigned tid;

	if (token == 0 || token != ba->timer_token)
		return;
	if (!rxba_require_current(ic, ni)) {
		ieee80211_rxba_reset(ic, ni);
		return;
	}
	now = ic->ic_rxba_ops->now_ms(ic);
	for (tid = 0; tid < IEEE80211_RXBA_TIDS; tid++) {
		r = ba->tid[tid];
		if (r == NULL)
			continue;
		if (now == UINT64_MAX || now < r->last_rx) {
			rxba_withdraw(ic, ni);
			return;
		}
		span = ((uint64_t)r->timeout * 1024 + 999) / 1000;
		if (r->timeout != 0 && now - r->last_rx >= span) {
			rxba_free_tid(ba, tid);
			continue;
		}
		(void)rxba_expire(r, now, IEEE80211_RXBA_HOLE_MS,
		    rxba_deliver, &owner);
	}
	(void)rxba_reschedule(ic, ni);
}

int
ieee80211_rxba_response_check(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct mbuf *m, struct ieee80211_rxba_candidate *c)
{
	const uint8_t *p = mtod(m, const uint8_t *);
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;

	if (c == NULL)
		return 0;
	if (!rxba_require_current(ic, ni) || c->attempt != ni->ni_ht20.attempt ||
	    c->epoch != ba->epoch || c->key_epoch != ba->key_epoch ||
	    m->m_len != 33 || m->m_pkthdr.len != 33 || p[0] != 0xd0 || p[1] != 0 ||
	    !IEEE80211_ADDR_EQ(p + 4, ni->ni_macaddr) ||
	    !IEEE80211_ADDR_EQ(p + 10, ic->ic_myaddr) ||
	    !IEEE80211_ADDR_EQ(p + 16, ni->ni_bssid) ||
	    p[24] != 3 || p[25] != 1 || p[26] != c->token ||
	    rxba_le16(p + 27) != 0 || rxba_le16(p + 29) != c->params ||
	    rxba_le16(p + 31) != c->timeout)
		return EINVAL;
	return 0;
}

void
ieee80211_rxba_response_commit(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_rxba_candidate *c)
{
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;

	KASSERT(c != NULL && !c->committed);
	if (c->fresh != NULL) {
		rxba_free_tid(ba, c->tid);
		ba->bytes += sizeof(*c->fresh);
		ba->tid[c->tid] = c->fresh;
		c->fresh = NULL;
	}
	c->committed = 1;
	/* Preflight scheduled this token before queue publication. Softintr cannot
	 * consume it until this Giant-held producer returns. Commit cannot fail. */
	ba->timer_token = c->timer_token;
	if (c->deadline == UINT64_MAX)
		ic->ic_rxba_ops->cancel(ic);
}

static int
rxba_candidate_schedule(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_rxba_candidate *c)
{
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;
	uint64_t next = UINT64_MAX, deadline;
	unsigned tid;
	int error;

	for (tid = 0; tid < IEEE80211_RXBA_TIDS; tid++) {
		struct ieee80211_rxba_tid *r = tid == c->tid && c->fresh != NULL ?
		    c->fresh : ba->tid[tid];
		if (r == NULL)
			continue;
		error = rxba_deadline(r, IEEE80211_RXBA_HOLE_MS, &deadline);
		if (error != 0)
			goto failed;
		if (deadline < next)
			next = deadline;
	}
	c->deadline = next;
	if (next == UINT64_MAX)
		return 0;
	c->timer_token = rxba_serial(ic);
	if (c->timer_token == 0) {
		error = EOVERFLOW;
		goto failed;
	}
	error = ic->ic_rxba_ops->schedule(ic, next, c->timer_token);
	if (error == 0)
		return 0;
failed:
	/* Scheduling may have changed the old timer before reporting error.
	 * Do not keep a peer while its hole deadline is uncertain. */
	rxba_withdraw(ic, ni);
	return error;
}

int
ieee80211_rxba_action(struct ieee80211com *ic, struct ieee80211_node *ni,
    const uint8_t *p, size_t len)
{
	struct ieee80211_rxba_state *ba = &ni->ni_rxba;
	struct ieee80211_rxba_tid *r;
	struct ieee80211_rxba_candidate *c;
	uint64_t now;
	unsigned params, timeout, ssn, tid, window, i;
	int error;

	if (p == NULL || len < 2 || p[0] != 3)
		return EINVAL;
	/* Decline a premature request (status 37) and keep the association;
	 * a premature DELBA has no session to remove. */
	if (rxba_premature(ic, ni)) {
		if (p[1] == 0 && len == 9)
			return ieee80211_send_rxba_response(ic, ni, p[2], 37,
			    rxba_le16(p + 3) & 0x3e, rxba_le16(p + 5), NULL);
		return EINVAL;
	}
	if (!rxba_require_current(ic, ni))
		return EINVAL;
	if (p[1] == 2) {
		if (len != 6)
			return EINVAL;
		params = rxba_le16(p + 2);
		tid = params >> 12;
		if ((params & 0x7ff) != 0 || tid >= IEEE80211_RXBA_TIDS)
			return EOPNOTSUPP;
		if ((params & 0x800) != 0) {
			rxba_free_tid(ba, tid);
		} else if (ba->tx[tid].state != IEEE80211_TXBA_IDLE) {
			/* The recipient ends our originator session. */
			ba->tx_diag.delbas++;
			txba_idle(&ba->tx[tid], ic->ic_rxba_ops->now_ms(ic));
		}
		return rxba_reschedule(ic, ni);
	}
	if (p[1] == 1)
		return txba_response(ic, ni, p, len);
	if (p[1] != 0 || len != 9)
		return EOPNOTSUPP;
	params = rxba_le16(p + 3);
	timeout = rxba_le16(p + 5);
	ssn = rxba_le16(p + 7);
	tid = (params >> 2) & 15;
	window = params >> 6;
	window = MIN(window == 0 ? IEEE80211_RXBA_WINDOW : window, IEEE80211_RXBA_WINDOW);
	if ((params & 2) == 0 || tid >= IEEE80211_RXBA_TIDS || (ssn & 15) != 0)
		return ieee80211_send_rxba_response(ic, ni, p[2], 37,
		    params & 0x3e, timeout, NULL);
	now = ic->ic_rxba_ops->now_ms(ic);
	if (now == UINT64_MAX) {
		rxba_withdraw(ic, ni);
		return EOVERFLOW;
	}
	for (i = 0; i < IEEE80211_RXBA_TIDS; i++) {
		if (ba->tid[i] != NULL && now < ba->tid[i]->last_rx) {
			rxba_withdraw(ic, ni);
			return EINVAL;
		}
	}
	if (ba->epoch == 0) {
		ba->epoch = rxba_serial(ic);
		if (ba->key_epoch == 0)
			ba->key_epoch = rxba_serial(ic);
		if (ba->epoch == 0 || ba->key_epoch == 0) {
			rxba_withdraw(ic, ni);
			return EOVERFLOW;
		}
	}
	if (sizeof(*c) > RXBA_HEAP_LIMIT - ba->bytes)
		return ENOBUFS;
	c = kmem_intr_zalloc(sizeof(*c), KM_NOSLEEP);
	if (c == NULL)
		return ENOMEM;
	c->token = p[2];
	c->tid = tid;
	c->params = (params & 0x3e) | (window << 6);
	c->timeout = timeout;
	c->epoch = ba->epoch;
	c->key_epoch = ba->key_epoch;
	c->attempt = ni->ni_ht20.attempt;
	r = ba->tid[tid];
	if (r == NULL || r->token != p[2] || r->request_params != params ||
	    r->timeout != timeout || r->request_ssn != ssn) {
		if (sizeof(*r) + sizeof(*c) > RXBA_HEAP_LIMIT - ba->bytes) {
			error = ENOBUFS;
			goto out;
		}
		r = kmem_intr_zalloc(sizeof(*r), KM_NOSLEEP);
		if (r == NULL) {
			error = ENOMEM;
			goto out;
		}
		c->fresh = r;
		r->window = window;
		r->head = ssn >> 4;
		r->token = p[2];
		r->request_params = params;
		r->timeout = timeout;
		r->request_ssn = ssn;
		r->last_rx = now;
	}
	error = rxba_candidate_schedule(ic, ni, c);
	if (error == 0)
		error = ieee80211_send_rxba_response(ic, ni, p[2], 0, c->params, timeout, c);
	if (error == 0 && !c->committed) {
		/* A reported success must cross the actual queue publication owner. */
		error = EIO;
		rxba_withdraw(ic, ni);
	}
	if (error != 0 && c->timer_token != 0 && ba->epoch != 0)
		(void)rxba_reschedule(ic, ni);
out:
	if (c->fresh != NULL) {
		kmem_intr_free(c->fresh, sizeof(*c->fresh));
	}
	kmem_intr_free(c, sizeof(*c));
	return error;
}

/* The negotiated raw owner and the firmware owner retain separate lifetimes,
 * but use the same insertion, advance, contiguous and hole-expiry mechanism. */
#define RXBA_OFFLOAD_LIMIT \
    (IEEE80211_RXBA_BUDGET - sizeof(struct ieee80211_rxba_offload))

struct rxba_offload_owner {
	struct ieee80211_rxba_offload *ba;
	struct ieee80211_rxba_output *out;
	unsigned tid;
	int drain;
};

static void
rxba_append(struct mbuf **head, struct mbuf **tail, struct mbuf *m,
    struct mbuf *last)
{

	if (*tail != NULL)
		(*tail)->m_nextpkt = m;
	else
		*head = m;
	*tail = last;
}

static void
rxba_offload_emit(void *arg, struct rxba_slot *slot)
{
	struct rxba_offload_owner *owner = arg;
	struct ieee80211_rxba_offload *ba = owner->ba;
	struct ieee80211_rxba_output *out = owner->out;
	struct ieee80211_rxba_batch *batch = &out->ready[owner->tid];

	if (slot->m == NULL)
		return;
	KASSERT(ba->bytes >= slot->charged);
	ba->bytes -= slot->charged;
	if (owner->drain) {
		batch->epoch = ba->epoch;
		batch->key_epoch = ba->key_epoch;
		batch->session = ba->session[owner->tid];
		rxba_append(&batch->head, &batch->tail, slot->m, slot->tail);
	} else {
		rxba_append(&out->discard, &out->discard_tail, slot->m, slot->tail);
	}
	memset(slot, 0, sizeof(*slot));
}

static void
rxba_offload_free_tid(struct rxba_offload_owner *owner)
{
	struct ieee80211_rxba_offload *ba = owner->ba;
	struct ieee80211_rxba_tid *r = ba->tid[owner->tid];

	if (r == NULL)
		return;
	rxba_advance(r, r->window, rxba_offload_emit, owner);
	KASSERT(ba->bytes >= sizeof(*r));
	ba->bytes -= sizeof(*r);
	ba->tid[owner->tid] = NULL;
	kmem_intr_free(r, sizeof(*r));
}

void
ieee80211_rxba_offload_retire(struct ieee80211_rxba_offload *ba,
    struct ieee80211_rxba_output *out)
{
	struct rxba_offload_owner owner = { ba, out, 0, 0 };

	for (; owner.tid < IEEE80211_RXBA_OFFLOAD_TIDS; owner.tid++) {
		rxba_offload_free_tid(&owner);
		ba->deliver[owner.tid] = 0;
	}
	ba->epoch = ba->key_epoch = ba->timer_token = 0;
	ba->deadline = UINT64_MAX;
	KASSERT(ba->bytes == 0);
}

static int
rxba_offload_epoch(const struct ieee80211_rxba_offload *ba, uint64_t epoch,
    uint64_t key)
{

	return epoch != 0 && key != 0 && epoch == ba->epoch && key == ba->key_epoch;
}

static int
rxba_offload_clock(struct ieee80211_rxba_offload *ba, uint64_t now,
    struct ieee80211_rxba_output *out)
{

	if (now < ba->now || now >= UINT64_MAX - ba->hole_ms) {
		ieee80211_rxba_offload_retire(ba, out);
		return EOVERFLOW;
	}
	ba->now = now;
	return 0;
}

static int
rxba_offload_schedule(struct ieee80211_rxba_offload *ba,
    struct ieee80211_rxba_output *out)
{
	uint64_t deadline, next = UINT64_MAX;
	unsigned tid;

	for (tid = 0; tid < IEEE80211_RXBA_OFFLOAD_TIDS; tid++) {
		if (ba->tid[tid] == NULL)
			continue;
		if (rxba_deadline(ba->tid[tid], ba->hole_ms, &deadline) != 0)
			goto invalid;
		if (deadline < next)
			next = deadline;
	}
	ba->deadline = next;
	if (next == UINT64_MAX) {
		ba->timer_token = 0;
		return 0;
	}
	if (ba->serial >= UINT64_MAX - 1) {
		ba->serial = UINT64_MAX;
		goto invalid;
	}
	ba->timer_token = ++ba->serial;
	return 0;
invalid:
	ieee80211_rxba_offload_retire(ba, out);
	return EOVERFLOW;
}

int
ieee80211_rxba_offload_configure(struct ieee80211_rxba_offload *ba,
    uint64_t epoch, uint64_t key, unsigned hole_ms, uint64_t now,
    struct ieee80211_rxba_output *out)
{

	if (epoch == 0 || key == 0 || epoch == UINT64_MAX || key == UINT64_MAX ||
	    hole_ms == 0 || now >= UINT64_MAX - hole_ms || ba->serial == UINT64_MAX)
		return EINVAL;
	if (epoch < ba->last_epoch || (epoch == ba->last_epoch && key <= ba->last_key))
		return ESTALE;
	ieee80211_rxba_offload_retire(ba, out);
	ba->epoch = ba->last_epoch = epoch;
	ba->key_epoch = ba->last_key = key;
	ba->hole_ms = hole_ms;
	ba->now = now;
	return 0;
}

int
ieee80211_rxba_offload_start(struct ieee80211_rxba_offload *ba, unsigned tid,
    uint64_t epoch, uint64_t key, uint64_t session, unsigned ssn,
    unsigned window, uint64_t now, struct ieee80211_rxba_output *out)
{
	struct rxba_offload_owner owner = { ba, out, tid, 0 };
	struct ieee80211_rxba_tid *r;

	if (tid >= IEEE80211_RXBA_OFFLOAD_TIDS || session == 0 ||
	    session == UINT64_MAX || ssn > 4095 || window == 0 ||
	    window > IEEE80211_RXBA_WINDOW)
		return EINVAL;
	if (!rxba_offload_epoch(ba, epoch, key) || session <= ba->session[tid])
		return ESTALE;
	if (rxba_offload_clock(ba, now, out) != 0)
		return EOVERFLOW;
	rxba_offload_free_tid(&owner);
	ba->session[tid] = session;
	ba->deliver[tid] = 0;
	if (sizeof(*r) > RXBA_OFFLOAD_LIMIT - ba->bytes)
		return ENOBUFS;
	r = kmem_intr_zalloc(sizeof(*r), KM_NOSLEEP);
	if (r == NULL) {
		(void)rxba_offload_schedule(ba, out);
		return ENOMEM;
	}
	r->window = window;
	r->head = ssn;
	r->last_rx = now;
	ba->tid[tid] = r;
	ba->deliver[tid] = session;
	ba->bytes += sizeof(*r);
	return rxba_offload_schedule(ba, out);
}

int
ieee80211_rxba_offload_stop(struct ieee80211_rxba_offload *ba, unsigned tid,
    uint64_t epoch, uint64_t key, uint64_t session, int drain,
    struct ieee80211_rxba_output *out)
{
	struct rxba_offload_owner owner = { ba, out, tid, drain };

	if (tid >= IEEE80211_RXBA_OFFLOAD_TIDS || session == 0 ||
	    (drain != 0 && drain != 1))
		return EINVAL;
	if (!rxba_offload_epoch(ba, epoch, key) || session != ba->session[tid])
		return ESTALE;
	rxba_offload_free_tid(&owner);
	if (!drain)
		ba->deliver[tid] = 0;
	return rxba_offload_schedule(ba, out);
}

int
ieee80211_rxba_offload_input(struct ieee80211_rxba_offload *ba, unsigned tid,
    uint64_t epoch, uint64_t key, uint64_t session, unsigned seq, uint64_t now,
    struct mbuf *m, struct ieee80211_rxba_output *out)
{
	struct rxba_offload_owner owner = { ba, out, tid, 1 };
	struct rxba_slot incoming = {0};
	struct mbuf *part;
	size_t cost;
	unsigned packets = 0;
	int error = EINVAL;

	KASSERT(out->rejected == NULL);
	if (tid >= IEEE80211_RXBA_OFFLOAD_TIDS || seq > 4095 || m == NULL)
		goto reject;
	error = ESTALE;
	if (!rxba_offload_epoch(ba, epoch, key) || session == 0 ||
	    session != ba->session[tid] || ba->tid[tid] == NULL)
		goto reject;
	error = rxba_offload_clock(ba, now, out);
	if (error != 0)
		goto reject;
	error = EINVAL;
	for (part = m; part != NULL; part = part->m_nextpkt) {
		if (++packets > IEEE80211_RXBA_PACKETS_MAX)
			goto reject;
		cost = rxba_cost(part);
		if (cost == 0 || cost > RXBA_OFFLOAD_LIMIT - incoming.charged)
			goto reject;
		incoming.charged += cost;
		incoming.tail = part;
	}
	incoming.m = m;
	incoming.seq = seq;
	incoming.arrived = now;
	incoming.epoch = epoch;
	incoming.key_epoch = key;
	ba->tid[tid]->last_rx = now;
	error = rxba_insert(ba->tid[tid], &incoming, &ba->bytes,
	    RXBA_OFFLOAD_LIMIT, rxba_offload_emit, &owner);
	if (error != 0)
		out->rejected = m;
	/* A rejected input can still have advanced the window. */
	if (rxba_offload_schedule(ba, out) != 0)
		return EOVERFLOW;
	return error;
reject:
	out->rejected = m;
	return error;
}

void
ieee80211_rxba_offload_age(struct ieee80211_rxba_offload *ba, uint64_t token,
    uint64_t now, struct ieee80211_rxba_output *out)
{
	struct rxba_offload_owner owner = { ba, out, 0, 1 };

	if (token == 0 || token != ba->timer_token)
		return;
	if (rxba_offload_clock(ba, now, out) != 0)
		return;
	for (; owner.tid < IEEE80211_RXBA_OFFLOAD_TIDS; owner.tid++) {
		if (ba->tid[owner.tid] != NULL)
			out->gaps += rxba_expire(ba->tid[owner.tid], now,
			    ba->hole_ms, rxba_offload_emit, &owner);
	}
	(void)rxba_offload_schedule(ba, out);
}

int
ieee80211_rxba_offload_current(const struct ieee80211_rxba_offload *ba,
    unsigned tid, const struct ieee80211_rxba_batch *batch)
{

	return tid < IEEE80211_RXBA_OFFLOAD_TIDS && batch->session != 0 &&
	    batch->session == ba->deliver[tid] &&
	    rxba_offload_epoch(ba, batch->epoch, batch->key_epoch);
}
