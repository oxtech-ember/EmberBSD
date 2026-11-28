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

/* AIC FullMAC station and net80211 adapter. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/atomic.h>
#include <sys/endian.h>
#include <sys/evcnt.h>
#include <sys/kernel.h>
#include <sys/kauth.h>
#include <sys/kmem.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/workqueue.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/route.h>

#include <netinet/in.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_beacon_ies.h>
#include <net80211/ieee80211_amsdu.h>

#include <net80211/ieee80211_rsn.h>
#include <net80211/ieee80211_sta.h>
#include <dev/ic/aicwf_command.h>
#include <dev/ic/aicwf_sae.h>
#include <dev/ic/aicwf_vht.h>
#include <dev/ic/aicwf_sta.h>

#include <dev/ic/aicwfvar.h>

/* MAC management task of the running firmware. */
#define	AICWF_MM_RESET_REQ		0x0000
#define	AICWF_MM_RESET_CFM		0x0001
#define	AICWF_MM_START_REQ		0x0002
#define	AICWF_MM_START_CFM		0x0003
#define	 AICWF_IF_STA			0
#define	AICWF_MM_SET_RF_CALIB_REQ	0x0069
#define	AICWF_MM_SET_RF_CALIB_CFM	0x006a

/* Host-side link liveness: the AP beacons roughly every 102 ms; two
 * consecutive silent checks at a 2 s cadence mean the association is dead
 * even though the firmware never raised SM_DISCONNECT_IND. */
#define	AICWF_ALIVE_PERIOD_MS	2000
#define	AICWF_ALIVE_SILENT_MS	10000
#define	AICWF_MM_GET_FW_VERSION_REQ	0x0080
#define	AICWF_MM_GET_FW_VERSION_CFM	0x0081

#define	AICWF_MM_KEY_ADD_REQ		0x0024
#define	AICWF_MM_KEY_ADD_CFM		0x0025
#define	 AICWF_CIPHER_CCMP		2
#define	 AICWF_CIPHER_BIP		5
#define	 AICWF_STA_NONE			0xff	/* a group key */
#define	AICWF_MM_KEY_DEL_REQ		0x0026
#define	AICWF_MM_KEY_DEL_CFM		0x0027
#define	AICWF_MM_CONNECTION_LOSS_IND	0x0043
#define	AICWF_MM_CHANNEL_SWITCH_IND	0x0044
#define	AICWF_MM_CHANNEL_PRE_SWITCH_IND	0x0045
#define	AICWF_MM_CHANNEL_SURVEY_IND	0x004f

/* Scan task. */
#define	AICWF_SCANU_CANCEL_REQ		0x100a
#define	AICWF_SCANU_CANCEL_CFM		0x100b

/* Management entity task. */
#define	AICWF_ME_CONFIG_REQ		0x1400
#define	AICWF_ME_CONFIG_CFM		0x1401
#define	AICWF_ME_CHAN_CONFIG_REQ	0x1402
#define	AICWF_ME_CHAN_CONFIG_CFM	0x1403

#define	AICWF_ME_SET_CONTROL_PORT_REQ	0x1404
#define	AICWF_ME_SET_CONTROL_PORT_CFM	0x1405

/* Station management task. */
#define	AICWF_SM_CONNECT_REQ		0x1800
#define	AICWF_SM_CONNECT_CFM		0x1801
#define	AICWF_SM_CONNECT_IND		0x1802
#define	AICWF_SM_DISCONNECT_REQ		0x1803
#define	AICWF_SM_DISCONNECT_CFM		0x1804
#define	AICWF_SM_DISCONNECT_IND		0x1805
#define	AICWF_SM_EXTERNAL_AUTH_IND	0x1806
#define	AICWF_SM_EXTERNAL_AUTH_RSP	0x1807
#define	AICWF_SM_EXTERNAL_AUTH_CFM	0x180c
#define	AICWF_ME_TRAFFIC_IND_REQ		0x140b
#define	 AICWF_CONNECT_MFP		__BIT(4)
#define	 AICWF_CONNECT_REASSOC		__BIT(5)
#define	 AICWF_FEATURE_MFP		__BIT(13)
#define	 AICWF_AUTH_SAE			3
#define	 AICWF_CONNECT_PORT_HOST	__BIT(0) /* the host opens the port */
#define	 AICWF_CONNECT_WPA		__BIT(3)

/*
 * A frame to send starts with a descriptor: payload length, flags,
 * host tag, destination, source, Ethernet type, queue, traffic class,
 * interface, station, flags. The payload of the Ethernet frame follows.
 */
#define	AICWF_HWQ_BE			1
#define	AICWF_HWQ_VO			3
#define	AICWF_TID_NONE			0xff
#define	AICWF_TX_RESERVE		2	/* buffers left to messages */

/*
 * A received frame starts with 56 bytes from the radio: the length of
 * the 802.11 frame first, a status word and a flags word further on.
 * The 802.11 frame follows after one more word.
 */
#define	AICWF_RXHDR_STATUS		36
#define	 AICWF_RX_DECR			__BITS(4,2)
#define	  AICWF_RX_DECR_WEP		1
#define	  AICWF_RX_DECR_TKIP		2
#define	  AICWF_RX_DECR_WAPI		7
#define	AICWF_RXHDR_FLAGS		48
#define	 AICWF_RX_FLAG_UPLOAD		__BIT(6)
#define	 AICWF_RX_FLAG_REORDER		__BIT(5)	/* part of an A-MPDU */
#define	 AICWF_RX_FLAG_AMSDU		__BIT(0)

/*
 * With 802.11n the firmware answers the block acknowledgement itself but
 * hands the frames up in the order they arrived; the host puts them in
 * sequence. The window and the wait for a missing frame are the vendor
 * driver's.
 */
#define	AICWF_REORDER_WAIT_MS		50

#define	AICWF_SCAN_INTERVAL_MS		3000

/* A channel as the firmware describes it: 6 bytes. */
#define	AICWF_CHAN_LEN			6
#define	AICWF_CHAN_2G_MAX		14
#define	AICWF_CHAN_5G_MAX		28
#define	AICWF_CHAN_MAX			(AICWF_CHAN_2G_MAX + AICWF_CHAN_5G_MAX)
#define	AICWF_BAND_2G			0
#define	AICWF_BAND_5G			1
#define	AICWF_TX_POWER_DBM		20

#define	AICWF_UAPSD_TIMEOUT_MS		300
#define	AICWF_LP_CLK_PPM		20

#define	AICWF_CMD_TIMEOUT_MS		2000

enum aicwf_task_cmd {
	AICWF_TASK_NEWSTATE,
	AICWF_TASK_KEY_SET,
	AICWF_TASK_KEY_DELETE,
	AICWF_TASK_STA_MGMT,
	AICWF_TASK_STA_CANCEL_SCAN,
	AICWF_TASK_RECOVER_SCAN,
};

struct aicwf_task {
	struct work		t_work;
	enum aicwf_task_cmd	t_cmd;
	enum ieee80211_state	t_state;
	int			t_arg;
	u_int			t_cipher;
	u_int			t_keyix;
	bool			t_group;
	uint64_t		t_epoch;
	uint64_t		t_rx_serial;
	uint64_t		t_join_serial;
	uint64_t		t_configure_serial;
	u_int			t_keylen;
	uint8_t			t_key[IEEE80211_KEYBUF_SIZE];
	struct ieee80211req_sta_mgmt t_sta;
};

#define	AICWF_TASK_COUNT		16
#define	AICWF_KEY_SLOT_PAIRWISE		IEEE80211_WEP_NKID
#define	AICWF_HWKEY_NONE		0xff

static void	aicwf_scan_result(struct aicwf_softc *, const uint8_t *,
		    size_t);
static void	aicwf_scan_done(struct aicwf_softc *, const uint8_t *, size_t);
static void	aicwf_connect_ind(struct aicwf_softc *, const uint8_t *,
		    size_t);
static void	aicwf_disconnect(struct aicwf_softc *);
static void	aicwf_disconnect_ind(struct aicwf_softc *, const uint8_t *, size_t);
static void	aicwf_alive_lost(struct aicwf_softc *);
static void	aicwf_alive_work(struct aicwf_softc *);
static uint64_t aicwf_recover_disconnect(struct aicwf_softc *, uint64_t);
static void	aicwf_sae_event(struct aicwf_softc *, bool,
		    const uint8_t *, size_t);
static void	aicwf_rx_data(struct aicwf_softc *, const uint8_t *, size_t, uint64_t);
static void	aicwf_rx_retire(struct aicwf_softc *);
static bool	aicwf_rx_begin(struct aicwf_softc *);
static void	aicwf_rx_resume(struct aicwf_softc *, uint64_t);
static void	aicwf_rx_fence_locked(struct aicwf_softc *,
		    struct ieee80211_rxba_output *);
static void	aicwf_rx_discard(struct ieee80211_rxba_output *);
static void	aicwf_rxba_timeout(void *);

static void	aicwf_sta_tx_cfm(struct aicwf_softc *, const uint8_t *, size_t);
static bool	aicwf_sta_ready(struct aicwf_softc *);
static bool	aicwf_sta_scan_finish_locked(struct aicwf_softc *);
static void	aicwf_sta_retire_locked(struct aicwf_softc *, uint8_t *, bool *,
		    bool *);
static int	aicwf_newstate(struct ieee80211com *, enum ieee80211_state, int);


static int
aicwf_write_attempted(struct aicwf_softc *sc, size_t len, u_int reserve,
    bool *attempted)
{
	KASSERT(mutex_owned(&sc->sc_lock));
	if (atomic_load_relaxed(&sc->sc_dying))
		return ENXIO;
	return sc->sc_ops->write(sc, len, reserve, attempted);
}

static int
aicwf_write(struct aicwf_softc *sc, size_t len, u_int reserve)
{
	return aicwf_write_attempted(sc, len, reserve, NULL);
}

/*
 * MM_ADD_IF_CFM contains a status and an assigned interface index. The
 * station worker must remove a VIF if the scan that requested it has left.
 */
static int
aicwf_survey_add_reply(struct aicwf_softc *sc, const uint8_t *p, size_t len,
    bool abandoned)
{
	KASSERT(mutex_owned(&sc->sc_lock));
	if (len != 2 || p[0] >= AICWF_CO_OP_IN_PROGRESS ||
	    (p[0] == 0 && p[1] > 3)) {
		device_printf(sc->sc_dev,
		    "invalid MM_ADD_IF confirmation: length %zu status %d\n",
		    len, len != 0 ? p[0] : -1);
		sc->sc_survey_failed = true;
		return EPROTO;
	}
	if (p[0] != 0) {
		device_printf(sc->sc_dev, "MM_ADD_IF firmware status %u\n", p[0]);
		return EIO;
	}
	if (abandoned && !atomic_load_relaxed(&sc->sc_dying)) {
		sc->sc_survey_cleanup = true;
		sc->sc_survey_vif = p[1];
		workqueue_enqueue(sc->sc_taskq, &sc->sc_survey_work, NULL);
	}
	return 0;
}

/*
 * Send a request and wait for its confirmation. The parameters of the
 * confirmation are copied to cfm, which holds cfm_len bytes.
 */
static int
aicwf_cmd_reply(struct aicwf_softc *sc, uint16_t id, const void *param,
    size_t param_len, uint16_t cfm_id, void *cfm, size_t cfm_len, int strict,
    uint64_t epoch)
{
	uint8_t * const buf = sc->sc_txbuf;
	size_t len;
	bool submitted = false;
	int error;

	KASSERT(param_len <= AICWF_MSG_MAX_PARAM);

	mutex_enter(&sc->sc_lock);
	while (sc->sc_cmd_busy && !atomic_load_relaxed(&sc->sc_dying))
		cv_wait(&sc->sc_cv, &sc->sc_lock);
	if (atomic_load_relaxed(&sc->sc_dying)) {
		mutex_exit(&sc->sc_lock);
		return ENXIO;
	}
	if (sc->sc_lost_cfm != 0 && !(sc->sc_receive_only &&
	    sc->sc_lost_cfm == AICWF_SCANU_START_ACK && id == AICWF_MM_REMOVE_IF_REQ)) {
		mutex_exit(&sc->sc_lock);
		return EAGAIN;
	}
	/* The caller may have retired while waiting for another command. */
	if (epoch != 0 && epoch != sc->sc_sae_epoch) {
		mutex_exit(&sc->sc_lock);
		return ESTALE;
	}
	if ((id == AICWF_SCANU_START_REQ && epoch != 0 &&
	    (!sc->sc_sta_scan_pending || !sc->sc_sta_scan_started ||
	    !aicwf_sta_ready(sc))) ||
	    (id == AICWF_SCANU_CANCEL_REQ &&
	    (!sc->sc_sta_scan_started || !sc->sc_sta_scan_draining ||
	    !sc->sc_scanning))) {
		mutex_exit(&sc->sc_lock);
		return ECANCELED;
	}
	len = aicwf_command_encode(buf, AICWF_TX_MAX, id, param, param_len);
	if (len == 0) {
		mutex_exit(&sc->sc_lock);
		return EMSGSIZE;
	}
	sc->sc_cmd_busy = true;
	sc->sc_cfm_id = cfm_id;
	sc->sc_cfm_done = false;

	error = aicwf_write_attempted(sc, len, 0, &submitted);
	if (id == AICWF_SCANU_START_REQ)
		sc->sc_scan_published = submitted;
	if (strict)
		explicit_memset(buf + AICWF_TX_MSG_OFFSET +
		    AICWF_MSG_HDR_LEN, 0, param_len);
	const unsigned start = getticks();
	const unsigned limit = mstohz(AICWF_CMD_TIMEOUT_MS);
	while (error == 0 && !sc->sc_cfm_done && !atomic_load_relaxed(&sc->sc_dying)) {
		const unsigned elapsed = (unsigned)getticks() - start;
		error = elapsed >= limit ? ETIMEDOUT :
		    cv_timedwait(&sc->sc_cv, &sc->sc_lock, limit - elapsed);
	}
	if (atomic_load_relaxed(&sc->sc_dying))
		error = ENXIO;
	/* Mode 0 pads legacy replies; 1 requires a minimum; 2 is exact. */
	if (error == 0 && strict && (sc->sc_cfm_len < cfm_len ||
	    (strict == 2 && sc->sc_cfm_len != cfm_len)))
		error = EPROTO;
	if (sc->sc_receive_only && cfm_id == AICWF_MM_ADD_IF_CFM &&
	    submitted && sc->sc_cfm_done) {
		int status = aicwf_survey_add_reply(sc, sc->sc_cfm,
		    sc->sc_cfm_len, error != 0);
		if (error == 0)
			error = status;
	}
	if (error == 0 && cfm_len != 0) {
		/* A shorter answer reads as zeros past its end. */
		memset(cfm, 0, cfm_len);
		memcpy(cfm, sc->sc_cfm, MIN(cfm_len, sc->sc_cfm_len));
	}
	/* USB firmware replies carry a message ID without a sequence number. */
	if (sc->sc_dc && submitted && !sc->sc_cfm_done && error != 0)
		sc->sc_lost_cfm = cfm_id;
	sc->sc_cfm_id = 0;
	sc->sc_cmd_busy = false;
	cv_broadcast(&sc->sc_cv);

	mutex_exit(&sc->sc_lock);

	return error;
}

int
aicwf_cmd(struct aicwf_softc *sc, uint16_t id, const void *param,
    size_t param_len, uint16_t cfm_id, void *cfm, size_t cfm_len)
{
	return aicwf_cmd_reply(sc, id, param, param_len, cfm_id, cfm,
	    cfm_len, false, 0);
}

/* Exact reply lengths are checked under sc_cmd_busy and sc_lock. */
int
aicwf_fw_cmd(struct aicwf_softc *sc, uint16_t id, const void *param,
    size_t param_len, uint16_t cfm_id, void *cfm, size_t cfm_len)
{
	if (!sc->sc_dc && !sc->sc_fw_bip_pending)
		return aicwf_cmd(sc, id, param, param_len, cfm_id, cfm, cfm_len);
	return aicwf_cmd_reply(sc, id, param, param_len, cfm_id, cfm,
	    cfm_len, 2, 0);
}

static void
aicwf_confirm(struct aicwf_softc *sc, uint16_t id, const void *p, size_t len)
{
	mutex_enter(&sc->sc_lock);
	if (sc->sc_lost_cfm != 0 && id == sc->sc_lost_cfm) {
		if (sc->sc_receive_only && id == AICWF_MM_ADD_IF_CFM)
			(void)aicwf_survey_add_reply(sc, p, len, true);
		sc->sc_lost_cfm = 0;
		cv_broadcast(&sc->sc_cv);
	} else if (sc->sc_cmd_busy && id == sc->sc_cfm_id && !sc->sc_cfm_done) {
		memcpy(sc->sc_cfm, p, len);
		sc->sc_cfm_len = len;
		sc->sc_cfm_done = true;
		cv_broadcast(&sc->sc_cv);
	} else {
		device_printf(sc->sc_dev, "unexpected message 0x%04x\n", id);
	}
	mutex_exit(&sc->sc_lock);
}

