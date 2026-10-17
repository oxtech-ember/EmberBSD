/* Origin: EmberBSD test-only external ucom transport parent, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/ucomvar.h>
#include <dev/usb/umoxa_g2_txq.h>
#include <rump/rumpuser.h>
#include "umock_test.h"

struct umock_port {
	int started;
	device_t device;
	struct ucom_softc *child;
	struct umock_record record;
};
struct umock_softc {
	kmutex_t lock;
	int mux;
	struct umoxa_g2_txq txq;
	struct umock_port port[7];
};
static struct umock_softc *mock;

static int
mock_attach(void *arg, int port, struct ucom_softc *child)
{
	struct umock_softc *s = arg;

	if (port == 5)
		return EIO;
	s->port[port].child = child;
	return 0;
}

static int
mock_start(void *arg, int port, uint64_t epoch)
{
	struct umock_softc *s = arg;
	int error;

	mutex_enter(&s->lock);
	s->port[port].record.epoch = epoch;
	s->port[port].record.starts++;
	error = s->port[port].record.start_error;
	if (error == 0 && s->mux)
		error = umoxa_g2_txq_open(&s->txq, port, epoch);
	if (error == 0)
		s->port[port].started = 1;
	mutex_exit(&s->lock);
	return error;
}

static void
mock_stop(void *arg, int port, uint64_t epoch)
{
	struct umock_softc *s = arg;

	(void)epoch;
	mutex_enter(&s->lock);
	KASSERT(!s->port[port].record.active);
	if (s->mux) {
		struct umoxa_g2_result ignored;

		(void)umoxa_g2_txq_close(&s->txq, port);
		/* Test-only abort is synchronous; closed owner receives no done. */
		if (s->txq.active && s->txq.flight.port == (unsigned)port)
			(void)umoxa_g2_txq_done(&s->txq, s->txq.flight.token,
			    0, 0, &ignored);
	}
	s->port[port].started = 0;
	s->port[port].record.pending = 0;
	s->port[port].record.stops++;
	mutex_exit(&s->lock);
}

static int
mock_submit(void *arg, int port, uint64_t epoch, uint64_t cookie,
    const uint8_t *data, size_t length)
{
	struct umock_softc *s = arg;
	struct umock_record *r = &s->port[port].record;
	int gate, error;

	mutex_enter(&s->lock);
	KASSERT(!r->pending);
	if (s->mux) {
		error = umoxa_g2_txq_submit(&s->txq, port, epoch, cookie,
		    data, length);
		if (error != 0) {
			mutex_exit(&s->lock);
			return error;
		}
	}
	r->epoch = epoch;
	r->cookie = cookie;
	r->length = length;
	r->pending = 1;
	r->submits++;
	r->active = 1;
	memcpy(r->bytes, data, MIN(length, sizeof(r->bytes)));
	gate = r->gate;
	mutex_exit(&s->lock);
	/* Test-only suspension simulates preemption during parent dispatch. */
	while (gate) {
		(void)rumpuser_clock_sleep(RUMPUSER_CLOCK_RELWALL, 0, 1000000);
		mutex_enter(&s->lock);
		gate = r->gate;
		mutex_exit(&s->lock);
	}
	mutex_enter(&s->lock);
	r->active = 0;
	mutex_exit(&s->lock);
	return 0;
}

static void
mock_flow(void *arg, int port, uint64_t epoch, int paused)
{
	struct umock_softc *s = arg;

	(void)epoch;
	mutex_enter(&s->lock);
	s->port[port].record.paused = paused;
	mutex_exit(&s->lock);
}

static int
mock_set(void *arg, int port, int which, int on)
{
	struct umock_softc *s = arg;
	int error;

	(void)which; (void)on;
	mutex_enter(&s->lock);
	error = s->port[port].record.set_error;
	mutex_exit(&s->lock);
	return error;
}

static void
mock_detach(void *arg, int port)
{
	struct umock_softc *s = arg;

	mutex_enter(&s->lock);
	KASSERT(!s->port[port].record.active);
	s->port[port].child = NULL;
	s->port[port].record.detaches++;
	mutex_exit(&s->lock);
}

