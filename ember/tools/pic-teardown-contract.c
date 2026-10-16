/*
 * Origin: EmberBSD (AI-assisted).
 * Host contracts for ARM PIC teardown: masked pending must stay blocked,
 * and the source must not be freed before the drain that waits out
 * in-flight dispatch. This is sequential ordering, not a concurrent
 * use-after-free reproducer.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef uint32_t u_int;

#define PIC_MAXSOURCES 64
#define INTRDEVNAMEBUF 256
#define __BIT(n) (1u << (n))
#define __predict_false(x) (x)
#define KASSERT(a) assert(a)
#define KASSERTMSG(a, ...) assert(a)

struct pic_softc;
struct intrsource {
	int (*is_func)(void *);
	void *is_arg;
	struct pic_softc *is_pic;
	uint8_t is_type;
	uint8_t is_ipl;
	uint32_t is_irq;
	uint8_t is_iplidx;
	bool is_mpsafe;
	char is_source[16];
	char *is_xname;
	uint32_t is_mask_count;
	bool is_percpu;
};

struct pic_ops {
	void (*pic_unblock_irqs)(struct pic_softc *, size_t, uint32_t);
	void (*pic_block_irqs)(struct pic_softc *, size_t, uint32_t);
};

struct pic_softc {
	const struct pic_ops *pic_ops;
	struct intrsource **pic_sources;
	size_t pic_maxsources;
	void *pic_percpu;
};

static struct intrsource *pic__iplsources[PIC_MAXSOURCES];
static size_t pic_ipl_offset[8];
static unsigned drain_calls;
static unsigned source_frees;
static void *last_freed;

static void
stub_block(struct pic_softc *pic, size_t base, uint32_t mask)
{

	(void)pic;
	(void)base;
	(void)mask;
}

static const struct pic_ops stub_ops = {
	.pic_block_irqs = stub_block,
};

void
xc_barrier(u_int flags)
{

	(void)flags;
	drain_calls++;
}

void
kmem_free(void *p, size_t sz)
{

	(void)sz;
	if (sz == sizeof(struct intrsource)) {
		assert(drain_calls > 0 &&
		    "source freed before dispatch drain");
		source_frees++;
		last_freed = p;
	}
	free(p);
}

void
pic_percpu_evcnt_deattach(void *v0, void *v1, void *ci)
{

	(void)v0;
	(void)v1;
	(void)ci;
}

void
percpu_foreach(void *pcpu, void (*cb)(void *, void *, void *), void *arg)
{

	(void)pcpu;
	(void)cb;
	(void)arg;
}

#include "pic_blocked_enable_mask.h"
#include "pic_disestablish_source.h"

int
main(void)
{
	struct pic_softc pic;
	struct intrsource *tab[PIC_MAXSOURCES];
	struct intrsource live, gone_slot;
	uint32_t enable;

	memset(&pic, 0, sizeof(pic));
	memset(tab, 0, sizeof(tab));
	memset(&live, 0, sizeof(live));
	memset(&gone_slot, 0, sizeof(gone_slot));
	pic.pic_ops = &stub_ops;
	pic.pic_sources = tab;
	pic.pic_maxsources = PIC_MAXSOURCES;

	live.is_pic = &pic;
	live.is_irq = 3;
	live.is_mask_count = 1;
	tab[3] = &live;

	gone_slot.is_pic = &pic;
	gone_slot.is_irq = 5;
	gone_slot.is_mask_count = 0;
	/* slot 5 empty: source already unlinked */

	enable = pic_blocked_enable_mask(&pic, 0, __BIT(3) | __BIT(5) | __BIT(7));
	assert(enable == 0 &&
	    "masked or missing sources must not be re-enabled");

	live.is_mask_count = 0;
	tab[7] = &live;
	live.is_irq = 7;
	enable = pic_blocked_enable_mask(&pic, 0, __BIT(3) | __BIT(7));
	assert(enable == (__BIT(3) | __BIT(7)));

	tab[7] = NULL;
	{
		struct intrsource *is = calloc(1, sizeof(*is));

		assert(is != NULL);
		is->is_pic = &pic;
		is->is_irq = 9;
		is->is_ipl = 1;
		is->is_iplidx = 0;
		pic_ipl_offset[1] = 0;
		pic__iplsources[0] = is;
		tab[9] = is;
		drain_calls = 0;
		source_frees = 0;
		pic_disestablish_source(is);
		assert(tab[9] == NULL);
		assert(pic__iplsources[0] == NULL);
		assert(drain_calls == 1);
		assert(source_frees == 1);
		assert(last_freed == is);
	}

	puts("pic-teardown-contract: PASS");
	return 0;
}