static void
aicwf_rx_msg(struct aicwf_softc *sc, const uint8_t *msg, size_t len)
{
	uint16_t id, param_len;

	if (!aicwf_reply_decode(msg, len, &id, &param_len))
		return;

	if (sc->sc_receive_only && id != AICWF_SCANU_RESULT_IND &&
	    id != AICWF_SCANU_START_CFM) {
		if ((id == AICWF_MM_GET_MAC_ADDR_CFM || id == AICWF_MM_ADD_IF_CFM ||
		    id == AICWF_MM_REMOVE_IF_CFM || id == AICWF_SCANU_START_ACK) &&
		    param_len <= sizeof(sc->sc_cfm))
			aicwf_confirm(sc, id, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	}
	switch (id) {
	case AICWF_SCANU_RESULT_IND:
		aicwf_scan_result(sc, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	case AICWF_SCANU_START_CFM:
		aicwf_scan_done(sc, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	case AICWF_SM_EXTERNAL_AUTH_IND:
		aicwf_sae_event(sc, true, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	case AICWF_SM_CONNECT_IND:
		aicwf_connect_ind(sc, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	case AICWF_SM_DISCONNECT_IND:
		aicwf_disconnect_ind(sc, msg + AICWF_RX_MSG_PARAM, param_len);
		return;
	case AICWF_MM_CHANNEL_SWITCH_IND:
	case AICWF_MM_CHANNEL_PRE_SWITCH_IND:
	case AICWF_MM_CHANNEL_SURVEY_IND:
		/* The radio moving between channels: nothing to do. */
		return;
	case AICWF_ME_TRAFFIC_IND_REQ:
		/* AP-mode UAPSD housekeeping; a station has nothing to add. */
		return;
	case AICWF_MM_CONNECTION_LOSS_IND:
		device_printf(sc->sc_dev, "firmware reported connection loss\n");
		aicwf_alive_lost(sc);
		return;
	}

	if (param_len <= sizeof(sc->sc_cfm))
		aicwf_confirm(sc, id, msg + AICWF_RX_MSG_PARAM, param_len);
}

/* Split what one read returned into frames. */
void
aicwf_rx(struct aicwf_softc *sc, const uint8_t *buf, size_t len, uint64_t serial)
{
	struct aicwf_rx_frame frame;
	int step;

	while ((step = aicwf_frame_decode(buf, len, &frame)) > 0) {
		if (sc->sc_receive_only && frame.type != AICWF_TYPE_MSG) {
			buf += step;
			len -= step;
			continue;
		}
		if ((frame.type & AICWF_TYPE_CFG) == 0)
			aicwf_rx_data(sc, buf, frame.len, serial);
		else if (frame.type == AICWF_TYPE_TX_CFM)
			aicwf_sta_tx_cfm(sc, buf + AICWF_HDR_LEN, frame.len);
		else if (frame.type == AICWF_TYPE_MSG)
			aicwf_rx_msg(sc, buf + AICWF_HDR_LEN, frame.len);
		buf += step;
		len -= step;
	}
}

/* The DC board profile and the current net80211 power limit both apply. */
static int8_t
aicwf_tx_power(struct aicwf_softc *sc)
{
	if (!sc->sc_dc)
		return AICWF_TX_POWER_DBM;
	if (!sc->sc_if_attached)
		return 15;
	return MIN(15, sc->sc_ic.ic_txpowlimit / 2);
}

static bool
aicwf_dc_channel(struct aicwf_softc *sc, u_int ch, u_int freq)
{
	struct ieee80211com * const ic = &sc->sc_ic;

	if (ch < 1 || ch > 11 || freq != 2407 + 5 * ch)
		return false;
	if (!sc->sc_if_attached)
		return true;
	return isset(ic->ic_chan_active, ch) &&
	    ic->ic_channels[ch].ic_freq == freq &&
	    IEEE80211_IS_CHAN_2GHZ(&ic->ic_channels[ch]);
}

static void
aicwf_chan_set(struct aicwf_softc *sc, uint8_t *chan, u_int band, u_int freq)
{
	le16enc(chan, freq);
	chan[2] = band;
	chan[3] = 0;			/* flags */
	chan[4] = aicwf_tx_power(sc);
}

static u_int
aicwf_chan_list(struct aicwf_softc *sc, uint8_t *chan2g, uint8_t *chan5g,
    u_int *n5g)
{
	static const uint8_t chan5[] = {
		36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120,
		124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165
	};
	u_int n = 0;

	for (u_int ch = 1; ch <= 13; ch++) {
		const u_int freq = 2407 + 5 * ch;
		if (sc->sc_dc && !aicwf_dc_channel(sc, ch, freq))
			continue;
		aicwf_chan_set(sc, chan2g + n * AICWF_CHAN_LEN,
		    AICWF_BAND_2G, freq);
		n++;
	}
	*n5g = 0;
	if (!sc->sc_dc && sc->sc_5ghz) {
		for (*n5g = 0; *n5g < __arraycount(chan5); (*n5g)++)
			aicwf_chan_set(sc, chan5g + *n5g * AICWF_CHAN_LEN,
			    AICWF_BAND_5G, 5000 + 5 * chan5[*n5g]);
	}

	return n;
}

static int
aicwf_add_vif(struct aicwf_softc *sc)
{
	uint8_t req[10], cfm[2];
	int error;

	memset(req, 0, sizeof(req));
	req[0] = AICWF_IF_STA;
	memcpy(req + 2, sc->sc_enaddr, ETHER_ADDR_LEN);
	error = aicwf_fw_cmd(sc, AICWF_MM_ADD_IF_REQ, req, 10,
	    AICWF_MM_ADD_IF_CFM, cfm, 2);
	if (error != 0)
		return error;
	if (cfm[0] != 0) {
		aprint_error_dev(sc->sc_dev, "no interface: status %u\n",
		    cfm[0]);
		return EIO;
	}
	if ((sc->sc_dc || sc->sc_fw_bip_pending) && cfm[1] > 3)
		return EPROTO;
	sc->sc_vif = cfm[1];
	return 0;
}

int
aicwf_station_setup(struct aicwf_softc *sc)
{
	struct aicwf_version fw_version;
	uint8_t req[256], cfm[64];
	u_int n2g, n5g;
	bool bip_mfp;
	int error;

	memset(req, 0, sizeof(req));
	le32enc(req, 1);
	error = aicwf_fw_cmd(sc, AICWF_MM_GET_MAC_ADDR_REQ, req, 4,
	    AICWF_MM_GET_MAC_ADDR_CFM, cfm, ETHER_ADDR_LEN);
	if (error != 0)
		return error;
	if (ETHER_IS_MULTICAST(cfm) ||
	    memcmp(cfm, (uint8_t[ETHER_ADDR_LEN]){ 0 }, ETHER_ADDR_LEN) == 0)
		return EINVAL;
	memcpy(sc->sc_enaddr, cfm, ETHER_ADDR_LEN);
	if (sc->sc_receive_only)
		return 0;
	aprint_normal_dev(sc->sc_dev, "Ethernet address %s\n",
	    ether_sprintf(sc->sc_enaddr));

	error = aicwf_cmd(sc, AICWF_MM_RESET_REQ, NULL, 0,
	    AICWF_MM_RESET_CFM, NULL, 0);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "MM_RESET: error %d\n", error);
		return error;
	}

	error = aicwf_fw_cmd(sc, AICWF_MM_VERSION_REQ, NULL, 0,
	    AICWF_MM_VERSION_CFM, cfm, AICWF_VERSION_LEN);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "MM_VERSION: error %d\n", error);
		return error;
	}
	aicwf_version_decode(cfm, &fw_version);
	aprint_verbose_dev(sc->sc_dev, "MAC 0x%08x, hardware 0x%08x 0x%08x, "
	    "PHY 0x%08x 0x%08x, features 0x%08x, %u stations, %u interfaces\n",
	    fw_version.lmac, fw_version.machw[0], fw_version.machw[1],
	    fw_version.phy[0], fw_version.phy[1], fw_version.features,
	    fw_version.max_sta, fw_version.max_vif);

	sc->sc_fw_version = fw_version.lmac;
	if (sc->sc_dc && fw_version.lmac != 0x06040301)
		return EPROTONOSUPPORT;
	bip_mfp = !sc->sc_dc && fw_version.lmac == 0x06090101 &&
	    (fw_version.features & AICWF_FEATURE_MFP) != 0;
	if (sc->sc_fw_bip_pending && !bip_mfp)
		return EPROTO;
	/* The prepared DC U02 FullMAC reports MFP in its feature word. */
	if (bip_mfp || (sc->sc_dc &&
	    (fw_version.features & AICWF_FEATURE_MFP) != 0)) {
		sc->sc_sae_caps = IEEE80211_SAE_CAP_EXTERNAL |
		    IEEE80211_SAE_CAP_PMF | IEEE80211_SAE_CAP_SA_QUERY;
		aprint_normal_dev(sc->sc_dev, "external SAE and firmware PMF available\n");
	}

	/* Firmware builds association capabilities from this bounded request. */
	if (aicwf_me_config_build(req, fw_version.lmac, fw_version.phy[0],
	    fw_version.features, sc->sc_5ghz))
		aprint_normal_dev(sc->sc_dev,
		    "VHT enabled: 1 stream, MCS 0-9, maximum width 80 MHz\n");
	error = aicwf_cmd(sc, AICWF_ME_CONFIG_REQ, req, AICWF_ME_CONFIG_LEN,
	    AICWF_ME_CONFIG_CFM, NULL, 0);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "ME_CONFIG: error %d\n", error);
		return error;
	}

	memset(req, 0, sizeof(req));
	n2g = aicwf_chan_list(sc, req, req + AICWF_CHAN_2G_MAX *
	    AICWF_CHAN_LEN, &n5g);
	req[AICWF_CHAN_MAX * AICWF_CHAN_LEN] = n2g;
	req[AICWF_CHAN_MAX * AICWF_CHAN_LEN + 1] = n5g;
	error = aicwf_cmd(sc, AICWF_ME_CHAN_CONFIG_REQ, req,
	    AICWF_CHAN_MAX * AICWF_CHAN_LEN + 2,
	    AICWF_ME_CHAN_CONFIG_CFM, NULL, 0);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "ME_CHAN_CONFIG: error %d\n", error);
		return error;
	}

	memset(req, 0, sizeof(req));
	le32enc(req + 64, AICWF_UAPSD_TIMEOUT_MS);
	le16enc(req + 68, AICWF_LP_CLK_PPM);
	error = aicwf_cmd(sc, AICWF_MM_START_REQ, req, 72,
	    AICWF_MM_START_CFM, NULL, 0);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "MM_START: error %d\n", error);
		return error;
	}

	error = aicwf_add_vif(sc);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "MM_ADD_IF: error %d\n", error);
		return error;
	}
	if (sc->sc_fw_bip_pending &&
	    (error = sc->sc_ops->verify(sc)) != 0)
		return error;
	sc->sc_fw_bip_ready = sc->sc_fw_bip_pending &&
	    sc->sc_fw_bip_verified && bip_mfp;

	return 0;
}

static int
aicwf_survey_scan_status(struct aicwf_softc *sc)
{
	int error = 0;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_survey_failed)
		error = EIO;
	else if (sc->sc_survey_cleanup || sc->sc_survey_draining ||
	    sc->sc_lost_cfm != 0)
		error = EBUSY;
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
aicwf_survey_remove_vif(struct aicwf_softc *sc, uint8_t vif)
{
	int error;

	error = aicwf_fw_cmd(sc, AICWF_MM_REMOVE_IF_REQ, &vif, 1,
	    AICWF_MM_REMOVE_IF_CFM, NULL, 0);
	mutex_enter(&sc->sc_lock);
	if (error != 0)
		sc->sc_survey_failed = true;
	sc->sc_survey_cleanup = false;
	mutex_exit(&sc->sc_lock);
	return error;
}

/*
 * Run in the station workqueue. The separate USB RX worker populates the
 * net80211 scan cache and wakes this waiter. Only a user scan creates a VIF.
 */
static void
aicwf_passive_scan(struct aicwf_softc *sc)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	uint16_t freqs[11];
	uint8_t req[AICWF_STA_SCAN_LEN];
	const unsigned limit = mstohz(4000);
	const char *phase = "admission";
	size_t n = 0;
	int error, release;

	for (u_int ch = 1; ch <= __arraycount(freqs); ch++) {
		const u_int freq = 2407 + ch * 5;
		if (aicwf_dc_channel(sc, ch, freq))
			freqs[n++] = freq;
	}
	error = aicwf_survey_scan_status(sc);
	if (error != 0)
		goto done;
	if (n == 0) {
		phase = "channel selection";
		error = EINVAL;
		goto done;
	}
	phase = "MM_ADD_IF";
	error = aicwf_add_vif(sc);
	if (error != 0)
		goto done;
	(void)aicwf_sta_scan_build(req, freqs, n, sc->sc_vif, 0);
	mutex_enter(&sc->sc_lock);
	sc->sc_scanning = true;
	sc->sc_survey_ok = false;
	sc->sc_scan_published = false;
	mutex_exit(&sc->sc_lock);
	phase = "SCANU_START";
	error = aicwf_cmd(sc, AICWF_SCANU_START_REQ, req, sizeof(req),
	    AICWF_SCANU_START_ACK, NULL, 0);
	mutex_enter(&sc->sc_lock);
	const unsigned start = getticks();
	if (error == 0)
		phase = "scan completion";
	while (error == 0 && sc->sc_scanning && !atomic_load_relaxed(&sc->sc_dying)) {
		const unsigned elapsed = (unsigned)getticks() - start;
		error = elapsed >= limit ? ETIMEDOUT :
		    cv_timedwait(&sc->sc_cv, &sc->sc_lock, limit - elapsed);
	}
	/* Late scan events have no generation. Do not open a replacement VIF. */
	if (sc->sc_scanning && sc->sc_scan_published)
		sc->sc_survey_draining = true;
	if (error == 0 && !sc->sc_survey_ok)
		error = EIO;
	sc->sc_scanning = false;
	mutex_exit(&sc->sc_lock);
	release = aicwf_survey_remove_vif(sc, sc->sc_vif);
	if (release != 0) {
		if (error == 0) {
			phase = "MM_REMOVE_IF";
			error = release;
		} else
			aprint_error_dev(sc->sc_dev, "MM_REMOVE_IF failed: %d\n",
			    release);
	}
done:
	if (!atomic_load_relaxed(&sc->sc_dying)) {
		if (error != 0)
			aprint_error_dev(sc->sc_dev, "passive scan %s failed: %d\n",
			    phase, error);
		int s = splnet();
		if (error != 0)
			ieee80211_node_table_reset(&ic->ic_scan);
		sc->sc_survey_result_failed = error != 0;
		ieee80211_end_scan(ic);
		splx(s);
	}
}

/* Scan every channel; the networks arrive as indications. */
static void
aicwf_scan(struct aicwf_softc *sc)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	const size_t chans = AICWF_CHAN_MAX * AICWF_CHAN_LEN;
	uint8_t req[376];
	u_int n2g, n5g, nssid;

	memset(req, 0, sizeof(req));
	n2g = aicwf_chan_list(sc, req, req + 13 * AICWF_CHAN_LEN, &n5g);
	if (n2g == 0 && n5g == 0)
		return;
	/* The wildcard, and the wanted network in case it is hidden. */
	nssid = 1;
	if (ic->ic_des_esslen != 0 && ic->ic_des_esslen <= 32) {
		req[chans + 33] = ic->ic_des_esslen;
		memcpy(req + chans + 34, ic->ic_des_essid, ic->ic_des_esslen);
		nssid++;
	}
	memset(req + chans + 100, 0xff, ETHER_ADDR_LEN);  /* any BSSID */
	req[chans + 114] = sc->sc_vif;
	req[chans + 115] = n2g + n5g;
	req[chans + 116] = nssid;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_scanning || sc->sc_sta_scan_pending ||
	    sc->sc_sta_cancel_queued) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	sc->sc_scanning = true;
	sc->sc_scan_published = false;
	sc->sc_scan_ticks = getticks();
	mutex_exit(&sc->sc_lock);
	if (aicwf_cmd(sc, AICWF_SCANU_START_REQ, req, sizeof(req),
	    AICWF_SCANU_START_ACK, NULL, 0) != 0) {
		mutex_enter(&sc->sc_lock);
		if (!sc->sc_scan_published)
			sc->sc_scanning = false;
		device_printf(sc->sc_dev,
		    "scan request failed; FIFO attempted %u, pending %u\n",
		    sc->sc_scan_published, sc->sc_scanning);
		mutex_exit(&sc->sc_lock);
	}
}

/* One network found by a scan: a beacon or a probe response. */
static void
aicwf_scan_result(struct aicwf_softc *sc, const uint8_t *ind, size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	const size_t hdr = 12, fixed = 12;
	struct ieee80211_scanparams scan;
	struct ieee80211_beacon_ies ies;
	struct ieee80211_frame wh;
	uint8_t tstamp[8];
	const uint8_t *frm, *efrm;
	u_int ds_chan = 0, ht_chan = 0, primary;
	int rssi, s;

	if (!sc->sc_if_attached || !sc->sc_scanning ||
	    !aicwf_sta_scan_result_valid(ind, len, sc->sc_vif))
		return;
	mutex_enter(&sc->sc_lock);
	const bool admitted = !sc->sc_sta_scan_started ||
	    (sc->sc_sta_scan_pending && !sc->sc_sta_scan_draining &&
	    sc->sc_sta_scan.generation == (uint32_t)sc->sc_sae_epoch);
	const uint64_t epoch = sc->sc_sae_epoch;
	const uint32_t serial = sc->sc_sta_scan_serial;
	mutex_exit(&sc->sc_lock);
	if (!admitted)
		return;
	const uint8_t * const frame = ind + hdr;
	const size_t frame_len = le16dec(ind);
	if (frame_len < sizeof(wh) + fixed)
		return;

	memcpy(&wh, frame, sizeof(wh));
	const int subtype = wh.i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if ((wh.i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
	    IEEE80211_FC0_TYPE_MGT ||
	    (subtype != IEEE80211_FC0_SUBTYPE_BEACON &&
	    subtype != IEEE80211_FC0_SUBTYPE_PROBE_RESP))
		return;

	memset(&scan, 0, sizeof(scan));
	frm = frame + sizeof(wh);
	efrm = frame + frame_len;
	memcpy(tstamp, frm, sizeof(tstamp));
	scan.sp_tstamp = tstamp;
	scan.sp_bintval = le16dec(frm + 8);
	scan.sp_capinfo = le16dec(frm + 10);
	const u_int chan = ieee80211_mhz2ieee(le16dec(ind + 4), 0);
	if (chan > IEEE80211_CHAN_MAX ||
	    ic->ic_channels[chan].ic_freq != le16dec(ind + 4) ||
	    !isset(ic->ic_chan_active, chan))
		return;
	scan.sp_chan = scan.sp_bchan = chan;

	const uint8_t *mdie = NULL;

	frm += fixed;
	if (ieee80211_beacon_ies_parse(&ies, frm, efrm - frm) != 0)
		return;
	scan.sp_ssid = __UNCONST(ies.ssid_valid);
	scan.sp_rates = __UNCONST(ies.rates_valid);
	scan.sp_country = __UNCONST(ies.country);
	scan.sp_tim = __UNCONST(ies.tim);
	scan.sp_xrates = __UNCONST(ies.xrates);
	scan.sp_wpa = __UNCONST(ies.rsn);
	scan.sp_rrm = __UNCONST(ies.rrm);
	scan.sp_extcap = __UNCONST(ies.extcap);
	scan.sp_rsnx = __UNCONST(ies.rsnx);
	scan.sp_erp = ies.erp != NULL ? ies.erp[2] : 0;
	scan.sp_phy_ies = ies.phy;
	/* Preserve the firmware adapter's fixed-body-relative offset. */
	scan.sp_timoff = ies.tim != NULL ? fixed + ies.timoff : 0;
	ds_chan = ies.ds_exact != NULL ? ies.ds_exact[2] : 0;
	ht_chan = ies.ht != NULL ? ies.ht[2] : 0;
	mdie = ies.mdie;
	if (scan.sp_ssid == NULL || scan.sp_rates == NULL)
		return;

	/* A 2.4 GHz beacon can be received on an adjacent scan channel. */
	primary = le16dec(ind + 4) < 3000 && ds_chan != 0 ? ds_chan :
	    ht_chan != 0 ? ht_chan : chan;
	if (primary > IEEE80211_CHAN_MAX ||
	    ic->ic_channels[primary].ic_freq == 0 ||
	    !isset(ic->ic_chan_active, primary) ||
	    (ic->ic_channels[primary].ic_freq < 3000) !=
	    (ic->ic_channels[chan].ic_freq < 3000))
		return;
	if (primary != chan)
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_STATE,
		    "scan primary channel %u received on %u\n", primary, chan);
	scan.sp_chan = primary;

	/* The firmware reports dBm; net80211 compares a positive scale. */
	scan.sp_rssi_dbm = (int8_t)ind[9];
	scan.sp_rssi_dbm_valid = true;
	rssi = (int8_t)ind[9] + 100;
	rssi = MAX(1, MIN(rssi, 100));

	mutex_enter(&sc->sc_lock);
	s = splnet();
	if (sc->sc_scanning && epoch == sc->sc_sae_epoch &&
	    serial == sc->sc_sta_scan_serial &&
	    (!sc->sc_sta_scan_started || (sc->sc_sta_scan_pending &&
	    !sc->sc_sta_scan_draining)) && (ic->ic_flags & IEEE80211_F_SCAN) != 0) {
		if (mdie != NULL) {
			struct aicwf_ft_bss *ft = NULL;
			for (u_int k = 0; k < __arraycount(sc->sc_ft_bss); k++) {
				if (IEEE80211_ADDR_EQ(sc->sc_ft_bss[k].bssid, wh.i_addr3)) {
					ft = &sc->sc_ft_bss[k];
					break;
				}
				if (ft == NULL && sc->sc_ft_bss[k].mdid == 0)
					ft = &sc->sc_ft_bss[k];
			}
			if (ft != NULL) {
				memcpy(ft->bssid, wh.i_addr3, ETHER_ADDR_LEN);
				ft->mdid = le16dec(mdie + 2);
				ft->r0kh_len = MIN((size_t)mdie[1] - 3, sizeof(ft->r0kh));
				memcpy(ft->r0kh, mdie + 5, ft->r0kh_len);
			}
		}
		ieee80211_add_scan(ic, &scan, &wh, subtype, rssi, 0);
	}
	splx(s);
	mutex_exit(&sc->sc_lock);
}

