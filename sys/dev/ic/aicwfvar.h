/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _DEV_IC_AICWFVAR_H_
#define _DEV_IC_AICWFVAR_H_

#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/evcnt.h>
#include <sys/mutex.h>
#include <sys/pool.h>
#include <sys/workqueue.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net80211/ieee80211_var.h>

#define	AICWF_MSG_MAX_PARAM		1032
#define	AICWF_TX_MAX			2048

#define	AICWF_KEY_SLOTS			(IEEE80211_WEP_NKID + 1)

struct aicwf_softc;

/*
 * write runs in sleepable context with sc_lock held. The core owns the
 * sc_txbuf allocation. The first len bytes contain one firmware command
 * or TX descriptor and payload. The bus may add its CRC and zero padding
 * within AICWF_TX_MAX. Finish DMA or copy the frame before returning.
 * The core may reuse or erase the buffer immediately.
 *
 * reserve is the number of firmware buffers to leave for commands on a
 * credit-based bus. USB uses separate command and data pipes.
 * If attempted is non-NULL, clear it before validation and set it just
 * before submitting the FIFO or USB transfer. An error after submission
 * leaves delivery uncertain. Return zero for a complete bus transfer,
 * or a BSD errno for admission, capacity, length, timeout or I/O errors.
 * The core receives the firmware confirmation separately through aicwf_rx.
 *
 * verify is optional. A bus that sets sc_fw_bip_pending must provide it.
 * The core calls it after ADD_IF in sleepable context with sc_lock free.
 * It checks the selected BIP firmware readback. Return EPROTO on a
 * mismatch and propagate transport errors. Zero permits the core's
 * remaining capability checks.
 */
struct aicwf_bus_ops {
	int (*write)(struct aicwf_softc *, size_t, unsigned int, bool *);
	int (*verify)(struct aicwf_softc *);
};

struct aicwf_softc {
	device_t		sc_dev;
	const struct aicwf_bus_ops *sc_ops;
	void			*sc_bus;
	bool			sc_dc;
	bool			sc_receive_only, sc_survey_failed;
	bool			sc_survey_draining, sc_survey_ok;
	bool			sc_dying;

	kmutex_t		sc_lock;
	kmutex_t		sc_epoch_lock;	/* non-sleeping key callbacks */
	kcondvar_t		sc_cv;

	uint8_t			*sc_txbuf;

	/* The confirmation the sender of a request waits for. */
	uint16_t		sc_cfm_id;
	uint16_t		sc_lost_cfm;	/* submitted USB command without a reply */
	bool			sc_cfm_done;
	uint16_t		sc_cfm_len;
	uint8_t			sc_cfm[AICWF_MSG_MAX_PARAM];

	bool			sc_cmd_busy;

	uint8_t			sc_enaddr[ETHER_ADDR_LEN];
	bool			sc_5ghz;
	uint32_t		sc_fw_version;
	uint8_t			sc_vif;

