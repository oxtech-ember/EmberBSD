/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD native ADI AXI DMAC driver. */
/*
 * Native ADI AXI DMAC, finite single-segment transfers with checked teardown.
 * Register contract: Analog Devices AXI DMAC 4.3 through 4.5 HDL documentation,
 * https://analogdevicesinc.github.io/hdl/library/axi_dmac/index.html
 * Board-specific drain acknowledgement is supplied by a kernel consumer.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/pmf.h>
#include <sys/queue.h>
#include <sys/systm.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/adi_axi_dmacvar.h>

#define DMAC_VERSION	0x000
#define DMAC_INTERFACE	0x010
#define DMAC_IRQ_MASK	0x080
#define DMAC_IRQ_PENDING	0x084
#define DMAC_CONTROL	0x400
#define DMAC_ID		0x404
#define DMAC_SUBMIT	0x408
#define DMAC_FLAGS	0x40c
#define DMAC_DEST	0x410
#define DMAC_SOURCE	0x414
#define DMAC_X_LENGTH	0x418
#define DMAC_Y_LENGTH	0x41c
#define DMAC_DEST_STRIDE	0x420
#define DMAC_SOURCE_STRIDE 0x424
#define DMAC_DONE	0x428
#define DMAC_DEBUG_STATUS 0x43c
#define DMAC_DEBUG_IDLE_45C 0x701U
#define DMAC_ENABLE	1
#define DMAC_IRQ_EOT	2
#define DMAC_IRQ_ALL	3
#define DMAC_FLAG_TLAST	2
#define DMAC_STREAM_HW_IDS 4U
#define DMAC_STREAM_MAX_QUEUED 31U

struct adi_axi_dmac_softc {
	device_t sc_dev;
	int sc_phandle;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	bus_size_t sc_size;
	bool sc_debug_idle_45c;
	bus_dma_tag_t sc_dmat;
	struct clk *sc_clk;
	void *sc_ih;
	void *sc_shutdownhook;
	/* Lock order: operation, CPU buffer state, interrupt state. */
	kmutex_t sc_lock;
	kmutex_t sc_buffer_lock;
	kmutex_t sc_irq_lock;
	kcondvar_t sc_done_cv;
	struct adi_axi_dmac_channel *sc_channel;
	struct adi_axi_dmac_guard *sc_guard;
	uint32_t sc_alignment;
	bool sc_read;
	bool sc_stream_source;
	bool sc_stream_dest;
	bool sc_quarantined;
	bool sc_stopping;
	/* Reset-only witness. Does not return CPU ownership or release a guard. */
	bool sc_reset_drained;
	bool sc_drained;
	bool sc_used;
	bool sc_pending;
	bool sc_done;
	uint32_t sc_id;
	LIST_ENTRY(adi_axi_dmac_softc) sc_next;
};

struct adi_axi_dmac_channel {
	struct adi_axi_dmac_softc *c_sc;
	device_t c_consumer;
	const struct adi_axi_dmac_drain_ops *c_ops;
	void *c_cookie;
	uint64_t c_max_length;
	uint32_t c_length_mask;
 bool c_streaming;
 bool c_stream_starved;
 struct adi_axi_dmac_buffer *c_stream_latched;
 uint64_t c_submit_sequence;
 uint64_t c_take_sequence;
 struct adi_axi_dmac_stream_status c_stream_status;
 void (*c_stream_notify)(void *);
 void *c_stream_cookie;
	LIST_HEAD(, adi_axi_dmac_buffer) c_buffers;
};

struct adi_axi_dmac_guard {
	struct adi_axi_dmac_softc *g_sc;
	device_t g_owner;
	struct adi_axi_dmac_channel *g_channel;
	bool g_bound;
	bool g_prepared;
};

struct adi_axi_dmac_buffer {
	struct adi_axi_dmac_channel *b_channel;
	bus_dma_segment_t b_segment;
	int b_nsegments;
	bus_dmamap_t b_map;
	void *b_data;
	size_t b_size;
	size_t b_valid;
	size_t b_inflight;
	size_t b_prepared;
	bool b_transferring;
 unsigned b_stream_state; /* 0: free, 1: accepted, 2: ready, 3: caller, 4: pending, 5: latched */
 uint32_t b_stream_id;
 uint64_t b_stream_sequence;
	LIST_ENTRY(adi_axi_dmac_buffer) b_next;
};

static LIST_HEAD(, adi_axi_dmac_softc) dmacs = LIST_HEAD_INITIALIZER(dmacs);
static kmutex_t dmac_registry_lock;
static ONCE_DECL(dmac_registry_once);

