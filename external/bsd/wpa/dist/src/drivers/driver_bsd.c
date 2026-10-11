/*
 * WPA Supplicant - driver interaction with BSD net80211 layer
 * Copyright (c) 2004, Sam Leffler <sam@errno.com>
 * Copyright (c) 2004, 2Wire, Inc
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <sys/ioctl.h>

#include "common.h"
#include "driver.h"
#include "eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_common.h"

#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#ifdef __NetBSD__
#include <net/if_ether.h>
#else
#include <net/ethernet.h>
#endif
#include <net/route.h>

#ifdef __DragonFly__
#include <netproto/802_11/ieee80211_ioctl.h>
#include <netproto/802_11/ieee80211_dragonfly.h>
#else /* __DragonFly__ */
#ifdef __GLIBC__
#include <netinet/ether.h>
#endif /* __GLIBC__ */
/* net80211 uses this name for the one-byte type, not the RSN selector. */
#undef WPA_OUI_TYPE
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_crypto.h>
#endif /* __DragonFly__ || __GLIBC__ */
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <net80211/ieee80211_freebsd.h>
#endif
#if __NetBSD__
#include <net80211/ieee80211_netbsd.h>
#endif

#include "l2_packet/l2_packet.h"

struct bsd_driver_global {
	void		*ctx;
	int		sock;			/* socket for 802.11 ioctls */
	int		route;			/* routing socket for events */
	struct dl_list	ifaces;			/* list of interfaces */
};

struct bsd_driver_data {
	struct dl_list	list;
	struct bsd_driver_global *global;
	void	*ctx;

	struct l2_packet_data *sock_xmit;/* raw packet xmit socket */
	char	ifname[IFNAMSIZ+1];	/* interface name */
	int	flags;
	unsigned int ifindex;		/* interface index */
	int	if_removed;		/* has the interface been removed? */
	struct wpa_driver_capa capa;	/* driver capability */
	int	is_ap;			/* Access point mode */
	int	prev_roaming;	/* roaming state to restore on deinit */
	int	prev_privacy;	/* privacy state to restore on deinit */
	int	prev_wpa;	/* wpa state to restore on deinit */
#if defined(IEEE80211_IOC_SAE) && defined(CONFIG_SAE)
	u32 sae_generation;
	u8 sae_bssid[ETH_ALEN];
	int sae_active;
#ifdef IEEE80211_SAE_CAP_SA_QUERY
	int sae_query;
#endif
#endif
#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
	int sta_btm;
	int sta_rrm;
	int sta_scan_pending;
	uintptr_t sta_scan_generation;
	u32 sta_scan_epoch;
	u32 sta_scan_cookie;
	u8 sta_scan_bssid[ETH_ALEN];
	struct os_reltime sta_scan_completed;
	u8 sta_addr[ETH_ALEN];
	u8 sta_extcap[4], sta_extcap_mask[4];
	u32 sta_cookie;
	u32 sta_tx_generation;
	u16 sta_tx_freq;
	u8 *sta_tx_frame;
	size_t sta_tx_len;
#endif
	enum ieee80211_opmode opmode;	/* operation mode */
};

#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
static void bsd_sta_scan_clear(struct bsd_driver_data *);
static void bsd_sta_tx_status(struct bsd_driver_data *, int, int);
#endif

/* Generic functions for hostapd and wpa_supplicant */

static struct bsd_driver_data *
bsd_get_drvindex(void *priv, unsigned int ifindex)
{
	struct bsd_driver_global *global = priv;
	struct bsd_driver_data *drv;

	dl_list_for_each(drv, &global->ifaces, struct bsd_driver_data, list) {
		if (drv->ifindex == ifindex)
			return drv;
	}
	return NULL;
}

static struct bsd_driver_data *
bsd_get_drvname(void *priv, const char *ifname)
{
	struct bsd_driver_global *global = priv;
	struct bsd_driver_data *drv;

	dl_list_for_each(drv, &global->ifaces, struct bsd_driver_data, list) {
		if (os_strcmp(drv->ifname, ifname) == 0)
			return drv;
	}
	return NULL;
}

static int
bsd_set80211(void *priv, int op, int val, const void *arg, int arg_len)
{
	struct bsd_driver_data *drv = priv;
	struct ieee80211req ireq;

	if (drv->ifindex == 0 || drv->if_removed)
		return -1;

	os_memset(&ireq, 0, sizeof(ireq));
	os_strlcpy(ireq.i_name, drv->ifname, sizeof(ireq.i_name));
	ireq.i_type = op;
	ireq.i_val = val;
	ireq.i_data = (void *) arg;
	ireq.i_len = arg_len;

	if (ioctl(drv->global->sock, SIOCS80211, &ireq) < 0) {
		wpa_printf(MSG_ERROR, "ioctl[SIOCS80211, op=%u, val=%u, "
			   "arg_len=%u]: %s", op, val, arg_len,
			   strerror(errno));
		return -1;
	}
	return 0;
}

static int
bsd_get80211(void *priv, struct ieee80211req *ireq, int op, void *arg,
	     int arg_len)
{
	struct bsd_driver_data *drv = priv;

	os_memset(ireq, 0, sizeof(*ireq));
	os_strlcpy(ireq->i_name, drv->ifname, sizeof(ireq->i_name));
	ireq->i_type = op;
	ireq->i_len = arg_len;
	ireq->i_data = arg;

	if (ioctl(drv->global->sock, SIOCG80211, ireq) < 0) {
		int level = drv->if_removed ? MSG_DEBUG : MSG_ERROR;

		wpa_printf(level, "ioctl[SIOCG80211, op=%u, "
			   "arg_len=%u]: %s", op, arg_len, strerror(errno));
		return -1;
	}
	return 0;
}

static int
get80211var(struct bsd_driver_data *drv, int op, void *arg, int arg_len)
{
	struct ieee80211req ireq;

	if (bsd_get80211(drv, &ireq, op, arg, arg_len) < 0)
		return -1;
	return ireq.i_len;
}

static int
set80211var(struct bsd_driver_data *drv, int op, const void *arg, int arg_len)
{
	return bsd_set80211(drv, op, 0, arg, arg_len);
}

static int
set80211param(struct bsd_driver_data *drv, int op, int arg)
{
	return bsd_set80211(drv, op, arg, NULL, 0);
}

static int
bsd_get_ssid(void *priv, u8 *ssid, int len)
{
	struct bsd_driver_data *drv = priv;
#ifdef SIOCG80211NWID
	struct ieee80211_nwid nwid;
	struct ifreq ifr;

	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, drv->ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (void *)&nwid;
	if (ioctl(drv->global->sock, SIOCG80211NWID, &ifr) < 0 ||
	    nwid.i_len > IEEE80211_NWID_LEN)
		return -1;
	os_memcpy(ssid, nwid.i_nwid, nwid.i_len);
	return nwid.i_len;
#else
	return get80211var(drv, IEEE80211_IOC_SSID, ssid, IEEE80211_NWID_LEN);
#endif
}

static int
bsd_set_ssid(void *priv, const u8 *ssid, int ssid_len)
{
	struct bsd_driver_data *drv = priv;
#ifdef SIOCS80211NWID
	struct ieee80211_nwid nwid;
	struct ifreq ifr;

	os_memcpy(nwid.i_nwid, ssid, ssid_len);
	nwid.i_len = ssid_len;
	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, drv->ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (void *)&nwid;
	return ioctl(drv->global->sock, SIOCS80211NWID, &ifr);
#else
	return set80211var(drv, IEEE80211_IOC_SSID, ssid, ssid_len);
#endif
}

static int
bsd_get_if_media(void *priv)
{
	struct bsd_driver_data *drv = priv;
	struct ifmediareq ifmr;

	os_memset(&ifmr, 0, sizeof(ifmr));
	os_strlcpy(ifmr.ifm_name, drv->ifname, sizeof(ifmr.ifm_name));

	if (ioctl(drv->global->sock, SIOCGIFMEDIA, &ifmr) < 0) {
		wpa_printf(MSG_ERROR, "%s: SIOCGIFMEDIA %s", __func__,
			   strerror(errno));
		return -1;
	}

	return ifmr.ifm_current;
}

static int
bsd_set_if_media(void *priv, int media)
{
	struct bsd_driver_data *drv = priv;
	struct ifreq ifr;

	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, drv->ifname, sizeof(ifr.ifr_name));
	ifr.ifr_media = media;

	if (ioctl(drv->global->sock, SIOCSIFMEDIA, &ifr) < 0) {
		wpa_printf(MSG_ERROR, "%s: SIOCSIFMEDIA %s", __func__,
			   strerror(errno));
		return -1;
	}

	return 0;
}

static int
bsd_set_mediaopt(void *priv, uint32_t mask, uint32_t mode)
{
	int media = bsd_get_if_media(priv);

	if (media < 0)
		return -1;
	media &= ~mask;
	media |= mode;
	if (bsd_set_if_media(priv, media) < 0)
		return -1;
	return 0;
}

#if defined(IEEE80211_IOC_SAE) && defined(CONFIG_SAE)
static int
bsd_sae_request(struct bsd_driver_data *drv, u16 op, const void *data, size_t len)
{
	struct ieee80211req_sae req;
	int ret;

	if (len > sizeof(req.data))
		return -1;
	os_memset(&req, 0, sizeof(req));
	req.version = IEEE80211_SAE_VERSION;
	req.generation = drv->sae_generation;
	req.op = op;
	req.len = len;
	os_memcpy(req.bssid, drv->sae_bssid, ETH_ALEN);
	if (len)
		os_memcpy(req.data, data, len);
	ret = set80211var(drv, IEEE80211_IOC_SAE, &req, sizeof(req));
	forced_memzero(&req, sizeof(req));
	return ret;
}

#ifndef HOSTAPD
static int
bsd_sae_send_mlme(void *priv, const u8 *data, size_t len, int noack,
		  unsigned int freq, const u16 *csa_offs, size_t csa_offs_len,
		  int no_encrypt, unsigned int wait, int link_id)
{
	struct bsd_driver_data *drv = priv;

	if (!drv->sae_active || len < 30 || WPA_GET_LE16(data) != 0xb0 ||
	    csa_offs_len || link_id >= 0)
		return -1;
	return bsd_sae_request(drv, IEEE80211_SAE_TX_FRAME, data, len);
}

#ifdef IEEE80211_SAE_CAP_SA_QUERY
static int
bsd_sae_send_action(void *priv, unsigned int freq, unsigned int wait,
		    const u8 *dst, const u8 *src, const u8 *bssid,
		    const u8 *data, size_t data_len, int no_cck)
{
	struct bsd_driver_data *drv = priv;
	u8 frame[28] = { 0xd0 };

	/* On-channel SA Query only; the kernel requires the current station PTK. */
	if (!drv->sae_active || !drv->sae_query || wait || data_len != 4 ||
	    data[0] != WLAN_ACTION_SA_QUERY || data[1] > 1 ||
	    os_memcmp(dst, drv->sae_bssid, ETH_ALEN) != 0 ||
	    os_memcmp(bssid, drv->sae_bssid, ETH_ALEN) != 0)
		return -1;
	os_memcpy(frame + 4, dst, ETH_ALEN);
	os_memcpy(frame + 10, src, ETH_ALEN);
	os_memcpy(frame + 16, bssid, ETH_ALEN);
	os_memcpy(frame + 24, data, data_len);
	return bsd_sae_request(drv, IEEE80211_SAE_TX_SA_QUERY, frame, sizeof(frame));
}
#endif

static int
bsd_sae_auth_status(
void *priv, struct external_auth *params)
{
	struct bsd_driver_data *drv = priv;
	u8 status[2];

	if (!drv->sae_active || !params->bssid ||
	    os_memcmp(params->bssid, drv->sae_bssid, ETH_ALEN) != 0 ||
	    params->pmkid || params->mld_addr)
		return -1;
	WPA_PUT_LE16(status, params->status);
	return bsd_sae_request(drv, IEEE80211_SAE_AUTH_STATUS, status, sizeof(status));
}
#endif /* HOSTAPD */
#endif /* IEEE80211_IOC_SAE */