static int
mock_param(void *arg, int port, struct termios *t)
{
	struct umock_softc *s = arg;
	int error, gate;

	(void)t;
	mutex_enter(&s->lock);
	error = s->port[port].record.param_error;
	if (s->port[port].record.param_active != 0)
		s->port[port].record.param_overlap++;
	s->port[port].record.param_active++;
	gate = s->port[port].record.param_gate;
	mutex_exit(&s->lock);
	while (gate) {
		(void)rumpuser_clock_sleep(RUMPUSER_CLOCK_RELWALL, 0, 1000000);
		mutex_enter(&s->lock);
		gate = s->port[port].record.param_gate;
		mutex_exit(&s->lock);
	}
	mutex_enter(&s->lock);
	s->port[port].record.param_active--;
	mutex_exit(&s->lock);
	return error;
}

static const struct ucom_transport_methods transport = {
	.uct_attach = mock_attach, .uct_start = mock_start,
	.uct_stop = mock_stop, .uct_submit = mock_submit,
	.uct_rx_flow = mock_flow, .uct_set = mock_set, .uct_detach = mock_detach
};
static const struct ucom_methods methods = {
	.ucom_param = mock_param, .ucom_transport = &transport
};
static const struct ucom_methods legacy = { 0 };

static int umock_match(device_t p, cfdata_t c, void *a)
{ (void)p; (void)c; (void)a; return 1; }
static void
umock_attach(device_t parent, device_t self, void *aux)
{
	struct umock_softc *s = device_private(self);
	struct ucom_attach_args a;

	(void)parent; (void)aux;
	mock = s;
	mutex_init(&s->lock, MUTEX_DEFAULT, IPL_SOFTUSB);
	for (int i = 0; i < 7; i++) {
		memset(&a, 0, sizeof(a));
		a.ucaa_portno = i;
		a.ucaa_obufsize = i == 4 ? 0 : 1020;
		a.ucaa_ibufsize = a.ucaa_ibufsizepad = 64;
		a.ucaa_methods = i == 6 ? &legacy : &methods;
		a.ucaa_arg = s;
		a.ucaa_info = "software transport fixture";
		s->port[i].device = config_found(self, &a, ucomprint,
		    CFARGS(.submatch = ucomsubmatch));
	}
}
CFATTACH_DECL_NEW(umock, sizeof(struct umock_softc), umock_match,
    umock_attach, NULL, NULL);

void
rump_umock_snapshot(int port, struct umock_record *r)
{

	mutex_enter(&mock->lock);
	*r = mock->port[port].record;
	mutex_exit(&mock->lock);
}
void
rump_umock_errors(int port, int param, int set, int start)
{

	mutex_enter(&mock->lock);
	mock->port[port].record.param_error = param;
	mock->port[port].record.set_error = set;
	mock->port[port].record.start_error = start;
	mutex_exit(&mock->lock);
}
int rump_umock_input(int p, uint64_t e, const void *d, size_t n)
{ return ucom_transport_input(mock->port[p].child, e, d, n); }
void rump_umock_done(int p, uint64_t e, uint64_t c, size_t n, int error)
{
	mutex_enter(&mock->lock);
	if (mock->port[p].record.epoch == e && mock->port[p].record.cookie == c)
		mock->port[p].record.pending = 0;
	mutex_exit(&mock->lock);
	ucom_transport_done(mock->port[p].child, e, c, n, error);
}
void rump_umock_fault(int p, uint64_t e, int error)
{ ucom_transport_fault(mock->port[p].child, e, error); }
void
rump_umock_pause(int p, int block)
{
	extern struct tty *ucomtty(dev_t);
	extern const struct cdevsw ucom_cdevsw;
	struct tty *tp = ucomtty(makedev(cdevsw_lookup_major(&ucom_cdevsw),
	    device_unit(mock->port[p].device)));

	ttylock(tp);
	(*tp->t_hwiflow)(tp, block);
	ttyunlock(tp);
}
void rump_umock_gate(int p, int on)
{
	mutex_enter(&mock->lock);
	mock->port[p].record.gate = on;
	mutex_exit(&mock->lock);
}
int rump_umock_remove(int p) { return config_detach(mock->port[p].device, DETACH_FORCE); }

