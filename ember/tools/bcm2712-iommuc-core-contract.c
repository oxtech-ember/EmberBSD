/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual pure core. Hardware values below are independent literal oracles. */
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bcm2712_iommuc_corevar.h"

static unsigned long checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #c); \
	return_error(); } } while (0)
static void return_error(void) { abort(); }

struct fake {
	struct bcm2712_iommuc_core *sc;
	unsigned int reads, writes, barriers, calls, fail_at, complete_at, bit;
	uint32_t initial, busy, complete;
	int error;
	bool reenter;
};
static const enum bcm2712_iommuc_mmu members[] = {
	BCM2712_IOMMUC_MMU2, BCM2712_IOMMUC_MMU4, BCM2712_IOMMUC_MMU5
};

static void
observe(struct fake *f)
{
	struct bcm2712_iommuc_status s;
	unsigned int before = f->calls;

	CHECK(bcm2712_iommuc_core_status(f->sc, &s) == 0);
	CHECK(f->calls == before && (s.reserved & f->bit) != 0);
	CHECK((s.retained & f->bit) != 0 && s.fault == 0);
	if (f->reenter)
		CHECK(bcm2712_iommuc_core_reserve(f->sc, BCM2712_IOMMUC_MMU5) == EBUSY);
}
static int
read_control(void *arg, uint32_t *out)
{
	struct fake *f = arg;
	unsigned int poll;

	f->calls++;
	f->reads++;
	observe(f);
	CHECK(!f->sc->status.raw_valid && f->reads <= 1025);
	if (f->reads == 1) {
		CHECK(f->writes == 0 && f->barriers == 0);
		CHECK(f->sc->status.stage == BCM2712_IOMMUC_PREFLIGHT);
	} else {
		CHECK(f->writes == 1 && f->barriers == 1);
		CHECK(f->sc->status.stage == BCM2712_IOMMUC_POLL);
		CHECK(f->sc->status.poll == f->reads - 1);
	}
	*out = UINT32_MAX; /* Failed callback output must not become a sample. */
	if (f->calls == f->fail_at)
		return f->error;
	poll = f->reads - 1;
	*out = f->reads == 1 ? f->initial :
	    (f->complete_at != 0 && poll >= f->complete_at ? f->complete : f->busy);
	return 0;
}
static int
write_control(void *arg, uint32_t value)
{
	struct fake *f = arg;

	f->calls++;
	f->writes++;
	observe(f);
	CHECK(value == 3 && f->writes == 1 && f->reads == 1 && f->barriers == 0);
	CHECK(f->sc->status.stage == BCM2712_IOMMUC_WRITE);
	CHECK(f->sc->status.mutation_attempted && f->sc->status.writes_attempted == 1);
	CHECK(f->sc->status.writes_completed == 0 && f->sc->status.raw_valid);
	return f->calls == f->fail_at ? f->error : 0;
}
static void
barrier(void *arg)
{
	struct fake *f = arg;

	observe(f);
	CHECK(f->writes == 1 && f->reads == 1 && f->barriers == 0);
	CHECK(f->sc->status.writes_completed == 1);
	f->barriers++;
}
static struct bcm2712_iommuc_io
io(struct fake *f)
{
	return (struct bcm2712_iommuc_io){ read_control, write_control, barrier, f };
}
static void
reset_io(struct fake *f, struct bcm2712_iommuc_core *sc, unsigned int bit)
{

	*f = (struct fake){ .sc = sc, .complete_at = 1, .bit = bit,
	    .busy = 5, .complete = 1, .error = EIO };
}
static void
setup(struct bcm2712_iommuc_core *sc, struct fake *f, unsigned int member)
{
	struct bcm2712_iommuc_io callbacks;

	memset(sc, 0, sizeof(*sc));
	reset_io(f, sc, 1U << member);
	callbacks = io(f);
	CHECK(bcm2712_iommuc_core_init(sc, &callbacks) == 0);
	CHECK(bcm2712_iommuc_core_reserve(sc, members[member]) == 0);
	CHECK(bcm2712_iommuc_core_retain(sc, members[member]) == 0);
	CHECK(f->calls == 0 && f->barriers == 0);
}
static void
no_reinit(struct bcm2712_iommuc_core *sc, struct fake *f)
{
	struct bcm2712_iommuc_core saved = *sc;
	struct bcm2712_iommuc_io callbacks = io(f);
	unsigned int calls = f->calls;

	CHECK(bcm2712_iommuc_core_init(sc, &callbacks) == EBUSY);
	CHECK(memcmp(sc, &saved, sizeof(*sc)) == 0 && f->calls == calls);
}
static void
fault_stops(struct bcm2712_iommuc_core *sc, struct fake *f, int error,
    unsigned int reserved, unsigned int retained)
{
	struct bcm2712_iommuc_status saved = sc->status, out;
	unsigned int calls = f->calls, barriers = f->barriers, i;

	CHECK(saved.fault == error && !saved.flush_complete);
	CHECK(saved.reserved == reserved && saved.retained == retained);
	for (i = 0; i < 3; i++) {
		CHECK(bcm2712_iommuc_core_reserve(sc, members[i]) == EIO);
		CHECK(bcm2712_iommuc_core_retain(sc, members[i]) == EIO);
		CHECK(bcm2712_iommuc_core_release(sc, members[i]) == EIO);
		CHECK(bcm2712_iommuc_core_enable_flush(sc, members[i]) == EIO);
	}
	CHECK(bcm2712_iommuc_core_reserve(sc, (enum bcm2712_iommuc_mmu)3) == EINVAL);
	CHECK(bcm2712_iommuc_core_fini(sc) == EBUSY);
	no_reinit(sc, f);
	CHECK(bcm2712_iommuc_core_status(sc, &out) == 0);
	CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
	CHECK(f->calls == calls && f->barriers == barriers);
}
static void
validation(void)
{
	struct bcm2712_iommuc_core sc = {0}, saved;
	struct bcm2712_iommuc_io callbacks;
	struct bcm2712_iommuc_status out, poison;
	struct fake f;
	unsigned int i;

	reset_io(&f, &sc, 1);
	callbacks = io(&f);
	memset(&out, 0xa5, sizeof(out)); poison = out;
	CHECK(bcm2712_iommuc_core_status(NULL, &out) == EINVAL);
	CHECK(bcm2712_iommuc_core_status(&sc, &out) == EINVAL);
	CHECK(memcmp(&out, &poison, sizeof(out)) == 0);
	CHECK(bcm2712_iommuc_core_status(&sc, NULL) == EINVAL);
	CHECK(bcm2712_iommuc_core_init(NULL, &callbacks) == EINVAL);
	CHECK(bcm2712_iommuc_core_init(&sc, NULL) == EINVAL);
	CHECK(bcm2712_iommuc_core_fini(NULL) == EINVAL);
	CHECK(bcm2712_iommuc_core_fini(&sc) == EINVAL);
	for (i = 0; i < 3; i++) {
		callbacks = io(&f); saved = sc;
		if (i == 0) callbacks.read_control = NULL;
		if (i == 1) callbacks.write_control = NULL;
		if (i == 2) callbacks.barrier = NULL;
		CHECK(bcm2712_iommuc_core_init(&sc, &callbacks) == EINVAL);
		CHECK(memcmp(&sc, &saved, sizeof(sc)) == 0);
	}
	for (i = 0; i < 3; i++) {
		CHECK(bcm2712_iommuc_core_reserve(NULL, members[i]) == EINVAL);
		CHECK(bcm2712_iommuc_core_retain(&sc, members[i]) == EINVAL);
		CHECK(bcm2712_iommuc_core_release(&sc, members[i]) == EINVAL);
		CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[i]) == EINVAL);
	}
	/* Aliased callback storage must survive init's zeroing. */
	sc.io = io(&f);
	CHECK(bcm2712_iommuc_core_init(&sc, &sc.io) == 0);
	CHECK(sc.io.read_control == read_control && sc.io.write_control == write_control);
	CHECK(sc.io.barrier == barrier && sc.io.arg == &f);
	no_reinit(&sc, &f);
	CHECK(bcm2712_iommuc_core_retain(&sc, BCM2712_IOMMUC_MMU2) == EINVAL);
	CHECK(bcm2712_iommuc_core_release(&sc, BCM2712_IOMMUC_MMU2) == EINVAL);
	CHECK(bcm2712_iommuc_core_enable_flush(&sc, BCM2712_IOMMUC_MMU2) == EINVAL);
	for (i = 0; i < 8; i++) {
		if (i == 2 || i == 4 || i == 5) continue;
		saved = sc;
		CHECK(bcm2712_iommuc_core_reserve(&sc, (enum bcm2712_iommuc_mmu)i) == EINVAL);
		CHECK(bcm2712_iommuc_core_retain(&sc, (enum bcm2712_iommuc_mmu)i) == EINVAL);
		CHECK(bcm2712_iommuc_core_release(&sc, (enum bcm2712_iommuc_mmu)i) == EINVAL);
		CHECK(bcm2712_iommuc_core_enable_flush(&sc, (enum bcm2712_iommuc_mmu)i) == EINVAL);
		CHECK(memcmp(&sc, &saved, sizeof(sc)) == 0);
	}
	for (i = 0; i < 3; i++) {
		CHECK(bcm2712_iommuc_core_reserve(&sc, members[i]) == 0);
		CHECK(sc.status.reserved == (1U << (i + 1)) - 1);
		CHECK(bcm2712_iommuc_core_reserve(&sc, members[i]) == EBUSY);
	}
	CHECK(bcm2712_iommuc_core_fini(&sc) == EBUSY);
	no_reinit(&sc, &f);
	CHECK(bcm2712_iommuc_core_enable_flush(&sc, BCM2712_IOMMUC_MMU4) == EBUSY);
	for (i = 0; i < 3; i++) CHECK(bcm2712_iommuc_core_release(&sc, members[i]) == 0);
	CHECK(bcm2712_iommuc_core_fini(&sc) == 0 && f.calls == 0);
	no_reinit(&sc, &f);
	CHECK(bcm2712_iommuc_core_fini(&sc) == EBUSY);
	CHECK(bcm2712_iommuc_core_reserve(&sc, BCM2712_IOMMUC_MMU4) == EBUSY);
}
static void
transactions(void)
{
	struct bcm2712_iommuc_core sc;
	struct bcm2712_iommuc_status out;
	struct fake f;
	unsigned int m, initial, command, last, bit;

	for (m = 0; m < 3; m++) for (initial = 0; initial < 4; initial++)
	for (command = 0; command < 4; command++) for (last = 0; last < 2; last++) {
		setup(&sc, &f, m); f.initial = initial; f.complete = command;
		f.complete_at = last ? 1024 : 1;
		no_reinit(&sc, &f);
		CHECK(bcm2712_iommuc_core_release(&sc, members[m]) == EBUSY);
		CHECK(bcm2712_iommuc_core_retain(&sc, members[m]) == EBUSY);
		CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[m]) == 0);
		CHECK(sc.status.stage == BCM2712_IOMMUC_COMPLETE && sc.status.flush_complete);
		CHECK(sc.status.poll == f.complete_at && sc.status.reads_completed == f.complete_at + 1);
		CHECK(sc.status.reads_attempted == sc.status.reads_completed);
		CHECK(sc.status.writes_attempted == 1 && sc.status.writes_completed == 1);
		CHECK(sc.status.raw_valid && sc.status.raw_control == f.complete);
		CHECK(sc.status.mutation_attempted && sc.status.fault == 0 && f.barriers == 1);
		CHECK(bcm2712_iommuc_core_status(&sc, &out) == 0 && out.mmu == members[m]);
		CHECK(bcm2712_iommuc_core_fini(&sc) == EBUSY);
		no_reinit(&sc, &f);
		/* A new operation resets counters/validity/completion, not retention. */
		reset_io(&f, &sc, 1U << m); f.initial = 1; f.complete = 3;
		CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[m]) == 0);
		CHECK(sc.status.reads_completed == 2 && sc.status.poll == 1 && f.barriers == 1);
		reset_io(&f, &sc, 1U << m); f.fail_at = 1;
		CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[m]) == EIO);
		CHECK(!sc.status.raw_valid && sc.status.mutation_attempted);
		fault_stops(&sc, &f, EIO, 1U << m, 1U << m);
	}
	for (bit = 3; bit < 32; bit++)
	for (initial = 0; initial < 4; initial++)
	for (command = 0; command < 4; command++) {
		setup(&sc, &f, 1);
		f.initial = (UINT32_C(1) << bit) | initial;
		f.busy = (UINT32_C(1) << bit) | 4;
		f.complete = (UINT32_C(1) << bit) | command;
		f.complete_at = 2;
		CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[1]) == 0);
		CHECK(f.reads == 3 && f.writes == 1 && f.barriers == 1);
		CHECK(sc.status.raw_valid && sc.status.raw_control == f.complete);
		CHECK(sc.status.flush_complete && sc.status.retained == 2);
	}
	for (initial = 4; initial < 8; initial++) {
		setup(&sc, &f, 1); f.initial = initial;
		CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[1]) == EBUSY);
		CHECK(f.reads == 1 && f.writes == 0 && sc.status.raw_valid);
		fault_stops(&sc, &f, EBUSY, 2, 2);
	}
	setup(&sc, &f, 1); f.complete = 0;
	CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[1]) == 0);
	CHECK(sc.status.flush_complete && sc.status.raw_valid && sc.status.raw_control == 0);
	setup(&sc, &f, 1); f.initial = 0x80000004U;
	CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[1]) == EBUSY);
	CHECK(sc.status.raw_control == 0x80000004U && f.writes == 0);
	fault_stops(&sc, &f, EBUSY, 2, 2);
	setup(&sc, &f, 1); f.complete_at = 0; f.busy = 0x80000004U;
	CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[1]) == ETIMEDOUT);
	CHECK(f.reads == 1025 && sc.status.poll == 1024 && sc.status.raw_valid);
	fault_stops(&sc, &f, ETIMEDOUT, 2, 2);
	setup(&sc, &f, 1); f.reenter = true;
	CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[1]) == 0);
	CHECK(bcm2712_iommuc_core_reserve(&sc, BCM2712_IOMMUC_MMU5) == 0);
	CHECK(bcm2712_iommuc_core_release(&sc, BCM2712_IOMMUC_MMU5) == 0);
}
static void
injected_failures(void)
{
	struct bcm2712_iommuc_core sc;
	struct fake f;
	unsigned int at, e;
	const int errors[] = { EIO, ETIMEDOUT };

	for (e = 0; e < 2; e++) for (at = 1; at <= 1026; at++) {
		setup(&sc, &f, 1); f.complete_at = 1024; f.fail_at = at; f.error = errors[e];
		CHECK(bcm2712_iommuc_core_reserve(&sc, members[0]) == 0);
		CHECK(bcm2712_iommuc_core_reserve(&sc, members[2]) == 0);
		CHECK(bcm2712_iommuc_core_enable_flush(&sc, members[1]) == errors[e]);
		CHECK(f.calls == at && sc.status.mutation_attempted == (at != 1));
		CHECK(f.barriers == (at >= 3 ? 1U : 0U));
		CHECK(sc.status.reads_attempted == (at == 1 ? 1U : at - 1));
		CHECK(sc.status.reads_completed == (at <= 2 ? at - 1 : at - 2));
		CHECK(sc.status.writes_attempted == (at >= 2 ? 1U : 0U));
		CHECK(sc.status.writes_completed == (at >= 3 ? 1U : 0U));
		CHECK(sc.status.raw_valid == (at == 2));
		CHECK(sc.status.stage == (at == 1 ? BCM2712_IOMMUC_PREFLIGHT :
		    at == 2 ? BCM2712_IOMMUC_WRITE : BCM2712_IOMMUC_POLL));
		CHECK(sc.status.poll == (at >= 3 ? at - 2 : 0));
		fault_stops(&sc, &f, errors[e], 7, 2);
	}
}
int
main(void)
{

	validation(); transactions(); injected_failures();
	printf("IOMMUC actual-source core: %lu checks PASS\n", checks);
	return 0;
}