static int
bsd_del_key(void *priv, const u8 *addr, int key_idx)
{
	struct ieee80211req_del_key wk;
#if defined(IEEE80211_IOC_SAE) && defined(CONFIG_SAE)
	struct bsd_driver_data *drv = priv;
	if (key_idx == 4 || key_idx == 5) {
		u8 index[2];
		if (!drv->sae_active)
			return 0;
		WPA_PUT_LE16(index, key_idx);
		return bsd_sae_request(drv, IEEE80211_SAE_DELETE_IGTK, index, sizeof(index));
	}
#endif


	os_memset(&wk, 0, sizeof(wk));
	if (addr == NULL) {
		wpa_printf(MSG_DEBUG, "%s: key_idx=%d", __func__, key_idx);
		wk.idk_keyix = key_idx;
	} else {
		wpa_printf(MSG_DEBUG, "%s: addr=" MACSTR, __func__,
			   MAC2STR(addr));
		os_memcpy(wk.idk_macaddr, addr, IEEE80211_ADDR_LEN);
		wk.idk_keyix = (u_int8_t) IEEE80211_KEYIX_NONE;	/* XXX */
	}

	return set80211var(priv, IEEE80211_IOC_DELKEY, &wk, sizeof(wk));
}

static int
bsd_send_mlme_param(void *priv, const u8 op, const u16 reason, const u8 *addr)
{
	struct ieee80211req_mlme mlme;

	os_memset(&mlme, 0, sizeof(mlme));
	mlme.im_op = op;
	mlme.im_reason = reason;
	os_memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
	wpa_printf(MSG_DEBUG, "%s: op=%d reason=%d addr=" MACSTR, __func__,
	    op, reason, MAC2STR(addr));
	return set80211var(priv, IEEE80211_IOC_MLME, &mlme, sizeof(mlme));
}

static int
bsd_get_iface_flags(struct bsd_driver_data *drv)
{
	struct ifreq ifr;

	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, drv->ifname, sizeof(ifr.ifr_name));

	if (ioctl(drv->global->sock, SIOCGIFFLAGS, &ifr) < 0) {
		wpa_printf(MSG_ERROR, "ioctl[SIOCGIFFLAGS]: %s",
			   strerror(errno));
		return -1;
	}
	drv->flags = ifr.ifr_flags;
	return 0;
}

static int
bsd_set_key(void *priv, struct wpa_driver_set_key_params *params)
{
	struct ieee80211req_key wk;
#ifdef IEEE80211_KEY_NOREPLAY
	struct bsd_driver_data *drv = priv;
#endif /* IEEE80211_KEY_NOREPLAY */
	enum wpa_alg alg = params->alg;
	const u8 *addr = params->addr;
	int key_idx = params->key_idx;
	int set_tx = params->set_tx;
	const u8 *seq = params->seq;
	size_t seq_len = params->seq_len;
	const u8 *key = params->key;
	size_t key_len = params->key_len;

	wpa_printf(MSG_DEBUG, "%s: alg=%d addr=%p key_idx=%d set_tx=%d "
		   "seq_len=%zu key_len=%zu", __func__, alg, addr, key_idx,
		   set_tx, seq_len, key_len);

	if (alg == WPA_ALG_NONE) {
#ifndef HOSTAPD
		if (addr == NULL || is_broadcast_ether_addr(addr))
			return bsd_del_key(priv, NULL, key_idx);
		else
#endif /* HOSTAPD */
			return bsd_del_key(priv, addr, key_idx);
	}

#if defined(IEEE80211_IOC_SAE) && defined(CONFIG_SAE)
	if (alg == WPA_ALG_BIP_CMAC_128) {
		struct bsd_driver_data *sae_drv = priv;
		u8 material[24];
		int ret;

		if (!sae_drv->sae_active || key_len != 16 || seq_len != 6 ||
		    (key_idx != 4 && key_idx != 5))
			return -1;
		WPA_PUT_LE16(material, key_idx);
		os_memcpy(material + 2, seq, 6);
		os_memcpy(material + 8, key, 16);
		ret = bsd_sae_request(sae_drv, IEEE80211_SAE_SET_IGTK, material, sizeof(material));
		forced_memzero(material, sizeof(material));
		return ret;
	}
#endif
	os_memset(&wk, 0, sizeof(wk));
	switch (alg) {

	case WPA_ALG_WEP:
		wk.ik_type = IEEE80211_CIPHER_WEP;
		break;
	case WPA_ALG_TKIP:
		wk.ik_type = IEEE80211_CIPHER_TKIP;
		break;
	case WPA_ALG_CCMP:
		wk.ik_type = IEEE80211_CIPHER_AES_CCM;
		break;
	default:
		wpa_printf(MSG_ERROR, "%s: unknown alg=%d", __func__, alg);
		return -1;
	}

	wk.ik_flags = IEEE80211_KEY_RECV;
	if (set_tx)
		wk.ik_flags |= IEEE80211_KEY_XMIT;

	if (addr == NULL) {
		os_memset(wk.ik_macaddr, 0xff, IEEE80211_ADDR_LEN);
		wk.ik_keyix = key_idx;
	} else {
		os_memcpy(wk.ik_macaddr, addr, IEEE80211_ADDR_LEN);
		/*
		 * Deduce whether group/global or unicast key by checking
		 * the address (yech).  Note also that we can only mark global
		 * keys default; doing this for a unicast key is an error.
		 */
		if (is_broadcast_ether_addr(addr)) {
			wk.ik_flags |= IEEE80211_KEY_GROUP;
			wk.ik_keyix = key_idx;
		} else {
			wk.ik_keyix = key_idx == 0 ? IEEE80211_KEYIX_NONE :
				key_idx;
		}
	}
	if (wk.ik_keyix != IEEE80211_KEYIX_NONE && set_tx)
		wk.ik_flags |= IEEE80211_KEY_DEFAULT;
#ifndef HOSTAPD
#ifdef IEEE80211_KEY_NOREPLAY
	/*
	 * Ignore replay failures in IBSS and AHDEMO mode.
	 */
	if (drv->opmode == IEEE80211_M_IBSS ||
	    drv->opmode == IEEE80211_M_AHDEMO)
		wk.ik_flags |= IEEE80211_KEY_NOREPLAY;
#endif /* IEEE80211_KEY_NOREPLAY */
#endif /* HOSTAPD */
	wk.ik_keylen = key_len;
	if (seq) {
#ifdef WORDS_BIGENDIAN
		/*
		 * wk.ik_keyrsc is in host byte order (big endian), need to
		 * swap it to match with the byte order used in WPA.
		 */
		int i;
		u8 *keyrsc = (u8 *) &wk.ik_keyrsc;
		for (i = 0; i < seq_len; i++)
			keyrsc[WPA_KEY_RSC_LEN - i - 1] = seq[i];
#else /* WORDS_BIGENDIAN */
		os_memcpy(&wk.ik_keyrsc, seq, seq_len);
#endif /* WORDS_BIGENDIAN */
	}
	os_memcpy(wk.ik_keydata, key, key_len);

	return set80211var(priv, IEEE80211_IOC_WPAKEY, &wk, sizeof(wk));
}

static int
bsd_configure_wpa(void *priv, struct wpa_bss_params *params)
{
#ifndef IEEE80211_IOC_APPIE
	static const char *ciphernames[] =
		{ "WEP", "TKIP", "AES-OCB", "AES-CCM", "CKIP", "NONE" };
	int v;

	switch (params->wpa_group) {
	case WPA_CIPHER_CCMP:
		v = IEEE80211_CIPHER_AES_CCM;
		break;
	case WPA_CIPHER_TKIP:
		v = IEEE80211_CIPHER_TKIP;
		break;
	case WPA_CIPHER_WEP104:
		v = IEEE80211_CIPHER_WEP;
		break;
	case WPA_CIPHER_WEP40:
		v = IEEE80211_CIPHER_WEP;
		break;
	case WPA_CIPHER_NONE:
		v = IEEE80211_CIPHER_NONE;
		break;
	default:
		wpa_printf(MSG_INFO, "Unknown group key cipher %u",
			   params->wpa_group);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "%s: group key cipher=%s (%u)",
		   __func__, ciphernames[v], v);
	if (set80211param(priv, IEEE80211_IOC_MCASTCIPHER, v)) {
		wpa_printf(MSG_INFO,
			   "Unable to set group key cipher to %u (%s)",
			   v, ciphernames[v]);
		return -1;
	}
	if (v == IEEE80211_CIPHER_WEP) {
		/* key length is done only for specific ciphers */
		v = (params->wpa_group == WPA_CIPHER_WEP104 ? 13 : 5);
		if (set80211param(priv, IEEE80211_IOC_MCASTKEYLEN, v)) {
			wpa_printf(MSG_INFO,
				   "Unable to set group key length to %u", v);
			return -1;
		}
	}

	v = 0;
	if (params->wpa_pairwise & WPA_CIPHER_CCMP)
		v |= 1<<IEEE80211_CIPHER_AES_CCM;
	if (params->wpa_pairwise & WPA_CIPHER_TKIP)
		v |= 1<<IEEE80211_CIPHER_TKIP;
	if (params->wpa_pairwise & WPA_CIPHER_NONE)
		v |= 1<<IEEE80211_CIPHER_NONE;
	wpa_printf(MSG_DEBUG, "%s: pairwise key ciphers=0x%x", __func__, v);
	if (set80211param(priv, IEEE80211_IOC_UCASTCIPHERS, v)) {
		wpa_printf(MSG_INFO,
			   "Unable to set pairwise key ciphers to 0x%x", v);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "%s: key management algorithms=0x%x",
		   __func__, params->wpa_key_mgmt);
	if (set80211param(priv, IEEE80211_IOC_KEYMGTALGS,
			  params->wpa_key_mgmt)) {
		wpa_printf(MSG_INFO,
			   "Unable to set key management algorithms to 0x%x",
			   params->wpa_key_mgmt);
		return -1;
	}

	v = 0;
	if (params->rsn_preauth)
		v |= BIT(0);
	wpa_printf(MSG_DEBUG, "%s: rsn capabilities=0x%x",
		   __func__, params->rsn_preauth);
	if (set80211param(priv, IEEE80211_IOC_RSNCAPS, v)) {
		wpa_printf(MSG_INFO, "Unable to set RSN capabilities to 0x%x",
			   v);
		return -1;
	}
#endif /* IEEE80211_IOC_APPIE */

	wpa_printf(MSG_DEBUG, "%s: enable WPA= 0x%x", __func__, params->wpa);
	if (set80211param(priv, IEEE80211_IOC_WPA, params->wpa)) {
		wpa_printf(MSG_INFO, "Unable to set WPA to %u", params->wpa);
		return -1;
	}
	return 0;
}

static int
bsd_set_ieee8021x(void *priv, struct wpa_bss_params *params)
{
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, params->enabled);

	if (!params->enabled) {
		/* XXX restore state */
		return set80211param(priv, IEEE80211_IOC_AUTHMODE,
				     IEEE80211_AUTH_AUTO);
	}
	if (!params->wpa && !params->ieee802_1x) {
		wpa_printf(MSG_ERROR, "%s: No 802.1X or WPA enabled",
			   __func__);
		return -1;
	}
	if (params->wpa && bsd_configure_wpa(priv, params) != 0) {
		wpa_printf(MSG_ERROR, "%s: Failed to configure WPA state",
			   __func__);
		return -1;
	}
	if (set80211param(priv, IEEE80211_IOC_AUTHMODE,
		(params->wpa ? IEEE80211_AUTH_WPA : IEEE80211_AUTH_8021X))) {
		wpa_printf(MSG_ERROR, "%s: Failed to enable WPA/802.1X",
			   __func__);
		return -1;
	}
	return 0;
}

