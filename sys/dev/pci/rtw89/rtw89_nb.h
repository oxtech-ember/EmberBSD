/*	$NetBSD$	*/

/*-
 * Copyright (c) 2026 The EmberBSD Project.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Interface between the NetBSD glue (if_rtw89.c, net80211 side) and the
 * Linux-derived rtw89 code (rtw89_bridge.c, mac80211 emulation side).
 * Only plain C types are used so both sides can include it.
 */

#ifndef _RTW89_NB_H_
#define _RTW89_NB_H_

#include <sys/types.h>
#include <sys/bus.h>

struct device;
struct rtw89_nb;

struct rtw89_nb_attach_args {
	struct device	*aa_dev;	/* NetBSD device_t */
	void		*aa_glue;	/* if_rtw89 softc */
	void		*aa_mmio;	/* linear mapping of BAR 2 */
	size_t		 aa_mmio_size;
	uint16_t	 aa_device_id;
};

/* Hardware rate indices used in rate masks: CCK 1/2/5.5/11, OFDM 6-54. */
#define RTW89_NB_RATE_CCK_MASK	0x00f
#define RTW89_NB_NRATES		12

struct rtw89_nb_bss {
	uint8_t		bssid[6];
	uint16_t	aid;
	uint16_t	beacon_int;
	uint8_t		dtim_period;
	bool		short_slot;
	uint16_t	rates;		/* RTW89_NB_RATE_* bitmap */
	uint16_t	basic_rates;
	unsigned int	chan;
	unsigned int	freq;
};

/* Frame flags for rtw89_nb_tx(). */
#define RTW89_NB_TX_NO_ACK	0x01
#define RTW89_NB_TX_EAPOL	0x02

/* Implemented by rtw89_bridge.c and rtw89_compat.c. */
int	rtw89_compat_init(bus_dma_tag_t);
void	rtw89_compat_fini(void);
int	rtw89_nb_attach(const struct rtw89_nb_attach_args *, struct rtw89_nb **);
void	rtw89_nb_detach(struct rtw89_nb *);
void	rtw89_nb_get_macaddr(struct rtw89_nb *, uint8_t *);
int	rtw89_nb_start(struct rtw89_nb *, const uint8_t *);
void	rtw89_nb_stop(struct rtw89_nb *);
int	rtw89_nb_set_channel(struct rtw89_nb *, unsigned int, unsigned int);
void	rtw89_nb_scan_start(struct rtw89_nb *);
void	rtw89_nb_scan_end(struct rtw89_nb *);
int	rtw89_nb_bss_join(struct rtw89_nb *, const struct rtw89_nb_bss *);
int	rtw89_nb_bss_assoc(struct rtw89_nb *, const struct rtw89_nb_bss *);
void	rtw89_nb_bss_leave(struct rtw89_nb *);
int	rtw89_nb_tx_quiet(struct rtw89_nb *, bool);
int	rtw89_nb_tx(struct rtw89_nb *, const void *, size_t, unsigned int);
void	rtw89_nb_set_debug(unsigned int);
extern unsigned int rtw89_debug_mask;

/* Implemented by the NetBSD glue (if_rtw89.c). */
void	rtw89_nb_rx(void *, const void *, size_t, int, unsigned int);
void	rtw89_nb_tx_done(void *, bool);
void	rtw89_nb_queues(void *, bool);
void	rtw89_nb_link_lost(void *);
int	rtw89_nb_pci_cfg_read(struct device *, int, int, uint32_t *);
int	rtw89_nb_pci_cfg_write(struct device *, int, int, uint32_t);
int	rtw89_nb_pci_capability(struct device *, int, bool);
int	rtw89_nb_request_irq(struct device *, int (*)(int, void *),
	    int (*)(int, void *), void *);
void	rtw89_nb_free_irq(struct device *);
void	rtw89_nb_synchronize_irq(struct device *);

#endif /* _RTW89_NB_H_ */
