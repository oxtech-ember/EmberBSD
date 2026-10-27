/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute production CMD52/RMW/registry logic; simulate the card boundary. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#ifndef __BIT
#define __BIT(n) (UINT64_C(1) << (n))
#endif
#ifndef __predict_false
#define __predict_false(value) __builtin_expect(!!(value), 0)
#endif
#include <dev/sdmmc/sdmmc_ioreg.h>
#include <dev/sdmmc/sdmmcreg.h>
#ifdef HAVE_SDMMC_IO_STATUS
#include <dev/sdmmc/sdmmc_io_status.h>
#endif
#include "command-flags.inc"

typedef unsigned char u_char;
typedef pthread_mutex_t kmutex_t;
struct sdmmc_softc;
struct test_device { struct sdmmc_softc *sc; };
typedef struct test_device *device_t;
struct sdmmc_function {
	struct sdmmc_softc *sc;
#include "function-number.inc"
};
struct sdmmc_intr_handler;
TAILQ_HEAD(test_intr_head, sdmmc_intr_handler);
struct sdmmcchip_functions { void (*card_enable_intr)(void *, int); };
struct model {
	uint8_t ien;
	unsigned commands, reads, writes, host_calls, live, allocations, fail_alloc;
	unsigned fail_command, fail_command2;
	int command_error, command_error2, select_error;
	uint32_t status, status2;
	bool host_enabled, ignore_write, require_lock;
};
struct sdmmc_softc {
	device_t sc_dev;
	struct sdmmc_function *sc_fn0;
	struct sdmmcchip_functions *sc_sct;
	void *sc_sch;
	kmutex_t sc_mtx;
	struct test_intr_head sc_intrq;
	struct model *model;
};
struct sdmmc_command {
	uint32_t c_opcode, c_arg, c_flags, c_resp[4];
};

static _Thread_local kmutex_t *held_mutex;
#define KASSERT assert
#define mutex_owned(lock) (held_mutex == (lock))
#define SDMMC_LOCK(sc) ((void)(sc))
#define SDMMC_UNLOCK(sc) ((void)(sc))
#define M_DEVBUF 0
#define M_WAITOK 1
#define M_ZERO 2
#define device_private(dev) ((dev)->sc)
#define device_printf(dev, ...) ((void)(dev))
#define sdmmc_chip_card_enable_intr(sct, sch, value) \
    ((sct)->card_enable_intr((sch), (value)))

void
mutex_enter(kmutex_t *lock)
{
	int error = pthread_mutex_lock(lock);

	assert(error == 0 && held_mutex == NULL);
	held_mutex = lock;
}

void
mutex_exit(kmutex_t *lock)
{
	int error;

	assert(held_mutex == lock);
	held_mutex = NULL;
	error = pthread_mutex_unlock(lock);
	assert(error == 0);
}

static int
sdmmc_select_card(struct sdmmc_softc *sc, struct sdmmc_function *sf)
{

	(void)sf;
	return sc->model->select_error;
}

static int
sdmmc_mmc_command(struct sdmmc_softc *sc, struct sdmmc_command *command)
{
	struct model *m = sc->model;
	unsigned address = (command->c_arg >> 9) & 0x1ffff;
	bool write = (command->c_arg & 0x80000000U) != 0;
	uint32_t status = 0;
	int error = 0;

	m->commands++;
	assert(command->c_opcode == 52);
	assert(command->c_flags == (SCF_CMD_AC | SCF_RSP_R5) ||
	    command->c_flags == (SCF_CMD_AC | SCF_RSP_R5 | SCF_TOUT_OK));
	if (address == 4 && m->require_lock)
		assert(mutex_owned(&sc->sc_mtx));
	if (write) {
		m->writes++;
		if (address == 4 && !m->ignore_write)
			m->ien = command->c_arg & 0xff;
	} else {
		m->reads++;
	}
	if (m->commands == m->fail_command) {
		status = m->status;
		error = m->command_error;
	} else if (m->commands == m->fail_command2) {
		status = m->status2;
		error = m->command_error2;
	}
	command->c_resp[0] = status | (address == 4 ? m->ien : 0x42);
	return error;
}

static void
host_enable(void *cookie, int enabled)
{
	struct model *m = cookie;

	m->host_calls++;
	m->host_enabled = enabled != 0;
}

#ifndef TEST_DIRECT_ONLY
static struct model *allocation_model;

static void *
test_malloc(size_t size, int flags)
{
	struct model *m = allocation_model;
	void *p;

	(void)flags;
	assert(held_mutex == NULL);
	m->allocations++;
	if (m->allocations == m->fail_alloc)
		return NULL;
	p = calloc(1, size);
	assert(p != NULL);
	m->live++;
	return p;
}

