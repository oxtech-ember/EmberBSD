/*-
 * Copyright (c) 2026 The NetBSD Foundation, Inc.
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

/*
 * The network interface of the ESP32-S31 radio.
 *
 * The vendor's full-MAC library trades Ethernet data frames with the OS.
 * An optional net80211 control path receives the original management frames.
 * Frames go out from a radio thread because the vendor call may sleep;
 * frames come in on the thread of the libraries.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include "opt_espwifi_net80211.h"
#include "opt_espwifi_wpa2.h"

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/condvar.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net/route.h>

#ifdef ESPWIFI_NET80211
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_proto.h>
#include <net80211/ieee80211_rsn.h>
#define ESPWIFI_SCAN_RING 8
#define ESPWIFI_SCAN_FRAME_MAX 2304
#define ESPWIFI_SAE_WAIT_SECONDS 5
#define ESPWIFI_SAE_HEADER_LEN 30
#define ESPWIFI_SAE_BODY_MAX (IEEE80211_SAE_DATA_MAX - ESPWIFI_SAE_HEADER_LEN)
#define ESPWIFI_SAE_WAIT_NONE 0
#define ESPWIFI_SAE_WAIT_TX 1
#define ESPWIFI_SAE_WAIT_STATUS 2
#define ESPWIFI_SAE_WAIT_RX 3
#define ESPWIFI_SAE_REPLY_NONE 0
#define ESPWIFI_SAE_REPLY_TX 1
#define ESPWIFI_SAE_REPLY_STATUS 2
#define ESPWIFI_SAE_REPLY_ANY 3
#define ESPWIFI_KEY_REQUEST_CCMP 0
#define ESPWIFI_KEY_REQUEST_IGTK 1
#define ESPWIFI_SAE_CAPS \
	(IEEE80211_SAE_CAP_EXTERNAL | IEEE80211_SAE_CAP_PMF)
struct espwifi_scan_frame {
	unsigned len;
	unsigned channel;
	uint32_t timestamp;
	uint64_t generation;
	int rssi;
	uint8_t data[ESPWIFI_SCAN_FRAME_MAX];
};
#endif

#include <riscv/espressif/esp_wifi_os.h>
#include <riscv/espressif/esp_wifi_var.h>

#define	ESPWIFI_MTU_FRAME	1600

struct espwifi_softc {
	struct ethercom	sc_ec;
	struct ifmedia	sc_media;
#ifdef ESPWIFI_NET80211
	struct ieee80211com sc_ic;
	kmutex_t	sc_scan_lock;
	kmutex_t	sc_key_lock;
	kcondvar_t	sc_key_cv;
	kmutex_t	sc_sae_lock;
	kcondvar_t	sc_sae_cv;
	bool		sc_sae_enabled, sc_sae_start_sent;
	bool		sc_sae_igtk_busy;
	bool		sc_sae_selection;
	unsigned	 sc_sae_waiting, sc_sae_reply;
	int		sc_sae_reply_status;
	unsigned	 sc_sae_reply_len;
	uint32_t	sc_sae_generation;
	uint32_t	sc_sae_selection_generation;
	uint8_t	sc_sae_bssid[ETHER_ADDR_LEN];
	uint8_t	sc_sae_selection_bssid[ETHER_ADDR_LEN];
	uint8_t	sc_sae_reply_frame[IEEE80211_SAE_DATA_MAX];
	uint32_t	sc_igtk_delete_generation;
	uint8_t	sc_igtk_delete_bssid[ETHER_ADDR_LEN];
	bool		sc_key_pending, sc_key_running, sc_key_done;
	bool		sc_key_abandoned;
	int		sc_key_error;
	uint64_t	sc_key_request_generation;
	uint8_t	sc_key_request_mac[ETHER_ADDR_LEN];
	uint8_t	sc_key_request_seq[6];
	uint8_t	sc_key_request_data[16];
	unsigned	sc_key_request_index;
	unsigned	sc_key_request_type;
	int		sc_key_request_group, sc_key_request_xmit;
	void		*sc_scan_sem;
	void		*sc_scan_ctl_sem;
	unsigned	sc_scan_head, sc_scan_count;
	uint64_t	sc_scan_generation, sc_scan_active;
	bool		sc_scan_enabled, sc_scan_pending, sc_scan_busy, sc_radio_ready;
	bool		sc_scan_shutdown;
	unsigned	sc_port_authorized;
	bool		sc_ptk_installed, sc_sta_started, sc_join_stopped;
	bool		sc_join_sae;
	uint8_t	sc_key_slots, sc_group_key_slots, sc_igtk_slots;
	uint64_t	sc_key_generation;
	uint64_t	sc_join_generation, sc_join_active;
	uint8_t	sc_join_bssid[ETHER_ADDR_LEN];
	bool		sc_join_pending, sc_joined, sc_join_disconnect_pending;
	bool		sc_join_barrier_busy;
	bool		sc_join_rescan_pending;
	uint8_t	sc_join_ssid[IEEE80211_NWID_LEN];
	unsigned	sc_join_ssid_len;
	uint8_t	sc_join_rsn[IEEE80211_MAX_OPT_IE];
	unsigned	sc_join_rsn_len;
	int		(*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);
	struct espwifi_scan_frame sc_scan_ring[ESPWIFI_SCAN_RING];
#endif
	struct if_percpuq *sc_ipq;
	void		*sc_txsem;
	uint8_t		*sc_txbuf;
	uint8_t		sc_mac[ETHER_ADDR_LEN];
	bool		sc_attached;
	bool		sc_tx_ready;
	bool		sc_link;
};

static struct espwifi_softc espwifi_sc;

#ifdef ESPWIFI_NET80211_WPA2
static int espwifi_if_igtk_install(struct espwifi_softc *,
	    const struct ieee80211req_sae *, uint64_t);
static int espwifi_if_igtk_delete(struct espwifi_softc *,
	    const struct ieee80211req_sae *, uint64_t);
#endif

#ifdef ESPWIFI_NET80211
#ifdef ESPWIFI_NET80211_WPA2
static int
espwifi_if_sae_notify(struct espwifi_softc *sc, uint16_t op,
    uint32_t generation, const uint8_t *bssid, const void *data, unsigned len)
{
	struct ieee80211req_sae *req;

	if (len > IEEE80211_SAE_DATA_MAX || (len != 0 && data == NULL))
		return EINVAL;
	req = kmem_zalloc(sizeof(*req), KM_NOSLEEP);
	if (req == NULL)
		return ENOMEM;
	req->version = IEEE80211_SAE_VERSION;
	req->generation = generation;
	req->op = op;
	req->len = len;
	memcpy(req->bssid, bssid, ETHER_ADDR_LEN);
	if (len != 0)
		memcpy(req->data, data, len);
	rt_ieee80211msg(&sc->sc_ec.ec_if, RTM_IEEE80211_SAE, req,
	    offsetof(struct ieee80211req_sae, data) + len);
	explicit_memset(req, 0, sizeof(*req));
	kmem_free(req, sizeof(*req));
	return 0;
}

static bool
espwifi_if_sae_current(struct espwifi_softc *sc, uint32_t generation,
    const uint8_t *bssid)
{
	return sc->sc_sae_enabled && generation == sc->sc_sae_generation &&
	    memcmp(bssid, sc->sc_sae_bssid, ETHER_ADDR_LEN) == 0;
}

static bool
espwifi_if_sae_teardown_pending(struct espwifi_softc *sc)
{
	bool pending;

	mutex_spin_enter(&sc->sc_scan_lock);
	pending = sc->sc_join_disconnect_pending || sc->sc_join_barrier_busy;
	mutex_spin_exit(&sc->sc_scan_lock);
	return pending;
}

static void
espwifi_if_sae_teardown_complete(struct espwifi_softc *sc)
{
	mutex_enter(&sc->sc_sae_lock);
	cv_broadcast(&sc->sc_sae_cv);
	mutex_exit(&sc->sc_sae_lock);
}

static int
espwifi_if_sae_wait(struct espwifi_softc *sc, uint32_t generation,
	    unsigned reply, unsigned int type, uint8_t *body, unsigned int capacity,
	    unsigned int *len)
{
	int error = 0, remain;
	u_int deadline = getticks() + MAX(1, hz * ESPWIFI_SAE_WAIT_SECONDS);

	mutex_enter(&sc->sc_sae_lock);
	if (generation != sc->sc_sae_generation) {
		mutex_exit(&sc->sc_sae_lock);
		return ESTALE;
	}
	while (espwifi_if_sae_current(sc, generation, sc->sc_sae_bssid) &&
	    (reply == ESPWIFI_SAE_REPLY_ANY ?
	    sc->sc_sae_reply == ESPWIFI_SAE_REPLY_NONE :
	    sc->sc_sae_reply != reply)) {
		remain = (int)(deadline - getticks());
		if (remain <= 0) {
			error = ETIMEDOUT;
			break;
		}
		error = cv_timedwait(&sc->sc_sae_cv, &sc->sc_sae_lock,
		    (u_int)remain);
		if (error == EWOULDBLOCK) {
			if (generation != sc->sc_sae_generation) {
				error = ESTALE;
				break;
			}
			if (espwifi_if_sae_current(sc, generation, sc->sc_sae_bssid) &&
			    (reply == ESPWIFI_SAE_REPLY_ANY ?
			    sc->sc_sae_reply != ESPWIFI_SAE_REPLY_NONE :
			    sc->sc_sae_reply == reply)) {
				error = 0;
				break;
			}
			error = ETIMEDOUT;
			break;
		}
		if (error != 0)
			break;
	}
	if (error == 0 && !espwifi_if_sae_current(sc, generation,
	    sc->sc_sae_bssid))
		error = ESTALE;
	/* A replaced session owns every mailbox field after this point. */
	if (generation != sc->sc_sae_generation) {
		mutex_exit(&sc->sc_sae_lock);
		return ESTALE;
	}
	if (error == 0 && (reply == ESPWIFI_SAE_REPLY_ANY ?
	    sc->sc_sae_reply == ESPWIFI_SAE_REPLY_NONE :
	    sc->sc_sae_reply != reply))
		error = ECANCELED;
	if (error == 0 && reply == ESPWIFI_SAE_REPLY_TX) {
		if (sc->sc_sae_reply_len < ESPWIFI_SAE_HEADER_LEN ||
		    le16dec(sc->sc_sae_reply_frame + 26) != type ||
		    sc->sc_sae_reply_len - ESPWIFI_SAE_HEADER_LEN > capacity) {
			error = EPROTO;
		} else {
			*len = sc->sc_sae_reply_len - ESPWIFI_SAE_HEADER_LEN;
			memcpy(body, sc->sc_sae_reply_frame + ESPWIFI_SAE_HEADER_LEN,
			    *len);
		}
	} else if (error == 0 && (reply == ESPWIFI_SAE_REPLY_STATUS ||
	    (reply == ESPWIFI_SAE_REPLY_ANY &&
	    sc->sc_sae_reply == ESPWIFI_SAE_REPLY_STATUS))) {
		error = sc->sc_sae_reply_status == 0 ? 0 : EACCES;
	}
	if (!(error == 0 && reply == ESPWIFI_SAE_REPLY_ANY &&
	    sc->sc_sae_reply == ESPWIFI_SAE_REPLY_TX)) {
		sc->sc_sae_reply = ESPWIFI_SAE_REPLY_NONE;
		sc->sc_sae_reply_len = 0;
		explicit_memset(sc->sc_sae_reply_frame,
		    0, sizeof(sc->sc_sae_reply_frame));
	}
	sc->sc_sae_waiting = ESPWIFI_SAE_WAIT_NONE;
	if (error == ETIMEDOUT) {
		sc->sc_sae_enabled = false;
		sc->sc_sae_start_sent = false;
		explicit_memset(sc->sc_sae_bssid, 0, sizeof(sc->sc_sae_bssid));
		mutex_spin_enter(&sc->sc_scan_lock);
		sc->sc_sae_selection = false;
		sc->sc_sae_selection_generation = 0;
		explicit_memset(sc->sc_sae_selection_bssid, 0,
		    sizeof(sc->sc_sae_selection_bssid));
		mutex_spin_exit(&sc->sc_scan_lock);
		if (++sc->sc_sae_generation == 0)
			sc->sc_sae_generation++;
	}
	cv_broadcast(&sc->sc_sae_cv);
	mutex_exit(&sc->sc_sae_lock);
	return error;
}

