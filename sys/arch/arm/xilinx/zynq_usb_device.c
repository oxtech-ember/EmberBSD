/* SPDX-License-Identifier: BSD-2-Clause */
/* Native Zynq USBPS peripheral attachment and copied function interface. */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/intr.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/pmf.h>
#include <sys/systm.h>
#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <arm/xilinx/zynq_usb_devicevar.h>

struct zynqudc_softc {
	device_t dev;
	struct zynqudc core;
	kmutex_t lock;
	kcondvar_t cv;
	callout_t timer;
	void *ih;
	void *softih;
	void *shutdownhook;
	bool stopping, reset_drained;
	uint64_t cancel_epoch[ZUDC_SLOTS];
	struct clk *clock;
	bool ready, opened, closing;
	bool phy_initial_valid;
	uint8_t phy_function_initial, phy_otg_initial;
	bool users[ZUDC_SLOTS];
	bool endpoint_closed[ZUDC_SLOTS];
	unsigned operations;
};

extern struct cfdriver zynqudc_cd;
static int zynqudc_match(device_t, cfdata_t, void *);
static void zynqudc_attach(device_t, device_t, void *);
CFATTACH_DECL_NEW(zynqudc, sizeof(struct zynqudc_softc),
    zynqudc_match, zynqudc_attach, NULL, NULL);
static int zynqudc_irq(void *);
static void zynqudc_softirq(void *);
static void zynqudc_timer(void *);
static int zynqudc_open(dev_t, int, int, struct lwp *);
static int zynqudc_close(dev_t, int, int, struct lwp *);
static int zynqudc_ioctl(dev_t, u_long, void *, int, struct lwp *);

dev_type_open(zynqudc_open);
dev_type_close(zynqudc_close);
dev_type_ioctl(zynqudc_ioctl);
const struct cdevsw usbperiph_cdevsw = {
	.d_open = zynqudc_open, .d_close = zynqudc_close,
	.d_read = noread, .d_write = nowrite, .d_ioctl = zynqudc_ioctl,
	.d_stop = nostop, .d_tty = notty, .d_poll = nopoll,
	.d_mmap = nommap, .d_kqfilter = nokqfilter, .d_discard = nodiscard,
	.d_flag = D_OTHER
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "xlnx,zynq-usb-2.20a" },
	DEVICE_COMPAT_EOL
};

static int
zynqudc_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *faa = aux;
	if (!zynqudc_role(fdtbus_get_string(faa->faa_phandle, "dr_mode")))
		return 0;
	return of_compatible_match(faa->faa_phandle, compat_data);
}

static int
zynqudc_alloc(struct zynqudc_softc *sc, struct zudc_dma *d,
    size_t size, size_t alignment)
{
	int nseg, error;

	error = bus_dmamem_alloc(sc->core.dmat, size, alignment, 0, &d->seg,
	    1, &nseg, BUS_DMA_WAITOK);
	if (error != 0)
		return error;
	error = bus_dmamem_map(sc->core.dmat, &d->seg, nseg, size, &d->kva,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT);
	if (error != 0)
		goto free_mem;
	error = bus_dmamap_create(sc->core.dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK, &d->map);
	if (error != 0)
		goto unmap_mem;
	error = bus_dmamap_load(sc->core.dmat, d->map, d->kva, size, NULL,
	    BUS_DMA_WAITOK);
	if (error != 0)
		goto destroy_map;
	d->size = size;
	memset(d->kva, 0, size);
	return 0;
 destroy_map:
	bus_dmamap_destroy(sc->core.dmat, d->map);
	d->map = NULL;
 unmap_mem:
	bus_dmamem_unmap(sc->core.dmat, d->kva, size);
	d->kva = NULL;
 free_mem:
	bus_dmamem_free(sc->core.dmat, &d->seg, 1);
	return error;
}

static void
zynqudc_free(struct zynqudc_softc *sc, struct zudc_dma *d)
{
	if (d->size == 0)
		return;
	bus_dmamap_unload(sc->core.dmat, d->map);
	bus_dmamap_destroy(sc->core.dmat, d->map);
	bus_dmamem_unmap(sc->core.dmat, d->kva, d->size);
	bus_dmamem_free(sc->core.dmat, &d->seg, 1);
	memset(d, 0, sizeof(*d));
}