static void
bsd_new_sta(void *priv, void *ctx, u8 addr[IEEE80211_ADDR_LEN])
{
	struct ieee80211req_wpaie ie;
	int ielen = 0;
	u8 *iebuf = NULL;

	/*
	 * Fetch and validate any negotiated WPA/RSN parameters.
	 */
	memset(&ie, 0, sizeof(ie));
	memcpy(ie.wpa_macaddr, addr, IEEE80211_ADDR_LEN);
	if (get80211var(priv, IEEE80211_IOC_WPAIE, &ie, sizeof(ie)) < 0) {
		wpa_printf(MSG_INFO,
			   "Failed to get WPA/RSN information element");
		goto no_ie;
	}
	iebuf = ie.wpa_ie;
	ielen = ie.wpa_ie[1];
	if (ielen == 0)
		iebuf = NULL;
	else
		ielen += 2;

no_ie:
	drv_event_assoc(ctx, addr, iebuf, ielen, NULL, 0, NULL, -1, 0);
}

static int
bsd_send_eapol(void *priv, const u8 *addr, const u8 *data, size_t data_len,
	       int encrypt, const u8 *own_addr, u32 flags, int link_id)
{
	struct bsd_driver_data *drv = priv;

	wpa_hexdump(MSG_MSGDUMP, "TX EAPOL", data, data_len);

	return l2_packet_send(drv->sock_xmit, addr, ETH_P_EAPOL, data,
			      data_len);
}

static int
bsd_set_freq(void *priv, struct hostapd_freq_params *freq)
{
	struct bsd_driver_data *drv = priv;
#ifdef SIOCS80211CHANNEL
	struct ieee80211chanreq creq;
#endif /* SIOCS80211CHANNEL */
	u32 mode;
	int channel = freq->channel;

	if (channel < 14) {
		mode =
			freq->ht_enabled ? IFM_IEEE80211_11NG :
			IFM_IEEE80211_11G;
	} else if (channel == 14) {
		mode = IFM_IEEE80211_11B;
	} else {
		mode =
			freq->ht_enabled ? IFM_IEEE80211_11NA :
			IFM_IEEE80211_11A;
	}
	if (bsd_set_mediaopt(drv, IFM_MMASK, mode) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set modulation mode",
			   __func__);
		return -1;
	}

#ifdef SIOCS80211CHANNEL
	os_memset(&creq, 0, sizeof(creq));
	os_strlcpy(creq.i_name, drv->ifname, sizeof(creq.i_name));
	creq.i_channel = (u_int16_t)channel;
	return ioctl(drv->global->sock, SIOCS80211CHANNEL, &creq);
#else /* SIOCS80211CHANNEL */
	return set80211param(priv, IEEE80211_IOC_CHANNEL, channel);
#endif /* SIOCS80211CHANNEL */
}

static int
bsd_set_opt_ie(void *priv, const u8 *ie, size_t ie_len)
{
#ifdef IEEE80211_IOC_APPIE
	wpa_printf(MSG_DEBUG, "%s: set WPA+RSN ie (len %lu)", __func__,
		   (unsigned long)ie_len);
	return bsd_set80211(priv, IEEE80211_IOC_APPIE, IEEE80211_APPIE_WPA,
			    ie, ie_len);
#endif /* IEEE80211_IOC_APPIE */
	return 0;
}

#ifdef SO_RERROR
static void
bsd_route_overflow(int sock, void *ctx, struct bsd_driver_global *global)
{
	char event_buf[2048]; /* max size of a single route(4) msg */
	int n;
	struct ifaddrs *ifaddrs, *ifa;
	struct bsd_driver_data *drv;
	struct sockaddr_dl *sdl;
	union wpa_event_data event;

	/* We need to match the system state, so drain the route
	 * socket to avoid stale messages. */
	do {
		n = read(sock, event_buf, sizeof(event_buf));
	} while (n != -1 || errno == ENOBUFS);

	if (getifaddrs(&ifaddrs) == -1) {
		wpa_printf(MSG_ERROR, "%s getifaddrs() failed: %s",
			   __func__, strerror(errno));
		return;
	}

	/* add or update existing interfaces */
	for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		sdl = (struct sockaddr_dl *) (void *) ifa->ifa_addr;
		drv = bsd_get_drvname(global, ifa->ifa_name);
		if (drv != NULL &&
		    (drv->ifindex != sdl->sdl_index || drv->if_removed)) {
			wpa_printf(MSG_DEBUG,
				   "RTM_IFANNOUNCE: Interface '%s' added",
				   drv->ifname);
			drv->ifindex = sdl->sdl_index;
			drv->if_removed = 0;
			event.interface_status.ievent = EVENT_INTERFACE_ADDED;
			os_strlcpy(event.interface_status.ifname, ifa->ifa_name,
				   sizeof(event.interface_status.ifname));
			wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_STATUS,
					     &event);
		}
		if (!drv &&
		    (drv = bsd_get_drvindex(global, sdl->sdl_index)) != NULL) {
			/* Driver name is invalid */
			wpa_printf(MSG_DEBUG,
				   "RTM_IFANNOUNCE: Interface '%s' removed",
				   drv->ifname);
			drv->if_removed = 1;
			event.interface_status.ievent = EVENT_INTERFACE_REMOVED;
			os_strlcpy(event.interface_status.ifname, drv->ifname,
				   sizeof(event.interface_status.ifname));
			wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_STATUS,
					     &event);
		}
	}

	/* punt missing interfaces and update flags */
	dl_list_for_each(drv, &global->ifaces, struct bsd_driver_data, list) {
		for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
			if (ifa->ifa_addr == NULL ||
			    ifa->ifa_addr->sa_family != AF_LINK)
				continue;
			sdl = (struct sockaddr_dl *) (void *) ifa->ifa_addr;
			if (os_strcmp(drv->ifname, ifa->ifa_name) == 0)
				break;
		}
		if (ifa == NULL && !drv->if_removed) {
			wpa_printf(MSG_DEBUG,
				   "RTM_IFANNOUNCE: Interface '%s' removed",
				   drv->ifname);
			drv->if_removed = 1;
			event.interface_status.ievent = EVENT_INTERFACE_REMOVED;
			os_strlcpy(event.interface_status.ifname, drv->ifname,
				   sizeof(event.interface_status.ifname));
			wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_STATUS,
					     &event);
		}
		if (!ifa)
			continue;

		if ((ifa->ifa_flags & IFF_UP) == 0 &&
		    (drv->flags & IFF_UP) != 0) {
			wpa_printf(MSG_DEBUG, "RTM_IFINFO: Interface '%s' DOWN",
				   drv->ifname);
#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
			bsd_sta_scan_clear(drv);
			bsd_sta_tx_status(drv, 0, 0);
#endif
			wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_DISABLED,
					     NULL);
		} else if ((ifa->ifa_flags & IFF_UP) != 0 &&
			   (drv->flags & IFF_UP) == 0) {
			wpa_printf(MSG_DEBUG, "RTM_IFINFO: Interface '%s' UP",
				   drv->ifname);
			wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_ENABLED,
					     NULL);
		}
		drv->flags = ifa->ifa_flags;
	}

	freeifaddrs(ifaddrs);
}
#endif /* SO_RERROR */

#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
/* Keep compatibility with kernels and drivers without this extension. */
static int
bsd_sta_info(struct bsd_driver_data *drv, struct ieee80211req_sta_mgmt *req)
{
	struct ieee80211req ireq;

	os_memset(req, 0, sizeof(*req));
	os_memset(&ireq, 0, sizeof(ireq));
	os_strlcpy(ireq.i_name, drv->ifname, sizeof(ireq.i_name));
	ireq.i_type = IEEE80211_IOC_STA_MGMT;
	ireq.i_len = sizeof(*req);
	ireq.i_data = req;
	if (ioctl(drv->global->sock, SIOCG80211, &ireq) < 0 ||
	    req->version != IEEE80211_STA_VERSION ||
	    req->op != IEEE80211_STA_GET || req->reserved ||
	    req->len > sizeof(req->data))
		return -1;
	return 0;
}

static int
bsd_sta_set(struct bsd_driver_data *drv, struct ieee80211req_sta_mgmt *req,
	    unsigned int op)
{
	req->version = IEEE80211_STA_VERSION;
	req->op = op;
	req->flags = 0;
	return set80211var(drv, IEEE80211_IOC_STA_MGMT, req, sizeof(*req));
}

static void bsd_sta_scan_timeout(void *eloop_ctx, void *timeout_ctx);

static void
bsd_sta_scan_clear(struct bsd_driver_data *drv)
{
	drv->sta_scan_pending = 0;
	drv->sta_scan_generation++;
	eloop_cancel_timeout(bsd_sta_scan_timeout, drv, ELOOP_ALL_CTX);
}

static void
bsd_sta_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct bsd_driver_data *drv = eloop_ctx;
	struct ieee80211req_sta_mgmt req;
	union wpa_event_data event;

	if (!drv->sta_scan_pending ||
	    (uintptr_t) timeout_ctx != drv->sta_scan_generation)
		return;
	bsd_sta_scan_clear(drv);
	os_memset(&req, 0, sizeof(req));
	req.generation = drv->sta_scan_epoch;
	req.cookie = drv->sta_scan_cookie;
	os_memcpy(req.bssid, drv->sta_scan_bssid, ETH_ALEN);
	bsd_sta_set(drv, &req, IEEE80211_STA_CANCEL_SCAN);
	os_memset(&event, 0, sizeof(event));
	event.scan_info.aborted = 1;
	wpa_printf(MSG_DEBUG, "BSD: connected scan timed out");
	wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS, &event);
}

static void bsd_sta_tx_timeout(void *eloop_ctx, void *timeout_ctx);

static void
bsd_sta_tx_status(struct bsd_driver_data *drv, int ack, int notify)
{
	union wpa_event_data event;
	u8 *frame = drv->sta_tx_frame;
	size_t len = drv->sta_tx_len;

	if (!frame)
		return;
	eloop_cancel_timeout(bsd_sta_tx_timeout, drv, ELOOP_ALL_CTX);
	drv->sta_tx_frame = NULL;
	drv->sta_tx_len = 0;
	if (notify) {
		os_memset(&event, 0, sizeof(event));
		event.tx_status.type = WLAN_FC_TYPE_MGMT;
		event.tx_status.stype = WLAN_FC_STYPE_ACTION;
		event.tx_status.dst = frame + 4;
		event.tx_status.data = frame;
		event.tx_status.data_len = len;
		event.tx_status.ack = ack;
		event.tx_status.link_id = -1;
		wpa_supplicant_event(drv->ctx, EVENT_TX_STATUS, &event);
	}
	os_free(frame);
}

static void
bsd_sta_disconnected(struct bsd_driver_data *drv)
{
	union wpa_event_data event;
	int aborted = drv->sta_scan_pending;

	bsd_sta_tx_status(drv, 0, 0);
	bsd_sta_scan_clear(drv);
	wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
	if (aborted) {
		/* Release old scan radio work after clearing connection state. */
		os_memset(&event, 0, sizeof(event));
		event.scan_info.aborted = 1;
		wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS, &event);
	}
}

static void
bsd_sta_tx_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct bsd_driver_data *drv = eloop_ctx;
	struct ieee80211req_sta_mgmt req;

	if (!drv->sta_tx_frame || (uintptr_t) timeout_ctx != drv->sta_cookie)
		return;
	os_memset(&req, 0, sizeof(req));
	req.cookie = drv->sta_cookie;
	req.generation = drv->sta_tx_generation;
	os_memcpy(req.bssid, drv->sta_tx_frame + 4, ETH_ALEN);
	bsd_sta_set(drv, &req, IEEE80211_STA_CANCEL_TX);
	wpa_printf(MSG_DEBUG, "BSD: station action TX completion timed out");
	bsd_sta_tx_status(drv, 0, 1);
}

