/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Native Zynq USB device engine. Register sequences follow AMD USBPS at
 * f8809afec39a7048597f34f39372169be20735cf (MIT); see docs/zynq-usb-device.md.
 * No vendor BSP, physical-pointer casts, or Linux APIs are used.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/endian.h>
#include <arm/xilinx/zynq_usb_devicevar.h>

static uint32_t
zread(struct zynqudc *sc, unsigned reg)
{
	return bus_space_read_4(sc->bst, sc->bsh, reg);
}

static void
zwrite(struct zynqudc *sc, unsigned reg, uint32_t value)
{
	bus_space_write_4(sc->bst, sc->bsh, reg, value);
	bus_space_barrier(sc->bst, sc->bsh, reg, 4, BUS_SPACE_BARRIER_WRITE);
}

static int
zwait(struct zynqudc *sc, unsigned reg, uint32_t bits)
{
	for (unsigned us = 0; us < ZUDC_POLL_US; us++) {
		if ((zread(sc, reg) & bits) == 0)
			return 0;
		delay(1);
	}
	return ETIMEDOUT;
}

static int
zslot(const struct zynqudc *sc, unsigned ep)
{
	if ((ep & ~0x8fU) != 0 || (ep & 15) >= sc->nep)
		return -1;
	return (ep & 15) * 2 + ((ep >> 7) & 1);
}

static uint32_t
zbit(unsigned slot)
{
	return 1U << ((slot / 2) + ((slot & 1) ? 16 : 0));
}

static void
zsync(struct zynqudc *sc, unsigned slot, int ops)
{
	bus_dmamap_sync(sc->dmat, sc->descriptors.map,
	    slot * sizeof(struct zudc_qh), sizeof(struct zudc_qh), ops);
	bus_dmamap_sync(sc->dmat, sc->descriptors.map,
	    2048 + slot * sizeof(struct zudc_td), sizeof(struct zudc_td), ops);
}

static void
zevent(struct zynqudc *sc, unsigned type)
{
	memset(&sc->event, 0, sizeof(sc->event));
	sc->event.type = type;
	sc->event.generation = sc->generation;
	sc->event.sequence = ++sc->sequence;
	sc->event.configured = sc->configuration;
	sc->event_pending = true;
}

int
zynqudc_role(const char *role)
{
	return role != NULL && strcmp(role, "peripheral") == 0;
}

