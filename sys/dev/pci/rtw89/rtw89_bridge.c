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
 * A minimal mac80211 for rtw89: one station interface and one peer (the
 * AP).  net80211 on the NetBSD side does scanning, authentication,
 * association and software crypto; this file translates its decisions
 * into the mac80211 driver operations rtw89 implements.
 *
 * The rtw89_nb_* entry points must be called from a thread context that
 * may sleep, except rtw89_nb_tx(), which may be called from a softint.
 */

#include "rtw89_compat.h"

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include "core.h"
#include "debug.h"
#include "mac.h"
#include "txrx.h"
#include "rtw89_nb.h"

int rtw89_8852be_probe(struct pci_dev *, u16);
void rtw89_pci_remove(struct pci_dev *);

struct rtw89_nb {
	struct ieee80211_hw	*hw;
	struct wiphy		 wiphy;
	struct pci_dev		 pdev;
	void			*glue;
	struct workqueue_struct	*wq;
	struct ieee80211_vif	*vif;
	struct ieee80211_sta	*sta;
	enum ieee80211_sta_state sta_state;
	struct ieee80211_channel chan;
	unsigned int		 filter;
	uint32_t		 quiet_tx_en;
	/* Operating channel saved for a connected-scan listen period. */
	struct ieee80211_channel oper_chan;
	struct cfg80211_chan_def oper_def;
	bool			 offchan;
	bool			 started;
	bool			 vif_added;
	bool			 tx_quiet;
	/* EDCA from net80211, applied by wme_work in thread context. */
	struct work_struct	 wme_work;
	spinlock_t		 wme_lock;
	struct rtw89_nb_edca	 wme[RTW89_NB_NAC];
	bool			 wme_pending;
};

static struct rtw89_nb *rtw89_nb_attaching;

static void rtw89_nb_wme_work(struct work_struct *);
static void rtw89_nb_fill_he(struct rtw89_nb *, const struct rtw89_nb_ht *);

static inline struct rtw89_nb *
hw_to_nb(struct ieee80211_hw *hw)
{

	return hw->bridge;
}

static inline struct rtw89_dev *
nb_to_rtwdev(struct rtw89_nb *nb)
{

	return nb->hw->priv;
}

/* ------------------------------------------------------------------ */
/* Channels                                                            */
/* ------------------------------------------------------------------ */

int
ieee80211_channel_to_frequency(int chan, enum nl80211_band band)
{

	switch (band) {
	case NL80211_BAND_2GHZ:
		if (chan == 14)
			return 2484;
		return chan < 14 ? 2407 + chan * 5 : 0;
	case NL80211_BAND_5GHZ:
		return 5000 + chan * 5;
	case NL80211_BAND_6GHZ:
		return chan == 2 ? 5935 : 5950 + chan * 5;
	default:
		return 0;
	}
}

int
ieee80211_frequency_to_channel(int freq)
{

	if (freq == 2484)
		return 14;
	if (freq < 2484)
		return (freq - 2407) / 5;
	if (freq >= 5955 && freq <= 7115)
		return (freq - 5950) / 5;
	if (freq >= 4910 && freq <= 5885)
		return (freq - 5000) / 5;
	return 0;
}

void
cfg80211_chandef_create(struct cfg80211_chan_def *chandef,
    struct ieee80211_channel *chan, enum nl80211_channel_type type)
{

	memset(chandef, 0, sizeof(*chandef));
	chandef->chan = chan;
	chandef->center_freq1 = chan->center_freq;
	chandef->width = type == NL80211_CHAN_NO_HT ?
	    NL80211_CHAN_WIDTH_20_NOHT : NL80211_CHAN_WIDTH_20;
}

const u8 *
cfg80211_find_ie(u8 eid, const u8 *ies, unsigned int len)
{
	const struct element *e = cfg80211_find_elem(eid, ies, len);

	return (const u8 *)e;
}

const struct element *
cfg80211_find_elem(u8 eid, const u8 *ies, unsigned int len)
{

	while (len >= 2 && len >= 2u + ies[1]) {
		if (ies[0] == eid)
			return (const struct element *)ies;
		len -= 2 + ies[1];
		ies += 2 + ies[1];
	}
	return NULL;
}

const u8 *
cfg80211_find_vendor_ie(unsigned int oui, int oui_type, const u8 *ies,
    unsigned int len)
{
	const u8 *e;

	while ((e = cfg80211_find_ie(WLAN_EID_VENDOR_SPECIFIC, ies, len))) {
		if (e[1] >= 4 && (((unsigned)e[2] << 16) | (e[3] << 8) |
		    e[4]) == oui && (oui_type < 0 || e[5] == oui_type))
			return e;
		len -= (e - ies) + 2 + e[1];
		ies = e + 2 + e[1];
	}
	return NULL;
}

u32
cfg80211_calculate_bitrate(struct rate_info *rate)
{

	if (rate->flags == 0)
		return rate->legacy;
	return 0;
}

/* ------------------------------------------------------------------ */
/* mac80211 services used by rtw89                                     */
/* ------------------------------------------------------------------ */

struct ieee80211_hw *
ieee80211_alloc_hw(size_t priv_size, const struct ieee80211_ops *ops)
{
	struct rtw89_nb *nb = rtw89_nb_attaching;
	struct ieee80211_hw *hw;

	KASSERT(nb != NULL);
	hw = kzalloc(ALIGN(sizeof(*hw), 32) + priv_size, GFP_KERNEL);
	if (hw == NULL)
		return NULL;
	hw->priv = (char *)hw + ALIGN(sizeof(*hw), 32);
	hw->wiphy = &nb->wiphy;
	hw->ops = ops;
	hw->bridge = nb;
	nb->wiphy.priv = hw;
	nb->hw = hw;
	/* default channel: 2.4GHz channel 1 */
	nb->chan.band = NL80211_BAND_2GHZ;
	nb->chan.center_freq = 2412;
	nb->chan.hw_value = 1;
	cfg80211_chandef_create(&hw->conf.chandef, &nb->chan,
	    NL80211_CHAN_NO_HT);
	return hw;
}

