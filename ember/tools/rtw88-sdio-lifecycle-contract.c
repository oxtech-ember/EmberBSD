/* SPDX-License-Identifier: BSD-2-Clause */
/* Host services model: the attachment, transport and firmware parser are real. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#define __predict_false(v) __builtin_expect(!!(v), 0)
#define __BIT(n) (1U << (n))
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw8723d_boot.h>
#include <dev/sdmmc/rtw88_sdio_io.h>
#include <dev/sdmmc/sdmmc_ioreg.h>
#include <dev/sdmmc/sdmmcreg.h>
#include "flags.inc"

typedef unsigned u_int;
typedef pthread_mutex_t kmutex_t;
typedef struct device *device_t;
typedef void *cfdata_t;
typedef device_t prop_dictionary_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define FDT 1
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
#define KASSERT assert
#define __unused __attribute__((unused))
#define CFATTACH_DECL_NEW(name, size, match, attach, detach, activate)
#define aprint_normal(...) ((void)0)
#define aprint_normal_dev(...) ((void)0)
#define aprint_error_dev(...) ((void)0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define SET(a,b) ((a)|=(b))
#define ISSET(a,b) ((a)&(b))

struct model;
struct device { void *private; device_t parent; struct model *m; };
struct sdmmc_task { void (*func)(void *); void *arg; bool queued; };
struct sdmmc_softc {
	device_t sc_dev;
	kmutex_t sc_mtx;
	void *sc_sct, *sc_sch;
	struct sdmmc_function *sc_fn0;
};
struct sdmmc_function {
	struct sdmmc_softc *sc;
	int number;
	u_int blklen;
	struct { uint16_t manufacturer, product; } cis;
};
struct sdmmc_attach_args { struct sdmmc_function *sf; };
struct sdmmc_command {
	uint16_t c_opcode;
	uint32_t c_arg, c_resp[4];
	void *c_data;
	int c_datalen, c_blklen, c_flags;
};
struct firmware_handle { struct model *m; };
typedef struct firmware_handle *firmware_handle_t;
struct model {
	struct device host, busdev, child;
	struct sdmmc_softc hostsc;
	struct sdmmc_function fn0, fn1;
	uint8_t cccr[512], mac[65536], local[4096], blob[32 + 1028];
	unsigned io, fail_io, windows, power_offs, host_limit, card_limit;
	unsigned open_count, close_count, alloc_count, free_count;
	int open_error, read_error, close_error, chip_error, boot_error;
	int registry_error, irq_error, select_error;
	bool enabled, removable, pwrseq, diagnostic, root_mounted;
	bool malformed, truncated, bad_fbr, irq_owned, freed;
	bool block_boot, boot_entered, release_boot, cancel_on_window;
	void (*root_hook)(device_t);
	struct sdmmc_task *task, *running;
	int (*handler)(void *);
	void *handler_arg;
	pthread_mutex_t qlock;
	pthread_cond_t qcv;
};
static struct model *active;
static void mutex_init(kmutex_t *m, int t __unused, int i __unused)
{ assert(pthread_mutex_init(m, NULL) == 0); }
static void mutex_enter(kmutex_t *m) { assert(pthread_mutex_lock(m) == 0); }
static void mutex_exit(kmutex_t *m) { assert(pthread_mutex_unlock(m) == 0); }
static void mutex_destroy(kmutex_t *m) { assert(pthread_mutex_destroy(m) == 0); }
static void *device_private(device_t d) { return d->private; }
static device_t device_parent(device_t d) { return d->parent; }
static const char *device_xname(device_t d __unused) { return "rtwsdio0"; }
static prop_dictionary_t device_properties(device_t d) { return d; }
static bool prop_dictionary_get_string(prop_dictionary_t d,
    const char *k __unused, const char **v)
{ if (d != &active->host) return false; *v = "/mmc"; return true; }
static int OF_finddevice(const char *p) { return strcmp(p, "/") == 0 ? 1 : 2; }
static int OF_getproplen(int p, const char *k)
{
	if (strcmp(k, "non-removable") == 0) return active->removable ? -1 : 0;
	if (strcmp(k, "mmc-pwrseq") == 0) return active->pwrseq ? 4 : -1;
	if (strstr(k, "ember,") == k) return active->diagnostic ? 0 : -1;
	(void)p;
	return -1;
}
static bool fdtbus_status_okay(int p __unused) { return active->enabled; }
static void *fdtbus_mmc_pwrseq_get(int p __unused)
{ return active->pwrseq ? active : NULL; }
static void sdmmc_init_task(struct sdmmc_task *t, void (*f)(void *), void *a)
{ memset(t, 0, sizeof(*t)); t->func = f; t->arg = a; }
static void sdmmc_add_task(struct sdmmc_softc *h __unused, struct sdmmc_task *t)
{
	struct model *m = active;
	mutex_enter(&m->qlock);
	assert(!m->freed);
	t->queued = true; m->task = t;
	mutex_exit(&m->qlock);
}
static bool sdmmc_del_task(struct sdmmc_softc *h __unused,
    struct sdmmc_task *t, kmutex_t *lock __unused)
{
	struct model *m = active;
	mutex_enter(&m->qlock);
	if (t->queued) {
		t->queued = false; m->task = NULL;
		mutex_exit(&m->qlock); return true;
	}
	while (m->running == t) pthread_cond_wait(&m->qcv, &m->qlock);
	mutex_exit(&m->qlock); return false;
}
static void config_mountroot(device_t d, void (*f)(device_t))
{ if (active->root_mounted) f(d); else active->root_hook = f; }
static int config_mountroot_cancel(device_t d __unused)
{ active->root_hook = NULL; return 0; }
static int sdmmc_select_card(struct sdmmc_softc *h __unused,
    struct sdmmc_function *f __unused) { return active->select_error; }
static int sdmmc_chip_host_maxblklen(void *t __unused, void *h __unused)
{ return (int)active->host_limit; }
static int sdmmc_io_maxblklen_checked(struct sdmmc_function *f __unused,
    u_int *v) { *v = active->card_limit; return 0; }
static int sdmmc_mmc_command(struct sdmmc_softc *h __unused,
    struct sdmmc_command *c)
{
	struct model *m = active;
	uint32_t reg = (c->c_arg >> 9) & 0x1ffff;
	unsigned fn = (c->c_arg >> 28) & 7;
	bool write = (c->c_arg & 0x80000000U) != 0;
	uint8_t *p;
	assert(!m->freed); m->io++;
	if (fn == 0) {
		assert(c->c_opcode == 52 && reg < sizeof(m->cccr));
		p = m->cccr + reg;
	} else if ((reg & 0x10000) != 0) {
		assert(reg - 0x10000 < sizeof(m->mac)); p = m->mac + (reg - 0x10000);
	} else { assert(reg < sizeof(m->local)); p = m->local + reg; }
	if (m->fail_io == m->io) {
		/* A card may apply a write before its response is lost. */
		if (write && c->c_opcode == 52) {
			*p = c->c_arg & 0xff;
			if (fn == 0 && reg == 2) m->cccr[3] = m->cccr[2];
		}
		return EIO;
	}
	if (c->c_opcode == 52) {
		assert(c->c_flags == (SCF_CMD_AC | SCF_RSP_R5));
		if (write && !(m->bad_fbr && fn == 0 && reg == 0x111))
			*p = c->c_arg & 0xff;
		if (fn == 0 && reg == 2) m->cccr[3] = m->cccr[2];
		c->c_resp[0] = *p;
	} else {
		unsigned count = c->c_arg & 511;
		assert(c->c_opcode == 53 && fn == 1);
		assert((c->c_flags & SCF_NO_STOP) != 0);
		assert(c->c_datalen > 0 && c->c_datalen <= 512);
		assert(c->c_blklen > 0 && c->c_blklen <= 512);
		if ((c->c_arg & (1U << 27)) != 0)
			assert(count != 0 && c->c_datalen == (int)(count * 512));
		else assert(c->c_datalen == (count == 0 ? 512 : (int)count));
		assert(((c->c_flags & SCF_CMD_READ) == 0) == write);
		if (write) memcpy(p, c->c_data, (size_t)c->c_datalen);
		else memcpy(c->c_data, p, (size_t)c->c_datalen);
	}
	return 0;
}
static int sdmmc_intr_establish_checked(device_t d __unused,
    int (*f)(void *), void *a, const char *n __unused, void **cookie)
{
	if (active->registry_error) return active->registry_error;
	active->handler = f; active->handler_arg = a; *cookie = active;
	return 0;
}
static int sdmmc_intr_disestablish_checked(void *p)
{ assert(p == active); active->handler = NULL; return 0; }
static int sdmmc_intr_enable_checked(struct sdmmc_function *f __unused)
{ active->cccr[4] |= 2; return active->irq_error; }
static int sdmmc_intr_disable_checked(struct sdmmc_function *f __unused)
{ active->cccr[4] &= ~2; return 0; }
static void sdmmc_pause(unsigned us __unused, void *p __unused) {}
static void delay(unsigned us __unused) {}
static int firmware_open(const char *d, const char *f, firmware_handle_t *h)
{
	assert(strcmp(d, "rtw88") == 0 && strcmp(f, "rtw8723d_fw.bin") == 0);
	if (active->open_error) return active->open_error;
	*h = malloc(sizeof(**h)); assert(*h != NULL); (*h)->m = active;
	active->open_count++; return 0;
}
static int firmware_read_alloc(firmware_handle_t h __unused, size_t max,
    void **p, size_t *n)
{
	if (active->read_error) return active->read_error;
	*n = active->truncated ? 31 : sizeof(active->blob);
	assert(max >= *n); *p = malloc(*n); assert(*p != NULL);
	memcpy(*p, active->blob, *n);
	if (active->malformed) ((uint8_t *)*p)[0] = 0;
	active->alloc_count++; return 0;
}
static int firmware_close(firmware_handle_t h)
{ active->close_count++; free(h); return active->close_error; }
static void firmware_free(void *p, size_t n __unused)
{ active->free_count++; free(p); }
static uint16_t le16dec(const void *v)
{ const uint8_t *p = v; return p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32dec(const void *v)
{ const uint8_t *p = v; return le16dec(p) | (uint32_t)le16dec(p + 2) << 16; }
static void le16enc(void *v, uint16_t n)
{ uint8_t *p = v; p[0] = n; p[1] = n >> 8; }
static void le32enc(void *v, uint32_t n)
{ uint8_t *p = v; le16enc(p, n); le16enc(p + 2, n >> 16); }
int rtw8723d_identify(struct rtw88_io *io __unused,
    struct rtw8723d_identity *id __unused) { return active->chip_error; }
int rtw8723d_power_on(struct rtw88_io *io __unused,
    const struct rtw8723d_boot_ops *ops __unused)
{
	/* Chip power transitions preserve their caller's initial HIMR value. */
	assert(le32dec(active->local + 0x14) == 0);
	return active->chip_error;
}
int rtw8723d_power_off(struct rtw88_io *io __unused,
    const struct rtw8723d_boot_ops *ops __unused)
{ active->power_offs++; return 0; }
#include "parser.inc"
int rtw8723d_fw_boot(struct rtw88_io *io __unused,
    const struct rtw8723d_fw *fw, const struct rtw8723d_boot_ops *ops)
{
	struct model *m = active;
	int error;
	mutex_enter(&m->qlock); m->boot_entered = true;
	pthread_cond_broadcast(&m->qcv);
	while (m->block_boot && !m->release_boot)
		pthread_cond_wait(&m->qcv, &m->qlock);
	mutex_exit(&m->qlock);
	if (ops->cancelled(ops->cookie)) return ECANCELED;
	assert(m->alloc_count == m->free_count + 1);
	error = ops->write_window(ops->cookie, 0x1000, fw->payload, fw->payload_size);
	m->windows++;
	if (error) return error;
	return m->boot_error;
}
#include "native.inc"

static void init(struct model *m, struct rtwsdio_softc *sc)
{
	memset(m, 0, sizeof(*m)); memset(sc, 0, sizeof(*sc)); active = m;
	m->host.m = m; m->busdev.m = m; m->child.m = m;
	m->child.private = sc; m->child.parent = &m->busdev;
	m->busdev.parent = &m->host; m->hostsc.sc_dev = &m->busdev;
	m->fn0.sc = m->fn1.sc = &m->hostsc; m->fn1.number = 1;
	m->hostsc.sc_fn0 = &m->fn0;
	m->fn1.blklen = 64; m->fn1.cis.manufacturer = 0x024c;
	m->fn1.cis.product = 0xd723;
	m->enabled = m->pwrseq = m->root_mounted = true;
	m->host_limit = m->card_limit = 512;
	m->cccr[8] = 2;
	m->cccr[0x110] = 64; m->cccr[2] = m->cccr[3] = 8;
	m->local[0x14] = 0xff;
	m->blob[0] = 0xd1; m->blob[1] = 0x23;
	m->blob[12] = 4; m->blob[13] = 4;
	mutex_init(&m->hostsc.sc_mtx, 0, 0); mutex_init(&m->qlock, 0, 0);
	assert(pthread_cond_init(&m->qcv, NULL) == 0);
}
static void attach(struct model *m)
{
	struct sdmmc_attach_args aa = { .sf = &m->fn1 };
	assert(rtwsdio_match(&m->busdev, NULL, &aa) != 0);
	rtwsdio_attach(&m->busdev, &m->child, &aa);
}
static void *run_task(void *arg)
{
	struct model *m = arg;
	struct sdmmc_task *t;
	mutex_enter(&m->qlock); t = m->task; assert(t != NULL && t->queued);
	t->queued = false; m->task = NULL; m->running = t;
	mutex_exit(&m->qlock); t->func(t->arg);
	mutex_enter(&m->qlock); m->running = NULL;
	pthread_cond_broadcast(&m->qcv); mutex_exit(&m->qlock);
	return NULL;
}
static void finish(struct model *m)
{
	assert(rtwsdio_detach(&m->child, 0) == 0);
	assert(m->handler == NULL && m->task == NULL && m->root_hook == NULL);
	assert(m->open_count == m->close_count && m->alloc_count == m->free_count);
	assert(m->cccr[2] == 8 && (m->cccr[4] & 2) == 0);
	m->freed = true;
	mutex_destroy(&m->hostsc.sc_mtx); mutex_destroy(&m->qlock);
	assert(pthread_cond_destroy(&m->qcv) == 0);
	active = NULL;
}
static void admission(void)
{
	struct model m; struct rtwsdio_softc sc;
	struct sdmmc_attach_args aa;
	init(&m, &sc); aa.sf = &m.fn1;
	m.enabled = false; assert(rtwsdio_match(&m.busdev, NULL, &aa) == 0);
	m.enabled = true; m.pwrseq = false;
	assert(rtwsdio_match(&m.busdev, NULL, &aa) == 0);
	m.pwrseq = true; m.diagnostic = true;
	assert(rtwsdio_match(&m.busdev, NULL, &aa) == 0);
	m.diagnostic = false; m.fn1.number = -1;
	assert(rtwsdio_match(&m.busdev, NULL, &aa) == 0);
	m.fn1.number = 1; m.fn1.cis.product = 0;
	assert(rtwsdio_match(&m.busdev, NULL, &aa) == 0);
	assert(m.io == 0);
	mutex_destroy(&m.hostsc.sc_mtx); mutex_destroy(&m.qlock);
	assert(pthread_cond_destroy(&m.qcv) == 0);
	active = NULL;
}
static void success_and_irq(void)
{
	struct model m; struct rtwsdio_softc sc;
	unsigned before;
	init(&m, &sc); attach(&m); run_task(&m);
	assert(sc.sc_state == RTWSDIO_FW_READY && sc.sc_error == 0);
	assert(m.windows == 1 && m.fn1.blklen == 512);
	assert(le32dec(m.local + 0x14) == 0);
	before = m.io; assert(m.handler != NULL);
	m.handler(m.handler_arg); assert(m.io == before && m.task != NULL);
	run_task(&m); assert(le32dec(m.local + 0x14) == 0);
	/* A queued IRQ cannot outlive detach. */
	m.handler(m.handler_arg); finish(&m);
	assert(m.fn1.blklen == 64 && m.cccr[0x110] == 64 && m.cccr[0x111] == 0);
}
static void scalars_and_repeated_stop(void)
{
	struct model m; struct rtwsdio_softc sc;
	uint32_t value = 0x11223344;
	uint16_t half = 0;
	unsigned before;
	init(&m, &sc); attach(&m); run_task(&m);
	assert(rtwsdio_write_2(&sc, 0x23, 0x1234) == 0);
	assert(rtwsdio_read_2(&sc, 0x23, &half) == 0 && half == 0x1234);
	before = m.io;
	assert(rtwsdio_read_4(&sc, 0xdead0000, &value) == EINVAL);
	assert(value == 0x11223344 && m.io == before);
	m.fail_io = m.io + 1;
	assert(rtwsdio_read_4(&sc, 0x20, &value) == EIO && value == 0x11223344);
	m.fail_io = 0;
	assert(rtwsdio_stop(&sc) == 0); before = m.io;
	assert(rtwsdio_stop(&sc) == 0 && m.io == before);
	finish(&m);
}
static void failures(void)
{
	struct model m; struct rtwsdio_softc sc;
	unsigned i, total;
	init(&m, &sc); attach(&m); run_task(&m); total = m.io; finish(&m);
	/* Every native command can fail, including uncertain FBR/IRQ writes. */
	for (i = 1; i <= total; i++) {
		init(&m, &sc); m.fail_io = i; attach(&m); run_task(&m);
		assert(sc.sc_error != 0); finish(&m);
	}
	for (i = 0; i < 12; i++) {
		init(&m, &sc);
		switch (i) {
		case 0: m.open_error = ENOENT; break;
		case 1: m.read_error = EIO; break;
		case 2: m.malformed = true; break;
		case 3: m.truncated = true; break;
		case 4: m.host_limit = 256; break;
		case 5: m.card_limit = 256; break;
		case 6: m.bad_fbr = true; break;
		case 7: m.chip_error = ENODEV; break;
		case 8: m.boot_error = ETIMEDOUT; break;
		case 9: m.registry_error = EIO; break;
		case 10: m.irq_error = EIO; break;
		case 11: m.cccr[8] = 0; break;
		}
		attach(&m); run_task(&m); assert(sc.sc_error != 0); finish(&m);
	}
}
static void deferred_detach(void)
{
	struct model m; struct rtwsdio_softc sc;
	init(&m, &sc); m.root_mounted = false; attach(&m);
	assert(m.root_hook != NULL && m.task == NULL && m.io == 0); finish(&m);
	init(&m, &sc); attach(&m); assert(m.task != NULL); finish(&m);
}
static void *detach_thread(void *arg)
{ struct model *m = arg; assert(rtwsdio_detach(&m->child, 0) == 0); return NULL; }
static void running_detach(void)
{
	struct model m; struct rtwsdio_softc sc;
	pthread_t worker, detacher;
	bool stopped = false;
	struct timespec limit, now;
	init(&m, &sc); m.block_boot = true; attach(&m);
	assert(pthread_create(&worker, NULL, run_task, &m) == 0);
	mutex_enter(&m.qlock);
	while (!m.boot_entered) pthread_cond_wait(&m.qcv, &m.qlock);
	mutex_exit(&m.qlock);
	assert(pthread_create(&detacher, NULL, detach_thread, &m) == 0);
	assert(clock_gettime(CLOCK_MONOTONIC, &limit) == 0); limit.tv_sec += 5;
	while (!stopped) {
		mutex_enter(&sc.sc_lock); stopped = sc.sc_stopping; mutex_exit(&sc.sc_lock);
		assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
		assert(now.tv_sec < limit.tv_sec); sched_yield();
	}
	mutex_enter(&m.qlock); m.release_boot = true;
	pthread_cond_broadcast(&m.qcv); mutex_exit(&m.qlock);
	assert(pthread_join(worker, NULL) == 0 && pthread_join(detacher, NULL) == 0);
	assert(m.windows == 0 && sc.sc_state == RTWSDIO_DETACHED);
	assert(m.handler == NULL && m.task == NULL);
	assert(m.alloc_count == m.free_count && m.open_count == m.close_count);
	mutex_destroy(&m.hostsc.sc_mtx); mutex_destroy(&m.qlock);
	assert(pthread_cond_destroy(&m.qcv) == 0);
	active = NULL;
}
int main(void)
{
	admission(); success_and_irq(); scalars_and_repeated_stop(); failures();
	deferred_detach(); running_detach();
	puts("native SDIO lifecycle: 6 groups PASS");
	return 0;
}
