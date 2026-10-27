/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8822CE resource lifecycle, AI-assisted. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#ifdef _KERNEL
#include <sys/bus.h>
#include <sys/null.h>
#else
#include <string.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw88_calibration.h>

int
rtw88_get_caps(const struct rtw88_softc *sc, struct rtw88_caps *out)
{
	const struct rtw88_caps *caps;

	if (sc == NULL || sc->sc_chip == NULL || out == NULL)
		return EINVAL;
	caps = &sc->sc_chip->caps;
	if (caps->bands == 0 || (caps->bands & ~3U) != 0 ||
	    (caps->widths & RTW88_WIDTH_20) == 0 ||
	    (caps->widths & ~7U) != 0 || caps->nss == 0 || caps->nss > 2 ||
	    caps->tx_desc_size == 0 || caps->rx_desc_size == 0 ||
	    ((caps->features & RTW88_CAP_VHT) != 0 &&
	     ((caps->bands & RTW88_BAND_5G) == 0 ||
	      (caps->widths & (RTW88_WIDTH_40 | RTW88_WIDTH_80)) == 0)))
		return EINVAL;
	*out = *caps;
	return 0;
}

int
rtw88_chip_valid(const struct rtw88_softc *sc)
{
	struct rtw88_caps caps;
	const struct rtw88_chip_ops *c;

	if (rtw88_get_caps(sc, &caps) != 0)
		return 0;
	c = sc->sc_chip;
	if (c->calibration != NULL &&
	    (c->calibration->blocked == NULL || c->calibration->active == NULL ||
	     c->calibration->begin == NULL || c->calibration->validate == NULL ||
	     c->calibration->run == NULL || c->calibration->retire == NULL ||
	     c->calibration->seal == NULL || c->calibration->progress == NULL))
		return 0;
	return c->identify != NULL && c->start != NULL && c->stop != NULL &&
	    c->fw_init != NULL && c->channel != NULL && c->station != NULL &&
	    c->edca != NULL && c->ra != NULL && c->mailbox_begin != NULL &&
	    c->mailbox_end != NULL && c->command != NULL && c->diag != NULL &&
	    c->loopback != NULL && c->tx_desc != NULL && c->rx_desc != NULL;
}

int
rtw88_start_tuple(struct rtw88_softc *sc, const struct rtw88_channel_tuple *tuple)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->start == NULL)
		return EINVAL;
	return sc->sc_chip->start(sc, tuple);
}

int
rtw88_channel_apply(struct rtw88_softc *sc, const struct rtw88_channel_tuple *tuple)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->channel == NULL)
		return EINVAL;
	return sc->sc_chip->channel(sc, tuple);
}

int
rtw88_station_config(struct rtw88_softc *sc, const uint8_t *bssid, unsigned aid, unsigned rates, unsigned slot, int linked)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->station == NULL)
		return EINVAL;
	return sc->sc_chip->station(sc, bssid, aid, rates, slot, linked);
}

int
rtw88_edca_apply(struct rtw88_softc *sc,
    const struct rtw88_edca_params params[RTW88_WME_NAC], unsigned slot)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->edca == NULL)
		return EINVAL;
	return sc->sc_chip->edca(sc, params, slot);
}

int
rtw88_ra_configure(struct rtw88_softc *sc, const struct rtw88_ra_config *cfg)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->ra == NULL)
		return EINVAL;
	return sc->sc_chip->ra(sc, cfg);
}

int
rtw88_fw_mailbox_begin(struct rtw88_softc *sc, unsigned count, int *ready)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->mailbox_begin == NULL)
		return EINVAL;
	return sc->sc_chip->mailbox_begin(sc, count, ready);
}

int
rtw88_fw_mailbox_end(struct rtw88_softc *sc, int commit)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->mailbox_end == NULL)
		return EINVAL;
	return sc->sc_chip->mailbox_end(sc, commit);
}

int
rtw88_fw_command(struct rtw88_softc *sc, uint8_t id, const void *payload, size_t len)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->command == NULL)
		return EINVAL;
	return sc->sc_chip->command(sc, id, payload, len);
}

int
rtw88_diag(struct rtw88_softc *sc, uint32_t *registers)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->diag == NULL)
		return EINVAL;
	return sc->sc_chip->diag(sc, registers);
}