static void
aicwf_scan_done(struct aicwf_softc *sc, const uint8_t *p, size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211req_sta_mgmt scan_req;
	bool background, aborted = false;
	int s;

	if (!sc->sc_if_attached || !aicwf_sta_scan_complete_valid(p, len, sc->sc_vif))
		return;
	mutex_enter(&sc->sc_lock);
	if (!sc->sc_scanning) {
		if (sc->sc_receive_only)
			sc->sc_survey_draining = false;
		mutex_exit(&sc->sc_lock);
		return;
	}
	if (sc->sc_receive_only) {
		sc->sc_survey_ok = p[1] == 0;
		sc->sc_scanning = false;
		cv_broadcast(&sc->sc_cv);
		mutex_exit(&sc->sc_lock);
		return;
	}
	background = sc->sc_sta_scan_started;
	if (background) {
		aborted = p[1] != 0 ||
		    sc->sc_sta_scan.generation != (uint32_t)sc->sc_sae_epoch;
		device_printf(sc->sc_dev,
		    "station scan complete: generation %u cookie %u serial %u status %u results %u draining %u\n",
		    sc->sc_sta_scan.generation, sc->sc_sta_scan.cookie, sc->sc_sta_scan_serial,
		    p[1], p[2], sc->sc_sta_scan_draining);
		(void)aicwf_sta_scan_finish_locked(sc);
		scan_req = sc->sc_sta_scan;
		sc->sc_sta_scan_started = false;
		sc->sc_scan_published = false;
		sc->sc_sta_scan_draining = false;
	}
	sc->sc_scanning = false;
	mutex_exit(&sc->sc_lock);
	s = splnet();
	if (ic->ic_state == IEEE80211_S_SCAN) {
		if (background)
			(void)aicwf_newstate(ic, IEEE80211_S_SCAN, -1);
		else
			ieee80211_end_scan(ic);
	}
	splx(s);
	if (background) {
		s = splnet();
		(void)ieee80211_sta_scan_report(ic, &scan_req, aborted);
		(void)ieee80211_sta_scan_release(ic, &scan_req);
		splx(s);
	}
}

/* Serialize retirement with the final check and write of a bound command. */
static uint64_t
aicwf_sae_advance_if(struct aicwf_softc *sc, uint64_t expected)
{
	uint8_t tx[AICWF_STA_NOTICE_LEN];
	struct ieee80211req_sta_mgmt scan_req;
	bool tx_event, scan_event, scan_release;
	uint64_t epoch;

	mutex_enter(&sc->sc_lock);
	if (expected != 0 && expected != sc->sc_sae_epoch) {
		mutex_exit(&sc->sc_lock);
		return 0;
	}
	aicwf_sta_retire_locked(sc, tx, &tx_event, &scan_event);
	scan_req = sc->sc_sta_scan;
	scan_release = scan_event && !sc->sc_sta_scan_started;
	aicwf_rx_retire(sc);
	mutex_spin_enter(&sc->sc_epoch_lock);
	sc->sc_ptk_valid = false;
	sc->sc_pmf_rx_pn = 0;
	explicit_memset(sc->sc_ptk, 0, sizeof(sc->sc_ptk));
	if (++sc->sc_sae_epoch == 0)
		sc->sc_sae_epoch++;
	epoch = sc->sc_sae_epoch;
	mutex_spin_exit(&sc->sc_epoch_lock);
	mutex_exit(&sc->sc_lock);
	if (tx_event)
		rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_TX, tx, sizeof(tx));
	if (scan_event) {
		int s = splnet();
		(void)ieee80211_sta_scan_report(&sc->sc_ic, &scan_req, true);
		splx(s);
	}
	if (scan_release) {
		int s = splnet();
		(void)ieee80211_sta_scan_release(&sc->sc_ic, &scan_req);
		splx(s);
	}
	return epoch;
}

static void
aicwf_sae_advance(struct aicwf_softc *sc)
{
	(void)aicwf_sae_advance_if(sc, 0);
}

/*
 * Firmware protocol references: Radxa's AIC8800 SDIO driver, revision
 * d13d07963cd15d731e2895e8288a04cca6152ac9, lmac_msg.h and rwnx_tx.c.
 * This is an independent BSD implementation of that wire interface.
 */
static void
aicwf_sae_event(struct aicwf_softc *sc, bool start, const uint8_t *data,
    size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211req_sae *req;

	sc->sc_ev_sae_drop.ev_count++;
	if (!sc->sc_if_attached || !sc->sc_sae_enabled ||
	    !sc->sc_connecting || ic->ic_state != IEEE80211_S_AUTH ||
	    ic->ic_bss == NULL ||
	    !IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, sc->sc_sae_bssid))
		return;
	if (start) {
		if (len >= 44)
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_STATE,
			    "external SAE request: len %zu vif %u AKM %02x%02x%02x%02x\n",
			    len, data[0], data[40], data[41], data[42], data[43]);
		if (!aicwf_sae_request_valid(data, len, sc->sc_vif,
		    sc->sc_sae_bssid, ic->ic_bss->ni_essid,
		    ic->ic_bss->ni_esslen))
			return;
		len = data[1];
		data += 2;
	} else {
		if (!sc->sc_sae_pending ||
		    !aicwf_sae_frame_valid(data, len, ic->ic_myaddr,
		    sc->sc_sae_bssid, sc->sc_sae_bssid))
			return;
	}
	/* Do not relabel a checked event after sleeping through a new join. */
	req = kmem_zalloc(sizeof(*req), KM_NOSLEEP);
	if (req == NULL)
		return;
	if (start) {
		sc->sc_sae_pending = true;
		sc->sc_ev_sae_start.ev_count++;
	} else {
		sc->sc_ev_sae_rx.ev_count++;
	}
	req->version = IEEE80211_SAE_VERSION;
	req->generation = sc->sc_sae_generation;
	req->op = start ? IEEE80211_SAE_START : IEEE80211_SAE_RX_FRAME;
	req->len = len;
	memcpy(req->bssid, sc->sc_sae_bssid, ETHER_ADDR_LEN);
	memcpy(req->data, data, len);
	rt_ieee80211msg(ic->ic_ifp, RTM_IEEE80211_SAE, req,
	    offsetof(struct ieee80211req_sae, data) + len);
	kmem_free(req, sizeof(*req));
	sc->sc_ev_sae_drop.ev_count--;
}

static int
aicwf_sae_tx(struct aicwf_softc *sc, const uint8_t *frame, size_t len,
    uint64_t epoch, bool query)
{
	uint8_t * const buf = sc->sc_txbuf;
	uint8_t * const desc = buf + AICWF_HDR_LEN;
	int error;

	mutex_enter(&sc->sc_lock);
	if (epoch != sc->sc_sae_epoch ||
	    (query && (!sc->sc_ptk_valid || !sc->sc_connected ||
	    !sc->sc_sae_authenticated || sc->sc_ic.ic_state != IEEE80211_S_RUN))) {
		mutex_exit(&sc->sc_lock);
		return ESTALE;
	}
	le16enc(buf, AICWF_TXDESC_LEN + len);
	buf[2] = AICWF_TYPE_DATA;
	memset(desc, 0, AICWF_TXDESC_LEN);
	le16enc(desc, len);
	memcpy(desc + 8, frame + 4, ETHER_ADDR_LEN);
	memcpy(desc + 14, frame + 10, ETHER_ADDR_LEN);
	desc[22] = AICWF_HWQ_VO;
	desc[23] = AICWF_TID_NONE;
	desc[24] = sc->sc_vif;
	desc[25] = query ? sc->sc_ap : AICWF_STA_NONE;
	/* Bit 7 asks firmware to protect robust management with the PTK. */
	le16enc(desc + 26, AICWF_TX_MGMT |
	    (query ? AICWF_TX_MGMT_ROBUST : 0));
	memcpy(desc + AICWF_TXDESC_LEN, frame, len);
	error = aicwf_write(sc, AICWF_HDR_LEN + AICWF_TXDESC_LEN + len,
	    AICWF_TX_RESERVE);
	mutex_exit(&sc->sc_lock);
	if (error == 0)
		sc->sc_ev_sae_tx.ev_count++;
	return error;
}

/* A command can sleep while a disconnect retires the current join. */
static bool
aicwf_sae_current(struct aicwf_softc *sc, const struct ieee80211req_sae *req,
    uint64_t epoch)
{
	return sc->sc_sae_enabled && sc->sc_sae_epoch == epoch &&
	    sc->sc_ic.ic_bss != NULL && req->generation == sc->sc_sae_generation &&
	    IEEE80211_ADDR_EQ(req->bssid, sc->sc_sae_bssid) &&
	    IEEE80211_ADDR_EQ(req->bssid, sc->sc_ic.ic_bss->ni_bssid);
}

static int
aicwf_sae(struct ieee80211com *ic, const struct ieee80211req_sae *req)
{
	struct aicwf_softc *sc = ic->ic_ifp->if_softc;
	uint8_t wire[44], cfm[2] = { 0xff };
	uint64_t epoch;
	uint16_t index;
	int error = EINVAL;

	if (req->op == IEEE80211_SAE_CONFIGURE) {
		error = ieee80211_sae_configure_check(ic, sc->sc_sae_caps,
		    req->data[0] != 0);
		if (error != 0)
			goto out;
		aicwf_sae_advance(sc);
		mutex_spin_enter(&sc->sc_epoch_lock);
		sc->sc_configure_serial++;
		mutex_spin_exit(&sc->sc_epoch_lock);
		sc->sc_sae_enabled = req->data[0] != 0;
		sc->sc_sae_pending = false;
		sc->sc_sae_authenticated = false;
		sc->sc_sae_generation = req->generation;
		memcpy(sc->sc_sae_bssid, req->bssid, ETHER_ADDR_LEN);
		error = 0;
		goto out;
	}
	if (!sc->sc_sae_enabled || ic->ic_bss == NULL ||
	    req->generation != sc->sc_sae_generation ||
	    !IEEE80211_ADDR_EQ(req->bssid, sc->sc_sae_bssid) ||
	    !IEEE80211_ADDR_EQ(req->bssid, ic->ic_bss->ni_bssid)) {
		error = ESTALE;
		goto out;
	}
	epoch = sc->sc_sae_epoch;
	memset(wire, 0, sizeof(wire));
	switch (req->op) {
	case IEEE80211_SAE_AUTH_STATUS:
		if (req->len != 2 || !sc->sc_sae_pending ||
		    !sc->sc_connecting || ic->ic_state != IEEE80211_S_AUTH)
			break;
		wire[0] = sc->sc_vif;
		le16enc(wire + 2, le16dec(req->data));
		/* A successful association can arrive before the confirmation. */
		sc->sc_sae_pending = false;
		sc->sc_sae_authenticated = le16dec(req->data) == 0;
		error = aicwf_cmd_reply(sc, AICWF_SM_EXTERNAL_AUTH_RSP, wire, 4,
		    AICWF_SM_EXTERNAL_AUTH_CFM, NULL, 0, true, epoch);
		if (!aicwf_sae_current(sc, req, epoch))
			error = ESTALE;
		else if (error != 0)
			sc->sc_sae_authenticated = false;
		break;
	case IEEE80211_SAE_TX_FRAME:
		if (!sc->sc_sae_pending || !sc->sc_connecting ||
		    ic->ic_state != IEEE80211_S_AUTH ||
		    !aicwf_sae_frame_valid(req->data, req->len, req->bssid,
		    ic->ic_myaddr, req->bssid))
			break;
		error = aicwf_sae_tx(sc, req->data, req->len, epoch, false);
		break;
	case IEEE80211_SAE_TX_SA_QUERY:
		if (!sc->sc_connected || !sc->sc_sae_authenticated ||
		    ic->ic_state != IEEE80211_S_RUN ||
		    !aicwf_pmf_query_valid(req->data, req->len, req->bssid,
		    ic->ic_myaddr, req->bssid))
			break;
		error = aicwf_sae_tx(sc, req->data, req->len, epoch, true);
		break;
	case IEEE80211_SAE_SET_IGTK:
	case IEEE80211_SAE_DELETE_IGTK:
		if (req->len != (req->op == IEEE80211_SAE_SET_IGTK ? 24 : 2))
			break;
		index = le16dec(req->data);
		if (index != 4 && index != 5)
			break;
		if (req->op == IEEE80211_SAE_DELETE_IGTK) {
			if (sc->sc_igtk[index - 4] == AICWF_HWKEY_NONE) {
				error = 0;
				break;
			}
			wire[0] = sc->sc_igtk[index - 4];
			error = aicwf_cmd_reply(sc, AICWF_MM_KEY_DEL_REQ, wire, 1,
			    AICWF_MM_KEY_DEL_CFM, NULL, 0, true, epoch);
			if (!aicwf_sae_current(sc, req, epoch) ||
			    sc->sc_igtk[index - 4] != wire[0])
				error = ESTALE;
			else if (error == 0)
				sc->sc_igtk[index - 4] = AICWF_HWKEY_NONE;
			break;
		}
		if (!sc->sc_connected || !sc->sc_sae_authenticated ||
		    ic->ic_state != IEEE80211_S_RUN)
			break;
		if (!aicwf_sae_igtk_wire(wire, req->data, req->len,
		    sc->sc_vif, sc->sc_fw_bip_ready)) {
			device_printf(sc->sc_dev, "nonzero initial IGTK IPN is unsupported\n");
			error = EOPNOTSUPP;
			break;
		}
		error = aicwf_cmd_reply(sc, AICWF_MM_KEY_ADD_REQ, wire, sizeof(wire),
		    AICWF_MM_KEY_ADD_CFM, cfm, sizeof(cfm), true, epoch);
		if (!aicwf_sae_current(sc, req, epoch) ||
		    !sc->sc_connected || !sc->sc_sae_authenticated ||
		    ic->ic_state != IEEE80211_S_RUN)
			error = ESTALE;
		if (error == 0 && (cfm[0] != 0 || cfm[1] == AICWF_HWKEY_NONE))
			error = EIO;
		if (error == 0)
			sc->sc_igtk[index - 4] = cfm[1];
		break;
	default:
		break;
	}
	explicit_memset(wire, 0, sizeof(wire));
out:
	return error;
}

static int
aicwf_sae_ioctl(struct aicwf_softc *sc, u_long cmd,
    struct ieee80211req *ireq)
{
	return ieee80211_ioctl_sae(&sc->sc_ic, cmd, ireq, sc->sc_sae_caps,
	    aicwf_sae);
}

/* Ask the firmware to join the network net80211 has chosen. */
static void
aicwf_connect(struct aicwf_softc *sc)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211_node * const ni = ic->ic_bss;
	uint8_t req[320], cfm[1] = { 0xff };
	uint32_t flags = 0;
	uint64_t epoch;
	bool reassoc, open, controlled;
	int error;

	KASSERT(IFNET_LOCKED(&sc->sc_if));
	if (ni == NULL || ni->ni_esslen == 0 || ni->ni_esslen > 32 ||
	    ni->ni_chan == IEEE80211_CHAN_ANYC)
		return;
	if (sc->sc_dc && (!IEEE80211_IS_CHAN_2GHZ(ni->ni_chan) ||
	    !aicwf_dc_channel(sc, ieee80211_mhz2ieee(ni->ni_chan->ic_freq, 0),
	    ni->ni_chan->ic_freq)))
		return;

	memset(req, 0, sizeof(req));
	req[0] = ni->ni_esslen;
	memcpy(req + 1, ni->ni_essid, ni->ni_esslen);
	memcpy(req + 34, ni->ni_bssid, ETHER_ADDR_LEN);
	aicwf_chan_set(sc, req + 40, IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ?
	    AICWF_BAND_5G : AICWF_BAND_2G, ni->ni_chan->ic_freq);
	if ((ic->ic_flags & IEEE80211_F_WPA) != 0) {
		/* The supplicant does the handshake and then opens the port. */
		flags |= AICWF_CONNECT_PORT_HOST | AICWF_CONNECT_WPA;
		be16enc(req + 52, ETHERTYPE_PAE);
		if (ic->ic_opt_ie != NULL && ic->ic_opt_ie_len <= 256) {
			le16enc(req + 54, ic->ic_opt_ie_len);
			memcpy(req + 64, ic->ic_opt_ie, ic->ic_opt_ie_len);
		}
	}
	if (sc->sc_sae_enabled) {
		if (ic->ic_opt_ie == NULL ||
		    !ieee80211_sae_rsn_valid(ic->ic_opt_ie, ic->ic_opt_ie_len) ||
		    !IEEE80211_ADDR_EQ(sc->sc_sae_bssid, ni->ni_bssid)) {
			device_printf(sc->sc_dev, "SAE requires CCMP, required PMF and the selected peer\n");
			return;
		}
		flags |= AICWF_CONNECT_MFP;
	}
	req[59] = sc->sc_sae_enabled ? AICWF_AUTH_SAE : IEEE80211_AUTH_ALG_OPEN;
	req[61] = sc->sc_vif;

	mutex_enter(&sc->sc_lock);
	reassoc = sc->sc_connected;
	mutex_exit(&sc->sc_lock);
	/* The native firmware protocol retains the old AP until reassociation. */
	if (reassoc)
		flags |= AICWF_CONNECT_REASSOC;
	le32enc(req + 48, flags);
	/* Bind admission to these request bytes and the selected BSS. */
	controlled = (flags & (AICWF_CONNECT_WPA | AICWF_CONNECT_PORT_HOST)) ==
	    (AICWF_CONNECT_WPA | AICWF_CONNECT_PORT_HOST);
	open = (flags & (AICWF_CONNECT_WPA | AICWF_CONNECT_PORT_HOST |
	    AICWF_CONNECT_MFP)) == 0 && (ic->ic_flags & IEEE80211_F_PRIVACY) == 0 &&
	    (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) == 0;
	epoch = aicwf_sae_advance_if(sc, 0);
	mutex_enter(&sc->sc_lock);
	if (epoch != sc->sc_sae_epoch) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	sc->sc_sae_pending = false;
	sc->sc_sae_authenticated = false;
	sc->sc_connecting = true;
	mutex_spin_enter(&sc->sc_epoch_lock);
	sc->sc_rx_join = sc->sc_rx_serial;
	sc->sc_rx_vif = req[61];
	memcpy(sc->sc_rx_bssid, req + 34, ETHER_ADDR_LEN);
	mutex_spin_exit(&sc->sc_epoch_lock);
	mutex_exit(&sc->sc_lock);
	error = aicwf_cmd_reply(sc, AICWF_SM_CONNECT_REQ, req, sizeof(req),
	    AICWF_SM_CONNECT_CFM, cfm, sizeof(cfm), true, epoch);
	mutex_enter(&sc->sc_lock);
	if (epoch != sc->sc_sae_epoch) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	if (error != 0 || cfm[0] != 0) {
		/* net80211 times the join out and scans again. */
		device_printf(sc->sc_dev, "connect request failed: %d, %u\n",
		    error, cfm[0]);
		sc->sc_connecting = false;
		aicwf_rx_retire(sc);
	} else {
		mutex_spin_enter(&sc->sc_epoch_lock);
		sc->sc_join_accepted = controlled || open;
		sc->sc_join_open = open;
		mutex_spin_exit(&sc->sc_epoch_lock);
		/* New IGTK requests cannot run until IFNET is released. */
		memset(sc->sc_igtk, AICWF_HWKEY_NONE, sizeof(sc->sc_igtk));
	}
	mutex_exit(&sc->sc_lock);
}