static void
espwifi_if_sae_clear_locked(struct espwifi_softc *sc)
{
	sc->sc_sae_enabled = false;
	sc->sc_sae_start_sent = false;
	sc->sc_sae_waiting = ESPWIFI_SAE_WAIT_NONE;
	sc->sc_sae_reply = ESPWIFI_SAE_REPLY_NONE;
	sc->sc_sae_reply_len = 0;
	explicit_memset(sc->sc_sae_bssid, 0, sizeof(sc->sc_sae_bssid));
	explicit_memset(sc->sc_sae_reply_frame,
	    0, sizeof(sc->sc_sae_reply_frame));
	if (++sc->sc_sae_generation == 0)
		sc->sc_sae_generation++;
	mutex_spin_enter(&sc->sc_scan_lock);
	sc->sc_sae_selection = false;
	sc->sc_sae_selection_generation = 0;
	explicit_memset(sc->sc_sae_selection_bssid, 0,
	    sizeof(sc->sc_sae_selection_bssid));
	mutex_spin_exit(&sc->sc_scan_lock);
	cv_broadcast(&sc->sc_sae_cv);
}

static void
espwifi_if_sae_cancel_generation(struct espwifi_softc *sc,
	    uint32_t generation)
{
	mutex_enter(&sc->sc_sae_lock);
	if (sc->sc_sae_generation == generation)
		espwifi_if_sae_clear_locked(sc);
	mutex_exit(&sc->sc_sae_lock);
}

static void
espwifi_if_sae_cancel(struct espwifi_softc *sc)
{
	mutex_enter(&sc->sc_sae_lock);
	espwifi_if_sae_clear_locked(sc);
	mutex_exit(&sc->sc_sae_lock);
}

static int
espwifi_if_sae_configure(struct espwifi_softc *sc,
	    const struct ieee80211req_sae *req)
{
	int error = 0, remain;
	u_int deadline = getticks() + MAX(1, hz * ESPWIFI_SAE_WAIT_SECONDS);

	if (req->len != 1)
		return EINVAL;
	error = ieee80211_sae_configure_check(&sc->sc_ic, ESPWIFI_SAE_CAPS,
	    req->data[0] != 0);
	if (error != 0)
		return error;
	mutex_enter(&sc->sc_sae_lock);
	while (sc->sc_sae_igtk_busy || espwifi_if_sae_teardown_pending(sc)) {
		remain = (int)(deadline - getticks());
		if (remain <= 0) {
			error = ETIMEDOUT;
			break;
		}
		error = cv_timedwait(&sc->sc_sae_cv, &sc->sc_sae_lock,
		    (u_int)remain);
		if (error == EWOULDBLOCK) {
			if (!sc->sc_sae_igtk_busy &&
			    !espwifi_if_sae_teardown_pending(sc))
				error = 0;
			else
				error = ETIMEDOUT;
		}
		if (error != 0)
			break;
	}
	if (error != 0) {
		mutex_exit(&sc->sc_sae_lock);
		return error;
	}
	sc->sc_sae_enabled = req->data[0] != 0;
	sc->sc_sae_start_sent = false;
	sc->sc_sae_waiting = ESPWIFI_SAE_WAIT_NONE;
	sc->sc_sae_reply = ESPWIFI_SAE_REPLY_NONE;
	sc->sc_sae_reply_len = 0;
	sc->sc_sae_generation = req->generation;
	memcpy(sc->sc_sae_bssid, req->bssid, sizeof(sc->sc_sae_bssid));
	explicit_memset(sc->sc_sae_reply_frame,
	    0, sizeof(sc->sc_sae_reply_frame));
	mutex_spin_enter(&sc->sc_scan_lock);
	sc->sc_sae_selection = req->data[0] != 0;
	sc->sc_sae_selection_generation = req->generation;
	memcpy(sc->sc_sae_selection_bssid, req->bssid,
	    sizeof(sc->sc_sae_selection_bssid));
	sc->sc_igtk_delete_generation = 0;
	explicit_memset(sc->sc_igtk_delete_bssid, 0,
	    sizeof(sc->sc_igtk_delete_bssid));
	mutex_spin_exit(&sc->sc_scan_lock);
	/* Do not accept a config raced by a newly queued old-session teardown. */
	if (espwifi_if_sae_teardown_pending(sc)) {
		espwifi_if_sae_clear_locked(sc);
		mutex_exit(&sc->sc_sae_lock);
		return EBUSY;
	}
	cv_broadcast(&sc->sc_sae_cv);
	mutex_exit(&sc->sc_sae_lock);
	return 0;
}

static bool
espwifi_if_sae_frame_valid(struct espwifi_softc *sc,
	    const struct ieee80211req_sae *req)
{
	return ieee80211_sae_auth_frame_validate(req->data, req->len,
	    req->bssid, sc->sc_ic.ic_myaddr, req->bssid, 0) == 0;
}

static int
espwifi_if_sae_request(struct ieee80211com *ic,
	    const struct ieee80211req_sae *req)
{
	struct espwifi_softc * const sc = ic->ic_ifp->if_softc;
	int error = EINVAL, s;

	if (req->op == IEEE80211_SAE_CONFIGURE)
		return espwifi_if_sae_configure(sc, req);
	if (req->op == IEEE80211_SAE_SET_IGTK ||
	    req->op == IEEE80211_SAE_DELETE_IGTK) {
		uint64_t join_generation;
		bool current;
		u_int deadline = getticks() +
		    MAX(1, hz * ESPWIFI_SAE_WAIT_SECONDS);
		int remain;

		if (req->op == IEEE80211_SAE_DELETE_IGTK && req->len == 2) {
			mutex_spin_enter(&sc->sc_scan_lock);
			if (sc->sc_igtk_delete_generation == req->generation &&
			    memcmp(sc->sc_igtk_delete_bssid, req->bssid,
			    ETHER_ADDR_LEN) == 0) {
				mutex_spin_exit(&sc->sc_scan_lock);
				return 0;
			}
			mutex_spin_exit(&sc->sc_scan_lock);
		}
		mutex_enter(&sc->sc_sae_lock);
		while (sc->sc_sae_igtk_busy &&
		    espwifi_if_sae_current(sc, req->generation, req->bssid)) {
			remain = (int)(deadline - getticks());
			if (remain <= 0) {
				mutex_exit(&sc->sc_sae_lock);
				return ETIMEDOUT;
			}
			error = cv_timedwait(&sc->sc_sae_cv, &sc->sc_sae_lock,
			    (u_int)remain);
			if (error == EWOULDBLOCK) {
				if (sc->sc_sae_igtk_busy &&
				    espwifi_if_sae_current(sc, req->generation,
				    req->bssid)) {
					mutex_exit(&sc->sc_sae_lock);
					return ETIMEDOUT;
				}
				error = 0;
			}
			if (error != 0) {
				mutex_exit(&sc->sc_sae_lock);
				return error;
			}
		}
		if (!espwifi_if_sae_current(sc, req->generation, req->bssid)) {
			mutex_exit(&sc->sc_sae_lock);
			return ESTALE;
		}
		/* Serialize selection validation with CONFIGURE before dropping lock. */
		mutex_spin_enter(&sc->sc_scan_lock);
		current = sc->sc_sae_selection && sc->sc_join_sae &&
		    sc->sc_sae_selection_generation == req->generation &&
		    memcmp(sc->sc_sae_selection_bssid, req->bssid,
		    ETHER_ADDR_LEN) == 0 && sc->sc_join_active != 0 &&
		    sc->sc_join_active == sc->sc_join_generation &&
		    sc->sc_joined && sc->sc_link && sc->sc_radio_ready &&
		    sc->sc_sta_started && !sc->sc_join_stopped &&
		    memcmp(sc->sc_join_bssid, req->bssid, ETHER_ADDR_LEN) == 0;
		join_generation = sc->sc_join_active;
		mutex_spin_exit(&sc->sc_scan_lock);
		if (!current) {
			mutex_exit(&sc->sc_sae_lock);
			return ESTALE;
		}
		sc->sc_sae_igtk_busy = true;
		mutex_exit(&sc->sc_sae_lock);

		error = req->op == IEEE80211_SAE_SET_IGTK ?
		    espwifi_if_igtk_install(sc, req, join_generation) :
		    espwifi_if_igtk_delete(sc, req, join_generation);
		mutex_enter(&sc->sc_sae_lock);
		sc->sc_sae_igtk_busy = false;
		cv_broadcast(&sc->sc_sae_cv);
		mutex_exit(&sc->sc_sae_lock);
		return error;
	}
	s = splnet();
	mutex_enter(&sc->sc_sae_lock);
	if (!espwifi_if_sae_current(sc, req->generation, req->bssid) ||
	    ic->ic_bss == NULL || ic->ic_state != IEEE80211_S_AUTH ||
	    memcmp(req->bssid, ic->ic_bss->ni_bssid, ETHER_ADDR_LEN) != 0) {
		error = ESTALE;
		goto out;
	}
	switch (req->op) {
	case IEEE80211_SAE_TX_FRAME:
		if ((sc->sc_sae_waiting != ESPWIFI_SAE_WAIT_NONE &&
		    sc->sc_sae_waiting != ESPWIFI_SAE_WAIT_TX &&
		    sc->sc_sae_waiting != ESPWIFI_SAE_WAIT_RX) ||
		    !espwifi_if_sae_frame_valid(sc, req) ||
		    sc->sc_sae_reply != ESPWIFI_SAE_REPLY_NONE)
			break;
		memcpy(sc->sc_sae_reply_frame, req->data, req->len);
		sc->sc_sae_reply_len = req->len;
		sc->sc_sae_reply = ESPWIFI_SAE_REPLY_TX;
		error = 0;
		cv_broadcast(&sc->sc_sae_cv);
		break;
	case IEEE80211_SAE_AUTH_STATUS:
		if (req->len != 2 || (sc->sc_sae_waiting !=
		    ESPWIFI_SAE_WAIT_STATUS && sc->sc_sae_waiting !=
		    ESPWIFI_SAE_WAIT_RX) ||
		    sc->sc_sae_reply != ESPWIFI_SAE_REPLY_NONE)
			break;
		sc->sc_sae_reply_status = le16dec(req->data);
		sc->sc_sae_reply = ESPWIFI_SAE_REPLY_STATUS;
		error = 0;
		cv_broadcast(&sc->sc_sae_cv);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
out:
	mutex_exit(&sc->sc_sae_lock);
	splx(s);
	return error;
}

int
espwifi_if_sae_build(const unsigned char *bssid, unsigned int type,
	    unsigned char *body, unsigned int capacity, unsigned int *len)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	struct ieee80211_node *ni;
	uint8_t ssid[IEEE80211_NWID_LEN];
	unsigned int ssid_len = 0;
	uint32_t generation;
	bool send_start = false;
	int error, s;

	if (bssid == NULL || body == NULL || len == NULL || capacity == 0 ||
	    (type != ESPWIFI_SAE_COMMIT && type != ESPWIFI_SAE_CONFIRM))
		return EINVAL;
	*len = 0;
	s = splnet();
	ni = sc->sc_ic.ic_bss;
	if (sc->sc_ic.ic_state != IEEE80211_S_AUTH || ni == NULL ||
	    memcmp(ni->ni_bssid, bssid, ETHER_ADDR_LEN) != 0) {
		splx(s);
		return ESTALE;
	}
	mutex_enter(&sc->sc_sae_lock);
	if (!espwifi_if_sae_current(sc, sc->sc_sae_generation, bssid)) {
		mutex_exit(&sc->sc_sae_lock);
		splx(s);
		return ESTALE;
	}
	generation = sc->sc_sae_generation;
	if (sc->sc_sae_waiting != ESPWIFI_SAE_WAIT_NONE) {
		mutex_exit(&sc->sc_sae_lock);
		splx(s);
		return EBUSY;
	}
	if (type == ESPWIFI_SAE_COMMIT && !sc->sc_sae_start_sent) {
		if (ni->ni_esslen > sizeof(ssid)) {
			mutex_exit(&sc->sc_sae_lock);
			splx(s);
			return ESTALE;
		}
		ssid_len = ni->ni_esslen;
		memcpy(ssid, ni->ni_essid, ssid_len);
		sc->sc_sae_start_sent = true;
		send_start = true;
	}
	sc->sc_sae_waiting = ESPWIFI_SAE_WAIT_TX;
	mutex_exit(&sc->sc_sae_lock);
	splx(s);
	if (send_start) {
		error = espwifi_if_sae_notify(sc, IEEE80211_SAE_START,
		    generation, bssid, ssid, ssid_len);
		explicit_memset(ssid, 0, sizeof(ssid));
		if (error != 0) {
			espwifi_if_sae_cancel_generation(sc, generation);
			return error;
		}
	}
	error = espwifi_if_sae_wait(sc, generation, ESPWIFI_SAE_REPLY_TX,
	    type, body, capacity, len);
	if (error != 0)
		*len = 0;
	return error;
}