static int
dmac_registry_init(void)
{
	mutex_init(&dmac_registry_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static int
dmac_interface(struct adi_axi_dmac_softc *sc, uint32_t core_version,
    uint32_t interface)
{
	const uint32_t src_type = (interface >> 12) & 3;
	const uint32_t dst_type = (interface >> 4) & 3;
	const uint32_t src_width = (interface >> 8) & 15;
	const uint32_t dst_width = interface & 15;

	/* INTERFACE_DESCRIPTION reports log2(bytes per beat), not bits. */
	if ((core_version >> 16) != 4 || ((core_version >> 8) & 255) < 3 ||
	    ((core_version >> 8) & 255) > 5 || src_type > 2 || dst_type > 2 ||
	    (src_type == 0) == (dst_type == 0) ||
	    src_width > 7 || dst_width > 7)
		return EOPNOTSUPP;
	/* Only this exact register layout has a reviewed reset-manager witness. */
	sc->sc_debug_idle_45c = core_version == 0x00040563;
	sc->sc_read = dst_type == 0;
	sc->sc_stream_source = src_type == 1;
	sc->sc_stream_dest = dst_type == 1;
	sc->sc_alignment = 1U << MAX(src_width, dst_width);
	return 0;
}

static uint32_t
dmac_read(struct adi_axi_dmac_softc *sc, bus_size_t reg)
{
	return bus_space_read_4(sc->sc_bst, sc->sc_bsh, reg);
}

/* The owner holds the guard lifetime and excludes all new submissions.
 * This read-only poll can run inside a drain callback with sc_lock held.
 * Transfer IDs are not a witness: the HDL clears both on CONTROL disable.
 * 4.5.c without SG reports RESET state after all AXI responses and resets.
 * A core with stripped debug registers reads zero and must time out.
 */
static bool
dmac_idle_45c(struct adi_axi_dmac_softc *sc)
{

	return sc->sc_debug_idle_45c && sc->sc_size >= DMAC_DEBUG_STATUS + 4 &&
	    dmac_read(sc, DMAC_CONTROL) == 0 &&
	    dmac_read(sc, DMAC_SUBMIT) == 0 &&
	    (dmac_read(sc, DMAC_DEBUG_STATUS) & 0xfffU) == DMAC_DEBUG_IDLE_45C;
}

static int
dmac_wait_idle_45c(struct adi_axi_dmac_softc *sc, unsigned int started,
    unsigned int wait_ticks)
{
	uint64_t remaining_us;

	if (!sc->sc_debug_idle_45c || sc->sc_size < DMAC_DEBUG_STATUS + 4)
		return EOPNOTSUPP;
	/* Shutdown and debugger entry can stop hardclock interrupts. */
	remaining_us = ((uint64_t)wait_ticks * 1000000 + hz - 1) / hz;
	for (;;) {
		if (dmac_idle_45c(sc))
			return 0;
		if ((unsigned int)getticks() - started >= wait_ticks ||
		    remaining_us < 10)
			return ETIMEDOUT;
		delay(10);
		remaining_us -= 10;
	}
}

int
adi_axi_dmac_guard_wait_idle(const struct adi_axi_dmac_guard *g,
    unsigned int timeout_ms)
{
	struct adi_axi_dmac_softc *sc;

	if (g == NULL || timeout_ms == 0 || timeout_ms > 60000)
		return EINVAL;
	sc = g->g_sc;
	if (sc->sc_guard != g || sc->sc_quarantined)
		return EIO;
	return dmac_wait_idle_45c(sc, (unsigned int)getticks(),
	    MAX(1, mstohz(timeout_ms)));
}

static void
dmac_write(struct adi_axi_dmac_softc *sc, bus_size_t reg, uint32_t value)
{
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, reg, value);
}

/* Returns a permanent built-in provider with its operation lock held. */
static struct adi_axi_dmac_softc *
dmac_lookup(int phandle)
{
	struct adi_axi_dmac_softc *sc;

	RUN_ONCE(&dmac_registry_once, dmac_registry_init);
	mutex_enter(&dmac_registry_lock);
	LIST_FOREACH(sc, &dmacs, sc_next)
		if (sc->sc_phandle == phandle)
			break;
	if (sc != NULL)
		mutex_enter(&sc->sc_lock);
	mutex_exit(&dmac_registry_lock);
	return sc;
}

/* The caller holds the operation lock; CPU access uses the buffer lock. */
static void
dmac_quarantine_locked(struct adi_axi_dmac_softc *sc)
{
	mutex_enter(&sc->sc_buffer_lock);
	sc->sc_quarantined = true;
	mutex_exit(&sc->sc_buffer_lock);
}

static int
dmac_admit(struct adi_axi_dmac_softc *sc)
{
	if (sc->sc_stopping)
		return ESHUTDOWN;
	if (sc->sc_quarantined)
		return EIO;
	if (sc->sc_channel != NULL || sc->sc_guard != NULL)
		return EBUSY;
	if (dmac_read(sc, DMAC_CONTROL) & DMAC_ENABLE) {
		/* No reset or new address may overwrite a previous owner's state. */
		dmac_quarantine_locked(sc);
		return EIO;
	}
	return 0;
}

int
adi_axi_dmac_reserve(int phandle, device_t owner,
    struct adi_axi_dmac_guard **result)
{
	struct adi_axi_dmac_softc *sc;
	struct adi_axi_dmac_guard *g;
	int error;

	if (owner == NULL || result == NULL)
		return EINVAL;
	*result = NULL;
	g = kmem_zalloc(sizeof(*g), KM_SLEEP);
	sc = dmac_lookup(phandle);
	if (sc == NULL) {
		kmem_free(g, sizeof(*g));
		return ENXIO;
	}
	error = dmac_admit(sc);
	if (error == 0) {
		g->g_sc = sc;
		g->g_owner = owner;
		sc->sc_guard = g;
		device_acquire(owner);
		device_acquire(sc->sc_dev);
		*result = g;
	}
	mutex_exit(&sc->sc_lock);
	if (error != 0)
		kmem_free(g, sizeof(*g));
	return error;
}

int
adi_axi_dmac_guard_check(struct adi_axi_dmac_guard *g)
{
	struct adi_axi_dmac_softc *sc = g->g_sc;
	int error = 0;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_guard != g || sc->sc_quarantined || sc->sc_pending ||
	    dmac_read(sc, DMAC_SUBMIT) != 0 ||
	    (dmac_read(sc, DMAC_CONTROL) & DMAC_ENABLE) ||
	    (g->g_prepared ? (sc->sc_channel != g->g_channel ||
	    !sc->sc_drained) :
	    sc->sc_channel != NULL)) {
		dmac_quarantine_locked(sc);
		error = EIO;
	}
	mutex_exit(&sc->sc_lock);
	return error;
}

int
adi_axi_dmac_unreserve(struct adi_axi_dmac_guard *g)
{
	struct adi_axi_dmac_softc *sc = g->g_sc;
	struct adi_axi_dmac_channel *c = g->g_channel;
	int error = 0;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_stopping) {
		error = ESHUTDOWN;
		goto out;
	}
	if (sc->sc_guard != g || sc->sc_quarantined) {
		error = EIO;
		goto out;
	}
	if (c != NULL && !LIST_EMPTY(&c->c_buffers)) {
		error = EBUSY;
		goto out;
	}
	if (g->g_prepared) {
		if (sc->sc_channel != c || !sc->sc_drained || sc->sc_pending ||
		    (dmac_read(sc, DMAC_CONTROL) & DMAC_ENABLE) ||
		    dmac_read(sc, DMAC_SUBMIT) != 0) {
			dmac_quarantine_locked(sc);
			error = EIO;
			goto out;
		}
		sc->sc_channel = NULL;
	} else if (sc->sc_channel != NULL || sc->sc_pending ||
	    (dmac_read(sc, DMAC_CONTROL) & DMAC_ENABLE) ||
	    dmac_read(sc, DMAC_SUBMIT) != 0) {
		dmac_quarantine_locked(sc);
		error = EIO;
		goto out;
	}
	sc->sc_guard = NULL;
	out:
	mutex_exit(&sc->sc_lock);
	if (error != 0)
		return error;
	device_release(sc->sc_dev);
	device_release(g->g_owner);
	if (c != NULL)
		kmem_free(c, sizeof(*c));
	kmem_free(g, sizeof(*g));
	return 0;
}

int
adi_axi_dmac_guard_cold_check(struct adi_axi_dmac_guard *g)
{
	struct adi_axi_dmac_softc *sc = g->g_sc;
	int error = 0;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_guard != g || sc->sc_quarantined || sc->sc_stopping ||
	    sc->sc_used || sc->sc_channel != NULL || sc->sc_pending ||
	    g->g_prepared || (g->g_channel != NULL &&
	    !LIST_EMPTY(&g->g_channel->c_buffers)) ||
	    (dmac_read(sc, DMAC_CONTROL) & DMAC_ENABLE) ||
	    dmac_read(sc, DMAC_SUBMIT) != 0)
		error = EIO;
	mutex_exit(&sc->sc_lock);
	return error;
}

/* The buffer lock excludes CPU access and transfer admission. */
static void
dmac_unprepare_locked(struct adi_axi_dmac_buffer *b)
{
	struct adi_axi_dmac_softc *sc = b->b_channel->c_sc;

	if (b->b_prepared == 0)
		return;
	KASSERT(!sc->sc_quarantined);
	KASSERT(b->b_inflight == 0 && !b->b_transferring);
	/* This range was never submitted; no hardware drain is needed. */
	bus_dmamap_sync(sc->sc_dmat, b->b_map, 0, b->b_prepared,
	    BUS_DMASYNC_POSTWRITE);
	b->b_prepared = 0;
}

