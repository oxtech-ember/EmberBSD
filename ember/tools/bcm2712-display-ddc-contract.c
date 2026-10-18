/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual native adapter/core, BSD iic dispatch and EDID validator; host only. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32(x) OSSwapHostToLittleInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#elif defined(__NetBSD__)
#include <sys/endian.h>
#else
#include <endian.h>
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((__aligned__(n)))
#endif
#define _I2C_PRIVATE
#include <dev/i2c/i2cvar.h>
#include <dev/i2c/brcmstb_i2creg.h>
#include <dev/i2c/brcmstb_i2cvar.h>

static unsigned int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)
#define KASSERT(x) assert(x)
#ifndef __predict_false
#define __predict_false(x) (x)
#endif
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define KM_SLEEP 0
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define ASSERT_SLEEPABLE() assert(!cpu_intr_p() && !cpu_softintr_p())
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
struct fixture_once { pthread_mutex_t lock; bool done; };
#define ONCE_DECL(n) struct fixture_once n = { PTHREAD_MUTEX_INITIALIZER, false }
#define RUN_ONCE(o, f) run_once(o, f)
static _Thread_local bool in_irq, in_softirq;
static unsigned int allocated, freed, tag_try, tag_enter, tag_exit, executions;
static bool cold, shutting_down;
static int token;
static bool cpu_intr_p(void) { return in_irq; }
static bool cpu_softintr_p(void) { return in_softirq; }
static void mutex_init(kmutex_t *m, int type, int level)
{ (void)type; (void)level; assert(pthread_mutex_init(&m->native, NULL) == 0); m->held = false; }
static void mutex_destroy(kmutex_t *m)
{ assert(pthread_mutex_destroy(&m->native) == 0); }
static void mutex_enter(kmutex_t *m)
{ if (m->tag) tag_enter++; assert(pthread_mutex_lock(&m->native) == 0); m->owner = pthread_self(); m->held = true; }
static int mutex_tryenter(kmutex_t *m)
{ if (m->tag) tag_try++; if (pthread_mutex_trylock(&m->native) != 0) return 0; m->owner = pthread_self(); m->held = true; return 1; }
static void mutex_exit(kmutex_t *m)
{ assert(m->held && pthread_equal(m->owner, pthread_self())); m->held = false; if (m->tag) tag_exit++; assert(pthread_mutex_unlock(&m->native) == 0); }
static int run_once(struct fixture_once *o, int (*f)(void))
{
	int error = 0;
	assert(pthread_mutex_lock(&o->lock) == 0);
	if (!o->done) { error = f(); o->done = error == 0; }
	assert(pthread_mutex_unlock(&o->lock) == 0); return error;
}
static void *kmem_zalloc(size_t n, int flags)
{ void *p; (void)flags; p = calloc(1, n); assert(p != NULL); allocated++; return p; }
static void kmem_free(void *p, size_t n)
{ (void)n; freed++; free(p); }
#include "display.h"
#include "properties.h"
#include "decoder.h"
#include "adapter-types.h"
#include "bsd-iic.h"
#include "bsd-edid.h"

struct hardware {
	uint32_t reg[22], mask, pending;
	uint8_t edid[128], offset;
	unsigned int reads, writes, mask_reads, status_reads, barriers;
	unsigned int commands, bytes, nack, lose_mask_write;
	uint64_t now;
	bool active, timeout, post_pending, post_unmask, guarded;
	bool block, entered, resume;
};
static struct hardware hw;
static pthread_mutex_t block_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t block_cv = PTHREAD_COND_INITIALIZER;

static void
complete(void)
{
	unsigned int word, byte;

	hw.active = false;
	hw.pending = 2;
	hw.reg[BRCMSTB_I2C_ENABLE / 4] |= BRCMSTB_I2C_INTRP;
	if (hw.nack == hw.commands) {
		hw.reg[BRCMSTB_I2C_ENABLE / 4] |= BRCMSTB_I2C_NOACK;
		return;
	}
	if ((hw.reg[BRCMSTB_I2C_CTL / 4] & BRCMSTB_I2C_DTF) == 0) {
		hw.offset = (uint8_t)hw.reg[BRCMSTB_I2C_DATA_IN(0) / 4];
		return;
	}
	assert(hw.offset == 0 && hw.bytes < 128);
	for (word = 0; word < 8; word++) {
		uint32_t value = 0;
		for (byte = 0; byte < 4; byte++)
			value |= (uint32_t)hw.edid[hw.bytes + word * 4 + byte] << (8 * byte);
		hw.reg[BRCMSTB_I2C_DATA_OUT(word) / 4] = value;
	}
	hw.bytes += hw.reg[BRCMSTB_I2C_COUNT / 4];
}

