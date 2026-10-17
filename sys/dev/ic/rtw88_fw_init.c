/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */
/*
 * The two mandatory 3081 packet-H2C initialization commands. Call once
 * after HCI start, before sc_started is published, after firmware DDMA
 * has completed. The caller serializes startup, stop and ring7 ownership.
 * hci_setup publishes ring7 NUM=size/16 (currently 128), then resets WP/RP.
 * This function requires that fresh state and uses slots0/1 only.
 *
 * sc_fw_dma supplies 48 descriptor bytes and a 32-byte command. Reuse
 * follows observed hardware consumption. Completion proves DMA ownership,
 * not firmware semantic acceptance. The caller MUST stop DMA on any error
 * before reusing/freeing either map. This helper never releases mappings.
 * The total readiness delay for both packets is at most one second.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#ifdef _KERNEL
#include <sys/bus.h>
#endif
#include <dev/ic/rtw88var.h>

#define H2C_DESA 0x1320U
#define H2C_NUM 0x1328U
#define H2C_IDX 0x132cU
#define H2C_BYTES 80U
#define H2C_STEP_US 50U
#define H2C_BUDGET_US 1000000U

static void
put16(uint8_t *p, uint16_t value)
{

	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void
put32(uint8_t *p, uint32_t value)
{

	put16(p, (uint16_t)value);
	put16(p + 2, (uint16_t)(value >> 16));
}

static int
canceled(const struct rtw88_softc *sc, uint32_t generation)
{

	return sc->sc_stop_requested || sc->sc_generation != generation;
}

static int
wait_consumed(struct rtw88_softc *sc, uint32_t generation, unsigned wp,
    unsigned *waited)
{
	for (;;) {
		uint32_t idx;
		unsigned rp;
		int error;

		if (canceled(sc, generation))
			return EINTR;
		error = sc->sc_ops->read_4(sc->sc_bus, H2C_IDX, &idx);
		if (error != 0)
			return error;
		if (canceled(sc, generation))
			return EINTR;
		rp = (idx >> 16) & 0xfff;
		if ((idx & 0xfff) != wp || (rp != wp - 1 && rp != wp))
			return EIO;
		if (rp == wp)
			return 0;
		if (*waited == H2C_BUDGET_US)
			return ETIMEDOUT;
		sc->sc_ops->delay_us(sc->sc_bus, H2C_STEP_US);
		*waited += H2C_STEP_US;
	}
}

int
rtw88_fw_init(struct rtw88_softc *sc)
{
	const struct rtw88_tx_info tx = { .packet_size = 32, .queue = 19 };
	struct rtw88_dma *ring, *packet;
	uint8_t *buf;
	uint32_t generation, desa, idx;
	uint16_t num;
	unsigned count, sequence, waited = 0;
	int error;

	if (sc == NULL || sc->sc_ops == NULL ||
	    sc->sc_ops->read_2 == NULL || sc->sc_ops->read_4 == NULL ||
	    sc->sc_ops->write_2 == NULL || sc->sc_ops->dma_sync == NULL ||
	    sc->sc_ops->delay_us == NULL)
		return EINVAL;
	if (sc->sc_started)
		return EBUSY;
	if (!sc->sc_bootstrapping || !sc->sc_running || !sc->sc_fw_dma_owned)
		return ENXIO;
	if (sc->sc_rfe > 6 || sc->sc_cut > 6)
		return ENOTSUP;
	generation = sc->sc_generation;
	if (canceled(sc, generation))
		return EINTR;
	ring = &sc->sc_ring[7];
	packet = &sc->sc_fw_dma;
	if (packet->vaddr == NULL || packet->size < H2C_BYTES ||
	    ring->vaddr == NULL || ring->size < 3 * 16 ||
	    ring->size % 16 != 0 || ring->size / 16 > 0xfff ||
	    (packet->addr & 3) != 0 || (ring->addr & 15) != 0)
		return EINVAL;
	error = rtw88_dma_validate(packet->addr, H2C_BYTES, 32);
	if (error != 0)
		return error;
	error = rtw88_dma_validate(ring->addr, ring->size, 32);
	if (error != 0)
		return error;
	/* Both validated extents fit below 2^32, so these sums cannot wrap. */
	if (packet->addr < ring->addr + ring->size &&
	    ring->addr < packet->addr + H2C_BYTES)
		return EINVAL;
	count = (unsigned)(ring->size / 16);
	error = sc->sc_ops->read_2(sc->sc_bus, H2C_NUM, &num);
	if (error != 0)
		return error;
	if (canceled(sc, generation))
		return EINTR;
	if ((num & 0xfff) != count)
		return EIO;
	error = sc->sc_ops->read_4(sc->sc_bus, H2C_DESA, &desa);
	if (error != 0)
		return error;
	if (canceled(sc, generation))
		return EINTR;
	if (desa != ring->addr)
		return EIO;
	error = sc->sc_ops->read_4(sc->sc_bus, H2C_IDX, &idx);
	if (error != 0)
		return error;
	if (canceled(sc, generation))
		return EINTR;
	if ((idx & UINT32_C(0x0fff0fff)) != 0)
		return EBUSY;
	buf = packet->vaddr;
	for (sequence = 0; sequence < 2; sequence++) {
		uint8_t *h2c, *bd;

		if (canceled(sc, generation))
			return EINTR;
		memset(buf, 0, H2C_BYTES);
		error = rtw88_tx_encode(buf, 48, &tx);
		if (error != 0)
			return error;
		h2c = buf + 48;
		h2c[0] = 1;
		h2c[1] = 0xff;
		h2c[2] = sequence == 0 ? 0x0d : 0x11;
		put16(h2c + 4, sequence == 0 ? 12 : 16);
		put16(h2c + 6, (uint16_t)sequence);
		if (sequence == 0) {
			/* Native MAC FIFO layout: 16+24+8+8 reserved pages. */
			h2c[10] = 56;
		} else {
			h2c[8] = sc->sc_rfe;
			h2c[9] = 2; /* FW_RF_2T2R */
			h2c[10] = sc->sc_cut;
			h2c[11] = 0x33; /* RX/TX antenna paths AB. */
		}
		bd = (uint8_t *)ring->vaddr + sequence * 16;
		memset(bd, 0, 16);
		put16(bd, 48); /* rtw_pci_tx_buffer_desc.buf_size */
		put16(bd + 2, 1); /* ceil((48+32)/128); BCN-only OWN stays off. */
		put32(bd + 4, (uint32_t)packet->addr);
		put16(bd + 8, 32);
		put32(bd + 12, (uint32_t)packet->addr + 48);
		sc->sc_ops->dma_sync(sc->sc_bus, packet, BUS_DMASYNC_PREWRITE);
		sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_PREWRITE);
		if (canceled(sc, generation))
			return EINTR;
		error = sc->sc_ops->write_2(sc->sc_bus, H2C_IDX,
		    (uint16_t)(sequence + 1));
		if (error != 0)
			return error;
		error = wait_consumed(sc, generation, sequence + 1, &waited);
		if (error != 0)
			return error;
		sc->sc_ops->dma_sync(sc->sc_bus, packet, BUS_DMASYNC_POSTWRITE);
		sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_POSTWRITE);
	}
	return canceled(sc, generation) ? EINTR : 0;
}
