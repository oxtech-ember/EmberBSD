/* Origin: EmberBSD - execute real audio adapter format/trigger/stop functions. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include "sun50i_a133_codec_io.h"

#define AUMODE_PLAY 1
#define AUMODE_RECORD 2
#define AUDIO_ENCODING_SLINEAR_LE 6
#define AUDIO_ENCODING_SLINEAR_BE 7
#define KASSERT(x) assert(x)
#define MIN(a,b) ((a) < (b) ? (a) : (b))
typedef struct { unsigned sample_rate, encoding, precision, validbits, channels; } audio_params_t;
typedef struct { int unused; } audio_filter_reg_t;
typedef int kmutex_t;
static void mutex_enter(kmutex_t *m) { assert(!*m); *m = 1; }
static void mutex_exit(kmutex_t *m) { assert(*m); *m = 0; }
static bool mutex_owned(kmutex_t *m) { return *m != 0; }

struct a133_memory {
	LIST_ENTRY(a133_memory) link;
	void *addr;
	size_t size;
	unsigned mode;
};
struct a133_channel {
	uint8_t *buffer;
	struct a133_pcm_ring ring;
	void (*callback)(void *);
	void *arg;
	bool running;
};
struct a133_softc {
	struct a133_codec_io io;
	struct a133_codec_lease lease;
	struct a133_channel play, record;
	struct a133_pcm_format format[2];
	LIST_HEAD(, a133_memory) memory;
	kmutex_t intr_lock;
	struct { unsigned ev_count; } fault, underrun, overrun, interrupts;
};
static int a133_halt(struct a133_softc *, unsigned);

struct hw {
	uint32_t reg[256];
	unsigned writes, reads, tx_words, rx_words, rx_available;
	bool stuck;
};
static uint32_t
rd(void *cookie, unsigned space, unsigned reg)
{
	struct hw *h = cookie;
	assert(space == A133_CODEC_REG && reg < 1024);
	h->reads++;
	if (reg == 0x40) {
		assert(h->rx_available != 0);
		h->rx_available--;
		h->reg[0x38 / 4] = (h->rx_available ? 0x800000 : 0) |
		    (MIN(h->rx_available, 127) << 8);
		return 0xffff8000U | (h->rx_words++ & 0x7fff);
	}
	return h->reg[reg / 4];
}
static void
wr(void *cookie, unsigned space, unsigned reg, uint32_t value)
{
	struct hw *h = cookie;
	assert(space == A133_CODEC_REG && reg < 1024);
	h->writes++;
	/* Hardware FIFO flush bits clear themselves; status flags are W1C. */
	if ((reg == 0x10 || reg == 0x30) && !h->stuck)
		value &= ~1U;
	if (reg == 0x20) {
		assert((h->reg[0x14 / 4] & 0x7fff00) != 0);
		h->reg[0x14 / 4] -= 0x100;
		h->tx_words++;
	}
	if (reg == 0x14 || reg == 0x38)
		h->reg[reg / 4] &= ~value;
	else
		h->reg[reg / 4] = value;
}
static void wait_us(void *cookie, unsigned us) { (void)cookie; (void)us; }
static uint32_t a133_read(void *sc, unsigned space, unsigned reg) {
	struct a133_softc *s = sc;
	return s->io.read(s->io.cookie, space, reg);
}
static void a133_write(void *sc, unsigned space, unsigned reg, uint32_t value) {
	struct a133_softc *s = sc;
	s->io.write(s->io.cookie, space, reg, value);
}

#include "adapter.inc"

static void callback(void *arg) { (*(unsigned *)arg)++; }

