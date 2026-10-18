/*
 * Origin: EmberBSD (AI-assisted).
 * Actual PIC establish/mask/teardown bodies, with modeled controller,
 * allocator and crosscall services. The pending-line model calls the real
 * installed handler on enable; it exposes establish-then-mask's early ISR.
 * Lifecycle calls are serialized, as required by the native PIC contract.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIC_MAXPICS 4
#define PIC_MAXSOURCES 512
#define NIPL 8
#define IPL_VM 5
#define IST_LEVEL 3
#define IST_MPSAFE 0x100
#define KM_SLEEP 0
#define INTRDEVNAMEBUF 256
#define __BIT(n) (UINT32_C(1) << (n))
#define KASSERT(x) assert(x)

typedef unsigned int u_int;
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
	void (*pic_establish_irq)(struct pic_softc *, struct intrsource *);
	void (*pic_source_name)(struct pic_softc *, int, char *, size_t);
};
struct pic_softc {
	const struct pic_ops *pic_ops;
	struct intrsource **pic_sources;
	int pic_irqbase;
	int pic_maxsources;
	const char *pic_name;
	void *pic_percpu;
};
static struct pic_softc *pic_list[PIC_MAXPICS];
static struct intrsource *pic__iplsources[PIC_MAXSOURCES];
static size_t pic_ipl_offset[NIPL + 1];
static struct intrsource *sources[PIC_MAXSOURCES];
static bool mp_online, configure_percpu, expect_masked, establishing;
static bool pending, ready, line_enabled;
static void *caller_cookie;
static unsigned checks, failures, early_calls, handler_calls, drains;
static unsigned allocations, frees;
static char events[256];
static size_t event_count;
static int establish_type;
static struct intrsource *allocated_source;

#define CHECK(x) do { \
	checks++; \
	if (!(x)) { \
		failures++; \
		fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
	} \
} while (0)

static void
record(char event)
{
	assert(event_count + 1 < sizeof(events));
	events[event_count++] = event;
	events[event_count] = '\0';
}

static void *
kmem_zalloc(size_t size, int flags)
{
	void *p;

	assert(flags == KM_SLEEP);
	p = calloc(1, size);
	assert(p != NULL);
	allocations++;
	if (size == sizeof(struct intrsource))
		allocated_source = p;
	record(size == sizeof(struct intrsource) ? 'A' : 'K');
	return p;
}

static void
kmem_free(void *p, size_t size)
{
	assert(drains != 0);
	record(size == sizeof(struct intrsource) ? 'F' : 'f');
	frees++;
	free(p);
}

static size_t
model_strlcat(char *dst, const char *src, size_t size)
{
	const size_t len = strlen(dst), add = strlen(src);

	assert(len + add < size);
	memcpy(dst + len, src, add + 1);
	record('L');
	return len + add;
}

static bool
cpu_intr_p(void)
{
	return false;
}

static bool
cpu_softintr_p(void)
{
	return false;
}

static uint32_t
atomic_inc_32_nv(uint32_t *value)
{
	return ++*value;
}

static uint32_t
atomic_dec_32_nv(uint32_t *value)
{
	assert(*value != 0);
	return --*value;
}

static int
handler(void *arg)
{
	CHECK(arg == &ready);
	handler_calls++;
	if (caller_cookie == NULL || !ready)
		early_calls++;
	return 1;
}

static void
block(struct pic_softc *pic, size_t base, uint32_t mask)
{
	CHECK(base == 352 && mask == __BIT(6));
	if (establishing && expect_masked) {
		CHECK(allocated_source->is_mask_count == 1);
		CHECK(pic->pic_sources[358] == NULL);
		CHECK(pic__iplsources[pic_ipl_offset[IPL_VM]] == NULL);
	}
	line_enabled = false;
	record('B');
}

static void
unblock(struct pic_softc *pic, size_t base, uint32_t mask)
{
	struct intrsource * const is = pic->pic_sources[base + 6];

	CHECK(base == 352 && mask == __BIT(6));
	CHECK(is != NULL && is->is_mask_count == 0);
	line_enabled = true;
	record('U');
	if (pending)
		(void)is->is_func(is->is_arg);
}

static void
configure(struct pic_softc *pic, struct intrsource *is)
{
	CHECK(pic->pic_sources[is->is_irq] == is);
	CHECK(pic__iplsources[pic_ipl_offset[is->is_ipl] +
	    is->is_iplidx] == is);
	CHECK(is->is_mask_count == (expect_masked ? 1u : 0u));
	if (expect_masked)
		CHECK(!line_enabled);
	is->is_percpu = configure_percpu;
	record('E');
}

static void
source_name(struct pic_softc *pic, int irq, char *name, size_t size)
{
	(void)pic;
	CHECK(irq == 358);
	(void)snprintf(name, size, "spi-%d", irq);
	record('N');
}

static void
pic_percpu_evcnt_attach(void *pcpu, void *is, void *ci)
{
	(void)pcpu;
	(void)is;
	(void)ci;
	record('C');
}

static void
pic_percpu_evcnt_deattach(void *pcpu, void *is, void *ci)
{
	(void)pcpu;
	(void)is;
	(void)ci;
	record('T');
}

static void
percpu_foreach(void *pcpu, void (*fn)(void *, void *, void *), void *arg)
{
	fn(pcpu, arg, NULL);
}

static uint64_t
xc_broadcast(u_int flags, void (*fn)(void *, void *), void *a, void *b)
{
	CHECK(flags == 0);
	record('X');
	fn(a, b);
	return UINT64_C(0x123456789);
}

static void
xc_wait(uint64_t token)
{
	CHECK(token == UINT64_C(0x123456789));
	record('W');
}

static void
xc_barrier(u_int flags)
{
	CHECK(flags == 0);
	CHECK(sources[358] == NULL);
	drains++;
	record('D');
}

void *intr_establish_xname(int, int, int, int (*)(void *), void *,
    const char *);
#define strlcat model_strlcat
#include "pic-functions.h"

static const struct pic_ops ops = {
	.pic_unblock_irqs = unblock,
	.pic_block_irqs = block,
	.pic_establish_irq = configure,
	.pic_source_name = source_name,
};

static void
reset(struct pic_softc *pic)
{
	CHECK(allocations == frees);
	memset(sources, 0, sizeof(sources));
	memset(pic__iplsources, 0, sizeof(pic__iplsources));
	memset(pic_ipl_offset, 0, sizeof(pic_ipl_offset));
	memset(pic_list, 0, sizeof(pic_list));
	memset(pic, 0, sizeof(*pic));
	pic->pic_ops = &ops;
	pic->pic_sources = sources;
	pic->pic_irqbase = 0;
	pic->pic_maxsources = PIC_MAXSOURCES;
	pic->pic_name = "model-gicv3";
	pic_list[1] = pic;
	event_count = 0;
	events[0] = '\0';
	mp_online = configure_percpu = expect_masked = establishing = false;
	pending = ready = false;
	line_enabled = true;
	caller_cookie = NULL;
	early_calls = handler_calls = drains = 0;
	establish_type = IST_LEVEL | IST_MPSAFE;
}

static struct intrsource *
establish_masked(void)
{
	struct intrsource *is;

#ifdef PIC_MASKED_API
	is = intr_establish_xname_masked(358, IPL_VM, establish_type,
	    handler, &ready, "vpu");
#else
	/* Causal RED: actual old establish enables before this actual mask. */
	is = intr_establish_xname(358, IPL_VM, establish_type,
	    handler, &ready, "vpu");
	if (is != NULL)
		intr_mask(is);