int
espwifi_if_sae_parse(const unsigned char *body, unsigned int len,
	    unsigned int type, unsigned int status)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	struct ieee80211req_sae *req;
	uint32_t generation;
	uint8_t bssid[ETHER_ADDR_LEN], local[ETHER_ADDR_LEN];
	unsigned int frame_len;
	int error, s;

	if (body == NULL || len == 0 || len > ESPWIFI_SAE_BODY_MAX ||
	    (type != ESPWIFI_SAE_COMMIT && type != ESPWIFI_SAE_CONFIRM))
		return EINVAL;
	s = splnet();
	mutex_enter(&sc->sc_sae_lock);
	if (!sc->sc_sae_enabled || sc->sc_ic.ic_bss == NULL ||
	    sc->sc_ic.ic_state != IEEE80211_S_AUTH ||
	    !espwifi_if_sae_current(sc, sc->sc_sae_generation,
	    sc->sc_sae_bssid)) {
		mutex_exit(&sc->sc_sae_lock);
		splx(s);
		return ESTALE;
	}
	generation = sc->sc_sae_generation;
	memcpy(bssid, sc->sc_sae_bssid, sizeof(bssid));
	memcpy(local, sc->sc_ic.ic_myaddr, sizeof(local));
	if (sc->sc_sae_waiting != ESPWIFI_SAE_WAIT_NONE) {
		mutex_exit(&sc->sc_sae_lock);
		splx(s);
		return EBUSY;
	}
	sc->sc_sae_waiting = type == ESPWIFI_SAE_CONFIRM ?
	    ESPWIFI_SAE_WAIT_STATUS : ESPWIFI_SAE_WAIT_RX;
	mutex_exit(&sc->sc_sae_lock);
	splx(s);

	frame_len = ESPWIFI_SAE_HEADER_LEN + len;
	req = kmem_zalloc(sizeof(*req), KM_NOSLEEP);
	if (req == NULL) {
		error = ENOMEM;
		goto fail;
	}
	req->version = IEEE80211_SAE_VERSION;
	req->generation = generation;
	req->op = IEEE80211_SAE_RX_FRAME;
	req->len = frame_len;
	memcpy(req->bssid, bssid, sizeof(req->bssid));
	req->data[0] = 0xb0;
	memcpy(req->data + 4, local, ETHER_ADDR_LEN);
	memcpy(req->data + 10, bssid, ETHER_ADDR_LEN);
	memcpy(req->data + 16, bssid, ETHER_ADDR_LEN);
	le16enc(req->data + 24, 3);
	le16enc(req->data + 26, type);
	if (status > 0xffffU) {
		explicit_memset(req, 0, sizeof(*req));
		kmem_free(req, sizeof(*req));
		error = EINVAL;
		goto fail;
	}
	le16enc(req->data + 28, (uint16_t)status);
	memcpy(req->data + ESPWIFI_SAE_HEADER_LEN, body, len);
	if (ieee80211_sae_auth_frame_validate(req->data, frame_len,
	    local, bssid, bssid, type) != 0) {
		explicit_memset(req, 0, sizeof(*req));
		kmem_free(req, sizeof(*req));
		error = EINVAL;
		goto fail;
	}
	rt_ieee80211msg(&sc->sc_ec.ec_if, RTM_IEEE80211_SAE, req,
	    offsetof(struct ieee80211req_sae, data) + frame_len);
	explicit_memset(req, 0, sizeof(*req));
	kmem_free(req, sizeof(*req));
	error = espwifi_if_sae_wait(sc, generation,
	    type == ESPWIFI_SAE_CONFIRM ? ESPWIFI_SAE_REPLY_STATUS :
	    ESPWIFI_SAE_REPLY_ANY, type, NULL, 0, NULL);
	return error;
	fail:
	mutex_enter(&sc->sc_sae_lock);
	if (sc->sc_sae_generation == generation)
		sc->sc_sae_waiting = ESPWIFI_SAE_WAIT_NONE;
	mutex_exit(&sc->sc_sae_lock);
	return error;
}
#endif /* ESPWIFI_NET80211_WPA2 */

static bool
espwifi_if_is_eapol(const uint8_t *data, unsigned len)
{
	return len >= ETHER_HDR_LEN && data[12] == (ETHERTYPE_PAE >> 8) &&
	    data[13] == (ETHERTYPE_PAE & 0xff);
}

/* The vendor callback can run outside sleepable thread context. */
void
espwifi_if_scan_frame(const void *data, unsigned len, unsigned channel, int rssi,
    unsigned timestamp)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	struct espwifi_scan_frame *frame;

	if (!sc->sc_attached || !sc->sc_scan_enabled ||
	    len < sizeof(struct ieee80211_frame) ||
	    len > ESPWIFI_SCAN_FRAME_MAX || channel == 0 || channel > 14)
		return;
	mutex_spin_enter(&sc->sc_scan_lock);
	if (sc->sc_scan_active == 0 || sc->sc_scan_count == ESPWIFI_SCAN_RING) {
		mutex_spin_exit(&sc->sc_scan_lock);
		return;
	}
	frame = &sc->sc_scan_ring[(sc->sc_scan_head + sc->sc_scan_count) %
	    ESPWIFI_SCAN_RING];
	frame->len = len;
	frame->channel = channel;
	frame->generation = sc->sc_scan_active;
	frame->rssi = rssi;
	frame->timestamp = timestamp;
	memcpy(frame->data, data, len);
	sc->sc_scan_count++;
	mutex_spin_exit(&sc->sc_scan_lock);
	espwifi_os_sem_give(sc->sc_scan_sem);
}

static void
espwifi_if_scanthread(void *v)
{
	struct espwifi_softc * const sc = v;
	struct espwifi_scan_frame frame;

	for (;;) {
		espwifi_os_sem_take(sc->sc_scan_sem, ESPWIFI_WAIT_FOREVER);
		for (;;) {
			bool current;

			mutex_spin_enter(&sc->sc_scan_lock);
			if (sc->sc_scan_shutdown) {
				mutex_spin_exit(&sc->sc_scan_lock);
				return;
			}
			if (sc->sc_scan_count == 0) {
				mutex_spin_exit(&sc->sc_scan_lock);
				break;
			}
			frame = sc->sc_scan_ring[sc->sc_scan_head];
			sc->sc_scan_head = (sc->sc_scan_head + 1) % ESPWIFI_SCAN_RING;
			sc->sc_scan_count--;
			sc->sc_scan_busy = true;
			mutex_spin_exit(&sc->sc_scan_lock);

			struct mbuf *m = m_devget(__UNCONST(frame.data), frame.len, 0,
			    &sc->sc_ec.ec_if);
			if (m == NULL)
				goto done;
			const int s = splnet();
			mutex_spin_enter(&sc->sc_scan_lock);
			current = frame.generation == sc->sc_scan_generation;
			mutex_spin_exit(&sc->sc_scan_lock);
			if (!current || (sc->sc_ic.ic_flags & IEEE80211_F_SCAN) == 0) {
				splx(s);
				m_freem(m);
				goto done;
			}
			sc->sc_ic.ic_curchan = &sc->sc_ic.ic_channels[frame.channel];
			struct ieee80211_node *ni = ieee80211_find_rxnode(&sc->sc_ic,
			    mtod(m, const struct ieee80211_frame_min *));
			/* The legacy scan quality byte is unsigned, not dBm. */
			int quality = MAX(1, MIN(100, frame.rssi + 100));
			ieee80211_input(&sc->sc_ic, m, ni, quality, frame.timestamp);
			ieee80211_free_node(ni);
			ni = ieee80211_find_node(&sc->sc_ic.ic_scan, frame.data + 10);
			if (ni != NULL) {
				ni->ni_rssi_dbm = frame.rssi;
				ni->ni_rssi_dbm_valid = true;
				ieee80211_free_node(ni);
			}
			splx(s);
done:
			mutex_spin_enter(&sc->sc_scan_lock);
			sc->sc_scan_busy = false;
			mutex_spin_exit(&sc->sc_scan_lock);
		}
	}
}