int
main(void)
{
	struct a133_softc sc = { 0 };
	struct hw hw = { 0 };
	struct a133_memory mem;
	uint8_t *buffer = calloc(1, 4096);
	audio_params_t play = {48000, 6, 16, 16, 2}, rec = {16000, 6, 16, 16, 1};
	struct a133_pcm_format saved[2];
	unsigned calls = 0, writes, reads, words, i;

	assert(buffer != NULL);
	sc.io = (struct a133_codec_io){ &hw, rd, wr, wait_us };
	LIST_INIT(&sc.memory);
	assert(a133_set_format(&sc, 3, &play, &rec, NULL, NULL) == 0);
	assert(sc.format[0].dac_fifoc == 0x01004000 &&
	    sc.format[1].adc_fifoc == 0x61001200 && hw.writes == 0 && hw.reads == 0);
	memcpy(saved, sc.format, sizeof(saved));
	rec.encoding = AUDIO_ENCODING_SLINEAR_BE;
	assert(a133_set_format(&sc, 3, &play, &rec, NULL, NULL) == EINVAL);
	assert(memcmp(saved, sc.format, sizeof(saved)) == 0 && hw.writes == 0);
	rec.encoding = 6; rec.validbits = 8;
	assert(a133_set_format(&sc, 2, NULL, &rec, NULL, NULL) == EINVAL);
	assert(a133_set_format(&sc, 0, &play, &rec, NULL, NULL) == EINVAL);
	assert(a133_set_format(&sc, 4, &play, &rec, NULL, NULL) == EINVAL);
	rec.validbits = 16;
	mem.addr = buffer; mem.size = 4096; mem.mode = AUMODE_PLAY;
	LIST_INSERT_HEAD(&sc.memory, &mem, link);
	mutex_enter(&sc.intr_lock);
	assert(a133_trigger(&sc, AUMODE_PLAY, buffer, buffer + 4096, 1024,
	    callback, &calls, &play) == EACCES);
	assert(hw.writes == 0 && hw.reads == 0 && calls == 0 && !sc.play.running);
	sc.lease.prepared = true; sc.lease.routes = AUMODE_PLAY;
	assert(a133_trigger(&sc, 3, buffer, buffer + 4096, 1024,
	    callback, &calls, &play) == EINVAL && hw.writes == 0);
	assert(a133_trigger(&sc, AUMODE_PLAY, buffer, (void *)((uintptr_t)buffer + 8192), 1024,
	    callback, &calls, &play) == EINVAL && hw.writes == 0);
	assert(a133_trigger(&sc, AUMODE_PLAY, buffer, buffer + 4096, 256,
	    callback, &calls, &play) == EINVAL && hw.writes == 0);
	play.encoding = 7;
	assert(a133_trigger(&sc, AUMODE_PLAY, buffer, buffer + 4096, 1024,
	    callback, &calls, &play) == EINVAL && hw.writes == 0);
	play.encoding = 6; hw.reg[0x14 / 4] = 0x00808008;
	assert(a133_trigger(&sc, AUMODE_PLAY, buffer, buffer + 4096, 1024,
	    callback, &calls, &play) == 0);
	assert(sc.play.running && calls == 0 && sc.play.ring.offset == 256);
	assert((hw.reg[0x10 / 4] & 0x1e) == 0xc);
	writes = hw.writes;
	assert(a133_trigger(&sc, AUMODE_PLAY, buffer, buffer + 4096, 1024,
	    callback, &calls, &play) == EBUSY && hw.writes == writes);
	mutex_exit(&sc.intr_lock);
	/* One finite 128-word FIFO budget per IRQ; two complete blocks. */
	for (i = 0; i < 7; i++) {
		hw.reg[0x14 / 4] = 0x00808008;
		words = hw.tx_words;
		assert(a133_interrupt(&sc) == 1 && hw.tx_words - words == 128);
		assert(calls == (i + 2) / 4);
	}
	assert(sc.play.ring.offset == 2048 && sc.interrupts.ev_count == 7);
	/* Complete the second half and wrap the actual production ring. */
	for (i = 0; i < 8; i++) {
		hw.reg[0x14 / 4] = 0x00808008;
		assert(a133_interrupt(&sc) == 1);
	}
	assert(calls == 4 && sc.play.ring.offset == 0);
	mutex_enter(&sc.intr_lock);
	assert(a133_halt(&sc, AUMODE_PLAY) == 0 && !sc.play.running &&
	    sc.play.callback == NULL && (hw.reg[0] & 0x80000000) == 0 &&
	    (hw.reg[0x10 / 4] & 0x1e) == 0);
	mutex_exit(&sc.intr_lock);
	writes = hw.writes; reads = hw.reads;
	assert(a133_interrupt(&sc) == 0 && hw.writes == writes && hw.reads == reads);
	assert(calls == 4);
	/* Flush timeout leaves no running stream or callback behind. */
	mutex_enter(&sc.intr_lock);
	hw.stuck = true;
	assert(a133_trigger(&sc, AUMODE_PLAY, buffer, buffer + 4096, 1024,
	    callback, &calls, &play) == ETIMEDOUT && !sc.play.running &&
	    sc.play.callback == NULL);
	hw.stuck = false; hw.reg[0x14 / 4] = 0;
	assert(a133_trigger(&sc, AUMODE_PLAY, buffer, buffer + 4096, 1024,
	    callback, &calls, &play) == EIO && !sc.play.running);
	hw.reg[0x14 / 4] = 0x00808008;
	assert(a133_trigger(&sc, AUMODE_PLAY, buffer, buffer + 4096, 1024,
	    callback, &calls, &play) == 0);
	mutex_exit(&sc.intr_lock);
	/* Impossible FIFO count is a fault; stop without a callback. */
	hw.reg[0x14 / 4] = 0x00808108;
	assert(a133_interrupt(&sc) == 1 && sc.fault.ev_count == 1 &&
	    !sc.play.running && calls == 4);
	/* Capture drains signed RX words into two finite blocks. */
	mem.mode = AUMODE_RECORD; sc.lease.routes = AUMODE_RECORD;
	mutex_enter(&sc.intr_lock);
	assert(a133_trigger(&sc, AUMODE_RECORD, buffer, buffer + 4096, 1024,
	    callback, &calls, &rec) == 0);
	mutex_exit(&sc.intr_lock);
	for (i = 0; i < 8; i++) {
		hw.rx_available = 128;
		hw.reg[0x38 / 4] = 0x00807f08;
		words = hw.rx_words;
		assert(a133_interrupt(&sc) == 1 && hw.rx_words - words == 128);
	}
	assert(calls == 6 && sc.record.ring.offset == 2048);
	assert(buffer[0] == 0 && buffer[1] == 0x80 &&
	    buffer[2046] == 0xff && buffer[2047] == 0x83);
	mutex_enter(&sc.intr_lock);
	assert(a133_halt(&sc, AUMODE_RECORD) == 0 && sc.record.callback == NULL);
	mutex_exit(&sc.intr_lock);
	writes = hw.writes; reads = hw.reads;
	assert(a133_interrupt(&sc) == 0 && hw.writes == writes && hw.reads == reads);
	LIST_REMOVE(&mem, link); free(buffer);
	puts("A133 audio adapter: formats, silent guards, finite IRQ blocks, wrap, capture and halt passed");
	return 0;
}