#endif
	return is;
}

static void
masked_tests(void)
{
	struct pic_softc pic;
	struct intrsource *is;

	for (unsigned mode = 0; mode < 8; mode++) {
		reset(&pic);
		mp_online = (mode & 1) != 0;
		configure_percpu = (mode & 2) != 0;
		establish_type = IST_LEVEL | ((mode & 4) ? IST_MPSAFE : 0);
		expect_masked = establishing = pending = true;
		is = establish_masked();
		establishing = false;
		CHECK(is != NULL);
		assert(is != NULL);
		CHECK(early_calls == 0 && handler_calls == 0);
		CHECK(strcmp(events, "ABNCEKL") == 0);
		CHECK(is->is_mask_count == 1 && !line_enabled);
		CHECK(is->is_irq == 358 && is->is_type == IST_LEVEL);
		CHECK(is->is_arg == &ready && is->is_func == handler);
		CHECK(strcmp(is->is_source, "spi-358") == 0);
		CHECK(strcmp(is->is_xname, "vpu") == 0);
		caller_cookie = is;
		ready = true;
		intr_mask(is);
		CHECK(is->is_mask_count == 2);
		intr_unmask(is);
		CHECK(is->is_mask_count == 1 && handler_calls == 0);
		intr_unmask(is);
		CHECK(is->is_mask_count == 0 && handler_calls == 1 && line_enabled);
		CHECK(early_calls == 0);
		intr_disestablish(is);
		CHECK(strcmp(events, "ABNCEKLUBDfTF") == 0);
		CHECK(sources[358] == NULL && pic__iplsources[0] == NULL);
		CHECK(drains == 1 && allocations == frees);
	}
	reset(&pic);
	expect_masked = establishing = true;
	is = establish_masked();
	assert(is != NULL);
	establishing = false;
	intr_disestablish(is);
	CHECK(strcmp(events, "ABNCEKLBDfTF") == 0);
	CHECK(strchr(events, 'U') == NULL && strchr(events, 'X') == NULL);
	CHECK(drains == 1 && allocations == frees);
}