static void
test_free(void *p)
{

	assert(held_mutex == NULL && allocation_model->live != 0);
	allocation_model->live--;
	free(p);
}

static size_t
test_strlcpy(char *destination, const char *source, size_t capacity)
{
	size_t length = strlen(source);
	size_t copied;

	if (capacity != 0) {
		copied = length < capacity - 1 ? length : capacity - 1;
		memcpy(destination, source, copied);
		destination[copied] = '\0';
	}
	return length;
}
#define malloc(size, pool, flags) test_malloc((size), (flags))
#define free(pointer, pool) test_free(pointer)
#define strlcpy test_strlcpy
#endif

#include "production.inc"
#undef malloc
#undef free

struct fixture {
	struct model model;
	struct sdmmc_softc sc;
	struct test_device dev;
	struct sdmmc_function fn0, fn1, fn2;
	struct sdmmcchip_functions chip;
};

static void
setup(struct fixture *f)
{

	memset(f, 0, sizeof(*f));
	assert(pthread_mutex_init(&f->sc.sc_mtx, NULL) == 0);
	f->dev.sc = &f->sc;
	f->fn0.sc = f->fn1.sc = f->fn2.sc = &f->sc;
	f->fn1.number = 1; f->fn2.number = 2;
	f->sc.sc_dev = &f->dev;
	f->sc.sc_fn0 = &f->fn0;
	f->chip.card_enable_intr = host_enable;
	f->sc.sc_sct = &f->chip;
	f->sc.sc_sch = &f->model;
	f->sc.model = &f->model;
	TAILQ_INIT(&f->sc.sc_intrq);
#ifndef TEST_DIRECT_ONLY
	allocation_model = &f->model;
#endif
}

static void
finish(struct fixture *f)
{

	assert(TAILQ_EMPTY(&f->sc.sc_intrq) && f->model.live == 0);
	assert(pthread_mutex_destroy(&f->sc.sc_mtx) == 0);
}

static void
direct_contract(void)
{
	static const struct { uint32_t flags; int error; } cases[] = {
		{ 0x8000, EILSEQ }, { 0x4000, EIO }, { 0x0800, EIO },
		{ 0x0200, EIO }, { 0x0100, EIO }, { 0xcb00, EILSEQ },
		{ 0x3000, 0 }
	};
	struct fixture f;
	unsigned i;
	u_char value;
	int error;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		setup(&f);
		value = 0xa5;
		f.model.fail_command = 1; f.model.status = cases[i].flags;
		error = sdmmc_io_rw_direct(&f.sc, &f.fn1, 0x40, &value, SD_ARG_CMD52_READ, false);
		assert(error == cases[i].error);
		assert(value == (error == 0 ? 0x42 : 0xa5));
		finish(&f);
	}
	setup(&f);
	value = 0xa5;
	f.model.fail_command = 1; f.model.status = 0x8000;
	f.model.command_error = ETIMEDOUT;
	assert(sdmmc_io_rw_direct(&f.sc, &f.fn1, 0x40, &value,
	    SD_ARG_CMD52_READ, false) == ETIMEDOUT);
	assert(value == 0xa5);
	f.model.select_error = EAGAIN;
	assert(sdmmc_io_rw_direct(&f.sc, &f.fn1, 0x40, &value,
	    SD_ARG_CMD52_READ, false) == EAGAIN);
	assert(f.model.commands == 1);
	finish(&f);
	puts("CMD52 response errors preserve data and first errno: PASS");
}

#ifndef TEST_DIRECT_ONLY
static int
handler(void *arg)
{

	(void)arg;
	return 1;
}