static int
espwifi_if_newstate(struct ieee80211com *ic, enum ieee80211_state state, int arg)
{
	struct espwifi_softc * const sc = ic->ic_ifp->if_softc;
	enum ieee80211_state oldstate = ic->ic_state;
	bool ready, was_link = false, radio_ready, queue_teardown = false;

	if (state == IEEE80211_S_AUTH) {
#ifndef ESPWIFI_NET80211_WPA2
		return EOPNOTSUPP;
#else
		struct ieee80211_node *ni = ic->ic_bss;
		const uint8_t *rsn = ic->ic_opt_ie;
		unsigned rsn_len = ic->ic_opt_ie_len;
		bool busy, started, stopped, sae;

		mutex_spin_enter(&sc->sc_scan_lock);
		busy = sc->sc_join_active != 0 || sc->sc_join_disconnect_pending ||
		    sc->sc_join_barrier_busy;
		stopped = sc->sc_join_stopped;
		ready = sc->sc_radio_ready;
		started = sc->sc_sta_started;
		sae = sc->sc_sae_selection;
		if (sae && (ni == NULL || sc->sc_sae_selection_generation == 0 ||
		    memcmp(sc->sc_sae_selection_bssid, ni->ni_bssid,
		    ETHER_ADDR_LEN) != 0)) {
			mutex_spin_exit(&sc->sc_scan_lock);
			return ESTALE;
		}
		mutex_spin_exit(&sc->sc_scan_lock);
		if (busy)
			return EBUSY;
		if (stopped)
			return EOPNOTSUPP;

		/* FullMAC owns 802.11 authentication and association frames. */
		if (!ready || !started || oldstate != IEEE80211_S_SCAN || ni == NULL ||
		    ni->ni_esslen == 0 || ni->ni_esslen > sizeof(sc->sc_join_ssid) ||
		    rsn == NULL || rsn_len == 0 || rsn_len > sizeof(sc->sc_join_rsn) ||
		    rsn[0] != IEEE80211_ELEMID_RSN ||
		    (ic->ic_flags & IEEE80211_F_WPA) == 0)
			return EOPNOTSUPP;
		if (sae && !ieee80211_sae_rsn_valid(rsn, rsn_len))
			return EOPNOTSUPP;
		mutex_spin_enter(&sc->sc_scan_lock);
		if (sc->sc_join_active != 0 || sc->sc_join_disconnect_pending ||
		    sc->sc_join_barrier_busy) {
			mutex_spin_exit(&sc->sc_scan_lock);
			return EBUSY;
		}
		if (!sc->sc_radio_ready || !sc->sc_sta_started ||
		    sc->sc_join_stopped) {
			mutex_spin_exit(&sc->sc_scan_lock);
			return EOPNOTSUPP;
		}
		if (sc->sc_sae_selection != sae || (sae &&
		    (sc->sc_sae_selection_generation == 0 ||
		    memcmp(sc->sc_sae_selection_bssid, ni->ni_bssid,
		    ETHER_ADDR_LEN) != 0))) {
			mutex_spin_exit(&sc->sc_scan_lock);
			return ESTALE;
		}
		if (++sc->sc_join_generation == 0)
			sc->sc_join_generation++;
		sc->sc_join_active = sc->sc_join_generation;
		sc->sc_join_pending = true;
		sc->sc_joined = false;
		sc->sc_join_sae = sae;
		sc->sc_ptk_installed = false;
		sc->sc_key_slots = 0;
		sc->sc_group_key_slots = 0;
		sc->sc_igtk_slots = 0;
		sc->sc_key_generation = 0;
		atomic_store_release(&sc->sc_port_authorized, 0);
		memcpy(sc->sc_join_bssid, ni->ni_bssid, ETHER_ADDR_LEN);
		memcpy(sc->sc_join_ssid, ni->ni_essid, ni->ni_esslen);
		sc->sc_join_ssid_len = ni->ni_esslen;
		memcpy(sc->sc_join_rsn, rsn, rsn_len);
		sc->sc_join_rsn_len = rsn_len;
		mutex_spin_exit(&sc->sc_scan_lock);
		espwifi_os_sem_give(sc->sc_scan_ctl_sem);
		return 0;
#endif
	}
	if (state == IEEE80211_S_ASSOC || state == IEEE80211_S_RUN)
		return EOPNOTSUPP; /* Only a matching vendor association event publishes RUN. */
	if (state != IEEE80211_S_INIT && state != IEEE80211_S_SCAN)
		return EOPNOTSUPP;
	if (state == IEEE80211_S_SCAN && !sc->sc_scan_enabled)
		return EOPNOTSUPP;
	if (state == IEEE80211_S_INIT) {
		bool teardown;

		mutex_spin_enter(&sc->sc_scan_lock);
		teardown = sc->sc_join_active != 0 || sc->sc_joined || sc->sc_link;
		was_link = sc->sc_link;
		sc->sc_link = false;
		if (++sc->sc_join_generation == 0)
			sc->sc_join_generation++;
		sc->sc_join_active = 0;
		sc->sc_join_pending = false;
		if ((sc->sc_radio_ready && teardown) || sc->sc_join_barrier_busy)
			sc->sc_join_disconnect_pending = true;
		if (sc->sc_join_barrier_busy)
			sc->sc_join_rescan_pending = true;
		queue_teardown = sc->sc_join_disconnect_pending;
		sc->sc_joined = false;
		sc->sc_join_sae = false;
		sc->sc_ptk_installed = false;
		sc->sc_key_slots = 0;
		sc->sc_group_key_slots = 0;
		sc->sc_igtk_slots = 0;
		sc->sc_key_generation = 0;
		atomic_store_release(&sc->sc_port_authorized, 0);
		mutex_spin_exit(&sc->sc_scan_lock);
#ifdef ESPWIFI_NET80211_WPA2
		espwifi_if_sae_cancel(sc);
#endif
		if (was_link && sc->sc_attached)
			if_link_state_change(&sc->sc_ec.ec_if, LINK_STATE_DOWN);
		if (oldstate == IEEE80211_S_RUN && ic->ic_bss != NULL) {
			/* FullMAC has no software management-frame transmitter. */
			ieee80211_sta_leave(ic, ic->ic_bss);
			ic->ic_state = IEEE80211_S_SCAN;
		}
	}
	if (state == IEEE80211_S_SCAN && oldstate == IEEE80211_S_SCAN) {
		/* Firmware performs channel changes and probes during a scan. */
		if (ic->ic_flags & IEEE80211_F_SCAN)
			return 0;
		/* Manual roaming leaves the state at SCAN after completion. */
		ieee80211_begin_scan(ic, 1);
	} else {
		int error = sc->sc_newstate(ic, state, arg);

		if (error)
			return error;
	}
	mutex_spin_enter(&sc->sc_scan_lock);
	if (++sc->sc_scan_generation == 0)
		sc->sc_scan_generation++;
	sc->sc_scan_pending = (state == IEEE80211_S_SCAN);
	ready = sc->sc_scan_pending && sc->sc_radio_ready && sc->sc_scan_enabled;
	radio_ready = sc->sc_radio_ready;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (ready)
		espwifi_os_sem_give(sc->sc_scan_ctl_sem);
	if (queue_teardown || (state == IEEE80211_S_INIT && radio_ready))
		espwifi_os_sem_give(sc->sc_scan_ctl_sem);
	return 0;
}

#ifdef ESPWIFI_NET80211_WPA2
static int
espwifi_if_set_port(struct ieee80211com *ic, bool authorized)
{
	struct espwifi_softc * const sc = ic->ic_ifp->if_softc;
	bool ready;

	mutex_spin_enter(&sc->sc_scan_lock);
	ready = sc->sc_link && sc->sc_join_active != 0 && sc->sc_joined &&
	    sc->sc_radio_ready && sc->sc_sta_started && !sc->sc_join_stopped &&
	    sc->sc_ptk_installed &&
	    sc->sc_key_generation == sc->sc_join_active &&
	    (!sc->sc_join_sae || sc->sc_igtk_slots != 0);
	if (authorized && !ready) {
		mutex_spin_exit(&sc->sc_scan_lock);
		return EACCES;
	}
	atomic_store_release(&sc->sc_port_authorized, authorized);
	mutex_spin_exit(&sc->sc_scan_lock);
	return 0;
}

static bool
espwifi_if_fail_closed_generation(struct espwifi_softc *sc,
	uint64_t generation)
{
	bool disconnect, was_link;

	mutex_spin_enter(&sc->sc_scan_lock);
	if (generation != 0 && sc->sc_join_active != generation) {
		mutex_spin_exit(&sc->sc_scan_lock);
		return false;
	}
	atomic_store_release(&sc->sc_port_authorized, 0);
	was_link = sc->sc_link;
	disconnect = sc->sc_radio_ready && (sc->sc_link ||
	    sc->sc_join_active != 0 || sc->sc_joined);
	sc->sc_link = false;
	sc->sc_ptk_installed = false;
	sc->sc_key_slots = 0;
	sc->sc_group_key_slots = 0;
	sc->sc_igtk_slots = 0;
	sc->sc_key_generation = 0;
	if (++sc->sc_join_generation == 0)
		sc->sc_join_generation++;
	sc->sc_join_active = 0;
	sc->sc_join_pending = false;
	sc->sc_joined = false;
	sc->sc_join_sae = false;
	if (disconnect) {
		sc->sc_join_disconnect_pending = true;
		sc->sc_join_rescan_pending = true;
	}
	mutex_spin_exit(&sc->sc_scan_lock);
#ifdef ESPWIFI_NET80211_WPA2
	espwifi_if_sae_cancel(sc);
#endif
	if (was_link && sc->sc_attached)
		if_link_state_change(&sc->sc_ec.ec_if, LINK_STATE_DOWN);
	if (disconnect)
		espwifi_os_sem_give(sc->sc_scan_ctl_sem);
	return true;
}

static void
espwifi_if_fail_closed(struct espwifi_softc *sc)
{
	(void)espwifi_if_fail_closed_generation(sc, 0);
}