static int
bsd_sta_tx_body(struct bsd_driver_data *drv, const u8 *data, size_t len)
{
	if (len < 3)
		return 0;
	if (data[0] == WLAN_ACTION_WNM)
		return drv->sta_btm && len >= 4 &&
		    (data[1] == WNM_BSS_TRANS_MGMT_QUERY ||
		     (data[1] == WNM_BSS_TRANS_MGMT_RESP && len >= 5));
	if (data[0] == WLAN_ACTION_RADIO_MEASUREMENT)
		return drv->sta_rrm &&
		    (data[1] == WLAN_RRM_NEIGHBOR_REPORT_REQUEST ||
		     data[1] == WLAN_RRM_RADIO_MEASUREMENT_REPORT);
	return 0;
}

static int
bsd_send_action(void *priv, unsigned int freq, unsigned int wait,
		const u8 *dst, const u8 *src, const u8 *bssid,
		const u8 *data, size_t data_len, int no_cck)
{
	struct bsd_driver_data *drv = priv;
	struct ieee80211req_sta_mgmt req;
	u8 *frame;

#if defined(IEEE80211_IOC_SAE) && defined(CONFIG_SAE) && \
    defined(IEEE80211_SAE_CAP_SA_QUERY)
	if (data_len > 0 && data[0] == WLAN_ACTION_SA_QUERY)
		return bsd_sae_send_action(priv, freq, wait, dst, src, bssid,
					   data, data_len, no_cck);
#endif
	if (drv->sta_tx_frame || data_len > sizeof(req.data) ||
	    !bsd_sta_tx_body(drv, data, data_len) ||
	    bsd_sta_info(drv, &req) < 0 ||
	    !(req.flags & IEEE80211_STA_ASSOCIATED) || req.freq != freq ||
	    os_memcmp(dst, req.bssid, ETH_ALEN) ||
	    os_memcmp(bssid, req.bssid, ETH_ALEN) ||
	    os_memcmp(src, drv->sta_addr, ETH_ALEN))
		return -1;
	frame = os_zalloc(24 + data_len);
	if (!frame)
		return -1;
	frame[0] = WLAN_FC_STYPE_ACTION << 4;
	os_memcpy(frame + 4, dst, ETH_ALEN);
	os_memcpy(frame + 10, src, ETH_ALEN);
	os_memcpy(frame + 16, bssid, ETH_ALEN);
	os_memcpy(frame + 24, data, data_len);
	if (++drv->sta_cookie == 0)
		drv->sta_cookie++;
	req.cookie = drv->sta_cookie;
	req.len = data_len;
	os_memcpy(req.data, data, data_len);
	if (bsd_sta_set(drv, &req, IEEE80211_STA_TX) < 0) {
		os_free(frame);
		return -1;
	}
	drv->sta_tx_frame = frame;
	drv->sta_tx_len = 24 + data_len;
	drv->sta_tx_generation = req.generation;
	drv->sta_tx_freq = req.freq;
	if (eloop_register_timeout(2, 0, bsd_sta_tx_timeout, drv, (void *) (uintptr_t) drv->sta_cookie) < 0) {
		req.len = 0;
		bsd_sta_set(drv, &req, IEEE80211_STA_CANCEL_TX);
		bsd_sta_tx_status(drv, 0, 0);
		return -1;
	}
	return 0;
}

static int
bsd_sta_rx_body(struct bsd_driver_data *drv, const u8 *data, size_t len)
{
	if (len < 3)
		return 0;
	if (data[0] == WLAN_ACTION_WNM)
		return drv->sta_btm && len >= 7 &&
		    data[1] == WNM_BSS_TRANS_MGMT_REQ;
	if (data[0] == WLAN_ACTION_RADIO_MEASUREMENT)
		return drv->sta_rrm &&
		    (data[1] == WLAN_RRM_NEIGHBOR_REPORT_RESPONSE ||
		     (data[1] == WLAN_RRM_RADIO_MEASUREMENT_REQUEST && len >= 5));
	return 0;
}

static void
bsd_sta_event(struct bsd_driver_data *drv, unsigned int what,
	      const void *payload, size_t len)
{
	const struct ieee80211req_sta_mgmt *req = payload;
	struct ieee80211req_sta_mgmt info;
	union wpa_event_data event;
	u8 frame[24 + IEEE80211_STA_MAX_BODY];

	if ((!drv->sta_btm && !drv->sta_rrm) ||
	    len < offsetof(struct ieee80211req_sta_mgmt, data) ||
	    req->version != IEEE80211_STA_VERSION || req->reserved ||
	    req->len > IEEE80211_STA_MAX_BODY ||
	    req->len != len - offsetof(struct ieee80211req_sta_mgmt, data))
		return;
	if (bsd_sta_info(drv, &info) < 0 ||
	    !(info.flags & IEEE80211_STA_ASSOCIATED) ||
	    info.generation != req->generation || info.freq != req->freq ||
	    os_memcmp(info.bssid, req->bssid, ETH_ALEN))
		return;
	if (what == RTM_IEEE80211_STA_SCAN) {
		if (req->op != IEEE80211_STA_SCAN || req->len ||
		    (req->flags & ~IEEE80211_STA_SCAN_ABORTED) ||
		    !drv->sta_scan_pending || req->cookie != drv->sta_scan_cookie ||
		    req->generation != drv->sta_scan_epoch ||
		    os_memcmp(req->bssid, drv->sta_scan_bssid, ETH_ALEN))
			return;
		bsd_sta_scan_clear(drv);
		os_get_reltime(&drv->sta_scan_completed);
		os_memset(&event, 0, sizeof(event));
		event.scan_info.aborted = !!(req->flags & IEEE80211_STA_SCAN_ABORTED);
		wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS, &event);
		return;
	}
	if (what == RTM_IEEE80211_STA_TX) {
		if (req->op == IEEE80211_STA_TX && !req->len &&
		    !(req->flags & ~IEEE80211_STA_TX_ACK) && drv->sta_tx_frame &&
		    req->cookie == drv->sta_cookie &&
		    req->generation == drv->sta_tx_generation &&
		    req->freq == drv->sta_tx_freq &&
		    !os_memcmp(req->bssid, drv->sta_tx_frame + 4, ETH_ALEN))
			bsd_sta_tx_status(drv, !!(req->flags & IEEE80211_STA_TX_ACK), 1);
		return;
	}
	if (what != RTM_IEEE80211_STA_RX || req->op != IEEE80211_STA_GET ||
	    req->cookie || req->flags ||
	    !bsd_sta_rx_body(drv, req->data, req->len))
		return;
	/* The driver has checked PMF integrity, replay state, and the sender. */
	os_memset(frame, 0, 24);
	frame[0] = WLAN_FC_STYPE_ACTION << 4;
	os_memcpy(frame + 4, drv->sta_addr, ETH_ALEN);
	os_memcpy(frame + 10, req->bssid, ETH_ALEN);
	os_memcpy(frame + 16, req->bssid, ETH_ALEN);
	os_memcpy(frame + 24, req->data, req->len);
	os_memset(&event, 0, sizeof(event));
	event.rx_mgmt.frame = frame;
	event.rx_mgmt.frame_len = 24 + req->len;
	event.rx_mgmt.freq = req->freq;
	event.rx_mgmt.link_id = -1;
	wpa_supplicant_event(drv->ctx, EVENT_RX_MGMT, &event);
}

static void
bsd_sta_init(struct bsd_driver_data *drv)
{
	struct ieee80211req_sta_mgmt info;

	/* Do not advertise WNM sleep or SSID List merely from CONFIG_WNM. */
	drv->sta_extcap_mask[2] = 0x0a;
	drv->sta_extcap_mask[3] = 0x02;
	drv->capa.extended_capa = drv->sta_extcap;
	drv->capa.extended_capa_mask = drv->sta_extcap_mask;
	drv->capa.extended_capa_len = sizeof(drv->sta_extcap);
	if (bsd_sta_info(drv, &info) < 0 || info.len < ETH_ALEN)
		return;
	os_memcpy(drv->sta_addr, info.data, ETH_ALEN);
	if (info.flags & IEEE80211_STA_CAP_BTM) {
		drv->sta_btm = 1;
		drv->sta_extcap_mask[2] = 0x02;
		wpa_printf(MSG_DEBUG, "BSD: host-managed BSS transitions available");
	}
	if (info.flags & IEEE80211_STA_CAP_RRM) {
		drv->sta_rrm = 1;
		drv->capa.rrm_flags |= WPA_DRIVER_FLAGS_SUPPORT_RRM |
		    WPA_DRIVER_FLAGS_RRM_NEIGHBOR_REPORT_ONLY;
		wpa_printf(MSG_DEBUG, "BSD: host-managed neighbor reports available");
	}
}

static int
bsd_sta_signal_poll(void *priv, struct wpa_signal_info *signal_info)
{
	struct bsd_driver_data *drv = priv;
	struct ieee80211req_sta_mgmt info;

	if (bsd_sta_info(drv, &info) < 0 ||
	    !(info.flags & IEEE80211_STA_ASSOCIATED))
		return -1;
	os_memset(signal_info, 0, sizeof(*signal_info));
	signal_info->frequency = info.freq;
	signal_info->current_noise = WPA_INVALID_NOISE;
	return 0;
}

static int
bsd_sta_associate(struct bsd_driver_data *drv,
		  struct wpa_driver_associate_params *params)
{
	struct ieee80211req_sta_mgmt req;
	const u8 *pos = params->wpa_ie;
	size_t left = params->wpa_ie_len;
	int btm = 0, enabled;

	if (!drv->sta_btm && !drv->sta_rrm)
		return 0;
	while (left >= 2 && pos[1] <= left - 2) {
		if (pos[0] == WLAN_EID_EXT_CAPAB && pos[1] >= 3)
			btm = !!(pos[4] & 0x08);
		left -= pos[1] + 2;
		pos += pos[1] + 2;
	}
	enabled = !left && ((drv->sta_btm && btm) ||
	    (drv->sta_rrm && params->rrm_used)) && params->wpa_ie_len &&
	    params->wpa_ie[0] == WLAN_EID_RSN &&
	    params->pairwise_suite == WPA_CIPHER_CCMP &&
	    params->group_suite == WPA_CIPHER_CCMP &&
	    (params->key_mgmt_suite == WPA_KEY_MGMT_PSK ||
	     (params->key_mgmt_suite == WPA_KEY_MGMT_SAE &&
	      params->mgmt_frame_protection == MGMT_FRAME_PROTECTION_REQUIRED));
	bsd_sta_tx_status(drv, 0, 0);
	bsd_sta_scan_clear(drv);
	os_memset(&req, 0, sizeof(req));
	return bsd_sta_set(drv, &req,
			   enabled ? IEEE80211_STA_ENABLE : IEEE80211_STA_DISABLE);
}
#endif /* __NetBSD__ && IEEE80211_IOC_STA_MGMT */