static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, unsigned int off)
{
	assert(tag == &token && !in_irq && !in_softirq);
	hw.reads++;
	if (handle == 13) {
		assert(off == 0 || off == 4);
		if (off == 4) {
			hw.mask_reads++;
			if (hw.lose_mask_write != 0 && hw.writes + 1 == hw.lose_mask_write)
				hw.mask &= ~2U;
			hw.guarded = true;
			return hw.mask;
		}
		hw.status_reads++;
		if (hw.bytes == 128 && hw.post_pending) hw.pending |= 2;
		return hw.pending;
	}
	assert(handle == 14 && off <= 0x50 && (off & 3) == 0);
	if (off == BRCMSTB_I2C_ENABLE && hw.active && !hw.timeout)
		complete();
	return hw.reg[off / 4];
}

static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int off, uint32_t value)
{
	assert(tag == &token && handle == 14 && off <= 0x50 && (off & 3) == 0);
	assert(!in_irq && !in_softirq && hw.guarded && (hw.mask & 2) != 0);
	hw.guarded = false;
	assert(hw.barriers == hw.writes);
	hw.writes++;
	if (off == BRCMSTB_I2C_ENABLE && value != 0) {
		assert(!hw.active && hw.commands < 5);
		hw.commands++;
		assert(hw.reg[BRCMSTB_I2C_COUNT / 4] == (hw.commands == 1 ? 1U : 32U));
		assert(hw.reg[BRCMSTB_I2C_ADDRESS / 4] == (hw.commands == 1 ? 0xa0U : 0xa1U));
		assert((value & (BRCMSTB_I2C_NOSTOP | BRCMSTB_I2C_NOSTART |
		    BRCMSTB_I2C_RESTART)) == (hw.commands == 1 ? 0x50U :
		    hw.commands == 2 ? 0x10U : hw.commands < 5 ? 0x30U : 0x20U));
		hw.active = true;
	}
	hw.reg[off / 4] = value;
	if (off == BRCMSTB_I2C_CTL && (value & BRCMSTB_I2C_INT_EN) == 0)
		hw.pending = 0;
	if (hw.bytes == 128 && hw.post_unmask && off == BRCMSTB_I2C_ENABLE && value == 0)
		hw.mask &= ~2U;
}

static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int off, unsigned int length, int flags)
{
	assert(tag == &token && handle == 14 && off == 0 && length == 0x58);
	assert(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE));
	hw.barriers++;
}
static void microuptime(struct timeval *out)
{ out->tv_sec = (time_t)(hw.now / 1000000); out->tv_usec = (suseconds_t)(hw.now % 1000000); }
static void delay(unsigned int us) { assert(us == 10); hw.now += us; }

/* Record the exact iic dispatch boundary, then call the unchanged real core. */
int fixture_core_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
    void *, size_t, int);
int
brcmstb_i2c_exec(void *arg, i2c_op_t op, i2c_addr_t address, const void *cmd,
    size_t cmdlen, void *out, size_t length, int flags)
{
	executions++;
	assert(op == I2C_OP_READ_WITH_STOP && address == 0x50 && cmd != NULL);
	assert(cmdlen == 1 && *(const uint8_t *)cmd == 0 && length == 128);
	assert(flags == I2C_F_POLL);
	assert(pthread_mutex_lock(&block_lock) == 0);
	if (hw.block) {
		hw.entered = true; assert(pthread_cond_broadcast(&block_cv) == 0);
		while (!hw.resume) assert(pthread_cond_wait(&block_cv, &block_lock) == 0);
	}
	assert(pthread_mutex_unlock(&block_lock) == 0);
	return fixture_core_exec(arg, op, address, cmd, cmdlen, out, length, flags);
}
#include "adapter.h"

