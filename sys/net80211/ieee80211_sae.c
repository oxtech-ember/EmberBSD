/* SPDX-License-Identifier: BSD-2-Clause */
/* Native external-SAE and mandatory PMF owner. Serialized by net80211/Giant. */
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/mbuf.h>
#include <sys/kmem.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_ether.h>
#include <net/route.h>
#include <net80211/ieee80211_netbsd.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_sae.h>
#include <net80211/ieee80211_rsn.h>
#include <net80211/ieee80211_bip.h>
#include <crypto/aes/aes.h>

/* The host contract compiles the actual implementation below this marker. */
enum sae_phase { SAE_OFF, SAE_CONFIGURED, SAE_AUTH, SAE_AUTHENTICATED,
    SAE_ASSOC, SAE_RUN, SAE_RETIRED };
struct sae_igtk {
	uint8_t key[16];
	uint64_t ipn;
	bool installed;
};
struct ieee80211_sae {
	struct ieee80211com *ic;
	uint64_t serial;
	uint32_t generation;
	uint8_t bssid[IEEE80211_ADDR_LEN];
	enum sae_phase phase;
	bool enabled, port, changing, exhausted;
	bool hw_blocked, hw_busy, hw_reopen;
	uint64_t hw_epoch;
	const struct ieee80211_sae_hw_ops *hw;
	struct sae_igtk igtk[2];
};
/* Private kernel provenance; users cannot create mbuf tags. */
#define SAE_PACKET_TAG 0x5345
struct sae_packet {
	uint64_t serial;
	bool encrypted, data, eapol;
};

uint64_t
ieee80211_sae_serial(const struct ieee80211com *ic)
{
#ifdef __HAVE_ATOMIC64_LOADSTORE
	return atomic_load_relaxed(&ic->ic_sae_serial);
#else
	/* A transport must never observe a torn lifetime cookie. */
	(void)ic;
	return 0;
#endif
}

bool
ieee80211_sae_active(const struct ieee80211com *ic)
{
	return ic->ic_sae != NULL && ic->ic_sae->enabled;
}

bool
ieee80211_sae_qos_tx_allowed(const struct ieee80211com *ic)
{
	return !ieee80211_sae_active(ic) || ic->ic_sae->hw == NULL ||
	    !(ic->ic_sae->hw->flags & IEEE80211_SAE_HW_NO_QOS_TX);
}

bool
ieee80211_sae_port_allowed(const struct ieee80211com *ic)
{
	return !ieee80211_sae_active(ic) || (ic->ic_opmode == IEEE80211_M_STA &&
	    ic->ic_sae->port && !ic->ic_sae->hw_blocked &&
	    ic->ic_sae->phase == SAE_RUN && !ic->ic_sae->exhausted);
}

static void
sae_advance(struct ieee80211_sae *s)
{
	if (s->serial == UINT64_MAX)
		s->exhausted = true;
	else
		s->serial++;
#ifdef __HAVE_ATOMIC64_LOADSTORE
	atomic_store_relaxed(&s->ic->ic_sae_serial, s->serial);
#endif
}

static void sae_hw_fence(struct ieee80211_sae *);

static void
sae_close(struct ieee80211com *ic)
{
	if (ic->ic_sae->port) {
		ic->ic_sae->port = false;
		sae_hw_fence(ic->ic_sae);
		sae_advance(ic->ic_sae);
		/* Close the asynchronous transport's already-encoded data too. */
		ieee80211_key_update_begin(ic);
		ieee80211_key_update_end(ic);
	}
	if (ic->ic_bss != NULL)
		ieee80211_node_unauthorize(ic->ic_bss);
}

static bool
sae_peer(const struct ieee80211com *ic, const struct ieee80211_node *ni)
{
	return ic->ic_sae != NULL && ic->ic_opmode == IEEE80211_M_STA &&
	    ni != NULL && ni == ic->ic_bss &&
	    IEEE80211_ADDR_EQ(ni->ni_bssid, ic->ic_sae->bssid);
}

static bool
sae_ptk(const struct ieee80211com *ic)
{
	const struct ieee80211_key *k;

	if (!sae_peer(ic, ic->ic_bss))
		return false;
	k = &ic->ic_bss->ni_ucastkey;
	return k->wk_cipher->ic_cipher == IEEE80211_CIPHER_AES_CCM && k->wk_keylen == 16 &&
	    (k->wk_flags & (IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV |
	    IEEE80211_KEY_SWCRYPT | IEEE80211_KEY_GROUP)) ==
	    (IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV | IEEE80211_KEY_SWCRYPT);
}

bool
ieee80211_sae_data_allowed(const struct ieee80211com *ic,
    const struct ieee80211_node *ni, const struct ieee80211_key *key, bool eapol)
{
	if (!ieee80211_sae_active(ic))
		return true;
	if (!sae_peer(ic, ni) || ic->ic_sae->phase != SAE_RUN || ic->ic_sae->exhausted ||
	    ic->ic_sae->hw_blocked)
		return false;
	if (eapol)
		return true;
	if (!ic->ic_sae->port || !sae_ptk(ic) || key == NULL ||
	    key->wk_cipher->ic_cipher != IEEE80211_CIPHER_AES_CCM ||
	    !(key->wk_flags & IEEE80211_KEY_SWCRYPT))
		return false;
	return key == &ni->ni_ucastkey || key == &ic->ic_nw_keys[0] ||
	    key == &ic->ic_nw_keys[1] || key == &ic->ic_nw_keys[2] ||
	    key == &ic->ic_nw_keys[3];
}

