/*	$NetBSD: bcm2835_mbox.c,v 1.17 2021/08/07 16:18:43 thorpej Exp $	*/

/* Origin: EmberBSD bounded mailbox transactions, 2026-10-08. */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nick Hudson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: bcm2835_mbox.c,v 1.17 2021/08/07 16:18:43 thorpej Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/timetc.h>
#include <sys/bus.h>
#include <sys/mutex.h>

#include <arm/broadcom/bcm2835_mbox.h>
#include <arm/broadcom/bcm2835_mboxreg.h>
#include <arm/broadcom/bcm2835reg.h>
#include <arm/broadcom/bcm2835var.h>

static struct bcm2835mbox_softc *bcm2835mbox_sc;

#define	BCMMBOX_TIMEOUT_SEC	1
#define	BCMMBOX_POLL_DELAY_US	10
#define	BCMMBOX_MAX_WAITS	100000
#define	BCMMBOX_RX_BATCH		16

struct bcmmbox_deadline {
	struct bintime end;
	u_int waits;
};

static void
bcmmbox_deadline_init(struct bcmmbox_deadline *deadline)
{

	binuptime(&deadline->end);
	deadline->end.sec += BCMMBOX_TIMEOUT_SEC;
	deadline->waits = 0;
}

static int
bcmmbox_timeleft(struct bcmmbox_deadline *deadline, struct bintime *left)
{
	struct bintime now;

	binuptime(&now);
	if (bintimecmp(&now, &deadline->end, >=) ||
	    deadline->waits >= BCMMBOX_MAX_WAITS)
		return ETIMEDOUT;
	*left = deadline->end;
	bintime_sub(left, &now);
	return 0;
}

static int
bcmmbox_intr1(struct bcm2835mbox_softc *sc, int cv)
{
	uint32_t mbox, chan, data;
	int ret = 0;

	KASSERT(mutex_owned(&sc->sc_intr_lock));

	bus_space_barrier(sc->sc_iot, sc->sc_ioh, 0, BCM2835_MBOX_SIZE,
	    BUS_SPACE_BARRIER_READ);

	/* A continuously replenished FIFO must not defeat a deadline. */
	for (u_int i = 0; i < BCMMBOX_RX_BATCH; i++) {
		if (bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    BCM2835_MBOX0_STATUS) & BCM2835_MBOX_STATUS_EMPTY)
			break;

		mbox = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    BCM2835_MBOX0_READ);
		chan = BCM2835_MBOX_CHAN(mbox);
		data = BCM2835_MBOX_DATA(mbox);
		ret = 1;

		if (sc->sc_quarantined[chan])
			continue;
		if (BCM2835_MBOX_CHAN(sc->sc_mbox[chan]) != 0)
			sc->sc_overflow[chan] = true;
		else
			sc->sc_mbox[chan] = data | BCM2835_MBOX_CHANMASK;

		if (cv)
			cv_broadcast(&sc->sc_chan[chan]);
	}

	return ret;
}

void
bcmmbox_attach(struct bcm2835mbox_softc *sc)
{
	struct bcmmbox_attach_args baa;
	int i;

	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->sc_intr_lock, MUTEX_DEFAULT, IPL_VM);
	for (i = 0; i < BCM2835_MBOX_NUMCHANNELS; ++i) {
		mutex_init(&sc->sc_chan_lock[i], MUTEX_DEFAULT, IPL_NONE);
		cv_init(&sc->sc_chan[i], "bcmmbox");
	}

	if (bcm2835mbox_sc == NULL)
		bcm2835mbox_sc = sc;

	/* enable mbox interrupt */
	if (sc->sc_intrh != NULL)
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, BCM2835_MBOX_CFG,
		    BCM2835_MBOX_CFG_DATAIRQEN);

	baa.baa_dmat = sc->sc_dmat;
	sc->sc_platdev = config_found(sc->sc_dev, &baa, NULL, CFARGS_NONE);
}

int
bcmmbox_intr(void *cookie)
{
	struct bcm2835mbox_softc *sc = cookie;
	int ret;

	mutex_enter(&sc->sc_intr_lock);
	ret = bcmmbox_intr1(sc, 1);
	mutex_exit(&sc->sc_intr_lock);

	return ret;
}

