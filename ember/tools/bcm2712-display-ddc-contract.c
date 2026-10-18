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
static atomic_uint provider_lock_attempts;
static int token;
static bool cpu_intr_p(void) { return in_irq; }
static bool cpu_softintr_p(void) { return in_softirq; }
static void mutex_init(kmutex_t *m, int type, int level)
{ (void)type; (void)level; assert(pthread_mutex_init(&m->native, NULL) == 0); m->held = false; }
static void mutex_destroy(kmutex_t *m)
{ assert(pthread_mutex_destroy(&m->native) == 0); }
static void mutex_enter(kmutex_t *m)
{ if (m->tag) tag_enter++; else atomic_fetch_add(&provider_lock_attempts, 1); assert(pthread_mutex_lock(&m->native) == 0); m->owner = pthread_self(); m->held = true; }
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
	uint8_t edid[256], offset, offsets[4];
	unsigned int reads, writes, mask_reads, status_reads, barriers;
	unsigned int commands, bytes, block_bytes, nack, lose_mask_write;
	unsigned int post_at, busy_second, timeout_exec, block_exec;
	uint64_t trace;
	uint64_t now;
	bool active, timeout, post_pending, post_unmask, guarded;
	bool block, entered, resume;
};
static struct hardware hw;
/* Fixed-width byte hashing makes the old base MMIO transcript endian-neutral. */
static void
trace(unsigned int operation, unsigned int handle, unsigned int offset,
    uint32_t value)
{
	uint32_t words[] = { operation, handle, offset, value };

	for (unsigned int i = 0; i < 4; i++) {
		for (unsigned int j = 0; j < 4; j++) {
			hw.trace ^= (words[i] >> (j * 8)) & 0xffU;
			hw.trace *= UINT64_C(1099511628211);
		}
	}
}

static uint32_t
observed(unsigned int handle, unsigned int offset, uint32_t value)
{

	trace('R', handle, offset, value);
	return value;
}

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
		hw.block_bytes = 0;
		return;
	}
	assert((hw.offset == 0 || hw.offset == 128) && hw.block_bytes < 128);
	for (word = 0; word < 8; word++) {
		uint32_t value = 0;
		for (byte = 0; byte < 4; byte++)
			value |= (uint32_t)hw.edid[hw.offset + hw.block_bytes + word * 4 + byte] << (8 * byte);
		hw.reg[BRCMSTB_I2C_DATA_OUT(word) / 4] = value;
	}
	hw.bytes += hw.reg[BRCMSTB_I2C_COUNT / 4];
	hw.block_bytes += hw.reg[BRCMSTB_I2C_COUNT / 4];
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
			return observed(13, off, hw.mask);
		}
		hw.status_reads++;
		if (hw.bytes == hw.post_at && hw.post_pending) hw.pending |= 2;
		return observed(13, off, hw.pending);
	}
	assert(handle == 14 && off <= 0x50 && (off & 3) == 0);
	if (off == BRCMSTB_I2C_ENABLE && hw.active && !hw.timeout)
		complete();
	return observed(14, off, hw.reg[off / 4]);
}

static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int off, uint32_t value)
{
	assert(tag == &token && handle == 14 && off <= 0x50 && (off & 3) == 0);
	assert(!in_irq && !in_softirq && hw.guarded && (hw.mask & 2) != 0);
	hw.guarded = false;
	assert(hw.barriers == hw.writes);
	trace('W', (unsigned int)handle, off, value);
	hw.writes++;
	if (off == BRCMSTB_I2C_ENABLE && value != 0) {
		unsigned int command = hw.commands % 5 + 1;
		assert(!hw.active && hw.commands < 10);
		hw.commands++;
		assert(hw.reg[BRCMSTB_I2C_COUNT / 4] == (command == 1 ? 1U : 32U));
		assert(hw.reg[BRCMSTB_I2C_ADDRESS / 4] == (command == 1 ? 0xa0U : 0xa1U));
		assert((value & (BRCMSTB_I2C_NOSTOP | BRCMSTB_I2C_NOSTART |
		    BRCMSTB_I2C_RESTART)) == (command == 1 ? 0x50U :
		    command == 2 ? 0x10U : command < 5 ? 0x30U : 0x20U));
		hw.active = true;
	}
	hw.reg[off / 4] = value;
	if (off == BRCMSTB_I2C_CTL && (value & BRCMSTB_I2C_INT_EN) == 0)
		hw.pending = 0;
	if (hw.bytes == hw.post_at && hw.post_unmask && off == BRCMSTB_I2C_ENABLE && value == 0)
		hw.mask &= ~2U;
}