static void
espwifi_if_key_request_clear(struct espwifi_softc *sc)
{
	explicit_memset(sc->sc_key_request_data, 0,
	    sizeof(sc->sc_key_request_data));
	explicit_memset(sc->sc_key_request_seq, 0,
	    sizeof(sc->sc_key_request_seq));
	explicit_memset(sc->sc_key_request_mac, 0,
	    sizeof(sc->sc_key_request_mac));
}

static bool
espwifi_if_key_wait(struct espwifi_softc *sc, u_int *left, u_int *start)
{
	u_int elapsed, now;

	if (*left == 0 || cv_timedwait(&sc->sc_key_cv, &sc->sc_key_lock,
	    (int)*left) == EWOULDBLOCK)
		return false;
	now = (u_int)getticks();
	elapsed = now - *start;
	*start = now;
	if (elapsed >= *left) {
		*left = 0;
		return false;
	}
	*left -= elapsed;
	return true;
}

static void
espwifi_if_key_control(struct espwifi_softc *sc)
{
	uint8_t mac[ETHER_ADDR_LEN], seq[6], key[16];
	uint64_t generation;
	bool current;
	unsigned index, type;
	int group, xmit, error;

	mutex_enter(&sc->sc_key_lock);
	if (!sc->sc_key_pending) {
		mutex_exit(&sc->sc_key_lock);
		return;
	}
	memcpy(mac, sc->sc_key_request_mac, sizeof(mac));
	memcpy(seq, sc->sc_key_request_seq, sizeof(seq));
	memcpy(key, sc->sc_key_request_data, sizeof(key));
	index = sc->sc_key_request_index;
	type = sc->sc_key_request_type;
	group = sc->sc_key_request_group;
	xmit = sc->sc_key_request_xmit;
	generation = sc->sc_key_request_generation;
	sc->sc_key_pending = false;
	sc->sc_key_running = true;
	mutex_exit(&sc->sc_key_lock);

	mutex_spin_enter(&sc->sc_scan_lock);
	current = sc->sc_join_active == generation && sc->sc_joined &&
	    sc->sc_radio_ready && sc->sc_sta_started && !sc->sc_join_stopped;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (!current)
		error = ECANCELED;
	else if (type == ESPWIFI_KEY_REQUEST_IGTK)
		error = espwifi_ext_set_igtk(index, seq, key);
	else
		error = espwifi_ext_set_key(mac, index, group, xmit, 0, seq,
		    sizeof(seq), key, sizeof(key));
	explicit_memset(mac, 0, sizeof(mac));
	explicit_memset(seq, 0, sizeof(seq));
	explicit_memset(key, 0, sizeof(key));

	mutex_enter(&sc->sc_key_lock);
	sc->sc_key_running = false;
	if (sc->sc_key_abandoned) {
		espwifi_if_key_request_clear(sc);
		sc->sc_key_abandoned = false;
		sc->sc_key_done = false;
	} else {
		sc->sc_key_error = error;
		sc->sc_key_done = true;
	}
	cv_broadcast(&sc->sc_key_cv);
	mutex_exit(&sc->sc_key_lock);
}

static bool
espwifi_if_key_request(struct espwifi_softc *sc, uint64_t generation,
	    unsigned type, const uint8_t mac[ETHER_ADDR_LEN], unsigned index,
	    int group, int xmit,
	    const uint8_t sequence[6], const uint8_t key[16])
{
	bool current;
	int error;
	u_int left, start;

	if (!sc->sc_scan_enabled) {
		(void)espwifi_if_fail_closed_generation(sc, generation);
		return false;
	}
	mutex_enter(&sc->sc_key_lock);
	left = (u_int)MAX(1, mstohz(30000));
	start = (u_int)getticks();
	while (sc->sc_key_pending || sc->sc_key_running || sc->sc_key_done) {
		if (!espwifi_if_key_wait(sc, &left, &start)) {
			mutex_exit(&sc->sc_key_lock);
			(void)espwifi_if_fail_closed_generation(sc, generation);
			return false;
		}
	}
	/* A request from an old association may not enter the radio worker. */
	mutex_spin_enter(&sc->sc_scan_lock);
	current = generation != 0 && sc->sc_join_active == generation &&
	    sc->sc_joined && sc->sc_radio_ready && sc->sc_sta_started &&
	    !sc->sc_join_stopped;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (!current) {
		mutex_exit(&sc->sc_key_lock);
		return false;
	}
	if (mac == NULL)
		memset(sc->sc_key_request_mac, 0, sizeof(sc->sc_key_request_mac));
	else
		memcpy(sc->sc_key_request_mac, mac, sizeof(sc->sc_key_request_mac));
	memcpy(sc->sc_key_request_seq, sequence, sizeof(sc->sc_key_request_seq));
	memcpy(sc->sc_key_request_data, key, sizeof(sc->sc_key_request_data));
	sc->sc_key_request_index = index;
	sc->sc_key_request_type = type;
	sc->sc_key_request_group = group;
	sc->sc_key_request_xmit = xmit;
	sc->sc_key_request_generation = generation;
	sc->sc_key_pending = true;
	(void)espwifi_os_sem_give(sc->sc_scan_ctl_sem);
	left = (u_int)MAX(1, mstohz(30000));
	start = (u_int)getticks();
	while (!sc->sc_key_done) {
		if (!espwifi_if_key_wait(sc, &left, &start)) {
			if (sc->sc_key_done)
				break;
			if (sc->sc_key_pending) {
				sc->sc_key_pending = false;
				espwifi_if_key_request_clear(sc);
			} else if (sc->sc_key_running)
				sc->sc_key_abandoned = true;
			cv_broadcast(&sc->sc_key_cv);
			mutex_exit(&sc->sc_key_lock);
			(void)espwifi_if_fail_closed_generation(sc, generation);
			return false;
		}
	}
	error = sc->sc_key_error;
	sc->sc_key_done = false;
	espwifi_if_key_request_clear(sc);
	cv_broadcast(&sc->sc_key_cv);
	mutex_exit(&sc->sc_key_lock);
	mutex_spin_enter(&sc->sc_scan_lock);
	current = sc->sc_join_active == generation && sc->sc_joined;
	if (error == 0 && current) {
		if (sc->sc_key_generation != generation) {
			sc->sc_key_slots = 0;
			sc->sc_group_key_slots = 0;
			sc->sc_igtk_slots = 0;
			sc->sc_key_generation = generation;
		}
		if (type == ESPWIFI_KEY_REQUEST_IGTK)
			sc->sc_igtk_slots |= (uint8_t)(1U << index);
	}
	mutex_spin_exit(&sc->sc_scan_lock);
	if (error != 0 && current)
		(void)espwifi_if_fail_closed_generation(sc, generation);
	return error == 0 && current;
}

static int
espwifi_if_key_set(struct ieee80211com *ic, const struct ieee80211_key *key,
    const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	struct espwifi_softc * const sc = ic->ic_ifp->if_softc;
	uint64_t generation;
	bool current;

	if (key->wk_cipher == NULL || key->wk_cipher->ic_cipher !=
	    IEEE80211_CIPHER_AES_CCM || key->wk_keylen != 16 ||
	    key->wk_keyix >= IEEE80211_WEP_NKID) {
		espwifi_if_fail_closed(sc);
		return 0;
	}
	mutex_spin_enter(&sc->sc_scan_lock);
	generation = sc->sc_join_active;
	current = generation != 0 && sc->sc_joined;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (!current) {
		espwifi_if_fail_closed(sc);
		return 0;
	}
	/*
	 * cs_key_set is called from the process-context ioctl path.  Run the
	 * ILP32F vendor routine on the radio control kthread, which owns FPU/PCU
	 * state; the bounded wait is permitted by the sleep queue at IPL_NET.
	 */
	uint8_t seq[6] = {0};
	for (unsigned i = 0; i < sizeof(sc->sc_key_request_seq); i++)
		seq[i] = (uint8_t)(key->wk_keyrsc >> (i * NBBY));
	current = espwifi_if_key_request(sc, generation,
	    ESPWIFI_KEY_REQUEST_CCMP, mac, key->wk_keyix,
	    (key->wk_flags & IEEE80211_KEY_GROUP) != 0,
	    (key->wk_flags & IEEE80211_KEY_XMIT) != 0, seq, key->wk_key);
	explicit_memset(seq, 0, sizeof(seq));
	mutex_spin_enter(&sc->sc_scan_lock);
	bool still_current = sc->sc_join_active == generation && sc->sc_joined;
	if (current && still_current) {
		if (sc->sc_key_generation != generation) {
			sc->sc_key_slots = 0;
			sc->sc_group_key_slots = 0;
			sc->sc_igtk_slots = 0;
			sc->sc_key_generation = generation;
		}
		if ((key->wk_flags & IEEE80211_KEY_GROUP) != 0)
			sc->sc_group_key_slots |= (uint8_t)(1U << key->wk_keyix);
		else {
			sc->sc_key_slots |= (uint8_t)(1U << key->wk_keyix);
			sc->sc_ptk_installed = true;
		}
	}
	mutex_spin_exit(&sc->sc_scan_lock);
	return current && still_current;
}

static int
espwifi_if_igtk_install(struct espwifi_softc *sc,
	    const struct ieee80211req_sae *req, uint64_t join_generation)
{
	unsigned index;
	bool current;

	if (req->len != 24)
		return EINVAL;
	index = le16dec(req->data);
	if (index != 4 && index != 5)
		return EINVAL;
	mutex_spin_enter(&sc->sc_scan_lock);
	current = join_generation != 0 && sc->sc_join_active == join_generation &&
	    sc->sc_joined && sc->sc_link &&
	    sc->sc_radio_ready && sc->sc_sta_started && !sc->sc_join_stopped &&
	    sc->sc_join_sae &&
	    memcmp(req->bssid, sc->sc_join_bssid, ETHER_ADDR_LEN) == 0;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (!current)
		return ESTALE;
	/* Route the vendor key operation through the FPU-owning radio thread. */
	return espwifi_if_key_request(sc, join_generation, ESPWIFI_KEY_REQUEST_IGTK,
	    NULL, index, 0, 0, req->data + 2, req->data + 8) ? 0 : EIO;
}