static void
bcmmbox_quarantine(struct bcm2835mbox_softc *sc, uint8_t chan)
{

	KASSERT(mutex_owned(&sc->sc_chan_lock[chan]));
	mutex_enter(&sc->sc_intr_lock);
	sc->sc_quarantined[chan] = true;
	sc->sc_mbox[chan] = 0;
	sc->sc_overflow[chan] = false;
	mutex_exit(&sc->sc_intr_lock);
}

static int
bcmmbox_read_locked(struct bcm2835mbox_softc *sc, uint8_t chan,
    uint32_t *data, struct bcmmbox_deadline *deadline)
{
	const struct bintime epsilon = { .sec = 0, .frac = 0 };
	struct bintime left;
	int error;

	KASSERT(mutex_owned(&sc->sc_chan_lock[chan]));
	mutex_enter(&sc->sc_intr_lock);
	for (;;) {
		error = bcmmbox_timeleft(deadline, &left);
		if (error)
			break;
		if (cold || sc->sc_intrh == NULL)
			bcmmbox_intr1(sc, !cold);
		if (sc->sc_quarantined[chan] || sc->sc_overflow[chan]) {
			error = EIO;
			break;
		}
		if (BCM2835_MBOX_CHAN(sc->sc_mbox[chan]) != 0) {
			error = bcmmbox_timeleft(deadline, &left);
			if (error == 0) {
				*data = BCM2835_MBOX_DATA(sc->sc_mbox[chan]);
				sc->sc_mbox[chan] = 0;
			}
			break;
		}
		deadline->waits++;
		if (cold || sc->sc_intrh == NULL) {
			mutex_exit(&sc->sc_intr_lock);
			delay(BCMMBOX_POLL_DELAY_US);
			mutex_enter(&sc->sc_intr_lock);
		} else {
			/* Recompute from the same deadline after every wakeup. */
			error = cv_timedwaitbt(&sc->sc_chan[chan],
			    &sc->sc_intr_lock, &left, &epsilon);
			if (error != 0 && error != EWOULDBLOCK)
				break;
		}
	}
	mutex_exit(&sc->sc_intr_lock);
	return error;
}

int
bcmmbox_read(uint8_t chan, uint32_t *data)
{
	struct bcm2835mbox_softc *sc = bcm2835mbox_sc;
	struct bcmmbox_deadline deadline;
	int error;

	if (sc == NULL)
		return ENXIO;
	if (chan >= BCM2835_MBOX_NUMCHANNELS || data == NULL)
		return EINVAL;

	mutex_enter(&sc->sc_chan_lock[chan]);
	if (sc->sc_quarantined[chan]) {
		error = EIO;
	} else {
		bcmmbox_deadline_init(&deadline);
		error = bcmmbox_read_locked(sc, chan, data, &deadline);
		if (error)
			bcmmbox_quarantine(sc, chan);
	}
	mutex_exit(&sc->sc_chan_lock[chan]);
	return error;
}

static int
bcmmbox_write_locked(struct bcm2835mbox_softc *sc, uint8_t chan,
    uint32_t data, struct bcmmbox_deadline *deadline)
{
	struct bintime left;
	int error;

	KASSERT(mutex_owned(&sc->sc_chan_lock[chan]));
	for (;;) {
		mutex_enter(&sc->sc_lock);
		error = bcmmbox_timeleft(deadline, &left);
		if (error == 0)
			error = bcm2835_mbox_trywrite(sc->sc_iot, sc->sc_ioh,
			    chan, data);
		mutex_exit(&sc->sc_lock);
		if (error != EAGAIN)
			return error;
		deadline->waits++;
		delay(BCMMBOX_POLL_DELAY_US);
	}
}

void
bcmmbox_write(uint8_t chan, uint32_t data)
{
	struct bcm2835mbox_softc *sc = bcm2835mbox_sc;
	int error;

	KASSERT(sc != NULL);
	KASSERT(chan < BCM2835_MBOX_NUMCHANNELS);
	KASSERT(BCM2835_MBOX_CHAN(data) == 0);

	mutex_enter(&sc->sc_chan_lock[chan]);
	if (sc->sc_quarantined[chan]) {
		device_printf(sc->sc_dev, "channel %u quarantined\n", chan);
		mutex_exit(&sc->sc_chan_lock[chan]);
		return;
	}
	/* Preserve the legacy void API, but never monopolize the TX lock. */
	do {
		mutex_enter(&sc->sc_lock);
		error = bcm2835_mbox_trywrite(sc->sc_iot, sc->sc_ioh,
		    chan, data);
		mutex_exit(&sc->sc_lock);
		if (error == EAGAIN)
			delay(BCMMBOX_POLL_DELAY_US);
	} while (error == EAGAIN);
	mutex_exit(&sc->sc_chan_lock[chan]);
}