static int
dmac_stop_locked(struct adi_axi_dmac_channel *c, unsigned int timeout_ms,
    bool discard_prepared)
{
	struct adi_axi_dmac_softc *sc = c->c_sc;
	struct adi_axi_dmac_buffer *b;
	unsigned int started, wait_ticks;
	int error;

	if (sc->sc_quarantined)
		return EIO;
	if (timeout_ms == 0 || timeout_ms > 60000)
		return EINVAL;
	started = (unsigned int)getticks();
	wait_ticks = MAX(1, mstohz(timeout_ms));
	sc->sc_drained = false;
	mutex_enter(&sc->sc_irq_lock);
	sc->sc_pending = false;
 c->c_streaming = false;
 c->c_stream_notify = NULL;
 c->c_stream_cookie = NULL;
 c->c_stream_latched = NULL;
	dmac_write(sc, DMAC_IRQ_MASK, DMAC_IRQ_ALL);
	dmac_write(sc, DMAC_CONTROL, 0);
	dmac_read(sc, DMAC_CONTROL);
	mutex_exit(&sc->sc_irq_lock);
	error = c->c_ops->drain(c->c_cookie, timeout_ms);
	if (error == 0 && sc->sc_debug_idle_45c)
		error = dmac_wait_idle_45c(sc, started, wait_ticks);
	else if (error == 0 && (dmac_read(sc, DMAC_CONTROL) != 0 ||
	    dmac_read(sc, DMAC_SUBMIT) != 0))
		error = EIO;
	if (error != 0) {
		dmac_quarantine_locked(sc);
		return error;
	}
	mutex_enter(&sc->sc_buffer_lock);
	LIST_FOREACH(b, &c->c_buffers, b_next) {
		if (b->b_inflight != 0) {
			bus_dmamap_sync(sc->sc_dmat, b->b_map, 0, b->b_inflight,
			    sc->sc_read ? BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
			b->b_inflight = 0;
		}
		if (discard_prepared)
			dmac_unprepare_locked(b);
	}
	mutex_exit(&sc->sc_buffer_lock);
	mutex_enter(&sc->sc_irq_lock);
	dmac_write(sc, DMAC_IRQ_PENDING, DMAC_IRQ_ALL);
	mutex_exit(&sc->sc_irq_lock);
	mutex_enter(&sc->sc_irq_lock);
 c->c_streaming = false;
 c->c_stream_notify = NULL;
 c->c_stream_cookie = NULL;
 mutex_exit(&sc->sc_irq_lock);
 LIST_FOREACH(b, &c->c_buffers, b_next) {
  if (b->b_stream_state != 0) {
   b->b_stream_state = 0;
   b->b_valid = 0;
  }
 }
 sc->sc_drained = true;
 return 0;
}

int
adi_axi_dmac_guard_bind(struct adi_axi_dmac_guard *g,
    const struct adi_axi_dmac_drain_ops *ops, void *cookie)
{
	struct adi_axi_dmac_softc *sc;
	struct adi_axi_dmac_channel *c;
	int error = 0;

	if (g == NULL || ops == NULL || ops->drain == NULL)
		return EINVAL;
	sc = g->g_sc;
	c = kmem_zalloc(sizeof(*c), KM_SLEEP);
	c->c_sc = sc;
	c->c_consumer = g->g_owner;
	c->c_ops = ops;
	c->c_cookie = cookie;
	LIST_INIT(&c->c_buffers);
	mutex_enter(&sc->sc_lock);
	if (sc->sc_guard != g || sc->sc_quarantined)
		error = EIO;
	else if (g->g_bound)
		error = EALREADY;
	else if (sc->sc_channel != NULL)
		error = EBUSY;
	else {
		g->g_channel = c;
		g->g_bound = true;
	}
	mutex_exit(&sc->sc_lock);
	if (error != 0)
		kmem_free(c, sizeof(*c));
	return error;
}

int
adi_axi_dmac_guard_prepare(struct adi_axi_dmac_guard *g)
{
	struct adi_axi_dmac_softc *sc;
	struct adi_axi_dmac_channel *c;
	int error;

	if (g == NULL)
		return EINVAL;
	sc = g->g_sc;
	mutex_enter(&sc->sc_lock);
	if (sc->sc_guard != g || sc->sc_quarantined || sc->sc_stopping) {
		error = EIO;
		goto out;
	}
	if (!g->g_bound || (c = g->g_channel) == NULL) {
		error = ENXIO;
		goto out;
	}
	if (g->g_prepared) {
		if (sc->sc_channel != c || !sc->sc_drained || sc->sc_pending ||
		    dmac_read(sc, DMAC_CONTROL) != 0 ||
		    dmac_read(sc, DMAC_SUBMIT) != 0) {
			dmac_quarantine_locked(sc);
			error = EIO;
		} else {
			error = 0;
		}
		goto out;
	}
	/* This API admits finite ADI FIFO source to AXI memory only. */
	if (!sc->sc_read || sc->sc_stream_source || sc->sc_stream_dest) {
		error = EOPNOTSUPP;
		goto out;
	}
	if (sc->sc_channel != NULL || sc->sc_pending) {
		error = EBUSY;
		goto out;
	}
	error = dmac_stop_locked(c, 1000, true);
	if (error != 0)
		goto out;
	dmac_write(sc, DMAC_X_LENGTH, UINT32_MAX);
	c->c_max_length = (uint64_t)dmac_read(sc, DMAC_X_LENGTH) + 1;
	dmac_write(sc, DMAC_X_LENGTH, 0);
	c->c_length_mask = dmac_read(sc, DMAC_X_LENGTH);
	if (c->c_max_length == 0 || c->c_length_mask == UINT32_MAX) {
		dmac_quarantine_locked(sc);
		error = EIO;
		goto out;
	}
	sc->sc_channel = c;
	sc->sc_used = true;
	g->g_prepared = true;
	error = 0;
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

int
adi_axi_dmac_guard_buffer_alloc(const struct adi_axi_dmac_guard *g, size_t size,
	struct adi_axi_dmac_buffer **result)
{
	if (g == NULL || !g->g_prepared || g->g_channel == NULL)
		return EINVAL;
	return adi_axi_dmac_buffer_alloc(g->g_channel, size, result);
}

int
adi_axi_dmac_guard_buffer_read(const struct adi_axi_dmac_guard *g,
	struct adi_axi_dmac_buffer *b, void *data, size_t size)
{
	if (g == NULL || b == NULL || g->g_channel == NULL ||
	    b->b_channel != g->g_channel)
		return EINVAL;
	return adi_axi_dmac_buffer_read(b, data, size);
}

int
adi_axi_dmac_guard_transfer(const struct adi_axi_dmac_guard *g,
	struct adi_axi_dmac_buffer *b, size_t size, unsigned int timeout_ms)
{
	if (g == NULL || !g->g_prepared || b == NULL ||
	    b->b_channel != g->g_channel)
		return EINVAL;
	return adi_axi_dmac_transfer(b, size, timeout_ms);
}

int
adi_axi_dmac_guard_buffer_free(const struct adi_axi_dmac_guard *g,
	struct adi_axi_dmac_buffer *b)
{
	if (g == NULL || b == NULL || g->g_channel == NULL ||
	    b->b_channel != g->g_channel)
		return EINVAL;
	return adi_axi_dmac_buffer_free(b);
}

/* PMF may sleep; the final hook must never sleep or call a consumer. */
static bool
adiaxidmac_shutdown(device_t dev, int howto)
{
	struct adi_axi_dmac_softc *sc = device_private(dev);
	int error = 0;

	(void)howto;
	if (!mutex_tryenter(&sc->sc_lock))
		return false;
	sc->sc_reset_drained = false;
	if (!mutex_tryenter(&sc->sc_buffer_lock)) {
		mutex_exit(&sc->sc_lock);
		return false;
	}
	sc->sc_stopping = true;
	mutex_exit(&sc->sc_buffer_lock);
	if (sc->sc_quarantined)
		error = EIO;
	else if (sc->sc_guard != NULL) {
		/* Retain every owner. This witness permits only a platform reset. */
		if (sc->sc_pending || !mutex_tryenter(&sc->sc_irq_lock)) {
			error = EBUSY;
		} else {
			dmac_write(sc, DMAC_IRQ_MASK, DMAC_IRQ_ALL);
			dmac_write(sc, DMAC_CONTROL, 0);
			dmac_read(sc, DMAC_CONTROL);
			mutex_exit(&sc->sc_irq_lock);
			error = dmac_wait_idle_45c(sc, (unsigned int)getticks(),
			    MAX(1, mstohz(1000)));
			if (error == 0)
				sc->sc_reset_drained = true;
		}
	} else if (sc->sc_channel != NULL)
		error = dmac_stop_locked(sc->sc_channel, 1000, true);
	else if ((dmac_read(sc, DMAC_CONTROL) & DMAC_ENABLE) ||
	    dmac_read(sc, DMAC_SUBMIT) != 0) {
		dmac_quarantine_locked(sc);
		error = EIO;
	}
	mutex_exit(&sc->sc_lock);
	return error == 0;
}

static void __dead
dmac_shutdown_halt(struct adi_axi_dmac_softc *sc)
{
	/* Panic or RB_HALT can still reach a platform reset on some machines. */
	(void)splhigh();
	printf("%s: DMA shutdown is unproved; reset blocked. "
	    "Remove power before restarting.\n", device_xname(sc->sc_dev));
	for (;;)
		SPINLOCK_BACKOFF_HOOK;
}

static void
adiaxidmac_shutdown_final(void *cookie)
{
	struct adi_axi_dmac_softc *sc = cookie;

	if (!mutex_tryenter(&sc->sc_lock))
		dmac_shutdown_halt(sc);
	if (!mutex_tryenter(&sc->sc_buffer_lock))
		dmac_shutdown_halt(sc);
	if (sc->sc_guard != NULL && (!sc->sc_stopping ||
	    !sc->sc_reset_drained || !dmac_idle_45c(sc)))
		dmac_shutdown_halt(sc);
	sc->sc_stopping = true;
	if (!mutex_tryenter(&sc->sc_irq_lock))
		dmac_shutdown_halt(sc);
	if (sc->sc_quarantined || sc->sc_pending ||
	    (dmac_read(sc, DMAC_CONTROL) & DMAC_ENABLE) ||
	    dmac_read(sc, DMAC_SUBMIT) != 0)
		dmac_shutdown_halt(sc);
	if (sc->sc_channel != NULL) {
		const struct adi_axi_dmac_buffer *b;

		if (!sc->sc_drained && sc->sc_guard == NULL)
			dmac_shutdown_halt(sc);
		LIST_FOREACH(b, &sc->sc_channel->c_buffers, b_next)
			if (b->b_inflight != 0 || b->b_prepared != 0)
				dmac_shutdown_halt(sc);
	}
	dmac_write(sc, DMAC_IRQ_MASK, DMAC_IRQ_ALL);
	dmac_read(sc, DMAC_IRQ_MASK);
	mutex_exit(&sc->sc_irq_lock);
	/* Keep the operation/buffer locks and mappings until platform reset. */
}

int
adi_axi_dmac_acquire(int phandle, device_t consumer,
    const struct adi_axi_dmac_drain_ops *ops, void *cookie,
    struct adi_axi_dmac_channel **result)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_softc *sc;
	int error;

	if (consumer == NULL || result == NULL)
		return EINVAL;
	*result = NULL;
	if (ops == NULL || ops->drain == NULL)
		return EOPNOTSUPP;
	c = kmem_zalloc(sizeof(*c), KM_SLEEP);
	sc = dmac_lookup(phandle);
	if (sc == NULL) {
		kmem_free(c, sizeof(*c));
		return ENXIO;
	}
	error = dmac_admit(sc);
	if (error != 0)
		goto out;
	c->c_sc = sc;
	c->c_consumer = consumer;
	c->c_ops = ops;
	c->c_cookie = cookie;
	LIST_INIT(&c->c_buffers);
	sc->sc_channel = c;
	sc->sc_used = true;
	device_acquire(sc->sc_dev);
	device_acquire(consumer);
	error = dmac_stop_locked(c, 1000, true);
	if (error != 0) {
		/* Failed admission drain retains the channel and its device refs. */
		mutex_exit(&sc->sc_lock);
		return error;
	}
	/* Probe writable length fields only after a positive drain certificate. */
	dmac_write(sc, DMAC_X_LENGTH, UINT32_MAX);
	c->c_max_length = (uint64_t)dmac_read(sc, DMAC_X_LENGTH) + 1;
	dmac_write(sc, DMAC_X_LENGTH, 0);
	c->c_length_mask = dmac_read(sc, DMAC_X_LENGTH);
	*result = c;
out:
	mutex_exit(&sc->sc_lock);
	if (error != 0)
		kmem_free(c, sizeof(*c));
	return error;
}

int
adi_axi_dmac_stop(struct adi_axi_dmac_channel *c, unsigned int timeout_ms)
{
	int error;

	mutex_enter(&c->c_sc->sc_lock);
	error = dmac_stop_locked(c, timeout_ms, true);
	mutex_exit(&c->c_sc->sc_lock);
	return error;
}

int
adi_axi_dmac_release(struct adi_axi_dmac_channel *c)
{
	struct adi_axi_dmac_softc *sc = c->c_sc;
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_stopping && sc->sc_guard != NULL) {
		mutex_exit(&sc->sc_lock);
		return ESHUTDOWN;
	}
	if (!LIST_EMPTY(&c->c_buffers)) {
		mutex_exit(&sc->sc_lock);
		return EBUSY;
	}
	error = dmac_stop_locked(c, 1000, true);
	if (error == 0)
		sc->sc_channel = NULL;
	mutex_exit(&sc->sc_lock);
	if (error == 0) {
		device_release(sc->sc_dev);
		device_release(c->c_consumer);
		kmem_free(c, sizeof(*c));
	}
	return error;
}

