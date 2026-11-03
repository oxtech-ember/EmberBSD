/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Deferred RX generation check; final peer/key admission belongs to netif. */
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw88_async.h>
#include <dev/ic/rtw88_executor.h>

int
rtw88_rx_epoch_check(struct rtw88_softc *sc, const struct rtw88_async_epoch *id)
{
	if (sc == NULL || id == NULL)
		return EINVAL;
	if (sc->sc_executor == NULL || !rtw88_executor_owned(sc))
		return EPERM;
	if (sc->sc_generation != id->generation)
		return ESTALE;
	if (!sc->sc_running || sc->sc_stop_requested || sc->sc_rx_frame == NULL)
		return ENXIO;
	return 0;
}

int
rtw88_rx_deliver_epoch(struct rtw88_softc *sc, const uint8_t *data, size_t len,
    uint64_t sequence, const struct rtw88_async_epoch *input)
{
	struct rtw88_async_epoch id;
	int error;

	if (data == NULL || input == NULL || sequence == 0)
		return EINVAL;
	id = *input;
	error = rtw88_rx_epoch_check(sc, &id);
	if (error != 0)
		return error;
	/* The synchronous net callback must recheck identity under its lock. */
	rtw88_rx_deliver_host(sc, data, len, sequence, &id);
	return 0;
}