/* Print only protocol metadata from the successful current-peer indication. */
static void
aicwf_assoc_report(struct aicwf_softc *sc, const uint8_t *ind, size_t len)
{
	struct aicwf_assoc_info info;

	if (sc->sc_fw_version != 0x06090101 || len < 13 ||
	    ind[9] != sc->sc_vif ||
	    !IEEE80211_ADDR_EQ(ind + 2, sc->sc_ic.ic_bss->ni_bssid))
		return;
	if (!aicwf_assoc_parse(ind, len, &info)) {
		device_printf(sc->sc_dev, "association metadata unavailable: len %zu\n",
		    len);
		return;
	}
	device_printf(sc->sc_dev,
	    "association VHT IEs: request %u cap 0x%08x RX/TX 0x%04x/0x%04x; "
	    "response %u cap 0x%08x RX/TX 0x%04x/0x%04x\n",
	    info.req.vht, info.req.cap, info.req.rx_map, info.req.tx_map,
	    info.rsp.vht, info.rsp.cap, info.rsp.rx_map, info.rsp.tx_map);
	if (info.rsp.operation)
		device_printf(sc->sc_dev,
		    "association VHT operation: width %u, center segments %u/%u\n",
		    info.rsp.op_width, info.rsp.seg0, info.rsp.seg1);
	if (info.channel)
		device_printf(sc->sc_dev,
		    "association firmware channel: band %u, primary %u MHz, "
		    "width code %u, centers %u/%u MHz\n",
		    info.band, info.primary, info.width, info.center1, info.center2);
	else
		device_printf(sc->sc_dev, "association firmware channel unavailable\n");
}

static void
aicwf_sta_assoc(struct aicwf_softc *sc, const uint8_t *ind, size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct aicwf_assoc_info info;
	size_t request, response;
	u_int channel;

	mutex_enter(&sc->sc_lock);
	sc->sc_sta_assoc_len = 0;
	if (len < 20 || ind[9] != sc->sc_vif || le16dec(ind) != 0 ||
	    ic->ic_bss == NULL || !IEEE80211_ADDR_EQ(ind + 2, ic->ic_bss->ni_bssid) ||
	    !aicwf_assoc_parse(ind, len, &info))
		goto out;
	request = le16dec(ind + 14);
	response = le16dec(ind + 16);
	if (request == 0 || response == 0)
		goto out;
	le16enc(sc->sc_sta_assoc, request);
	le16enc(sc->sc_sta_assoc + 2, response);
	memcpy(sc->sc_sta_assoc + 4, ind + 20, request + response);
	sc->sc_sta_assoc_len = 4 + request + response;
	if (info.channel && info.band <= 1 &&
	    (info.band == 0 ? info.primary < 3000 : info.primary >= 4000)) {
		channel = ieee80211_mhz2ieee(info.primary, 0);
		if (channel > 0 && channel <= IEEE80211_CHAN_MAX &&
		    ic->ic_channels[channel].ic_freq == info.primary &&
		    isset(ic->ic_chan_active, channel))
			ic->ic_bss->ni_chan = &ic->ic_channels[channel];
	}
	device_printf(sc->sc_dev, "station association IEs: request %zu response %zu generation %u\n",
	    request, response, (uint32_t)sc->sc_sae_epoch);
out:
	mutex_exit(&sc->sc_lock);
}

static void
aicwf_connect_ind(struct aicwf_softc *sc, const uint8_t *ind, size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	int s;

	if (!sc->sc_if_attached || len < 13 || ind[9] != sc->sc_vif)
		return;
	if ((!sc->sc_connecting || ic->ic_bss == NULL ||
	    !IEEE80211_ADDR_EQ(ind + 2, ic->ic_bss->ni_bssid)) && le16dec(ind) == 0) {
		if (sc->sc_connected) {
			device_printf(sc->sc_dev, "unsolicited firmware association: roamed %u; reconnecting through host\n", ind[8]);
			aicwf_alive_lost(sc);
		}
		return;
	}
	if (!sc->sc_connecting || ic->ic_bss == NULL ||
	    !IEEE80211_ADDR_EQ(ind + 2, ic->ic_bss->ni_bssid) ||
	    (sc->sc_sae_enabled && !IEEE80211_ADDR_EQ(ind + 2, sc->sc_sae_bssid)))
		return;
	sc->sc_connecting = false;

	const u_int status = le16dec(ind);
	s = splnet();
	if (status == 0 && sc->sc_sae_enabled && !sc->sc_sae_authenticated) {
		device_printf(sc->sc_dev, "association before SAE completion rejected\n");
		sc->sc_connecting = true;
		ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
		splx(s);
		return;
	}
	if (status == 0) {
		/* An indication can precede CONNECT_CFM; no new key is installed yet. */
		memset(sc->sc_igtk, AICWF_HWKEY_NONE, sizeof(sc->sc_igtk));
		aicwf_assoc_report(sc, ind, len);
		aicwf_sta_assoc(sc, ind, len);
		sc->sc_ap = ind[10];
		sc->sc_qos = ind[12] != 0;
		if (!aicwf_rx_begin(sc)) {
			splx(s);
			return;
		}
		sc->sc_connected = true;
		sc->sc_alive_rx = ticks;
		sc->sc_alive_misses = 0;
		for (u_int k = 0; k < __arraycount(sc->sc_ft_bss); k++) {
			if (sc->sc_ft_bss[k].mdid != 0 && IEEE80211_ADDR_EQ(
			    sc->sc_ft_bss[k].bssid, ic->ic_bss->ni_bssid)) {
				device_printf(sc->sc_dev,
				    "802.11r mobility domain 0x%04x, R0KH len %u\n",
				    sc->sc_ft_bss[k].mdid,
				    sc->sc_ft_bss[k].r0kh_len);
				break;
			}
		}
		/*
		 * net80211 tells the supplicant about the association
		 * only on the way from ASSOC to RUN.
		 */
		if (ic->ic_state == IEEE80211_S_AUTH)
			ieee80211_new_state(ic, IEEE80211_S_ASSOC, -1);
		if (ic->ic_state == IEEE80211_S_AUTH ||
		    ic->ic_state == IEEE80211_S_ASSOC)
			ieee80211_new_state(ic, IEEE80211_S_RUN,
			    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	} else {
		device_printf(sc->sc_dev, "connection refused: status %u\n",
		    status);
		if (ic->ic_state == IEEE80211_S_AUTH ||
		    ic->ic_state == IEEE80211_S_ASSOC)
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	}
	splx(s);
}

/* Firmware routes measurement requests as well as neighbor reports. */
static bool
aicwf_sta_action_valid(const uint8_t *body, size_t len, bool to_ap)
{
	return ieee80211_sta_action_valid(body, len, to_ap,
	    IEEE80211_STA_ACTION_BTM | IEEE80211_STA_ACTION_NR |
	    IEEE80211_STA_ACTION_RM_REPORT | IEEE80211_STA_ACTION_RM_REQUEST,
	    IEEE80211_STA_MAX_BODY);
}

/* The checked SDIO firmware ABI is separate from the current security mode. */
static bool
aicwf_sta_supported(struct aicwf_softc *sc)
{
	return sc->sc_if_attached && (sc->sc_fw_version == 0x06090101 ||
	    (sc->sc_dc && sc->sc_fw_version == 0x06040301)) &&
	    sc->sc_ic.ic_opmode == IEEE80211_M_STA;
}

static bool
aicwf_sta_security(struct aicwf_softc *sc)
{
	struct ieee80211com * const ic = &sc->sc_ic;

	if (sc->sc_sae_enabled &&
	    (sc->sc_sae_caps & IEEE80211_SAE_CAP_PMF) == 0)
		return false;
	return (sc->sc_sae_enabled ||
	    (ic->ic_flags & IEEE80211_F_WPA) == IEEE80211_F_WPA2) &&
	    ieee80211_sta_rsn_valid(ic->ic_opt_ie, ic->ic_opt_ie_len,
	    sc->sc_sae_enabled, true);
}

static bool
aicwf_sta_ready(struct aicwf_softc *sc)
{
	struct ieee80211com * const ic = &sc->sc_ic;

	KASSERT(mutex_owned(&sc->sc_lock));
	return aicwf_sta_supported(sc) && sc->sc_sta_enabled &&
	    sc->sc_connected && !sc->sc_connecting &&
	    ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
	    ic->ic_bss->ni_chan != IEEE80211_CHAN_ANYC &&
	    (sc->sc_if.if_flags & IFF_RUNNING) != 0 &&
	    sc->sc_sta_assoc_len != 0 && aicwf_sta_security(sc) &&
	    sc->sc_ptk_valid && (!sc->sc_sae_enabled || sc->sc_sae_authenticated);
}

/* Capture only the fixed ABI header. Deliver it after dropping sc_lock. */
static bool
aicwf_sta_tx_finish_locked(struct aicwf_softc *sc, int tx_error,
    bool ack_known, bool ack, void *event)
{
	struct ieee80211req_sta_mgmt notice;
	bool notify;
	int s;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (!sc->sc_sta_tx_pending)
		return false;
	sc->sc_sta_tx_pending = false;
	sc->sc_sta_tx_sent = false;
	s = splnet();
	notify = ieee80211_sta_action_report(&sc->sc_ic.ic_sta_action,
	    &sc->sc_sta_tx, tx_error, ack_known, ack, &notice);
	(void)ieee80211_sta_action_release(&sc->sc_ic.ic_sta_action,
	    &sc->sc_sta_tx);
	splx(s);
	if (notify)
		memcpy(event, &notice, AICWF_STA_NOTICE_LEN);
	return notify;
}

static bool
aicwf_sta_scan_finish_locked(struct aicwf_softc *sc)
{
	KASSERT(mutex_owned(&sc->sc_lock));
	if (!sc->sc_sta_scan_pending)
		return false;
	sc->sc_sta_scan_pending = false;
	/* Keep a published scan owned until its real firmware completion. */
	if (sc->sc_sta_scan_started)
		sc->sc_sta_scan_draining = true;
	return true;
}

/* Cancel is best effort. A missing reply never releases radio ownership. */
static void
aicwf_sta_scan_cancel_locked(struct aicwf_softc *sc)
{
	struct aicwf_task *t;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (atomic_load_relaxed(&sc->sc_dying) || !sc->sc_sta_scan_started || sc->sc_sta_cancel_queued)
		return;
	t = pool_cache_get(sc->sc_taskpool, PR_NOWAIT);
	if (t == NULL) {
		device_printf(sc->sc_dev, "station scan cancellation allocation failed\n");
		return;
	}
	sc->sc_sta_cancel_queued = true;
	t->t_cmd = AICWF_TASK_STA_CANCEL_SCAN;
	t->t_sta.flags = sc->sc_sta_scan_serial;
	workqueue_enqueue(sc->sc_taskq, &t->t_work, NULL);
}

/* Called with sc_lock held before the association epoch changes. */
static void
aicwf_sta_retire_locked(struct aicwf_softc *sc, uint8_t *tx, bool *tx_event,
    bool *scan_event)
{
	int s;

	KASSERT(mutex_owned(&sc->sc_lock));
	s = splnet();
	ieee80211_sta_action_invalidate(&sc->sc_ic.ic_sta_action);
	splx(s);
	*tx_event = aicwf_sta_tx_finish_locked(sc, 0, false, false, tx);
	*scan_event = aicwf_sta_scan_finish_locked(sc);
	if (sc->sc_sta_scan_draining)
		aicwf_sta_scan_cancel_locked(sc);
	sc->sc_sta_assoc_len = 0;
}

static void
aicwf_sta_tx_cfm(struct aicwf_softc *sc, const uint8_t *p, size_t len)
{
	uint8_t event[AICWF_STA_NOTICE_LEN];
	uint32_t status, token;
	bool notify = false;

	if (!aicwf_sta_cfm_decode(p, len, &status, &token))
		return;
	mutex_enter(&sc->sc_lock);
	if (sc->sc_sta_tx_pending && sc->sc_sta_tx_sent &&
	    token != sc->sc_sta_token && !sc->sc_sta_tx_mismatch_reported) {
		sc->sc_sta_tx_mismatch_reported = true;
		device_printf(sc->sc_dev,
		    "station TX confirmation mismatch: cookie %u expected %u "
		    "raw 0x%08x status 0x%08x\n", sc->sc_sta_tx.cookie,
		    sc->sc_sta_token, le32dec(p + 4), status);
	}
	if (sc->sc_sta_tx_pending && sc->sc_sta_tx_sent &&
	    token == sc->sc_sta_token &&
	    sc->sc_sta_tx.generation == (uint32_t)sc->sc_sae_epoch &&
	    aicwf_sta_ready(sc) &&
	    IEEE80211_ADDR_EQ(sc->sc_sta_tx.bssid, sc->sc_ic.ic_bss->ni_bssid)) {
		device_printf(sc->sc_dev,
		    "station TX confirmation: cookie %u token %u raw 0x%08x "
		    "status 0x%08x ACK %u\n", sc->sc_sta_tx.cookie, token,
		    le32dec(p + 4), status, (status & AICWF_STA_CFM_ACK) != 0);
		notify = aicwf_sta_tx_finish_locked(sc, 0, true,
		    (status & AICWF_STA_CFM_ACK) != 0, event);
	}
	mutex_exit(&sc->sc_lock);
	if (notify)
		rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_TX, event, sizeof(event));
}

/* Check each requested frequency before queuing a command that can sleep. */
static int
aicwf_sta_scan_request(struct aicwf_softc *sc,
    const struct ieee80211req_sta_mgmt *req, uint8_t *wire)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	uint16_t freqs[AICWF_STA_SCAN_CHANNELS], f;
	size_t count = 0, i, j;
	u_int channel;

	if ((req->len & 1) != 0 || req->len > IEEE80211_STA_MAX_CHANNELS * 2)
		return EINVAL;
	if (req->len != 0) {
		for (i = 0; i < req->len / 2; i++) {
			memcpy(&f, req->data + i * 2, sizeof(f));
			channel = ieee80211_mhz2ieee(f, 0);
			if (channel == 0 || channel > IEEE80211_CHAN_MAX ||
			    ic->ic_channels[channel].ic_freq != f ||
			    !isset(ic->ic_chan_active, channel) ||
			    (sc->sc_dc && !aicwf_dc_channel(sc, channel, f)))
				return EINVAL;
			for (j = 0; j < count; j++)
				if (freqs[j] == f)
					return EINVAL;
			freqs[count++] = f;
		}
	} else {
		for (channel = 1; channel <= IEEE80211_CHAN_MAX; channel++) {
			if (!isset(ic->ic_chan_active, channel) ||
			    ic->ic_channels[channel].ic_freq == 0 ||
			    (sc->sc_dc && !aicwf_dc_channel(sc, channel,
			    ic->ic_channels[channel].ic_freq)))
				continue;
			if (count == __arraycount(freqs))
				return E2BIG;
			freqs[count++] = ic->ic_channels[channel].ic_freq;
		}
	}
	return aicwf_sta_scan_build(wire, freqs, count, sc->sc_vif,
	    aicwf_tx_power(sc)) ? 0 : EINVAL;
}