/* ULPI operations have finite wake and access budgets. */
static int
zynqudc_ulpi(struct zynqudc_softc *sc, unsigned addr, unsigned data,
    bool write, uint8_t *result)
{
	struct zynqudc *c = &sc->core;
	uint32_t reg;

	reg = bus_space_read_4(c->bst, c->bsh, ZUDC_ULPI);
	if ((reg & 0x08000000U) == 0) {
		bus_space_write_4(c->bst, c->bsh, ZUDC_ULPI, 0x80000000U);
		for (unsigned us = 0;; us++) {
			if ((bus_space_read_4(c->bst, c->bsh, ZUDC_ULPI) &
			    0x80000000U) == 0)
				break;
			if (us == ZUDC_POLL_US)
				return ETIMEDOUT;
			delay(1);
		}
	}
	reg = 0x40000000U | (addr << 16) | data;
	if (write)
		reg |= 0x20000000U;
	bus_space_write_4(c->bst, c->bsh, ZUDC_ULPI, reg);
	for (unsigned us = 0; us < ZUDC_POLL_US; us++) {
		reg = bus_space_read_4(c->bst, c->bsh, ZUDC_ULPI);
		if ((reg & 0x40000000U) == 0) {
			if (result != NULL)
				*result = reg >> 8;
			return 0;
		}
		delay(1);
	}
	return ETIMEDOUT;
}

static int
zynqudc_phy(struct zynqudc_softc *sc)
{
	uint8_t value;
	int error;

	/* Clear host VBUS, charge and discharge bits in ULPI OTG control. */
	error = zynqudc_ulpi(sc, ZUDC_ULPI_OTG_CLEAR,
	    ZUDC_ULPI_OTG_VBUS | ZUDC_ULPI_OTG_DP_PULLDOWN |
	    ZUDC_ULPI_OTG_ID_PULLUP, true, NULL);
	if (error != 0)
		return error;
	/* Device termination requires D- pulldown, never a host D+ pulldown. */
	error = zynqudc_ulpi(sc, ZUDC_ULPI_OTG_SET,
	    ZUDC_ULPI_OTG_DM_PULLDOWN, true, NULL);
	if (error != 0)
		return error;
	/* UG585: leave D+ detached until prepared EP0 and IRQ precede RUN. */
	error = zynqudc_ulpi(sc, ZUDC_ULPI_FUNCTION, 0, false, &value);
	if (error != 0)
		return error;
	value = (value & ~(ZUDC_ULPI_XCVR | ZUDC_ULPI_OPMODE |
	    ZUDC_ULPI_TERMSELECT)) | ZUDC_ULPI_XCVR_FS | ZUDC_ULPI_SUSPENDM;
	error = zynqudc_ulpi(sc, ZUDC_ULPI_FUNCTION, value, true, NULL);
	if (error != 0)
		return error;
	/* Cache only these non-destructive register reads before RUN. */
	error = zynqudc_ulpi(sc, ZUDC_ULPI_FUNCTION, 0, false,
	    &sc->phy_function_initial);
	if (error != 0)
		return error;
	error = zynqudc_ulpi(sc, 0x0a, 0, false, &sc->phy_otg_initial);
	if (error == 0)
		sc->phy_initial_valid = true;
	return error;
}

/* The final reset hook cannot wait for hardclock, soft interrupts, or users. */
static bool
zynqudc_idle(struct zynqudc_softc *sc)
{
	struct zynqudc *c = &sc->core;
	if (bus_space_read_4(c->bst, c->bsh, ZUDC_CMD) & ZUDC_RUN)
		return false;
	if (bus_space_read_4(c->bst, c->bsh, ZUDC_PRIME) != 0 ||
	    bus_space_read_4(c->bst, c->bsh, ZUDC_FLUSH) != 0 ||
	    bus_space_read_4(c->bst, c->bsh, ZUDC_STAT) != 0)
		return false;
	for (unsigned i = 0; i < c->nep * 2; i++)
		if (c->req[i].active || c->req[i].retained)
			return false;
	return true;
}