int
rtw88_loopback(struct rtw88_softc *sc, int mode)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->loopback == NULL)
		return EINVAL;
	return sc->sc_chip->loopback(sc, mode);
}

int
rtw88_fw_init(struct rtw88_softc *sc)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->fw_init == NULL)
		return EINVAL;
	return sc->sc_chip->fw_init(sc);
}

int
rtw88_chip_tx_desc(struct rtw88_softc *sc, uint8_t *out, size_t size,
    const struct rtw88_tx_info *info)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->tx_desc == NULL)
		return EINVAL;
	return sc->sc_chip->tx_desc(sc, out, size, info);
}

int
rtw88_chip_rx_desc(struct rtw88_softc *sc, const uint8_t *data, size_t size,
    struct rtw88_rx_info *info)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->rx_desc == NULL)
		return EINVAL;
	return sc->sc_chip->rx_desc(sc, data, size, info);
}

#ifdef RTW88_VHT80
int
rtw88_tuple_check(const struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *tuple)
{
	if (sc == NULL || sc->sc_chip == NULL || tuple == NULL)
		return EINVAL;
	if (sc->sc_chip->tuple_check == NULL)
		return EOPNOTSUPP;
	return sc->sc_chip->tuple_check(sc, tuple);
}
#endif

int
rtw88_calibration_blocked(const struct rtw88_softc *sc)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->calibration == NULL)
		return 0;
	if (sc->sc_chip->calibration->blocked == NULL)
		return 1;
	return sc->sc_chip->calibration->blocked(sc);
}

int
rtw88_calibration_active(const struct rtw88_softc *sc)
{
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->calibration == NULL)
		return 0;
	if (sc->sc_chip->calibration->active == NULL)
		return 0;
	return sc->sc_chip->calibration->active(sc);
}

int
rtw88_calibration_validate(struct rtw88_softc *sc)
{
	if (sc == NULL || sc->sc_chip == NULL)
		return EINVAL;
	if (sc->sc_chip->calibration == NULL ||
	    sc->sc_chip->calibration->validate == NULL)
		return EOPNOTSUPP;
	return sc->sc_chip->calibration->validate(sc);
}

int
rtw88_calibration_run(struct rtw88_softc *sc)
{
	if (sc == NULL || sc->sc_chip == NULL)
		return EINVAL;
	if (sc->sc_chip->calibration == NULL ||
	    sc->sc_chip->calibration->run == NULL)
		return EOPNOTSUPP;
	return sc->sc_chip->calibration->run(sc);
}

int
rtw88_calibration_retire(struct rtw88_softc *sc)
{
	if (sc == NULL || sc->sc_chip == NULL)
		return EINVAL;
	if (sc->sc_chip->calibration == NULL ||
	    sc->sc_chip->calibration->retire == NULL)
		return EOPNOTSUPP;
	return sc->sc_chip->calibration->retire(sc);
}

int
rtw88_calibration_seal(struct rtw88_softc *sc)
{
	if (sc == NULL || sc->sc_chip == NULL)
		return EINVAL;
	if (sc->sc_chip->calibration == NULL ||
	    sc->sc_chip->calibration->seal == NULL)
		return EOPNOTSUPP;
	return sc->sc_chip->calibration->seal(sc);
}

int
rtw88_calibration_begin(struct rtw88_softc *sc,
    const struct rtw88_calibration_services *services)
{
	if (sc == NULL || sc->sc_chip == NULL || services == NULL)
		return EINVAL;
	if (sc->sc_chip->calibration == NULL || sc->sc_chip->calibration->begin == NULL)
		return EOPNOTSUPP;
	return sc->sc_chip->calibration->begin(sc, services);
}

int
rtw88_calibration_progress(struct rtw88_softc *sc, struct rtw88_rfk_diag *out)
{
	if (sc == NULL || sc->sc_chip == NULL || out == NULL)
		return EINVAL;
	if (sc->sc_chip->calibration == NULL || sc->sc_chip->calibration->progress == NULL)
		return EOPNOTSUPP;
	sc->sc_chip->calibration->progress(sc, out);
	return 0;
}