static int
espwifi_if_igtk_delete(struct espwifi_softc *sc,
	    const struct ieee80211req_sae *req, uint64_t join_generation)
{
	unsigned index;
	bool current, installed;

	if (req->len != 2)
		return EINVAL;
	index = le16dec(req->data);
	if (index != 4 && index != 5)
		return EINVAL;
	mutex_spin_enter(&sc->sc_scan_lock);
	current = join_generation != 0 && sc->sc_join_active == join_generation &&
	    sc->sc_joined && sc->sc_join_sae &&
	    memcmp(req->bssid, sc->sc_join_bssid, ETHER_ADDR_LEN) == 0;
	installed = current && sc->sc_key_generation == join_generation &&
	    (sc->sc_igtk_slots & (uint8_t)(1U << index)) != 0;
	if (installed) {
		sc->sc_igtk_delete_generation = req->generation;
		memcpy(sc->sc_igtk_delete_bssid, req->bssid, ETHER_ADDR_LEN);
	}
	mutex_spin_exit(&sc->sc_scan_lock);
	if (!current)
		return ESTALE;
	if (!installed)
		return 0;
	/* No per-slot delete API exists; the common teardown resets the STA context. */
	return espwifi_if_fail_closed_generation(sc, join_generation) ? 0 : ESTALE;
}

static int
espwifi_if_key_delete(struct ieee80211com *ic, const struct ieee80211_key *key)
{
	struct espwifi_softc * const sc = ic->ic_ifp->if_softc;
	bool installed;

	if (key->wk_keyix == IEEE80211_KEYIX_NONE)
		return 1;
	if (key->wk_keyix >= IEEE80211_WEP_NKID) {
		espwifi_if_fail_closed(sc);
		return 0;
	}
	mutex_spin_enter(&sc->sc_scan_lock);
	installed = sc->sc_key_generation == sc->sc_join_active &&
	    (((key->wk_flags & IEEE80211_KEY_GROUP) != 0 ?
	    sc->sc_group_key_slots : sc->sc_key_slots) &
	    (1U << key->wk_keyix)) != 0;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (!installed)
		return 1;
	/* IDF 6.1 exposes set_sta_key_internal but no documented delete contract. */
	espwifi_if_fail_closed(sc);
	return 0;
}
#endif /* ESPWIFI_NET80211_WPA2 */

static void
espwifi_if_join_control(struct espwifi_softc *sc)
{
	uint8_t ssid[IEEE80211_NWID_LEN], bssid[ETHER_ADDR_LEN];
	uint8_t rsn[IEEE80211_MAX_OPT_IE];
	unsigned ssid_len = 0, rsn_len = 0;
	uint64_t generation = 0;
	bool disconnect, join, sae = false, rescan = false, current;
	int error;

	mutex_spin_enter(&sc->sc_scan_lock);
	disconnect = sc->sc_join_disconnect_pending;
	join = !disconnect && sc->sc_join_pending;
	if (disconnect) {
		sc->sc_join_disconnect_pending = false;
		sc->sc_join_barrier_busy = true;
		sc->sc_join_stopped = true;
		sc->sc_sta_started = false;
		sc->sc_radio_ready = false;
		rescan = sc->sc_join_rescan_pending;
		sc->sc_join_rescan_pending = false;
	} else if (join) {
		generation = sc->sc_join_active;
		memcpy(bssid, sc->sc_join_bssid, sizeof(bssid));
		memcpy(ssid, sc->sc_join_ssid, sc->sc_join_ssid_len);
		ssid_len = sc->sc_join_ssid_len;
		memcpy(rsn, sc->sc_join_rsn, sc->sc_join_rsn_len);
		rsn_len = sc->sc_join_rsn_len;
		sae = sc->sc_join_sae;
		sc->sc_join_pending = false;
	}
	mutex_spin_exit(&sc->sc_scan_lock);
	if (!disconnect && !join)
		return;

	if (disconnect) {
		error = espwifi_disconnect();
		mutex_spin_enter(&sc->sc_scan_lock);
		if (error == 0)
			sc->sc_join_barrier_busy = false;
		else
			sc->sc_radio_ready = false;
		mutex_spin_exit(&sc->sc_scan_lock);
		if (error != 0) {
			printf("espwifi0: radio teardown barrier failed\n");
			return;
		}
#ifdef ESPWIFI_NET80211_WPA2
		espwifi_if_sae_teardown_complete(sc);
#endif
		if (rescan && (sc->sc_ec.ec_if.if_flags & IFF_RUNNING) != 0) {
			int s = splnet();
			(void)ieee80211_new_state(&sc->sc_ic,
			    IEEE80211_S_SCAN, -1);
			splx(s);
		}
		return;
	}

	error = espwifi_ext_connect(ssid, ssid_len, bssid, rsn, rsn_len, sae);
	explicit_memset(rsn, 0, sizeof(rsn));
	explicit_memset(ssid, 0, sizeof(ssid));
	mutex_spin_enter(&sc->sc_scan_lock);
	current = generation != 0 && sc->sc_join_active == generation;
	if (error != 0 && current) {
		if (++sc->sc_join_generation == 0)
			sc->sc_join_generation++;
		sc->sc_join_active = 0;
		sc->sc_joined = false;
		sc->sc_join_sae = false;
		sc->sc_join_disconnect_pending = true;
		sc->sc_join_rescan_pending = true;
	}
	mutex_spin_exit(&sc->sc_scan_lock);
	if (error != 0 && current) {
		printf("espwifi0: WPA2 association request failed\n");
		espwifi_os_sem_give(sc->sc_scan_ctl_sem);
	}
}

void
espwifi_if_radio_started(void)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	bool pending = false;

	if (!sc->sc_attached)
		return;
	mutex_spin_enter(&sc->sc_scan_lock);
	sc->sc_join_stopped = false;
	sc->sc_sta_started = true;
	sc->sc_radio_ready = sc->sc_attached;
	pending = sc->sc_scan_pending && sc->sc_scan_enabled &&
	    sc->sc_radio_ready;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (pending)
		espwifi_os_sem_give(sc->sc_scan_ctl_sem);
}

void
espwifi_if_radio_stopped(void)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	bool was_link;

	if (!sc->sc_attached)
		return;
	mutex_spin_enter(&sc->sc_scan_lock);
	atomic_store_release(&sc->sc_port_authorized, 0);
	was_link = sc->sc_link;
	sc->sc_link = false;
	sc->sc_radio_ready = false;
	sc->sc_sta_started = false;
	sc->sc_join_stopped = true;
	if (++sc->sc_join_generation == 0)
		sc->sc_join_generation++;
	sc->sc_join_active = 0;
	sc->sc_join_pending = false;
	sc->sc_joined = false;
	sc->sc_join_sae = false;
	sc->sc_join_disconnect_pending = false;
	sc->sc_join_rescan_pending = false;
	sc->sc_ptk_installed = false;
	sc->sc_key_slots = 0;
	sc->sc_group_key_slots = 0;
	sc->sc_igtk_slots = 0;
	sc->sc_key_generation = 0;
	mutex_spin_exit(&sc->sc_scan_lock);
#ifdef ESPWIFI_NET80211_WPA2
	espwifi_if_sae_cancel(sc);
#endif
	/* Wake the control worker to reject any queued key from the stopped epoch. */
	espwifi_os_sem_give(sc->sc_scan_ctl_sem);
	if (was_link && sc->sc_attached)
		if_link_state_change(&sc->sc_ec.ec_if, LINK_STATE_DOWN);
}

void
espwifi_if_radio_ready(void)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	bool pending;

	if (!sc->sc_attached)
		return;
	mutex_spin_enter(&sc->sc_scan_lock);
	sc->sc_radio_ready = sc->sc_sta_started && !sc->sc_join_stopped;
	pending = sc->sc_scan_pending && sc->sc_scan_enabled &&
	    sc->sc_radio_ready;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (pending)
		espwifi_os_sem_give(sc->sc_scan_ctl_sem);
}

static void
espwifi_if_scan_control(void *v)
{
	struct espwifi_softc * const sc = v;

	for (;;) {
		bool run, failed, current, rescan = false;
		int error, s;
		uint64_t generation;

		espwifi_os_sem_take(sc->sc_scan_ctl_sem, ESPWIFI_WAIT_FOREVER);
		/* Drain coalesced control notifications before consuming scan work. */
		for (;;) {
			bool control_pending;

			mutex_spin_enter(&sc->sc_scan_lock);
			control_pending = sc->sc_join_disconnect_pending ||
			    sc->sc_join_pending;
			mutex_spin_exit(&sc->sc_scan_lock);
			if (control_pending) {
				espwifi_if_join_control(sc);
				continue;
			}
#ifdef ESPWIFI_NET80211_WPA2
			mutex_enter(&sc->sc_key_lock);
			bool key_pending = sc->sc_key_pending;
			mutex_exit(&sc->sc_key_lock);
			if (key_pending) {
				espwifi_if_key_control(sc);
				continue;
			}
#endif
			break;
		}
		mutex_spin_enter(&sc->sc_scan_lock);
		run = sc->sc_scan_pending && sc->sc_radio_ready;
		generation = sc->sc_scan_generation;
		sc->sc_scan_active = run ? generation : 0;
		sc->sc_scan_pending = false;
		mutex_spin_exit(&sc->sc_scan_lock);
		if (!run)
			continue;
		error = espwifi_scan_capture(1);
		failed = error != 0;
		if (!error) {
			error = espwifi_scan();
			int stop_error = espwifi_scan_capture(0);
			failed = error < 0 || stop_error != 0;
		} else
			(void)espwifi_scan_capture(0);
		mutex_spin_enter(&sc->sc_scan_lock);
		sc->sc_scan_active = 0;
		mutex_spin_exit(&sc->sc_scan_lock);
		if (failed)
			printf("espwifi0: scan failed\n");
		for (;;) {
			bool drained;

			mutex_spin_enter(&sc->sc_scan_lock);
			drained = sc->sc_scan_count == 0 && !sc->sc_scan_busy;
			mutex_spin_exit(&sc->sc_scan_lock);
			if (drained)
				break;
			espwifi_os_task_delay(1);
		}
		s = splnet();
		mutex_spin_enter(&sc->sc_scan_lock);
		current = generation == sc->sc_scan_generation;
		mutex_spin_exit(&sc->sc_scan_lock);
		if (current && (sc->sc_ic.ic_flags & IEEE80211_F_SCAN)) {
			if (failed) {
				ieee80211_cancel_scan(&sc->sc_ic);
			} else {
				ieee80211_end_scan(&sc->sc_ic);
				if (sc->sc_ic.ic_flags & IEEE80211_F_SCAN) {
					mutex_spin_enter(&sc->sc_scan_lock);
					if (generation == sc->sc_scan_generation) {
						sc->sc_scan_pending = true;
						rescan = true;
					}
					mutex_spin_exit(&sc->sc_scan_lock);
				}
			}
		}
		splx(s);
		if (rescan)
			espwifi_os_sem_give(sc->sc_scan_ctl_sem);
	}
}

#else
void
espwifi_if_scan_frame(const void *data, unsigned len, unsigned channel, int rssi,
    unsigned timestamp)
{
	(void)data;
	(void)len;
	(void)channel;
	(void)rssi;
	(void)timestamp;
}
void
espwifi_if_radio_ready(void)
{
}
void
espwifi_if_radio_started(void)
{
}
void
espwifi_if_radio_stopped(void)
{
}
#endif