static bool
zynqudc_shutdown(device_t dev, int howto)
{
	struct zynqudc_softc *sc = device_private(dev);
	int error;
	(void)howto;

	if (!mutex_tryenter(&sc->lock))
		return false;
	sc->stopping = true;
	sc->reset_drained = false;
	error = zynqudc_disconnect(&sc->core);
	cv_broadcast(&sc->cv);
	callout_stop(&sc->timer);
	mutex_exit(&sc->lock);
	/* PMF runs before the final interrupt-disabled phase and may wait. */
	callout_halt(&sc->timer, NULL);
	if (!mutex_tryenter(&sc->lock))
		return false;
	if (error == 0 && !sc->core.faulted && zynqudc_idle(sc))
		sc->reset_drained = true;
	bool drained = sc->reset_drained;
	mutex_exit(&sc->lock);
	return drained;
}

static void __dead
zynqudc_shutdown_halt(struct zynqudc_softc *sc)
{
	(void)splhigh();
	printf("%s: USB DMA shutdown is unproved; reset blocked. "
	    "Remove power before restarting.\n", device_xname(sc->dev));
	for (;;)
		SPINLOCK_BACKOFF_HOOK;
}

static void
zynqudc_shutdown_final(void *cookie)
{
	struct zynqudc_softc *sc = cookie;
	if (!mutex_tryenter(&sc->lock))
		zynqudc_shutdown_halt(sc);
	if (!sc->stopping || !sc->reset_drained || sc->core.connected ||
	    sc->core.faulted || !zynqudc_idle(sc))
		zynqudc_shutdown_halt(sc);
	bus_space_write_4(sc->core.bst, sc->core.bsh, ZUDC_INTR, 0);
	if (bus_space_read_4(sc->core.bst, sc->core.bsh, ZUDC_INTR) != 0)
		zynqudc_shutdown_halt(sc);
	/* Keep the lock and all mappings until the platform reset. */
}

static bool
zynqudc_register_shutdown(struct zynqudc_softc *sc)
{
	sc->shutdownhook = shutdownhook_establish(zynqudc_shutdown_final, sc);
	if (sc->shutdownhook == NULL ||
	    !pmf_device_register1(sc->dev, NULL, NULL, zynqudc_shutdown)) {
		/* No controller admission without both reset coordination hooks. */
		sc->stopping = true;
		sc->core.faulted = true;
		bus_space_write_4(sc->core.bst, sc->core.bsh, ZUDC_INTR, 0);
		if (!zynqudc_idle(sc) && sc->shutdownhook == NULL)
			zynqudc_shutdown_halt(sc);
		return false;
	}
	return true;
}