int
adi_axi_dmac_buffer_alloc(struct adi_axi_dmac_channel *c, size_t size,
    struct adi_axi_dmac_buffer **result)
{
	struct adi_axi_dmac_softc *sc = c->c_sc;
	struct adi_axi_dmac_buffer *b;
	int error;

	if (result == NULL || size == 0 || size > c->c_max_length)
		return EINVAL;
	*result = NULL;
	b = kmem_zalloc(sizeof(*b), KM_SLEEP);
	b->b_channel = c;
	b->b_size = size;
	mutex_enter(&sc->sc_lock);
	if (c->c_streaming) {
		error = EBUSY;
		goto fail;
	}
	if (sc->sc_stopping) {
		error = ESHUTDOWN;
		goto fail;
	}
	if (sc->sc_quarantined) {
		error = EIO;
		goto fail;
	}
	error = bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0,
	    &b->b_segment, 1, &b->b_nsegments, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail;
	/* TX is CPU-filled; PREWRITE publishes cached stores before submit. */
	error = bus_dmamem_map(sc->sc_dmat, &b->b_segment, b->b_nsegments,
	    size, &b->b_data, BUS_DMA_WAITOK |
	    (sc->sc_read ? BUS_DMA_COHERENT : 0));
	if (error != 0)
		goto fail_mem;
	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK, &b->b_map);
	if (error != 0)
		goto fail_map;
	error = bus_dmamap_load(sc->sc_dmat, b->b_map, b->b_data, size, NULL,
	    BUS_DMA_WAITOK | (sc->sc_read ? BUS_DMA_READ : BUS_DMA_WRITE));
	if (error != 0)
		goto fail_create;
	if (b->b_map->dm_nsegs != 1 ||
	    (uint64_t)b->b_map->dm_segs[0].ds_addr + size > (UINT64_C(1) << 32) ||
	    (b->b_map->dm_segs[0].ds_addr & (sc->sc_alignment - 1))) {
		error = EFBIG;
		goto fail_load;
	}
	memset(b->b_data, 0, size);
	LIST_INSERT_HEAD(&c->c_buffers, b, b_next);
	*result = b;
	mutex_exit(&sc->sc_lock);
	return 0;