static void
bsd_wireless_event_receive(int sock, void *ctx, void *sock_ctx)
{
	char event_buf[2048]; /* max size of a single route(4) msg */
	struct bsd_driver_global *global = sock_ctx;
	struct bsd_driver_data *drv;
	struct if_announcemsghdr *ifan;
	struct if_msghdr *ifm;
	struct rt_msghdr *rtm;
	union wpa_event_data event;
	struct ieee80211_michael_event *mic;
	struct ieee80211_leave_event *leave;
	struct ieee80211_join_event *join;
	int n;

	n = read(sock, event_buf, sizeof(event_buf));
	if (n < 0) {
		if (errno != EINTR && errno != EAGAIN)
			wpa_printf(MSG_ERROR, "%s read() failed: %s",
				   __func__, strerror(errno));
#ifdef SO_RERROR
		if (errno == ENOBUFS)
			bsd_route_overflow(sock, ctx, sock_ctx);
#endif /* SO_RERROR */
		return;
	}

	if (n < 4)
		return;
	rtm = (struct rt_msghdr *) event_buf;
	if (rtm->rtm_msglen > n || rtm->rtm_msglen < 4)
		return;
	n = rtm->rtm_msglen;
	if (rtm->rtm_version != RTM_VERSION) {
		wpa_printf(MSG_DEBUG, "Invalid routing message version=%d",
			   rtm->rtm_version);
		return;
	}
	os_memset(&event, 0, sizeof(event));
	switch (rtm->rtm_type) {
	case RTM_IEEE80211:
		if (n < (int)sizeof(*ifan))
			return;
		ifan = (struct if_announcemsghdr *) rtm;
		drv = bsd_get_drvindex(global, ifan->ifan_index);
		if (drv == NULL)
			return;
		switch (ifan->ifan_what) {
#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
		case RTM_IEEE80211_STA_RX:
		case RTM_IEEE80211_STA_TX:
		case RTM_IEEE80211_STA_SCAN:
			if (!drv->is_ap)
				bsd_sta_event(drv, ifan->ifan_what, &ifan[1], n - sizeof(*ifan));
			break;
#endif
#if defined(IEEE80211_IOC_SAE) && defined(CONFIG_SAE)
		case RTM_IEEE80211_SAE: {
			const struct ieee80211req_sae *req = (const void *)(ifan + 1);
			size_t hdr = sizeof(*ifan) + offsetof(struct ieee80211req_sae, data);

			if (drv->is_ap || !drv->sae_active || n < (int)hdr ||
			    req->version != IEEE80211_SAE_VERSION ||
			    req->generation != drv->sae_generation ||
			    req->len > IEEE80211_SAE_DATA_MAX || req->len > n - hdr ||
			    os_memcmp(req->bssid, drv->sae_bssid, ETH_ALEN) != 0)
				break;
			if (req->op == IEEE80211_SAE_START && req->len <= 32) {
				event.external_auth.action = EXT_AUTH_START;
				event.external_auth.bssid = req->bssid;
				event.external_auth.ssid = req->data;
				event.external_auth.ssid_len = req->len;
				event.external_auth.key_mgmt_suite = RSN_AUTH_KEY_MGMT_SAE;
				wpa_supplicant_event(drv->ctx, EVENT_EXTERNAL_AUTH, &event);
			} else if (req->op == IEEE80211_SAE_RX_FRAME && req->len >= 30) {
				event.rx_mgmt.frame = req->data;
				event.rx_mgmt.frame_len = req->len;
				wpa_supplicant_event(drv->ctx, EVENT_RX_MGMT, &event);
#ifdef IEEE80211_SAE_CAP_SA_QUERY
			} else if (drv->sae_query && req->op == IEEE80211_SAE_RX_SA_QUERY &&
				   req->len == 28 && req->data[0] == 0xd0 &&
				   req->data[24] == WLAN_ACTION_SA_QUERY && req->data[25] <= 1) {
				event.rx_mgmt.frame = req->data;
				event.rx_mgmt.frame_len = req->len;
				wpa_supplicant_event(drv->ctx, EVENT_RX_MGMT, &event);
			} else if (drv->sae_query && req->op == IEEE80211_SAE_UNPROT_DISCONNECT &&
				   req->len == 26 && req->data[0] == 0xc0) {
				event.unprot_deauth.da = req->data + 4;
				event.unprot_deauth.sa = req->data + 10;
				event.unprot_deauth.reason_code = WPA_GET_LE16(req->data + 24);
				wpa_supplicant_event(drv->ctx, EVENT_UNPROT_DEAUTH, &event);
			} else if (drv->sae_query && req->op == IEEE80211_SAE_UNPROT_DISCONNECT &&
				   req->len == 26 && req->data[0] == 0xa0) {
				event.unprot_disassoc.da = req->data + 4;
				event.unprot_disassoc.sa = req->data + 10;
				event.unprot_disassoc.reason_code = WPA_GET_LE16(req->data + 24);
				wpa_supplicant_event(drv->ctx, EVENT_UNPROT_DISASSOC, &event);
#endif
			}
			break;

		}
#endif
		case RTM_IEEE80211_ASSOC:
		case RTM_IEEE80211_REASSOC:
			if (drv->is_ap)
				break;
#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
			{
				struct ieee80211req_sta_mgmt info;

				if (bsd_sta_info(drv, &info) == 0 &&
				    (info.flags & IEEE80211_STA_ASSOCIATED)) {
					event.assoc_info.freq = info.freq;
					if ((info.flags & IEEE80211_STA_ASSOC_IES) &&
					    info.len >= ETH_ALEN + 4) {
						size_t req_len = WPA_GET_LE16(info.data + ETH_ALEN);
						size_t resp_len = WPA_GET_LE16(info.data + ETH_ALEN + 2);

						if (req_len + resp_len == info.len - ETH_ALEN - 4) {
							event.assoc_info.req_ies = info.data + ETH_ALEN + 4;
							event.assoc_info.req_ies_len = req_len;
							event.assoc_info.resp_ies = event.assoc_info.req_ies + req_len;
							event.assoc_info.resp_ies_len = resp_len;
						}
					}
				}
				/* The callback consumes these stack-backed IEs synchronously. */
				wpa_supplicant_event(drv->ctx, EVENT_ASSOC, &event);
			}
#else
			wpa_supplicant_event(drv->ctx, EVENT_ASSOC, &event);
#endif
			break;
		case RTM_IEEE80211_DISASSOC:
			if (drv->is_ap)
				break;
#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
			bsd_sta_disconnected(drv);
#else
			wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
#endif
			break;
		case RTM_IEEE80211_SCAN:
			if (drv->is_ap)
				break;
#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
			/* Connected scans use the versioned, cookie-bound event only. */
			if (drv->sta_scan_pending)
				break;
			os_get_reltime(&drv->sta_scan_completed);
#endif
			wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS,
					     NULL);
			break;
		case RTM_IEEE80211_LEAVE:
			leave = (struct ieee80211_leave_event *) &ifan[1];
			drv_event_disassoc(drv->ctx, leave->iev_addr);
			break;
		case RTM_IEEE80211_JOIN:
#ifdef RTM_IEEE80211_REJOIN
		case RTM_IEEE80211_REJOIN:
#endif
			join = (struct ieee80211_join_event *) &ifan[1];
			bsd_new_sta(drv, drv->ctx, join->iev_addr);
			break;
		case RTM_IEEE80211_REPLAY:
			/* ignore */
			break;
		case RTM_IEEE80211_MICHAEL:
			mic = (struct ieee80211_michael_event *) &ifan[1];
			wpa_printf(MSG_DEBUG,
				"Michael MIC failure wireless event: "
				"keyix=%u src_addr=" MACSTR, mic->iev_keyix,
				MAC2STR(mic->iev_src));
			os_memset(&event, 0, sizeof(event));
			event.michael_mic_failure.unicast =
				!IEEE80211_IS_MULTICAST(mic->iev_dst);
			event.michael_mic_failure.src = mic->iev_src;
			wpa_supplicant_event(drv->ctx,
					     EVENT_MICHAEL_MIC_FAILURE, &event);
			break;
		}
		break;
	case RTM_IFANNOUNCE:
		ifan = (struct if_announcemsghdr *) rtm;
		switch (ifan->ifan_what) {
		case IFAN_DEPARTURE:
			drv = bsd_get_drvindex(global, ifan->ifan_index);
			if (drv)
				drv->if_removed = 1;
			event.interface_status.ievent = EVENT_INTERFACE_REMOVED;
			break;
		case IFAN_ARRIVAL:
			drv = bsd_get_drvname(global, ifan->ifan_name);
			if (drv) {
				drv->ifindex = ifan->ifan_index;
				drv->if_removed = 0;
			}
			event.interface_status.ievent = EVENT_INTERFACE_ADDED;
			break;
		default:
			wpa_printf(MSG_DEBUG, "RTM_IFANNOUNCE: unknown action");
			return;
		}
		wpa_printf(MSG_DEBUG, "RTM_IFANNOUNCE: Interface '%s' %s",
			   ifan->ifan_name,
			   ifan->ifan_what == IFAN_DEPARTURE ?
				"removed" : "added");
		os_strlcpy(event.interface_status.ifname, ifan->ifan_name,
			   sizeof(event.interface_status.ifname));
		if (drv) {
			wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_STATUS,
					     &event);
			/*
			 * Set ifindex to zero after sending the event as the
			 * event might query the driver to ensure a match.
			 */
			if (ifan->ifan_what == IFAN_DEPARTURE)
				drv->ifindex = 0;
		} else {
			wpa_supplicant_event_global(global->ctx,
						    EVENT_INTERFACE_STATUS,
						    &event);
		}
		break;
	case RTM_IFINFO:
		ifm = (struct if_msghdr *) rtm;
		drv = bsd_get_drvindex(global, ifm->ifm_index);
		if (drv == NULL)
			return;
		if ((ifm->ifm_flags & IFF_UP) == 0 &&
		    (drv->flags & IFF_UP) != 0) {
			wpa_printf(MSG_DEBUG, "RTM_IFINFO: Interface '%s' DOWN",
				   drv->ifname);
#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
			bsd_sta_scan_clear(drv);
			bsd_sta_tx_status(drv, 0, 0);
#endif
			wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_DISABLED,
					     NULL);
		} else if ((ifm->ifm_flags & IFF_UP) != 0 &&
		    (drv->flags & IFF_UP) == 0) {
			wpa_printf(MSG_DEBUG, "RTM_IFINFO: Interface '%s' UP",
				   drv->ifname);
			wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_ENABLED,
					     NULL);
		}
		drv->flags = ifm->ifm_flags;
		break;
	}
}

#ifdef HOSTAPD

/*
 * Avoid conflicts with hostapd definitions by undefining couple of defines
 * from net80211 header files.
 */
#undef RSN_VERSION
#undef WPA_VERSION
#undef WPA_OUI_TYPE

static int bsd_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr,
			  u16 reason_code, int link_id);

static const char *
ether_sprintf(const u8 *addr)
{
	static char buf[sizeof(MACSTR)];

	if (addr != NULL)
		snprintf(buf, sizeof(buf), MACSTR, MAC2STR(addr));
	else
		snprintf(buf, sizeof(buf), MACSTR, 0,0,0,0,0,0);
	return buf;
}

static int
bsd_set_privacy(void *priv, int enabled)
{
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, enabled);

	return set80211param(priv, IEEE80211_IOC_PRIVACY, enabled);
}

static int
bsd_get_seqnum(const char *ifname, void *priv, const u8 *addr, int idx,
	       int link_id, u8 *seq)
{
	struct ieee80211req_key wk;

	wpa_printf(MSG_DEBUG, "%s: addr=%s idx=%d",
		   __func__, ether_sprintf(addr), idx);

	memset(&wk, 0, sizeof(wk));
	if (addr == NULL)
		memset(wk.ik_macaddr, 0xff, IEEE80211_ADDR_LEN);
	else
		memcpy(wk.ik_macaddr, addr, IEEE80211_ADDR_LEN);
	wk.ik_keyix = idx;

	if (get80211var(priv, IEEE80211_IOC_WPAKEY, &wk, sizeof(wk)) < 0) {
		wpa_printf(MSG_INFO, "Failed to get encryption");
		return -1;
	}

#ifdef WORDS_BIGENDIAN
	{
		/*
		 * wk.ik_keytsc is in host byte order (big endian), need to
		 * swap it to match with the byte order used in WPA.
		 */
		int i;
		u8 tmp[WPA_KEY_RSC_LEN];
		memcpy(tmp, &wk.ik_keytsc, sizeof(wk.ik_keytsc));
		for (i = 0; i < WPA_KEY_RSC_LEN; i++) {
			seq[i] = tmp[WPA_KEY_RSC_LEN - i - 1];
		}
	}
#else /* WORDS_BIGENDIAN */
	memcpy(seq, &wk.ik_keytsc, sizeof(wk.ik_keytsc));
#endif /* WORDS_BIGENDIAN */
	return 0;
}


static int
bsd_flush(void *priv, int link_id)
{
	u8 allsta[IEEE80211_ADDR_LEN];

	memset(allsta, 0xff, IEEE80211_ADDR_LEN);
	return bsd_sta_deauth(priv, NULL, allsta, IEEE80211_REASON_AUTH_LEAVE,
			      -1);
}