static void
zynqudc_attach(device_t parent, device_t self, void *aux)
{
	struct zynqudc_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	struct zynqudc *c = &sc->core;
	bus_addr_t addr;
	bus_size_t size;
	const int phandle = faa->faa_phandle;
	const char *phy = fdtbus_get_string(phandle, "phy_type");
	char intrstr[128];
	int error;

	sc->dev = self;
	c->bst = faa->faa_bst;
	c->dmat = faa->faa_dmat;
	if (phy == NULL || strcmp(phy, "ulpi") != 0 ||
	    fdtbus_get_reg(phandle, 0, &addr, &size) != 0 || size < 0x200 ||
	    !fdtbus_intr_str(phandle, 0, intrstr, sizeof(intrstr))) {
		aprint_error(": invalid ULPI, register, or interrupt description\n");
		return;
	}
	sc->clock = fdtbus_clock_get_index(phandle, 0);
	if (sc->clock == NULL || clk_enable(sc->clock) != 0) {
		aprint_error(": cannot enable peripheral clock\n");
		return;
	}
	if (bus_space_map(c->bst, addr, size, 0, &c->bsh) != 0) {
		aprint_error(": cannot map registers\n");
		goto disable_clock;
	}
	uint32_t hw = bus_space_read_4(c->bst, c->bsh, 0x0c);
	c->nep = (hw >> 1) & 31;
	if ((hw & 1) == 0 || c->nep < 3 || c->nep > ZUDC_EP_COUNT) {
		aprint_error(": unsupported endpoint capabilities\n");
		goto unmap_regs;
	}
	error = zynqudc_alloc(sc, &c->descriptors, 4096, ZUDC_QH_ALIGN);
	if (error != 0)
		goto unmap_regs;
	c->qh = c->descriptors.kva;
	c->td = (void *)((uint8_t *)c->descriptors.kva + 2048);
	for (unsigned i = 0; i < c->nep * 2; i++) {
		error = zynqudc_alloc(sc, &c->req[i].data,
		    USBPERIPH_MAX_XFER, 4096);
		if (error != 0)
			goto free_dma;
	}
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_SOFTSERIAL);
	cv_init(&sc->cv, "usbperiph");
	callout_init(&sc->timer, CALLOUT_MPSAFE);
	callout_setfunc(&sc->timer, zynqudc_timer, sc);
	error = zynqudc_init(c);
	if (error == 0)
		error = zynqudc_phy(sc);
	if (error != 0) {
		aprint_error(": device/PHY initialization failed: %d\n", error);
		goto destroy_locks;
	}
	sc->softih = softint_establish(SOFTINT_SERIAL | SOFTINT_MPSAFE,
	    zynqudc_softirq, sc);
	if (sc->softih == NULL)
		goto destroy_locks;
	sc->ih = fdtbus_intr_establish_xname(phandle, 0, IPL_VM,
	    FDT_INTR_MPSAFE, zynqudc_irq, sc, device_xname(self));
	if (sc->ih == NULL) {
		aprint_error(": cannot establish interrupt\n");
		softint_disestablish(sc->softih);
		goto destroy_locks;
	}
	if (!zynqudc_register_shutdown(sc)) {
		aprint_error(": reset coordination unavailable; peripheral disabled\n");
		return; /* Quarantine initialized mappings and the registered IRQ. */
	}
	sc->ready = true;
	callout_schedule(&sc->timer, hz);
	aprint_normal(": native USB peripheral, %u endpoints, interrupting on %s\n",
	    c->nep, intrstr);
	return;
 destroy_locks:
	callout_destroy(&sc->timer);
	cv_destroy(&sc->cv);
	mutex_destroy(&sc->lock);
 free_dma:
	/* No request was primed: all allocation failure paths own their DMA. */
	for (unsigned i = 0; i < c->nep * 2; i++)
		zynqudc_free(sc, &c->req[i].data);
	zynqudc_free(sc, &c->descriptors);
 unmap_regs:
	bus_space_unmap(c->bst, c->bsh, size);
 disable_clock:
	clk_disable(sc->clock);
}

static int
zynqudc_irq(void *cookie)
{
	struct zynqudc_softc *sc = cookie;
	struct zynqudc *c = &sc->core;
	uint32_t status = bus_space_read_4(c->bst, c->bsh, ZUDC_STS);
	uint32_t mask = bus_space_read_4(c->bst, c->bsh, ZUDC_INTR);
	if ((status & mask & ZUDC_INT_MASK) == 0)
		return 0;
	/* The hard interrupt never takes the sleepable function lock. */
	bus_space_write_4(c->bst, c->bsh, ZUDC_INTR, 0);
	softint_schedule(sc->softih);
	return 1;
}

static void
zynqudc_softirq(void *cookie)
{
	struct zynqudc_softc *sc = cookie;
	mutex_enter(&sc->lock);
	if (!sc->stopping)
		(void)zynqudc_intr(&sc->core);
	if (!sc->stopping && sc->core.connected && !sc->core.faulted)
		bus_space_write_4(sc->core.bst, sc->core.bsh, ZUDC_INTR,
		    ZUDC_INT_MASK);
	cv_broadcast(&sc->cv);
	mutex_exit(&sc->lock);
}

static void
zynqudc_timer(void *cookie)
{
	struct zynqudc_softc *sc = cookie;
	mutex_enter(&sc->lock);
	if (!sc->stopping) {
		zynqudc_tick(&sc->core);
		cv_broadcast(&sc->cv);
		callout_schedule(&sc->timer, hz);
	}
	mutex_exit(&sc->lock);
}