void
ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct rtw89_nb *nb = hw_to_nb(hw);

	nb->hw = NULL;
	kfree(hw);
}

int
ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct regulatory_request req = {
		.alpha2 = "00",
		.initiator = NL80211_REGDOM_SET_BY_CORE,
	};

	/*
	 * wiphy_register() reports the world regulatory domain to the driver
	 * before returning; rtw89_regd_init() relies on that.
	 */
	rtw89_regd_notifier(hw->wiphy, &req);
	return 0;
}

void
ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
}

void
ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *w)
{

	queue_work(hw_to_nb(hw)->wq, w);
}

void
ieee80211_queue_delayed_work(struct ieee80211_hw *hw, struct delayed_work *dw,
    unsigned long delay)
{

	queue_delayed_work(hw_to_nb(hw)->wq, dw, delay);
}

void
ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
    void (*iter)(void *, struct ieee80211_sta *), void *data)
{
	struct rtw89_nb *nb = hw_to_nb(hw);

	if (nb->sta != NULL && nb->sta_state >= IEEE80211_STA_NONE)
		(*iter)(data, nb->sta);
}

void
ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw, u32 flags,
    void (*iter)(void *, u8 *, struct ieee80211_vif *), void *data)
{
	struct rtw89_nb *nb = hw_to_nb(hw);

	if (nb->vif_added)
		(*iter)(data, nb->vif->addr, nb->vif);
}

struct ieee80211_sta *
ieee80211_find_sta(struct ieee80211_vif *vif, const u8 *addr)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	struct rtw89_nb *nb = rtwvif->rtwdev->hw->bridge;

	if (nb->sta != NULL && nb->sta_state >= IEEE80211_STA_NONE &&
	    ether_addr_equal(nb->sta->addr, addr))
		return nb->sta;
	return NULL;
}

struct ieee80211_sta *
ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw, const u8 *addr,
    const u8 *localaddr)
{
	struct rtw89_nb *nb = hw_to_nb(hw);

	if (nb->sta != NULL && nb->sta_state >= IEEE80211_STA_NONE &&
	    ether_addr_equal(nb->sta->addr, addr))
		return nb->sta;
	return NULL;
}

void
ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
    struct sk_buff *skb, struct napi_struct *napi)
{
	struct rtw89_nb *nb = hw_to_nb(hw);
	struct ieee80211_rx_status *rs = IEEE80211_SKB_RXCB(skb);
	unsigned int len = skb->len;

	if ((rs->flag & RX_FLAG_FAILED_FCS_CRC) == 0 && len > 4 + 10) {
		/* RX_INCLUDES_FCS: strip the trailing CRC */
		rtw89_nb_rx(nb->glue, skb->data, len - 4, rs->signal, rs->freq);
	}
	dev_kfree_skb_any(skb);
}

void
ieee80211_tx_status_ni(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	rtw89_nb_tx_done(hw_to_nb(hw)->glue,
	    (info->flags & IEEE80211_TX_STAT_ACK) != 0);
	dev_kfree_skb_any(skb);
}

void
ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{

	ieee80211_tx_status_ni(hw, skb);
}

void
ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb)
{

	rtw89_nb_tx_done(hw_to_nb(hw)->glue, false);
	dev_kfree_skb_any(skb);
}

void
ieee80211_stop_queues(struct ieee80211_hw *hw)
{

	rtw89_nb_queues(hw_to_nb(hw)->glue, false);
}

void
ieee80211_wake_queues(struct ieee80211_hw *hw)
{

	rtw89_nb_queues(hw_to_nb(hw)->glue, true);
}

void
ieee80211_restart_hw(struct ieee80211_hw *hw)
{

	printf("rtw89: firmware requested a hardware restart\n");
}

void
ieee80211_connection_loss(struct ieee80211_vif *vif)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	rtw89_nb_link_lost(hw_to_nb(rtwvif->rtwdev->hw)->glue);
}

void
ieee80211_beacon_loss(struct ieee80211_vif *vif)
{

	ieee80211_connection_loss(vif);
}

void
ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
    enum nl80211_cqm_rssi_threshold_event ev, s32 level, gfp_t gfp)
{
}

void
ieee80211_scan_completed(struct ieee80211_hw *hw, struct cfg80211_scan_info *i)
{
}

void
ieee80211_ready_on_channel(struct ieee80211_hw *hw)
{
}

void
ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw)
{
}

void
ieee80211_stop_tx_ba_session(struct ieee80211_sta *sta, u16 tid)
{
}

int
ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, u16 tid, u16 timeout)
{

	return -EOPNOTSUPP;
}

void
ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
    u16 tid)
{
}

static struct sk_buff *
rtw89_nb_mgmt_skb(size_t len)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(len + 64);
	if (skb == NULL)
		return NULL;
	memset(skb_put(skb, len), 0, len);
	return skb;
}

struct sk_buff *
ieee80211_nullfunc_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    int link_id, bool qos_ok)
{
	struct ieee80211_hdr_3addr *h;
	struct sk_buff *skb;

	skb = rtw89_nb_mgmt_skb(sizeof(*h));
	if (skb == NULL)
		return NULL;
	h = (void *)skb->data;
	h->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
	    IEEE80211_STYPE_NULLFUNC | IEEE80211_FCTL_TODS);
	if (vif->bss_conf.bssid != NULL) {
		ether_addr_copy(h->addr1, vif->bss_conf.bssid);
		ether_addr_copy(h->addr3, vif->bss_conf.bssid);
	}
	ether_addr_copy(h->addr2, vif->addr);
	return skb;
}

