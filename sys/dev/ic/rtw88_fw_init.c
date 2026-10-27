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

int
rtw88_fw_packet(struct rtw88_softc *sc, const uint8_t *packet, size_t len)
{
	if (sc == NULL || sc->sc_hci == NULL)
		return EINVAL;
	if (sc->sc_hci->fw_packet == NULL)
		return EOPNOTSUPP;
	if (sc->sc_bootstrapping)
		return ENXIO;
	return sc->sc_hci->fw_packet(sc, packet, len);
}