fail_load:
	bus_dmamap_unload(sc->sc_dmat, b->b_map);
fail_create:
	bus_dmamap_destroy(sc->sc_dmat, b->b_map);
fail_map:
	bus_dmamem_unmap(sc->sc_dmat, b->b_data, size);
fail_mem:
	bus_dmamem_free(sc->sc_dmat, &b->b_segment, b->b_nsegments);
fail:
	mutex_exit(&sc->sc_lock);
	kmem_free(b, sizeof(*b));
	return error;
}

int
adi_axi_dmac_buffer_free(struct adi_axi_dmac_buffer *b)
{
	struct adi_axi_dmac_softc *sc = b->b_channel->c_sc;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_stopping && sc->sc_guard != NULL) {
		mutex_exit(&sc->sc_lock);
		return ESHUTDOWN;
	}
	mutex_enter(&sc->sc_buffer_lock);
	if (b->b_channel->c_streaming || sc->sc_quarantined ||
	    b->b_inflight != 0 || b->b_transferring) {
		mutex_exit(&sc->sc_buffer_lock);
		mutex_exit(&sc->sc_lock);
		return EBUSY;
	}
	dmac_unprepare_locked(b);
	LIST_REMOVE(b, b_next);
	bus_dmamap_unload(sc->sc_dmat, b->b_map);
	bus_dmamap_destroy(sc->sc_dmat, b->b_map);
	bus_dmamem_unmap(sc->sc_dmat, b->b_data, b->b_size);
	bus_dmamem_free(sc->sc_dmat, &b->b_segment, b->b_nsegments);
	mutex_exit(&sc->sc_buffer_lock);
	mutex_exit(&sc->sc_lock);
	kmem_free(b, sizeof(*b));
	return 0;
}

int
adi_axi_dmac_buffer_read(struct adi_axi_dmac_buffer *b, void *data, size_t size)
{
	struct adi_axi_dmac_softc *sc = b->b_channel->c_sc;
	int error = 0;

	mutex_enter(&sc->sc_buffer_lock);
	if (data == NULL)
		error = EINVAL;
	else if (sc->sc_stopping)
		error = ESHUTDOWN;
	else if (sc->sc_quarantined || b->b_inflight != 0 || b->b_transferring)
		error = EBUSY;
	else if (size > b->b_valid)
		error = EINVAL;
	else {
		dmac_unprepare_locked(b);
		memcpy(data, b->b_data, size);
	}
	mutex_exit(&sc->sc_buffer_lock);
	return error;
}

int
adi_axi_dmac_buffer_write(struct adi_axi_dmac_buffer *b, const void *data, size_t size)
{
	struct adi_axi_dmac_softc *sc = b->b_channel->c_sc;
	int error = 0;

	mutex_enter(&sc->sc_buffer_lock);
	if (data == NULL || size > b->b_size || sc->sc_read)
		error = EINVAL;
	else if (sc->sc_stopping)
		error = ESHUTDOWN;
	else if (sc->sc_quarantined || b->b_inflight != 0 || b->b_transferring)
		error = EBUSY;
	else {
		dmac_unprepare_locked(b);
		memcpy(b->b_data, data, size);
		b->b_valid = size;
	}
	mutex_exit(&sc->sc_buffer_lock);
	return error;
}

int
adi_axi_dmac_buffer_prepare(struct adi_axi_dmac_buffer *b, size_t size)
{
	struct adi_axi_dmac_channel *c = b->b_channel;
	struct adi_axi_dmac_softc *sc = c->c_sc;
	int error = 0;

	mutex_enter(&sc->sc_buffer_lock);
	if (sc->sc_read)
		error = EOPNOTSUPP;
	else if (size == 0 || size > b->b_size || size > c->c_max_length ||
	    ((size - 1) & c->c_length_mask) != c->c_length_mask)
		error = EINVAL;
	else if (sc->sc_stopping)
		error = ESHUTDOWN;
	else if (sc->sc_quarantined || b->b_inflight != 0 || b->b_transferring)
		error = EBUSY;
	else if (size > b->b_valid)
		error = EINVAL;
	else if (b->b_prepared != size) {
		dmac_unprepare_locked(b);
		bus_dmamap_sync(sc->sc_dmat, b->b_map, 0, size,
		    BUS_DMASYNC_PREWRITE);
		b->b_prepared = size;
	}
	mutex_exit(&sc->sc_buffer_lock);
	return error;
}