	struct ieee80211com	sc_ic;
	struct ethercom		sc_ec;
#define	sc_if			sc_ec.ec_if
	bool			sc_if_attached;
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);
	struct workqueue	*sc_taskq;
	struct work		sc_barrier;
	pool_cache_t		sc_taskpool;
	struct workqueue	*sc_txq;
	struct work		sc_txwork;
	volatile u_int		sc_txqueued;

	/* Serialize RX producers through delivery; state uses sc_epoch_lock.
	 * Lock order: sc_rx_lock -> sc_epoch_lock. Retirement needs only epoch.
	 * No path takes sc_rx_lock while it holds sc_lock or sc_epoch_lock. */
	kmutex_t		sc_rx_lock;
	callout_t		sc_rxba_ch;
	callout_t		sc_alive_ch;
	struct work		sc_alive_work;
	struct aicwf_ft_bss {
		uint8_t		bssid[ETHER_ADDR_LEN];
		uint16_t	mdid;
		uint8_t		r0kh_len;
		uint8_t		r0kh[45];
	}			sc_ft_bss[4];
	/* Host-managed 802.11k/v exchange (IEEE80211_IOC_STA_MGMT). */
	/* Station requests and firmware ownership are protected by sc_lock. */
	bool			sc_sta_enabled;
	bool			sc_sta_tx_pending, sc_sta_tx_sent;
	bool			sc_sta_tx_mismatch_reported;
	uint32_t		sc_sta_token;
	struct ieee80211req_sta_mgmt sc_sta_tx;
	bool			sc_sta_scan_pending, sc_sta_scan_started;
	bool			sc_scan_published, sc_sta_scan_draining;
	bool			sc_sta_cancel_queued;
	uint32_t		sc_sta_scan_serial;
	struct ieee80211req_sta_mgmt sc_sta_scan;
	uint16_t		sc_sta_assoc_len;
	uint8_t			sc_sta_assoc[804];
	volatile u_int		sc_alive_rx;	/* ticks of last RX frame */
	u_int			sc_alive_misses;
	bool			sc_alive_queued;
	uint64_t		sc_alive_epoch;
	uint64_t		sc_join_serial;	/* sc_epoch_lock */
	uint64_t		sc_configure_serial; /* sc_epoch_lock */
	uint64_t		sc_alive_join_serial;
	struct ieee80211_rxba_offload sc_rxba;
	uint64_t		sc_rx_serial, sc_rx_assoc; /* sc_epoch_lock */
	uint64_t		sc_rx_join; /* Host CONNECT request eligible for indication. */
	bool			sc_rx_active;
	uint8_t			sc_rx_vif, sc_rx_ap, sc_rx_bssid[ETHER_ADDR_LEN];
	struct evcnt		sc_ev_ampdu;	/* frames of an A-MPDU */
	struct evcnt		sc_ev_amsdu;	/* frames with several packets */
	struct evcnt		sc_ev_gap;	/* waits that ran out */

	bool			sc_scanning;
	int			sc_scan_ticks;
	bool			sc_connecting;
	bool			sc_connected;
	bool			sc_qos;
	uint8_t			sc_ap;		/* station index of the AP */
	uint8_t			sc_hwkey[AICWF_KEY_SLOTS];
	uint16_t		sc_sae_caps;
	bool			sc_fw_bip_pending;
	bool			sc_fw_bip_verified;
	bool			sc_fw_bip_ready;
	bool			sc_fw_bip_wstatus_valid;
	bool			sc_fw_bip_bootstatus_valid;
	uint32_t		sc_fw_bip_wstatus;
	uint32_t		sc_fw_bip_bootstatus;
	bool			sc_sae_enabled;
	bool			sc_sae_pending;
	bool			sc_sae_authenticated;
	uint32_t		sc_sae_generation;
	uint64_t		sc_sae_epoch;	/* changes on every join/leave */
	uint8_t			sc_sae_bssid[ETHER_ADDR_LEN];
	uint8_t			sc_igtk[2];
	/* Protected by sc_lock; valid only after a complete key confirmation. */
	bool			sc_ptk_valid;
	uint8_t			sc_ptk[16];
	uint64_t		sc_pmf_rx_pn;
	struct evcnt		sc_ev_sae_start;
	struct evcnt		sc_ev_sae_rx;
	struct evcnt		sc_ev_sae_tx;
	struct evcnt		sc_ev_sae_drop;
};


/*
 * Call prepare once in thread context before installing the bus RX callback.
 * The ops and bus cookie must outlive the prepared station. Complete the
 * bus-specific startup, station_setup and ifattach in sleepable context.
 * Deliver each complete RX buffer in thread context. It is borrowed by
 * aicwf_rx until return. Capture the RX serial under sc_epoch_lock before
 * deferring delivery.
 *
 * On failure or detach, unregister the RX producer so that no new callback
 * can enter, then call quiesce. Quiesce rejects commands and wakes waiters.
 * Drain all bus callbacks and deferred RX work before release. Release
 * detaches the ifnet and frees core workqueues, timers, buffers and locks.
 * The bus can then free its own resources. Teardown may sleep.
 */
void aicwf_prepare(struct aicwf_softc *, device_t,
    const struct aicwf_bus_ops *, void *, bool);
int aicwf_cmd(struct aicwf_softc *, uint16_t, const void *, size_t, uint16_t,
    void *, size_t);
int aicwf_fw_cmd(struct aicwf_softc *, uint16_t, const void *, size_t,
    uint16_t, void *, size_t);
int aicwf_station_setup(struct aicwf_softc *);
int aicwf_ifattach(struct aicwf_softc *);
void aicwf_rx(struct aicwf_softc *, const uint8_t *, size_t, uint64_t);
void aicwf_quiesce(struct aicwf_softc *);
void aicwf_release(struct aicwf_softc *);
#endif /* _DEV_IC_AICWFVAR_H_ */