struct sk_buff *
ieee80211_pspoll_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct ieee80211_pspoll *p;
	struct sk_buff *skb;

	skb = rtw89_nb_mgmt_skb(sizeof(*p));
	if (skb == NULL)
		return NULL;
	p = (void *)skb->data;
	p->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
	    IEEE80211_STYPE_PSPOLL);
	p->aid = cpu_to_le16(vif->cfg.aid | 0xc000);
	if (vif->bss_conf.bssid != NULL)
		ether_addr_copy(p->bssid, vif->bss_conf.bssid);
	ether_addr_copy(p->ta, vif->addr);
	return skb;
}

struct sk_buff *
ieee80211_probereq_get(struct ieee80211_hw *hw, const u8 *src,
    const u8 *ssid, size_t ssid_len, size_t tailroom)
{
	struct ieee80211_hdr_3addr *h;
	struct sk_buff *skb;
	u8 *ie;

	skb = dev_alloc_skb(sizeof(*h) + 2 + ssid_len + tailroom + 64);
	if (skb == NULL)
		return NULL;
	h = skb_put_zero(skb, sizeof(*h));
	h->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
	    IEEE80211_STYPE_PROBE_REQ);
	eth_broadcast_addr(h->addr1);
	ether_addr_copy(h->addr2, src);
	eth_broadcast_addr(h->addr3);
	ie = skb_put(skb, 2 + ssid_len);
	ie[0] = WLAN_EID_SSID;
	ie[1] = ssid_len;
	if (ssid_len)
		memcpy(ie + 2, ssid, ssid_len);
	return skb;
}

struct sk_buff *
ieee80211_proberesp_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{

	return NULL;
}

struct sk_buff *
ieee80211_beacon_get_tim(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    u16 *tim_offset, u16 *tim_length, unsigned int link_id)
{

	return NULL;
}

/* ------------------------------------------------------------------ */
/* Entry points for the NetBSD glue                                    */
/* ------------------------------------------------------------------ */

void
rtw89_nb_set_debug(unsigned int mask)
{

	rtw89_debug_mask = mask;
}

int
rtw89_nb_attach(const struct rtw89_nb_attach_args *aa, struct rtw89_nb **nbp)
{
	struct rtw89_nb *nb;
	int ret;

	nb = kzalloc(sizeof(*nb), GFP_KERNEL);
	if (nb == NULL)
		return ENOMEM;
	nb->glue = aa->aa_glue;
	nb->pdev.dev = aa->aa_dev;
	nb->pdev.nb_priv = aa->aa_glue;
	nb->pdev.mmio = aa->aa_mmio;
	nb->pdev.mmio_size = aa->aa_mmio_size;
	nb->wq = alloc_ordered_workqueue("rtw89hw", 0);
	if (nb->wq == NULL) {
		kfree(nb);
		return ENOMEM;
	}
	INIT_WORK(&nb->wme_work, rtw89_nb_wme_work);
	spin_lock_init(&nb->wme_lock);

	rtw89_nb_attaching = nb;
	ret = rtw89_8852be_probe(&nb->pdev, aa->aa_device_id);
	rtw89_nb_attaching = NULL;
	if (ret) {
		destroy_workqueue(nb->wq);
		rtw89_kmutex_destroy(&nb->wme_lock.mtx);
		kfree(nb);
		return -ret;
	}
	*nbp = nb;
	return 0;
}

void
rtw89_nb_detach(struct rtw89_nb *nb)
{

	if (nb->started)
		rtw89_nb_stop(nb);
	cancel_work_sync(&nb->wme_work);
	rtw89_pci_remove(&nb->pdev);
	destroy_workqueue(nb->wq);
	rtw89_kmutex_destroy(&nb->wme_lock.mtx);
	kfree(nb->vif);
	kfree(nb->sta);
	kfree(nb);
}

void
rtw89_nb_get_macaddr(struct rtw89_nb *nb, uint8_t *addr)
{

	memcpy(addr, nb->hw->perm_addr, ETH_ALEN);
}

static void
rtw89_nb_configure_filter(struct rtw89_nb *nb, unsigned int set,
    unsigned int clear)
{
	struct ieee80211_hw *hw = nb->hw;
	unsigned int old = nb->filter, new;

	nb->filter = (nb->filter | set) & ~clear;
	new = nb->filter;
	hw->ops->configure_filter(hw, old ^ new, &new, 0);
}

static void
rtw89_nb_conf_tx_defaults(struct rtw89_nb *nb)
{
	static const struct {
		u16 cw_min, cw_max, txop;
		u8 aifs;
	} defs[IEEE80211_NUM_ACS] = {
		[IEEE80211_AC_VO] = { 3, 7, 47, 2 },
		[IEEE80211_AC_VI] = { 7, 15, 94, 2 },
		[IEEE80211_AC_BE] = { 15, 1023, 0, 3 },
		[IEEE80211_AC_BK] = { 15, 1023, 0, 7 },
	};
	struct ieee80211_tx_queue_params p;
	int ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		memset(&p, 0, sizeof(p));
		p.cw_min = defs[ac].cw_min;
		p.cw_max = defs[ac].cw_max;
		p.txop = defs[ac].txop;
		p.aifs = defs[ac].aifs;
		nb->hw->ops->conf_tx(nb->hw, nb->vif, 0, ac, &p);
	}
}

/*
 * net80211 applies the EDCA parameters of the AP under its beacon lock,
 * where the rtw89 mutex cannot be taken.  Keep the newest set and apply
 * it from the ordered work queue.
 */
