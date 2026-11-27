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

/*
 * The HT and VHT link that net80211 negotiated with the AP.  All zero
 * means a legacy link.  The capability words are the words of the AP;
 * the MCS fields are the transmit sets after the intersection.
 */
struct rtw89_nb_ht {
	bool		ht;
	bool		vht;
	uint16_t	ht_cap;		/* HT Capabilities Info of the AP */
	uint8_t		ht_ampdu;	/* A-MPDU Parameters of the AP */
	uint16_t	ht_mcs;		/* TX HT-MCS 0-15 */
	uint32_t	vht_cap;	/* VHT Capabilities Info of the AP */
	uint16_t	vht_map;	/* TX VHT-MCS map */
	bool		vht_bfee;	/* local SU beamformee, AP beamformer */
	unsigned int	width;		/* 20, 40 or 80 MHz */
	unsigned int	center;		/* center channel at 40 or 80 MHz */
	/* HE (802.11ax): the words of the AP and the negotiated TX map. */
	bool		he;
	uint8_t		he_mac[6];
	uint8_t		he_phy[11];
	uint16_t	he_map;		/* TX HE-MCS map, VHT coding */
	uint8_t		he_ppe_len;
	uint8_t		he_ppe[25];
	uint8_t		he_bss_color;	/* BSS Color Information octet */
	uint32_t	he_params;	/* HE Operation Parameters */
	uint16_t	he_basic_map;
	bool		he_bfee;	/* local SU beamformee, AP beamformer */
};

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
	struct rtw89_nb_ht ht;		/* used by rtw89_nb_bss_assoc() */
};

/* EDCA parameters of one access category, in net80211 units. */
struct rtw89_nb_edca {
	uint8_t		aifsn;
	uint8_t		ecw_min;
	uint8_t		ecw_max;
	uint16_t	txop;		/* 32 us units */
};

/* Link observations of the associated AP, for diagnostics only. */
#define RTW89_NB_MODE_LEGACY	0
#define RTW89_NB_MODE_HT	1
#define RTW89_NB_MODE_VHT	2
#define RTW89_NB_MODE_HE	3
struct rtw89_nb_link {
	bool		valid;		/* a station entry exists */
	uint8_t		tx_mode;	/* RTW89_NB_MODE_* of the last RA report */
	uint8_t		tx_mcs;		/* HT MCS 0-31 or VHT/HE MCS 0-11 */
	uint8_t		tx_nss;
	uint8_t		tx_sgi;
	uint16_t	tx_bw;		/* MHz */
	uint16_t	tx_legacy;	/* 100 kbit/s units, legacy mode only */
	uint16_t	tx_hw_rate;	/* RTW89 hardware rate code */
	uint16_t	rx_hw_rate;	/* hardware rate code of the last RX data */
	int		rssi;		/* average RSSI in dBm */
};

/* Access categories in net80211 order (WME_AC_BE, BK, VI, VO). */
#define RTW89_NB_NAC		4

/* Frame flags for rtw89_nb_tx(). */
#define RTW89_NB_TX_NO_ACK	0x01
#define RTW89_NB_TX_EAPOL	0x02
#define RTW89_NB_TX_AMPDU	0x04	/* QoS data of an originator session */

/* Implemented by rtw89_bridge.c and rtw89_compat.c. */
int	rtw89_compat_init(bus_dma_tag_t);
void	rtw89_compat_fini(void);
int	rtw89_nb_attach(const struct rtw89_nb_attach_args *, struct rtw89_nb **);
void	rtw89_nb_detach(struct rtw89_nb *);
void	rtw89_nb_get_macaddr(struct rtw89_nb *, uint8_t *);
int	rtw89_nb_start(struct rtw89_nb *, const uint8_t *);
void	rtw89_nb_stop(struct rtw89_nb *);
int	rtw89_nb_suspend(struct rtw89_nb *);
int	rtw89_nb_resume(struct rtw89_nb *);
int	rtw89_nb_set_channel(struct rtw89_nb *, unsigned int, unsigned int);
void	rtw89_nb_scan_start(struct rtw89_nb *);
void	rtw89_nb_scan_end(struct rtw89_nb *);
int	rtw89_nb_offchan(struct rtw89_nb *, unsigned int, unsigned int);
int	rtw89_nb_onchan(struct rtw89_nb *, bool);
int	rtw89_nb_bss_join(struct rtw89_nb *, const struct rtw89_nb_bss *);
int	rtw89_nb_bss_assoc(struct rtw89_nb *, const struct rtw89_nb_bss *);
void	rtw89_nb_bss_leave(struct rtw89_nb *);
int	rtw89_nb_tx_quiet(struct rtw89_nb *, bool);
int	rtw89_nb_tx(struct rtw89_nb *, const void *, size_t, unsigned int,
	    unsigned int);
bool	rtw89_nb_tx_space(struct rtw89_nb *);
unsigned int rtw89_nb_nss(struct rtw89_nb *);
void	rtw89_nb_link_info(struct rtw89_nb *, struct rtw89_nb_link *);
void	rtw89_nb_conf_wme(struct rtw89_nb *,
	    const struct rtw89_nb_edca[RTW89_NB_NAC]);
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
