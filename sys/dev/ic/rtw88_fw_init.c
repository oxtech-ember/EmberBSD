/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */
/*
 * One shared descriptor/packet buffer for synchronous packet-H2C commands.
 * The caller owns command/channel/stop operations and pins core lifetime.
 * Run in process context without the PCI spin lock. The busy flag only
 * rejects reentry; it does not provide concurrent lifetime protection.
 * A new firmware download resets the producer epoch. No command resets
 * hardware indices. Uncertain DMA ownership blocks reuse until a new epoch.
 * Hardware consumption is distinct from firmware semantic acceptance.
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
validate(struct rtw88_softc *sc)
{
	struct rtw88_dma *ring, *packet;
	int error;

	if (sc == NULL || sc->sc_ops == NULL ||
	    sc->sc_ops->read_2 == NULL || sc->sc_ops->read_4 == NULL ||
	    sc->sc_ops->write_2 == NULL || sc->sc_ops->dma_sync == NULL ||
	    sc->sc_ops->delay_us == NULL)
		return EINVAL;
	if (sc->sc_packet_h2c.busy || sc->sc_packet_h2c.dirty ||
	    sc->sc_packet_h2c.error)
		return EBUSY;
	if (!sc->sc_packet_h2c.active || !sc->sc_running ||
	    !sc->sc_powered || !sc->sc_fw_dma_owned)
		return ENXIO;
	if (canceled(sc, sc->sc_packet_h2c.generation))
		return EINTR;
	ring = &sc->sc_ring[7];
	packet = &sc->sc_fw_dma;
	if (packet->vaddr == NULL || packet->size < H2C_BYTES ||
	    ring->vaddr == NULL || ring->size < 3 * 16 ||
	    ring->size % 16 != 0 || ring->size / 16 > 0xfff ||
	    (packet->addr & 3) != 0 || (ring->addr & 15) != 0 ||
	    sc->sc_packet_h2c.wp >= ring->size / 16)
		return EINVAL;
	error = rtw88_dma_validate(packet->addr, H2C_BYTES, 32);
	if (error != 0)
		return error;
	error = rtw88_dma_validate(ring->addr, ring->size, 32);
	if (error != 0)
		return error;
	if (packet->addr < ring->addr + ring->size &&
	    ring->addr < packet->addr + H2C_BYTES)
		return EINVAL;
	return 0;
}

/* Validate the idle queue before changing either DMA mapping. */
static int
idle(struct rtw88_softc *sc, unsigned count)
{
	uint32_t desa, idx, wp = sc->sc_packet_h2c.wp;
	uint16_t num;
	int error;

	error = sc->sc_ops->read_2(sc->sc_bus, H2C_NUM, &num);
	if (error != 0)
		return error;
	if (canceled(sc, sc->sc_packet_h2c.generation))
		return EINTR;
	if ((num & 0xfff) != count)
		return EIO;
	error = sc->sc_ops->read_4(sc->sc_bus, H2C_DESA, &desa);
	if (error != 0)
		return error;
	if (canceled(sc, sc->sc_packet_h2c.generation))
		return EINTR;
	if (desa != sc->sc_ring[7].addr)
		return EIO;
	error = sc->sc_ops->read_4(sc->sc_bus, H2C_IDX, &idx);
	if (error != 0)
		return error;
	if (canceled(sc, sc->sc_packet_h2c.generation))
		return EINTR;
	return (idx & UINT32_C(0x0fff0fff)) == (wp | (wp << 16)) ? 0 : EBUSY;
}

static int
wait_consumed(struct rtw88_softc *sc, unsigned old, unsigned next,
    unsigned *waited)
{
	for (;;) {
		uint32_t idx;
		unsigned rp;
		int error;

		if (canceled(sc, sc->sc_packet_h2c.generation))
			return EINTR;
		error = sc->sc_ops->read_4(sc->sc_bus, H2C_IDX, &idx);
		if (error != 0)
			return error;
		if (canceled(sc, sc->sc_packet_h2c.generation))
			return EINTR;
		rp = (idx >> 16) & 0xfff;
		if ((idx & 0xfff) != next || (rp != old && rp != next))
			return EIO;
		if (rp == next)
			return 0;
		if (*waited == H2C_BUDGET_US)
			return ETIMEDOUT;
		sc->sc_ops->delay_us(sc->sc_bus, H2C_STEP_US);
		*waited += H2C_STEP_US;
	}
}