void
rtw89_nb_conf_wme(struct rtw89_nb *nb,
    const struct rtw89_nb_edca edca[RTW89_NB_NAC])
{

	spin_lock(&nb->wme_lock);
	memcpy(nb->wme, edca, sizeof(nb->wme));
	nb->wme_pending = true;
	spin_unlock(&nb->wme_lock);
	queue_work(nb->wq, &nb->wme_work);
}

static void
rtw89_nb_wme_work(struct work_struct *w)
{
	/* net80211 WME_AC_BE, BK, VI, VO to the mac80211 queue index. */
	static const u8 map[RTW89_NB_NAC] = {
		IEEE80211_AC_BE, IEEE80211_AC_BK, IEEE80211_AC_VI,
		IEEE80211_AC_VO
	};
	struct rtw89_nb *nb = container_of(w, struct rtw89_nb, wme_work);
	struct rtw89_nb_edca edca[RTW89_NB_NAC];
	struct ieee80211_tx_queue_params p;
	bool pending;
	int ac;

	spin_lock(&nb->wme_lock);
	memcpy(edca, nb->wme, sizeof(edca));
	pending = nb->wme_pending;
	nb->wme_pending = false;
	spin_unlock(&nb->wme_lock);
	if (!pending || !nb->started || !nb->vif_added)
		return;
	for (ac = 0; ac < RTW89_NB_NAC; ac++) {
		memset(&p, 0, sizeof(p));
		p.aifs = edca[ac].aifsn;
		p.cw_min = (1U << edca[ac].ecw_min) - 1;
		p.cw_max = (1U << edca[ac].ecw_max) - 1;
		p.txop = edca[ac].txop;
		nb->hw->ops->conf_tx(nb->hw, nb->vif, 0, map[ac], &p);
	}
}

int
rtw89_nb_start(struct rtw89_nb *nb, const uint8_t *addr)
{
	struct ieee80211_hw *hw = nb->hw;
	int ret;

	if (nb->started)
		return 0;
	if (nb->vif == NULL) {
		nb->vif = kzalloc(sizeof(*nb->vif) + hw->vif_data_size,
		    GFP_KERNEL);
		nb->sta = kzalloc(sizeof(*nb->sta) + hw->sta_data_size,
		    GFP_KERNEL);
		if (nb->vif == NULL || nb->sta == NULL)
			return ENOMEM;
	}
	memset(nb->vif, 0, sizeof(*nb->vif) + hw->vif_data_size);
	nb->vif->type = NL80211_IFTYPE_STATION;
	memcpy(nb->vif->addr, addr, ETH_ALEN);
	nb->vif->bss_conf.chandef = hw->conf.chandef;
	nb->vif->bss_conf.use_short_slot = true;
	nb->sta_state = IEEE80211_STA_NOTEXIST;
	hw->conf.flags = 0;

	ret = hw->ops->start(hw);
	if (ret)
		return -ret;
	nb->started = true;

	ret = hw->ops->add_interface(hw, nb->vif);
	if (ret) {
		hw->ops->stop(hw);
		nb->started = false;
		return -ret;
	}
	nb->vif_added = true;

	nb->filter = 0;
	rtw89_nb_configure_filter(nb, FIF_ALLMULTI, 0);
	rtw89_nb_conf_tx_defaults(nb);
	hw->ops->config(hw, IEEE80211_CONF_CHANGE_CHANNEL);
	return 0;
}

void
rtw89_nb_stop(struct rtw89_nb *nb)
{
	struct ieee80211_hw *hw = nb->hw;

	if (!nb->started)
		return;
	spin_lock(&nb->wme_lock);
	nb->wme_pending = false;
	spin_unlock(&nb->wme_lock);
	cancel_work_sync(&nb->wme_work);
	rtw89_nb_bss_leave(nb);
	if (nb->vif_added) {
		hw->ops->remove_interface(hw, nb->vif);
		nb->vif_added = false;
	}
	hw->ops->stop(hw);
	nb->started = false;
	nb->offchan = false;
	nb->tx_quiet = false;
	nb->quiet_tx_en = 0;
}

int
rtw89_nb_set_channel(struct rtw89_nb *nb, unsigned int chan, unsigned int freq)
{
	struct ieee80211_hw *hw = nb->hw;

	if (!nb->started)
		return ENXIO;
	if (freq < 3000)
		nb->chan.band = NL80211_BAND_2GHZ;
	else if (freq < 5950)
		nb->chan.band = NL80211_BAND_5GHZ;
	else
		nb->chan.band = NL80211_BAND_6GHZ;
	nb->chan.center_freq = freq;
	nb->chan.hw_value = chan;
	nb->chan.flags = 0;
	cfg80211_chandef_create(&hw->conf.chandef, &nb->chan,
	    NL80211_CHAN_NO_HT);
	nb->vif->bss_conf.chandef = hw->conf.chandef;
	return -hw->ops->config(hw, IEEE80211_CONF_CHANGE_CHANNEL);
}

void
rtw89_nb_scan_start(struct rtw89_nb *nb)
{

	if (!nb->started)
		return;
	nb->hw->ops->sw_scan_start(nb->hw, nb->vif, nb->vif->addr);
	rtw89_nb_configure_filter(nb, FIF_BCN_PRBRESP_PROMISC, 0);
}

void
rtw89_nb_scan_end(struct rtw89_nb *nb)
{

	if (!nb->started)
		return;
	nb->hw->ops->sw_scan_complete(nb->hw, nb->vif);
	rtw89_nb_configure_filter(nb, 0, FIF_BCN_PRBRESP_PROMISC);
}

/*
 * Connected scan: listen on a 20 MHz channel and keep the association.
 * mac80211 does the same for a software scan: the scan hooks first, then
 * the channel. The first call saves the operating channel and width.
 */