static struct zynqudc_softc *
zynqudc_lookup(dev_t dev)
{
	struct zynqudc_softc *sc = device_lookup_private(&zynqudc_cd, minor(dev));
	return sc != NULL && sc->ready ? sc : NULL;
}

static int
zynqudc_open(dev_t dev, int flags, int mode, struct lwp *l)
{
	struct zynqudc_softc *sc = zynqudc_lookup(dev);
	int error = 0;
	(void)mode;
	(void)l;
	if (sc == NULL)
		return ENXIO;
	if ((flags & (FREAD | FWRITE)) != (FREAD | FWRITE))
		return EACCES;
	mutex_enter(&sc->lock);
	if (sc->stopping || sc->opened || sc->closing)
		error = EBUSY;
	else if (sc->core.faulted)
		error = EIO;
	else {
		memset(sc->endpoint_closed, 0, sizeof(sc->endpoint_closed));
		sc->opened = true;
	}
	mutex_exit(&sc->lock);
	return error;
}

static int
zynqudc_close(dev_t dev, int flags, int mode, struct lwp *l)
{
	struct zynqudc_softc *sc = zynqudc_lookup(dev);
	int error;
	(void)flags;
	(void)mode;
	(void)l;
	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->lock);
	sc->closing = true;
	error = zynqudc_disconnect(&sc->core);
	cv_broadcast(&sc->cv);
	while (sc->operations != 0)
		cv_wait(&sc->cv, &sc->lock);
	sc->opened = false;
	sc->closing = false;
	/* Buffers remain controller-owned until reboot, including failed drain. */
	mutex_exit(&sc->lock);
	return error;
}

static void
zynqudc_snapshot(struct zynqudc_softc *sc, struct usbperiph_snapshot *s)
{
	const struct zynqudc *c = &sc->core;

	/* Includes all reserved fields and any ABI padding. */
	memset(s, 0, sizeof(*s));
	s->version = USBPERIPH_SNAPSHOT_VERSION;
	s->generation = c->generation;
	s->irq_count = c->irq_count;
	s->setup_count = c->setup_count;
	s->reset_count = c->reset_count;
	s->disconnect_count = c->disconnect_count;
	s->last_irq_status = c->last_irq_status;
	memcpy(s->last_setup, c->last_setup, sizeof(s->last_setup));
	s->flags = (c->connected ? USBPERIPH_SNAPSHOT_CONNECTED : 0) |
	    (c->faulted ? USBPERIPH_SNAPSHOT_FAULTED : 0) |
	    (c->suspended ? USBPERIPH_SNAPSHOT_SUSPENDED : 0) |
	    (sc->stopping ? USBPERIPH_SNAPSHOT_STOPPING : 0) |
	    (sc->phy_initial_valid ? USBPERIPH_SNAPSHOT_PHY_INITIAL_VALID : 0);
	s->configuration = c->configuration;
	s->address = c->address;
	s->ep0_phase = c->ep0_phase;
	for (unsigned i = 0; i < c->nep * 2; i++) {
		if (c->req[i].active)
			s->active_slots |= 1U << i;
		if (c->req[i].retained)
			s->retained_slots |= 1U << i;
	}
	s->phy_function_initial = sc->phy_function_initial;
	s->phy_otg_initial = sc->phy_otg_initial;
	/* These registers have no read-to-clear semantics. Never read ULPI here. */
	s->command = bus_space_read_4(c->bst, c->bsh, ZUDC_CMD);
	s->status = bus_space_read_4(c->bst, c->bsh, ZUDC_STS);
	s->interrupt_enable = bus_space_read_4(c->bst, c->bsh, ZUDC_INTR);
	s->port = bus_space_read_4(c->bst, c->bsh, ZUDC_PORT);
	s->otg = bus_space_read_4(c->bst, c->bsh, ZUDC_OTG);
	s->mode = bus_space_read_4(c->bst, c->bsh, ZUDC_MODE);
	s->setup_status = bus_space_read_4(c->bst, c->bsh, ZUDC_SETUP);
	s->prime = bus_space_read_4(c->bst, c->bsh, ZUDC_PRIME);
	s->flush = bus_space_read_4(c->bst, c->bsh, ZUDC_FLUSH);
	s->endpoint_status = bus_space_read_4(c->bst, c->bsh, ZUDC_STAT);
	s->complete = bus_space_read_4(c->bst, c->bsh, ZUDC_COMPLETE);
	s->epctrl0 = bus_space_read_4(c->bst, c->bsh, ZUDC_EPCTRL(0));
}