static void
espwifi_if_start(struct ifnet *ifp)
{
	struct espwifi_softc * const sc = ifp->if_softc;

	espwifi_os_sem_give(sc->sc_txsem);
}

static void
espwifi_if_txthread(void *v)
{
	struct espwifi_softc * const sc = v;
	struct ifnet * const ifp = &sc->sc_ec.ec_if;

	for (;;) {
		espwifi_os_sem_take(sc->sc_txsem, ESPWIFI_WAIT_FOREVER);
		for (;;) {
			struct mbuf *m;
			const int s = splnet();

			IFQ_DEQUEUE(&ifp->if_snd, m);
			splx(s);
			if (m == NULL)
				break;

			const u_int len = m->m_pkthdr.len;
			if (len > ESPWIFI_MTU_FRAME || !sc->sc_link) {
				if_statinc(ifp, if_oerrors);
				m_freem(m);
				continue;
			}
#ifdef ESPWIFI_NET80211
			if (!atomic_load_acquire(&sc->sc_port_authorized)) {
				uint8_t header[ETHER_HDR_LEN];

				if (len < sizeof(header)) {
					m_freem(m);
					continue;
				}
				m_copydata(m, 0, sizeof(header), header);
				if (!espwifi_if_is_eapol(header, sizeof(header))) {
					m_freem(m);
					continue;
				}
			}
#endif
			m_copydata(m, 0, len, sc->sc_txbuf);
			bpf_mtap(ifp, m, BPF_D_OUT);
			m_freem(m);
			if (espwifi_tx(sc->sc_txbuf, len) == 0)
				if_statinc(ifp, if_opackets);
			else
				if_statinc(ifp, if_oerrors);
		}
	}
}

void
espwifi_if_input(const void *buf, unsigned int len)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	struct ifnet * const ifp = &sc->sc_ec.ec_if;

	if (!sc->sc_attached || (ifp->if_flags & IFF_RUNNING) == 0)
		return;
#ifdef ESPWIFI_NET80211
	if (!atomic_load_acquire(&sc->sc_port_authorized) &&
	    !espwifi_if_is_eapol(buf, len))
		return;
#endif

	struct mbuf * const m = m_devget(__UNCONST(buf), len, 0, ifp);
	if (m == NULL) {
		if_statinc(ifp, if_ierrors);
		return;
	}
	const int s = splnet();
	if_percpuq_enqueue(sc->sc_ipq, m);
	splx(s);
}

/*
 * Convert a vendor EAPOL-Key payload into an Ethernet frame for BPF.
 * Match the vendor callback contract: 1 consumed, 0 not a key frame,
 * -1 on malformed input or failure to enqueue.
 */
int
espwifi_if_rx_eapol(const unsigned char *source, const void *buf, unsigned len)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	struct ifnet * const ifp = &sc->sc_ec.ec_if;
	uint8_t frame[ETHER_HDR_LEN + ETHERMTU];
	struct mbuf *m;
	const uint8_t *eapol = buf;
	int s;
#ifdef ESPWIFI_NET80211_WPA2
	bool associated;
#endif

	if (!sc->sc_attached || (ifp->if_flags & IFF_RUNNING) == 0 ||
	    source == NULL || buf == NULL || len < 4 || len > ETHERMTU)
		return -1;
#ifdef ESPWIFI_NET80211_WPA2
	/* Consume but never pass pre-association EAPOL to another owner. */
	mutex_spin_enter(&sc->sc_scan_lock);
	associated = sc->sc_join_active != 0 && sc->sc_joined &&
	    !sc->sc_join_stopped;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (!associated)
		return 1;
#endif
	if (eapol[1] != 3) /* This profile accepts EAPOL-Key only. */
		return 1;
	if (((unsigned)eapol[2] << 8 | eapol[3]) > len - 4) {
#ifdef ESPWIFI_NET80211_WPA2
		espwifi_if_fail_closed(sc);
#endif
		return -1;
	}
	memcpy(frame, sc->sc_mac, ETHER_ADDR_LEN);
	memcpy(frame + ETHER_ADDR_LEN, source, ETHER_ADDR_LEN);
	frame[12] = ETHERTYPE_PAE >> 8;
	frame[13] = ETHERTYPE_PAE & 0xff;
	memcpy(frame + ETHER_HDR_LEN, buf, len);
	m = m_devget(__UNCONST(frame), ETHER_HDR_LEN + len, 0, ifp);
	explicit_memset(frame, 0, sizeof(frame));
	if (m == NULL) {
#ifdef ESPWIFI_NET80211_WPA2
		espwifi_if_fail_closed(sc);
#endif
		return -1;
	}
	s = splnet();
	if_percpuq_enqueue(sc->sc_ipq, m);
	splx(s);
	return 1;
}

static void
espwifi_if_link_event(int up, const unsigned char *bssid, bool association)
{
	struct espwifi_softc * const sc = &espwifi_sc;
#ifdef ESPWIFI_NET80211
	bool accepted = false, publish_run = false, queue_barrier = false;
	bool link_state;
	uint64_t generation = 0;

	if (!sc->sc_attached)
		return;
	if (up && bssid != NULL) {
		mutex_spin_enter(&sc->sc_scan_lock);
		accepted = sc->sc_join_active != 0 && sc->sc_joined &&
		    memcmp(sc->sc_join_bssid, bssid, ETHER_ADDR_LEN) == 0;
		if (accepted) {
			generation = sc->sc_join_active;
			publish_run = association;
		}
		mutex_spin_exit(&sc->sc_scan_lock);
		if (!accepted)
			return;
	} else if (!up && bssid != NULL) {
		mutex_spin_enter(&sc->sc_scan_lock);
		accepted = (sc->sc_join_active != 0 || sc->sc_joined) &&
		    memcmp(sc->sc_join_bssid, bssid, ETHER_ADDR_LEN) == 0;
		if (accepted) {
			if (++sc->sc_join_generation == 0)
				sc->sc_join_generation++;
			sc->sc_join_active = 0;
			sc->sc_joined = false;
			sc->sc_join_sae = false;
			sc->sc_join_pending = false;
			sc->sc_join_disconnect_pending = true;
			sc->sc_join_rescan_pending = true;
			queue_barrier = true;
		}
		mutex_spin_exit(&sc->sc_scan_lock);
		if (!accepted)
			return;
	} else if (!up) {
		mutex_spin_enter(&sc->sc_scan_lock);
		accepted = sc->sc_join_active != 0 || sc->sc_joined || sc->sc_link;
		if (accepted) {
			if (++sc->sc_join_generation == 0)
				sc->sc_join_generation++;
			sc->sc_join_active = 0;
			sc->sc_join_pending = false;
			sc->sc_joined = false;
			sc->sc_join_sae = false;
			sc->sc_join_disconnect_pending = sc->sc_radio_ready;
			sc->sc_join_rescan_pending = sc->sc_radio_ready;
			queue_barrier = sc->sc_join_disconnect_pending;
		}
		mutex_spin_exit(&sc->sc_scan_lock);
	}
	if (!up) {
		espwifi_if_sae_cancel(sc);
		mutex_spin_enter(&sc->sc_scan_lock);
		atomic_store_release(&sc->sc_port_authorized, 0);
		sc->sc_ptk_installed = false;
		sc->sc_key_slots = 0;
		sc->sc_group_key_slots = 0;
				sc->sc_igtk_slots = 0;
		sc->sc_key_generation = 0;
		mutex_spin_exit(&sc->sc_scan_lock);
	}
	if (publish_run) {
		int s = splnet();
		bool current;

		mutex_spin_enter(&sc->sc_scan_lock);
		current = generation != 0 && sc->sc_join_active == generation &&
		    sc->sc_joined && !sc->sc_join_stopped;
		mutex_spin_exit(&sc->sc_scan_lock);
		if (!current || sc->sc_ic.ic_bss == NULL ||
		    memcmp(sc->sc_ic.ic_bss->ni_bssid, bssid, ETHER_ADDR_LEN) != 0) {
			accepted = false;
		} else if (sc->sc_ic.ic_state == IEEE80211_S_SCAN) {
			if (sc->sc_newstate(&sc->sc_ic, IEEE80211_S_RUN,
			    IEEE80211_FC0_SUBTYPE_ASSOC_RESP) != 0) {
				mutex_spin_enter(&sc->sc_scan_lock);
				atomic_store_release(&sc->sc_port_authorized, 0);
				if (++sc->sc_join_generation == 0)
					sc->sc_join_generation++;
				sc->sc_join_active = 0;
				sc->sc_joined = false;
				sc->sc_join_sae = false;
				sc->sc_join_disconnect_pending = true;
				sc->sc_join_rescan_pending = true;
				sc->sc_ptk_installed = false;
				sc->sc_key_slots = 0;
				sc->sc_group_key_slots = 0;
		sc->sc_igtk_slots = 0;
				sc->sc_key_generation = 0;
				mutex_spin_exit(&sc->sc_scan_lock);
				queue_barrier = true;
				accepted = false;
			}
		} else if (sc->sc_ic.ic_state != IEEE80211_S_RUN)
			accepted = false;
		splx(s);
	}
	if (queue_barrier)
		espwifi_os_sem_give(sc->sc_scan_ctl_sem);
	mutex_spin_enter(&sc->sc_scan_lock);
	if (!up)
		sc->sc_link = false;
	else {
		accepted = accepted && generation != 0 &&
		    sc->sc_join_active == generation && sc->sc_joined &&
		    sc->sc_radio_ready && sc->sc_sta_started &&
		    !sc->sc_join_stopped;
		if (accepted)
			sc->sc_link = true;
	}
	link_state = sc->sc_link;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (sc->sc_attached)
		if_link_state_change(&sc->sc_ec.ec_if,
	    link_state ? LINK_STATE_UP : LINK_STATE_DOWN);
#else
	(void)bssid;
	(void)association;
	sc->sc_link = up != 0;
	if (sc->sc_attached)
		if_link_state_change(&sc->sc_ec.ec_if,
		    up ? LINK_STATE_UP : LINK_STATE_DOWN);
#endif
}

void
espwifi_if_link(int up, const unsigned char *bssid)
{
	espwifi_if_link_event(up, bssid, false);
}