int
adi_axi_dmac_transfer(struct adi_axi_dmac_buffer *b, size_t size,
    unsigned int timeout_ms)
{
	struct adi_axi_dmac_channel *c = b->b_channel;
	struct adi_axi_dmac_softc *sc = c->c_sc;
	unsigned int started, wait_ticks, elapsed;
	int error, stop_error;

	if (size == 0 || size > b->b_size || size > c->c_max_length ||
	    ((size - 1) & c->c_length_mask) != c->c_length_mask ||
	    timeout_ms == 0 || timeout_ms > 60000)
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	mutex_enter(&sc->sc_buffer_lock);
	if (sc->sc_stopping) {
		error = ESHUTDOWN;
		goto out;
	}
	if (c->c_streaming) {
		error = EBUSY;
		goto out;
	}
	if (sc->sc_stream_source) {
		/* Early AXIS TLAST needs partial-length reporting, not a full-buffer
		 * success inferred from EOT. Finite FIFO/MM transfers are supported.
		 */
		error = EOPNOTSUPP;
		goto out;
	}
	if (sc->sc_quarantined || b->b_inflight != 0) {
		error = EBUSY;
		goto out;
	}
	if (!sc->sc_read && b->b_valid < size) {
		error = EINVAL;
		goto out;
	}
	if (dmac_read(sc, DMAC_SUBMIT) != 0 ||
	    (dmac_read(sc, DMAC_CONTROL) & DMAC_ENABLE)) {
		sc->sc_quarantined = true;
		error = EIO;
		goto out;
	}
	if (!sc->sc_read && b->b_prepared != size)
		dmac_unprepare_locked(b);
	b->b_transferring = true;
	b->b_valid = 0;
	sc->sc_drained = false;
	if (b->b_prepared == 0)
		bus_dmamap_sync(sc->sc_dmat, b->b_map, 0, size,
		    sc->sc_read ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);
	b->b_prepared = 0;
	b->b_inflight = size;
	mutex_exit(&sc->sc_buffer_lock);
	mutex_enter(&sc->sc_irq_lock);
	dmac_write(sc, DMAC_IRQ_MASK, DMAC_IRQ_ALL);
	dmac_write(sc, DMAC_IRQ_PENDING, DMAC_IRQ_ALL);
	dmac_write(sc, DMAC_FLAGS,
	    sc->sc_stream_dest ? DMAC_FLAG_TLAST : 0);
	dmac_write(sc, sc->sc_read ? DMAC_DEST : DMAC_SOURCE,
	    b->b_map->dm_segs[0].ds_addr);
	dmac_write(sc, DMAC_X_LENGTH, size - 1);
	dmac_write(sc, DMAC_Y_LENGTH, 0);
	dmac_write(sc, DMAC_DEST_STRIDE, 0);
	dmac_write(sc, DMAC_SOURCE_STRIDE, 0);
	dmac_write(sc, DMAC_CONTROL, DMAC_ENABLE);
	sc->sc_id = dmac_read(sc, DMAC_ID);
	if (sc->sc_id >= 32) {
		mutex_exit(&sc->sc_irq_lock);
		error = EIO;
		goto stop;
	}
	sc->sc_done = false;
	sc->sc_pending = true;
	dmac_write(sc, DMAC_IRQ_MASK, 1); /* EOT only */
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, 0, sc->sc_size,
	    BUS_SPACE_BARRIER_WRITE);
	dmac_write(sc, DMAC_SUBMIT, 1);
	dmac_read(sc, DMAC_SUBMIT);
	started = (unsigned int)getticks();
	wait_ticks = MAX(1, mstohz(timeout_ms));
	mutex_exit(&sc->sc_irq_lock);
	mutex_enter(&sc->sc_irq_lock);
	while (!sc->sc_done) {
		/* Spurious wakes must not restart the original timeout. */
		elapsed = (unsigned int)getticks() - started;
		if (elapsed >= wait_ticks)
			break;
		if (cv_timedwait(&sc->sc_done_cv, &sc->sc_irq_lock,
		    wait_ticks - elapsed) == EWOULDBLOCK)
			break;
	}
	error = sc->sc_done ? 0 : ETIMEDOUT;
	mutex_exit(&sc->sc_irq_lock);
stop:
	stop_error = dmac_stop_locked(c, timeout_ms, false);
	if (stop_error != 0)
		error = stop_error;
	mutex_enter(&sc->sc_buffer_lock);
	if (error == 0)
		b->b_valid = size;
	b->b_transferring = false;
out:
	mutex_exit(&sc->sc_buffer_lock);
	mutex_exit(&sc->sc_lock);
	return error;
}


/* The posted request is not accepted until SUBMIT clears and ID advances.
 * Exact 4.5.c has two-bit IDs. Old DONE clears on SOT, not on SUBMIT.
 * Only one register request can be latched, so an observed advance is unique.
 */
static void
dmac_stream_accept_locked(struct adi_axi_dmac_channel *c)
{
 struct adi_axi_dmac_softc *sc = c->c_sc;
 struct adi_axi_dmac_buffer *b = c->c_stream_latched;
 if (b == NULL || dmac_read(sc, DMAC_SUBMIT) != 0)
  return;
 if (dmac_read(sc, DMAC_ID) != ((b->b_stream_id + 1) & 3U))
  return;
 b->b_stream_state = 1;
 c->c_stream_latched = NULL;
}

/* IRQ lock protects queue state. PRE sync completes before state 4 is visible.
 * SOT frees the request latch. EOT and next() also retry queued requests.
 * The buffer list stays fixed until a positive stop removes the IRQ callback.
 */
static void
dmac_stream_pump_locked(struct adi_axi_dmac_channel *c)
{
 struct adi_axi_dmac_softc *sc = c->c_sc;
 struct adi_axi_dmac_buffer *b;
 const struct adi_axi_dmac_buffer *other;
 uint32_t done;
 bool live = false, completed = false;
 dmac_stream_accept_locked(c);
 live = c->c_stream_latched != NULL;
 done = dmac_read(sc, DMAC_DONE);
 LIST_FOREACH(b, &c->c_buffers, b_next) {
  if (b->b_stream_state != 1)
   continue;
  if (done & __BIT(b->b_stream_id)) {
   /* Retire only metadata in interrupt context. Keep PREREAD ownership
    * until the exact task consumer takes this map, or a positive stop.
    * Its hardware ID can now serve another independently owned map.
    */
   b->b_stream_state = 2;
   c->c_stream_status.queued--;
   c->c_stream_status.ready++;
   c->c_stream_status.completed_blocks++;
   c->c_stream_status.completed_bytes += b->b_inflight;
   completed = true;
  } else
   live = true;
 }
 if (completed && !live && !c->c_stream_starved) {
  c->c_stream_starved = true;
  c->c_stream_status.discontinuities++;
 }
 for (unsigned n = 0; n < DMAC_STREAM_HW_IDS; n++) {
  struct adi_axi_dmac_buffer *first;
  uint32_t id;
  if (c->c_stream_latched != NULL || dmac_read(sc, DMAC_SUBMIT) != 0)
   return;
  first = NULL;
  LIST_FOREACH(b, &c->c_buffers, b_next)
   if (b->b_stream_state == 4 && (first == NULL ||
       b->b_stream_sequence < first->b_stream_sequence))
    first = b;
  if (first == NULL)
   return;
  id = dmac_read(sc, DMAC_ID);
  if (id >= DMAC_STREAM_HW_IDS)
   return;
  LIST_FOREACH(other, &c->c_buffers, b_next)
   if (other->b_stream_state == 1 && other->b_stream_id == id)
    return;
  first->b_stream_id = id;
  first->b_stream_state = 5;
  c->c_stream_latched = first;
  dmac_write(sc, DMAC_FLAGS, 0);
  dmac_write(sc, DMAC_DEST, first->b_map->dm_segs[0].ds_addr);
  dmac_write(sc, DMAC_X_LENGTH, first->b_inflight - 1);
  dmac_write(sc, DMAC_Y_LENGTH, 0);
  dmac_write(sc, DMAC_DEST_STRIDE, 0);
  dmac_write(sc, DMAC_SOURCE_STRIDE, 0);
  bus_space_barrier(sc->sc_bst, sc->sc_bsh, 0, sc->sc_size,
      BUS_SPACE_BARRIER_WRITE);
  dmac_write(sc, DMAC_SUBMIT, 1);
  dmac_read(sc, DMAC_SUBMIT);
  dmac_stream_accept_locked(c);
 }
}

/* The operation and buffer locks exclude finite calls and CPU access. */
int
adi_axi_dmac_stream_start(struct adi_axi_dmac_channel *c)
{
 struct adi_axi_dmac_softc *sc = c->c_sc;
 int error = 0;
 mutex_enter(&sc->sc_lock);
 if (sc->sc_quarantined || sc->sc_channel != c)
  error = EIO;
 else if (sc->sc_stopping)
  error = ESHUTDOWN;
 else if (c->c_streaming || !sc->sc_drained)
  error = EBUSY;
 else if (!sc->sc_debug_idle_45c || !sc->sc_read ||
     sc->sc_stream_source || sc->sc_stream_dest)
  error = EOPNOTSUPP;
 else {
  memset(&c->c_stream_status, 0, sizeof(c->c_stream_status));
  c->c_submit_sequence = c->c_take_sequence = 0;
  c->c_stream_starved = false;
  c->c_stream_latched = NULL;
  sc->sc_drained = false;
  mutex_enter(&sc->sc_irq_lock);
  c->c_streaming = true;
  dmac_write(sc, DMAC_IRQ_PENDING, DMAC_IRQ_ALL);
  dmac_write(sc, DMAC_IRQ_MASK, 0);
  dmac_write(sc, DMAC_CONTROL, DMAC_ENABLE);
  mutex_exit(&sc->sc_irq_lock);
 }
 mutex_exit(&sc->sc_lock);
 return error;
}