static int
bsd_read_sta_driver_data(void *priv, struct hostap_sta_driver_data *data,
			 const u8 *addr)
{
	struct ieee80211req_sta_stats stats;

	memcpy(stats.is_u.macaddr, addr, IEEE80211_ADDR_LEN);
	if (get80211var(priv, IEEE80211_IOC_STA_STATS, &stats, sizeof(stats))
	    > 0) {
		/* XXX? do packets counts include non-data frames? */
		data->rx_packets = stats.is_stats.ns_rx_data;
		data->rx_bytes = stats.is_stats.ns_rx_bytes;
		data->tx_packets = stats.is_stats.ns_tx_data;
		data->tx_bytes = stats.is_stats.ns_tx_bytes;
	}
	return 0;
}

static int
bsd_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr, u16 reason_code,
	       int link_id)
{
	return bsd_send_mlme_param(priv, IEEE80211_MLME_DEAUTH, reason_code,
				   addr);
}

static int
bsd_sta_disassoc(void *priv, const u8 *own_addr, const u8 *addr,
		 u16 reason_code)
{
	return bsd_send_mlme_param(priv, IEEE80211_MLME_DISASSOC, reason_code,
				   addr);
}

static void
handle_read(void *ctx, const u8 *src_addr, const u8 *buf, size_t len)
{
	struct bsd_driver_data *drv = ctx;
	drv_event_eapol_rx(drv->ctx, src_addr, buf, len);
}

static void *
bsd_init(struct hostapd_data *hapd, struct wpa_init_params *params)
{
	struct bsd_driver_data *drv;

	drv = os_zalloc(sizeof(struct bsd_driver_data));
	if (drv == NULL) {
		wpa_printf(MSG_ERROR, "Could not allocate memory for bsd driver data");
		return NULL;
	}

	drv->ifindex = if_nametoindex(params->ifname);
	if (drv->ifindex == 0) {
		wpa_printf(MSG_DEBUG, "%s: interface %s does not exist",
			   __func__, params->ifname);
		goto bad;
	}

	drv->ctx = hapd;
	drv->is_ap = 1;
	drv->global = params->global_priv;
	os_strlcpy(drv->ifname, params->ifname, sizeof(drv->ifname));

	drv->sock_xmit = l2_packet_init(drv->ifname, NULL, ETH_P_EAPOL,
					handle_read, drv, 0);
	if (drv->sock_xmit == NULL)
		goto bad;
	if (l2_packet_get_own_addr(drv->sock_xmit, params->own_addr))
		goto bad;

	if (bsd_get_iface_flags(drv) < 0)
		goto bad;

	if (bsd_set_mediaopt(drv, IFM_OMASK, IFM_IEEE80211_HOSTAP) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set operation mode",
			   __func__);
		goto bad;
	}

	dl_list_add(&drv->global->ifaces, &drv->list);

	return drv;
bad:
	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	os_free(drv);
	return NULL;
}


static void
bsd_deinit(void *priv)
{
	struct bsd_driver_data *drv = priv;

	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	os_free(drv);
}


static int
bsd_set_sta_authorized(void *priv, const u8 *addr,
		       unsigned int total_flags, unsigned int flags_or,
		       unsigned int flags_and)
{
	int authorized = -1;

	/* For now, only support setting Authorized flag */
	if (flags_or & WPA_STA_AUTHORIZED)
		authorized = 1;
	if (!(flags_and & WPA_STA_AUTHORIZED))
		authorized = 0;

	if (authorized < 0)
		return 0;

	return bsd_send_mlme_param(priv, authorized ?
				   IEEE80211_MLME_AUTHORIZE :
				   IEEE80211_MLME_UNAUTHORIZE, 0, addr);
}
#else /* HOSTAPD */

static int
get80211param(struct bsd_driver_data *drv, int op)
{
	struct ieee80211req ireq;

	if (bsd_get80211(drv, &ireq, op, NULL, 0) < 0)
		return -1;
	return ireq.i_val;
}

static int
wpa_driver_bsd_get_bssid(void *priv, u8 *bssid)
{
	struct bsd_driver_data *drv = priv;
#ifdef SIOCG80211BSSID
	struct ieee80211_bssid bs;

	os_strlcpy(bs.i_name, drv->ifname, sizeof(bs.i_name));
	if (ioctl(drv->global->sock, SIOCG80211BSSID, &bs) < 0)
		return -1;
	os_memcpy(bssid, bs.i_bssid, sizeof(bs.i_bssid));
	return 0;
#else
	return get80211var(drv, IEEE80211_IOC_BSSID,
		bssid, IEEE80211_ADDR_LEN) < 0 ? -1 : 0;
#endif
}

static int
wpa_driver_bsd_get_ssid(void *priv, u8 *ssid)
{
	struct bsd_driver_data *drv = priv;
	return bsd_get_ssid(drv, ssid, 0);
}

static int
wpa_driver_bsd_set_wpa_ie(struct bsd_driver_data *drv, const u8 *wpa_ie,
			  size_t wpa_ie_len)
{
#ifdef IEEE80211_IOC_APPIE
	return bsd_set_opt_ie(drv, wpa_ie, wpa_ie_len);
#else /* IEEE80211_IOC_APPIE */
	return set80211var(drv, IEEE80211_IOC_OPTIE, wpa_ie, wpa_ie_len);
#endif /* IEEE80211_IOC_APPIE */
}

static int
wpa_driver_bsd_set_wpa_internal(void *priv, int wpa, int privacy)
{
	int ret = 0;

	wpa_printf(MSG_DEBUG, "%s: wpa=%d privacy=%d",
		__func__, wpa, privacy);

	if (!wpa && wpa_driver_bsd_set_wpa_ie(priv, NULL, 0) < 0)
		ret = -1;
	if (set80211param(priv, IEEE80211_IOC_PRIVACY, privacy) < 0)
		ret = -1;
	if (set80211param(priv, IEEE80211_IOC_WPA, wpa) < 0)
		ret = -1;

	return ret;
}

static int
wpa_driver_bsd_set_wpa(void *priv, int enabled)
{
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, enabled);

	return wpa_driver_bsd_set_wpa_internal(priv, enabled ? 3 : 0, enabled);
}

static int
wpa_driver_bsd_set_countermeasures(void *priv, int enabled)
{
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, enabled);
	return set80211param(priv, IEEE80211_IOC_COUNTERMEASURES, enabled);
}


static int
wpa_driver_bsd_set_drop_unencrypted(void *priv, int enabled)
{
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, enabled);
	return set80211param(priv, IEEE80211_IOC_DROPUNENCRYPTED, enabled);
}

static int
wpa_driver_bsd_deauthenticate(void *priv, const u8 *addr, u16 reason_code)
{
	return bsd_send_mlme_param(priv, IEEE80211_MLME_DEAUTH, reason_code,
				   addr);
}

static int
wpa_driver_bsd_set_auth_alg(void *priv, int auth_alg)
{
	int authmode;

	if ((auth_alg & WPA_AUTH_ALG_OPEN) &&
	    (auth_alg & WPA_AUTH_ALG_SHARED))
		authmode = IEEE80211_AUTH_AUTO;
	else if (auth_alg & WPA_AUTH_ALG_SHARED)
		authmode = IEEE80211_AUTH_SHARED;
	else
		authmode = IEEE80211_AUTH_OPEN;

	return set80211param(priv, IEEE80211_IOC_AUTHMODE, authmode);
}

static void
handle_read(void *ctx, const u8 *src_addr, const u8 *buf, size_t len)
{
	struct bsd_driver_data *drv = ctx;

	drv_event_eapol_rx(drv->ctx, src_addr, buf, len);
}

static int
wpa_driver_bsd_associate(void *priv, struct wpa_driver_associate_params *params)
{
	struct bsd_driver_data *drv = priv;
	struct ieee80211req_mlme mlme;
	u32 mode;
	int privacy;
	int ret = 0;

	wpa_printf(MSG_DEBUG,
		"%s: ssid '%.*s' wpa ie len %u pairwise %u group %u key mgmt %u"
		, __func__
		   , (unsigned int) params->ssid_len, params->ssid
		, (unsigned int) params->wpa_ie_len
		, params->pairwise_suite
		, params->group_suite
		, params->key_mgmt_suite
	);

	switch (params->mode) {
	case IEEE80211_MODE_INFRA:
		mode = 0 /* STA */;
		break;
	case IEEE80211_MODE_IBSS:
		mode = IFM_IEEE80211_IBSS;
		break;
	case IEEE80211_MODE_AP:
		mode = IFM_IEEE80211_HOSTAP;
		break;
	default:
		wpa_printf(MSG_ERROR, "%s: unknown operation mode", __func__);
		return -1;
	}
	if (bsd_set_mediaopt(drv, IFM_OMASK, mode) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set operation mode",
			   __func__);
		return -1;
	}

	if (params->mode == IEEE80211_MODE_AP) {
		drv->sock_xmit = l2_packet_init(drv->ifname, NULL, ETH_P_EAPOL,
						handle_read, drv, 0);
		if (drv->sock_xmit == NULL)
			return -1;
		drv->is_ap = 1;
		return 0;
	}

	if (wpa_driver_bsd_set_drop_unencrypted(drv, params->drop_unencrypted)
	    < 0)
		ret = -1;
	if (wpa_driver_bsd_set_auth_alg(drv, params->auth_alg) < 0)
		ret = -1;
	/* XXX error handling is wrong but unclear what to do... */
	if (wpa_driver_bsd_set_wpa_ie(drv, params->wpa_ie, params->wpa_ie_len) < 0)
		return -1;

	privacy = !(params->pairwise_suite == WPA_CIPHER_NONE &&
	    params->group_suite == WPA_CIPHER_NONE &&
	    params->key_mgmt_suite == WPA_KEY_MGMT_NONE &&
	    params->wpa_ie_len == 0);
	wpa_printf(MSG_DEBUG, "%s: set PRIVACY %u", __func__, privacy);

	if (set80211param(drv, IEEE80211_IOC_PRIVACY, privacy) < 0)
		return -1;

	if (params->wpa_ie_len &&
	    set80211param(drv, IEEE80211_IOC_WPA,
			  params->wpa_ie[0] == WLAN_EID_RSN ? 2 : 1) < 0)
		return -1;

#if defined(IEEE80211_IOC_SAE) && defined(CONFIG_SAE)
	if (drv->capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_SAE) {
		u8 enabled = params->key_mgmt_suite == WPA_KEY_MGMT_SAE;

		if (enabled && (params->mgmt_frame_protection != MGMT_FRAME_PROTECTION_REQUIRED ||
		    params->pairwise_suite != WPA_CIPHER_CCMP ||
		    params->group_suite != WPA_CIPHER_CCMP || !params->bssid))
			return -1;
		if (++drv->sae_generation == 0)
			drv->sae_generation++;
		os_memset(drv->sae_bssid, 0, ETH_ALEN);
		if (params->bssid)
			os_memcpy(drv->sae_bssid, params->bssid, ETH_ALEN);
		drv->sae_active = enabled;
		if (bsd_sae_request(drv, IEEE80211_SAE_CONFIGURE, &enabled, 1) < 0) {
			drv->sae_active = 0;
			return -1;
		}
	}
#endif
	if (ret)
		return ret;
#if defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
	if (bsd_sta_associate(drv, params) < 0)
		return -1;
#endif
	os_memset(&mlme, 0, sizeof(mlme));
	mlme.im_op = IEEE80211_MLME_ASSOC;
	if (params->ssid != NULL)
		os_memcpy(mlme.im_ssid, params->ssid, params->ssid_len);
	mlme.im_ssid_len = params->ssid_len;
	if (params->bssid != NULL)
		os_memcpy(mlme.im_macaddr, params->bssid, IEEE80211_ADDR_LEN);
	if (set80211var(drv, IEEE80211_IOC_MLME, &mlme, sizeof(mlme)) < 0)
		return -1;
	return ret;
}