static int
sae_attach(struct ieee80211com *ic, bool hardware)
{
#ifndef __HAVE_ATOMIC64_LOADSTORE
	/* No native opt-in on kernels without atomic 64-bit publication. */
	(void)ic;
	(void)hardware;
	return EOPNOTSUPP;
#else
	if (ic->ic_sae != NULL)
		return EALREADY;
	/* This owner cannot authenticate firmware-decrypted management. */
	if (!hardware && (ic->ic_caps & IEEE80211_C_AES_CCM))
		return EOPNOTSUPP;
	if (ieee80211_sae_serial(ic) == UINT64_MAX)
		return EOVERFLOW;
	ic->ic_sae = kmem_zalloc(sizeof(*ic->ic_sae), KM_SLEEP);
	ic->ic_sae->ic = ic;
	ic->ic_sae->serial = ieee80211_sae_serial(ic) + 1;
	atomic_store_relaxed(&ic->ic_sae_serial, ic->ic_sae->serial);
	return 0;
#endif
}

int
ieee80211_sae_attach(struct ieee80211com *ic)
{
	return sae_attach(ic, false);
}

int
ieee80211_sae_attach_hw(struct ieee80211com *ic,
    const struct ieee80211_sae_hw_ops *ops)
{
	int error;

	if (ops == NULL || ops->fence == NULL || ops->replace == NULL)
		return EINVAL;
	/* The explicit RX contract supersedes the legacy hardware capability. */
	error = sae_attach(ic, true);
	if (error == 0)
		ic->ic_sae->hw = ops;
	return error;
}

static void
sae_hw_fence(struct ieee80211_sae *s)
{
	if (s->hw != NULL) {
		s->hw_reopen = false;
		s->hw_blocked = true;
		s->hw_epoch = 0;
		s->hw->fence(s->ic);
	}
}

/* Build a bounded complete snapshot, overriding one slot transactionally. */
static int
sae_hw_replace(struct ieee80211com *ic, uint16_t id,
    const struct ieee80211_sae_hw_key *replacement)
{
	struct ieee80211_sae *s = ic->ic_sae;
	struct ieee80211_sae_hw_key keys[7];
	size_t n = 0;
	uint64_t serial;
	int error;

	if (s->hw == NULL)
		return 0;
	if (s->hw_busy || s->exhausted)
		return EBUSY;
	memset(keys, 0, sizeof(keys));
	for (unsigned i = 0; i < 7; i++) {
		uint16_t slot = i == 0 ? IEEE80211_SAE_HW_PTK : i - 1;
		if (slot == id) {
			if (replacement != NULL)
				keys[n++] = *replacement;
			continue;
		}
		if (i < 5) {
			const struct ieee80211_key *k = i == 0 ?
			    &ic->ic_bss->ni_ucastkey : &ic->ic_nw_keys[i - 1];
			if (k->wk_keylen != 16 ||
			    k->wk_cipher->ic_cipher != IEEE80211_CIPHER_AES_CCM)
				continue;
			keys[n].cipher = IEEE80211_CIPHER_AES_CCM;
			keys[n].flags = k->wk_flags & IEEE80211_KEY_COMMON;
			keys[n].rsc = k->wk_keyrsc;
			memcpy(keys[n].key, k->wk_key, 16);
		} else {
			const struct sae_igtk *k = &s->igtk[i - 5];
			if (!k->installed)
				continue;
			keys[n].cipher = IEEE80211_SAE_HW_BIP;
			keys[n].flags = IEEE80211_KEY_GROUP | IEEE80211_KEY_RECV;
			keys[n].rsc = k->ipn;
			memcpy(keys[n].key, k->key, 16);
		}
		keys[n++].id = slot;
	}
	sae_hw_fence(s);
	sae_advance(s);
	serial = s->serial;
	s->hw_busy = true;
	error = s->hw->replace(ic, serial, keys, n);
	explicit_memset(keys, 0, sizeof(keys));
	if (ic->ic_sae != s || ieee80211_sae_serial(ic) != serial) {
		if (ic->ic_sae == s) {
			s->hw_busy = false;
			sae_hw_fence(s);
		}
		return ESTALE;
	}
	s->hw_busy = false;
	if (error != 0 || s->exhausted) {
		sae_hw_fence(s);
		return error != 0 ? error : EOVERFLOW;
	}
	s->hw_epoch = serial;
	s->hw_blocked = false;
	return 0;
}

#define SAE_RX_TAG 0x5346
struct sae_rx_packet {
	unsigned amsdu_index;
	bool amsdu_last;
	uint64_t epoch;
	uint16_t id;
	uint8_t cipher;
};

int
ieee80211_sae_tag_rx(struct ieee80211com *ic, struct mbuf *m,
    uint64_t epoch, uint16_t id, uint8_t cipher, unsigned flags)
{
	struct m_tag *tag;
	struct sae_rx_packet *rx;
	struct ieee80211_sae *s = ic->ic_sae;

	if (s == NULL || !s->enabled || s->hw == NULL || s->hw_blocked ||
	    epoch == 0 || epoch != s->hw_epoch || epoch != s->serial ||
	    cipher != IEEE80211_CIPHER_AES_CCM ||
	    (id != IEEE80211_SAE_HW_PTK && id > 3) || flags !=
	    (IEEE80211_SAE_RX_AUTHENTICATED | IEEE80211_SAE_RX_IV_PRESENT |
	    IEEE80211_SAE_RX_MIC_STRIPPED) || m_tag_find(m, SAE_RX_TAG) != NULL)
		return EACCES;
	tag = m_tag_get(SAE_RX_TAG, sizeof(*rx), M_NOWAIT);
	if (tag == NULL)
		return ENOMEM;
	rx = (void *)(tag + 1);
	memset(rx, 0, sizeof(*rx));
	rx->epoch = epoch;
	rx->id = id;
	rx->cipher = cipher;
	m_tag_prepend(m, tag);
	return 0;
}

