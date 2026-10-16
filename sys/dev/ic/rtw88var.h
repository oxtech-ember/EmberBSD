/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8822CE resource lifecycle, AI-assisted. */
#ifndef _DEV_IC_RTW88VAR_H_
#define _DEV_IC_RTW88VAR_H_

#include <dev/ic/rtw88_hw.h>

/* Queue order follows the pinned rtw88 PCI transport. */
#define RTW88_NTXRING 8
#define RTW88_RXRING 8
#define RTW88_NRING 9

struct rtw88_dma {
	void *cookie;		/* Bus-owned mapping; NULL until fully allocated. */
	void *vaddr;
	uint64_t addr;		/* Never narrow before validating the full range. */
	size_t size;
};

struct rtw88_bus_ops {
	/* On read failure, leave the caller's output unchanged. */
	int (*read_1)(void *, uint32_t, uint8_t *);
	int (*read_2)(void *, uint32_t, uint16_t *);
	int (*read_4)(void *, uint32_t, uint32_t *);
	int (*write_1)(void *, uint32_t, uint8_t);
	int (*write_2)(void *, uint32_t, uint16_t);
	int (*write_4)(void *, uint32_t, uint32_t);
	void (*delay_us)(void *, unsigned);
	/* Failure acquires nothing; success transfers one complete mapping. */
	int (*dma_alloc)(void *, struct rtw88_dma *, size_t);
	void (*dma_free)(void *, struct rtw88_dma *);
	void (*dma_sync)(void *, struct rtw88_dma *, int);
	int (*intr_establish)(void *);
	/* Mask and synchronously drain handlers before returning. */
	void (*intr_disestablish)(void *);
};

struct rtw88_softc {
	const struct rtw88_bus_ops *sc_ops;
	void *sc_bus;
	struct rtw88_dma sc_ring[RTW88_NRING];
	unsigned sc_nring;
	int sc_intr_established;
	int sc_resources_ready;
};

/* Attach only quiescent resources. No firmware, DMA publication or ifnet. */
int rtw88_core_attach(struct rtw88_softc *);
/* Caller serializes attach/detach; IRQ teardown precedes every DMA free. */
void rtw88_core_detach(struct rtw88_softc *);

#endif /* _DEV_IC_RTW88VAR_H_ */