static int
wpa_driver_bsd_scan(void *priv, struct wpa_driver_scan_params *params)
{
	struct bsd_driver_data *drv = priv;
#ifdef IEEE80211_IOC_SCAN_MAX_SSID
	struct ieee80211_scan_req sr;
	int i;
#endif /* IEEE80211_IOC_SCAN_MAX_SSID */

#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
	if (drv->sta_btm || drv->sta_rrm) {
		struct ieee80211req_sta_mgmt req;
		if (bsd_sta_info(drv, &req) == 0 &&
		    (req.flags & IEEE80211_STA_ASSOCIATED)) {
			size_t nfreq = 0;
			req.len = 0;
			if (params->freqs) {
				while (params->freqs[nfreq]) {
					u16 freq;
					if (nfreq == IEEE80211_STA_MAX_CHANNELS ||
					    params->freqs[nfreq] < 0 || params->freqs[nfreq] > 65535)
						return -1;
					freq = params->freqs[nfreq];
					os_memcpy(req.data + nfreq * 2, &freq, 2);
					nfreq++;
				}
				req.len = nfreq * 2;
			}
			if (drv->sta_scan_pending)
				return -1;
			if (++drv->sta_scan_cookie == 0)
				drv->sta_scan_cookie++;
			req.cookie = drv->sta_scan_cookie;
			if (bsd_sta_set(drv, &req, IEEE80211_STA_SCAN) < 0)
				return -1;
			bsd_sta_scan_clear(drv);
			drv->sta_scan_pending = 1;
			drv->sta_scan_epoch = req.generation;
			os_memcpy(drv->sta_scan_bssid, req.bssid, ETH_ALEN);
			if (eloop_register_timeout(15, 0, bsd_sta_scan_timeout, drv,
			    (void *) drv->sta_scan_generation) < 0) {
				req.len = 0;
				bsd_sta_set(drv, &req, IEEE80211_STA_CANCEL_SCAN);
				bsd_sta_scan_clear(drv);
				return -1;
			}
			return 0;
		}
	}
#endif

	if (bsd_set_mediaopt(drv, IFM_OMASK, 0 /* STA */) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set operation mode",
			   __func__);
		return -1;
	}

	if (set80211param(drv, IEEE80211_IOC_ROAMING,
			  IEEE80211_ROAMING_MANUAL) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set "
			   "wpa_supplicant-based roaming: %s", __func__,
			   strerror(errno));
		return -1;
	}

	if (wpa_driver_bsd_set_wpa(drv, 1) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set wpa: %s", __func__,
			   strerror(errno));
		return -1;
	}

	/* NB: interface must be marked UP to do a scan */
	if (!(drv->flags & IFF_UP)) {
		wpa_printf(MSG_DEBUG, "%s: interface is not up, cannot scan",
			   __func__);
		return -1;
	}

#ifdef IEEE80211_IOC_SCAN_MAX_SSID
	os_memset(&sr, 0, sizeof(sr));
	sr.sr_flags = IEEE80211_IOC_SCAN_ACTIVE | IEEE80211_IOC_SCAN_ONCE |
		IEEE80211_IOC_SCAN_NOJOIN;
	sr.sr_duration = IEEE80211_IOC_SCAN_FOREVER;
	if (params->num_ssids > 0) {
		sr.sr_nssid = params->num_ssids;
#if 0
		/* Boundary check is done by upper layer */
		if (sr.sr_nssid > IEEE80211_IOC_SCAN_MAX_SSID)
			sr.sr_nssid = IEEE80211_IOC_SCAN_MAX_SSID;
#endif

		/* NB: check scan cache first */
		sr.sr_flags |= IEEE80211_IOC_SCAN_CHECK;
	}
	for (i = 0; i < sr.sr_nssid; i++) {
		sr.sr_ssid[i].len = params->ssids[i].ssid_len;
		os_memcpy(sr.sr_ssid[i].ssid, params->ssids[i].ssid,
			  sr.sr_ssid[i].len);
	}

	/* NB: net80211 delivers a scan complete event so no need to poll */
	return set80211var(drv, IEEE80211_IOC_SCAN_REQ, &sr, sizeof(sr));
#else /* IEEE80211_IOC_SCAN_MAX_SSID */
	/* set desired ssid before scan */
	if (params->num_ssids > 0 && bsd_set_ssid(drv, params->ssids[0].ssid,
			 params->ssids[0].ssid_len) < 0)
		return -1;

	/* NB: net80211 delivers a scan complete event so no need to poll */
	return set80211param(drv, IEEE80211_IOC_SCAN_REQ, 0);
#endif /* IEEE80211_IOC_SCAN_MAX_SSID */
}

static void
wpa_driver_bsd_add_scan_entry(struct bsd_driver_data *drv, struct wpa_scan_results *res,
			      struct ieee80211req_scan_result *sr)
{
	struct wpa_scan_res *result, **tmp;
	size_t extra_len;
	u8 *pos;

	extra_len = 2 + sr->isr_ssid_len;
	extra_len += 2 + sr->isr_nrates;
	extra_len += 3; /* ERP IE */
	extra_len += sr->isr_ie_len;

	result = os_zalloc(sizeof(*result) + extra_len);
	if (result == NULL)
		return;
	os_memcpy(result->bssid, sr->isr_bssid, ETH_ALEN);
	result->freq = sr->isr_freq;
	result->beacon_int = sr->isr_intval;
	result->caps = sr->isr_capinfo;
#ifdef IEEE80211_SCAN_CAPINFO_HI
	if (sr->isr_pad[1] & IEEE80211_SCAN_CAPINFO_HI)
		result->caps |= (u16) sr->isr_pad[0] << 8;
#endif
	result->qual = sr->isr_rssi;
	result->noise = sr->isr_noise;

#ifdef __FreeBSD__
	/*
	 * the rssi value reported by the kernel is in 0.5dB steps relative to
	 * the reported noise floor. see ieee80211_node.h for details.
	 */
	result->level = sr->isr_rssi / 2 + sr->isr_noise;
#elif defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
	if (drv->sta_btm || drv->sta_rrm) {
		struct os_reltime now, elapsed;
		result->level = sr->isr_rssi;
		os_get_reltime(&now);
		os_reltime_sub(&now, &drv->sta_scan_completed, &elapsed);
		result->age = elapsed.sec >= 600 ? 600000 :
		    elapsed.sec * 1000 + elapsed.usec / 1000;
	} else {
		result->level = sr->isr_rssi;
	}
#else
	result->level = sr->isr_rssi;
#endif

#if defined(__NetBSD__) && defined(IEEE80211_SCAN_RSSI_DBM)
	if (sr->isr_pad[1] & IEEE80211_SCAN_RSSI_DBM) {
		result->level = (int8_t) sr->isr_pad[2];
		result->flags |= WPA_SCAN_LEVEL_DBM | WPA_SCAN_NOISE_INVALID;
	}
#endif

	pos = (u8 *)(result + 1);

	*pos++ = WLAN_EID_SSID;
	*pos++ = sr->isr_ssid_len;
	os_memcpy(pos, sr + 1, sr->isr_ssid_len);
	pos += sr->isr_ssid_len;

	/*
	 * Deal all rates as supported rate.
	 * Because net80211 doesn't report extended supported rate or not.
	 */
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = sr->isr_nrates;
	os_memcpy(pos, sr->isr_rates, sr->isr_nrates);
	pos += sr->isr_nrates;

	*pos++ = WLAN_EID_ERP_INFO;
	*pos++ = 1;
	*pos++ = sr->isr_erp;

#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
	os_memcpy(pos, (u8 *)(sr + 1) + sr->isr_ssid_len + sr->isr_meshid_len,
		  sr->isr_ie_len);
#else
	os_memcpy(pos, (u8 *)(sr + 1) + sr->isr_ssid_len, sr->isr_ie_len);
#endif
	pos += sr->isr_ie_len;

	result->ie_len = pos - (u8 *)(result + 1);

	tmp = os_realloc_array(res->res, res->num + 1,
			       sizeof(struct wpa_scan_res *));
	if (tmp == NULL) {
		os_free(result);
		return;
	}
	tmp[res->num++] = result;
	res->res = tmp;
}

struct wpa_scan_results *
wpa_driver_bsd_get_scan_results2(void *priv)
{
	struct ieee80211req_scan_result *sr;
	struct wpa_scan_results *res;
	int len, rest;
	uint8_t buf[24*1024], *pos;

	len = get80211var(priv, IEEE80211_IOC_SCAN_RESULTS, buf, 24*1024);
	if (len < 0)
		return NULL;

	res = os_zalloc(sizeof(*res));
	if (res == NULL)
		return NULL;

	pos = buf;
	rest = len;
	while (rest >= sizeof(struct ieee80211req_scan_result)) {
		sr = (struct ieee80211req_scan_result *)pos;
		if (sr->isr_len < sizeof(*sr) || sr->isr_len > rest ||
		    sr->isr_nrates > sizeof(sr->isr_rates) ||
		    sr->isr_ssid_len > 32 ||
		    sizeof(*sr) + sr->isr_ssid_len + sr->isr_ie_len > sr->isr_len)
			break;
		wpa_driver_bsd_add_scan_entry(priv, res, sr);
		pos += sr->isr_len;
		rest -= sr->isr_len;
	}

	wpa_printf(MSG_DEBUG, "Received %d bytes of scan results (%lu BSSes)",
		   len, (unsigned long)res->num);

	return res;
}