int
rtw89_nb_offchan(struct rtw89_nb *nb, unsigned int chan, unsigned int freq)
{

	if (!nb->started)
		return ENXIO;
	if (!nb->offchan) {
		nb->oper_chan = nb->chan;
		nb->oper_def = nb->hw->conf.chandef;
		rtw89_nb_scan_start(nb);
		nb->offchan = true;
	}
	return rtw89_nb_set_channel(nb, chan, freq);
}

/* Restore the saved operating channel and width if asked, then end the scan. */
int
rtw89_nb_onchan(struct rtw89_nb *nb, bool restore)
{
	struct ieee80211_hw *hw = nb->hw;
	int error = 0;

	if (!nb->started || !nb->offchan)
		return 0;
	nb->offchan = false;
	if (restore) {
		nb->chan = nb->oper_chan;
		hw->conf.chandef = nb->oper_def;
		hw->conf.chandef.chan = &nb->chan;
		nb->vif->bss_conf.chandef = hw->conf.chandef;
		error = -hw->ops->config(hw, IEEE80211_CONF_CHANGE_CHANNEL);
	}
	rtw89_nb_scan_end(nb);
	return error;
}

static void
rtw89_nb_fill_bss(struct rtw89_nb *nb, const struct rtw89_nb_bss *bss)
{
	struct ieee80211_vif *vif = nb->vif;
	struct ieee80211_sta *sta = nb->sta;
	enum nl80211_band band = nb->chan.band;
	u32 rates = bss->rates, basic = bss->basic_rates;

	if (band != NL80211_BAND_2GHZ) {
		/* the 5/6GHz bands start at OFDM 6M */
		rates >>= 4;
		basic >>= 4;
	}
	memcpy(vif->bss_conf.bssid_buf, bss->bssid, ETH_ALEN);
	vif->bss_conf.bssid = vif->bss_conf.bssid_buf;
	vif->bss_conf.beacon_int = bss->beacon_int;
	vif->bss_conf.dtim_period = bss->dtim_period ? bss->dtim_period : 1;
	vif->bss_conf.use_short_slot = bss->short_slot;
	vif->bss_conf.basic_rates = basic;
	vif->bss_conf.chandef = nb->hw->conf.chandef;

	memcpy(sta->addr, bss->bssid, ETH_ALEN);
	sta->aid = bss->aid;
	sta->deflink.supp_rates[band] = rates ? rates : 1;
	sta->deflink.rx_nss = 1;
	sta->deflink.bandwidth = IEEE80211_STA_RX_BW_20;
	sta->deflink.agg.max_amsdu_len = 3839;
	sta->deflink.agg.max_rc_amsdu_len = 3839;
	sta->max_amsdu_subframes = 1;
}

/*
 * Give the firmware rate adaptation the negotiated HT and VHT link.  The
 * AP receives what its capability words announce, so its short guard
 * interval and LDPC bits enable those encodings for transmission.  STBC
 * and beamforming stay disabled.
 */
static void
rtw89_nb_fill_ht(struct rtw89_nb *nb, const struct rtw89_nb_ht *ht)
{
	struct ieee80211_sta *sta = nb->sta;
	struct ieee80211_sta_ht_cap *hc = &sta->deflink.ht_cap;
	struct ieee80211_sta_vht_cap *vc = &sta->deflink.vht_cap;
	unsigned int nss, i;

	memset(hc, 0, sizeof(*hc));
	memset(vc, 0, sizeof(*vc));
	rtw89_nb_fill_he(nb, ht);
	if (!ht->ht)
		return;
	hc->ht_supported = true;
	hc->cap = ht->ht_cap & (IEEE80211_HT_CAP_LDPC_CODING |
	    IEEE80211_HT_CAP_SUP_WIDTH_20_40 | IEEE80211_HT_CAP_SGI_20 |
	    IEEE80211_HT_CAP_SGI_40);
	hc->ampdu_factor = ht->ht_ampdu & 0x3;
	hc->ampdu_density = (ht->ht_ampdu >> 2) & 0x7;
	hc->mcs.rx_mask[0] = ht->ht_mcs & 0xff;
	hc->mcs.rx_mask[1] = ht->ht_mcs >> 8;
	nss = hc->mcs.rx_mask[1] != 0 ? 2 : 1;
	if (ht->vht) {
		vc->vht_supported = true;
		vc->cap = ht->vht_cap & (IEEE80211_VHT_CAP_RXLDPC |
		    IEEE80211_VHT_CAP_SHORT_GI_80 |
		    IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);
		/* rtw89_mac_bf_assoc() sets up the sounding response. */
		if (ht->vht_bfee)
			vc->cap |= ht->vht_cap &
			    (IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
			    IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK);
		vc->vht_mcs.rx_mcs_map = cpu_to_le16(ht->vht_map);
		vc->vht_mcs.tx_mcs_map = cpu_to_le16(ht->vht_map);
		for (nss = 0, i = 0; i < 8; i++)
			if (((ht->vht_map >> (2 * i)) & 0x3) != 0x3)
				nss = i + 1;
	}
	if (ht->he) {
		for (nss = 0, i = 0; i < 8; i++)
			if (((ht->he_map >> (2 * i)) & 0x3) != 0x3)
				nss = i + 1;
	}
	sta->deflink.rx_nss = nss != 0 ? nss : 1;
	switch (ht->width) {
	case 80:
		sta->deflink.bandwidth = IEEE80211_STA_RX_BW_80;
		break;
	case 40:
		sta->deflink.bandwidth = IEEE80211_STA_RX_BW_40;
		break;
	default:
		sta->deflink.bandwidth = IEEE80211_STA_RX_BW_20;
		break;
	}
}