void
rump_umock_termios(int p, struct termios *t)
{
	extern struct tty *ucomtty(dev_t);
	extern const struct cdevsw ucom_cdevsw;
	struct tty *tp = ucomtty(makedev(cdevsw_lookup_major(&ucom_cdevsw),
	    device_unit(mock->port[p].device)));

	ttylock(tp);
	*t = tp->t_termios;
	ttyunlock(tp);
}

void
rump_umock_param_gate(int p, int on)
{

	mutex_enter(&mock->lock);
	mock->port[p].record.param_gate = on;
	mutex_exit(&mock->lock);
}

/* Test calls hold device lifetime; notifications never hold parent lock. */
struct mux_notice {
	struct ucom_softc *child[4];
	uint64_t epoch[4];
	int error[4];
	uint32_t mask;
};

static void
mux_notice_capture(struct mux_notice *n, uint32_t mask)
{

	KASSERT(mutex_owned(&mock->lock));
	n->mask = mask;
	for (int p = 0; p < 4; p++) {
		n->child[p] = mock->port[p].child;
		n->epoch[p] = mock->txq.ports[p].epoch;
		n->error[p] = mock->txq.ports[p].error;
		if (mask & (1U << p))
			mock->port[p].record.pending = 0;
	}
}

static void
mux_notice_deliver(const struct mux_notice *n)
{

	KASSERT(!mutex_owned(&mock->lock));
	for (int p = 0; p < 4; p++)
		if (n->mask & (1U << p))
			ucom_transport_fault(n->child[p], n->epoch[p], n->error[p]);
}

int
rump_umock_mux_enable(int on)
{
	int error = 0;

	mutex_enter(&mock->lock);
	for (int p = 0; p < 4; p++)
		if (mock->port[p].started || mock->port[p].record.active)
			error = EBUSY;
	if (mock->mux && mock->txq.active)
		error = EBUSY;
	if (error == 0) {
		if (on)
			error = umoxa_g2_txq_init(&mock->txq, 1024, 100);
		mock->mux = on != 0;
	}
	mutex_exit(&mock->lock);
	return error;
}

int
rump_umock_mux_pick(uint64_t now, struct umock_mux_frame *f)
{
	struct umoxa_g2_xfer x;
	struct mux_notice n;
	int error;

	memset(f, 0, sizeof(*f));
	mutex_enter(&mock->lock);
	error = umoxa_g2_txq_pick(&mock->txq, now, &x);
	if (error == 0) {
		f->token = x.token;
		f->epoch = x.epoch;
		f->cookie = x.cookie;
		f->port = x.port;
		f->length = x.len;
		memcpy(f->bytes, x.data, x.len);
	}
	mux_notice_capture(&n, x.fault_mask);
	mutex_exit(&mock->lock);
	mux_notice_deliver(&n);
	return error;
}

void
rump_umock_mux_done(uint64_t token, size_t actual, int error)
{
	struct umoxa_g2_result r;
	struct mux_notice n;
	struct ucom_softc *child;

	mutex_enter(&mock->lock);
	(void)umoxa_g2_txq_done(&mock->txq, token, actual, error, &r);
	child = mock->port[r.port].child;
	if (r.deliver)
		mock->port[r.port].record.pending = 0;
	mux_notice_capture(&n, r.fault_mask);
	mutex_exit(&mock->lock);
	if (r.deliver)
		ucom_transport_done(child, r.epoch, r.cookie, r.actual, r.error);
	mux_notice_deliver(&n);
}

int
rump_umock_mux_events(const uint8_t *data, size_t len)
{
	struct mux_notice n;
	uint32_t mask;
	int error;

	mutex_enter(&mock->lock);
	error = umoxa_g2_txq_events(&mock->txq, data, len, &mask);
	mux_notice_capture(&n, mask);
	mutex_exit(&mock->lock);
	mux_notice_deliver(&n);
	return error;
}

int
rump_umock_mux_tick(uint64_t now)
{
	struct mux_notice n;
	uint32_t mask;
	int error;

	mutex_enter(&mock->lock);
	error = umoxa_g2_txq_tick(&mock->txq, now, &mask);
	mux_notice_capture(&n, mask);
	mutex_exit(&mock->lock);
	mux_notice_deliver(&n);
	return error;
}