static void
legacy_tests(void)
{
	struct pic_softc pic;
	struct intrsource *is, sentinel;
	struct pic_ops no_name = ops;

	for (unsigned mode = 0; mode < 8; mode++) {
		reset(&pic);
		mp_online = (mode & 1) != 0;
		configure_percpu = (mode & 2) != 0;
		establish_type = IST_LEVEL | ((mode & 4) ? IST_MPSAFE : 0);
		establishing = true;
		is = intr_establish_xname(358, IPL_VM, establish_type,
		    handler, &ready, "old");
		establishing = false;
#ifdef MULTIPROCESSOR
		CHECK(strcmp(events, mode == 7 ? "ANCEXUWKL" :
		    "ANCEUKL") == 0);
#else
		CHECK(strcmp(events, "ANCEUKL") == 0);
#endif
		CHECK(is->is_mask_count == 0);
		intr_disestablish(is);
	}
	reset(&pic);
	is = intr_establish(358, IPL_VM, IST_LEVEL, handler, &ready);
	CHECK(strcmp(events, "ANCEU") == 0 && is->is_xname == NULL);
	intr_disestablish(is);
	reset(&pic);
	{
		no_name.pic_source_name = NULL;
		pic.pic_ops = &no_name;
		is = intr_establish(358, IPL_VM, IST_LEVEL, handler, &ready);
		assert(is != NULL);
		CHECK(strcmp(events, "ACEU") == 0);
		CHECK(strcmp(is->is_source, "irq 358") == 0);
		intr_disestablish(is);
	}
	reset(&pic);
	/* Reuse a hole at IPL_VM, preserving the following IPL's source. */
	memset(&sentinel, 0, sizeof(sentinel));
	for (unsigned i = IPL_VM + 1; i <= NIPL; i++)
		pic_ipl_offset[i] = 1;
	pic_ipl_offset[NIPL] = 2;
	pic__iplsources[1] = &sentinel;
	is = pic_establish_intr(&pic, 358, IPL_VM, IST_LEVEL, handler,
	    &ready, NULL);
	CHECK(is->is_iplidx == 0 && pic__iplsources[0] == is);
	CHECK(pic__iplsources[1] == &sentinel && pic_ipl_offset[NIPL] == 2);
	intr_disestablish(is);
	reset(&pic);
	/* Insert into an empty IPL before an already populated higher IPL. */
	pic__iplsources[0] = &sentinel;
	pic_ipl_offset[IPL_VM + 2] = 1;
	pic_ipl_offset[NIPL] = 1;
	is = pic_establish_intr(&pic, 358, IPL_VM, IST_LEVEL, handler,
	    &ready, NULL);
	CHECK(pic__iplsources[0] == is && pic__iplsources[1] == &sentinel);
	CHECK(pic_ipl_offset[IPL_VM + 1] == 1);
	CHECK(pic_ipl_offset[NIPL] == 2);
	intr_disestablish(is);
}

static void
rejection_tests(void)
{
	struct pic_softc pic;
	struct intrsource foreign, saved;

	reset(&pic);
	memset(&foreign, 0xa5, sizeof(foreign));
	memcpy(&saved, &foreign, sizeof(saved));
	sources[358] = &foreign;
	CHECK(establish_masked() == NULL);
	CHECK(event_count == 0);
	CHECK(memcmp(&foreign, &saved, sizeof(saved)) == 0);
	CHECK(pic_establish_intr(&pic, 358, IPL_VM, IST_LEVEL, handler,
	    &ready, NULL) == NULL);
	CHECK(event_count == 0 && sources[358] == &foreign);
	sources[358] = NULL;
	CHECK(intr_establish_xname(-1, IPL_VM, IST_LEVEL, handler,
	    &ready, NULL) == NULL);
	CHECK(intr_establish_xname(512, IPL_VM, IST_LEVEL, handler,
	    &ready, NULL) == NULL);
	pic.pic_irqbase = -1;
	CHECK(establish_masked() == NULL);
	pic_list[1] = NULL;
	CHECK(establish_masked() == NULL);
	CHECK(event_count == 0 && allocations == frees);
}

int
main(void)
{
	masked_tests();
	legacy_tests();
	rejection_tests();
	printf("pic-establish-contract: %u checks, %u failures\n", checks,
	    failures);
	return failures != 0;
}