int
adi_axi_dmac_stream_submit(struct adi_axi_dmac_buffer *b, size_t size)
{
 struct adi_axi_dmac_channel *c = b->b_channel;
 struct adi_axi_dmac_softc *sc = c->c_sc;
 int error = 0;
 mutex_enter(&sc->sc_lock);
 mutex_enter(&sc->sc_buffer_lock);
 if (!c->c_streaming || sc->sc_quarantined || sc->sc_stopping) {
  error = EIO;
  goto out;
 }
 if (size == 0 || size > b->b_size || size > c->c_max_length ||
     ((size - 1) & c->c_length_mask) != c->c_length_mask) {
  error = EINVAL;
  goto out;
 }
 mutex_enter(&sc->sc_irq_lock);
 if (b->b_inflight || b->b_stream_state == 2 ||
     c->c_stream_status.queued >= DMAC_STREAM_MAX_QUEUED) {
  mutex_exit(&sc->sc_irq_lock);
  error = EBUSY;
  goto out;
 }
 mutex_exit(&sc->sc_irq_lock);
 /* sc_lock excludes another submit; IRQ retirement can only reduce queued. */
 bus_dmamap_sync(sc->sc_dmat, b->b_map, 0, size, BUS_DMASYNC_PREREAD);
 b->b_valid = 0;
 b->b_inflight = size;
 mutex_enter(&sc->sc_irq_lock);
 b->b_stream_state = 4;
 b->b_stream_sequence = c->c_submit_sequence++;
 c->c_stream_status.queued++;
 dmac_stream_pump_locked(c);
 mutex_exit(&sc->sc_irq_lock);
out:
 mutex_exit(&sc->sc_buffer_lock);
 mutex_exit(&sc->sc_lock);
 return error;
}

int
adi_axi_dmac_stream_next(struct adi_axi_dmac_channel *c,
    struct adi_axi_dmac_buffer **result, struct adi_axi_dmac_stream_status *status)
{
 struct adi_axi_dmac_softc *sc = c->c_sc;
 struct adi_axi_dmac_buffer *b, *first = NULL;
 int error = EAGAIN;
 if (status == NULL)
  return EINVAL;
 if (result != NULL)
  *result = NULL;
 mutex_enter(&sc->sc_lock);
 mutex_enter(&sc->sc_buffer_lock);
 mutex_enter(&sc->sc_irq_lock);
 if (!c->c_streaming || sc->sc_quarantined || sc->sc_stopping) {
  error = EIO;
  goto out;
 }
 dmac_stream_pump_locked(c);
 LIST_FOREACH(b, &c->c_buffers, b_next)
  if (b->b_stream_sequence == c->c_take_sequence && b->b_stream_state == 2)
   first = b;
 if (first != NULL && result == NULL)
  error = 0;
 if (first != NULL && result != NULL) {
  bus_dmamap_sync(sc->sc_dmat, first->b_map, 0, first->b_inflight,
      BUS_DMASYNC_POSTREAD);
  first->b_valid = first->b_inflight;
  first->b_inflight = 0;
  first->b_stream_state = 3;
  c->c_stream_status.ready--;
  c->c_stream_status.sequence = c->c_take_sequence++;
  *result = first;
  error = 0;
 }
out:
 *status = c->c_stream_status;
 mutex_exit(&sc->sc_irq_lock);
 mutex_exit(&sc->sc_buffer_lock);
 mutex_exit(&sc->sc_lock);
 return error;
}

int
adi_axi_dmac_guard_stream_prime(const struct adi_axi_dmac_guard *g,
    unsigned int timeout_ms)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_softc *sc;
	unsigned int remaining, started, wait_ticks;

	if (g == NULL || !g->g_prepared || g->g_channel == NULL ||
	    timeout_ms == 0 || timeout_ms > 1000)
		return EINVAL;
	c = g->g_channel;
	sc = c->c_sc;
	remaining = timeout_ms * 1000;
	started = (unsigned int)getticks();
	wait_ticks = MAX(1, mstohz(timeout_ms));
	do {
		int error = EAGAIN;

		mutex_enter(&sc->sc_lock);
		mutex_enter(&sc->sc_buffer_lock);
		mutex_enter(&sc->sc_irq_lock);
		if (!c->c_streaming || sc->sc_channel != c || sc->sc_quarantined ||
		    sc->sc_stopping) {
			error = EIO;
		} else {
			uint32_t done;

			dmac_stream_pump_locked(c);
			done = dmac_read(sc, DMAC_DONE);
			if (c->c_stream_status.discontinuities != 0 ||
			    c->c_stream_status.completed_blocks != 0)
				error = EOVERFLOW;
			else {
				const struct adi_axi_dmac_buffer *b;

				LIST_FOREACH(b, &c->c_buffers, b_next) {
					if (b->b_stream_sequence == 0 && b->b_stream_state == 1) {
						error = (done & __BIT(b->b_stream_id)) ?
						    EOVERFLOW : 0;
						break;
					}
				}
			}
		}
		mutex_exit(&sc->sc_irq_lock);
		mutex_exit(&sc->sc_buffer_lock);
		mutex_exit(&sc->sc_lock);
		if (error != EAGAIN)
			return error;
		if ((unsigned int)getticks() - started >= wait_ticks ||
		    remaining-- == 0)
			return ETIMEDOUT;
		/* This deadline also terminates if hardclock cannot advance. */
		delay(1);
	} while (true);
}

int
adi_axi_dmac_guard_stream_snapshot(const struct adi_axi_dmac_guard *g,
    struct adi_axi_dmac_stream_snapshot *snapshot)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_softc *sc;
	int error = 0;

	if (g == NULL || !g->g_prepared || g->g_channel == NULL ||
	    snapshot == NULL)
		return EINVAL;
	c = g->g_channel;
	sc = c->c_sc;
	mutex_enter(&sc->sc_lock);
	mutex_enter(&sc->sc_buffer_lock);
	mutex_enter(&sc->sc_irq_lock);
	memset(snapshot, 0, sizeof(*snapshot));
	if (sc->sc_channel != c) {
		error = EIO;
	} else {
		const struct adi_axi_dmac_buffer *b;

		snapshot->status = c->c_stream_status;
		snapshot->control = dmac_read(sc, DMAC_CONTROL);
		snapshot->irq_pending = dmac_read(sc, DMAC_IRQ_PENDING);
		snapshot->irq_mask = dmac_read(sc, DMAC_IRQ_MASK);
		snapshot->transfer_id = dmac_read(sc, DMAC_ID);
		snapshot->submit = dmac_read(sc, DMAC_SUBMIT);
		snapshot->done = dmac_read(sc, DMAC_DONE);
		if (sc->sc_debug_idle_45c)
			snapshot->debug_status = dmac_read(sc, DMAC_DEBUG_STATUS);
		LIST_FOREACH(b, &c->c_buffers, b_next) {
			if (b->b_stream_state < ADI_AXI_DMAC_STREAM_STATES)
				snapshot->states[b->b_stream_state]++;
		}
	}
	mutex_exit(&sc->sc_irq_lock);
	mutex_exit(&sc->sc_buffer_lock);
	mutex_exit(&sc->sc_lock);
	return error;
}