/*
 * HE: the firmware rate adaptation reads the HE words of the AP, and the
 * MAC reads the BSS color and HE Operation of the BSS.  Keep only the AP
 * bits that this station uses with it: the width set, LDPC reception and
 * the PPE Thresholds or nominal padding, and the SU beamformer bit and
 * sounding dimensions when the station offered SU beamformee.  STBC, DCM
 * and MU beamforming stay disabled.  The maps are the negotiated TX map in
 * both directions.
 */
static void
rtw89_nb_fill_he(struct rtw89_nb *nb, const struct rtw89_nb_ht *ht)
{
	struct ieee80211_sta_he_cap *he = &nb->sta->deflink.he_cap;
	struct ieee80211_bss_conf *bss = &nb->vif->bss_conf;

	memset(he, 0, sizeof(*he));
	bss->he_support = false;
	memset(&bss->he_bss_color, 0, sizeof(bss->he_bss_color));
	memset(&bss->he_oper, 0, sizeof(bss->he_oper));
	if (!ht->ht || !ht->he)
		return;
	he->has_he = true;
	he->he_cap_elem.phy_cap_info[0] = ht->he_phy[0] & 0x1e;
	he->he_cap_elem.phy_cap_info[1] = ht->he_phy[1] &
	    IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
	if (ht->he_bfee) {
		he->he_cap_elem.phy_cap_info[3] = ht->he_phy[3] &
		    IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER;
		he->he_cap_elem.phy_cap_info[5] = ht->he_phy[5] &
		    IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK;
	}
	he->he_cap_elem.phy_cap_info[6] = ht->he_phy[6] &
	    IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT;
	he->he_cap_elem.phy_cap_info[9] = ht->he_phy[9] &
	    IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK;
	he->he_mcs_nss_supp.rx_mcs_80 = cpu_to_le16(ht->he_map);
	he->he_mcs_nss_supp.tx_mcs_80 = cpu_to_le16(ht->he_map);
	he->he_mcs_nss_supp.rx_mcs_160 = cpu_to_le16(0xffff);
	he->he_mcs_nss_supp.tx_mcs_160 = cpu_to_le16(0xffff);
	he->he_mcs_nss_supp.rx_mcs_80p80 = cpu_to_le16(0xffff);
	he->he_mcs_nss_supp.tx_mcs_80p80 = cpu_to_le16(0xffff);
	memcpy(he->ppe_thres, ht->he_ppe,
	    MIN(ht->he_ppe_len, sizeof(he->ppe_thres)));
	bss->he_support = true;
	bss->he_bss_color.color = ht->he_bss_color & 0x3f;
	bss->he_bss_color.enabled = (ht->he_bss_color & 0x80) == 0;
	bss->he_oper.params = ht->he_params;
	bss->he_oper.nss_set = ht->he_basic_map;
}

/* Tune the current primary channel to the negotiated 40 or 80 MHz width. */
static int
rtw89_nb_set_width(struct rtw89_nb *nb, const struct rtw89_nb_ht *ht)
{
	struct ieee80211_hw *hw = nb->hw;
	struct cfg80211_chan_def *def = &hw->conf.chandef;

	if (!ht->ht || (ht->width != 40 && ht->width != 80) ||
	    nb->chan.band != NL80211_BAND_5GHZ)
		return 0;
	cfg80211_chandef_create(def, &nb->chan, NL80211_CHAN_HT20);
	def->width = ht->width == 80 ? NL80211_CHAN_WIDTH_80 :
	    NL80211_CHAN_WIDTH_40;
	def->center_freq1 = ieee80211_channel_to_frequency(ht->center,
	    NL80211_BAND_5GHZ);
	nb->vif->bss_conf.chandef = *def;
	return -hw->ops->config(hw, IEEE80211_CONF_CHANGE_CHANNEL);
}

int
rtw89_nb_bss_join(struct rtw89_nb *nb, const struct rtw89_nb_bss *bss)
{
	struct ieee80211_hw *hw = nb->hw;
	int ret;

	if (!nb->started)
		return ENXIO;
	rtw89_nb_bss_leave(nb);
	memset(nb->sta, 0, sizeof(*nb->sta) + hw->sta_data_size);
	rtw89_nb_fill_bss(nb, bss);

	hw->ops->bss_info_changed(hw, nb->vif, &nb->vif->bss_conf,
	    BSS_CHANGED_BSSID | BSS_CHANGED_ERP_SLOT);
	ret = hw->ops->sta_state(hw, nb->vif, nb->sta, IEEE80211_STA_NOTEXIST,
	    IEEE80211_STA_NONE);
	if (ret)
		return -ret;
	nb->sta_state = IEEE80211_STA_NONE;
	hw->ops->sta_state(hw, nb->vif, nb->sta, IEEE80211_STA_NONE,
	    IEEE80211_STA_AUTH);
	nb->sta_state = IEEE80211_STA_AUTH;
	return 0;
}

int
rtw89_nb_bss_assoc(struct rtw89_nb *nb, const struct rtw89_nb_bss *bss)
{
	struct ieee80211_hw *hw = nb->hw;
	int ret;

	if (!nb->started || nb->sta_state != IEEE80211_STA_AUTH)
		return ENXIO;
	/* The firmware reads the width at association; tune it first. */
	ret = rtw89_nb_set_width(nb, &bss->ht);
	if (ret)
		return ret;
	rtw89_nb_fill_bss(nb, bss);
	rtw89_nb_fill_ht(nb, &bss->ht);
	nb->vif->cfg.aid = bss->aid;
	ret = hw->ops->sta_state(hw, nb->vif, nb->sta, IEEE80211_STA_AUTH,
	    IEEE80211_STA_ASSOC);
	if (ret)
		return -ret;
	nb->sta_state = IEEE80211_STA_ASSOC;
	nb->vif->cfg.assoc = true;
	hw->ops->bss_info_changed(hw, nb->vif, &nb->vif->bss_conf,
	    BSS_CHANGED_ASSOC | BSS_CHANGED_ERP_SLOT);
	hw->ops->sta_state(hw, nb->vif, nb->sta, IEEE80211_STA_ASSOC,
	    IEEE80211_STA_AUTHORIZED);
	nb->sta_state = IEEE80211_STA_AUTHORIZED;
	return 0;
}