static struct bcm2712_display_contract fixture_contract;
static struct bcm2712_ddc_provider *owner;
static void
prepare(void)
{
	unsigned int i, sum = 0;
	const uint8_t signature[8] = EDID_SIGNATURE;

	assert(bcm2712_ddc_owner == NULL && allocated == freed);
	memset(&hw, 0, sizeof(hw)); hw.mask = 0xf; hw.now = 100;
	hw.reg[BRCMSTB_I2C_CTLHI / 4] = 0xc0;
	for (i = 0; i < 127; i++) hw.edid[i] = (uint8_t)(i * 17U);
	memcpy(hw.edid, signature, sizeof(signature));
	for (i = 0; i < 127; i++) sum += hw.edid[i];
	hw.edid[127] = (uint8_t)(0U - sum);
	memset(&fixture_contract, 0, sizeof(fixture_contract)); fixture_contract.version = BCM2712_DISPLAY_VERSION;
	for (i = 0; i < BCM2712_DISPLAY_NMEM; i++)
		fixture_contract.memory[i] = bcm2712_display_resources[i].memory;
	for (i = 0; i < BCM2712_DISPLAY_NIRQ; i++)
		fixture_contract.irq[i] = bcm2712_display_irqs[i].gsi;
	owner = NULL; tag_try = tag_enter = tag_exit = executions = 0;
}
static int create(void)
{ return bcm2712_display_ddc_create(&token, &fixture_contract, &fixture_contract.memory[13], 13,
    &fixture_contract.memory[14], 14, &token, &owner); }
static void
created(void)
{
	CHECK(create() == 0 && owner != NULL && bcm2712_ddc_owner == owner);
	CHECK(owner->tag.ic_cookie == &owner->core);
	CHECK(owner->tag.ic_exec == brcmstb_i2c_exec && owner->core.io.arg == owner);
	CHECK(owner->tag.ic_read_byte == NULL && owner->tag.ic_write_byte == NULL);
	CHECK(owner->tag.ic_acquire_bus == NULL && owner->tag.ic_release_bus == NULL);
	owner->tag.ic_bus_lock.tag = true;
}
static void unchanged(const uint8_t *data)
{ unsigned int i; for (i = 0; i < 128; i++) CHECK(data[i] == 0xa5); }
static void
retained(int error)
{
	struct bcm2712_ddc_status status;
	unsigned int reads = hw.reads, writes = hw.writes;

	CHECK(bcm2712_display_ddc_status(owner, &status) == 0 && status.fault == error);
	CHECK(bcm2712_display_ddc_destroy(owner) == error);
	CHECK(bcm2712_ddc_owner == owner && allocated == freed + 1);
	CHECK(hw.reads == reads && hw.writes == writes);
	/* Model a controlled reboot only after retention has been established. */
	iic_tag_fini(&owner->tag); kmem_free(owner, sizeof(*owner));
	bcm2712_ddc_owner = owner = NULL;
}

struct worker { int result; uint8_t data[128]; atomic_bool started, done; };
static void *reader(void *arg)
{ struct worker *w = arg; atomic_store(&w->started, true);
    w->result = bcm2712_display_ddc_read_edid(owner, w->data, sizeof(w->data));
    atomic_store(&w->done, true); return NULL; }
static void *destroyer(void *arg)
{ struct worker *w = arg; atomic_store(&w->started, true);
    w->result = bcm2712_display_ddc_destroy(owner);
    atomic_store(&w->done, true); return NULL; }
static void *observer(void *arg)
{ struct worker *w = arg; struct bcm2712_ddc_status status;
    atomic_store(&w->started, true); w->result = bcm2712_display_ddc_status(owner, &status);
    atomic_store(&w->done, true); return NULL; }
static void
serialized(bool destroy)
{
	struct worker read = { 0 }, other = { 0 };
	pthread_t t1, t2;

	prepare(); created(); hw.block = true;
	CHECK(pthread_create(&t1, NULL, reader, &read) == 0);
	assert(pthread_mutex_lock(&block_lock) == 0);
	while (!hw.entered) assert(pthread_cond_wait(&block_cv, &block_lock) == 0);
	assert(pthread_mutex_unlock(&block_lock) == 0);
	CHECK(pthread_create(&t2, NULL, destroy ? destroyer : observer, &other) == 0);
	while (!atomic_load(&other.started)) { }
	CHECK(!atomic_load(&other.done));
	assert(pthread_mutex_lock(&block_lock) == 0); hw.resume = true;
	assert(pthread_cond_broadcast(&block_cv) == 0);
	assert(pthread_mutex_unlock(&block_lock) == 0);
	CHECK(pthread_join(t1, NULL) == 0 && pthread_join(t2, NULL) == 0);
	CHECK(read.result == 0 && other.result == 0);
	CHECK(memcmp(read.data, hw.edid, sizeof(read.data)) == 0);
	if (!destroy) CHECK(bcm2712_display_ddc_destroy(owner) == 0);
	CHECK(bcm2712_ddc_owner == NULL && allocated == freed);
}