static int
aicwf_sta_mgmt(struct ieee80211com *ic, struct ieee80211req_sta_mgmt *req)
{
	struct aicwf_softc * const sc = ic->ic_ifp->if_softc;
	struct aicwf_task *t;
	struct ieee80211req_sta_mgmt scan_req;
	uint8_t event[AICWF_STA_NOTICE_LEN];
	uint8_t scan_wire[AICWF_STA_SCAN_LEN];
	bool tx_notify = false, scan_notify = false, scan_release = false;
	int error = 0;

	error = ieee80211_sta_request_validate(req);
	if (error != 0)
		return error;
	mutex_enter(&sc->sc_lock);
	if (atomic_load_relaxed(&sc->sc_dying)) {
		error = ENXIO;
		goto out;
	}
	if (!aicwf_sta_supported(sc)) {
		error = EOPNOTSUPP;
		goto out;
	}
	if (req->op >= IEEE80211_STA_TX &&
	    req->op != IEEE80211_STA_CANCEL_SCAN &&
	    sc->sc_sae_epoch > UINT32_MAX) {
		error = EOVERFLOW;
		goto out;
	}
	if (req->op >= IEEE80211_STA_TX &&
	    req->op != IEEE80211_STA_CANCEL_SCAN &&
	    (req->generation != (uint32_t)sc->sc_sae_epoch || ic->ic_bss == NULL ||
	    !IEEE80211_ADDR_EQ(req->bssid, ic->ic_bss->ni_bssid))) {
		error = ESTALE;
		goto out;
	}
	switch (req->op) {
	case IEEE80211_STA_GET:
		if (sc->sc_sae_epoch > UINT32_MAX) {
			error = EOVERFLOW;
			break;
		}
		req->flags = IEEE80211_STA_CAP_BTM | IEEE80211_STA_CAP_NR |
		    IEEE80211_STA_CAP_RM_REQUEST_RX |
		    IEEE80211_STA_CAP_RM_REPORT_TX | IEEE80211_STA_CAP_SCAN;
		req->generation = (uint32_t)sc->sc_sae_epoch;
		memset(req->bssid, 0, sizeof(req->bssid));
		IEEE80211_ADDR_COPY(req->data, ic->ic_myaddr);
		req->len = IEEE80211_ADDR_LEN;
		req->freq = 0;
		if (sc->sc_connected && ic->ic_state == IEEE80211_S_RUN &&
		    ic->ic_bss != NULL && ic->ic_bss->ni_chan != IEEE80211_CHAN_ANYC) {
			if (!aicwf_sta_security(sc))
				req->flags = IEEE80211_STA_CAP_SCAN;
			req->flags |= IEEE80211_STA_ASSOCIATED;
			req->freq = ic->ic_bss->ni_chan->ic_freq;
			IEEE80211_ADDR_COPY(req->bssid, ic->ic_bss->ni_bssid);
			if (sc->sc_sta_assoc_len != 0) {
				req->flags |= IEEE80211_STA_ASSOC_IES;
				memcpy(req->data + 6, sc->sc_sta_assoc, sc->sc_sta_assoc_len);
				req->len += sc->sc_sta_assoc_len;
			}
		}
		break;
	case IEEE80211_STA_ENABLE:
	case IEEE80211_STA_DISABLE:
		if (req->len != 0) {
			error = EINVAL;
			break;
		}
		sc->sc_sta_enabled = req->op == IEEE80211_STA_ENABLE;
		if (!sc->sc_sta_enabled) {
			int s = splnet();
			ieee80211_sta_action_invalidate(&ic->ic_sta_action);
			splx(s);
			tx_notify = aicwf_sta_tx_finish_locked(sc, 0, false, false,
			    event);
			scan_notify = aicwf_sta_scan_finish_locked(sc);
			if (sc->sc_sta_scan_draining)
				aicwf_sta_scan_cancel_locked(sc);
		}
		break;
	case IEEE80211_STA_CANCEL_TX:
		if (req->len != 0 || req->cookie == 0) {
			error = EINVAL;
			break;
		}
		if (!sc->sc_sta_tx_pending) {
			error = ESTALE;
			break;
		}
		{
			int s = splnet();
			bool cancelled = ieee80211_sta_action_cancel(
			    &ic->ic_sta_action, req);
			splx(s);
			if (!cancelled) {
				error = ESTALE;
				break;
			}
		}
		tx_notify = aicwf_sta_tx_finish_locked(sc, 0, false, false, event);
		break;
	case IEEE80211_STA_CANCEL_SCAN:
		if (req->cookie == 0 || req->cookie != sc->sc_sta_scan.cookie) {
			error = ESTALE;
			break;
		}
		if (!ieee80211_sta_scan_match(ic, req)) {
			error = ESTALE;
			break;
		}
		if (req->len != 0) {
			error = EINVAL;
			break;
		}
		scan_notify = aicwf_sta_scan_finish_locked(sc);
		aicwf_sta_scan_cancel_locked(sc);
		scan_release = !sc->sc_sta_scan_started;
		break;
	case IEEE80211_STA_TX:
	case IEEE80211_STA_SCAN:
		if (!aicwf_sta_ready(sc) || req->freq != ic->ic_bss->ni_chan->ic_freq) {
			error = ENOTCONN;
			break;
		}
		if (req->cookie == 0 || (req->op == IEEE80211_STA_TX &&
		    !aicwf_sta_action_valid(req->data, req->len, true))) {
			error = EINVAL;
			break;
		}
		if (req->op == IEEE80211_STA_TX) {
			if (sc->sc_sta_tx_pending || sc->sc_scanning ||
			    sc->sc_sta_scan_pending || ic->ic_sta_scan.active) {
				error = EBUSY;
				break;
			}
			if (sc->sc_sta_token >= (sc->sc_dc ?
			    AICWF_DC_DATA_TOKEN - 1 : AICWF_STA_TOKEN_MAX)) {
				error = EOVERFLOW;
				break;
			}
		} else {
			if (sc->sc_scanning || sc->sc_sta_scan_pending || sc->sc_sta_cancel_queued ||
			    sc->sc_sta_tx_pending || ic->ic_sta_action.active ||
			    (ic->ic_flags & IEEE80211_F_SCAN) != 0) {
				error = EBUSY;
				break;
			}
			if (sc->sc_sta_scan_serial == UINT32_MAX) {
				error = EOVERFLOW;
				break;
			}
			error = aicwf_sta_scan_request(sc, req, scan_wire);
			if (error != 0)
				break;
		}
		t = pool_cache_get(sc->sc_taskpool, PR_NOWAIT);
		if (t == NULL) {
			error = ENOBUFS;
			break;
		}
		if (req->op == IEEE80211_STA_SCAN) {
			int s = splnet();
			error = ieee80211_sta_scan_begin(ic, req,
			    (uint32_t)sc->sc_sae_epoch);
			splx(s);
			if (error != 0) {
				pool_cache_put(sc->sc_taskpool, t);
				break;
			}
		} else {
			int s = splnet();
			error = ieee80211_sta_action_begin(&ic->ic_sta_action, req);
			splx(s);
			if (error != 0) {
				pool_cache_put(sc->sc_taskpool, t);
				break;
			}
		}
		t->t_cmd = AICWF_TASK_STA_MGMT;
		t->t_epoch = sc->sc_sae_epoch;
		t->t_sta = *req;
		if (req->op == IEEE80211_STA_TX) {
			sc->sc_sta_tx_pending = true;
			sc->sc_sta_tx_sent = false;
			sc->sc_sta_tx_mismatch_reported = false;
			sc->sc_sta_tx = *req;
			t->t_sta.flags = ++sc->sc_sta_token;
		} else {
			sc->sc_sta_scan_pending = true;
			sc->sc_sta_scan = *req;
			t->t_sta.flags = ++sc->sc_sta_scan_serial;
		}
		workqueue_enqueue(sc->sc_taskq, &t->t_work, NULL);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
out:
	if (scan_notify && !sc->sc_sta_scan_started)
		scan_release = true;
	if (scan_notify || scan_release)
		scan_req = sc->sc_sta_scan;
	mutex_exit(&sc->sc_lock);
	if (tx_notify)
		rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_TX, event, sizeof(event));
	if (scan_notify) {
		int s = splnet();
		(void)ieee80211_sta_scan_report(ic, &scan_req, true);
		splx(s);
	}
	if (scan_release) {
		int s = splnet();
		(void)ieee80211_sta_scan_release(ic, &scan_req);
		splx(s);
	}
	return error;
}

static void
aicwf_sta_mgmt_cb(struct aicwf_softc *sc, const struct aicwf_task *t)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	const struct ieee80211req_sta_mgmt * const req = &t->t_sta;
	struct ieee80211req_sta_mgmt scan_req;
	uint8_t event[AICWF_STA_NOTICE_LEN], wire[AICWF_STA_SCAN_LEN];
	uint8_t *buf, *desc, *frame;
	bool notify = false, scan_notify = false, scan_release = false;
	int error = 0, s;

	mutex_enter(&sc->sc_lock);
	if (req->op == IEEE80211_STA_TX) {
		if (!sc->sc_sta_tx_pending || req->flags != sc->sc_sta_token)
			goto out;
	} else if (!sc->sc_sta_scan_pending || req->flags != sc->sc_sta_scan_serial)
		goto out;
	if (!aicwf_sta_ready(sc) || t->t_epoch != sc->sc_sae_epoch ||
	    !IEEE80211_ADDR_EQ(req->bssid, ic->ic_bss->ni_bssid)) {
		error = ESTALE;
		goto failed;
	}
	if (req->op == IEEE80211_STA_TX) {
		buf = sc->sc_txbuf;
		desc = buf + AICWF_HDR_LEN;
		frame = desc + AICWF_TXDESC_LEN;
		le16enc(buf, AICWF_TXDESC_LEN + 24 + req->len);
		buf[2] = AICWF_TYPE_DATA;
		memset(desc, 0, AICWF_TXDESC_LEN + 24);
		le16enc(desc, 24 + req->len);
		le32enc(desc + 4, AICWF_STA_CFM_REQUEST | req->flags);
		memcpy(desc + 8, req->bssid, ETHER_ADDR_LEN);
		memcpy(desc + 14, ic->ic_myaddr, ETHER_ADDR_LEN);
		desc[22] = AICWF_HWQ_VO;
		desc[23] = AICWF_TID_NONE;
		desc[24] = sc->sc_vif;
		desc[25] = sc->sc_ap;
		le16enc(desc + 26, AICWF_TX_MGMT |
		    (sc->sc_sae_enabled ? AICWF_TX_MGMT_ROBUST : 0));
		frame[0] = 0xd0;
		memcpy(frame + 4, req->bssid, ETHER_ADDR_LEN);
		memcpy(frame + 10, ic->ic_myaddr, ETHER_ADDR_LEN);
		memcpy(frame + 16, req->bssid, ETHER_ADDR_LEN);
		memcpy(frame + 24, req->data, req->len);
		sc->sc_sta_tx_sent = true;
		error = aicwf_write(sc, AICWF_HDR_LEN + AICWF_TXDESC_LEN + 24 + req->len,
		    AICWF_TX_RESERVE);
		device_printf(sc->sc_dev,
		    "station TX: generation %u cookie %u token %u category %u action %u error %d\n",
		    req->generation, req->cookie, req->flags, req->data[0], req->data[1], error);
		if (error != 0)
			goto failed;
		goto out;
	}
	error = aicwf_sta_scan_request(sc, req, wire);
	if (error != 0)
		goto failed;
	sc->sc_scanning = true;
	sc->sc_sta_scan_started = true;
	sc->sc_scan_published = false;
	sc->sc_sta_scan_draining = false;
	sc->sc_scan_ticks = getticks();
	mutex_exit(&sc->sc_lock);
	error = aicwf_cmd_reply(sc, AICWF_SCANU_START_REQ, wire, sizeof(wire),
	    AICWF_SCANU_START_ACK, NULL, 0, true, t->t_epoch);
	mutex_enter(&sc->sc_lock);
	device_printf(sc->sc_dev,
	    "station scan start: generation %u cookie %u serial %u channels %u error %d\n",
	    req->generation, req->cookie, req->flags, wire[AICWF_STA_SCAN_COUNT], error);
	if (req->flags != sc->sc_sta_scan_serial || !sc->sc_sta_scan_started) {
		if (!sc->sc_scanning && !sc->sc_sta_scan_started) {
			/* This callback can only retire the request it actually queued. */
			scan_req = *req;
			scan_release = true;
		}
		goto out;
	}
	if (error != 0) {
		if (!sc->sc_scan_published) {
			sc->sc_sta_scan_started = false;
			sc->sc_scanning = false;
			sc->sc_sta_scan_draining = false;
		}
		goto failed;
	}
	goto out;
failed:
	device_printf(sc->sc_dev,
	    "station request aborted: generation %u cookie %u op %u error %d\n",
	    req->generation, req->cookie, req->op, error);
	if (req->op == IEEE80211_STA_TX)
		notify = aicwf_sta_tx_finish_locked(sc, error, false, false, event);
	else {
		scan_notify = aicwf_sta_scan_finish_locked(sc);
		if (sc->sc_sta_scan_draining)
			aicwf_sta_scan_cancel_locked(sc);
		if (!sc->sc_scanning) {
			scan_release = true;
		}
	}
	if (scan_notify || scan_release)
		scan_req = sc->sc_sta_scan;
out:
	mutex_exit(&sc->sc_lock);
	if (notify)
		rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_TX, event, sizeof(event));
	if (scan_notify) {
		s = splnet();
		(void)ieee80211_sta_scan_report(ic, &scan_req, true);
		splx(s);
	}
	if (scan_release) {
		s = splnet();
		(void)ieee80211_sta_scan_release(ic, &scan_req);
		splx(s);
	}
}

static void
aicwf_sta_cancel_scan_cb(struct aicwf_softc *sc, uint32_t serial)
{
	uint8_t status = 0xff;
	int error;

	mutex_enter(&sc->sc_lock);
	if (serial != sc->sc_sta_scan_serial || !sc->sc_sta_scan_started ||
	    !sc->sc_sta_scan_draining) {
		sc->sc_sta_cancel_queued = false;
		mutex_exit(&sc->sc_lock);
		return;
	}
	mutex_exit(&sc->sc_lock);
	error = aicwf_cmd_reply(sc, AICWF_SCANU_CANCEL_REQ, NULL, 0,
	    AICWF_SCANU_CANCEL_CFM, &status, sizeof(status), true, 0);
	mutex_enter(&sc->sc_lock);
	sc->sc_sta_cancel_queued = false;
	mutex_exit(&sc->sc_lock);
	device_printf(sc->sc_dev, "station scan cancel: serial %u error %d status %u; completion required\n",
	    serial, error, status);
}

/* Validate one unicast management body while the association lock is held. */
static const uint8_t *
aicwf_mgmt_body(struct aicwf_softc *sc, const uint8_t *frame, size_t len,
    uint32_t status, uint32_t flags, size_t *body_len, uint64_t *pn)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	const uint8_t *peer = ic->ic_bss->ni_bssid;
	const bool pmf = sc->sc_sae_enabled;
	size_t hdr = 24;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (!sc->sc_if_attached || !sc->sc_connected ||
	    ic->ic_state != IEEE80211_S_RUN ||
	    ((flags >> 8) & 0xff) != sc->sc_vif ||
	    !aicwf_pmf_header_valid(frame, len, ic->ic_myaddr, peer, peer,
	    pmf ? 0x40 : 0) || (status & 0x7e0) != 0 ||
	    (status & 0x6000) != 0x6000)
		return NULL;
	*pn = sc->sc_pmf_rx_pn;
	if (pmf) {
		if (!sc->sc_ptk_valid || !sc->sc_sae_authenticated ||
		    !IEEE80211_ADDR_EQ(peer, sc->sc_sae_bssid) || len < 32 ||
		    (status & 0x02000000) == 0 ||
		    ((status >> 2) & 7) != 3 ||
		    sc->sc_hwkey[AICWF_KEY_SLOT_PAIRWISE] == AICWF_HWKEY_NONE ||
		    ((status >> 15) & 0x3ff) !=
		    sc->sc_hwkey[AICWF_KEY_SLOT_PAIRWISE] ||
		    frame[26] != 0 || frame[27] != 0x20)
			return NULL;
		*pn = (uint64_t)frame[24] | (uint64_t)frame[25] << 8 |
		    (uint64_t)frame[28] << 16 | (uint64_t)frame[29] << 24 |
		    (uint64_t)frame[30] << 32 | (uint64_t)frame[31] << 40;
		if (*pn <= sc->sc_pmf_rx_pn)
			return NULL;
		/* The firmware strips the MIC but retains the CCMP header. */
		hdr += 8;
	} else if (((status >> 2) & 7) != 0) {
		return NULL;
	}
	*body_len = len - hdr;
	return frame + hdr;
}

static void
aicwf_sta_rx_event(struct aicwf_softc *sc, const uint8_t *frame, size_t len,
    uint32_t status, uint32_t flags)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211req_sta_mgmt *ev;
	const uint64_t epoch = sc->sc_sae_epoch;
	const uint8_t *body;
	size_t body_len;
	uint64_t pn;

	if (len < 27 || len > 32 + IEEE80211_STA_MAX_BODY || frame[0] != 0xd0)
		return;
	ev = kmem_zalloc(sizeof(*ev), KM_NOSLEEP);
	if (ev == NULL)
		return;
	mutex_enter(&sc->sc_lock);
	if (!aicwf_sta_ready(sc) || epoch != sc->sc_sae_epoch)
		goto drop;
	body = aicwf_mgmt_body(sc, frame, len, status, flags, &body_len, &pn);
	if (body == NULL || !aicwf_sta_action_valid(body, body_len, false))
		goto drop;
	sc->sc_pmf_rx_pn = pn;
	device_printf(sc->sc_dev, "station RX: generation %u category %u action %u length %zu PMF %u\n",
	    (uint32_t)epoch, body[0], body[1], body_len, sc->sc_sae_enabled);
	ev->version = IEEE80211_STA_VERSION;
	ev->op = IEEE80211_STA_GET;
	ev->generation = (uint32_t)epoch;
	ev->freq = ic->ic_bss->ni_chan->ic_freq;
	ev->len = body_len;
	IEEE80211_ADDR_COPY(ev->bssid, ic->ic_bss->ni_bssid);
	memcpy(ev->data, body, body_len);
	mutex_exit(&sc->sc_lock);
	rt_ieee80211msg(&sc->sc_if, RTM_IEEE80211_STA_RX, ev,
	    offsetof(struct ieee80211req_sta_mgmt, data) + body_len);
	kmem_free(ev, sizeof(*ev));
	return;
drop:
	mutex_exit(&sc->sc_lock);
	kmem_free(ev, sizeof(*ev));
}

/* Pure liveness decision: the link counts as dead only while the driver
 * believes it is associated and no frame (beacons included) arrived for
 * AICWF_ALIVE_SILENT_MS. */
static bool
aicwf_alive_silent(struct aicwf_softc *sc, u_int now)
{
	const int delta = (int)(now - sc->sc_alive_rx);

	if (!sc->sc_if_attached || !sc->sc_connected || sc->sc_connecting ||
	    (sc->sc_if.if_flags & IFF_RUNNING) == 0 ||
	    sc->sc_ic.ic_state != IEEE80211_S_RUN)
		return false;
	return delta > (int)mstohz(AICWF_ALIVE_SILENT_MS);
}

/* The teardown sleeps in the firmware roundtrip: defer it to the taskq. */
static void
aicwf_alive_lost(struct aicwf_softc *sc)
{

	mutex_enter(&sc->sc_lock);
	if (atomic_load_relaxed(&sc->sc_dying) || !sc->sc_if_attached) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	if (!sc->sc_alive_queued) {
		sc->sc_alive_queued = true;
		sc->sc_alive_epoch = sc->sc_sae_epoch;
		mutex_spin_enter(&sc->sc_epoch_lock);
		sc->sc_alive_join_serial = sc->sc_join_serial;
		mutex_spin_exit(&sc->sc_epoch_lock);
		workqueue_enqueue(sc->sc_taskq, &sc->sc_alive_work, NULL);
	}
	mutex_exit(&sc->sc_lock);
}

/* Both watchdog and firmware loss events queue generation-bound recovery. */
static void
aicwf_recover_scan(struct aicwf_softc *sc, uint64_t epoch, uint64_t serial)
{
	struct aicwf_task *t;

	if (atomic_load_relaxed(&sc->sc_dying))
		return;
	t = pool_cache_get(sc->sc_taskpool, PR_NOWAIT);
	if (t == NULL) {
		device_printf(sc->sc_dev, "no free task for recovery scan\n");
		return;
	}
	t->t_cmd = AICWF_TASK_RECOVER_SCAN;
	t->t_epoch = epoch;
	t->t_join_serial = serial;
	workqueue_enqueue(sc->sc_taskq, &t->t_work, NULL);
}

static void
aicwf_alive_work(struct aicwf_softc *sc)
{
	struct ifnet * const ifp = &sc->sc_if;
	uint64_t expected, serial, epoch;
	bool current;

	if (!sc->sc_if_attached)
		return;
	/* CONFIGURE and MLME use this same sleepable interface lock. */
	IFNET_LOCK(ifp);
	mutex_enter(&sc->sc_lock);
	expected = sc->sc_alive_epoch;
	serial = sc->sc_alive_join_serial;
	sc->sc_alive_queued = false;
	mutex_spin_enter(&sc->sc_epoch_lock);
	current = serial == sc->sc_join_serial;
	mutex_spin_exit(&sc->sc_epoch_lock);
	current = current && sc->sc_connected && !sc->sc_connecting &&
	    sc->sc_if_attached && (ifp->if_flags & IFF_RUNNING) != 0 &&
	    sc->sc_ic.ic_state == IEEE80211_S_RUN &&
	    expected == sc->sc_sae_epoch;
	mutex_exit(&sc->sc_lock);
	if (!current)
		goto out;

	device_printf(sc->sc_dev, "no frames from the AP; reconnecting\n");
	epoch = aicwf_recover_disconnect(sc, expected);
	if (epoch == 0)
		goto out;
	aicwf_recover_scan(sc, epoch, serial);
out:
	IFNET_UNLOCK(ifp);
}

static void
aicwf_alive_tick(void *arg)
{
	struct aicwf_softc * const sc = arg;

	mutex_enter(&sc->sc_lock);
	if (atomic_load_relaxed(&sc->sc_dying)) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	if (aicwf_alive_silent(sc, ticks)) {
		if (++sc->sc_alive_misses >= 2) {
			sc->sc_alive_misses = 0;
			if (!sc->sc_alive_queued) {
				sc->sc_alive_queued = true;
				sc->sc_alive_epoch = sc->sc_sae_epoch;
				mutex_spin_enter(&sc->sc_epoch_lock);
				sc->sc_alive_join_serial = sc->sc_join_serial;
				mutex_spin_exit(&sc->sc_epoch_lock);
				workqueue_enqueue(sc->sc_taskq,
				    &sc->sc_alive_work, NULL);
			}
		}
	} else
		sc->sc_alive_misses = 0;
	mutex_exit(&sc->sc_lock);

	callout_schedule(&sc->sc_alive_ch, mstohz(AICWF_ALIVE_PERIOD_MS));
}