int
ieee80211_sae_tag_rx_amsdu(struct ieee80211com *ic, struct mbuf *m,
    uint64_t epoch, uint16_t id, uint8_t cipher, unsigned flags,
    unsigned index, bool last)
{
	struct sae_rx_packet *rx;
	int error;

	if (index == 0 || index > 127)
		return EINVAL;
	error = ieee80211_sae_tag_rx(ic, m, epoch, id, cipher, flags);
	if (error != 0)
		return error;
	rx = (void *)(m_tag_find(m, SAE_RX_TAG) + 1);
	rx->amsdu_index = index;
	rx->amsdu_last = last;
	return 0;
}

bool
ieee80211_sae_rx_amsdu(const struct ieee80211com *ic,
    const struct ieee80211_key *key, struct mbuf *m, uint64_t *epoch,
    unsigned *index, bool *last)
{
	const struct sae_rx_packet *rx;

	if (ieee80211_sae_rx_status(ic, key, m) != 1)
		return false;
	rx = (const void *)(m_tag_find(m, SAE_RX_TAG) + 1);
	if (rx->amsdu_index == 0)
		return false;
	*epoch = rx->epoch;
	*index = rx->amsdu_index;
	*last = rx->amsdu_last;
	return true;
}

static bool
sae_rx_deaggregated(const struct ieee80211com *ic,
    const struct ieee80211_node *ni, struct mbuf *m, unsigned first)
{
	struct m_tag *tag = m_tag_find(m, SAE_RX_TAG);
	const struct sae_rx_packet *rx;
	const struct ieee80211_key *key;
	const uint8_t *frame = mtod(m, const void *);

	if (!sae_peer(ic, ni) || tag == NULL || tag->m_tag_len != sizeof(*rx) ||
	    m->m_len < 26 || (frame[0] & 0x8c) != 0x88 ||
	    !(frame[1] & IEEE80211_FC1_WEP))
		return false;
	rx = (const void *)(tag + 1);
	if (rx->amsdu_index < first || rx->amsdu_index > 127 ||
	    (rx->id != IEEE80211_SAE_HW_PTK && rx->id > 3))
		return false;
	key = rx->id == IEEE80211_SAE_HW_PTK ? &ni->ni_ucastkey :
	    &ic->ic_nw_keys[rx->id];
	return ieee80211_sae_rx_status(ic, key, m) == 1;
}

bool
ieee80211_sae_rx_deaggregated(const struct ieee80211com *ic,
    const struct ieee80211_node *ni, struct mbuf *m)
{
	return sae_rx_deaggregated(ic, ni, m, 1);
}

bool
ieee80211_sae_rx_continuation(const struct ieee80211com *ic,
    const struct ieee80211_node *ni, struct mbuf *m)
{
	return sae_rx_deaggregated(ic, ni, m, 2);
}

int
ieee80211_sae_rx_status(const struct ieee80211com *ic,
    const struct ieee80211_key *key, struct mbuf *m)
{
	const struct ieee80211_sae *s = ic->ic_sae;
	struct m_tag *tag = m_tag_find(m, SAE_RX_TAG);
	const struct sae_rx_packet *rx;
	const struct ieee80211_key *expected;

	if (s == NULL || !s->enabled || s->hw == NULL)
		return tag == NULL ? 0 : -1;
	if (s->hw_blocked || tag == NULL || tag->m_tag_len != sizeof(*rx))
		return -1;
	rx = (const void *)(tag + 1);
	if (rx->epoch == 0 || rx->epoch != s->hw_epoch ||
	    rx->epoch != s->serial || rx->cipher != IEEE80211_CIPHER_AES_CCM ||
	    (rx->id != IEEE80211_SAE_HW_PTK && rx->id > 3) || ic->ic_bss == NULL)
		return -1;
	expected = rx->id == IEEE80211_SAE_HW_PTK ? &ic->ic_bss->ni_ucastkey :
	    &ic->ic_nw_keys[rx->id];
	return expected == key && key->wk_keylen == 16 &&
	    key->wk_cipher->ic_cipher == rx->cipher ? 1 : -1;
}

void
ieee80211_sae_retire(struct ieee80211com *ic)
{
	struct ieee80211_sae *s = ic->ic_sae;

	if (s == NULL)
		return;
	sae_hw_fence(s);
	sae_advance(s);
	if (!s->enabled)
		return;
	sae_close(ic);
	s->phase = SAE_RETIRED;
	explicit_memset(s->igtk, 0, sizeof(s->igtk));
	s->changing = true;
	ieee80211_key_update_begin(ic);
	if (ic->ic_bss != NULL)
		ieee80211_crypto_delkey(ic, &ic->ic_bss->ni_ucastkey);
	ieee80211_crypto_delglobalkeys(ic);
	ieee80211_key_update_end(ic);
	s->changing = false;
}

/* Core notifications after an explicit driver retirement are idempotent.
 * Explicit stop/retire must still invalidate a configure sleeping in copyin. */
void
ieee80211_sae_leave(struct ieee80211com *ic)
{
	if (ieee80211_sae_active(ic) && ic->ic_sae->phase != SAE_RETIRED)
		ieee80211_sae_retire(ic);
}

void
ieee80211_sae_detach(struct ieee80211com *ic)
{
	if (ic->ic_sae == NULL)
		return;
	ieee80211_sae_retire(ic);
	explicit_memset(ic->ic_sae, 0, sizeof(*ic->ic_sae));
	kmem_free(ic->ic_sae, sizeof(*ic->ic_sae));
	ic->ic_sae = NULL;
}

/* Also closes the port for non-ioctl teardown of an installed key. */
void
ieee80211_sae_key_changed(struct ieee80211com *ic, const struct ieee80211_key *key)
{
	if (!ieee80211_sae_active(ic) || ic->ic_sae->changing)
		return;
	if ((ic->ic_bss != NULL && key == &ic->ic_bss->ni_ucastkey) ||
	    key == &ic->ic_nw_keys[0] || key == &ic->ic_nw_keys[1] ||
	    key == &ic->ic_nw_keys[2] || key == &ic->ic_nw_keys[3]) {
		sae_hw_fence(ic->ic_sae);
		sae_close(ic);
		sae_advance(ic->ic_sae);
	}
}