#ifdef ESPWIFI_NET80211_WPA2
int
espwifi_if_assoc_response(unsigned int subtype, const void *body,
    unsigned int len, const unsigned char *sender)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	const uint8_t *response = body;
	bool accepted, sae;
	uint64_t generation;

	/* IDF's wpa_sta_rx_mgmt receives the response body, without FC/header. */
	if ((subtype != 1 && subtype != 3) || response == NULL || len < 6 ||
	    sender == NULL || response[2] != 0 || response[3] != 0)
		return 0;
	if (!sc->sc_attached)
		return 0;
	mutex_spin_enter(&sc->sc_scan_lock);
	accepted = sc->sc_radio_ready && sc->sc_sta_started &&
	    !sc->sc_join_stopped && sc->sc_join_active != 0 &&
	    !sc->sc_joined && memcmp(sc->sc_join_bssid, sender,
	    ETHER_ADDR_LEN) == 0;
	generation = sc->sc_join_active;
	sae = accepted && sc->sc_join_sae;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (!accepted)
		return 0;
	/* Confirm PMF configuration; RSN and installed keys gate authorization. */
	if (sae && !espwifi_ext_sta_pmf_configured()) {
		(void)espwifi_if_fail_closed_generation(sc, generation);
		return 0;
	}
	mutex_spin_enter(&sc->sc_scan_lock);
	accepted = sc->sc_radio_ready && sc->sc_sta_started &&
	    !sc->sc_join_stopped && sc->sc_join_active == generation &&
	    !sc->sc_joined && memcmp(sc->sc_join_bssid, sender,
	    ETHER_ADDR_LEN) == 0;
	if (accepted)
		sc->sc_joined = true;
	mutex_spin_exit(&sc->sc_scan_lock);
	if (!accepted)
		return 0;
	espwifi_if_link_event(1, sender, true);
	mutex_spin_enter(&sc->sc_scan_lock);
	accepted = sc->sc_joined && sc->sc_link &&
	    memcmp(sc->sc_join_bssid, sender, ETHER_ADDR_LEN) == 0;
	mutex_spin_exit(&sc->sc_scan_lock);
	return accepted ? 1 : 0;
}

bool
espwifi_if_wpa_in_4way(void)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	bool active;

	if (!sc->sc_attached)
		return false;
	mutex_spin_enter(&sc->sc_scan_lock);
	active = sc->sc_join_active != 0 && sc->sc_joined &&
	    !sc->sc_join_stopped && !atomic_load_acquire(&sc->sc_port_authorized);
	mutex_spin_exit(&sc->sc_scan_lock);
	return active;
}
#else
int
espwifi_if_assoc_response(unsigned int subtype, const void *body,
    unsigned int len, const unsigned char *sender)
{
	(void)subtype;
	(void)body;
	(void)len;
	(void)sender;
	return 0;
}

bool
espwifi_if_wpa_in_4way(void)
{
	return false;
}
#endif

static int
espwifi_if_init(struct ifnet *ifp)
{
	struct espwifi_softc * const sc = ifp->if_softc;
#ifdef ESPWIFI_NET80211
	int error;
#endif

	if (!sc->sc_tx_ready)
		return ENXIO;
	ifp->if_flags |= IFF_RUNNING;
#ifdef ESPWIFI_NET80211
	if (sc->sc_ic.ic_roaming != IEEE80211_ROAMING_MANUAL) {
		error = ieee80211_new_state(&sc->sc_ic, IEEE80211_S_SCAN, -1);
		if (error)
			ifp->if_flags &= ~IFF_RUNNING;
		return error;
	}
#endif
	return 0;
}

static void
espwifi_if_stop(struct ifnet *ifp, int disable)
{
#ifdef ESPWIFI_NET80211
	struct espwifi_softc * const sc = ifp->if_softc;

	(void)ieee80211_new_state(&sc->sc_ic, IEEE80211_S_INIT, -1);
#endif
	ifp->if_flags &= ~IFF_RUNNING;
}

static int
espwifi_if_ioctl(struct ifnet *ifp, u_long cmd, void *data)
{
#ifdef ESPWIFI_NET80211
	struct espwifi_softc * const sc = ifp->if_softc;
	int s, error;

#ifdef ESPWIFI_NET80211_WPA2
	if ((cmd == SIOCG80211 || cmd == SIOCS80211) &&
	    ((struct ieee80211req *)data)->i_type == IEEE80211_IOC_SAE)
		return ieee80211_ioctl_sae(&sc->sc_ic, cmd, data, ESPWIFI_SAE_CAPS,
		    espwifi_if_sae_request);
#endif
	s = splnet();
	error = ieee80211_ioctl(&sc->sc_ic, cmd, data);
#else
	int s = splnet();
	int error = ether_ioctl(ifp, cmd, data);
#endif

	if (error == ENETRESET) {
#ifdef ESPWIFI_NET80211
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			(void)ieee80211_new_state(&sc->sc_ic, IEEE80211_S_INIT, -1);
			error = espwifi_if_init(ifp);
		} else
#endif
		error = 0;
	}
	splx(s);
	return error;
}

#ifndef ESPWIFI_NET80211
static int
espwifi_if_mediachange(struct ifnet *ifp)
{
	return 0;
}

static void
espwifi_if_mediastatus(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct espwifi_softc * const sc = ifp->if_softc;

	imr->ifm_status = IFM_AVALID | (sc->sc_link ? IFM_ACTIVE : 0);
	imr->ifm_active = IFM_ETHER | IFM_AUTO;
}
#endif

int
espwifi_if_attach(void)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	struct ifnet * const ifp = &sc->sc_ec.ec_if;
	uint8_t enaddr[ETHER_ADDR_LEN];
	void *handle;

	espwifi_get_mac(enaddr);
	memcpy(sc->sc_mac, enaddr, sizeof(sc->sc_mac));
	sc->sc_txbuf = malloc(ESPWIFI_MTU_FRAME, M_DEVBUF, M_WAITOK);
	sc->sc_txsem = espwifi_os_sem_create(1, 0);
	if (sc->sc_txsem == NULL) {
		free(sc->sc_txbuf, M_DEVBUF);
		printf("espwifi0: cannot allocate transmit semaphore\n");
		return 0;
	}
#ifdef ESPWIFI_NET80211
	mutex_init(&sc->sc_scan_lock, MUTEX_DEFAULT, IPL_HIGH);
	mutex_init(&sc->sc_key_lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&sc->sc_key_cv, "espwkey");
#ifdef ESPWIFI_NET80211_WPA2
	/* splnet() aliases splvm(); this state lock must not sleep there. */
	mutex_init(&sc->sc_sae_lock, MUTEX_DEFAULT, IPL_VM);
	cv_init(&sc->sc_sae_cv, "espwsae");
#endif
	sc->sc_scan_sem = espwifi_os_sem_create(1, 0);
	sc->sc_scan_ctl_sem = espwifi_os_sem_create(1, 0);
	if (sc->sc_scan_sem == NULL || sc->sc_scan_ctl_sem == NULL) {
		if (sc->sc_scan_sem != NULL)
			espwifi_os_sem_delete(sc->sc_scan_sem);
		if (sc->sc_scan_ctl_sem != NULL)
			espwifi_os_sem_delete(sc->sc_scan_ctl_sem);
		mutex_destroy(&sc->sc_scan_lock);
		cv_destroy(&sc->sc_key_cv);
		mutex_destroy(&sc->sc_key_lock);
#ifdef ESPWIFI_NET80211_WPA2
		cv_destroy(&sc->sc_sae_cv);
		mutex_destroy(&sc->sc_sae_lock);
#endif
		espwifi_os_sem_delete(sc->sc_txsem);
		free(sc->sc_txbuf, M_DEVBUF);
		printf("espwifi0: cannot allocate scan semaphore\n");
		return 0;
	}
#endif

	strlcpy(ifp->if_xname, "espwifi0", IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = espwifi_if_ioctl;
	ifp->if_start = espwifi_if_start;
	ifp->if_init = espwifi_if_init;
	ifp->if_stop = espwifi_if_stop;
	IFQ_SET_READY(&ifp->if_snd);

#ifndef ESPWIFI_NET80211
	sc->sc_ec.ec_ifmedia = &sc->sc_media;
	ifmedia_init(&sc->sc_media, 0, espwifi_if_mediachange,
	    espwifi_if_mediastatus);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);
#endif

	if_initialize(ifp);
	sc->sc_ipq = if_percpuq_create(ifp);
	if_deferred_start_init(ifp, NULL);
#ifdef ESPWIFI_NET80211
	sc->sc_ic.ic_ifp = ifp;
	sc->sc_ic.ic_phytype = IEEE80211_T_OFDM;
	sc->sc_ic.ic_opmode = IEEE80211_M_STA;
	sc->sc_ic.ic_state = IEEE80211_S_INIT;
#ifdef ESPWIFI_NET80211_WPA2
	sc->sc_ic.ic_caps = IEEE80211_C_AES_CCM | IEEE80211_C_WPA2;
#endif
	sc->sc_ic.ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	sc->sc_ic.ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
	for (unsigned chan = 1; chan <= 14; chan++) {
		sc->sc_ic.ic_channels[chan].ic_freq =
		    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_2GHZ);
		sc->sc_ic.ic_channels[chan].ic_flags = IEEE80211_CHAN_CCK |
		    IEEE80211_CHAN_2GHZ;
		if (chan != 14)
			sc->sc_ic.ic_channels[chan].ic_flags |=
			    IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN;
	}
	memcpy(sc->sc_ic.ic_myaddr, enaddr, sizeof(sc->sc_ic.ic_myaddr));
	ieee80211_ifattach(&sc->sc_ic);
#ifdef ESPWIFI_NET80211_WPA2
	sc->sc_ic.ic_crypto.cs_key_set = espwifi_if_key_set;
	sc->sc_ic.ic_crypto.cs_key_delete = espwifi_if_key_delete;
	sc->sc_ic.ic_sta_set_port = espwifi_if_set_port;
#endif
	sc->sc_newstate = sc->sc_ic.ic_newstate;
	sc->sc_ic.ic_newstate = espwifi_if_newstate;
#else
	ether_ifattach(ifp, enaddr);
#endif
	if (!espwifi_os_task_create(espwifi_if_txthread, "tx", 0, sc,
	    &handle))
		printf("espwifi0: cannot create the transmit thread\n");
	else
		sc->sc_tx_ready = true;
#ifdef ESPWIFI_NET80211
	if (!espwifi_os_task_create(espwifi_if_scanthread, "scan", 0, sc,
	    &handle)) {
		printf("espwifi0: cannot create the scan thread\n");
	} else if (!espwifi_os_task_create(espwifi_if_scan_control, "scanctl",
	    0, sc, &handle)) {
		mutex_spin_enter(&sc->sc_scan_lock);
		sc->sc_scan_shutdown = true;
		mutex_spin_exit(&sc->sc_scan_lock);
		espwifi_os_sem_give(sc->sc_scan_sem);
		printf("espwifi0: cannot create the scan control thread\n");
	} else {
		sc->sc_scan_enabled = true;
	}
#endif
	if_register(ifp);
#ifdef ESPWIFI_NET80211
	ieee80211_media_init(&sc->sc_ic, ieee80211_media_change,
	    ieee80211_media_status);
#endif
	if_link_state_change(ifp, LINK_STATE_DOWN);
	sc->sc_attached = true;

	printf("espwifi0: Ethernet address %s\n", ether_sprintf(enaddr));
	return 1;
}
