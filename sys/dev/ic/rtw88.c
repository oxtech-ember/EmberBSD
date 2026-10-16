/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8822CE resource lifecycle, AI-assisted. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>

int
rtw88_core_attach(struct rtw88_softc *sc)
{
	/* BK, BE, VI, VO, beacon, management, high, H2C, then MPDU RX. */
	static const size_t sizes[RTW88_NRING] = {
		128 * 16, 256 * 16, 128 * 16, 128 * 16,
		1 * 16, 128 * 16, 128 * 16, 128 * 16, 512 * 8
	};
	int error;

	if (sc->sc_nring != 0 || sc->sc_intr_established ||
	    sc->sc_resources_ready)
		return EBUSY;
	for (unsigned i = 0; i < RTW88_NRING; i++) {
		struct rtw88_dma *ring = &sc->sc_ring[i];

		error = sc->sc_ops->dma_alloc(sc->sc_bus, ring, sizes[i]);
		if (error != 0)
			goto fail;
		sc->sc_nring++;
		/* Validate the whole mapping again at the ownership boundary. */
		error = rtw88_dma_validate(ring->addr, ring->size,
		    RTW8822C_DMA_ADDR_BITS);
		if (error != 0)
			goto fail;
	}
	error = sc->sc_ops->intr_establish(sc->sc_bus);
	if (error != 0)
		goto fail;
	sc->sc_intr_established = 1;
	sc->sc_resources_ready = 1;
	return 0;
fail:
	rtw88_core_detach(sc);
	return error;
}

void
rtw88_core_detach(struct rtw88_softc *sc)
{
	sc->sc_resources_ready = 0;
	if (sc->sc_intr_established) {
		sc->sc_ops->intr_disestablish(sc->sc_bus);
		sc->sc_intr_established = 0;
	}
	while (sc->sc_nring != 0) {
		sc->sc_nring--;
		sc->sc_ops->dma_free(sc->sc_bus, &sc->sc_ring[sc->sc_nring]);
	}
}