static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int off, unsigned int length, int flags)
{
	assert(tag == &token && handle == 14 && off == 0 && length == 0x58);
	assert(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE));
	trace('B', (unsigned int)handle, off, length | (unsigned int)flags << 16);
	hw.barriers++;
}
static void microuptime(struct timeval *out)
{ out->tv_sec = (time_t)(hw.now / 1000000); out->tv_usec = (suseconds_t)(hw.now % 1000000); }
static void delay(unsigned int us) { assert(us == 10); hw.now += us; }

/* Record the exact iic dispatch boundary, then call the unchanged real core. */
static void transfer_locked(void *);
int fixture_core_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
    void *, size_t, int);
int
brcmstb_i2c_exec(void *arg, i2c_op_t op, i2c_addr_t address, const void *cmd,
    size_t cmdlen, void *out, size_t length, int flags)
{
	transfer_locked(arg);
	executions++;
	assert(op == I2C_OP_READ_WITH_STOP && address == 0x50 && cmd != NULL);
	assert(cmdlen == 1 && length == 128 && executions <= 2);
	assert(*(const uint8_t *)cmd == 0 || *(const uint8_t *)cmd == 128);
	hw.offsets[executions - 1] = *(const uint8_t *)cmd;
	trace('X', (unsigned int)address, *(const uint8_t *)cmd, (uint32_t)length);
	if (executions == 2 && hw.busy_second != 0)
		hw.reg[(hw.busy_second == 1 ? BRCMSTB_I2C_COUNT :
		    hw.busy_second == 2 ? BRCMSTB_I2C_ENABLE : BRCMSTB_I2C_CTL) / 4] =
		    hw.busy_second == 3 ? BRCMSTB_I2C_INT_EN : 1;
	if (hw.timeout_exec == executions) hw.timeout = true;
	assert(flags == I2C_F_POLL);
	assert(pthread_mutex_lock(&block_lock) == 0);
	if (hw.block && (hw.block_exec == 0 || hw.block_exec == executions)) {
		hw.entered = true; assert(pthread_cond_broadcast(&block_cv) == 0);
		while (!hw.resume) assert(pthread_cond_wait(&block_cv, &block_lock) == 0);
	}
	assert(pthread_mutex_unlock(&block_lock) == 0);
	return fixture_core_exec(arg, op, address, cmd, cmdlen, out, length, flags);
}
#include "adapter.h"

static void
transfer_locked(void *core)
{
	struct bcm2712_ddc_provider *sc = ((struct brcmstb_i2c *)core)->io.arg;

	assert(bcm2712_ddc_lock.held && pthread_equal(bcm2712_ddc_lock.owner, pthread_self()));
	assert(sc->tag.ic_bus_lock.held && pthread_equal(sc->tag.ic_bus_lock.owner, pthread_self()));
}

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
	hw.post_at = 128; hw.trace = UINT64_C(14695981039346656037);
	for (i = 128; i < 256; i++) hw.edid[i] = (uint8_t)(i * 29U + 7U);
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