static void
function_contract(void)
{
	struct fixture f;

	setup(&f);
	f.fn1.number = -1; /* Real memory-card sentinel, from the production type. */
	assert(sdmmc_intr_enable_checked(&f.fn1) == EINVAL);
	assert(sdmmc_intr_disable_checked(&f.fn1) == EINVAL);
	sdmmc_intr_enable(&f.fn1);
	assert(f.model.commands == 0);
	f.fn1.number = 8;
	assert(sdmmc_intr_enable_checked(&f.fn1) == EINVAL);
	assert(f.model.commands == 0);
	finish(&f);
	setup(&f);
	f.model.ien = 0xa1; f.model.require_lock = true;
	assert(sdmmc_intr_enable_checked(&f.fn1) == 0 && f.model.ien == 0xa3);
	f.model.ien = 0xe3;
	assert(sdmmc_intr_disable_checked(&f.fn1) == 0 && f.model.ien == 0xe1);
	/* Existing callers already holding sc_mtx must not recurse. */
	mutex_enter(&f.sc.sc_mtx);
	assert(sdmmc_intr_enable_checked(&f.fn2) == 0 && f.model.ien == 0xe5);
	sdmmc_intr_disable(&f.fn2);
	mutex_exit(&f.sc.sc_mtx);
	assert(f.model.ien == 0xe1);
	finish(&f);
	setup(&f);
	f.model.ien = 0x80; f.model.require_lock = true;
	f.model.fail_command = 1; f.model.status = 0x8000;
	assert(sdmmc_intr_enable_checked(&f.fn1) == EILSEQ);
	assert(f.model.writes == 0 && f.model.ien == 0x80);
	finish(&f);
	setup(&f);
	f.model.ien = 0x80; f.model.require_lock = true;
	f.model.fail_command = 2; f.model.status = 0x8000;
	assert(sdmmc_intr_enable_checked(&f.fn1) == EILSEQ);
	assert(f.model.ien == 0x82); /* A failed write can have reached the card. */
	f.model.ien |= 0x40;
	assert(sdmmc_intr_disable_checked(&f.fn1) == 0 && f.model.ien == 0xc0);
	finish(&f);
	setup(&f);
	f.model.ien = 0x80; f.model.ignore_write = true;
	assert(sdmmc_intr_enable_checked(&f.fn1) == EIO);
	finish(&f);
	puts("checked IEN updates preserve foreign bits and caller locks: PASS");
}

static void
registry_contract(void)
{
	struct fixture f;
	void *first = NULL, *second = NULL;
	unsigned which;

	setup(&f);
	f.model.ien = 0x80; f.model.require_lock = true;
	assert(sdmmc_intr_establish_checked(&f.dev, handler, NULL, "first", &first) == 0);
	assert(f.model.ien == 0x81 && f.model.host_enabled);
	assert(sdmmc_intr_establish_checked(&f.dev, handler, NULL, "second", &second) == 0);
	assert(sdmmc_intr_disestablish_checked(first) == 0);
	assert(f.model.host_enabled && f.model.ien == 0x81);
	f.model.ien |= 0x40;
	assert(sdmmc_intr_disestablish_checked(second) == 0);
	assert(!f.model.host_enabled && f.model.ien == 0xc0);
	finish(&f);
	setup(&f);
	f.model.ien = 0x81;
	assert(sdmmc_intr_establish_checked(&f.dev, handler, NULL, "foreign", &first) == 0);
	assert(sdmmc_intr_disestablish_checked(first) == 0 && f.model.ien == 0x81);
	finish(&f);
	for (which = 1; which <= 3; which++) {
		setup(&f);
		first = &f;
		f.model.ien = 0x80; f.model.fail_command = which; f.model.status = 0x8000;
		assert(sdmmc_intr_establish_checked(&f.dev, handler, NULL, "fail", &first) == EILSEQ);
		assert(first == &f && !f.model.host_enabled && f.model.ien == 0x80);
		finish(&f);
	}
	setup(&f);
	first = &f;
	f.model.fail_command = 2; f.model.status = 0x8000;
	f.model.fail_command2 = 3; f.model.command_error2 = ETIMEDOUT;
	assert(sdmmc_intr_establish_checked(&f.dev, handler, NULL, "double-fail", &first) == EILSEQ);
	assert(first == &f && !f.model.host_enabled);
	finish(&f);
	for (which = 1; which <= 2; which++) {
		setup(&f);
		first = &f; f.model.fail_alloc = which;
		assert(sdmmc_intr_establish_checked(&f.dev, handler, NULL, "alloc", &first) == ENOMEM);
		assert(first == &f && f.model.commands == 0);
		finish(&f);
	}
	setup(&f);
	assert(sdmmc_intr_establish_checked(&f.dev, handler, NULL, "release", &first) == 0);
	f.model.fail_command = f.model.commands + 1; f.model.status = 0x8000;
	assert(sdmmc_intr_disestablish_checked(first) == EILSEQ);
	assert(!f.model.host_enabled); /* Cookie is consumed even when card I/O fails. */
	assert(sdmmc_intr_disestablish_checked(NULL) == 0);
	finish(&f);
	puts("checked registry ownership, transfer, failures and teardown: PASS");
}
#endif

int
main(void)
{

	direct_contract();
#ifndef TEST_DIRECT_ONLY
	function_contract();
	registry_contract();
#endif
	return 0;
}