void
rtw89_nb_bss_leave(struct rtw89_nb *nb)
{
	struct ieee80211_hw *hw = nb->hw;

	if (!nb->started || nb->sta_state == IEEE80211_STA_NOTEXIST)
		return;
	if (nb->sta_state == IEEE80211_STA_AUTHORIZED) {
		hw->ops->sta_state(hw, nb->vif, nb->sta,
		    IEEE80211_STA_AUTHORIZED, IEEE80211_STA_ASSOC);
		nb->sta_state = IEEE80211_STA_ASSOC;
	}
	if (nb->vif->cfg.assoc) {
		nb->vif->cfg.assoc = false;
		nb->vif->cfg.aid = 0;
		hw->ops->bss_info_changed(hw, nb->vif, &nb->vif->bss_conf,
		    BSS_CHANGED_ASSOC);
	}
	if (nb->sta_state == IEEE80211_STA_ASSOC) {
		hw->ops->sta_state(hw, nb->vif, nb->sta, IEEE80211_STA_ASSOC,
		    IEEE80211_STA_AUTH);
		nb->sta_state = IEEE80211_STA_AUTH;
	}
	if (nb->sta_state == IEEE80211_STA_AUTH) {
		hw->ops->sta_state(hw, nb->vif, nb->sta, IEEE80211_STA_AUTH,
		    IEEE80211_STA_NONE);
		nb->sta_state = IEEE80211_STA_NONE;
	}
	hw->ops->sta_state(hw, nb->vif, nb->sta, IEEE80211_STA_NONE,
	    IEEE80211_STA_NOTEXIST);
	nb->sta_state = IEEE80211_STA_NOTEXIST;
	memset(nb->vif->bss_conf.bssid_buf, 0, ETH_ALEN);
	hw->ops->bss_info_changed(hw, nb->vif, &nb->vif->bss_conf,
	    BSS_CHANGED_BSSID);
	nb->vif->bss_conf.bssid = NULL;
	nb->vif->bss_conf.he_support = false;
	memset(&nb->vif->bss_conf.he_bss_color, 0,
	    sizeof(nb->vif->bss_conf.he_bss_color));
	memset(&nb->vif->bss_conf.he_oper, 0,
	    sizeof(nb->vif->bss_conf.he_oper));
	/* The next BSS can be without WME: drop the EDCA of the old AP. */
	spin_lock(&nb->wme_lock);
	nb->wme_pending = false;
	spin_unlock(&nb->wme_lock);
	if (nb->vif_added)
		rtw89_nb_conf_tx_defaults(nb);
}

/* Synchronously gate the MAC scheduler around a station channel change. */
int
rtw89_nb_tx_quiet(struct rtw89_nb *nb, bool quiet)
{
	struct rtw89_dev *rtwdev = nb_to_rtwdev(nb);
	int error;

	if (!nb->started)
		return ENXIO;
	if (quiet == nb->tx_quiet)
		return 0;
	if (quiet) {
		error = rtw89_chip_stop_sch_tx(rtwdev, RTW89_MAC_0,
		    &nb->quiet_tx_en, RTW89_SCH_TX_SEL_ALL);
		if (error)
			return error;
		nb->tx_quiet = true;
	} else {
		error = rtw89_chip_resume_sch_tx(rtwdev, RTW89_MAC_0,
		    nb->quiet_tx_en);
		if (error)
			return error;
		nb->tx_quiet = false;
	}
	return 0;
}

/* Fill in the protocol and header offsets used by the BT-coex hints. */
static void
rtw89_nb_tx_classify(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	unsigned int hdrlen, off;
	const u8 *llc;

	if (!ieee80211_is_data(hdr->frame_control) ||
	    ieee80211_has_protected(hdr->frame_control))
		return;
	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	if (skb->len < hdrlen + 8 + 20)
		return;
	llc = skb->data + hdrlen;
	if (llc[0] != 0xaa || llc[1] != 0xaa || llc[2] != 0x03)
		return;
	skb->protocol = cpu_to_be16((llc[6] << 8) | llc[7]);
	off = (skb->data - skb->head) + hdrlen + 8;
	skb->network_header = off;
	if (skb->protocol == htons(ETH_P_IP))
		skb->transport_header = off + (skb->head[off] & 0x0f) * 4;
	else
		skb->transport_header = off;
}

/* Spatial streams that both directions of this chip support. */
unsigned int
rtw89_nb_nss(struct rtw89_nb *nb)
{
	struct rtw89_dev *rtwdev = nb_to_rtwdev(nb);

	return MAX(1, MIN(rtwdev->hal.tx_nss, rtwdev->hal.rx_nss));
}

/*
 * The last firmware rate-adaptation report and RX rate of the AP entry.
 * Plain reads of values that the C2H handler updates; diagnostics only.
 */