static int
zynqudc_endpoint_gate(struct zynqudc_softc *sc,
    const struct usbperiph_endpoint_gate *gate)
{
	struct zynqudc *c = &sc->core;
	unsigned ep = gate->endpoint, slot;
	bool present = false;

	if (gate->enable > 1 || (ep & ~0x8fU) != 0 ||
	    (ep & 15) == 0 || (ep & 15) >= c->nep)
		return EINVAL;
	if (c->faulted)
		return EIO;
	if (gate->generation != 0 && gate->generation != c->generation)
		return ESTALE;
	if (!c->valid_config)
		return ENXIO;
	/* Validate installed descriptors, so a function can gate before CONNECT. */
	for (unsigned off = 9; off < c->config.config_length;) {
		const uint8_t *d = c->config.config + off;
		if (d[1] == 5 && d[2] == ep && d[3] == 2) {
			present = true;
			break;
		}
		off += d[0];
	}
	if (!present)
		return EINVAL;
	slot = (ep & 15) * 2 + ((ep >> 7) & 1);
	if (gate->enable != 0) {
		sc->endpoint_closed[slot] = false;
		return 0;
	}
	/* Close admission before drain, including a request outside copyin. */
	sc->endpoint_closed[slot] = true;
	sc->cancel_epoch[slot]++;
	return zynqudc_cancel(c, ep, ECANCELED);
}

static int
zynqudc_xfer(struct zynqudc_softc *sc, struct usbperiph_xfer *x)
{
	struct zynqudc *c = &sc->core;
	unsigned slot;
	void *user;
	int error;
	uint64_t generation, epoch;

	if ((x->endpoint & ~0x8fU) != 0 || (x->endpoint & 15) == 0 ||
	    (x->endpoint & 15) >= c->nep || x->length > USBPERIPH_MAX_XFER ||
	    x->timeout_ms == 0 || x->timeout_ms > 60000 ||
	    x->buffer > (uint64_t)UINTPTR_MAX)
		return EINVAL;
	slot = (x->endpoint & 15) * 2 + ((x->endpoint >> 7) & 1);
	if (c->faulted || c->req[slot].retained)
		return EIO;
	if (sc->endpoint_closed[slot])
		return EPIPE;
	if (sc->users[slot] || c->req[slot].active)
		return EBUSY;
	sc->users[slot] = true;
	generation = c->generation;
	epoch = sc->cancel_epoch[slot];
	user = (void *)(uintptr_t)x->buffer;
	x->actual = 0;
	/* User faults and copying can sleep. Never hold the interrupt lock here. */
	mutex_exit(&sc->lock);
	error = 0;
	if ((slot & 1) && x->length != 0)
		error = copyin(user, c->req[slot].data.kva, x->length);
	mutex_enter(&sc->lock);
	if (error != 0)
		goto out;
	if (c->faulted || sc->endpoint_closed[slot]) {
		error = c->faulted ? EIO : EPIPE;
		goto out;
	}
	if (sc->stopping || sc->closing || !sc->opened || generation != c->generation ||
	    epoch != sc->cancel_epoch[slot]) {
		error = ESTALE;
		goto out;
	}
	error = zynqudc_submit(c, x->endpoint, x->length, x->generation);
	if (error != 0)
		goto out;
	/* Absolute deadline: unrelated endpoint IRQs cannot extend the timeout. */
	unsigned start = (unsigned)getticks();
	unsigned budget = (unsigned)mstohz(x->timeout_ms);
	while (c->req[slot].active && !c->faulted) {
		unsigned elapsed = (unsigned)getticks() - start;
		if (elapsed >= budget) {
			error = ETIMEDOUT;
			break;
		}
		error = cv_timedwait_sig(&sc->cv, &sc->lock, (int)(budget - elapsed));
		if (error == EWOULDBLOCK)
			error = ETIMEDOUT;
		if (error != 0)
			break;
	}
	if (c->faulted) {
		error = EIO;
	} else if (error != 0) {
		if (zynqudc_cancel(c, x->endpoint, error) != 0)
			error = EIO;
	} else {
		error = c->req[slot].status;
	}
	x->generation = c->req[slot].generation;
	if (error == 0) {
		x->actual = c->req[slot].actual;
		if ((slot & 1) == 0 && x->actual != 0) {
			mutex_exit(&sc->lock);
			error = copyout(c->req[slot].data.kva, user, x->actual);
			mutex_enter(&sc->lock);
			if (error == 0 && (c->faulted || sc->endpoint_closed[slot] ||
			    epoch != sc->cancel_epoch[slot] ||
			    x->generation != c->generation)) {
				error = c->faulted ? EIO :
				    (sc->endpoint_closed[slot] ? EPIPE : ESTALE);
				x->actual = 0;
			}
		}
	}
 out:
	sc->users[slot] = false;
	cv_broadcast(&sc->cv);
	return error;
}