int
bcmmbox_request(uint8_t chan, void *buf, size_t buflen, uint32_t *pres)
{
	struct bcm2835mbox_softc *sc = bcm2835mbox_sc;
	struct bcmmbox_deadline deadline;
	void *dma_buf;
	bus_dmamap_t map;
	bus_dma_segment_t segs[1];
	uint32_t res;
	int nsegs;
	int error;

	if (sc == NULL)
		return ENXIO;
	if (chan >= BCM2835_MBOX_NUMCHANNELS || buf == NULL || buflen == 0 ||
	    pres == NULL)
		return EINVAL;

	/* Serialize the complete transaction, without excluding other channels. */
	mutex_enter(&sc->sc_chan_lock[chan]);
	if (sc->sc_quarantined[chan]) {
		error = EIO;
		goto out;
	}
	mutex_enter(&sc->sc_intr_lock);
	bcmmbox_intr1(sc, !cold);
	if (sc->sc_overflow[chan] ||
	    BCM2835_MBOX_CHAN(sc->sc_mbox[chan]) != 0) {
		mutex_exit(&sc->sc_intr_lock);
		bcmmbox_quarantine(sc, chan);
		error = EIO;
		goto out;
	}
	mutex_exit(&sc->sc_intr_lock);

	error = bus_dmamem_alloc(sc->sc_dmat, buflen, 16, 0, segs, 1,
	    &nsegs, BUS_DMA_WAITOK);
	if (error)
		goto out;
	error = bus_dmamem_map(sc->sc_dmat, segs, nsegs, buflen, &dma_buf,
	    BUS_DMA_WAITOK);
	if (error)
		goto map_failed;
	error = bus_dmamap_create(sc->sc_dmat, buflen, 1, buflen, 0,
	    BUS_DMA_WAITOK, &map);
	if (error)
		goto create_failed;
	error = bus_dmamap_load(sc->sc_dmat, map, dma_buf, buflen, NULL,
	    BUS_DMA_WAITOK);
	if (error)
		goto load_failed;

	/* Mailbox data has 28 address bits above the four channel bits. */
	if (map->dm_nsegs != 1 || map->dm_segs[0].ds_addr > UINT32_MAX ||
	    (map->dm_segs[0].ds_addr & BCM2835_MBOX_CHANMASK) != 0 ||
	    buflen - 1 > UINT32_MAX - map->dm_segs[0].ds_addr) {
		error = EFBIG;
		goto address_failed;
	}
	memcpy(dma_buf, buf, buflen);
	bus_dmamap_sync(sc->sc_dmat, map, 0, buflen,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	bcmmbox_deadline_init(&deadline);
	error = bcmmbox_write_locked(sc, chan, map->dm_segs[0].ds_addr,
	    &deadline);
	if (error)
		goto not_sent;
	error = bcmmbox_read_locked(sc, chan, &res, &deadline);
	if (error == 0 && res != map->dm_segs[0].ds_addr)
		error = EIO;
	if (error) {
		/*
		 * No matching completion: firmware may still write this buffer.
		 * Retain it permanently; quarantine prevents further allocations
		 * or submissions on this channel, even after a late response.
		 */
		sc->sc_retained_map[chan] = map;
		sc->sc_retained_buf[chan] = dma_buf;
		bcmmbox_quarantine(sc, chan);
		device_printf(sc->sc_dev, "channel %u request failed (%d), "
		    "quarantined until reboot\n", chan, error);
		goto out;
	}
not_sent:
	bus_dmamap_sync(sc->sc_dmat, map, 0, buflen,
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	if (error == 0) {
		memcpy(buf, dma_buf, buflen);
		*pres = res;
	}
address_failed:
	bus_dmamap_unload(sc->sc_dmat, map);
load_failed:
	bus_dmamap_destroy(sc->sc_dmat, map);
create_failed:
	bus_dmamem_unmap(sc->sc_dmat, dma_buf, buflen);
map_failed:
	bus_dmamem_free(sc->sc_dmat, segs, nsegs);
out:
	mutex_exit(&sc->sc_chan_lock[chan]);
	return error;
}