int
main(void)
{
	struct bcm2712_ddc_status status, expected;
	struct bcm2712_display_memory bsc, ddc;
	struct bcm2712_ddc_provider *sentinel = (void *)(uintptr_t)1;
	uint8_t data[128];
	unsigned int i, reads;

	prepare(); bsc = fixture_contract.memory[13]; ddc = fixture_contract.memory[14];
	for (i = 0; i < 32; i++) {
		fixture_contract.memory[i].address++;
		CHECK(create() == EINVAL && owner == NULL && hw.reads == 0);
		fixture_contract.memory[i].address--;
	}
	for (i = 0; i < 7; i++) {
		fixture_contract.irq[i]++; CHECK(create() == EINVAL && hw.reads == 0);
		fixture_contract.irq[i]--;
	}
	CHECK(bcm2712_display_ddc_create(NULL, &fixture_contract, &bsc, 13, &ddc, 14, &token, &owner) == EINVAL);
	CHECK(bcm2712_display_ddc_create(&token, NULL, &bsc, 13, &ddc, 14, &token, &owner) == EINVAL);
	CHECK(bcm2712_display_ddc_create(&token, &fixture_contract, NULL, 13, &ddc, 14, &token, &owner) == EINVAL);
	CHECK(bcm2712_display_ddc_create(&token, &fixture_contract, &bsc, 13, NULL, 14, &token, &owner) == EINVAL);
	CHECK(bcm2712_display_ddc_create(&token, &fixture_contract, &bsc, 13, &ddc, 14, NULL, &owner) == EINVAL);
	CHECK(bcm2712_display_ddc_create(&token, &fixture_contract, &bsc, 13, &ddc, 14, &token, NULL) == EINVAL);
	bsc.length--; CHECK(bcm2712_display_ddc_create(&token, &fixture_contract, &bsc, 13, &ddc, 14, &token, &sentinel) == EINVAL);
	CHECK(sentinel == (void *)(uintptr_t)1 && hw.reads == 0); bsc = fixture_contract.memory[13];
	ddc = fixture_contract.memory[15]; CHECK(bcm2712_display_ddc_create(&token, &fixture_contract, &bsc, 13, &ddc, 14, &token, &sentinel) == EINVAL);
	CHECK(sentinel == (void *)(uintptr_t)1 && hw.reads == 0);
	for (i = 0; i < 2; i++) {
		prepare(); if (i == 0) hw.mask &= ~2U; else hw.pending = 2;
		CHECK(create() == EBUSY && owner == NULL && allocated == freed && hw.writes == 0);
	}
	prepare(); created(); reads = hw.reads;
	memset(&status, 0xa5, sizeof(status)); memset(&expected, 0, sizeof(expected));
	expected.bsc_mask = 0xf; expected.status_valid = expected.mask_valid = true;
	CHECK(bcm2712_display_ddc_status(owner, &status) == 0);
	CHECK(memcmp(&status, &expected, sizeof(status)) == 0 && hw.reads == reads);

	CHECK(create() == EBUSY && hw.reads == reads);
	for (i = 0; i < 2; i++) {
		in_irq = i == 0; in_softirq = i == 1;
		CHECK(create() == EWOULDBLOCK);
		CHECK(bcm2712_display_ddc_read_edid(owner, data, sizeof(data)) == EWOULDBLOCK);
		CHECK(bcm2712_display_ddc_status(owner, &status) == EWOULDBLOCK);
		CHECK(bcm2712_display_ddc_destroy(owner) == EWOULDBLOCK);
		CHECK(hw.reads == reads && hw.writes == 0);
	}
	in_irq = in_softirq = false;
	for (i = 0; i < 130; i++) if (i != 128)
		CHECK(bcm2712_display_ddc_read_edid(owner, data, i) == EINVAL);
	CHECK(bcm2712_display_ddc_read_edid(owner, NULL, 128) == EINVAL);
	CHECK(hw.reads == reads && hw.writes == 0 && executions == 0);
	mutex_enter(&owner->tag.ic_bus_lock); memset(data, 0xa5, sizeof(data));
	CHECK(bcm2712_display_ddc_read_edid(owner, data, sizeof(data)) == EBUSY);
	CHECK(tag_exit == 0 && executions == 0 && hw.reads == reads);
	mutex_exit(&owner->tag.ic_bus_lock); unchanged(data);
	CHECK(bcm2712_display_ddc_status(owner, &status) == 0 && status.fault == 0);
	CHECK(bcm2712_display_ddc_read_edid(owner, data, sizeof(data)) == 0);
	CHECK(memcmp(data, hw.edid, sizeof(data)) == 0 && executions == 1);
	CHECK(tag_try == 2 && tag_enter == 1 && tag_exit == 2);
	CHECK(bcm2712_display_ddc_status(owner, &status) == 0);
	CHECK(status.fault == 0 && status.core.fault == 0 && !status.core.active);
	CHECK(status.core.commands == 5 && hw.commands == 5 && hw.bytes == 128);
	CHECK(status.status_valid && status.mask_valid && status.bsc_status == 0 && status.bsc_mask == 0xf);
	reads = hw.reads; CHECK(bcm2712_display_ddc_status(owner, &status) == 0 && hw.reads == reads);
	CHECK(bcm2712_display_ddc_destroy(owner) == 0 && bcm2712_ddc_owner == NULL);
	CHECK(bcm2712_display_ddc_status(owner, &status) == ENXIO);
	CHECK(bcm2712_display_ddc_destroy(owner) == ENXIO);
	for (i = 0; i < 2; i++) {
		prepare(); created(); hw.edid[i == 0 ? 0 : 127] ^= 1;
		/* Isolate a bad signature from the checksum failure. */
		if (i == 0) hw.edid[127]--;
		memset(data, 0xa5, sizeof(data));
		CHECK(bcm2712_display_ddc_read_edid(owner, data, sizeof(data)) == EBADMSG);
		unchanged(data); CHECK(bcm2712_display_ddc_status(owner, &status) == 0);
		CHECK(status.fault == 0 && status.core.fault == 0);
		CHECK(bcm2712_display_ddc_destroy(owner) == 0);
	}
	for (i = 0; i < 3; i++) {
		prepare(); created(); memset(data, 0xa5, sizeof(data));
		hw.reg[(i == 0 ? BRCMSTB_I2C_COUNT :
		    i == 1 ? BRCMSTB_I2C_ENABLE : BRCMSTB_I2C_CTL) / 4] =
		    i == 2 ? BRCMSTB_I2C_INT_EN : 1;
		CHECK(bcm2712_display_ddc_read_edid(owner, data, sizeof(data)) == EBUSY);
		unchanged(data); CHECK(hw.writes == 0 && owner->core.status.fault == 0);
		CHECK(!owner->core.status.mutation_attempted); retained(EBUSY);
	}
	for (i = 1; i <= 5; i++) {
		unsigned int writes;

		prepare(); created(); hw.nack = i; memset(data, 0xa5, sizeof(data));
		CHECK(bcm2712_display_ddc_read_edid(owner, data, sizeof(data)) == EIO);
		unchanged(data); writes = hw.writes; reads = hw.reads;
		CHECK(bcm2712_display_ddc_read_edid(owner, data, sizeof(data)) == EIO);
		CHECK(hw.writes == writes && hw.reads == reads && tag_exit == 1);
		retained(EIO);
	}
	prepare(); created(); hw.timeout = true; memset(data, 0xa5, sizeof(data));
	CHECK(bcm2712_display_ddc_read_edid(owner, data, sizeof(data)) == ETIMEDOUT);
	unchanged(data); retained(ETIMEDOUT);
	for (i = 0; i < 2; i++) {
		prepare(); created(); hw.post_pending = i == 0; hw.post_unmask = i == 1;
		memset(data, 0xa5, sizeof(data));
		CHECK(bcm2712_display_ddc_read_edid(owner, data, sizeof(data)) == EBUSY);
		unchanged(data); CHECK(owner->core.status.fault == 0 && owner->fault == EBUSY);
		retained(EBUSY);
	}
	for (i = 1; i <= 34; i++) {
		prepare(); created(); hw.lose_mask_write = i; memset(data, 0xa5, sizeof(data));
		CHECK(bcm2712_display_ddc_read_edid(owner, data, sizeof(data)) == EBUSY);
		CHECK(hw.writes == i - 1); unchanged(data); retained(EBUSY);
	}
	serialized(false); serialized(true);
	CHECK(allocated == freed);
	printf("BCM2712 DDC adapter: %u checks PASS; actual core/iic/EDID, host only\n", checks);
	return 0;
}