struct worker { int result; uint8_t data[256]; size_t length; bool all; atomic_bool started, done; };
static void *reader(void *arg)
{ struct worker *w = arg; atomic_store(&w->started, true);
    #ifndef DDC_BASE_ONLY
    if (w->all)
        w->result = bcm2712_display_ddc_read_edid_complete(owner, w->data, sizeof(w->data), &w->length);
    else
#endif
        w->result = bcm2712_display_ddc_read_edid(owner, w->data, 128);
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
checksum(uint8_t *block)
{
	unsigned int sum = 0;

	for (unsigned int i = 0; i < 127; i++) sum += block[i];
	block[127] = (uint8_t)(0U - sum);
}
static void
serialized(bool destroy, bool all)
{
	struct worker read = { .all = all }, other = { 0 };
	pthread_t t1, t2;
	unsigned int attempts;

	prepare();
	if (all) { hw.edid[126] = 1; checksum(hw.edid); checksum(hw.edid + 128); hw.block_exec = 2; }
	created(); hw.block = true;
	CHECK(pthread_create(&t1, NULL, reader, &read) == 0);
	assert(pthread_mutex_lock(&block_lock) == 0);
	while (!hw.entered) assert(pthread_cond_wait(&block_cv, &block_lock) == 0);
	assert(pthread_mutex_unlock(&block_lock) == 0);
	attempts = atomic_load(&provider_lock_attempts);
	CHECK(pthread_create(&t2, NULL, destroy ? destroyer : observer, &other) == 0);
	while (!atomic_load(&other.started) || atomic_load(&provider_lock_attempts) == attempts) { }
	CHECK(!atomic_load(&other.done));
	assert(pthread_mutex_lock(&block_lock) == 0); hw.resume = true;
	assert(pthread_cond_broadcast(&block_cv) == 0);
	assert(pthread_mutex_unlock(&block_lock) == 0);
	CHECK(pthread_join(t1, NULL) == 0 && pthread_join(t2, NULL) == 0);
	CHECK(read.result == 0 && other.result == 0);
	CHECK(memcmp(read.data, hw.edid, all ? 256 : 128) == 0);
	if (!destroy) CHECK(bcm2712_display_ddc_destroy(owner) == 0);
	CHECK(bcm2712_ddc_owner == NULL && allocated == freed);
}

#ifndef DDC_BASE_ONLY
static void
prepare_complete(unsigned int extensions)
{

	prepare();
	hw.edid[126] = (uint8_t)extensions;
	checksum(hw.edid);
	/* Deliberately not a base signature. This block needs checksum only. */
	hw.edid[128] = 2;
	hw.edid[129] = 3;
	checksum(hw.edid + 128);
	created();
}

static void
complete_unchanged(const uint8_t *data, size_t length)
{

	for (size_t i = 0; i < 264; i++) CHECK(data[i] == 0xa5);
	CHECK(length == 777);
}

static void
complete_clean(void)
{
	struct bcm2712_ddc_status status;

	CHECK(bcm2712_display_ddc_status(owner, &status) == 0);
	CHECK(status.fault == 0 && status.core.fault == 0);
	CHECK(tag_try == 1 && tag_enter == 0 && tag_exit == 1);
	CHECK(bcm2712_display_ddc_destroy(owner) == 0);
}

static void
complete_tests(void)
{
	uint8_t data[264];
	size_t length;
	unsigned int reads, writes, all_writes = 0;

	prepare_complete(1); reads = hw.reads;
	memset(data, 0xa5, sizeof(data)); length = 777;
	for (size_t capacity = 0; capacity < 256; capacity++)
		CHECK(bcm2712_display_ddc_read_edid_complete(owner, data,
		    capacity, &length) == EINVAL);
	CHECK(bcm2712_display_ddc_read_edid_complete(owner, NULL, 256, &length) == EINVAL);
	CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, 256, NULL) == EINVAL);
	CHECK(hw.reads == reads && hw.writes == 0 && executions == 0 && tag_try == 0);
	complete_unchanged(data, length);
	for (unsigned int i = 0; i < 2; i++) {
		in_irq = i == 0; in_softirq = i == 1;
		CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, 256, &length) == EWOULDBLOCK);
		CHECK(hw.reads == reads && hw.writes == 0 && executions == 0 && tag_try == 0);
	}
	in_irq = in_softirq = false;
	CHECK(bcm2712_display_ddc_destroy(owner) == 0);

	for (unsigned int count = 0; count <= 1; count++) {
		for (size_t capacity = 256; capacity <= sizeof(data); capacity++) {
			prepare_complete(count); memset(data, 0xa5, sizeof(data)); length = 777;
			CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, capacity, &length) == 0);
			CHECK(length == (count == 0 ? 128U : 256U));
			CHECK(memcmp(data, hw.edid, length) == 0);
			for (size_t i = length; i < 256; i++) CHECK(data[i] == 0);
			for (size_t i = 256; i < sizeof(data); i++) CHECK(data[i] == 0xa5);
			CHECK(executions == count + 1 && hw.offsets[0] == 0);
			CHECK(count == 0 || hw.offsets[1] == 128);
			CHECK(hw.commands == (count + 1) * 5 && hw.bytes == (count + 1) * 128);
			CHECK(owner->core.status.commands == 5 && owner->core.status.fault == 0);
			if (count == 1) all_writes = hw.writes;
			complete_clean();
		}
	}
	CHECK(all_writes == 68);
	for (unsigned int count = 2; count <= 255; count += 253) {
		prepare_complete(count); memset(data, 0xa5, sizeof(data)); length = 777;
		CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, sizeof(data), &length) == EOPNOTSUPP);
		CHECK(executions == 1 && hw.commands == 5 && hw.bytes == 128);
		complete_unchanged(data, length); complete_clean();
	}
	for (unsigned int kind = 0; kind < 3; kind++) {
		prepare_complete(1);
		if (kind == 0) { hw.edid[0] ^= 1; checksum(hw.edid); }
		if (kind == 1) hw.edid[127] ^= 1;
		if (kind == 2) hw.edid[255] ^= 1;
		memset(data, 0xa5, sizeof(data)); length = 777;
		CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, sizeof(data), &length) == EBADMSG);
		CHECK(executions == (kind == 2 ? 2U : 1U));
		complete_unchanged(data, length); complete_clean();
	}
	prepare_complete(1); reads = hw.reads;
	memset(data, 0xa5, sizeof(data)); length = 777;
	mutex_enter(&owner->tag.ic_bus_lock);
	CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, sizeof(data), &length) == EBUSY);
	CHECK(executions == 0 && hw.reads == reads && hw.writes == 0 && tag_exit == 0);
	mutex_exit(&owner->tag.ic_bus_lock);
	complete_unchanged(data, length);
	CHECK(owner->fault == 0 && bcm2712_display_ddc_destroy(owner) == 0);

	/* Every command in both actual 128-byte core transfers can fail. */
	for (unsigned int command = 1; command <= 10; command++) {
		prepare_complete(1); hw.nack = command;
		memset(data, 0xa5, sizeof(data)); length = 777;
		CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, sizeof(data), &length) == EIO);
		CHECK(executions == (command <= 5 ? 1U : 2U) && hw.commands == command);
		CHECK(tag_try == 1 && tag_enter == 0 && tag_exit == 1);
		complete_unchanged(data, length); reads = hw.reads; writes = hw.writes;
		CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, sizeof(data), &length) == EIO);
		CHECK(hw.reads == reads && hw.writes == writes && tag_exit == 1);
		complete_unchanged(data, length); retained(EIO);
	}
	for (unsigned int block = 1; block <= 2; block++) {
		prepare_complete(1); hw.timeout_exec = block;
		memset(data, 0xa5, sizeof(data)); length = 777;
		CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, sizeof(data), &length) == ETIMEDOUT);
		CHECK(executions == block && tag_exit == 1);
		complete_unchanged(data, length); retained(ETIMEDOUT);
		for (unsigned int busy = 1; busy <= 3; busy++) {
			prepare_complete(1);
			if (block == 2) hw.busy_second = busy;
			else hw.reg[(busy == 1 ? BRCMSTB_I2C_COUNT :
			    busy == 2 ? BRCMSTB_I2C_ENABLE : BRCMSTB_I2C_CTL) / 4] =
			    busy == 3 ? BRCMSTB_I2C_INT_EN : 1;
			memset(data, 0xa5, sizeof(data)); length = 777;
			CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, sizeof(data), &length) == EBUSY);
			CHECK(executions == block && hw.writes == (block == 1 ? 0U : 34U));
			CHECK(owner->core.status.fault == 0 && !owner->core.status.mutation_attempted);
			CHECK(tag_try == 1 && tag_exit == 1);
			complete_unchanged(data, length); retained(EBUSY);
		}
		for (unsigned int guard = 0; guard < 2; guard++) {
			prepare_complete(1); hw.post_at = block * 128;
			hw.post_pending = guard == 0; hw.post_unmask = guard == 1;
			memset(data, 0xa5, sizeof(data)); length = 777;
			CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, sizeof(data), &length) == EBUSY);
			CHECK(executions == block && owner->core.status.fault == 0);
			CHECK(tag_try == 1 && tag_exit == 1);
			complete_unchanged(data, length); retained(EBUSY);
		}
	}
	for (unsigned int write = 1; write <= all_writes; write++) {
		prepare_complete(1); hw.lose_mask_write = write;
		memset(data, 0xa5, sizeof(data)); length = 777;
		CHECK(bcm2712_display_ddc_read_edid_complete(owner, data, sizeof(data), &length) == EBUSY);
		CHECK(hw.writes == write - 1 && tag_try == 1 && tag_exit == 1);
		complete_unchanged(data, length); retained(EBUSY);
	}
	serialized(false, true); serialized(true, true);
}
#endif

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
	/* Accepted EmberBSD 6245a467 adapter, same actual core/iic fixture and input. */
	CHECK(hw.trace == UINT64_C(0x2f1f210dc513d0a0));
	printf("Base EDID MMIO transcript: %016llx\n", (unsigned long long)hw.trace);
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
	serialized(false, false); serialized(true, false);
#ifndef DDC_BASE_ONLY
	complete_tests();
#endif
	CHECK(allocated == freed);
	printf("BCM2712 DDC adapter: %u checks PASS; actual core/iic/EDID, host only\n", checks);
	return 0;
}