void
rtw89_nb_link_info(struct rtw89_nb *nb, struct rtw89_nb_link *link)
{
	static const u16 bw_mhz[] = {
		[RATE_INFO_BW_20] = 20, [RATE_INFO_BW_40] = 40,
		[RATE_INFO_BW_80] = 80, [RATE_INFO_BW_160] = 160,
	};
	const struct rtw89_sta *rtwsta;
	const struct rate_info *r;

	memset(link, 0, sizeof(*link));
	if (!nb->started || nb->sta_state < IEEE80211_STA_ASSOC)
		return;
	rtwsta = (const struct rtw89_sta *)nb->sta->drv_priv;
	r = &rtwsta->ra_report.txrate;
	link->valid = true;
	if (r->flags & RATE_INFO_FLAGS_HE_MCS)
		link->tx_mode = RTW89_NB_MODE_HE;
	else if (r->flags & RATE_INFO_FLAGS_VHT_MCS)
		link->tx_mode = RTW89_NB_MODE_VHT;
	else if (r->flags & RATE_INFO_FLAGS_MCS)
		link->tx_mode = RTW89_NB_MODE_HT;
	else
		link->tx_mode = RTW89_NB_MODE_LEGACY;
	link->tx_mcs = r->mcs;
	/* rtw89 reports NSS for VHT and HE; an HT MCS implies it. */
	link->tx_nss = link->tx_mode == RTW89_NB_MODE_HT ? r->mcs / 8 + 1 :
	    r->nss;
	link->tx_sgi = (r->flags & RATE_INFO_FLAGS_SHORT_GI) != 0;
	link->tx_bw = r->bw < __arraycount(bw_mhz) ? bw_mhz[r->bw] : 0;
	link->tx_legacy = r->legacy;
	link->tx_hw_rate = rtwsta->ra_report.hw_rate;
	link->rx_hw_rate = rtwsta->rx_hw_rate;
	link->rssi = RTW89_RSSI_RAW_TO_DBM(ewma_rssi_read(
	    __UNCONST(&rtwsta->avg_rssi)));
}

/* True when the best-effort data ring can take one more frame. */
bool
rtw89_nb_tx_space(struct rtw89_nb *nb)
{
	struct rtw89_dev *rtwdev = nb_to_rtwdev(nb);

	if (!nb->started || !nb->vif_added)
		return false;
	return rtw89_hci_check_and_reclaim_tx_resource(rtwdev,
	    rtw89_core_get_ch_dma(rtwdev, rtw89_core_get_qsel(rtwdev, 0))) != 0;
}

/*
 * Send one 802.11 frame.  With RTW89_NB_TX_AMPDU, "agg" is the recipient
 * window of the established originator session of the frame's TID; the
 * MAC then aggregates the frame with its neighbours of that TID.
 */
int
rtw89_nb_tx(struct rtw89_nb *nb, const void *frame, size_t len,
    unsigned int flags, unsigned int agg)
{
	struct ieee80211_hw *hw = nb->hw;
	struct ieee80211_tx_control control = { 0 };
	struct ieee80211_tx_info *info;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb;

	if (!nb->started || !nb->vif_added)
		return ENXIO;
	if (len < sizeof(struct ieee80211_hdr_3addr))
		return EINVAL;
	hdr = (void *)__UNCONST(frame);
	{
		/* Reclaim completed descriptors first, like mac80211 TXQs. */
		struct rtw89_dev *rtwdev = nb_to_rtwdev(nb);
		u8 qsel, tid = 0;

		if (ieee80211_is_mgmt(hdr->frame_control) ||
		    ieee80211_is_nullfunc(hdr->frame_control)) {
			qsel = RTW89_TX_QSEL_B0_MGMT;
		} else {
			if (ieee80211_is_data_qos(hdr->frame_control))
				tid = *ieee80211_get_qos_ctl(hdr) &
				    IEEE80211_QOS_CTL_TAG1D_MASK;
			qsel = rtw89_core_get_qsel(rtwdev, tid);
		}
		if (rtw89_hci_check_and_reclaim_tx_resource(rtwdev,
		    rtw89_core_get_ch_dma(rtwdev, qsel)) == 0)
			return ENOBUFS;
	}
	skb = dev_alloc_skb(len + IEEE80211_HT_CTL_LEN);
	if (skb == NULL)
		return ENOBUFS;
	skb_reserve(skb, IEEE80211_HT_CTL_LEN);
	memcpy(skb_put(skb, len), frame, len);
	hdr = (void *)skb->data;

	info = IEEE80211_SKB_CB(skb);
	memset(info, 0, sizeof(*info));
	info->band = nb->chan.band;
	info->control.vif = nb->vif;
	if (flags & RTW89_NB_TX_NO_ACK || is_multicast_ether_addr(hdr->addr1))
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	if (flags & RTW89_NB_TX_EAPOL)
		info->control.flags |= IEEE80211_TX_CTRL_PORT_CTRL_PROTO;
	if (ieee80211_is_data_qos(hdr->frame_control))
		skb->priority = *ieee80211_get_qos_ctl(hdr) &
		    IEEE80211_QOS_CTL_TAG1D_MASK;
	rtw89_nb_tx_classify(skb);

	if (nb->sta_state >= IEEE80211_STA_NONE &&
	    ether_addr_equal(hdr->addr1, nb->sta->addr))
		control.sta = nb->sta;
	if ((flags & RTW89_NB_TX_AMPDU) != 0 && agg != 0 &&
	    control.sta != NULL && nb->sta->deflink.ht_cap.ht_supported &&
	    nb->sta_state == IEEE80211_STA_AUTHORIZED &&
	    ieee80211_is_data_qos(hdr->frame_control) &&
	    (flags & (RTW89_NB_TX_NO_ACK | RTW89_NB_TX_EAPOL)) == 0) {
		struct rtw89_sta *rtwsta = (struct rtw89_sta *)nb->sta->drv_priv;

		/* mac80211 sets this at IEEE80211_AMPDU_TX_OPERATIONAL. */
		rtwsta->ampdu_params[skb->priority].agg_num = MIN(agg, 64);
		info->flags |= IEEE80211_TX_CTL_AMPDU;
	}

	hw->ops->tx(hw, &control, skb);
	return 0;
}