/* The entry points hold external ownership and have set busy. */
static int
send_packet(struct rtw88_softc *sc, const uint8_t command[32], unsigned *waited)
{
	struct rtw88_packet_h2c *state = &sc->sc_packet_h2c;
	struct rtw88_dma *packet = &sc->sc_fw_dma, *ring = &sc->sc_ring[7];
	uint8_t copy[32], *buf = packet->vaddr, *bd;
	unsigned count = (unsigned)(ring->size / 16), old = state->wp, next;
	int error;

	error = idle(sc, count);
	if (error != 0)
		goto uncertain;
	/* Take a copy before writing the shared buffer, including aliased input. */
	memcpy(copy, command, sizeof(copy));
	put16(copy + 6, state->sequence);
	memset(buf, 0, H2C_BYTES);
	/* Packet-H2C sets only TXPKTSIZE and QSEL, with no OFFSET, LS or sum. */
	put16(buf, 32);
	put32(buf + 4, 19U << 8);
	memcpy(buf + 48, copy, sizeof(copy));
	bd = (uint8_t *)ring->vaddr + old * 16;
	memset(bd, 0, 16);
	put16(bd, 48);
	put16(bd + 2, 1);
	put32(bd + 4, (uint32_t)packet->addr);
	put16(bd + 8, 32);
	put32(bd + 12, (uint32_t)packet->addr + 48);
	if (canceled(sc, state->generation))
		return EINTR;
	state->dirty = state->attempted = 1;
	sc->sc_ops->dma_sync(sc->sc_bus, packet, BUS_DMASYNC_PREWRITE);
	if (canceled(sc, state->generation)) {
		error = EINTR;
		goto uncertain;
	}
	sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_PREWRITE);
	if (canceled(sc, state->generation)) {
		error = EINTR;
		goto uncertain;
	}
	next = (old + 1) % count;
	/* Reserve before the doorbell: a failing write can already be accepted. */
	state->wp = (uint16_t)next;
	state->sequence++;
	error = sc->sc_ops->write_2(sc->sc_bus, H2C_IDX, (uint16_t)next);
	if (error != 0)
		goto uncertain;
	error = wait_consumed(sc, old, next, waited);
	if (error != 0)
		goto uncertain;
	sc->sc_ops->dma_sync(sc->sc_bus, packet, BUS_DMASYNC_POSTWRITE);
	if (canceled(sc, state->generation)) {
		error = EINTR;
		goto uncertain;
	}
	sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_POSTWRITE);
	if (canceled(sc, state->generation)) {
		error = EINTR;
		goto uncertain;
	}
	state->dirty = 0;
	return 0;
uncertain:
	/* A failed hardware snapshot also invalidates ownership trust. */
	state->dirty = 1;
	state->error = error;
	return error;
}

int
rtw88_fw_packet(struct rtw88_softc *sc, const uint8_t *packet, size_t len)
{
	unsigned total, waited = 0;
	int error;

	if (packet == NULL || len != 32)
		return EINVAL;
	error = validate(sc);
	if (error != 0)
		return error;
	total = packet[4] | (unsigned)packet[5] << 8;
	if (packet[0] != 1 || packet[1] != 0xff || total < 8 || total > 32)
		return EINVAL;
	if (!sc->sc_started || sc->sc_bootstrapping ||
	    !sc->sc_packet_h2c.initialized)
		return ENXIO;
	sc->sc_packet_h2c.busy = 1;
	error = send_packet(sc, packet, &waited);
	sc->sc_packet_h2c.busy = 0;
	return error;
}

int
rtw88_fw_init(struct rtw88_softc *sc)
{
	uint8_t packet[32] = { 1, 0xff, 0x0d, 0, 12, 0, 0, 0, [10] = 56 };
	unsigned waited = 0;
	int error;

	error = validate(sc);
	if (error != 0)
		return error;
	if (sc->sc_started || sc->sc_packet_h2c.attempted ||
	    sc->sc_packet_h2c.initialized)
		return EBUSY;
	if (!sc->sc_bootstrapping)
		return ENXIO;
	if (sc->sc_rfe > 6 || sc->sc_cut > 6)
		return ENOTSUP;
	sc->sc_packet_h2c.busy = 1;
	error = send_packet(sc, packet, &waited);
	if (error == 0) {
		memset(packet, 0, sizeof(packet));
		packet[0] = 1;
		packet[1] = 0xff;
		packet[2] = 0x11;
		packet[4] = 16;
		packet[8] = sc->sc_rfe;
		packet[9] = 2;
		packet[10] = sc->sc_cut;
		packet[11] = 0x33;
		error = send_packet(sc, packet, &waited);
	}
	if (error == 0)
		sc->sc_packet_h2c.initialized = 1;
	sc->sc_packet_h2c.busy = 0;
	return error;
}