int
adi_axi_dmac_guard_stream_notify(const struct adi_axi_dmac_guard *g,
    void (*notify)(void *), void *cookie)
{
 struct adi_axi_dmac_softc *sc;
 if (g == NULL || g->g_channel == NULL)
  return EINVAL;
 sc = g->g_sc;
 mutex_enter(&sc->sc_lock);
 mutex_enter(&sc->sc_irq_lock);
 g->g_channel->c_stream_notify = notify;
 g->g_channel->c_stream_cookie = cookie;
 mutex_exit(&sc->sc_irq_lock);
 mutex_exit(&sc->sc_lock);
 return 0;
}

int
adi_axi_dmac_guard_stream_start(const struct adi_axi_dmac_guard *g)
{
 if (g == NULL || !g->g_prepared || g->g_channel == NULL)
  return EINVAL;
 return adi_axi_dmac_stream_start(g->g_channel);
}
int
adi_axi_dmac_guard_stream_submit(const struct adi_axi_dmac_guard *g,
    struct adi_axi_dmac_buffer *b, size_t size)
{
 if (g == NULL || b == NULL || b->b_channel != g->g_channel)
  return EINVAL;
 return adi_axi_dmac_stream_submit(b, size);
}
int
adi_axi_dmac_guard_stream_next(const struct adi_axi_dmac_guard *g,
    struct adi_axi_dmac_buffer **result, struct adi_axi_dmac_stream_status *status)
{
 if (g == NULL || !g->g_prepared || g->g_channel == NULL)
  return EINVAL;
 return adi_axi_dmac_stream_next(g->g_channel, result, status);
}
int
adi_axi_dmac_guard_stream_stop(const struct adi_axi_dmac_guard *g, unsigned ms)
{
 if (g == NULL || !g->g_prepared || g->g_channel == NULL)
  return EINVAL;
 return adi_axi_dmac_stop(g->g_channel, ms);
}

static int
adiaxidmac_intr(void *cookie)
{
	struct adi_axi_dmac_softc *sc = cookie;
	uint32_t pending;

	mutex_enter(&sc->sc_irq_lock);
	pending = dmac_read(sc, DMAC_IRQ_PENDING) & DMAC_IRQ_ALL;
	if (pending != 0) {
		/* Refill can raise another SOT before a later W1C write. */
		dmac_write(sc, DMAC_IRQ_PENDING, pending);
		if (sc->sc_channel != NULL && sc->sc_channel->c_streaming)
			dmac_stream_pump_locked(sc->sc_channel);
		if (sc->sc_channel != NULL && sc->sc_channel->c_stream_notify != NULL)
			sc->sc_channel->c_stream_notify(sc->sc_channel->c_stream_cookie);
		if (sc->sc_pending && (pending & DMAC_IRQ_EOT) &&
		    (dmac_read(sc, DMAC_DONE) & __BIT(sc->sc_id))) {
			sc->sc_done = true;
			cv_signal(&sc->sc_done_cv);
		}
	}
	mutex_exit(&sc->sc_irq_lock);
	return pending != 0;
}

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "adi,axi-dmac-1.00.a" },
	DEVICE_COMPAT_EOL
};

static int
adiaxidmac_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
adiaxidmac_attach(device_t parent, device_t self, void *aux)
{
	struct adi_axi_dmac_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	bus_addr_t address;
	uint32_t core_version, interface;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;
	sc->sc_dmat = faa->faa_dmat;
	if (fdtbus_get_reg(sc->sc_phandle, 0, &address, &sc->sc_size) != 0 ||
	    sc->sc_size < DMAC_DEBUG_STATUS + 4) {
		aprint_error(": invalid registers\n");
		return;
	}
	sc->sc_clk = fdtbus_clock_get_index(sc->sc_phandle, 0);
	if (sc->sc_clk == NULL || clk_enable(sc->sc_clk) != 0) {
		aprint_error(": register clock unavailable\n");
		return;
	}
	if (bus_space_map(sc->sc_bst, address, sc->sc_size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": register mapping failed\n");
		clk_disable(sc->sc_clk);
		return;
	}
	core_version = dmac_read(sc, DMAC_VERSION);
	interface = dmac_read(sc, DMAC_INTERFACE);
	if (dmac_interface(sc, core_version, interface) != 0) {
		aprint_error(": unsupported core_version/interface %#x/%#x\n", core_version, interface);
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, sc->sc_size);
		clk_disable(sc->sc_clk);
		return;
	}
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->sc_buffer_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->sc_irq_lock, MUTEX_DEFAULT, IPL_BIO);
	cv_init(&sc->sc_done_cv, "adidma");
	sc->sc_shutdownhook = shutdownhook_establish(adiaxidmac_shutdown_final, sc);
	if (sc->sc_shutdownhook == NULL) {
		/* No new DMA is permitted without a final reset interlock. */
		if ((dmac_read(sc, DMAC_CONTROL) & DMAC_ENABLE) ||
		    dmac_read(sc, DMAC_SUBMIT) != 0)
			dmac_shutdown_halt(sc);
		aprint_error(": shutdown hook unavailable; capture disabled\n");
		cv_destroy(&sc->sc_done_cv);
		mutex_destroy(&sc->sc_irq_lock);
		mutex_destroy(&sc->sc_buffer_lock);
		mutex_destroy(&sc->sc_lock);
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, sc->sc_size);
		clk_disable(sc->sc_clk);
		return;
	}
	if (!pmf_device_register1(self, NULL, NULL, adiaxidmac_shutdown)) {
		sc->sc_quarantined = true;
		aprint_error(": power management unavailable; capture disabled\n");
	}
	if (dmac_read(sc, DMAC_CONTROL) & DMAC_ENABLE) {
		sc->sc_quarantined = true;
		aprint_error(": inherited enabled engine; resources retained\n");
	} else {
		dmac_write(sc, DMAC_IRQ_MASK, DMAC_IRQ_ALL);
		sc->sc_ih = fdtbus_intr_establish_xname(sc->sc_phandle, 0,
		    IPL_BIO, FDT_INTR_MPSAFE, adiaxidmac_intr, sc, device_xname(self));
		if (sc->sc_ih == NULL) {
			sc->sc_quarantined = true;
			aprint_error(": interrupt unavailable; capture disabled\n");
		} else {
			aprint_naive("\n");
			aprint_normal(": ADI AXI DMAC %u.%u.%c, checked drain required\n",
			    (core_version >> 16) & 255, (core_version >> 8) & 255, core_version & 255);
		}
	}
	RUN_ONCE(&dmac_registry_once, dmac_registry_init);
	mutex_enter(&dmac_registry_lock);
	LIST_INSERT_HEAD(&dmacs, sc, sc_next);
	mutex_exit(&dmac_registry_lock);
}

/* Built-in only: retained IRQ/MMIO/clock lifetime also covers quarantine. */
CFATTACH_DECL_NEW(adiaxidmac, sizeof(struct adi_axi_dmac_softc),
    adiaxidmac_match, adiaxidmac_attach, NULL, NULL);