static int
sae_event(struct ieee80211com *ic, uint16_t op, const void *data, size_t len)
{
	struct ieee80211req_sae *ev;

	if (len > IEEE80211_SAE_DATA_MAX)
		return EINVAL;
	ev = kmem_zalloc(sizeof(*ev), KM_NOSLEEP);
	if (ev == NULL)
		return ENOMEM;
	ev->version = IEEE80211_SAE_VERSION;
	ev->generation = ic->ic_sae->generation;
	ev->op = op;
	ev->len = len;
	IEEE80211_ADDR_COPY(ev->bssid, ic->ic_sae->bssid);
	memcpy(ev->data, data, len);
	rt_ieee80211msg(ic->ic_ifp, RTM_IEEE80211_SAE, ev,
	    offsetof(struct ieee80211req_sae, data) + len);
	explicit_memset(ev, 0, sizeof(*ev));
	kmem_free(ev, sizeof(*ev));
	return 0;
}

void
ieee80211_sae_state_request(struct ieee80211com *ic, int next)
{
	const struct ieee80211_sae *s = ic->ic_sae;

	if (s == NULL)
		return;
	if (next < IEEE80211_S_INIT || next > IEEE80211_S_RUN ||
	    next == IEEE80211_S_INIT ||
	    (next == IEEE80211_S_SCAN && s->phase != SAE_CONFIGURED) ||
	    (next == IEEE80211_S_AUTH && s->phase != SAE_CONFIGURED) ||
	    (next == IEEE80211_S_ASSOC && s->phase != SAE_AUTHENTICATED) ||
	    (next == IEEE80211_S_RUN && s->phase != SAE_ASSOC))
		ieee80211_sae_retire(ic);
}

int
ieee80211_sae_state(struct ieee80211com *ic, int next)
{
	struct ieee80211_sae *s = ic->ic_sae;
	int error;

	if (!ieee80211_sae_active(ic))
		return 0;
	if (next < IEEE80211_S_INIT || next > IEEE80211_S_RUN)
		goto fail;
	if (next == IEEE80211_S_INIT || (next == IEEE80211_S_SCAN &&
	    s->phase != SAE_CONFIGURED)) {
		ieee80211_sae_leave(ic);
		return 0;
	}
	if (next == IEEE80211_S_SCAN)
		return 0;
	if (s->exhausted || !sae_peer(ic, ic->ic_bss) ||
	    !ieee80211_sta_rsn_valid(ic->ic_opt_ie, ic->ic_opt_ie_len,
	    true, false))
		goto fail;
	switch (next) {
	case IEEE80211_S_AUTH:
		if (s->phase != SAE_CONFIGURED || ic->ic_bss->ni_esslen == 0 ||
		    ic->ic_bss->ni_esslen > IEEE80211_NWID_LEN)
			goto fail;
		sae_close(ic);
		s->phase = SAE_AUTH;
		error = sae_event(ic, IEEE80211_SAE_START, ic->ic_bss->ni_essid,
		    ic->ic_bss->ni_esslen);
		if (error != 0)
			goto fail;
		ic->ic_mgt_timer = IEEE80211_TRANS_WAIT;
		ic->ic_ifp->if_timer = 1;
		break;
	case IEEE80211_S_ASSOC:
		if (s->phase != SAE_AUTHENTICATED)
			goto fail;
		s->phase = SAE_ASSOC;
		break;
	case IEEE80211_S_RUN:
		if (s->phase != SAE_ASSOC || ic->ic_state != IEEE80211_S_ASSOC)
			goto fail;
		s->phase = SAE_RUN;
		sae_close(ic);
		break;
	default:
		break;
	}
	return 0;
fail:
	ieee80211_sae_retire(ic);
	return EACCES;
}

static struct sae_packet *
sae_tag(struct mbuf *m)
{
	struct m_tag *tag = m_tag_find(m, SAE_PACKET_TAG);

	return tag != NULL && tag->m_tag_len == sizeof(struct sae_packet) ?
	    (struct sae_packet *)(tag + 1) : NULL;
}

bool
ieee80211_sae_tx_current(const struct ieee80211com *ic, const struct ieee80211_node *ni,
    struct mbuf *m)
{
	const struct sae_packet *tag = sae_tag(m);
	const struct ieee80211_sae *s = ic->ic_sae;

	if (s != NULL && s->enabled && s->hw_blocked)
		return false;
	if (tag == NULL) {
		uint8_t fc[2];
		if (!ieee80211_sae_active(ic))
			return true;
		/* Discovery probes have no security state and may use a scan node. */
		if (m->m_pkthdr.len < 24)
			return false;
		m_copydata(m, 0, sizeof(fc), fc);
		return fc[0] == 0x40 && fc[1] == 0;
	}
	return s != NULL && s->enabled && !s->exhausted &&
	    s->phase != SAE_RETIRED && tag->serial == s->serial && sae_peer(ic, ni) &&
	    (!tag->data || (s->phase == SAE_RUN && (tag->eapol || s->port)));
}

int
ieee80211_sae_tag_tx(struct ieee80211com *ic, const struct ieee80211_node *ni,
    struct mbuf *m)
{
	uint8_t frame[26];
	struct m_tag *tag;
	struct sae_packet *packet;
	size_t len;

