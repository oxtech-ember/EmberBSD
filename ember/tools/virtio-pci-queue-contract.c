/* Origin: EmberBSD native VirtIO PCI queue teardown contract, 2026-10-08. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#include "virtio_pcireg.h"

#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define BUS_ADDR_LO32(a) ((uint32_t)(a))
#define BUS_ADDR_HI32(a) ((uint32_t)((a) >> 32))
#define VIRTIO_PAGE_SIZE 4096
#define VIRTIO_MSIX_QUEUE_VECTOR_INDEX 1
#define VRING_DESC_CHAIN_END UINT16_MAX
#define BUS_DMASYNC_POSTWRITE 1
#if KERNEL_ASSERTIONS
#define KASSERT(e) assert(e)
#else
#define KASSERT(e) ((void)0)
#endif

typedef unsigned int bus_space_tag_t;
typedef unsigned int bus_space_handle_t;
typedef unsigned int bus_size_t;

struct virtqueue {
	uint16_t vq_index, vq_num, vq_free_idx;
	uint32_t vq_notify_off;
	uint64_t vq_availoffset, vq_usedoffset;
	void *vq_vaddr, *vq_dmamap;
	uint32_t *vq_descx;
	size_t vq_bytesize;
	struct { uint16_t next; } vq_desc[2];
	struct { uint64_t ds_addr; } vq_segs[1];
	int vq_freedesc_lock, vq_uring_lock, vq_aring_lock;
};
struct virtio_softc;
struct virtio_ops {
	void (*setup_queue)(struct virtio_softc *, uint16_t, uint64_t);
	void (*free_interrupts)(struct virtio_softc *);
};
struct virtio_softc {
	struct virtqueue *sc_vqs;
	const struct virtio_ops *sc_ops;
	void *sc_child, *sc_soft_ih, *sc_dmat, *sc_dev;
};
struct virtio_pci_softc {
	struct virtio_softc sc_sc;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	int sc_ihs_num;
	bool sc_intr_pervq;
};
struct access {
	unsigned int width, reg;
	uint32_t value;
	bool read;
};
static struct access accesses[32];
static size_t naccess;
static unsigned int resets, drains, softdrains, freed, destroyed, diagnostics;
static unsigned int cases;
static bool stopped, drained;

static void
access_record(unsigned int width, unsigned int reg, uint32_t value, bool read)
{

	assert(naccess < sizeof(accesses) / sizeof(accesses[0]));
	accesses[naccess++] = (struct access){ width, reg, value, read };
}

static void
bus_space_write_2(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t reg, uint16_t value)
{

	assert(tag == 3 && handle == 5);
	access_record(2, reg, value, false);
}

static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t reg, uint32_t value)
{

	assert(tag == 3 && handle == 5);
	access_record(4, reg, value, false);
}

static uint16_t
bus_space_read_2(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t reg)
{

	assert(tag == 3 && handle == 5);
	assert(reg == VIRTIO_CONFIG1_QUEUE_NOTIFY_OFF);
	access_record(2, reg, 0x1357, true);
	return 0x1357;
}

static void
virtio_device_reset(struct virtio_softc *sc)
{

	assert(sc->sc_child != NULL && sc->sc_vqs != NULL);
	stopped = true;
	resets++;
}

static void
free_interrupts(struct virtio_softc *sc)
{

	assert(stopped && sc->sc_vqs != NULL);
	drained = true;
	drains++;
}

static void
softint_disestablish(void *ih)
{

	assert(ih != NULL && stopped && drained);
	softdrains++;
}

static const char *
device_xname(void *dev)
{

	(void)dev;
	return "virtio-test";
}

static uint16_t
virtio_rw16(struct virtio_softc *sc, uint16_t value)
{

	(void)sc;
	return value;
}

static void
vq_sync_aring_all(struct virtio_softc *sc, struct virtqueue *vq, int flags)
{

	assert(sc->sc_vqs == NULL && sc->sc_child == NULL);
	assert(stopped && drained && vq->vq_vaddr != NULL);
	assert(naccess >= 2 && flags == BUS_DMASYNC_POSTWRITE);
}

static void
kmem_free(void *p, size_t size)
{

	assert(stopped && drained && p != NULL && size != 0);
	freed++;
}

static void
bus_dmamap_unload(void *tag, void *map)
{

	(void)tag;
	assert(stopped && drained && map != NULL);
	freed++;
}

static void
bus_dmamap_destroy(void *tag, void *map)
{

	bus_dmamap_unload(tag, map);
}

static void
bus_dmamem_unmap(void *tag, void *addr, size_t size)
{

	(void)tag;
	assert(stopped && drained && addr != NULL && size != 0);
	freed++;
}

static void
bus_dmamem_free(void *tag, void *seg, int nseg)
{

	(void)tag;
	assert(stopped && drained && seg != NULL && nseg == 1);
	freed++;
}

static void
mutex_destroy(int *lock)
{

	assert(stopped && drained && *lock == 1);
	*lock = 0;
	destroyed++;
}

static int
queue_diagnostic(const char *fmt, ...)
{

	assert(strstr(fmt, "freeing non-empty vq") != NULL);
	diagnostics++;
	return 0;
}

#define printf queue_diagnostic
#include "queue-under-test.h"
#undef printf

static void
expect(size_t *i, unsigned int width, unsigned int reg, uint32_t value,
    bool read)
{
	struct access *a;

	assert(*i < naccess);
	a = &accesses[(*i)++];
	assert(a->width == width && a->reg == reg);
	assert(a->value == value && a->read == read);
}

static void
expect_addr(size_t *i, unsigned int reg, uint64_t value)
{

	expect(i, 4, reg, BUS_ADDR_LO32(value), false);
	expect(i, 4, reg + 4, BUS_ADDR_HI32(value), false);
}

static void
check_accesses(struct virtio_pci_softc *psc, uint16_t idx, uint64_t addr,
    bool modern)
{
	size_t i = 0;
	unsigned int vec = VIRTIO_MSIX_QUEUE_VECTOR_INDEX;

	if (modern) {
		expect(&i, 2, VIRTIO_CONFIG1_QUEUE_SELECT, idx, false);
		if (addr == 0)
			expect(&i, 2, VIRTIO_CONFIG1_QUEUE_ENABLE, 0, false);
		expect_addr(&i, VIRTIO_CONFIG1_QUEUE_DESC, addr);
		expect_addr(&i, VIRTIO_CONFIG1_QUEUE_AVAIL,
		    addr ? addr + 0x2468 : 0);
		expect_addr(&i, VIRTIO_CONFIG1_QUEUE_USED,
		    addr ? addr + 0x4680 : 0);
		if (addr != 0) {
			expect(&i, 2, VIRTIO_CONFIG1_QUEUE_ENABLE, 1, false);
			expect(&i, 2, VIRTIO_CONFIG1_QUEUE_NOTIFY_OFF, 0x1357, true);
		}
	} else {
		expect(&i, 2, VIRTIO_CONFIG_QUEUE_SELECT, idx, false);
		expect(&i, 4, VIRTIO_CONFIG_QUEUE_ADDRESS,
		    addr / VIRTIO_PAGE_SIZE, false);
	}
	if (psc->sc_ihs_num > 1) {
		if (psc->sc_intr_pervq)
			vec += idx;
		expect(&i, 2, modern ? VIRTIO_CONFIG1_QUEUE_MSIX_VECTOR :
		    VIRTIO_CONFIG_MSI_QUEUE_VECTOR, vec, false);
	}
	assert(i == naccess);
	cases++;
}

static void
run_case(bool modern, int ninterrupts, bool pervq, bool soft)
{
	struct virtio_ops ops = { modern ? virtio_pci_setup_queue_10 :
	    virtio_pci_setup_queue_09, free_interrupts };
	struct virtqueue queues[2] = { 0 }, empty = { 0 };
	struct virtio_pci_softc psc = { .sc_iot = 3, .sc_ioh = 5,
	    .sc_ihs_num = ninterrupts, .sc_intr_pervq = pervq };
	struct virtio_softc *sc = &psc.sc_sc;
	uint64_t addr = UINT64_C(0x123456780000);
	unsigned int i;

	resets = drains = softdrains = freed = destroyed = diagnostics = 0;
	stopped = drained = false;
	sc->sc_ops = &ops;
	sc->sc_vqs = queues;
	sc->sc_child = &psc;
	sc->sc_soft_ih = soft ? &psc : NULL;
	for (i = 0; i < 2; i++) {
		struct virtqueue *vq = &queues[i];
		vq->vq_index = i;
		vq->vq_num = 2;
		vq->vq_desc[0].next = 1;
		vq->vq_desc[1].next = VRING_DESC_CHAIN_END;
		vq->vq_vaddr = vq->vq_dmamap = vq;
		vq->vq_descx = (void *)vq;
		vq->vq_bytesize = 0x8000;
		vq->vq_availoffset = 0x2468;
		vq->vq_usedoffset = 0x4680;
		vq->vq_freedesc_lock = vq->vq_uring_lock = vq->vq_aring_lock = 1;
		naccess = 0;
		ops.setup_queue(sc, i, addr);
		check_accesses(&psc, i, addr, modern);
		assert(vq->vq_notify_off == (modern ? 0x1357 : 0));
	}

	/* Actual detach clears the registry before actual native ring free. */
	virtio_child_detach(sc);
	assert(sc->sc_vqs == NULL && sc->sc_child == NULL);
	assert(sc->sc_soft_ih == NULL);
	assert(resets == 1 && drains == 1 && softdrains == (unsigned int)soft);
	virtio_child_detach(sc);
	assert(resets == 1 && drains == 1);
	for (i = 0; i < 2; i++) {
		naccess = 0;
		assert(virtio_free_vq(sc, &queues[i]) == 0);
		check_accesses(&psc, i, 0, modern);
		assert(memcmp(&queues[i], &empty, sizeof(empty)) == 0);
		assert(freed == 5 * (i + 1) && destroyed == 3 * (i + 1));
		naccess = 0;
		assert(virtio_free_vq(sc, &queues[i]) == 0 && naccess == 0);
	}
	/* Allocation failure may also disable an unpublished queue. */
	naccess = 0;
	ops.setup_queue(sc, 7, 0);
	check_accesses(&psc, 7, 0, modern);

	/* Non-empty rings still fail without freeing DMA or writing registers. */
	queues[0].vq_vaddr = queues;
	queues[0].vq_num = 2;
	queues[0].vq_free_idx = VRING_DESC_CHAIN_END;
	naccess = 0;
	assert(virtio_free_vq(sc, &queues[0]) == EBUSY);
	assert(naccess == 0 && freed == 10 && destroyed == 6);
	assert(diagnostics == 1);
	cases++;
}

int
main(void)
{
	unsigned int modern, n, pervq, soft;

	for (modern = 0; modern < 2; modern++)
		for (n = 0; n < 4; n++)
			for (pervq = 0; pervq < 2; pervq++)
				for (soft = 0; soft < 2; soft++)
					run_case(modern, n, pervq, soft);
	printf("VirtIO PCI queue lifecycle: %u cases passed (assertions %d)\n",
	    cases, KERNEL_ASSERTIONS);
	return 0;
}