static void
zqhinit(struct zynqudc *sc)
{
	memset(sc->descriptors.kva, 0, sc->descriptors.size);
	for (unsigned i = 0; i < sc->nep * 2; i++) {
		sc->qh[i].cap = htole32((64U << 16) | ZUDC_QH_ZLT |
		    (i == 0 ? ZUDC_QH_IOS : 0));
		sc->qh[i].next = htole32(ZUDC_TD_TERM);
	}
	bus_dmamap_sync(sc->dmat, sc->descriptors.map, 0,
	    sc->descriptors.size, BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
}

int
zynqudc_init(struct zynqudc *sc)
{
	uint64_t base = sc->descriptors.map->dm_segs[0].ds_addr;

	if (sc->nep < 3 || sc->nep > ZUDC_EP_COUNT ||
	    sc->descriptors.size < 3072 || (base & 2047) != 0)
		return EINVAL;
	if (base > 0xfffff000ULL)
		return EFBIG;
	for (unsigned i = 0; i < sc->nep * 2; i++) {
		uint64_t addr = sc->req[i].data.map->dm_segs[0].ds_addr;
		if (addr > 0xffffbfffULL ||
		    sc->req[i].data.size < USBPERIPH_MAX_XFER)
			return EFBIG;
	}
	zwrite(sc, ZUDC_INTR, 0);
	zwrite(sc, ZUDC_CMD, 0);
	zwrite(sc, ZUDC_CMD, ZUDC_RESET);
	if (zwait(sc, ZUDC_CMD, ZUDC_RESET) != 0)
		return ETIMEDOUT;
	/* Device mode and setup lockout disable. Never set host port power. */
	zwrite(sc, ZUDC_MODE, 0x0a);
	zwrite(sc, ZUDC_PORT, 0x80000000U); /* External ULPI, no VBUS drive. */
	zwrite(sc, ZUDC_OTG, ZUDC_OTG_TERMINATION);
	sc->generation = 1;
	sc->pending_address = 0xff;
	zqhinit(sc);
	zwrite(sc, ZUDC_LIST, (uint32_t)base);
	zwrite(sc, ZUDC_EPCTRL(0), 0x00800080);
	zwrite(sc, ZUDC_ADDR, 0);
	zwrite(sc, ZUDC_SETUP, 0xffff);
	zwrite(sc, ZUDC_COMPLETE, ~0U);
	zwrite(sc, ZUDC_STS, ~0U);
	return 0;
}

/* Validate every descriptor before the controller can expose it to a host. */
int
zynqudc_configure(struct zynqudc *sc, const struct usbperiph_config *c)
{
	uint32_t seen = 0;
	unsigned interfaces = 0, endpoints = 0, expected = 0, offset;

	if (sc->faulted)
		return EIO;
	if (sc->connected)
		return EBUSY;
	if (c->version != USBPERIPH_ABI || c->reserved != 0 ||
	    c->device[0] != 18 || c->device[1] != 1 ||
	    c->device[7] != 64 || c->device[17] != 1 ||
	    c->config_length < 9 || c->config_length > USBPERIPH_MAX_CONFIG ||
	    c->strings_length > USBPERIPH_MAX_STRINGS ||
	    c->config[0] != 9 || c->config[1] != 2 || c->config[5] != 1 ||
	    (unsigned)c->config[2] + ((unsigned)c->config[3] << 8) != c->config_length ||
	    (c->config[7] & 0xe0) != 0x80)
		return EINVAL;
	for (offset = 9; offset < c->config_length;) {
		const uint8_t *d = c->config + offset;
		unsigned n = d[0];
		if (n < 2 || n > c->config_length - offset)
			return EINVAL;
		if (d[1] == 4) {
			if (n != 9 || d[2] != interfaces || d[3] != 0 ||
			    endpoints != expected)
				return EINVAL;
			interfaces++;
			expected = d[4];
			endpoints = 0;
		} else if (d[1] == 5) {
			int slot;
			if (n != 7 || interfaces == 0 || d[3] != 2 ||
			    d[4] != 0 || d[5] != 2 || (d[2] & 15) == 0)
				return EINVAL;
			slot = zslot(sc, d[2]);
			if (slot < 0 || (seen & (1U << slot)) != 0)
				return EINVAL;
			seen |= 1U << slot;
			endpoints++;
		} else {
			return EINVAL;
		}
		offset += n;
	}
	if (interfaces != c->config[4] || endpoints != expected)
		return EINVAL;
	for (offset = 0; offset < c->strings_length;) {
		unsigned n = c->strings[offset];
		if (n < 2 || (n & 1) != 0 || n > c->strings_length - offset ||
		    c->strings[offset + 1] != 3)
			return EINVAL;
		offset += n;
	}
	sc->config = *c;
	sc->valid_config = true;
	return 0;
}

int
zynqudc_connect(struct zynqudc *sc)
{
	if (sc->faulted)
		return EIO;
	if (!sc->valid_config)
		return EINVAL;
	if (sc->connected)
		return EBUSY;
	/* A distinct lifecycle cannot consume pending work from the last one. */
	zwrite(sc, ZUDC_SETUP, 0xffff);
	zwrite(sc, ZUDC_COMPLETE, ~0U);
	zwrite(sc, ZUDC_STS, ~0U);
	sc->generation++;
	sc->pending_address = 0xff;
	sc->ep0_phase = 0;
	sc->suspended = false;
	sc->vendor_pending = false;
	zqhinit(sc);
	zwrite(sc, ZUDC_ADDR, 0);
	zwrite(sc, ZUDC_EPCTRL(0), 0x00800080);
	zwrite(sc, ZUDC_INTR, ZUDC_INT_MASK);
	zwrite(sc, ZUDC_CMD, ZUDC_RUN);
	sc->connected = true;
	return 0;
}

static int
zprime(struct zynqudc *sc, unsigned slot, uint32_t length)
{
	struct zudc_request *r = &sc->req[slot];
	struct zudc_td *td = &sc->td[slot];
	uint64_t addr = r->data.map->dm_segs[0].ds_addr;
	uint32_t bit = zbit(slot);

	if (sc->faulted)
		return EIO;
	if (!sc->connected)
		return ENXIO;
	if (r->active || r->retained)
		return EBUSY;
	/* This engine has one request per direction. No live chain is patched. */
	if ((zread(sc, ZUDC_PRIME) | zread(sc, ZUDC_STAT)) & bit)
		return EBUSY;
	memset(td, 0, sizeof(*td));
	td->next = htole32(ZUDC_TD_TERM);
	td->token = htole32((length << 16) | ZUDC_TD_IOC | ZUDC_TD_ACTIVE);
	td->page[0] = htole32((uint32_t)addr);
	for (unsigned p = 1; p < 5; p++)
		td->page[p] = htole32(((uint32_t)addr & ~4095U) + p * 4096);
	bus_dmamap_sync(sc->dmat, r->data.map, 0,
	    MAX(length, 1), (slot & 1) ? BUS_DMASYNC_PREWRITE : BUS_DMASYNC_PREREAD);
	/* Clear stale completion before installing a new descriptor generation. */
	zwrite(sc, ZUDC_COMPLETE, bit);
	sc->qh[slot].next = htole32((uint32_t)
	    sc->descriptors.map->dm_segs[0].ds_addr + 2048 + slot * 32);
	sc->qh[slot].token = 0;
	zsync(sc, slot, BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	r->generation = sc->generation;
	r->length = length;
	r->actual = 0;
	r->status = EINPROGRESS;
	r->active = true;
	zwrite(sc, ZUDC_PRIME, bit);
	/* A bounded prime wait also prevents teardown racing a pending prime. */
	if (zwait(sc, ZUDC_PRIME, bit) != 0) {
		(void)zynqudc_cancel(sc, (slot / 2) | ((slot & 1) ? 0x80 : 0),
		    ETIMEDOUT);
		return ETIMEDOUT;
	}
	return 0;
}

int
zynqudc_submit(struct zynqudc *sc, unsigned ep, uint32_t length, uint64_t gen)
{
	int slot = zslot(sc, ep);
	uint32_t ctrl;

	if (slot < 2 || length > USBPERIPH_MAX_XFER)
		return EINVAL;
	if (sc->faulted)
		return EIO;
	if (gen != 0 && gen != sc->generation)
		return ESTALE;
	if (!sc->connected || sc->configuration == 0)
		return ENXIO;
	if (sc->suspended)
		return EAGAIN;
	ctrl = zread(sc, ZUDC_EPCTRL(ep & 15)) >> ((ep & 0x80) ? 16 : 0);
	if ((ctrl & 0x80) == 0)
		return ENXIO;
	if (ctrl & 1)
		return EPIPE;
	return zprime(sc, (unsigned)slot, length);
}

int
zynqudc_cancel(struct zynqudc *sc, unsigned ep, int status)
{
	int slot = zslot(sc, ep);
	uint32_t bit;
	struct zudc_request *r;

	if (slot < 0)
		return EINVAL;
	r = &sc->req[slot];
	bit = zbit((unsigned)slot);
	/* Flush again if a pending prime becomes active during the first flush. */
	for (unsigned us = 0; us < ZUDC_POLL_US; us++) {
		zwrite(sc, ZUDC_FLUSH, bit);
		if (((zread(sc, ZUDC_FLUSH) | zread(sc, ZUDC_PRIME) |
		    zread(sc, ZUDC_STAT)) & bit) == 0) {
			if (r->active) {
				zsync(sc, (unsigned)slot,
				    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
				bus_dmamap_sync(sc->dmat, r->data.map, 0,
				    MAX(r->length, 1), (slot & 1) ?
				    BUS_DMASYNC_POSTWRITE : BUS_DMASYNC_POSTREAD);
			}
			r->active = false;
			r->status = status;
			zwrite(sc, ZUDC_COMPLETE, bit);
			sc->qh[slot].next = htole32(ZUDC_TD_TERM);
			sc->qh[slot].token = 0;
			zsync(sc, (unsigned)slot,
			    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
			return 0;
		}
		delay(1);
	}
	r->retained = r->active;
	sc->faulted = true;
	sc->connected = false;
	sc->vendor_pending = false;
	sc->generation++;
	zwrite(sc, ZUDC_INTR, 0);
	zwrite(sc, ZUDC_CMD, 0);
	return ETIMEDOUT;
}

static int
zcancelall(struct zynqudc *sc, int status)
{
	int error = 0;
	for (unsigned i = 0; i < sc->nep * 2; i++) {
		if (zynqudc_cancel(sc, (i / 2) | ((i & 1) ? 0x80 : 0), status) != 0)
			error = ETIMEDOUT;
	}
	return error;
}

int
zynqudc_disconnect(struct zynqudc *sc)
{
	int error;
	zwrite(sc, ZUDC_CMD, 0);
	zwrite(sc, ZUDC_INTR, 0);
	sc->connected = false;
	sc->configuration = 0;
	sc->vendor_pending = false;
	sc->ep0_phase = 0;
	sc->pending_address = 0xff;
	sc->generation++;
	error = zcancelall(sc, ENXIO);
	/* A queued soft interrupt must see no old setup or completion work. */
	zwrite(sc, ZUDC_SETUP, 0xffff);
	zwrite(sc, ZUDC_COMPLETE, ~0U);
	zwrite(sc, ZUDC_STS, ~0U);
	if (error == 0) {
		for (unsigned ep = 1; ep < sc->nep; ep++)
			zwrite(sc, ZUDC_EPCTRL(ep), 0);
	}
	zevent(sc, USBPERIPH_EVENT_DISCONNECT);
	return error;
}

static void
zstall(struct zynqudc *sc)
{
	sc->vendor_pending = false;
	sc->ep0_phase = 0;
	sc->pending_address = 0xff;
	zwrite(sc, ZUDC_EPCTRL(0), zread(sc, ZUDC_EPCTRL(0)) | 0x10001);
}

static int
zcontrol(struct zynqudc *sc, const void *data, unsigned length, unsigned wanted)
{
	if (length > wanted)
		length = wanted;
	if (length > USBPERIPH_MAX_CONTROL)
		return EINVAL;
	if (length != 0)
		memcpy(sc->req[1].data.kva, data, length);
	/* Enable automatic ZLP for a data stage that ends at a packet boundary. */
	sc->qh[1].cap = htole32((64U << 16) |
	    (length == wanted ? ZUDC_QH_ZLT : 0));
	sc->ep0_phase = wanted != 0 ? 1 : 2;
	return zprime(sc, 1, length);
}

static int
zsetconfig(struct zynqudc *sc, unsigned value)
{
	if (value > 1 || !sc->valid_config)
		return EINVAL;
	for (unsigned i = 2; i < sc->nep * 2; i++) {
		if (zynqudc_cancel(sc, (i / 2) | ((i & 1) ? 0x80 : 0),
		    ECANCELED) != 0)
			return ETIMEDOUT;
	}
	for (unsigned ep = 1; ep < sc->nep; ep++)
		zwrite(sc, ZUDC_EPCTRL(ep), 0);
	sc->generation++;
	sc->configuration = value;
	if (value != 0) {
		for (unsigned off = 9; off < sc->config.config_length;) {
			const uint8_t *d = sc->config.config + off;
			if (d[1] == 5) {
				unsigned slot = (unsigned)zslot(sc, d[2]);
				unsigned shift = (d[2] & 0x80) ? 16 : 0;
				unsigned mps = (zread(sc, ZUDC_PORT) & ZUDC_PORT_HS) ? 512 : 64;
				sc->qh[slot].cap = htole32((mps << 16) | ZUDC_QH_ZLT);
				zsync(sc, slot, BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
				zwrite(sc, ZUDC_EPCTRL(d[2] & 15),
				    zread(sc, ZUDC_EPCTRL(d[2] & 15)) | (0xc8U << shift));
			}
			off += d[0];
		}
	}
	zevent(sc, USBPERIPH_EVENT_CONFIGURE);
	return 0;
}

static int
zdescriptor(struct zynqudc *sc, unsigned value, unsigned index, unsigned wanted)
{
	unsigned n = 0, type = value >> 8, number = value & 255;

	if (type == 1 && number == 0 && index == 0)
		return zcontrol(sc, sc->config.device, 18, wanted);
	if ((type == 2 || type == 7) && number == 0 && index == 0) {
		uint8_t buffer[USBPERIPH_MAX_CONFIG];
		n = sc->config.config_length;
		memcpy(buffer, sc->config.config, n);
		buffer[1] = type;
		bool hs = (zread(sc, ZUDC_PORT) & ZUDC_PORT_HS) != 0;
		if (type == 7)
			hs = !hs;
		for (unsigned off = 9; off < n; off += buffer[off]) {
			if (buffer[off + 1] == 5) {
				buffer[off + 4] = hs ? 0 : 64;
				buffer[off + 5] = hs ? 2 : 0;
			}
		}
		return zcontrol(sc, buffer, n, wanted);
	}
	if (type == 6 && number == 0 && index == 0) {
		const uint8_t qualifier[10] = {10, 6, 0, 2, 0, 0, 0, 64, 1, 0};
		return zcontrol(sc, qualifier, 10, wanted);
	}
	if (type == 3 && (index == 0 || index == 0x409)) {
		for (unsigned off = 0; off < sc->config.strings_length;
		    off += sc->config.strings[off]) {
			if (n++ == number)
				return zcontrol(sc, sc->config.strings + off,
				    sc->config.strings[off], wanted);
		}
	}
	return EINVAL;
}

static void
zsetup(struct zynqudc *sc)
{
	uint8_t setup[8], status[2] = {0, 0};
	unsigned value, index, length;
	int error = EINVAL;
	bool stable = false;

	for (unsigned us = 0; us < ZUDC_POLL_US; us++) {
		zwrite(sc, ZUDC_CMD, zread(sc, ZUDC_CMD) | ZUDC_SUTW);
		bus_dmamap_sync(sc->dmat, sc->descriptors.map, 0, 64,
		    BUS_DMASYNC_POSTREAD);
		memcpy(setup, sc->qh[0].setup, 8);
		if (zread(sc, ZUDC_CMD) & ZUDC_SUTW) {
			stable = true;
			break;
		}
		delay(1);
	}
	zwrite(sc, ZUDC_SETUP, 1);
	zwrite(sc, ZUDC_CMD, zread(sc, ZUDC_CMD) & ~ZUDC_SUTW);
	if (!stable || zwait(sc, ZUDC_SETUP, 1) != 0 ||
	    zynqudc_cancel(sc, 0, ECANCELED) != 0 ||
	    zynqudc_cancel(sc, 0x80, ECANCELED) != 0) {
		zstall(sc);
		return;
	}
	zwrite(sc, ZUDC_EPCTRL(0), 0x00800080);
	sc->pending_address = 0xff;
	sc->vendor_pending = false;
	value = setup[2] | (setup[3] << 8);
	index = setup[4] | (setup[5] << 8);
	length = setup[6] | (setup[7] << 8);
	if (setup[0] == 0x80 && setup[1] == 6) {
		error = zdescriptor(sc, value, index, length);
	} else if (setup[0] == 0 && setup[1] == 5 && value < 128 &&
	    index == 0 && length == 0 && sc->configuration == 0) {
		sc->pending_address = value;
		error = zcontrol(sc, NULL, 0, 0);
	} else if (setup[0] == 0 && setup[1] == 9 && index == 0 && length == 0) {
		error = zsetconfig(sc, value);
		if (error == 0)
			error = zcontrol(sc, NULL, 0, 0);
	} else if (setup[0] == 0x80 && setup[1] == 8 && value == 0 &&
	    index == 0 && length == 1) {
		status[0] = sc->configuration;
		error = zcontrol(sc, status, 1, 1);
	} else if ((setup[0] == 0x80 || setup[0] == 0x81 || setup[0] == 0x82) &&
	    setup[1] == 0 && value == 0 && length == 2) {
		if (setup[0] == 0x80) {
			if (index != 0)
				goto bad;
		} else if (setup[0] == 0x81) {
			if (sc->configuration == 0 || index >= sc->config.config[4])
				goto bad;
		} else {
			int slot = zslot(sc, index);
			unsigned shift = (index & 0x80) ? 16 : 0;
			if (slot < 0 || (slot >= 2 && sc->configuration == 0))
				goto bad;
			uint32_t ctrl = zread(sc, ZUDC_EPCTRL(index & 15)) >> shift;
			if ((ctrl & 0x80) == 0)
				goto bad;
			status[0] = ctrl & 1;
		}
		error = zcontrol(sc, status, 2, 2);
	} else if (setup[0] == 0x02 && (setup[1] == 1 || setup[1] == 3) &&
	    value == 0 && length == 0 && sc->configuration != 0) {
		int slot = zslot(sc, index);
		if (slot < 2)
			goto bad;
		unsigned shift = (index & 0x80) ? 16 : 0;
		uint32_t ctrl = zread(sc, ZUDC_EPCTRL(index & 15));
		if ((ctrl & (0x80U << shift)) == 0)
			goto bad;
		if (zynqudc_cancel(sc, index, EPIPE) != 0)
			goto bad;
		ctrl = setup[1] == 3 ? ctrl | (1U << shift) :
		    (ctrl & ~(1U << shift)) | (0x40U << shift);
		zwrite(sc, ZUDC_EPCTRL(index & 15), ctrl);
		error = zcontrol(sc, NULL, 0, 0);
	} else if (setup[0] == 0x81 && setup[1] == 10 && value == 0 &&
	    index < sc->config.config[4] && length == 1 && sc->configuration != 0) {
		error = zcontrol(sc, status, 1, 1);
	} else if (setup[0] == 1 && setup[1] == 11 && value == 0 &&
	    index < sc->config.config[4] && length == 0 && sc->configuration != 0) {
		error = zcontrol(sc, NULL, 0, 0);
	} else if (setup[0] == 0x41 && setup[1] <= 2 && length == 0 &&
	    index < sc->config.config[4] && sc->configuration != 0) {
		zevent(sc, USBPERIPH_EVENT_SETUP);
		memcpy(sc->event.setup, setup, 8);
		sc->vendor_pending = true;
		sc->vendor_ticks = 0;
		return;
	}
 bad:
	if (error != 0)
		zstall(sc);
}

int
zynqudc_reply(struct zynqudc *sc, const struct usbperiph_reply *r)
{
	if (sc->faulted)
		return EIO;
	if (!sc->connected)
		return ENXIO;
	if (!sc->vendor_pending || r->generation != sc->generation ||
	    r->sequence != sc->event.sequence)
		return ESTALE;
	if (r->length != 0 || r->stall > 1)
		return EINVAL;
	sc->vendor_pending = false;
	if (r->stall != 0) {
		zstall(sc);
		return 0;
	}
	return zcontrol(sc, NULL, 0, 0);
}

void
zynqudc_tick(struct zynqudc *sc)
{
	if (sc->connected && !sc->faulted && sc->vendor_pending &&
	    ++sc->vendor_ticks >= 2)
		zstall(sc);
}

int
zynqudc_intr(struct zynqudc *sc)
{
	uint32_t status = zread(sc, ZUDC_STS) & ZUDC_INT_MASK;
	if (status == 0)
		return 0;
	zwrite(sc, ZUDC_STS, status);
	if (!sc->connected || sc->faulted) {
		zwrite(sc, ZUDC_SETUP, 0xffff);
		zwrite(sc, ZUDC_COMPLETE, ~0U);
		return 1;
	}
	if (status & ZUDC_INT_URI) {
		sc->configuration = sc->address = 0;
		sc->pending_address = 0xff;
		sc->ep0_phase = 0;
		sc->vendor_pending = sc->suspended = false;
		sc->generation++;
		if (zcancelall(sc, ECONNRESET) == 0) {
			zqhinit(sc);
			for (unsigned ep = 1; ep < sc->nep; ep++)
				zwrite(sc, ZUDC_EPCTRL(ep), 0);
			zwrite(sc, ZUDC_ADDR, 0);
			zwrite(sc, ZUDC_EPCTRL(0), 0x00800080);
		}
		zevent(sc, USBPERIPH_EVENT_RESET);
		return 1;
	}
	if (status & ZUDC_INT_PCI) {
		if ((zread(sc, ZUDC_PORT) & ZUDC_PORT_CCS) == 0) {
			(void)zynqudc_disconnect(sc);
			return 1;
		}
		sc->suspended = false;
	}
	if (status & ZUDC_INT_SLI) {
		sc->suspended = true;
		zevent(sc, USBPERIPH_EVENT_SUSPEND);
	}
	/* Setup wins over old completions. It flushes and retires old EP0 work. */
	if (zread(sc, ZUDC_SETUP) & 1)
		zsetup(sc);
	uint32_t complete = zread(sc, ZUDC_COMPLETE);
	zwrite(sc, ZUDC_COMPLETE, complete);
	for (unsigned i = 0; i < sc->nep * 2; i++) {
		struct zudc_request *r = &sc->req[i];
		if (!r->active || r->retained || (complete & zbit(i)) == 0)
			continue;
		zsync(sc, i, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		uint32_t token = le32toh(sc->td[i].token);
		if ((token & ZUDC_TD_ACTIVE) != 0)
			continue;
		unsigned residual = (token >> 16) & 0x7fff;
		if (residual > r->length || (token & ZUDC_TD_ERRORS) != 0) {
			(void)zynqudc_cancel(sc, (i / 2) | ((i & 1) ? 0x80 : 0), EIO);
			continue;
		}
		/* A short OUT can leave endpoint status asserted. Prove its drain. */
		if (zread(sc, ZUDC_STAT) & zbit(i)) {
			if (zynqudc_cancel(sc, (i / 2) | ((i & 1) ? 0x80 : 0), 0) != 0)
				continue;
		} else {
			bus_dmamap_sync(sc->dmat, r->data.map, 0, MAX(r->length, 1),
			    (i & 1) ? BUS_DMASYNC_POSTWRITE : BUS_DMASYNC_POSTREAD);
			r->active = false;
		}
		r->actual = r->length - residual;
		r->status = r->generation == sc->generation ? 0 : ESTALE;
		if (i == 1 && sc->ep0_phase == 1) {
			sc->ep0_phase = 3;
			if (zprime(sc, 0, 0) != 0)
				zstall(sc);
		} else if (i == 1 && sc->ep0_phase == 2) {
			sc->ep0_phase = 0;
			if (sc->pending_address != 0xff) {
				sc->address = sc->pending_address;
				sc->pending_address = 0xff;
				zwrite(sc, ZUDC_ADDR, (uint32_t)sc->address << 25);
			}
		} else if (i == 0 && sc->ep0_phase == 3) {
			sc->ep0_phase = 0;
		}
	}
	return 1;
}