	if (!ieee80211_sae_active(ic))
		return sae_tag(m) != NULL ? ESTALE : 0;
	if (ic->ic_sae->hw_blocked)
		return EBUSY;
	if (!sae_peer(ic, ni) || ic->ic_sae->exhausted ||
	    ic->ic_sae->phase == SAE_RETIRED || m->m_pkthdr.len < 24)
		return ESTALE;
	if (sae_tag(m) != NULL)
		return ieee80211_sae_tx_current(ic, ni, m) ? 0 : ESTALE;
	len = MIN((size_t)m->m_pkthdr.len, sizeof(frame));
	m_copydata(m, 0, len, frame);
	if ((frame[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_DATA &&
	    ic->ic_sae->phase != SAE_RUN)
		return ENOTCONN;
	if ((frame[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT) {
		if (frame[1] & (IEEE80211_FC1_DIR_MASK | IEEE80211_FC1_MORE_FRAG |
		    IEEE80211_FC1_ORDER | IEEE80211_FC1_WEP) || (frame[22] & 15))
			return EINVAL;
		if (ieee80211_is_robust_mgmt(frame, len)) {
			if (ic->ic_sae->phase != SAE_RUN || !sae_ptk(ic) ||
			    IEEE80211_IS_MULTICAST(frame + 4))
				return EACCES;
			mtod(m, struct ieee80211_frame *)->i_fc[1] |= IEEE80211_FC1_WEP;
		}
	}
	tag = m_tag_get(SAE_PACKET_TAG, sizeof(*packet), M_NOWAIT);
	if (tag == NULL)
		return ENOMEM;
	packet = (struct sae_packet *)(tag + 1);
	memset(packet, 0, sizeof(*packet));
	packet->serial = ic->ic_sae->serial;
	packet->data = (frame[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_DATA;
	if (packet->data) {
		uint8_t llc[8];
		int hdrlen = ieee80211_hdrspace(ic, mtod(m, void *));
		if (hdrlen <= m->m_pkthdr.len - (int)sizeof(llc)) {
			m_copydata(m, hdrlen, sizeof(llc), llc);
			packet->eapol = memcmp(llc, "\xaa\xaa\x03\0\0\0\x88\x8e", 8) == 0;
		}
	}
	m_tag_prepend(m, tag);
	return 0;
}

int
ieee80211_sae_crypto_tx(const struct ieee80211com *ic, const struct ieee80211_node *ni,
    struct mbuf *m)
{
	const struct sae_packet *tag = sae_tag(m);

	if (!ieee80211_sae_tx_current(ic, ni, m) || !sae_ptk(ic) ||
	    ic->ic_sae->phase != SAE_RUN || tag == NULL)
		return -1;
	return tag->encrypted ? 1 : 0;
}

void
ieee80211_sae_crypto_done(struct mbuf *m)
{
	struct sae_packet *tag = sae_tag(m);

	if (tag != NULL)
		tag->encrypted = true;
}

/* The existing transport owns encryption; reserve headroom and one cluster so
 * its legacy crypto_encap interface cannot replace the mbuf on prepend. */
static int
sae_send(struct ieee80211com *ic, const uint8_t *frame, size_t len)
{
	struct ieee80211_node *ni = ic->ic_bss;
	struct mbuf *m;
	int error;

	if (len > IEEE80211_SAE_DATA_MAX || len < 24 ||
	    ic->ic_mgtq.ifq_len >= (ic->ic_mgtq.ifq_maxlen > 0 ?
	    ic->ic_mgtq.ifq_maxlen : IFQ_MAXLEN))
		return ENOBUFS;
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return ENOMEM;
	MCLGET(m, M_DONTWAIT);
	if (!(m->m_flags & M_EXT)) {
		m_freem(m);
		return ENOMEM;
	}
	m->m_data += 32;
	m->m_len = m->m_pkthdr.len = len;
	memcpy(mtod(m, void *), frame, len);
	le16enc(mtod(m, uint8_t *) + 22, ni->ni_txseqs[0]++ << 4);
	error = ieee80211_sae_tag_tx(ic, ni, m);
	if (error != 0) {
		m_freem(m);
		return error;
	}
	M_SETCTX(m, ieee80211_ref_node(ni));
	IF_ENQUEUE(&ic->ic_mgtq, m);
	if_start_lock(ic->ic_ifp);
	return 0;
}

static int
sae_set_key(struct ieee80211com *ic, const struct ieee80211req_sae *req)
{
	struct ieee80211req_key ik;
	struct ieee80211_key staged, *key;
	struct ieee80211_sae *s = ic->ic_sae;
	unsigned flags;
	int error = 0;

	if (req->len != sizeof(ik))
		return EINVAL;
	memcpy(&ik, req->data, sizeof(ik));
	if (ik.ik_type != IEEE80211_CIPHER_AES_CCM || ik.ik_pad != 0 ||
	    ik.ik_keylen != 16 ||
	    ik.ik_keyrsc > IEEE80211_BIP_MAX_PN || ik.ik_keytsc != 0 ||
	    (ik.ik_flags & ~(IEEE80211_KEY_COMMON | IEEE80211_KEY_DEFAULT))) {
		error = EINVAL;
		goto out;
	}
	flags = ik.ik_flags & IEEE80211_KEY_COMMON;
	if (ik.ik_keyix == IEEE80211_KEYIX_NONE) {
		if (ik.ik_flags != (IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV) ||
		    !IEEE80211_ADDR_EQ(ik.ik_macaddr, s->bssid)) {
			error = EINVAL;
			goto out;
		}
		key = &ic->ic_bss->ni_ucastkey;
	} else {
		if (ik.ik_keyix >= IEEE80211_WEP_NKID ||
		    !(flags & IEEE80211_KEY_RECV) || !(flags & IEEE80211_KEY_GROUP)) {
			error = EINVAL;
			goto out;
		}
		key = &ic->ic_nw_keys[ik.ik_keyix];
		/* A key moved between slots must not acquire fresh replay state. */
		for (unsigned i = 0; i < IEEE80211_WEP_NKID; i++) {
			const struct ieee80211_key *other = &ic->ic_nw_keys[i];
			if (other != key && other->wk_keylen == 16 &&
			    other->wk_cipher->ic_cipher == IEEE80211_CIPHER_AES_CCM &&
			    memcmp(other->wk_key, ik.ik_keydata, 16) == 0) {
				error = EINVAL;
				goto out;
			}
		}
	}
	if (key->wk_cipher->ic_cipher == IEEE80211_CIPHER_AES_CCM && key->wk_keylen == 16 &&
	    memcmp(key->wk_key, ik.ik_keydata, 16) == 0) {
		/* Never call setkey: it resets per-TID and management replay. */
		if ((key->wk_flags & IEEE80211_KEY_COMMON) != flags)
			error = EINVAL;
		goto out;
	}
	/* Keep an authorized old key usable until atomic replacement.
	 * A successful rekey preserves authorization; it never grants it. */
	memset(&staged, 0, sizeof(staged));
	staged.wk_cipher = ieee80211_crypto_cipher(IEEE80211_CIPHER_AES_CCM);
	if (staged.wk_cipher == NULL) {
		error = EOPNOTSUPP;
		goto stage_out;
	}
	staged.wk_flags = flags | IEEE80211_KEY_SWCRYPT;
	staged.wk_keyix = ik.ik_keyix == IEEE80211_KEYIX_NONE ? 0 : ik.ik_keyix;
	staged.wk_rxkeyix = IEEE80211_KEYIX_NONE;
	staged.wk_keylen = 16;
	staged.wk_keyrsc = ik.ik_keyrsc;
	memcpy(staged.wk_key, ik.ik_keydata, 16);
	staged.wk_private = staged.wk_cipher->ic_attach(ic, &staged);
	if (staged.wk_private == NULL) {
		error = ENOMEM;
		goto stage_out;
	}
	if (!staged.wk_cipher->ic_setkey(&staged)) {
		staged.wk_cipher->ic_detach(&staged);
		error = EIO;
		goto stage_out;
	}
	struct ieee80211_sae_hw_key hwkey = {
	    .id = ik.ik_keyix, .cipher = IEEE80211_CIPHER_AES_CCM,
	    .flags = flags, .rsc = ik.ik_keyrsc
	};
	memcpy(hwkey.key, ik.ik_keydata, 16);
	error = sae_hw_replace(ic, hwkey.id, &hwkey);
	explicit_memset(&hwkey, 0, sizeof(hwkey));
	if (error != 0) {
		staged.wk_cipher->ic_detach(&staged);
		goto stage_out;
	}
	/* Provider fences queued immutable bytes and asynchronous key work. */
	ieee80211_key_update_begin(ic);
	s->changing = true;
	if (s->hw == NULL)
		sae_advance(s);
	ieee80211_crypto_delkey(ic, key);
	*key = staged;
	if (ik.ik_flags & IEEE80211_KEY_DEFAULT)
		ic->ic_def_txkey = ik.ik_keyix;
	s->changing = false;
	ieee80211_key_update_end(ic);
stage_out:
	if (error != 0 && ic->ic_sae == s)
		sae_close(ic);
	explicit_memset(&staged, 0, sizeof(staged));
out:
	explicit_memset(&ik, 0, sizeof(ik));
	return error;
}

static int
sae_igtk(struct ieee80211com *ic, const struct ieee80211req_sae *req)
{
	struct sae_igtk *key;
	uint64_t ipn = 0;
	unsigned key_index;

	if (req->len != (req->op == IEEE80211_SAE_SET_IGTK ? 24 : 2))
		return EINVAL;
	key_index = le16dec(req->data);
	if (key_index < 4 || key_index > 5)
		return EINVAL;
	key = &ic->ic_sae->igtk[key_index - 4];
	if (req->op == IEEE80211_SAE_SET_IGTK) {
		for (unsigned i = 0; i < 6; i++)
			ipn |= (uint64_t)req->data[2 + i] << (8 * i);
		if (key->installed && memcmp(key->key, req->data + 8, 16) == 0)
			return 0; /* Preserve the authenticated receive IPN. */
		for (unsigned i = 0; i < 2; i++)
			if (&ic->ic_sae->igtk[i] != key && ic->ic_sae->igtk[i].installed &&
			    memcmp(ic->ic_sae->igtk[i].key, req->data + 8, 16) == 0)
				return EINVAL;
	}
	struct ieee80211_sae_hw_key hwkey = {
	    .id = key_index, .cipher = IEEE80211_SAE_HW_BIP,
	    .flags = IEEE80211_KEY_GROUP | IEEE80211_KEY_RECV, .rsc = ipn
	};
	if (req->op == IEEE80211_SAE_SET_IGTK)
		memcpy(hwkey.key, req->data + 8, 16);
	if (req->op == IEEE80211_SAE_DELETE_IGTK)
		sae_close(ic);
	int error = sae_hw_replace(ic, key_index,
	    req->op == IEEE80211_SAE_SET_IGTK ? &hwkey : NULL);
	explicit_memset(&hwkey, 0, sizeof(hwkey));
	if (error != 0)
		return error;
	if (ic->ic_sae->hw == NULL)
		sae_advance(ic->ic_sae);
	explicit_memset(key, 0, sizeof(*key));
	if (req->op == IEEE80211_SAE_SET_IGTK) {
		memcpy(key->key, req->data + 8, 16);
		key->ipn = ipn;
		key->installed = true;
	}
	return 0;
}

int
ieee80211_sae_request(struct ieee80211com *ic, const struct ieee80211req_sae *req)
{
	struct ieee80211_sae *s = ic->ic_sae;
	struct ieee80211req_del_key dk;
	struct ieee80211_key *key;
	int error;

	if (s == NULL)
		return EOPNOTSUPP;
	if (s->hw_busy)
		return EBUSY;
	if (req->version != IEEE80211_SAE_VERSION || req->reserved[0] ||
	    req->reserved[1] || req->len > sizeof(req->data))
		return EINVAL;
	if (req->op == IEEE80211_SAE_CONFIGURE) {
		if (req->len != 1 || req->data[0] > 1)
			return EINVAL;
		if (!req->data[0]) {
			if (req->generation != s->generation ||
			    !IEEE80211_ADDR_EQ(req->bssid, s->bssid))
				return ESTALE;
			ieee80211_sae_retire(ic);
			error = sae_hw_replace(ic, IEEE80211_SAE_HW_PTK, NULL);
			if (error != 0)
				return error;
			s->enabled = false;
			s->phase = SAE_OFF;
			return 0;
		}
		if (ieee80211_crypto_cipher(IEEE80211_CIPHER_AES_CCM) == NULL)
			return EOPNOTSUPP;
		if (s->enabled && s->phase != SAE_RETIRED)
			return s->phase == SAE_CONFIGURED && req->generation == s->generation &&
			    IEEE80211_ADDR_EQ(req->bssid, s->bssid) ? 0 : EBUSY;
		if (s->exhausted || s->serial == UINT64_MAX || req->generation == 0 ||
		    req->generation == s->generation || IEEE80211_IS_MULTICAST(req->bssid) ||
		    memcmp(req->bssid, "\0\0\0\0\0\0", 6) == 0 ||
		    ic->ic_opmode != IEEE80211_M_STA ||
		    (ic->ic_flags & IEEE80211_F_WPA) != IEEE80211_F_WPA2 ||
		    !(ic->ic_flags & IEEE80211_F_PRIVACY) ||
		    !ieee80211_sta_rsn_valid(ic->ic_opt_ie, ic->ic_opt_ie_len, true, false))
			return EINVAL;
		/* Validate everything before publishing the user equality token.
		 * Clear keys from a preceding WPA2 session as well. */
		s->enabled = true;
		ieee80211_sae_retire(ic);
		error = sae_hw_replace(ic, IEEE80211_SAE_HW_PTK, NULL);
		if (error != 0)
			return error;
		s->generation = req->generation;
		IEEE80211_ADDR_COPY(s->bssid, req->bssid);
		s->enabled = true;
		s->phase = SAE_CONFIGURED;
		sae_close(ic);
		return 0;
	}
	if (!s->enabled || s->exhausted || s->phase == SAE_RETIRED ||
	    req->generation != s->generation ||
	    !IEEE80211_ADDR_EQ(req->bssid, s->bssid) || !sae_peer(ic, ic->ic_bss))
		return ESTALE;
	switch (req->op) {
	case IEEE80211_SAE_AUTH_STATUS:
		if (req->len != 2 || s->phase != SAE_AUTH || ic->ic_state != IEEE80211_S_AUTH)
			return EINVAL;
		if (le16dec(req->data) != 0) {
			ieee80211_sae_retire(ic);
			return ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		}
		s->phase = SAE_AUTHENTICATED;
		error = ieee80211_new_state(ic, IEEE80211_S_ASSOC, 0);
		if (error != 0)
			ieee80211_sae_retire(ic);
		return error;
	case IEEE80211_SAE_TX_FRAME:
		if (s->phase != SAE_AUTH || ic->ic_state != IEEE80211_S_AUTH ||
		    req->len < 30 || (req->data[22] & 15) ||
		    ieee80211_sae_auth_frame_validate(req->data, req->len,
		    s->bssid, ic->ic_myaddr, s->bssid, 0) != 0)
			return EINVAL;
		return sae_send(ic, req->data, req->len);
	default:
		break;
	}
	if (s->phase != SAE_RUN || ic->ic_state != IEEE80211_S_RUN)
		return ENOTCONN;
	switch (req->op) {
	case IEEE80211_SAE_SET_KEY:
		return sae_set_key(ic, req);
	case IEEE80211_SAE_DELETE_KEY:
		if (req->len != sizeof(dk))
			return EINVAL;
		memcpy(&dk, req->data, sizeof(dk));
		if (dk.idk_keyix == (uint8_t)IEEE80211_KEYIX_NONE) {
			if (!IEEE80211_ADDR_EQ(dk.idk_macaddr, s->bssid))
				return EINVAL;
			key = &ic->ic_bss->ni_ucastkey;
		} else {
			if (dk.idk_keyix >= IEEE80211_WEP_NKID)
				return EINVAL;
			key = &ic->ic_nw_keys[dk.idk_keyix];
		}
		sae_close(ic);
		error = sae_hw_replace(ic, dk.idk_keyix == (uint8_t)IEEE80211_KEYIX_NONE ?
		    IEEE80211_SAE_HW_PTK : dk.idk_keyix, NULL);
		if (error != 0)
			return error;
		ieee80211_key_update_begin(ic);
		s->changing = true;
		ieee80211_crypto_delkey(ic, key);
		s->changing = false;
		ieee80211_key_update_end(ic);
		return 0;
	case IEEE80211_SAE_SET_IGTK:
	case IEEE80211_SAE_DELETE_IGTK:
		return sae_igtk(ic, req);
	case IEEE80211_SAE_SET_PORT:
		if (req->len != 1 || req->data[0] > 1)
			return EINVAL;
		if (!req->data[0]) {
			bool reopen = s->hw != NULL && s->port && !s->hw_blocked;
			sae_close(ic);
			if (reopen)
				s->hw_reopen = true;
			return 0;
		}
		if (s->hw_reopen) {
			/* No matching slot: republish existing keys without resetting
			 * any shared data, management or BIP replay counter. */
			error = sae_hw_replace(ic, 0xfffe, NULL);
			if (error != 0)
				return error;
		}
		if (s->hw_blocked || !sae_ptk(ic) || (!s->igtk[0].installed && !s->igtk[1].installed))
			return EACCES;
		s->port = true;
		ieee80211_node_authorize(ic->ic_bss);
		return 0;
	case IEEE80211_SAE_TX_SA_QUERY:
		if (req->len != 28 || le16dec(req->data) != 0x00d0 ||
		    (req->data[22] & 15) ||
		    !IEEE80211_ADDR_EQ(req->data + 4, s->bssid) ||
		    !IEEE80211_ADDR_EQ(req->data + 10, ic->ic_myaddr) ||
		    !IEEE80211_ADDR_EQ(req->data + 16, s->bssid) ||
		    req->data[24] != 8 || req->data[25] > 1 || !sae_ptk(ic))
			return EINVAL;
		return sae_send(ic, req->data, req->len);
	default:
		return EOPNOTSUPP;
	}
}

static void
sae_aes(const void *ctx, const uint8_t in[16], uint8_t out[16])
{
	aes_enc(ctx, in, out, 10);
}

int
ieee80211_sae_input(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct mbuf **mp)
{
	struct ieee80211_sae *s = ic->ic_sae;
	struct mbuf *m = *mp;
	uint8_t frame[IEEE80211_SAE_DATA_MAX];
	struct aesenc aes;
	struct sae_igtk *igtk;
	uint64_t ipn;
	size_t len;
	bool protected, group, verified = false;

	if (!ieee80211_sae_active(ic))
		return 0;
	len = m->m_pkthdr.len;
	if (len < 24 || len > sizeof(frame))
		return -1;
	m_copydata(m, 0, len, frame);
	/* Beacon/probe handling remains available during discovery. */
	if (frame[0] == 0x80 || frame[0] == 0x50 || frame[0] == 0x40)
		return 0;
	if ((frame[0] & 15) != 0 || s->exhausted || s->hw_blocked ||
	    s->phase == SAE_RETIRED || !sae_peer(ic, ni) ||
	    !IEEE80211_ADDR_EQ(frame + 10, s->bssid) ||
	    !IEEE80211_ADDR_EQ(frame + 16, s->bssid) ||
	    (frame[1] & (IEEE80211_FC1_DIR_MASK | IEEE80211_FC1_MORE_FRAG |
	    IEEE80211_FC1_ORDER)) || (frame[22] & 15))
		return -1;
	group = IEEE80211_IS_MULTICAST(frame + 4);
	if (!group && !IEEE80211_ADDR_EQ(frame + 4, ic->ic_myaddr))
		return -1;
	if (frame[0] == 0xb0) {
		/* Raw radios preserve Retry/Power Management/More Data. These
		 * do not change the external SAE authentication envelope. */
		frame[1] &= ~0x38;
		if (s->phase != SAE_AUTH ||
		    ieee80211_sae_auth_frame_validate(frame, len, ic->ic_myaddr,
		    s->bssid, s->bssid, 0) != 0)
			return -1;
		return sae_event(ic, IEEE80211_SAE_RX_FRAME, frame, len) == 0 ? 1 : -1;
	}
	protected = (frame[1] & IEEE80211_FC1_WEP) != 0;
	if (protected) {
		/* Only the current pairwise CCMP key authenticates unicast PMF. */
		if (group || s->phase != SAE_RUN || !sae_ptk(ic) ||
		    len < (ieee80211_sae_rx_status(ic, &ni->ni_ucastkey, m) == 1 ?
		    34U : 42U) ||
		    (frame[0] != 0xa0 && frame[0] != 0xc0 && frame[0] != 0xd0) ||
		    (frame[27] & 0xc0) != 0)
			return -1;
		if (ieee80211_crypto_decap(ic, ni, mp, 24) != &ni->ni_ucastkey)
			return -1;
		m = *mp;
		len = m->m_pkthdr.len;
		m_copydata(m, 0, len, frame);
		frame[1] &= ~IEEE80211_FC1_WEP;
		mtod(m, struct ieee80211_frame *)->i_fc[1] &= ~IEEE80211_FC1_WEP;
		if (!ieee80211_is_robust_mgmt(frame, len))
			return -1;
		verified = true;
	}
	if (ieee80211_is_robust_mgmt(frame, len)) {
		if (group) {
			unsigned key_index;
			if (s->phase != SAE_RUN || len < 24 + IEEE80211_BIP_MMIE_LEN)
				return -1;
			key_index = le16dec(frame + len - IEEE80211_BIP_MMIE_LEN + 2);
			if (key_index < 4 || key_index > 5)
				return -1;
			igtk = &s->igtk[key_index - 4];
			if (!igtk->installed)
				return -1;
			aes_setenckey128(&aes, igtk->key);
			verified = ieee80211_bip_verify(sae_aes, &aes, frame, len,
			    s->bssid, s->bssid, key_index, igtk->ipn, &ipn);
			explicit_memset(&aes, 0, sizeof(aes));
			if (!verified)
				return -1;
			igtk->ipn = ipn;
			m_adj(m, -IEEE80211_BIP_MMIE_LEN);
			len -= IEEE80211_BIP_MMIE_LEN;
		}
		if (!verified) {
			if (!group && s->phase == SAE_RUN && len == 26 &&
			    (frame[0] == 0xc0 || frame[0] == 0xa0) &&
			    (le16dec(frame + 24) == 6 || le16dec(frame + 24) == 7))
				(void)sae_event(ic, IEEE80211_SAE_UNPROT_DISCONNECT, frame, len);
			return -1;
		}
		if (frame[0] == 0xd0 && frame[24] == 8) {
			if (group || len != 28 || frame[25] > 1)
				return -1;
			return sae_event(ic, IEEE80211_SAE_RX_SA_QUERY, frame, len) == 0 ? 1 : -1;
		}
		if (frame[0] == 0xc0 || frame[0] == 0xa0) {
			if (len != 26)
				return -1;
			/* The legacy handler rejects group RA and attempts open-auth
			 * reconnect. Retire through SCAN so node-leave notifies userspace. */
			(void)ieee80211_new_state(ic, IEEE80211_S_SCAN, 0);
			return 1;
		}
	} else if (frame[0] == 0xd0 && len < 26) {
		return -1;
	}
	/* No unsolicited response can complete or replace the association. */
	if ((frame[0] == 0x10 || frame[0] == 0x30) &&
	    (s->phase != SAE_ASSOC || group || protected))
		return -1;
	return 0;
}