static int wpa_driver_bsd_capa(struct bsd_driver_data *drv)
{
#ifdef IEEE80211_IOC_DEVCAPS
/* kernel definitions copied from net80211/ieee80211_var.h */
#define IEEE80211_CIPHER_WEP            0
#define IEEE80211_CIPHER_TKIP           1
#define IEEE80211_CIPHER_AES_CCM        3
#define IEEE80211_CRYPTO_WEP            (1<<IEEE80211_CIPHER_WEP)
#define IEEE80211_CRYPTO_TKIP           (1<<IEEE80211_CIPHER_TKIP)
#define IEEE80211_CRYPTO_AES_CCM        (1<<IEEE80211_CIPHER_AES_CCM)
#define IEEE80211_C_HOSTAP      0x00000400      /* CAPABILITY: HOSTAP avail */
#define IEEE80211_C_WPA1        0x00800000      /* CAPABILITY: WPA1 avail */
#define IEEE80211_C_WPA2        0x01000000      /* CAPABILITY: WPA2 avail */
	struct ieee80211_devcaps_req devcaps;

	if (get80211var(drv, IEEE80211_IOC_DEVCAPS, &devcaps,
			sizeof(devcaps)) < 0) {
		wpa_printf(MSG_ERROR, "failed to IEEE80211_IOC_DEVCAPS: %s",
			   strerror(errno));
		return -1;
	}

	wpa_printf(MSG_DEBUG, "%s: drivercaps=0x%08x,cryptocaps=0x%08x",
		   __func__, devcaps.dc_drivercaps, devcaps.dc_cryptocaps);

	if (devcaps.dc_drivercaps & IEEE80211_C_WPA1)
		drv->capa.key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA |
			WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK;
	if (devcaps.dc_drivercaps & IEEE80211_C_WPA2)
		drv->capa.key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
			WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK;

	if (devcaps.dc_cryptocaps & IEEE80211_CRYPTO_WEP)
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_WEP40 |
			WPA_DRIVER_CAPA_ENC_WEP104;
	if (devcaps.dc_cryptocaps & IEEE80211_CRYPTO_TKIP)
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_TKIP;
	if (devcaps.dc_cryptocaps & IEEE80211_CRYPTO_AES_CCM)
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_CCMP;

	if (devcaps.dc_drivercaps & IEEE80211_C_HOSTAP)
		drv->capa.flags |= WPA_DRIVER_FLAGS_AP;
#undef IEEE80211_CIPHER_WEP
#undef IEEE80211_CIPHER_TKIP
#undef IEEE80211_CIPHER_AES_CCM
#undef IEEE80211_CRYPTO_WEP
#undef IEEE80211_CRYPTO_TKIP
#undef IEEE80211_CRYPTO_AES_CCM
#undef IEEE80211_C_HOSTAP
#undef IEEE80211_C_WPA1
#undef IEEE80211_C_WPA2
#else /* IEEE80211_IOC_DEVCAPS */
	/* For now, assume TKIP, CCMP, WPA, WPA2 are supported */
	drv->capa.key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK;
	drv->capa.enc = WPA_DRIVER_CAPA_ENC_WEP40 |
		WPA_DRIVER_CAPA_ENC_WEP104 |
		WPA_DRIVER_CAPA_ENC_TKIP |
		WPA_DRIVER_CAPA_ENC_CCMP;
	drv->capa.flags |= WPA_DRIVER_FLAGS_AP;
#endif /* IEEE80211_IOC_DEVCAPS */
#ifdef IEEE80211_IOC_SCAN_MAX_SSID
	drv->capa.max_scan_ssids = IEEE80211_IOC_SCAN_MAX_SSID;
#else /* IEEE80211_IOC_SCAN_MAX_SSID */
	drv->capa.max_scan_ssids = 1;
#endif /* IEEE80211_IOC_SCAN_MAX_SSID */
#if defined(IEEE80211_IOC_SAE) && defined(CONFIG_SAE)
	{
		struct ieee80211req req;
		os_memset(&req, 0, sizeof(req));
		os_strlcpy(req.i_name, drv->ifname, sizeof(req.i_name));
		req.i_type = IEEE80211_IOC_SAE;
		if (ioctl(drv->global->sock, SIOCG80211, &req) == 0 &&
		    (req.i_val & (IEEE80211_SAE_CAP_EXTERNAL | IEEE80211_SAE_CAP_PMF)) ==
		    (IEEE80211_SAE_CAP_EXTERNAL | IEEE80211_SAE_CAP_PMF)) {
			drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_SAE;
			drv->capa.enc |= WPA_DRIVER_CAPA_ENC_BIP;
			drv->capa.flags |= WPA_DRIVER_FLAGS_SAE;
			drv->capa.flags2 |= WPA_DRIVER_FLAGS2_SAE_NO_PMKSA;
#ifdef IEEE80211_SAE_CAP_SA_QUERY
			drv->sae_query = !!(req.i_val & IEEE80211_SAE_CAP_SA_QUERY);
#endif
		}
	}
#endif
	drv->capa.auth = WPA_DRIVER_AUTH_OPEN |
		WPA_DRIVER_AUTH_SHARED |
		WPA_DRIVER_AUTH_LEAP;
	return 0;
}

static enum ieee80211_opmode
get80211opmode(struct bsd_driver_data *drv)
{
	struct ifmediareq ifmr;

	(void) memset(&ifmr, 0, sizeof(ifmr));
	(void) os_strlcpy(ifmr.ifm_name, drv->ifname, sizeof(ifmr.ifm_name));

	if (ioctl(drv->global->sock, SIOCGIFMEDIA, (caddr_t)&ifmr) >= 0) {
		if (ifmr.ifm_current & IFM_IEEE80211_ADHOC) {
			if (ifmr.ifm_current & IFM_FLAG0)
				return IEEE80211_M_AHDEMO;
			else
				return IEEE80211_M_IBSS;
		}
		if (ifmr.ifm_current & IFM_IEEE80211_HOSTAP)
			return IEEE80211_M_HOSTAP;
		if (ifmr.ifm_current & IFM_IEEE80211_MONITOR)
			return IEEE80211_M_MONITOR;
#ifdef IEEE80211_M_MBSS
		if (ifmr.ifm_current & IFM_IEEE80211_MBSS)
			return IEEE80211_M_MBSS;
#endif /* IEEE80211_M_MBSS */
	}
	return IEEE80211_M_STA;
}

static void *
wpa_driver_bsd_init(void *ctx, const char *ifname, void *priv)
{
#define	GETPARAM(drv, param, v) \
	(((v) = get80211param(drv, param)) != -1)
	struct bsd_driver_data *drv;
	int i;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;

	drv->ifindex = if_nametoindex(ifname);
	if (drv->ifindex == 0) {
		wpa_printf(MSG_DEBUG, "%s: interface %s does not exist",
			   __func__, ifname);
		goto fail;
	}

	drv->ctx = ctx;
	drv->global = priv;
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));

	/* Set the interface as removed until proven to work. */
	drv->if_removed = 1;

	if (!GETPARAM(drv, IEEE80211_IOC_ROAMING, drv->prev_roaming)) {
		wpa_printf(MSG_DEBUG, "%s: failed to get roaming state: %s",
			__func__, strerror(errno));
		goto fail;
	}
	if (!GETPARAM(drv, IEEE80211_IOC_PRIVACY, drv->prev_privacy)) {
		wpa_printf(MSG_DEBUG, "%s: failed to get privacy state: %s",
			__func__, strerror(errno));
		goto fail;
	}
	if (!GETPARAM(drv, IEEE80211_IOC_WPA, drv->prev_wpa)) {
		wpa_printf(MSG_DEBUG, "%s: failed to get wpa state: %s",
			__func__, strerror(errno));
		goto fail;
	}

	if (wpa_driver_bsd_capa(drv))
		goto fail;

	/* Update per interface supported AKMs */
	for (i = 0; i < WPA_IF_MAX; i++)
		drv->capa.key_mgmt_iftype[i] = drv->capa.key_mgmt;

	/* Down interface during setup. */
	if (bsd_get_iface_flags(drv) < 0)
		goto fail;

	/* Proven to work, lets go! */
	drv->if_removed = 0;

	drv->opmode = get80211opmode(drv);
#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
	bsd_sta_init(drv);
#endif
	dl_list_add(&drv->global->ifaces, &drv->list);

	return drv;
fail:
	os_free(drv);
	return NULL;
#undef GETPARAM
}

static void
wpa_driver_bsd_deinit(void *priv)
{
	struct bsd_driver_data *drv = priv;

#if !defined(HOSTAPD) && defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
	bsd_sta_scan_clear(drv);
	bsd_sta_tx_status(drv, 0, 0);
	if ((drv->sta_btm || drv->sta_rrm) && drv->ifindex != 0 && !drv->if_removed) {
		struct ieee80211req_sta_mgmt req;
		os_memset(&req, 0, sizeof(req));
		bsd_sta_set(drv, &req, IEEE80211_STA_DISABLE);
	}
#endif

	if (drv->ifindex != 0 && !drv->if_removed) {
		wpa_driver_bsd_set_wpa(drv, 0);

		wpa_driver_bsd_set_wpa_internal(drv, drv->prev_wpa,
						drv->prev_privacy);

		if (set80211param(drv, IEEE80211_IOC_ROAMING, drv->prev_roaming)
		    < 0)
			wpa_printf(MSG_DEBUG,
				   "%s: failed to restore roaming state",
				   __func__);
	}

	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	dl_list_del(&drv->list);
	os_free(drv);
}

static int
wpa_driver_bsd_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	struct bsd_driver_data *drv = priv;

	os_memcpy(capa, &drv->capa, sizeof(*capa));
	return 0;
}
#endif /* HOSTAPD */

static void *
bsd_global_init(void *ctx)
{
	struct bsd_driver_global *global;
#if defined(RO_MSGFILTER) || defined(ROUTE_MSGFILTER)
	unsigned char msgfilter[] = {
		RTM_IEEE80211,
		RTM_IFINFO, RTM_IFANNOUNCE,
	};
#endif
#ifdef ROUTE_MSGFILTER
	unsigned int i, msgfilter_mask;
#endif

	global = os_zalloc(sizeof(*global));
	if (global == NULL)
		return NULL;

	global->ctx = ctx;
	dl_list_init(&global->ifaces);

	global->sock = socket(PF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (global->sock < 0) {
		wpa_printf(MSG_ERROR, "socket[PF_INET,SOCK_DGRAM]: %s",
			   strerror(errno));
		goto fail1;
	}

	global->route = socket(PF_ROUTE,
			       SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (global->route < 0) {
		wpa_printf(MSG_ERROR, "socket[PF_ROUTE,SOCK_RAW]: %s",
			   strerror(errno));
		goto fail;
	}

#if defined(RO_MSGFILTER)
	if (setsockopt(global->route, PF_ROUTE, RO_MSGFILTER,
	    &msgfilter, sizeof(msgfilter)) < 0)
		wpa_printf(MSG_ERROR, "socket[PF_ROUTE,RO_MSGFILTER]: %s",
			   strerror(errno));
#elif defined(ROUTE_MSGFILTER)
	msgfilter_mask = 0;
	for (i = 0; i < (sizeof(msgfilter) / sizeof(msgfilter[0])); i++)
		msgfilter_mask |= ROUTE_FILTER(msgfilter[i]);
	if (setsockopt(global->route, PF_ROUTE, ROUTE_MSGFILTER,
	    &msgfilter_mask, sizeof(msgfilter_mask)) < 0)
		wpa_printf(MSG_ERROR, "socket[PF_ROUTE,ROUTE_MSGFILTER]: %s",
			   strerror(errno));
#endif

	eloop_register_read_sock(global->route, bsd_wireless_event_receive,
				 NULL, global);

	return global;

fail:
	close(global->sock);
fail1:
	os_free(global);
	return NULL;
}

static void
bsd_global_deinit(void *priv)
{
	struct bsd_driver_global *global = priv;

	eloop_unregister_read_sock(global->route);
	(void) close(global->route);
	(void) close(global->sock);
	os_free(global);
}


const struct wpa_driver_ops wpa_driver_bsd_ops = {
	.name			= "bsd",
	.desc			= "BSD 802.11 support",
	.global_init		= bsd_global_init,
	.global_deinit		= bsd_global_deinit,
#ifdef HOSTAPD
	.hapd_init		= bsd_init,
	.hapd_deinit		= bsd_deinit,
	.set_privacy		= bsd_set_privacy,
	.get_seqnum		= bsd_get_seqnum,
	.flush			= bsd_flush,
	.read_sta_data		= bsd_read_sta_driver_data,
	.sta_disassoc		= bsd_sta_disassoc,
	.sta_deauth		= bsd_sta_deauth,
	.sta_set_flags		= bsd_set_sta_authorized,
#else /* HOSTAPD */
	.init2			= wpa_driver_bsd_init,
	.deinit			= wpa_driver_bsd_deinit,
	.get_bssid		= wpa_driver_bsd_get_bssid,
	.get_ssid		= wpa_driver_bsd_get_ssid,
	.set_countermeasures	= wpa_driver_bsd_set_countermeasures,
	.scan2			= wpa_driver_bsd_scan,
	.get_scan_results2	= wpa_driver_bsd_get_scan_results2,
	.deauthenticate		= wpa_driver_bsd_deauthenticate,
	.associate		= wpa_driver_bsd_associate,
	.get_capa		= wpa_driver_bsd_get_capa,
#if defined(__NetBSD__) && defined(IEEE80211_IOC_STA_MGMT)
	.send_action = bsd_send_action,
	.signal_poll = bsd_sta_signal_poll,
#endif
#if defined(IEEE80211_IOC_SAE) && defined(CONFIG_SAE)
	.send_mlme = bsd_sae_send_mlme,
#if !defined(__NetBSD__) || !defined(IEEE80211_IOC_STA_MGMT)
#ifdef IEEE80211_SAE_CAP_SA_QUERY
	.send_action = bsd_sae_send_action,
#endif
#endif
	.send_external_auth_status = bsd_sae_auth_status,
#endif
#endif /* HOSTAPD */
	.set_freq		= bsd_set_freq,
	.set_key		= bsd_set_key,
	.set_ieee8021x		= bsd_set_ieee8021x,
	.hapd_set_ssid		= bsd_set_ssid,
	.hapd_get_ssid		= bsd_get_ssid,
	.hapd_send_eapol	= bsd_send_eapol,
	.set_generic_elem	= bsd_set_opt_ie,
};