static void
aicwf_disconnect_ind(struct aicwf_softc *sc, const uint8_t *ind, size_t len)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	uint64_t expected, epoch, serial;
	bool recover, old_ap;

	/* reason (u16), VIF, FT-over-DS, reassociation, alignment byte. */
	if (len < 6 || ind[2] != sc->sc_vif || !sc->sc_if_attached)
		return;
	mutex_enter(&sc->sc_lock);
	/* An old AP departure does not terminate the configured join. */
	if (ind[4] != 0 || sc->sc_connecting) {
		old_ap = sc->sc_connecting;
		if (old_ap) {
			sc->sc_connected = false;
			memset(sc->sc_igtk, AICWF_HWKEY_NONE, sizeof(sc->sc_igtk));
		}
		mutex_exit(&sc->sc_lock);
		/* CONNECT already retired the old RX lifetime before its command.
		 * Preserve that pending CONNECT token; an old-AP indication does
		 * not cancel the replacement association. */
		return;
	}
	if (!sc->sc_connected) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	expected = sc->sc_sae_epoch;
	mutex_spin_enter(&sc->sc_epoch_lock);
	serial = sc->sc_join_serial;
	mutex_spin_exit(&sc->sc_epoch_lock);
	mutex_exit(&sc->sc_lock);
	epoch = aicwf_sae_advance_if(sc, expected);
	if (epoch == 0)
		return;
	mutex_enter(&sc->sc_lock);
	if (epoch != sc->sc_sae_epoch) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	sc->sc_connected = false;
	sc->sc_connecting = false;
	sc->sc_sae_pending = false;
	sc->sc_sae_authenticated = false;
	memset(sc->sc_igtk, AICWF_HWKEY_NONE, sizeof(sc->sc_igtk));
	recover = ic->ic_state == IEEE80211_S_RUN;
	mutex_exit(&sc->sc_lock);
	aicwf_rx_retire(sc);
	if (recover)
		aicwf_recover_scan(sc, epoch, serial);
}

/* Delete management keys before the firmware can reuse their hardware slots. */
static void
aicwf_sae_clear_keys(struct aicwf_softc *sc)
{
	const uint64_t epoch = sc->sc_sae_epoch;
	uint8_t key;
	unsigned i;

	for (i = 0; i < __arraycount(sc->sc_igtk); i++) {
		if (sc->sc_igtk[i] == AICWF_HWKEY_NONE)
			continue;
		key = sc->sc_igtk[i];
		(void)aicwf_cmd_reply(sc, AICWF_MM_KEY_DEL_REQ,
		    &key, 1, AICWF_MM_KEY_DEL_CFM, NULL, 0, true, epoch);
		if (epoch != sc->sc_sae_epoch)
			return;
		if (sc->sc_igtk[i] == key)
			sc->sc_igtk[i] = AICWF_HWKEY_NONE;
	}
}

/* Retire only the association that queued watchdog recovery. */
static uint64_t
aicwf_recover_disconnect(struct aicwf_softc *sc, uint64_t expected)
{
	uint8_t keys[2], req[4];
	uint64_t epoch;
	bool disconnect, current;

	KASSERT(IFNET_LOCKED(&sc->sc_if));
	epoch = aicwf_sae_advance_if(sc, expected);
	if (epoch == 0)
		return 0;
	mutex_enter(&sc->sc_lock);
	if (epoch != sc->sc_sae_epoch) {
		mutex_exit(&sc->sc_lock);
		return 0;
	}
	memcpy(keys, sc->sc_igtk, sizeof(keys));
	memset(sc->sc_igtk, AICWF_HWKEY_NONE, sizeof(sc->sc_igtk));
	disconnect = sc->sc_connected || sc->sc_connecting;
	sc->sc_connected = sc->sc_connecting = false;
	sc->sc_sae_pending = sc->sc_sae_authenticated = false;
	sc->sc_alive_misses = 0;
	mutex_exit(&sc->sc_lock);
	aicwf_rx_retire(sc);
	for (size_t i = 0; i < __arraycount(keys); i++) {
		if (keys[i] == AICWF_HWKEY_NONE)
			continue;
		if (aicwf_cmd_reply(sc, AICWF_MM_KEY_DEL_REQ, &keys[i], 1,
		    AICWF_MM_KEY_DEL_CFM, NULL, 0, true, epoch) == ESTALE)
			return 0;
	}
	if (disconnect) {
		memset(req, 0, sizeof(req));
		le16enc(req, IEEE80211_REASON_AUTH_LEAVE);
		req[2] = sc->sc_vif;
		if (aicwf_cmd_reply(sc, AICWF_SM_DISCONNECT_REQ, req, sizeof(req),
		    AICWF_SM_DISCONNECT_CFM, NULL, 0, true, epoch) == ESTALE)
			return 0;
	}
	mutex_enter(&sc->sc_lock);
	current = epoch == sc->sc_sae_epoch;
	mutex_exit(&sc->sc_lock);
	return current ? epoch : 0;
}

static void
aicwf_disconnect(struct aicwf_softc *sc)
{
	uint8_t req[4];

	aicwf_sae_advance(sc);

	sc->sc_sae_pending = false;
	sc->sc_sae_authenticated = false;
	aicwf_sae_clear_keys(sc);
	/* CONNECT shares this workqueue; a new CONFIGURE cannot cancel leave. */
	memset(sc->sc_igtk, AICWF_HWKEY_NONE, sizeof(sc->sc_igtk));
	if (!sc->sc_connected && !sc->sc_connecting)
		return;
	sc->sc_connected = false;
	sc->sc_connecting = false;
	aicwf_rx_retire(sc);

	memset(req, 0, sizeof(req));
	le16enc(req, IEEE80211_REASON_AUTH_LEAVE);
	req[2] = sc->sc_vif;
	(void)aicwf_cmd(sc, AICWF_SM_DISCONNECT_REQ, req, sizeof(req),
	    AICWF_SM_DISCONNECT_CFM, NULL, 0);
}

static void
aicwf_newstate_cb(struct aicwf_softc *sc, enum ieee80211_state nstate,
    int arg)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211_assoc_event assoc;
	const enum ieee80211_state ostate = ic->ic_state;
	int s;

	if (sc->sc_receive_only && nstate == IEEE80211_S_SCAN) {
		/* begin_scan selects a channel and queues a second SCAN transition. */
		if (ostate == IEEE80211_S_SCAN)
			return;
		s = splnet();
		sc->sc_newstate(ic, nstate, arg);
		ic->ic_flags &= ~IEEE80211_F_ASCAN;
		splx(s);
		aicwf_passive_scan(sc);
		return;
	}

	switch (nstate) {
	case IEEE80211_S_INIT:
		aicwf_disconnect(sc);
		break;

	case IEEE80211_S_SCAN:
		/*
		 * A user scan forces ic_state to INIT in setupscan(),
		 * bypassing RUN's normal station departure.  The radio
		 * may still have the association if this path arrived
		 * without the common setupscan retirement.  Retire only a
		 * still-current common BSS identity; setupscan has already
		 * emitted the old DISASSOC and cleared its epoch.
		 */
		if (ostate == IEEE80211_S_INIT && sc->sc_connected &&
		    ieee80211_assoc_event_snapshot(ic, ic->ic_bss, &assoc)) {
			s = splnet();
			ieee80211_sta_leave(ic, ic->ic_bss);
			splx(s);
		}
		aicwf_disconnect(sc);
		if (ostate != IEEE80211_S_SCAN) {
			if (!sc->sc_scanning)
				aicwf_scan(sc);
			break;
		}
		/*
		 * net80211 found nothing and goes round again: one scan
		 * covers every channel, so start the next after a pause.
		 */
		if (sc->sc_scanning ||
		    (ic->ic_flags & IEEE80211_F_SCAN) == 0)
			break;
		const int wait = sc->sc_scan_ticks +
		    mstohz(AICWF_SCAN_INTERVAL_MS) - getticks();
		if (wait > 0)
			kpause("aicwfscn", false, wait, NULL);
		if (ic->ic_state == IEEE80211_S_SCAN && !sc->sc_scanning &&
		    (ic->ic_flags & IEEE80211_F_SCAN) != 0)
			aicwf_scan(sc);
		break;

	default:
		break;
	}

	s = splnet();
	sc->sc_newstate(ic, nstate, arg);
	splx(s);
}

/* Serialize queued transitions with CONFIGURE and replacement MLME requests. */
static void
aicwf_state_cb(struct aicwf_softc *sc, const struct aicwf_task *t)
{
	struct ifnet * const ifp = &sc->sc_if;
	bool current;
	int s;

	if (!sc->sc_if_attached)
		return;
	IFNET_LOCK(ifp);
	mutex_enter(&sc->sc_lock);
	mutex_spin_enter(&sc->sc_epoch_lock);
	current = t->t_join_serial == sc->sc_join_serial &&
	    (t->t_state != IEEE80211_S_AUTH ||
	    t->t_configure_serial == sc->sc_configure_serial);
	mutex_spin_exit(&sc->sc_epoch_lock);
	current = current &&
	    (t->t_state == IEEE80211_S_INIT || (ifp->if_flags & IFF_RUNNING) != 0);
	mutex_exit(&sc->sc_lock);
	if (current && t->t_state == IEEE80211_S_AUTH) {
		/* Firmware may request SAE before CONNECT_CFM arrives. */
		s = splnet();
		sc->sc_newstate(&sc->sc_ic, IEEE80211_S_AUTH, t->t_arg);
		splx(s);
		aicwf_connect(sc);
	} else if (current)
		aicwf_newstate_cb(sc, t->t_state, t->t_arg);
	IFNET_UNLOCK(ifp);
}

/* The interface lock excludes a new ioctl across this sleeping transition. */
static void
aicwf_recover_scan_cb(struct aicwf_softc *sc, const struct aicwf_task *t)
{
	struct ifnet * const ifp = &sc->sc_if;
	bool current;

	if (!sc->sc_if_attached)
		return;
	IFNET_LOCK(ifp);
	mutex_enter(&sc->sc_lock);
	mutex_spin_enter(&sc->sc_epoch_lock);
	current = t->t_join_serial == sc->sc_join_serial;
	mutex_spin_exit(&sc->sc_epoch_lock);
	current = current && t->t_epoch == sc->sc_sae_epoch &&
	    sc->sc_if_attached && !sc->sc_connecting &&
	    (ifp->if_flags & IFF_RUNNING) != 0 &&
	    sc->sc_ic.ic_state == IEEE80211_S_RUN;
	mutex_exit(&sc->sc_lock);
	if (current)
		aicwf_newstate_cb(sc, IEEE80211_S_SCAN, -1);
	IFNET_UNLOCK(ifp);
}

static void
aicwf_key_set_cb(struct aicwf_softc *sc, const struct aicwf_task *t)
{
	const u_int slot = t->t_group ? t->t_keyix : AICWF_KEY_SLOT_PAIRWISE;
	uint8_t req[44], cfm[2] = { 0xff, 0xff };
	bool duplicate;
	int error;

	if (t->t_cipher != IEEE80211_CIPHER_AES_CCM ||
	    slot >= AICWF_KEY_SLOTS || t->t_keylen != 16) {
		device_printf(sc->sc_dev, "cipher %u is not supported\n",
		    t->t_cipher);
		return;
	}
	mutex_enter(&sc->sc_lock);
	if (t->t_epoch != sc->sc_sae_epoch || !sc->sc_connected) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	duplicate = !t->t_group && sc->sc_ptk_valid &&
	    memcmp(sc->sc_ptk, t->t_key, sizeof(sc->sc_ptk)) == 0;
	/* Never reset firmware TX PN by programming the same live PTK twice. */
	mutex_spin_enter(&sc->sc_epoch_lock);
	if (!t->t_group && !duplicate)
		sc->sc_ptk_valid = false;
	mutex_spin_exit(&sc->sc_epoch_lock);
	memset(req, 0, sizeof(req));
	req[0] = t->t_group ? t->t_keyix : 0;
	req[1] = t->t_group ? AICWF_STA_NONE : sc->sc_ap;
	req[4] = t->t_keylen;
	memcpy(req + 8, t->t_key, t->t_keylen);
	req[40] = AICWF_CIPHER_CCMP;
	req[41] = sc->sc_vif;
	req[43] = !t->t_group;
	mutex_exit(&sc->sc_lock);

	if (!duplicate) {
		error = aicwf_cmd_reply(sc, AICWF_MM_KEY_ADD_REQ, req,
		    sizeof(req), AICWF_MM_KEY_ADD_CFM, cfm, sizeof(cfm),
		    true, t->t_epoch);
		explicit_memset(req, 0, sizeof(req));
		if (error != 0 || cfm[0] != 0 || cfm[1] == AICWF_HWKEY_NONE) {
			device_printf(sc->sc_dev, "key %u not set: %d, %u\n",
			    slot, error, cfm[0]);
			return;
		}
	}
	mutex_enter(&sc->sc_lock);
	if (t->t_epoch != sc->sc_sae_epoch || !sc->sc_connected) {
		mutex_exit(&sc->sc_lock);
		explicit_memset(req, 0, sizeof(req));
		return;
	}
	if (!duplicate) {
		sc->sc_hwkey[slot] = cfm[1];
		if (!t->t_group) {
			mutex_spin_enter(&sc->sc_epoch_lock);
			memcpy(sc->sc_ptk, t->t_key, sizeof(sc->sc_ptk));
			sc->sc_pmf_rx_pn = 0;
			sc->sc_ptk_valid = true;
			mutex_spin_exit(&sc->sc_epoch_lock);
		}
	}
	explicit_memset(req, 0, sizeof(req));
	req[0] = sc->sc_ap;
	req[1] = 1;
	mutex_exit(&sc->sc_lock);
	if (!t->t_group) {
		/* The pairwise key is in: let all traffic through. */
		error = aicwf_cmd_reply(sc, AICWF_ME_SET_CONTROL_PORT_REQ, req, 2,
		    AICWF_ME_SET_CONTROL_PORT_CFM, NULL, 0, true, t->t_epoch);
		if (error == 0)
			aicwf_rx_resume(sc, t->t_rx_serial);
	}
}

static void
aicwf_key_delete_cb(struct aicwf_softc *sc, const struct aicwf_task *t)
{
	const u_int slot = t->t_group ? t->t_keyix : AICWF_KEY_SLOT_PAIRWISE;
	struct ieee80211_rxba_output out = {0};
	uint8_t req[1];

	if (slot >= AICWF_KEY_SLOTS)
		return;
	mutex_enter(&sc->sc_lock);
	if (t->t_epoch != sc->sc_sae_epoch ||
	    sc->sc_hwkey[slot] == AICWF_HWKEY_NONE ||
	    (!t->t_group && sc->sc_ptk_valid &&
	    memcmp(sc->sc_ptk, t->t_key, sizeof(sc->sc_ptk)) != 0)) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	req[0] = sc->sc_hwkey[slot];
	sc->sc_hwkey[slot] = AICWF_HWKEY_NONE;
	if (!t->t_group) {
		mutex_spin_enter(&sc->sc_epoch_lock);
		/* Admission normally fenced this deletion. If the live hardware
		 * identity changed meanwhile, retire it before this actual delete.
		 * Do not cancel a later SET whose admission already closed RX. */
		if (sc->sc_rx_active)
			aicwf_rx_fence_locked(sc, &out);
		sc->sc_ptk_valid = false;
		explicit_memset(sc->sc_ptk, 0, sizeof(sc->sc_ptk));
		mutex_spin_exit(&sc->sc_epoch_lock);
	}
	mutex_exit(&sc->sc_lock);
	aicwf_rx_discard(&out);
	(void)aicwf_cmd_reply(sc, AICWF_MM_KEY_DEL_REQ, req, sizeof(req),
	    AICWF_MM_KEY_DEL_CFM, NULL, 0, true, t->t_epoch);
}

static void
aicwf_task(struct work *wk, void *arg)
{
	struct aicwf_softc * const sc = arg;
	struct aicwf_task * const t = (struct aicwf_task *)wk;

	if (wk == &sc->sc_barrier)
		return;
	if (wk == &sc->sc_survey_work) {
		if (!atomic_load_relaxed(&sc->sc_dying))
			(void)aicwf_survey_remove_vif(sc, sc->sc_survey_vif);
		return;
	}
	if (wk == &sc->sc_alive_work) {
		if (!atomic_load_relaxed(&sc->sc_dying))
			aicwf_alive_work(sc);
		return;
	}
	if (atomic_load_relaxed(&sc->sc_dying))
		goto out;
	switch (t->t_cmd) {
	case AICWF_TASK_NEWSTATE:
		aicwf_state_cb(sc, t);
		break;
	case AICWF_TASK_KEY_SET:
		aicwf_key_set_cb(sc, t);
		break;
	case AICWF_TASK_KEY_DELETE:
		aicwf_key_delete_cb(sc, t);
		break;
	case AICWF_TASK_STA_MGMT:
		aicwf_sta_mgmt_cb(sc, t);
		break;
	case AICWF_TASK_STA_CANCEL_SCAN:
		aicwf_sta_cancel_scan_cb(sc, t->t_sta.flags);
		break;
	case AICWF_TASK_RECOVER_SCAN:
		aicwf_recover_scan_cb(sc, t);
		break;
	}

out:
	explicit_memset(t->t_key, 0, sizeof(t->t_key));
	pool_cache_put(sc->sc_taskpool, t);
}

/* Requests to the firmware sleep, so net80211's calls are deferred. */
static int
aicwf_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct aicwf_softc * const sc = ic->ic_ifp->if_softc;
	struct aicwf_task *t;

	if (atomic_load_relaxed(&sc->sc_dying))
		return ENXIO;
	if (sc->sc_receive_only && nstate != IEEE80211_S_INIT &&
	    nstate != IEEE80211_S_SCAN)
		return EOPNOTSUPP;
	if (nstate == IEEE80211_S_AUTH &&
	    (sc->sc_if.if_flags & IFF_RUNNING) == 0)
		return ENETDOWN;
	if (nstate == IEEE80211_S_INIT || nstate == IEEE80211_S_SCAN ||
	    nstate == IEEE80211_S_AUTH)
		aicwf_rx_retire(sc);
	t = pool_cache_get(sc->sc_taskpool, PR_NOWAIT);
	if (t == NULL) {
		device_printf(sc->sc_dev, "no free tasks\n");
		return EIO;
	}
	/* A raw MLME association can queue AUTH without SAE CONFIGURE. */
	mutex_spin_enter(&sc->sc_epoch_lock);
	if (nstate == IEEE80211_S_AUTH)
		sc->sc_join_serial++;
	t->t_join_serial = sc->sc_join_serial;
	t->t_configure_serial = sc->sc_configure_serial;
	t->t_epoch = sc->sc_sae_epoch;
	mutex_spin_exit(&sc->sc_epoch_lock);
	t->t_cmd = AICWF_TASK_NEWSTATE;
	t->t_state = nstate;
	t->t_arg = arg;
	workqueue_enqueue(sc->sc_taskq, &t->t_work, NULL);

	return 0;
}

static int
aicwf_key_task(struct ieee80211com *ic, const struct ieee80211_key *wk,
    enum aicwf_task_cmd cmd)
{
	struct aicwf_softc * const sc = ic->ic_ifp->if_softc;
	struct aicwf_task *t;
	struct ieee80211_rxba_output out = {0};
	uint64_t serial = 0, epoch;

	if (sc->sc_receive_only || atomic_load_relaxed(&sc->sc_dying))
		return 0;