static int
zynqudc_ioctl(dev_t dev, u_long command, void *data, int flags, struct lwp *l)
{
	struct zynqudc_softc *sc = zynqudc_lookup(dev);
	int error = 0;
	(void)flags;
	(void)l;

	if (sc == NULL)
		return ENXIO;
	mutex_enter(&sc->lock);
	if (sc->stopping || !sc->opened || sc->closing) {
		error = ENXIO;
		goto unlock;
	}
	sc->operations++;
	switch (command) {
	case USBPERIPH_IOC_CONFIGURE:
		error = zynqudc_configure(&sc->core, data);
		break;
	case USBPERIPH_IOC_CONNECT:
		error = zynqudc_connect(&sc->core);
		break;
	case USBPERIPH_IOC_DISCONNECT:
		error = zynqudc_disconnect(&sc->core);
		break;
	case USBPERIPH_IOC_SNAPSHOT:
		zynqudc_snapshot(sc, data);
		break;
	case USBPERIPH_IOC_ENDPOINT_GATE:
		error = zynqudc_endpoint_gate(sc, data);
		break;
	case USBPERIPH_IOC_XFER:
		error = zynqudc_xfer(sc, data);
		break;
	case USBPERIPH_IOC_CANCEL:
		if ((*(uint32_t *)data & 15) == 0)
			error = EINVAL;
		else {
			uint32_t ep = *(uint32_t *)data;
			if ((ep & ~0x8fU) == 0 && (ep & 15) < sc->core.nep)
				sc->cancel_epoch[(ep & 15) * 2 + ((ep >> 7) & 1)]++;
			error = zynqudc_cancel(&sc->core, ep, ECANCELED);
		}
		break;
	case USBPERIPH_IOC_GET_EVENT: {
		unsigned start = (unsigned)getticks();
		while (!sc->core.event_pending && !sc->closing &&
		    !sc->core.faulted && !sc->stopping && error == 0) {
			unsigned elapsed = (unsigned)getticks() - start;
			if (elapsed >= (unsigned)hz) {
				error = EWOULDBLOCK;
				break;
			}
			error = cv_timedwait_sig(&sc->cv, &sc->lock,
			    hz - (int)elapsed);
		}
		if (error == 0 && sc->core.event_pending) {
			*(struct usbperiph_event *)data = sc->core.event;
			sc->core.event_pending = false;
		} else if (error == 0) {
			error = sc->closing || sc->stopping ? ENXIO : EIO;
		}
		break;
	}
	case USBPERIPH_IOC_REPLY_SETUP:
		error = zynqudc_reply(&sc->core, data);
		break;
	default:
		error = ENOTTY;
		break;
	}
	sc->operations--;
	cv_broadcast(&sc->cv);
 unlock:
	mutex_exit(&sc->lock);
	return error;
}