	/* Pairwise firmware key changes retire the decrypt/reorder lifetime at
	 * queue admission. GTK/IGTK do not decrypt these unicast BA slots.
	 * Ignore a stale DELETE for a different stable current PTK. A pending
	 * SET has closed RX: conservatively invalidate that completion on DELETE.
	 * Both PTK writers
	 * hold sc_lock and sc_epoch_lock; this IPL_NET callback needs only epoch. */
	mutex_spin_enter(&sc->sc_epoch_lock);
	epoch = sc->sc_sae_epoch;
	if ((wk->wk_flags & IEEE80211_KEY_GROUP) == 0 &&
	    (cmd == AICWF_TASK_KEY_SET || !sc->sc_rx_active ||
	    !sc->sc_ptk_valid || (wk->wk_keylen == sizeof(sc->sc_ptk) &&
	    memcmp(wk->wk_key, sc->sc_ptk, sizeof(sc->sc_ptk)) == 0))) {
		aicwf_rx_fence_locked(sc, &out);
		serial = sc->sc_rx_serial;
	}
	mutex_spin_exit(&sc->sc_epoch_lock);
	aicwf_rx_discard(&out);

	t = pool_cache_get(sc->sc_taskpool, PR_NOWAIT);
	if (t == NULL) {
		device_printf(sc->sc_dev, "no free tasks\n");
		return 0;
	}
	/* Keep the epoch captured with the RX fence, before pool allocation. */
	t->t_epoch = epoch;
	t->t_rx_serial = serial;
	t->t_cmd = cmd;
	t->t_cipher = wk->wk_cipher->ic_cipher;
	t->t_keyix = wk->wk_keyix;
	t->t_group = (wk->wk_flags & IEEE80211_KEY_GROUP) != 0;
	t->t_keylen = wk->wk_keylen;
	memcpy(t->t_key, wk->wk_key, sizeof(t->t_key));
	workqueue_enqueue(sc->sc_taskq, &t->t_work, NULL);

	return 1;
}

static int
aicwf_key_set(struct ieee80211com *ic, const struct ieee80211_key *wk,
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	return aicwf_key_task(ic, wk, AICWF_TASK_KEY_SET);
}

static int
aicwf_key_delete(struct ieee80211com *ic, const struct ieee80211_key *wk)
{
	return aicwf_key_task(ic, wk, AICWF_TASK_KEY_DELETE);
}

/* The firmware does the management frames and picks the rates. */
static int
aicwf_send_mgmt(struct ieee80211com *ic, struct ieee80211_node *ni,
    int type, int arg)
{
	return 0;
}

static void
aicwf_recv_mgmt(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int subtype, int rssi, uint32_t rstamp)
{
}

static void
aicwf_newassoc(struct ieee80211_node *ni, int isnew)
{
	ni->ni_txrate = 0;
}

static int
aicwf_media_change(struct ifnet *ifp)
{
	return 0;
}

/* Hand an Ethernet frame to the firmware. */
static int
aicwf_tx_data(struct aicwf_softc *sc, struct mbuf *m)
{
	uint8_t * const buf = sc->sc_txbuf;
	uint8_t * const desc = buf + AICWF_HDR_LEN;
	const size_t len = m->m_pkthdr.len;
	int error;

	if (len < ETHER_HDR_LEN || len > ETHER_HDR_LEN + ETHERMTU)
		return EINVAL;
	const size_t payload = len - ETHER_HDR_LEN;

	mutex_enter(&sc->sc_lock);
	le16enc(buf, AICWF_TXDESC_LEN + payload);
	buf[2] = AICWF_TYPE_DATA;
	memset(desc, 0, AICWF_TXDESC_LEN);
	le16enc(desc, payload);
	m_copydata(m, 0, 2 * ETHER_ADDR_LEN + 2, desc + 8);
	desc[22] = AICWF_HWQ_BE;
	desc[23] = sc->sc_qos ? 0 : AICWF_TID_NONE;
	desc[24] = sc->sc_vif;
	desc[25] = sc->sc_ap;
	m_copydata(m, ETHER_HDR_LEN, payload, desc + AICWF_TXDESC_LEN);
	error = aicwf_write(sc, AICWF_HDR_LEN + AICWF_TXDESC_LEN + payload,
	    AICWF_TX_RESERVE);
	mutex_exit(&sc->sc_lock);

	return error;
}

static void
aicwf_tx_task(struct work *wk, void *arg)
{
	struct aicwf_softc * const sc = arg;
	struct ifnet * const ifp = &sc->sc_if;
	struct mbuf *m;
	int error;

	atomic_swap_uint(&sc->sc_txqueued, 0);

	for (;;) {
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		if (!sc->sc_connected) {
			if_statinc(ifp, if_oerrors);
			m_freem(m);
			continue;
		}
		bpf_mtap(ifp, m, BPF_D_OUT);
		error = aicwf_tx_data(sc, m);
		if_statinc(ifp, error != 0 ? if_oerrors : if_opackets);
		m_freem(m);
	}
}

/* Writes to the chip sleep, so a thread sends the queue. */
static void
aicwf_start(struct ifnet *ifp)
{
	struct aicwf_softc * const sc = ifp->if_softc;

	if (atomic_load_relaxed(&sc->sc_dying))
		return;
	if (sc->sc_receive_only) {
		IFQ_PURGE(&ifp->if_snd);
		return;
	}
	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;
	if (atomic_swap_uint(&sc->sc_txqueued, 1) == 0)
		workqueue_enqueue(sc->sc_txq, &sc->sc_txwork, NULL);
}

/*
 * A received 802.11 data frame, already decrypted: turn it into an
 * Ethernet frame.
 */
/* Packet handoff is called with sc_epoch_lock held, after window mutation.
 * The RX producer lock prevents a timeout from overtaking an earlier batch. */
static void
aicwf_rx_input(struct aicwf_softc *sc, struct mbuf *m)
{
	struct mbuf *next;

	KASSERT(mutex_owned(&sc->sc_epoch_lock));
	for (; m != NULL; m = next) {
		next = m->m_nextpkt;
		m->m_nextpkt = NULL;
		if_percpuq_enqueue(sc->sc_if.if_percpuq, m);
	}
}

static void
aicwf_rx_free(struct mbuf *m)
{
	struct mbuf *next;

	for (; m != NULL; m = next) {
		next = m->m_nextpkt;
		m->m_nextpkt = NULL;
		m_freem(m);
	}
}

static void
aicwf_rx_discard(struct ieee80211_rxba_output *out)
{
	unsigned tid;

	for (tid = 0; tid < IEEE80211_RXBA_OFFLOAD_TIDS; tid++)
		aicwf_rx_free(out->ready[tid].head);
	aicwf_rx_free(out->discard);
	aicwf_rx_free(out->rejected);
}

static uint64_t
aicwf_rx_now(void)
{
	struct timespec ts;

	getnanouptime(&ts);
	if (ts.tv_sec < 0 || (uint64_t)ts.tv_sec >= UINT64_MAX / 1000)
		return UINT64_MAX;
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
aicwf_rx_schedule_locked(struct aicwf_softc *sc)
{
	uint64_t now, delay;

	KASSERT(mutex_owned(&sc->sc_epoch_lock));
	if (atomic_load_relaxed(&sc->sc_dying) || sc->sc_rxba.timer_token == 0) {
		callout_stop(&sc->sc_rxba_ch);
		return;
	}
	now = aicwf_rx_now();
	delay = sc->sc_rxba.deadline > now ? sc->sc_rxba.deadline - now : 1;
	callout_schedule(&sc->sc_rxba_ch,
	    MAX(1, mstohz(MIN(delay, AICWF_REORDER_WAIT_MS))));
}

/* Fence host-held packets before a sleeping firmware or deferred state change.
 * The firmware ABI has no key/BA generation: this fence covers host ownership,
 * not packets that firmware retained before the next FIFO read. */
static void
aicwf_rx_fence_locked(struct aicwf_softc *sc,
    struct ieee80211_rxba_output *out)
{

	KASSERT(mutex_owned(&sc->sc_epoch_lock));
	ieee80211_rxba_offload_retire(&sc->sc_rxba, out);
	sc->sc_rx_active = false;
	sc->sc_port_serial = 0;
	sc->sc_rx_join = 0;
	if (sc->sc_rx_serial < UINT64_MAX)
		sc->sc_rx_serial++;
	callout_stop(&sc->sc_rxba_ch);
}

static void
aicwf_rx_retire(struct aicwf_softc *sc)
{
	struct ieee80211_rxba_output out = {0};

	mutex_spin_enter(&sc->sc_epoch_lock);
	aicwf_rx_fence_locked(sc, &out);
	sc->sc_rx_assoc = 0;
	sc->sc_join_accepted = sc->sc_join_open = false;
	mutex_spin_exit(&sc->sc_epoch_lock);
	aicwf_rx_discard(&out);
}

static bool
aicwf_rx_begin(struct aicwf_softc *sc)
{
	struct ieee80211_rxba_output out = {0};
	bool active;

	mutex_spin_enter(&sc->sc_epoch_lock);
	if (sc->sc_rx_join == 0 || sc->sc_rx_join != sc->sc_rx_serial ||
	    (sc->sc_dc && (sc->sc_rx_vif != sc->sc_vif ||
	    !IEEE80211_ADDR_EQ(sc->sc_rx_bssid, sc->sc_ic.ic_bss->ni_bssid))) ||
	    (sc->sc_if.if_flags & IFF_RUNNING) == 0) {
		mutex_spin_exit(&sc->sc_epoch_lock);
		return false;
	}
	aicwf_rx_fence_locked(sc, &out);
	sc->sc_rx_assoc = sc->sc_rx_serial;
	sc->sc_rx_vif = sc->sc_vif;
	sc->sc_rx_ap = sc->sc_ap;
	memcpy(sc->sc_rx_bssid, sc->sc_ic.ic_bss->ni_bssid, ETHER_ADDR_LEN);
	sc->sc_rx_active = ieee80211_rxba_offload_configure(&sc->sc_rxba,
	    sc->sc_rx_assoc, sc->sc_rx_serial, AICWF_REORDER_WAIT_MS,
	    aicwf_rx_now(), &out) == 0;
	active = sc->sc_rx_active;
	mutex_spin_exit(&sc->sc_epoch_lock);
	aicwf_rx_discard(&out);
	return active;
}

/* Called only after the bound key and controlled-port firmware commands pass.
 * A later queued key change or disconnect prevents this completion reopening RX. */
static void
aicwf_rx_resume(struct aicwf_softc *sc, uint64_t serial)
{
	struct ieee80211_rxba_output out = {0};

	mutex_spin_enter(&sc->sc_epoch_lock);
	if (serial != 0 && serial == sc->sc_rx_serial && sc->sc_rx_assoc != 0) {
		aicwf_rx_fence_locked(sc, &out);
		sc->sc_rx_active = ieee80211_rxba_offload_configure(&sc->sc_rxba,
		    sc->sc_rx_assoc, sc->sc_rx_serial, AICWF_REORDER_WAIT_MS,
		    aicwf_rx_now(), &out) == 0;
		if (sc->sc_rx_active && sc->sc_rx_serial != UINT64_MAX)
			sc->sc_port_serial = sc->sc_rx_serial;
	}
	mutex_spin_exit(&sc->sc_epoch_lock);
	aicwf_rx_discard(&out);
}

static void
aicwf_rx_output(struct aicwf_softc *sc, struct ieee80211_rxba_output *out)
{
	unsigned tid;

	KASSERT(mutex_owned(&sc->sc_rx_lock));
	for (tid = 0; tid < IEEE80211_RXBA_OFFLOAD_TIDS; tid++) {
		struct ieee80211_rxba_batch *batch = &out->ready[tid];
		mutex_spin_enter(&sc->sc_epoch_lock);
		if (sc->sc_rx_active && ieee80211_rxba_offload_current(
		    &sc->sc_rxba, tid, batch)) {
			aicwf_rx_input(sc, batch->head);
			batch->head = NULL;
		}
		mutex_spin_exit(&sc->sc_epoch_lock);
	}
	sc->sc_ev_gap.ev_count += out->gaps;
	aicwf_rx_discard(out);
}

/* Convert the observed firmware reorder-run flag to explicit start/stop.
 * Firmware owns negotiation and the controlled port. No ADDBA is synthesized. */
static void
aicwf_rx_submit(struct aicwf_softc *sc, uint64_t serial, unsigned tid,
    unsigned seq, bool qos, bool reorder, struct mbuf *m)
{
	struct ieee80211_rxba_output out = {0};
	struct ieee80211_rxba_offload *ba = &sc->sc_rxba;
	uint64_t now;
	int error = 0;

	mutex_spin_enter(&sc->sc_rx_lock);
	mutex_spin_enter(&sc->sc_epoch_lock);
	if (!sc->sc_rx_active || serial != sc->sc_rx_serial)
		goto unlock;
	now = aicwf_rx_now();
	if (qos && reorder) {
		if (ba->tid[tid] == NULL) {
			error = ieee80211_rxba_offload_start(ba, tid, ba->epoch,
			    ba->key_epoch, ba->session[tid] + 1, seq,
			    IEEE80211_RXBA_WINDOW, now, &out);
		}
		if (error == 0) {
			(void)ieee80211_rxba_offload_input(ba, tid, ba->epoch,
			    ba->key_epoch, ba->session[tid], seq, now, m, &out);
			m = NULL; /* The complete MPDU is consumed on every return. */
		}
	} else if (qos && ba->tid[tid] != NULL) {
		(void)ieee80211_rxba_offload_stop(ba, tid, ba->epoch,
		    ba->key_epoch, ba->session[tid], 1, &out);
	}
	if (ba->epoch == 0)
		sc->sc_rx_active = false;
	aicwf_rx_schedule_locked(sc);
unlock:
	mutex_spin_exit(&sc->sc_epoch_lock);
	aicwf_rx_output(sc, &out);
	/* The non-reordered frame follows the drain from its old run. */
	mutex_spin_enter(&sc->sc_epoch_lock);
	if (m != NULL && !reorder && sc->sc_rx_active && serial == sc->sc_rx_serial) {
		aicwf_rx_input(sc, m);
		m = NULL;
	}
	mutex_spin_exit(&sc->sc_epoch_lock);
	aicwf_rx_free(m);
	mutex_spin_exit(&sc->sc_rx_lock);
}

static void
aicwf_rxba_timeout(void *arg)
{
	struct aicwf_softc *sc = arg;
	struct ieee80211_rxba_output out = {0};
	uint64_t token;

	mutex_spin_enter(&sc->sc_epoch_lock);
	token = sc->sc_rxba.timer_token;
	mutex_spin_exit(&sc->sc_epoch_lock);
	mutex_spin_enter(&sc->sc_rx_lock);
	mutex_spin_enter(&sc->sc_epoch_lock);
	ieee80211_rxba_offload_age(&sc->sc_rxba, token, aicwf_rx_now(), &out);
	if (sc->sc_rxba.epoch == 0)
		sc->sc_rx_active = false;
	aicwf_rx_schedule_locked(sc);
	mutex_spin_exit(&sc->sc_epoch_lock);
	aicwf_rx_output(sc, &out);
	mutex_spin_exit(&sc->sc_rx_lock);
}

/* One Ethernet packet from addresses and an LLC/SNAP payload. */
static struct mbuf *
aicwf_rx_ether(struct aicwf_softc *sc, const uint8_t *dst,
    const uint8_t *src, const uint8_t *llc, size_t llc_len)
{
	static const uint8_t snap[5] = { 0xaa, 0xaa, 0x03, 0, 0 };
	struct ether_header *eh;
	struct mbuf *m;

	if (llc_len < 8 || memcmp(llc, snap, sizeof(snap)) != 0 ||
	    (llc[5] != 0 && llc[5] != 0xf8))
		return NULL;
	const size_t len = ETHER_HDR_LEN + llc_len - 8;
	if (len + ETHER_ALIGN > MCLBYTES)
		return NULL;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	if (len + ETHER_ALIGN > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			return NULL;
		}
	}
	m->m_data += ETHER_ALIGN;
	eh = mtod(m, struct ether_header *);
	memcpy(eh->ether_dhost, dst, ETHER_ADDR_LEN);
	memcpy(eh->ether_shost, src, ETHER_ADDR_LEN);
	memcpy(&eh->ether_type, llc + 6, 2);
	memcpy(eh + 1, llc + 8, llc_len - 8);
	m->m_len = m->m_pkthdr.len = len;
	m->m_nextpkt = NULL;
	m_set_rcvif(m, &sc->sc_if);
	return m;
}

/* The net80211 parser bounds each read against the supplied image size. */
/* The shared reader ABI takes a mutable context; this reader only copies. */
static int
aicwf_amsdu_read(void *arg, size_t offset, size_t len, void *dst)
{

	memcpy(dst, (const uint8_t *)arg + offset, len);
	return 0;
}

/* Validate the full aggregate before publishing any Ethernet packet. */
static struct mbuf *
aicwf_rx_amsdu(struct aicwf_softc *sc, const uint8_t *data, size_t len)
{
	struct ieee80211_amsdu_subframe sub;
	struct mbuf *head = NULL, **tailp = &head, *m;
	size_t offset = 0;
	int result;

	while ((result = ieee80211_amsdu_next(aicwf_amsdu_read,
	    __UNCONST(data), len, sc->sc_ic.ic_myaddr, &offset, &sub)) == 1) {
		/* Complete validation before the first allocation. */
	}
	if (result != 0)
		return NULL;

	offset = 0;
	while ((result = ieee80211_amsdu_next(aicwf_amsdu_read,
	    __UNCONST(data), len, sc->sc_ic.ic_myaddr, &offset, &sub)) == 1) {
		m = aicwf_rx_ether(sc, sub.header, sub.header + ETHER_ADDR_LEN,
		    data + sub.offset + ETHER_HDR_LEN, sub.length);
		if (m == NULL) {
			aicwf_rx_free(head);
			return NULL;
		}
		*tailp = m;
		tailp = &m->m_nextpkt;
	}
	if (result != 0) {
		aicwf_rx_free(head);
		return NULL;
	}
	return head;
}

/* Keep peer/key/replay admission atomic with retirement and rekey. */
static void
aicwf_pmf_rx(struct aicwf_softc *sc, const uint8_t *frame, size_t len,
    uint32_t status, uint32_t flags)
{
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ieee80211req_sae *req;
	uint8_t query[28];
	uint64_t pn;
	uint16_t op;

	req = kmem_zalloc(sizeof(*req), KM_NOSLEEP);
	if (req == NULL)
		return;
	mutex_enter(&sc->sc_lock);
	if (!sc->sc_sae_enabled || !sc->sc_connected || !sc->sc_ptk_valid ||
	    !sc->sc_sae_authenticated || ic->ic_state != IEEE80211_S_RUN ||
	    ((flags >> 8) & 0xff) != sc->sc_vif ||
	    !IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, sc->sc_sae_bssid))
		goto drop;
	if (aicwf_pmf_unprot_valid(frame, len, ic->ic_myaddr,
	    sc->sc_sae_bssid)) {
		op = IEEE80211_SAE_UNPROT_DISCONNECT;
	} else if (aicwf_pmf_query_rx(frame, len, ic->ic_myaddr,
	    sc->sc_sae_bssid, status, sc->sc_hwkey[AICWF_KEY_SLOT_PAIRWISE],
	    sc->sc_pmf_rx_pn, query, &pn)) {
		op = IEEE80211_SAE_RX_SA_QUERY;
		sc->sc_pmf_rx_pn = pn;
		frame = query;
		len = sizeof(query);
	} else {
		goto drop;
	}
	IEEE80211_DPRINTF(ic, IEEE80211_MSG_STATE,
	    "PMF event 0x%x action/reason %u\n", op,
	    frame[op == IEEE80211_SAE_RX_SA_QUERY ? 25 : 24]);
	req->version = IEEE80211_SAE_VERSION;
	req->generation = sc->sc_sae_generation;
	req->op = op;
	req->len = len;
	memcpy(req->bssid, sc->sc_sae_bssid, ETHER_ADDR_LEN);
	memcpy(req->data, frame, len);
	mutex_exit(&sc->sc_lock);
	rt_ieee80211msg(ic->ic_ifp, RTM_IEEE80211_SAE, req,
	    offsetof(struct ieee80211req_sae, data) + len);
	kmem_free(req, sizeof(*req));
	return;
drop:
	mutex_exit(&sc->sc_lock);
	kmem_free(req, sizeof(*req));
}

/* Keep BTM policy in the supplicant; only valid disconnects retire the link. */
static void
aicwf_mgmt_rx(struct aicwf_softc *sc, const uint8_t *frame, size_t len,
    uint32_t status, uint32_t flags)
{
	const uint8_t *body;
	size_t body_len;
	uint64_t pn;
	bool disconnected = false;

	if (len < 24)
		return;
	if (frame[0] == 0xd0) {
		aicwf_sta_rx_event(sc, frame, len, status, flags);
		aicwf_pmf_rx(sc, frame, len, status, flags);
		return;
	}
	if (frame[0] != 0xc0 && frame[0] != 0xa0)
		return;
	/* Unprotected reasons 6/7 still start the validated SA Query path. */
	if ((frame[1] & 0x40) == 0 && sc->sc_sae_enabled) {
		aicwf_pmf_rx(sc, frame, len, status, flags);
		return;
	}
	mutex_enter(&sc->sc_lock);
	body = aicwf_mgmt_body(sc, frame, len, status, flags, &body_len, &pn);
	if (body != NULL && body_len >= 2) {
		sc->sc_pmf_rx_pn = pn;
		disconnected = true;
	}
	mutex_exit(&sc->sc_lock);
	if (disconnected)
		aicwf_alive_lost(sc);
}

static void
aicwf_rx_data(struct aicwf_softc *sc, const uint8_t *pkt, size_t mpdu_len,
    uint64_t serial)
{
	struct ifnet * const ifp = &sc->sc_if;
	const uint8_t * const wh = pkt + AICWF_RXHDR_LEN;
	const uint32_t status = le32dec(pkt + AICWF_RXHDR_STATUS);
	const uint32_t flags = le32dec(pkt + AICWF_RXHDR_FLAGS);
	uint16_t seq;
	struct mbuf *m = NULL;
	bool qos = false, amsdu = false, current;
	u_int tid = 0;
	size_t hdr;

	if (!sc->sc_if_attached || (flags & AICWF_RX_FLAG_UPLOAD) == 0)
		return;
	/* Any frame from the air, beacons included, proves the link lives. */
	sc->sc_alive_rx = ticks;
	/* Metadata only: protected management bodies may contain key material. */
	if (mpdu_len >= sizeof(struct ieee80211_frame) &&
	    (wh[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT &&
	    (wh[0] == IEEE80211_FC0_SUBTYPE_DEAUTH ||
	    wh[0] == IEEE80211_FC0_SUBTYPE_DISASSOC ||
	    wh[0] == 0xd0))	/* Action; absent from legacy net80211. */
		IEEE80211_DPRINTF(&sc->sc_ic, IEEE80211_MSG_STATE,
		    "management RX: subtype 0x%02x flags 0x%02x len %zu "
		    "status 0x%08x descriptor 0x%08x\n",
		    wh[0], wh[1], mpdu_len, status, flags);
	if (mpdu_len >= 2 && wh[0] == IEEE80211_FC0_SUBTYPE_AUTH) {
		/* Unassociated management frames can have no firmware VIF. */
		const uint8_t vif = (flags >> 8) & 0xff;
		if (vif == sc->sc_vif || vif == 0xff)
			aicwf_sae_event(sc, false, wh, mpdu_len);
		return;
	}
	if (mpdu_len >= 24 &&
	    (wh[0] == 0xc0 || wh[0] == 0xa0 || wh[0] == 0xd0)) {
		aicwf_mgmt_rx(sc, wh, mpdu_len, status, flags);
		return;
	}
	/* Beacons and data frames without a body come up too. */
	if (mpdu_len < sizeof(struct ieee80211_frame) + 8 ||
	    (wh[0] & (IEEE80211_FC0_TYPE_MASK |
	    IEEE80211_FC0_SUBTYPE_NODATA)) != IEEE80211_FC0_TYPE_DATA)
		return;
	seq = le16dec(wh + 22) >> 4;
	/* Whole frames from the access point. */
	if ((wh[1] & IEEE80211_FC1_DIR_MASK) != IEEE80211_FC1_DIR_FROMDS ||
	    (wh[1] & IEEE80211_FC1_MORE_FRAG) != 0 || (wh[22] & 0x0f) != 0)
		goto drop;
	/* hw_rxhdr flags_vif_idx and flags_sta_idx are bytes 1 and 2.
	 * The vendor ABI permits STA=0xff; the actual transmitter/BSSID and
	 * destination checks still bind such a frame to the current peer. */
	mutex_spin_enter(&sc->sc_epoch_lock);
	current = sc->sc_rx_active && serial == sc->sc_rx_serial &&
	    ((flags >> 8) & 0xff) == sc->sc_rx_vif &&
	    (((flags >> 16) & 0xff) == sc->sc_rx_ap ||
	    ((flags >> 16) & 0xff) == 0xff) &&
	    IEEE80211_ADDR_EQ(wh + 10, sc->sc_rx_bssid) &&
	    (ETHER_IS_MULTICAST(wh + 4) ||
	    IEEE80211_ADDR_EQ(wh + 4, sc->sc_ic.ic_myaddr));
	mutex_spin_exit(&sc->sc_epoch_lock);
	if (!current)
		goto drop;

	hdr = sizeof(struct ieee80211_frame);
	if ((wh[0] & IEEE80211_FC0_SUBTYPE_QOS) != 0) {
		qos = true;
		tid = wh[hdr] & 0x0f;
		amsdu = (wh[hdr] & 0x80) != 0;
		hdr += 2;
		if ((wh[1] & IEEE80211_FC1_ORDER) != 0)
			hdr += 4;
	}
	switch (__SHIFTOUT(status, AICWF_RX_DECR)) {
	case 0:
		break;
	case AICWF_RX_DECR_WEP:
		hdr += 4;
		break;
	case AICWF_RX_DECR_WAPI:
		goto drop;
	default:
		hdr += 8;
		break;
	}
	if (mpdu_len < hdr + 8)
		goto drop;

	if (!amsdu) {
		m = aicwf_rx_ether(sc, wh + 4, wh + 16, wh + hdr,
		    mpdu_len - hdr);
		if (m == NULL)
			goto drop;
	} else {
		/* Several packets in one frame, each with its addresses. */
		sc->sc_ev_amsdu.ev_count++;
		m = aicwf_rx_amsdu(sc, wh + hdr, mpdu_len - hdr);
		if (m == NULL)
			goto drop;
	}

	qos = qos && !ETHER_IS_MULTICAST(wh + 4);
	const bool reorder = qos && (flags & AICWF_RX_FLAG_REORDER) != 0;
	if (reorder)
		sc->sc_ev_ampdu.ev_count++;
	aicwf_rx_submit(sc, serial, tid, seq, qos, reorder, m);
	return;

drop:
	if_statinc(ifp, if_ierrors);
}

static int
aicwf_init(struct ifnet *ifp)
{
	struct aicwf_softc * const sc = ifp->if_softc;
	struct ieee80211com * const ic = &sc->sc_ic;

	if (atomic_load_relaxed(&sc->sc_dying))
		return ENXIO;
	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	if (ic->ic_roaming != IEEE80211_ROAMING_MANUAL)
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return 0;
}

static void
aicwf_stop(struct ifnet *ifp, int disable)
{
	struct aicwf_softc * const sc = ifp->if_softc;

	ifp->if_timer = 0;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ieee80211_new_state(&sc->sc_ic, IEEE80211_S_INIT, -1);
}

static void
aicwf_watchdog(struct ifnet *ifp)
{
	struct aicwf_softc * const sc = ifp->if_softc;

	ifp->if_timer = 0;
	ieee80211_watchdog(&sc->sc_ic);
}

/* State and WPA configuration stay unavailable on a receive-only radio. */
static int
aicwf_survey_ioctl(struct aicwf_softc *sc, unsigned type, bool set)
{
	if (!set) {
		if (type == IEEE80211_IOC_SCAN_RESULTS &&
		    sc->sc_survey_result_failed)
			return EIO;
		return 0;
	}
	switch (type) {
	case IEEE80211_IOC_SCAN_REQ:
		return aicwf_survey_scan_status(sc);
	case IEEE80211_IOC_CHANLIST:
		return 0;
	default:
		return EOPNOTSUPP;
	}
}

static int
aicwf_ioctl(struct ifnet *ifp, u_long cmd, void *data)
{
	struct aicwf_softc * const sc = ifp->if_softc;
	struct ieee80211com * const ic = &sc->sc_ic;
	int s, error = 0, oflags;

	if (atomic_load_relaxed(&sc->sc_dying))
		return ENXIO;
	if (sc->sc_receive_only) {
		if (cmd == SIOCS80211 || cmd == SIOCG80211) {
			const struct ieee80211req *ireq = data;
			error = aicwf_survey_ioctl(sc, ireq->i_type,
			    cmd == SIOCS80211);
			if (error != 0)
				return error;
		} else if (cmd == SIOCS80211NWID || cmd == SIOCS80211NWKEY ||
		    cmd == SIOCS80211AUTH || cmd == SIOCS80211BSSID ||
		    cmd == SIOCS80211POWER)
			return EOPNOTSUPP;
	}
	if ((cmd == SIOCG80211 || cmd == SIOCS80211) &&
	    ((struct ieee80211req *)data)->i_type == IEEE80211_IOC_SAE)
		return aicwf_sae_ioctl(sc, cmd, data);
	s = splnet();

	switch (cmd) {
	case SIOCSIFFLAGS:
		oflags = ifp->if_flags;
		if ((error = ifioctl_common(ifp, cmd, data)) != 0)
			break;
		switch (ifp->if_flags & (IFF_UP | IFF_RUNNING)) {
		case IFF_UP:
			if ((oflags & IFF_UP) == 0)
				aicwf_init(ifp);
			break;
		case IFF_RUNNING:
			if ((oflags & IFF_UP) != 0)
				aicwf_stop(ifp, 1);
			break;
		default:
			break;
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* The firmware passes every multicast frame of the network. */
		if ((error = ether_ioctl(ifp, cmd, data)) == ENETRESET)
			error = 0;
		break;

	default:
		error = ieee80211_ioctl(ic, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			aicwf_init(ifp);
		error = 0;
	}

	splx(s);

	return error;
}

/* Give the system a wireless network interface. */
int
aicwf_ifattach(struct aicwf_softc *sc)
{
	static const uint8_t chan5[] = {
		36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120,
		124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165
	};
	struct ieee80211com * const ic = &sc->sc_ic;
	struct ifnet * const ifp = &sc->sc_if;
	u_int n;

	int error;

	error = workqueue_create(&sc->sc_taskq, device_xname(sc->sc_dev),
	    aicwf_task, sc, PRI_NONE, IPL_NET, 0);
	if (error != 0)
		return error;
	error = workqueue_create(&sc->sc_txq, "aicwftx", aicwf_tx_task, sc,
	    PRI_NONE, IPL_NET, 0);
	if (error != 0) {
		workqueue_destroy(sc->sc_taskq);
		sc->sc_taskq = NULL;
		return error;
	}
	sc->sc_taskpool = pool_cache_init(sizeof(struct aicwf_task), 0, 0, 0,
	    "aicwftask", NULL, IPL_NET, NULL, NULL, NULL);
	pool_cache_prime(sc->sc_taskpool, AICWF_TASK_COUNT);
	memset(sc->sc_hwkey, AICWF_HWKEY_NONE, sizeof(sc->sc_hwkey));

	ic->ic_ifp = ifp;
	ic->ic_phytype = IEEE80211_T_OFDM;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_caps = sc->sc_receive_only ? 0 : IEEE80211_C_AES_CCM | IEEE80211_C_SHSLOT |
	    IEEE80211_C_SHPREAMBLE | IEEE80211_C_WPA;
	memcpy(ic->ic_myaddr, sc->sc_enaddr, ETHER_ADDR_LEN);
	ic->ic_ibss_chan = &ic->ic_channels[0];

	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
	for (n = 1; n <= (sc->sc_dc ? 11 : 13); n++) {
		ic->ic_channels[n].ic_freq =
		    ieee80211_ieee2mhz(n, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[n].ic_flags = IEEE80211_CHAN_CCK |
		    IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN |
		    IEEE80211_CHAN_2GHZ;
		if (sc->sc_receive_only)
			ic->ic_channels[n].ic_flags |= IEEE80211_CHAN_PASSIVE;
	}
	if (sc->sc_5ghz) {
		ic->ic_sup_rates[IEEE80211_MODE_11A] =
		    ieee80211_std_rateset_11a;
		for (n = 0; n < __arraycount(chan5); n++) {
			ic->ic_channels[chan5[n]].ic_freq =
			    ieee80211_ieee2mhz(chan5[n], IEEE80211_CHAN_5GHZ);
			ic->ic_channels[chan5[n]].ic_flags = IEEE80211_CHAN_A;
		}
	}

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = aicwf_init;
	ifp->if_ioctl = aicwf_ioctl;
	ifp->if_start = aicwf_start;
	ifp->if_stop = aicwf_stop;
	ifp->if_watchdog = aicwf_watchdog;
	IFQ_SET_READY(&ifp->if_snd);
	strlcpy(ifp->if_xname, device_xname(sc->sc_dev), sizeof(ifp->if_xname));

	if_initialize(ifp);
	ieee80211_ifattach(ic);
	if (sc->sc_receive_only)
		ic->ic_roaming = IEEE80211_ROAMING_MANUAL;
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = aicwf_newstate;
	ic->ic_sta_mgmt = aicwf_sta_mgmt;
	ic->ic_newassoc = aicwf_newassoc;
	ic->ic_send_mgmt = aicwf_send_mgmt;
	ic->ic_recv_mgmt = aicwf_recv_mgmt;
	ic->ic_crypto.cs_key_set = aicwf_key_set;
	ic->ic_crypto.cs_key_delete = aicwf_key_delete;

	ifp->if_percpuq = if_percpuq_create(ifp);
	if_deferred_start_init(ifp, NULL);
	if_register(ifp);
	ieee80211_media_init(ic, aicwf_media_change, ieee80211_media_status);

	ieee80211_announce(ic);

	memset(sc->sc_igtk, AICWF_HWKEY_NONE, sizeof(sc->sc_igtk));
	evcnt_attach_dynamic(&sc->sc_ev_sae_start, EVCNT_TYPE_MISC, NULL,
	    device_xname(sc->sc_dev), "SAE starts");
	evcnt_attach_dynamic(&sc->sc_ev_sae_rx, EVCNT_TYPE_MISC, NULL,
	    device_xname(sc->sc_dev), "SAE received");
	evcnt_attach_dynamic(&sc->sc_ev_sae_tx, EVCNT_TYPE_MISC, NULL,
	    device_xname(sc->sc_dev), "SAE sent");
	evcnt_attach_dynamic(&sc->sc_ev_sae_drop, EVCNT_TYPE_MISC, NULL,
	    device_xname(sc->sc_dev), "SAE rejected");
	sc->sc_if_attached = true;
	callout_schedule(&sc->sc_alive_ch, mstohz(AICWF_ALIVE_PERIOD_MS));
	return 0;
}

void
aicwf_prepare(struct aicwf_softc *sc, device_t self,
    const struct aicwf_bus_ops *ops, void *bus, bool dc)
{
	sc->sc_dev = self;
	sc->sc_ops = ops;
	sc->sc_bus = bus;
	sc->sc_dc = dc;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->sc_epoch_lock, MUTEX_DEFAULT, IPL_NET);
	mutex_init(&sc->sc_rx_lock, MUTEX_DEFAULT, IPL_NET);
	callout_init(&sc->sc_rxba_ch, CALLOUT_MPSAFE);
	callout_setfunc(&sc->sc_rxba_ch, aicwf_rxba_timeout, sc);
	callout_init(&sc->sc_alive_ch, CALLOUT_MPSAFE);
	callout_setfunc(&sc->sc_alive_ch, aicwf_alive_tick, sc);
	sc->sc_alive_rx = ticks;
	evcnt_attach_dynamic(&sc->sc_ev_ampdu, EVCNT_TYPE_MISC, NULL,
	    device_xname(self), "A-MPDU frames");
	evcnt_attach_dynamic(&sc->sc_ev_amsdu, EVCNT_TYPE_MISC, NULL,
	    device_xname(self), "A-MSDU frames");
	evcnt_attach_dynamic(&sc->sc_ev_gap, EVCNT_TYPE_MISC, NULL,
	    device_xname(self), "A-MPDU gaps");
	cv_init(&sc->sc_cv, "aicwfcmd");
	sc->sc_txbuf = kmem_alloc(AICWF_TX_MAX, KM_SLEEP);
}

/* The bus stops receive producers before freeing station state. */
void
aicwf_quiesce(struct aicwf_softc *sc)
{
	mutex_enter(&sc->sc_lock);
	atomic_store_relaxed(&sc->sc_dying, true);
	cv_broadcast(&sc->sc_cv);
	mutex_exit(&sc->sc_lock);
	if (sc->sc_if_attached) {
		IFNET_LOCK(&sc->sc_if);
		if_deactivate(&sc->sc_if);
		IFNET_UNLOCK(&sc->sc_if);
	}
	callout_halt(&sc->sc_alive_ch, NULL);
	callout_halt(&sc->sc_rxba_ch, NULL);
}

void
aicwf_release(struct aicwf_softc *sc)
{
	struct ifnet * const ifp = &sc->sc_if;

	if (sc->sc_if_attached) {
		ifp->if_timer = 0;
		ifp->if_flags &= ~IFF_RUNNING;
		aicwf_rx_retire(sc);
	}
	if (sc->sc_txq != NULL)
		workqueue_wait(sc->sc_txq, &sc->sc_txwork);
	if (sc->sc_taskq != NULL) {
		workqueue_enqueue(sc->sc_taskq, &sc->sc_barrier, NULL);
		workqueue_wait(sc->sc_taskq, &sc->sc_barrier);
	}
	if (sc->sc_if_attached) {
		bpf_detach(ifp);
		ieee80211_ifdetach(&sc->sc_ic);
		if_detach(ifp);
		evcnt_detach(&sc->sc_ev_sae_start);
		evcnt_detach(&sc->sc_ev_sae_rx);
		evcnt_detach(&sc->sc_ev_sae_tx);
		evcnt_detach(&sc->sc_ev_sae_drop);
	}
	if (sc->sc_taskq != NULL)
		workqueue_destroy(sc->sc_taskq);
	if (sc->sc_txq != NULL)
		workqueue_destroy(sc->sc_txq);
	if (sc->sc_taskpool != NULL)
		pool_cache_destroy(sc->sc_taskpool);
	evcnt_detach(&sc->sc_ev_ampdu);
	evcnt_detach(&sc->sc_ev_amsdu);
	evcnt_detach(&sc->sc_ev_gap);
	callout_destroy(&sc->sc_alive_ch);
	callout_destroy(&sc->sc_rxba_ch);
	explicit_memset(sc->sc_txbuf, 0, AICWF_TX_MAX);
	kmem_free(sc->sc_txbuf, AICWF_TX_MAX);
	cv_destroy(&sc->sc_cv);
	mutex_destroy(&sc->sc_lock);
	mutex_destroy(&sc->sc_rx_lock);
	mutex_destroy(&sc->sc_epoch_lock);
}
