/* Origin: EmberBSD optional STA channel-switch quiet contract, AI-assisted. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute native WME producers and consumers; kernel services are modeled. */
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole16(x) OSSwapHostToLittleInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)
#else
#include <endian.h>
#endif
#define __packed __attribute__((__packed__))
#define IEEE80211_RATE_MAXSIZE 15
#define IEEE80211_RATE_SIZE 8
#define IEEE80211_T_FH 1
#define IEEE80211_FH_CHANSET(c) 0
#define IEEE80211_FH_CHANPAT(c) 0
#define ieee80211_chan2ieee(ic, c) \
	((c) != NULL && (c)->ic_freq == 5180 ? 36U : 1U)
#define ETHERTYPE_IP 0x0800
#define IFNAMSIZ 16
#define IEEE80211_MODE_MAX 7
#define IEEE80211_CHAN_MAX 255
#define IEEE80211_FH_CHAN(set, pat) 1
#define IEEE80211_FEXT_PROBECHAN 1
#define isclr(a, n) (((a)[(n) / 8] & (1U << ((n) % 8))) == 0)
#define IEEE80211_DISCARD(...) ((void)0)
#define IEEE80211_NO_HOSTAP 1
#define INET 1
struct ieee80211com;
#include "native-wme-types.inc"
#define IEEE80211_M_STA 1
#define IEEE80211_M_IBSS 0
#define IEEE80211_M_AHDEMO 3
#define IEEE80211_M_HOSTAP 6
#define IEEE80211_M_MONITOR 8
#define IEEE80211_MODE_11A 1
#define IEEE80211_MODE_11B 2
#define IEEE80211_MODE_11G 3
#define IEEE80211_AUTH_SHARED 2
#define IEEE80211_S_SCAN 1
#define IEEE80211_S_AUTH 2
#define IEEE80211_S_ASSOC 3
#define IEEE80211_S_RUN 4
#define IEEE80211_TRANS_WAIT 5
#ifndef IEEE80211_NODE_WME_OFFER
#define IEEE80211_NODE_WME_OFFER 0x0080
#define IEEE80211_NODE_WME_VALID 0x0100
#endif
#define M_DONTWAIT 0
#define M_DEVBUF 0
#define M_LINK0 1
#define M_MORE_DATA 2
#define M_FF 4
#define ETHERTYPE_PAE 0x888e
#define LLC_SNAP_LSAP 0xaa
#define LLC_UI 3
#define IEEE80211_CIPHER_WEP 0
#define IEEE80211_CIPHER_AES_CCM 3
#define IEEE80211_KEYIX_NONE 0xffff
#define IEEE80211_MSG_ELEMID 0
#define IEEE80211_MSG_WME 0
#define IEEE80211_ADDR_COPY(dst, src) memcpy(dst, src, 6)
#define IEEE80211_ADDR_EQ(a, b) (memcmp(a, b, 6) == 0)
#define IEEE80211_DPRINTF(...) ((void)0)
#define IEEE80211_DISCARD_IE(ic, flags, wh, ...) ((void)(wh))
#define IEEE80211_DEBUGVAR(x)
#define IEEE80211_NODE_STAT(ni, field) ((void)(ni))
#define IEEE80211_NODE_STAT_SET(ni, field, val) ((void)(ni), (void)(val))
#define IEEE80211_NODE_STAT_ADD(ni, field, val) ((void)(ni), (void)(val))
#define IASSERT(c, args) assert(c)
#define roundup(x, n) (((x) + (n) - 1) / (n) * (n))
struct ieee80211_channel {
	uint16_t ic_freq, ic_flags;
};
const struct ieee80211_channel ieee80211_channel_anyc = {0, 0};
struct test_rsn { uint8_t rsn_ucastcipher; };
struct cipher {
	unsigned ic_header, ic_trailer, ic_miclen, ic_cipher;
};
struct ieee80211_key {
	struct cipher *wk_cipher;
	unsigned wk_flags;
};
static struct cipher ieee80211_cipher_none = { .ic_cipher = IEEE80211_CIPHER_NONE };
struct ieee80211_node {
	struct ieee80211_key ni_ucastkey;
	struct ieee80211_ht20_node ni_ht20;
	struct ieee80211_phy_ies ni_phy_ies;
	struct ieee80211_rxba_state ni_rxba;
	struct ieee80211com *ni_ic;
	struct ieee80211_channel *ni_chan, *ni_wme_offer_chan,
	    *ni_wme_applied_chan;
	unsigned ni_wme_applied_slot;
	struct ieee80211_rateset ni_rates;
	uint16_t ni_flags, ni_associd, ni_vlan, ni_capinfo, ni_txseqs[17];
	unsigned ni_wme_attempt, ni_wme_offer_attempt;
	uint8_t ni_macaddr[6], ni_bssid[6], ni_wme_offer_bssid[6];
	uint8_t ni_wme_offer_type, ni_essid[32], ni_esslen, ni_erp;
	uint8_t ni_authmode, ni_fhindex, ni_dtim_count, ni_dtim_period;
	uint16_t ni_intval, ni_fhdwell, ni_txpower;
	struct test_rsn ni_rsn;
	uint32_t *ni_challenge;
	uint8_t *ni_wme_ie;
	unsigned ni_fails;
	union {
		uint8_t data[8];
	} ni_tstamp;
};
struct mbuf {
	_Alignas(8) uint8_t data[8192];
	uint8_t *m_data;
	int m_len, m_flags, ac;
	size_t model_capacity; /* Requested producer extent, independent of backing array. */
	struct {
		int len;
		struct ieee80211_node *ctx;
	} m_pkthdr;
	struct mbuf *m_next;
};
struct ifnet {
	void *if_softc;
	int if_timer, if_flags;
	unsigned if_mtu;
	uint8_t if_broadcastaddr[6];
};
struct queue {
	struct mbuf *m;
	int ifq_len, ifq_maxlen, ifq_drops;
};
struct ieee80211_node_table {
	const char *nt_name;
	unsigned nt_inact_init;
};
struct ieee80211com {
	void (*ic_sta_quiet)(struct ieee80211com *);
	int (*ic_scan_probe_admit)(struct ieee80211com *);
	struct ieee80211_key ic_nw_keys[4];
	unsigned ic_def_txkey;
	const struct ieee80211_rxba_ops *ic_rxba_ops;
	uint64_t ic_rxba_serial;
	int (*ic_ht20_admit)(struct ieee80211com *, struct ieee80211_ht20_profile *);
	int (*ic_vht_admit)(struct ieee80211com *, struct ieee80211_vht_cap *);
	int ic_fixed_rate;
	int (*ic_test_newstate)(struct ieee80211com *, int, int);
	struct ieee80211_node_table ic_sta;
	unsigned ic_inact_run;
	struct ieee80211_wme_state ic_wme;
	struct ieee80211_node *ic_bss;
	struct ieee80211_channel *ic_curchan;
	struct ifnet *ic_ifp;
	struct ieee80211_stats ic_stats;
	struct ieee80211_rateset ic_sup_rates[7];
	unsigned ic_caps, ic_flags, ic_opmode, ic_state, ic_curmode, ic_phytype;
	unsigned ic_lintval, ic_mgt_timer, ic_bmisstimeout, ic_fragthreshold, ic_rtsthreshold;
	unsigned ic_flags_ext, ic_bmiss_count, ic_sta_assoc;
	uint8_t ic_chan_active[32];
	struct ieee80211_channel ic_channels[256];
	uint8_t ic_myaddr[6];
	uint8_t ic_des_essid[32];
	unsigned ic_des_esslen;
	uint8_t *ic_opt_ie;
	unsigned ic_opt_ie_len;
	struct queue ic_mgtq;
	void (*ic_node_cleanup)(struct ieee80211_node *);
	void (*ic_updateslot)(struct ifnet *);
};
struct ether_header {
	uint8_t ether_dhost[6], ether_shost[6];
	uint16_t ether_type;
} __packed;
struct ip {
	uint8_t ip_vhl, ip_tos, rest[18];
};
struct llc {
	uint8_t llc_dsap, llc_ssap, llc_control;
	struct {
		uint8_t org_code[3];
		uint16_t ether_type;
	} __packed llc_snap;
} __packed;
#define mtod(m, t) ((t)(m)->m_data)
#define M_SETCTX(m, n) ((m)->m_pkthdr.ctx = (n))
#define M_WME_SETAC(m, value) ((m)->ac = (value))
#define M_WME_GETAC(m) ((m)->ac)
#define IF_ENQUEUE(q, m) do { if (during_enqueue) during_enqueue(); \
	(q)->ifq_len++; (q)->m = (m); } while (0)
#define IF_DROP(q) ((q)->ifq_drops++)
#define IEEE80211_IS_CHAN_2GHZ(c) ((c)->ic_freq < 3000)
#define IEEE80211_IS_CHAN_FHSS(c) 0
#define EVL_VLANOFTAG(t) ((t) & 0xfff)
#define EVL_PRIOFTAG(t) (((t) >> 13) & 7)
#define vlan_has_tag(m) 1
#define vlan_get_tag(m) 1
#define ieee80211_crypto_getmcastkey(...) NULL
#define ieee80211_crypto_enmic(...) 1
#define ieee80211_fragment(...) 1
#define ieee80211_add_wpa(frm, ...) (frm)
#define ieee80211_add_erp(frm, ...) (frm)
#define ieee80211_add_ath(frm, ...) (frm)

#define ieee80211_notify_node_leave(...) ((void)0)
static unsigned checks, failures, allocations, fail_allocation, prepends,
    fail_prepend;
static unsigned node_refs = 1, applies, adjust_failure;
static struct ieee80211_node *last_freed;
static struct ieee80211com *active_ic;
static struct mbuf *captured;
static void (*during_start)(void);
static void (*during_enqueue)(void);
static void (*during_prepend)(struct mbuf *);
#define CHECK(x)                                                               \
	do {                                                                   \
		checks++;                                                      \
		if (!(x)) {                                                    \
			failures++;                                            \
			fprintf(stderr, "%d: %s\n", __LINE__, #x);             \
		}                                                              \
	} while (0)
static void
m_freem(struct mbuf *m)
{
	while (m) {
		struct mbuf *next = m->m_next;
		free(m);
		m = next;
	}
}
static struct mbuf *
newbuf(size_t len)
{
	struct mbuf *m;
	if (++allocations == fail_allocation)
		return NULL;
	m = calloc(1, sizeof(*m));
	assert(m != NULL);
	m->m_data = m->data + 128;
	m->m_len = (int)len;
	m->model_capacity = len;
	m->m_pkthdr.len = (int)len;
	return m;
}
static struct mbuf *
prepend(struct mbuf *m, int len)
{
	if (++prepends == fail_prepend) {
		m_freem(m);
		return NULL;
	}
	assert(m->m_data - m->data >= len);
	m->m_data -= len;
	m->m_len += len;
	m->model_capacity += len;
	m->m_pkthdr.len += len;
	if (during_prepend) during_prepend(m);
	return m;
}
#define M_PREPEND(m, n, how) ((m) = prepend(m, n))
static void
m_adj(struct mbuf *m, int len)
{
	assert(len >= 0 && len <= m->m_len);
	m->m_data += len;
	m->m_len -= len;
	m->m_pkthdr.len -= len;
}
static void
m_copydata(struct mbuf *m, int off, int len, void *dst)
{
	assert(off >= 0 && len >= 0 && off + len <= m->m_pkthdr.len);
	while (m && off >= m->m_len) {
		off -= m->m_len;
		m = m->m_next;
	}
	while (len) {
		int n;
		assert(m);
		n = m->m_len - off;
		if (n > len)
			n = len;
		memcpy(dst, m->m_data + off, (size_t)n);
		dst = (uint8_t *)dst + n;
		len -= n;
		m = m->m_next;
		off = 0;
	}
}
static struct mbuf *
ieee80211_getmgtframe(uint8_t **frm, int len)
{
	struct mbuf *m = newbuf((size_t)len);
	if (m)
		*frm = m->m_data;
	return m;
}
static struct mbuf *
ieee80211_mbuf_adjust(struct ieee80211com *ic, int hdr,
		      struct ieee80211_key *key, struct mbuf *m)
{
	struct mbuf *copy;
	(void)ic;
	(void)hdr;
	(void)key;
	if (adjust_failure) {
		m_freem(m);
		return NULL;
	}
	/* Model the documented relocation outcome, including allocation
	 * failure. */
	copy = malloc(sizeof(*copy));
	if (!copy) {
		m_freem(m);
		return NULL;
	}
	memcpy(copy, m, sizeof(*copy));
	copy->m_data = copy->data + (m->m_data - m->data);
	free(m);
	return copy;
}
static struct ieee80211_node *
ieee80211_ref_node(struct ieee80211_node *ni)
{
	node_refs++;
	return ni;
}
static void
ieee80211_free_node(struct ieee80211_node *ni)
{
	last_freed = ni;
	assert(node_refs > 1);
	node_refs--;
}
static void
if_start_lock(struct ifnet *ifp)
{
	(void)ifp;
	assert(captured == NULL);
	captured = active_ic->ic_mgtq.m;
	CHECK(captured != NULL && captured->m_len >= 0 &&
	    (size_t)captured->m_len <= captured->model_capacity);
	active_ic->ic_mgtq.m = NULL;
	active_ic->ic_mgtq.ifq_len--;
	if (during_start) {
		void (*callback)(void) = during_start;
		during_start = NULL;
		callback();
	}
}
static int apply_error, beacon_lock, expect_uncommitted;
static void (*during_apply)(struct ieee80211com *);
static struct chanAccParams applied_params;
static unsigned applied_slot;
#define IEEE80211_BEACON_LOCK(ic)                                              \
	do {                                                                   \
		(void)(ic);                                                    \
		assert(!beacon_lock);                                          \
		beacon_lock = 1;                                               \
	} while (0)
#define IEEE80211_BEACON_UNLOCK(ic)                                            \
	do {                                                                   \
		(void)(ic);                                                    \
		assert(beacon_lock);                                           \
		beacon_lock = 0;                                               \
	} while (0)
static int
legacy_update(struct ieee80211com *ic)
{
	applies++;
	applied_params = ic->ic_wme.wme_chanParams;
	return apply_error;
}
static int
strict_update(struct ieee80211com *ic, const struct chanAccParams *params,
	      unsigned slot)
{
	assert(beacon_lock);
	if (expect_uncommitted) {
		CHECK((ic->ic_bss->ni_flags &
		    (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)) == 0);
		CHECK(ic->ic_wme.wme_wmeChanParams.cap_info == 7);
		expect_uncommitted = 0;
	}
	applies++;
	applied_params = *params;
	applied_slot = slot;
	if (during_apply != NULL)
		during_apply(ic);
	return apply_error;
}
static void
ieee80211_node_unauthorize(struct ieee80211_node *ni)
{
	(void)ni;
}
static int
ieee80211_new_state(struct ieee80211com *ic, int state, int arg)
{
	(void)arg;
	assert(!beacon_lock);
	if (ic->ic_test_newstate)
		return ic->ic_test_newstate(ic, state, arg);
	ic->ic_state = (unsigned)state;
	return 0;
}
static uint8_t
ieee80211_setup_rates(struct ieee80211_node *ni, uint8_t *rates,
		      uint8_t *xrates, int flags)
{
	(void)ni;
	(void)rates;
	(void)xrates;
	(void)flags;
	return 2;
}
#define IEEE80211_R_DOSORT 1
#define IEEE80211_R_DOFRATE 2
#define IEEE80211_R_DONEGO 4
#define IEEE80211_R_DODEL 8
#define IEEE80211_VERIFY_LENGTH(have, need)                                    \
	do {                                                                   \
		if ((have) < (need)) {                                         \
			ic->ic_stats.is_rx_tooshort++;                         \
			return;                                                \
		}                                                              \
	} while (0)
#define IEEE80211_VERIFY_ELEMENT(ie, max)                                      \
	do {                                                                   \
		if ((ie) == NULL || (ie)[1] > (max))                           \
			return;                                                \
	} while (0)
static struct ieee80211_node *
ieee80211_refine_node_for_beacon(struct ieee80211com *ic,
				 struct ieee80211_node *ni,
				 struct ieee80211_channel *c, uint8_t *ssid)
{
	(void)ic;
	(void)c;
	(void)ssid;
	return ni;
}
static void
ieee80211_add_scan(struct ieee80211com *ic,
		   const struct ieee80211_scanparams *sp,
		   const struct ieee80211_frame *wh, int type, int rssi,
		   uint32_t stamp)
{
	(void)ic;
	(void)sp;
	(void)wh;
	(void)type;
	(void)rssi;
	(void)stamp;
}
static void
ieee80211_update_adhoc_node(struct ieee80211com *ic, struct ieee80211_node *ni,
			    struct ieee80211_frame *wh,
			    struct ieee80211_scanparams *sp, int rssi,
			    uint32_t stamp)
{
	(void)ic;
	(void)ni;
	(void)wh;
	(void)sp;
	(void)rssi;
	(void)stamp;
}
static void ieee80211_probe_curchan(struct ieee80211com *, int);
#define IEEE80211_NODE_LOCK(nt) ((void)(nt))
#define IEEE80211_NODE_UNLOCK(nt) ((void)(nt))
#define ieee80211_fix_rate(ni, flags) ((void)(ni), (void)(flags))
#define ieee80211_chan2mode(ic, chan)                                          \
	((void)(ic), (void)(chan), IEEE80211_MODE_11G)
#define ieee80211_reset_erp(ic) ((void)(ic))
#include "native-rxba-lifecycle.inc"
/* Unexpected BA Actions must fail the legacy/WME fixture, not succeed through
 * a dummy parser. Active BA wire/crypto runs in the dedicated full B fixture. */
void ieee80211_rxba_age(struct ieee80211com *ic,struct ieee80211_node *ni,uint64_t token)
{ (void)ic;(void)ni;(void)token;abort(); }
int ieee80211_rxba_action(struct ieee80211com *ic,struct ieee80211_node *ni,const uint8_t *p,size_t len)
{ (void)ic;(void)ni;(void)p;(void)len;abort(); }
#include "native-wme.inc"

static struct ieee80211com test_ic;
static struct ieee80211_node test_node;
static struct ieee80211_channel test_channel = {2412, 0};
static struct ifnet test_ifp;
static uint8_t observed_wme = 1;

static void
release_capture(void)
{
	if (captured) {
		ieee80211_free_node(captured->m_pkthdr.ctx);
		m_freem(captured);
		captured = NULL;
	}
}
static void
cleanup_node(struct ieee80211_node *ni)
{
	(void)ni;
}
static void
setup(void)
{
	int ac;
	release_capture();
	assert(node_refs == 1);
	memset(&test_ic, 0, sizeof(test_ic));
	memset(&test_node, 0, sizeof(test_node));
	test_node.ni_ucastkey.wk_cipher = &ieee80211_cipher_none;
	test_ic.ic_def_txkey = IEEE80211_KEYIX_NONE;
	for (unsigned i = 0; i < 4; i++)
		test_ic.ic_nw_keys[i].wk_cipher = &ieee80211_cipher_none;
	test_ic.ic_bss = &test_node;
	test_ic.ic_curchan = &test_channel;
	test_ic.ic_ifp = &test_ifp;
	test_ic.ic_opmode = IEEE80211_M_STA;
	test_ic.ic_state = IEEE80211_S_ASSOC;
	test_ic.ic_caps = IEEE80211_C_WME;
	test_ic.ic_flags = IEEE80211_F_WME;
	test_ic.ic_curmode = IEEE80211_MODE_11G;
	test_ic.ic_fragthreshold = 4096;
	test_ic.ic_node_cleanup = cleanup_node;
	memset(test_ic.ic_chan_active, 255, sizeof(test_ic.ic_chan_active));
	test_node.ni_ic = &test_ic;
	test_node.ni_chan = &test_channel;
	test_node.ni_wme_ie = &observed_wme;
	memset(test_node.ni_macaddr, 0x22, 6);
	memset(test_node.ni_bssid, 0x22, 6);
	memset(test_ic.ic_myaddr, 0x44, 6);
	test_node.ni_rates.rs_nrates = 1;
	test_node.ni_rates.rs_rates[0] = 2;
	for (ac = 0; ac < 4; ac++) {
		struct wmeParams p = {0, 2, 3, 4, 47, 0};
		test_ic.ic_wme.wme_bssChanParams.cap_wmeParams[ac] = p;
		test_ic.ic_wme.wme_wmeBssChanParams.cap_wmeParams[ac] = p;
	}
	during_start = NULL;
	during_enqueue = NULL;
	during_prepend = NULL;
	during_apply = NULL;
	adjust_failure = 0;
	active_ic = &test_ic;
	applies = 0;
	apply_error = 0;
	expect_uncommitted = 0;
	test_ic.ic_wme.wme_update = legacy_update;
	allocations = prepends = fail_allocation = fail_prepend = 0;
}
static struct mbuf *
response(int include_wme, int status, int subtype)
{
	struct mbuf *m = newbuf(sizeof(struct ieee80211_frame) + 6 + 3 +
				(include_wme ? 26 : 0));
	struct ieee80211_frame *wh;
	uint8_t *p;
	assert(m);
	wh = mtod(m, struct ieee80211_frame *);
	memset(wh, 0, sizeof(*wh));
	wh->i_fc[0] = (uint8_t)(IEEE80211_FC0_TYPE_MGT | subtype);
	memcpy(wh->i_addr1, test_ic.ic_myaddr, 6);
	memcpy(wh->i_addr2, test_node.ni_macaddr, 6);
	memcpy(wh->i_addr3, test_node.ni_bssid, 6);
	p = (uint8_t *)(wh + 1);
	p[0] = IEEE80211_CAPINFO_ESS;
	p[2] = (uint8_t)status;
	p[4] = 1;
	p += 6;
	*p++ = IEEE80211_ELEMID_RATES;
	*p++ = 1;
	*p++ = 2;
	if (include_wme)
		CHECK(ieee80211_add_wme_param(p, &test_ic.ic_wme) ==
		      m->m_data + m->m_len);
	return m;
}
static void
deliver(struct mbuf *m, int subtype)
{
	ieee80211_recv_mgmt_assoc_resp(&test_ic, m, &test_node, subtype, 0, 0);
	m_freem(m);
}
static uint8_t *
response_ie(struct mbuf *m)
{
	return m->m_data + sizeof(struct ieee80211_frame) + 9;
}
static int
request(int subtype)
{
	return ieee80211_send_mgmt(&test_ic, &test_node, subtype, 0);
}
static bool
offered_on_wire(int subtype)
{
	const uint8_t *p, *end;
	assert(captured);
	p = captured->m_data + sizeof(struct ieee80211_frame) +
	    (subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ ? 10 : 4);
	end = captured->m_data + captured->m_len;
	while (end - p >= 2) {
		if ((size_t)(end - p) < (size_t)p[1] + 2)
			return false;
		if (p[0] == IEEE80211_ELEMID_VENDOR && p[1] == 7 &&
		    p[6] == WME_INFO_OUI_SUBTYPE && p[7] == WME_VERSION)
			return true;
		p += p[1] + 2;
	}
	return false;
}
static const uint8_t *
request_ie(int subtype, uint8_t element, unsigned *matches)
{
	const uint8_t *p, *end, *found = NULL;
	size_t fixed = sizeof(struct ieee80211_frame) + 4 +
	    (subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ ? 6 : 0);

	assert(captured && matches != NULL);
	*matches = 0;
	p = captured->m_data + fixed;
	end = captured->m_data + captured->m_len;
	while (p < end) {
		if ((size_t)(end - p) < 2 || p[1] > (size_t)(end - p) - 2)
			return NULL;
		if (p[0] == element) {
			(*matches)++;
			found = p;
		}
		p += p[1] + 2;
	}
	return found;
}
static void
before_association_enqueue(void)
{
	CHECK(!(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING));
}

static void
test_pending_lifetime(void)
{
	for (unsigned legacy = 0; legacy < 2; legacy++) {
		for (unsigned reassoc = 0; reassoc < 2; reassoc++) {
			setup();
			if (legacy) test_ic.ic_flags &= ~IEEE80211_F_WME;
			test_node.ni_wme_attempt = UINT32_MAX;
			during_enqueue = before_association_enqueue;
			int req = reassoc ? IEEE80211_FC0_SUBTYPE_REASSOC_REQ :
			    IEEE80211_FC0_SUBTYPE_ASSOC_REQ;
			CHECK(request(req) == 0);
			CHECK(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING);
			CHECK(!!(test_node.ni_flags & IEEE80211_NODE_WME_OFFER) == !legacy);
			CHECK(test_node.ni_wme_attempt == 0 && test_node.ni_wme_offer_attempt == 0);
			CHECK(test_node.ni_wme_offer_chan == &test_channel &&
			    test_node.ni_wme_offer_type == req &&
			    IEEE80211_ADDR_EQ(test_node.ni_wme_offer_bssid, test_node.ni_bssid));
			release_capture();
			deliver(response(1, 1, req + 0x10), req + 0x10);
			CHECK(!(test_node.ni_flags & (IEEE80211_NODE_ASSOC_PENDING | IEEE80211_NODE_WME_OFFER)));
			CHECK(request(req) == 0);
			CHECK(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING);
			ieee80211_wme_reset(&test_node);
			CHECK(!(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING));
			release_capture();
		}
	}
	for (unsigned type = 0; type < 2; type++) {
		setup();
		CHECK(request(type ? IEEE80211_FC0_SUBTYPE_PROBE_RESP :
		    IEEE80211_FC0_SUBTYPE_AUTH) == 0);
		CHECK(!(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING));
		release_capture();
	}
	setup();
	CHECK(ieee80211_send_probereq(&test_node, test_ic.ic_myaddr,
	    test_node.ni_macaddr, test_node.ni_bssid, test_ic.ic_des_essid,
	    0, NULL, 0) == 0);
	CHECK(!(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING));
	release_capture();
	/* Preserve the existing unbounded legacy queue policy. */
	setup(); test_ic.ic_flags &= ~IEEE80211_F_WME;
	test_ic.ic_mgtq.ifq_len = test_ic.ic_mgtq.ifq_maxlen = 1;
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
	CHECK(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING);
	CHECK(test_ic.ic_mgtq.ifq_drops == 0);
	release_capture();
}

static void
test_exchange(void)
{
	int variant;
	setup();
	deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
		IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK((test_node.ni_flags & IEEE80211_NODE_QOS) == 0);
	CHECK(applies == 0);
	setup();
	test_ic.ic_caps = 0;
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
	CHECK(!offered_on_wire(IEEE80211_FC0_SUBTYPE_ASSOC_REQ));
	deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
		IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK((test_node.ni_flags & IEEE80211_NODE_QOS) == 0);
	CHECK(applies == 0);
	for (variant = 0; variant < 2; variant++) {
		int req = variant ? IEEE80211_FC0_SUBTYPE_REASSOC_REQ
				  : IEEE80211_FC0_SUBTYPE_ASSOC_REQ;
		int resp = variant ? IEEE80211_FC0_SUBTYPE_REASSOC_RESP
				   : IEEE80211_FC0_SUBTYPE_ASSOC_RESP;
		setup();
		CHECK(request(req) == 0);
		CHECK(offered_on_wire(req));
		deliver(response(1, 0, resp), resp);
		CHECK(test_node.ni_flags & IEEE80211_NODE_QOS);
		CHECK(test_ic.ic_state == IEEE80211_S_RUN);
		CHECK(applies == 1);
		CHECK(test_ic.ic_wme.wme_wmeChanParams.cap_wmeParams[3]
			  .wmep_aifsn == 2);
		CHECK(test_ic.ic_wme.wme_wmeChanParams.cap_wmeParams[3]
			  .wmep_txopLimit == 47);
	}
	for (variant = 0; variant < 7; variant++) {
		struct mbuf *m;
		uint8_t *p;
		setup();
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		m = response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		p = response_ie(m);
		switch (variant) {
		case 0:
			p[7] = 2;
			break; /* Unsupported WME version. */
		case 1:
			p[6] = WME_INFO_OUI_SUBTYPE;
			break;
		case 2:
			p[22] = p[10];
			break; /* Duplicate ACI, missing VO. */
		case 3:
			p[23] = 0x34;
			break; /* ECWmin exceeds ECWmax. */
		case 4:
			p[22] &= 0xf0;
			break; /* Invalid final AIFSN. */
		case 5:
			p[9] = 1;
			break; /* Reserved octet. */
		case 6:
			p[5] = 0xff;
			break; /* Unrecognized vendor type, exercises native
				  LE_READ_4. */
		}
		deliver(m, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK((test_node.ni_flags & IEEE80211_NODE_QOS) == 0);
		CHECK(applies == 0);
	}
	for (variant = 0; variant < 3; variant++) {
		setup();
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		release_capture();
		if (variant == 0) {
			fail_allocation = allocations + 1;
			CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) ==
			      ENOMEM);
			fail_allocation = 0;
		} else if (variant == 1) {
			fail_prepend = prepends + 1;
			CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) ==
			      ENOMEM);
			fail_prepend = 0;
		} else {
			ieee80211_sta_leave(&test_ic, &test_node);
		}
		deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
			IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK((test_node.ni_flags & IEEE80211_NODE_QOS) == 0);
		CHECK(applies == 0);
	}
	release_capture();
}
static void
test_parser(void)
{
	struct mbuf *m;
	uint8_t original[26], *p;
	struct chanAccParams saved;
	int bad;
	setup();
	m = response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	p = response_ie(m);
	memcpy(original, p, 26);
	CHECK(ieee80211_parse_wmeparams(&test_ic, p, NULL) == 1);
	CHECK(test_ic.ic_wme.wme_wmeChanParams.cap_wmeParams[3].wmep_aifsn ==
	      2);
	saved = test_ic.ic_wme.wme_wmeChanParams;
	for (bad = 0; bad < 4; bad++) {
		memcpy(p, original, 26);
		p[8] = 1;
		if (bad == 0)
			p[7] = 2;
		else if (bad == 1)
			p[22] = p[10];
		else if (bad == 2)
			p[23] = 0x34;
		else
			p[9] = 1;
		CHECK(ieee80211_parse_wmeparams(&test_ic, p, NULL) < 0);
		CHECK(memcmp(&saved, &test_ic.ic_wme.wme_wmeChanParams,
			     sizeof(saved)) == 0);
	}
	m_freem(m);
}
static void
immediate_response(void)
{
	CHECK(offered_on_wire(IEEE80211_FC0_SUBTYPE_ASSOC_REQ));
	CHECK(test_node.ni_flags & IEEE80211_NODE_ASSOC_PENDING);
	deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
		IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
}
static void
test_lifetime(void)
{
	int variant;
	setup();
	during_start = immediate_response;
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
	CHECK(test_node.ni_flags & IEEE80211_NODE_QOS);
	CHECK(applies == 1);
	for (variant = 0; variant < 3; variant++) {
		struct mbuf *m;
		struct ieee80211_frame *wh;
		struct ieee80211_channel other = {5180, 0};
		setup();
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		m = response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		wh = mtod(m, struct ieee80211_frame *);
		if (variant == 0)
			wh->i_addr2[0] ^= 2;
		else if (variant == 1)
			test_ic.ic_curchan = &other;
		deliver(m, variant == 2 ? IEEE80211_FC0_SUBTYPE_REASSOC_RESP
					: IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK(test_ic.ic_state == IEEE80211_S_ASSOC);
		CHECK(applies == 0);
		CHECK((test_node.ni_flags & IEEE80211_NODE_QOS) == 0);
		test_ic.ic_curchan = &test_channel;
		deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
			IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK(test_node.ni_flags & IEEE80211_NODE_QOS);
		CHECK(applies == 1);
	}
	for (variant = 0; variant < 5; variant++) {
		struct mbuf *m;
		setup();
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		release_capture();
		m = response(1, variant == 0 ? 1 : 0,
			     IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		if (variant == 1)
			m->m_len = m->m_pkthdr.len =
			    sizeof(struct ieee80211_frame) + 5;
		else if (variant == 2) {
			response_ie(m)[1] = 25;
			m->m_len++;
			m->m_pkthdr.len++;
		} else if (variant == 3) {
			memcpy(response_ie(m) + 26, response_ie(m), 26);
			m->m_len += 26;
			m->m_pkthdr.len += 26;
		} else if (variant == 4) {
			response_ie(m)[1] = 27;
		}
		deliver(m, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK((test_node.ni_flags & IEEE80211_NODE_QOS) == 0);
		CHECK(applies == 0);
		test_ic.ic_state = IEEE80211_S_ASSOC;
		deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
			IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK((test_node.ni_flags & IEEE80211_NODE_QOS) == 0);
		CHECK(applies == 0);
	}
	setup();
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
	release_capture();
	test_ic.ic_state = IEEE80211_S_AUTH;
	CHECK(request(IEEE80211_FC0_SUBTYPE_AUTH) == 0);
	release_capture();
	test_ic.ic_state = IEEE80211_S_ASSOC;
	deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
		IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK((test_node.ni_flags & IEEE80211_NODE_QOS) == 0);
	CHECK(applies == 0);
	release_capture();
}
static void
test_request_ie(void)
{
	uint8_t opt[9] = {221, 7, 0, 0x50, 0xf2, 2, 0, 1, 0};
	int variant;
	for (variant = 0; variant < 3; variant++) {
		setup();
		test_ic.ic_opt_ie = opt;
		test_ic.ic_opt_ie_len = variant == 1 ? 8 : 9;
		opt[7] = variant == 2 ? 2 : 1;
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
			IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK((test_node.ni_flags & IEEE80211_NODE_QOS) == 0);
		CHECK(applies == 0);
	}
	release_capture();
}
static uint64_t
vht_now_ms(struct ieee80211com *ic)
{
	(void)ic;
	return 1;
}
static int
vht_schedule(struct ieee80211com *ic, uint64_t deadline, uint64_t token)
{
	(void)ic;
	(void)deadline;
	(void)token;
	return 0;
}
static void
vht_cancel(struct ieee80211com *ic)
{
	(void)ic;
}
static int
vht_ht20_admit(struct ieee80211com *ic, struct ieee80211_ht20_profile *p)
{
	(void)ic;
	p->generation = 1;
	p->rx_mcs = p->tx_mcs = 0xff;
	p->ready = IEEE80211_HT20_READY_ALL;
	return 0;
}
static int
vht_local_admit(struct ieee80211com *ic, struct ieee80211_vht_cap *cap)
{
	(void)ic;
	*cap = (struct ieee80211_vht_cap){
		.info = 0, .rx_map = 0xfffc, .rx_highest = 0,
		.tx_map = 0xfffc, .tx_highest = 0
	};
	return 0;
}
static int
vht_bad_local_admit(struct ieee80211com *ic, struct ieee80211_vht_cap *cap)
{
	int error = vht_local_admit(ic, cap);
	cap->info = 1; /* Advertise an unqualified optional capability. */
	return error;
}
static void
vht_peer_candidate(struct ieee80211_node *ni)
{
	uint8_t raw[IEEE80211_PHY_IES_MAXLEN] = {0};
	uint8_t *p = raw;

	p[0] = 45; p[1] = 26; p[2] = 0x02;	/* HT20 plus 40 MHz support. */
	p[5] = 0xff;					/* HT MCS0-7. */
	p[28] = 61; p[29] = 22; p[30] = 36; p[31] = 0x05;	/* 36/40-above. */
	p[52] = 191; p[53] = 12;
	p[58] = 0xfc; p[59] = 0xff;		/* Peer RX MCS0-7, NSS1. */
	p[62] = 0xfc; p[63] = 0xff;		/* Peer TX MCS0-7, NSS1. */
	p[66] = 192; p[67] = 5; p[68] = 1; p[69] = 42;
	p[71] = 0xfc; p[72] = 0xff;		/* Basic MCS0-7, NSS1. */
	CHECK(ieee80211_phy_ies_parse(&ni->ni_phy_ies, raw, sizeof(raw)) == 0);
}
static int
vht_wme_apply(struct ieee80211com *ic, const struct chanAccParams *params,
	unsigned slot)
{
	(void)ic;
	(void)params;
	(void)slot;
	return 0;
}
static const struct ieee80211_rxba_ops vht_rxba_ops = {
	.now_ms = vht_now_ms,
	.schedule = vht_schedule,
	.cancel = vht_cancel
};
static void
prepare_vht_request(void)
{
	setup();
	test_ic.ic_channels[36].ic_freq = 5180;
	test_ic.ic_curchan = &test_ic.ic_channels[36];
	test_node.ni_chan = test_ic.ic_curchan;
	CHECK(ieee80211_chan2ieee(&test_ic, test_ic.ic_curchan) == 36);
	test_ic.ic_curmode = IEEE80211_MODE_11A;
	test_node.ni_rates.rs_rates[0] = 0x82;
	test_ic.ic_sup_rates[IEEE80211_MODE_11A].rs_nrates = 1;
	test_ic.ic_sup_rates[IEEE80211_MODE_11A].rs_rates[0] = 0x82;
	vht_peer_candidate(&test_node);
	test_ic.ic_ht20_admit = vht_ht20_admit;
	test_ic.ic_rxba_ops = &vht_rxba_ops;
	test_ic.ic_wme.wme_sta_apply = vht_wme_apply;
	test_ic.ic_vht_admit = vht_local_admit;
}
static void
test_vht_request_bytes(void)
{
	static const uint8_t expected_cap[12] = {
		0, 0, 0, 0, 0xfc, 0xff, 0, 0, 0xfc, 0xff, 0, 0
	};
	const uint8_t *ht, *vht;
	unsigned count;

	test_channel.ic_freq = 5180;
	setup();
	test_ic.ic_vht_admit = NULL;
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
	(void)request_ie(IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 191, &count);
	CHECK(count == 0);
	release_capture();

	prepare_vht_request();
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
	ht = request_ie(IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 45, &count);
	CHECK(count == 1 && ht != NULL);
	if (ht != NULL)
		CHECK((ht[2] & 0x02) != 0);
	vht = request_ie(IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 191, &count);
	CHECK(count == 1 && vht != NULL);
	if (vht != NULL) {
		CHECK(vht[1] == sizeof(expected_cap));
		CHECK(memcmp(vht + 2, expected_cap, sizeof(expected_cap)) == 0);
	}
	release_capture();
	for (unsigned variant = 0; variant < 10; variant++) {
		prepare_vht_request();
		switch (variant) {
		case 0: test_node.ni_phy_ies.data[30] = 40; break;	/* Primary. */
		case 1: test_node.ni_phy_ies.data[31] = 0x06; break;	/* HT below. */
		case 2: test_node.ni_phy_ies.data[68] = 0; break;	/* 20/40 only. */
		case 3: test_node.ni_phy_ies.data[69] = 46; break;	/* Center. */
		case 4:
			test_node.ni_phy_ies.data[58] = 0xff;
			test_node.ni_phy_ies.data[59] = 0xff;
			break;						/* Peer RX unsupported. */
		case 5:
			test_node.ni_phy_ies.data[62] = 0xff;
			test_node.ni_phy_ies.data[63] = 0xff;
			break;						/* Peer TX unsupported. */
		case 6:
			test_node.ni_phy_ies.data[71] = 0xfd;
			break;						/* Basic map exceeds local. */
		case 7: test_node.ni_rates.rs_rates[0] = 2; break;	/* No basic rate. */
		case 8: test_ic.ic_vht_admit = vht_bad_local_admit; break;
		case 9: test_node.ni_phy_ies.data[5] = 0; break;	/* No HT MCS0-7. */
		}
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		vht = request_ie(IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 191, &count);
		CHECK(count == 0 && vht == NULL);
		ht = request_ie(IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 45, &count);
		CHECK(count == 1 && ht != NULL);
		if (ht != NULL)
			CHECK((ht[2] & 0x02) == 0);
		release_capture();
	}
	test_channel.ic_freq = 2412;
}
static struct mbuf *
vht_assoc_response(unsigned variant)
{
	uint8_t ies[96] = {0};
	struct mbuf *m;
	size_t len = 73;
	uint8_t *cap, *op;

	ies[0] = 45; ies[1] = 26; ies[2] = 0x02; /* HT40 support. */
	ies[5] = 0xff; /* HT MCS0-7. */
	ies[28] = 61; ies[29] = 22; ies[30] = 36;
	ies[31] = 0x05; /* Primary36, HT40 above. */
	cap = ies + 52;
	cap[0] = 191; cap[1] = IEEE80211_VHT_CAP_LEN;
	cap[6] = 0xfd; cap[7] = 0xff; /* Peer RX MCS0-8, NSS1. */
	cap[10] = 0xfe; cap[11] = 0xff; /* Peer TX MCS0-9, NSS1. */
	op = cap + 14;
	op[0] = 192; op[1] = IEEE80211_VHT_OPERATION_LEN;
	op[2] = 1; op[3] = 42; /* VHT80 center segment. */
	op[5] = 0xfc; op[6] = 0xff; /* Basic MCS0-7, NSS1. */
	switch (variant) {
	case 1:
		len = 52; /* No VHT response; HT fallback remains available. */
		break;
	case 2:
		op[3] = 46; /* Wrong center. */
		break;
	case 3:
		cap[6] = cap[7] = 0xff; /* Peer cannot receive VHT MCS. */
		break;
	case 4:
		op[5] = 0xfd; /* AP requires unsupported MCS8. */
		break;
	case 5:
		memcpy(ies + len, cap, 14); /* Duplicate capability. */
		len += 14;
		break;
	case 6:
		memcpy(ies + len, op, 7); /* Duplicate operation. */
		len += 7;
		break;
	case 7:
		/* A complete but wrong-length capability IE, followed by VHT Op. */
		memmove(ies + 65, op, 7);
		cap[1] = IEEE80211_VHT_CAP_LEN - 1;
		len--;
		break;
	case 8:
		op[1]++; /* Truncated final operation IE. */
		break;
	default:
		break;
	}
	m = response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	assert(m != NULL && (size_t)m->m_len + len < sizeof(m->data) - 128);
	memcpy(m->m_data + m->m_len, ies, len);
	m->m_len += (int)len;
	m->m_pkthdr.len = m->m_len;
	m->model_capacity += len;
	return m;
}
static void
test_vht_assoc_response(void)
{
	struct ieee80211_channel other = {5200, 0};
	struct mbuf *m;
	struct ieee80211_frame *wh;
	unsigned variant;

	prepare_vht_request();
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
	m = vht_assoc_response(0);
	/* The full response IE window includes ordinary Rates/WME alongside PHY. */
	CHECK((size_t)(m->m_data + m->m_len - response_ie(m)) >
	    IEEE80211_PHY_IES_MAXLEN);
	deliver(m, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(test_ic.ic_state == IEEE80211_S_RUN);
	CHECK(test_node.ni_ht20.valid);
	CHECK(test_node.ni_ht20.vht.valid);
	CHECK(test_node.ni_ht20.vht.peer.rx_map == 0xfffd);
	CHECK(test_node.ni_ht20.vht.peer.tx_map == 0xfffe);
	CHECK(test_node.ni_ht20.vht.maps.tx_map == 0xfffc);
	CHECK(test_node.ni_ht20.vht.maps.rx_map == 0xfffc);
	CHECK(test_node.ni_ht20.vht.operation.width == 1);
	CHECK(test_node.ni_ht20.vht.operation.seg0 == 42);

	/* Unsupported and malformed VHT responses retain HT20 where framing is
	 * still valid, and a malformed TLV cannot leave negotiated state behind. */
	for (variant = 1; variant <= 8; variant++) {
		prepare_vht_request();
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		deliver(vht_assoc_response(variant),
		    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK(!test_node.ni_ht20.vht.valid);
		if (variant == 8) {
			CHECK(!test_node.ni_ht20.valid);
			CHECK(test_ic.ic_state == IEEE80211_S_SCAN);
		}
		else {
			CHECK(test_node.ni_ht20.valid);
			CHECK(test_ic.ic_state == IEEE80211_S_RUN);
		}
	}

	/* The offer is bound to the queued association's peer and attempt. */
	for (variant = 0; variant < 4; variant++) {
		prepare_vht_request();
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		m = vht_assoc_response(0);
		wh = mtod(m, struct ieee80211_frame *);
		if (variant == 0)
			wh->i_addr2[5] ^= 1;
		else if (variant == 1)
			test_node.ni_wme_attempt++;
		else if (variant == 2)
			test_ic.ic_curchan = &other;
		deliver(m, variant == 3 ? IEEE80211_FC0_SUBTYPE_REASSOC_RESP :
		    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK(test_ic.ic_state == IEEE80211_S_ASSOC);
		CHECK(!test_node.ni_ht20.vht.valid);
	}

	/* A changed local capability invalidates VHT but retains HT20. */
	prepare_vht_request();
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
	test_ic.ic_vht_admit = vht_bad_local_admit;
	deliver(vht_assoc_response(0), IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(test_node.ni_ht20.valid);
	CHECK(!test_node.ni_ht20.vht.valid);
	test_channel.ic_freq = 2412;
}
static void
test_parser_boundaries(void)
{
	struct mbuf *m;
	uint8_t base[26], *p;
	unsigned len, iteration;
	uint32_t random = 0x715ac;
	struct chanAccParams saved;
	setup();
	m = response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	p = response_ie(m);
	memcpy(base, p, 26);
	for (len = 0; len < 256; len++) {
		memcpy(p, base, 26);
		p[1] = (uint8_t)len;
		saved = test_ic.ic_wme.wme_wmeChanParams;
		int result = ieee80211_parse_wmeparams(&test_ic, p, NULL);
		CHECK(len == 24 ? result >= 0 : result < 0);
		if (result < 0)
			CHECK(memcmp(&saved, &test_ic.ic_wme.wme_wmeChanParams,
				     sizeof(saved)) == 0);
	}
	for (iteration = 0; iteration < 10000; iteration++) {
		unsigned i;
		memcpy(p, base, 26);
		for (i = 8; i < 26; i++) {
			random ^= random << 13;
			random ^= random >> 17;
			random ^= random << 5;
			p[i] = (uint8_t)random;
		}
		p[8] &= 0x8f;
		p[9] = 0;
		saved = test_ic.ic_wme.wme_wmeChanParams;
		if (ieee80211_parse_wmeparams(&test_ic, p, NULL) < 0)
			CHECK(memcmp(&saved, &test_ic.ic_wme.wme_wmeChanParams,
				     sizeof(saved)) == 0);
	}
	setup();
	/* Every ACI order identifies the same logical AC, with no TXOP
	 * narrowing. */
	{
		static const uint16_t txop[4] = {1, 256, 2047, 65535};
		unsigned a, b, c, d, count = 0;
		for (a = 0; a < 4; a++)
			for (b = 0; b < 4; b++)
				for (c = 0; c < 4; c++)
					for (d = 0; d < 4; d++) {
						unsigned order[4] = {a, b, c,
								     d},
							 i;
						if (a == b || a == c ||
						    a == d || b == c ||
						    b == d || c == d)
							continue;
						memcpy(p, base, 26);
						p[8] = (uint8_t)(++count & 15);
						for (i = 0; i < 4; i++) {
							unsigned ac = order[i],
								 off =
								     10 + 4 * i;
							p[off] =
							    (uint8_t)(ac << 5 |
								      2);
							p[off + 2] =
							    (uint8_t)txop[ac];
							p[off + 3] =
							    (uint8_t)(txop[ac] >>
								      8);
						}
						CHECK(ieee80211_parse_wmeparams(
							  &test_ic, p, NULL) ==
						      1);
						for (i = 0; i < 4; i++)
							CHECK(
							    test_ic.ic_wme
								.wme_wmeChanParams
								.cap_wmeParams
								    [i]
								.wmep_txopLimit ==
							    txop[i]);
					}
		CHECK(count == 24);
	}
	m_freem(m);
}
static struct mbuf *
ethernet(uint8_t tos, uint16_t type)
{
	struct mbuf *m =
	    newbuf(sizeof(struct ether_header) + sizeof(struct ip));
	struct ether_header *eh;
	struct ip *ip;
	assert(m);
	eh = mtod(m, struct ether_header *);
	memset(eh->ether_dhost, 0x66, 6);
	memcpy(eh->ether_shost, test_ic.ic_myaddr, 6);
	eh->ether_type = htons(type);
	ip = (struct ip *)(eh + 1);
	ip->ip_vhl = 0x45;
	ip->ip_tos = tos;
	{
		uint8_t value;
		m_copydata(m, sizeof(*eh) + 1, 1, &value);
		assert(value == tos);
	}
	return m;
}
static void
associate_with_acm(unsigned mask)
{
	struct mbuf *m;
	uint8_t *p;
	int ac;
	setup();
	CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
	m = response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	p = response_ie(m);
	p[8] = 1;
	for (ac = 0; ac < 4; ac++)
		if (mask & (1U << ac))
			p[10 + 4 * ac] |= 0x10;
	deliver(m, IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(test_node.ni_flags & IEEE80211_NODE_QOS);
	release_capture();
}
static void
test_classification(void)
{
	static const uint8_t tos[4] = {0, 8, 0x28, 0x30};
	static const int priority[4] = {1, 0, 2, 3};
	static const int vlanpri[4] = {0, 1, 4, 6};
	static const int admitted[16][4] = {
	    {0, 1, 2, 3}, {1, 1, 2, 3}, {0, -1, 2, 3}, {-1, -1, 2, 3},
	    {0, 1, 0, 3}, {1, 1, 1, 3}, {0, -1, 0, 3}, {-1, -1, -1, 3},
	    {0, 1, 2, 2}, {1, 1, 2, 2}, {0, -1, 2, 2}, {-1, -1, 2, 2},
	    {0, 1, 0, 0}, {1, 1, 1, 1}, {0, -1, 0, 0}, {-1, -1, -1, -1},
	};
	unsigned mask;
	int ac, vlan;
	for (mask = 0; mask < 16; mask++) {
		associate_with_acm(mask);
		for (ac = 0; ac < 4; ac++) {
			struct mbuf *m = ethernet(tos[ac], ETHERTYPE_IP);
			int result =
			    ieee80211_classify(&test_ic, m, &test_node);
			CHECK(result == (admitted[mask][ac] < 0 ? 1 : 0));
			if (result == 0)
				CHECK(m->ac == admitted[mask][ac]);
			m_freem(m);
		}
	}
	associate_with_acm(0);
	for (vlan = 0; vlan < 4; vlan++)
		for (ac = 0; ac < 4; ac++) {
			struct mbuf *m = ethernet(tos[ac], ETHERTYPE_IP);
			test_node.ni_vlan = (uint16_t)(1 | vlanpri[vlan] << 13);
			CHECK(ieee80211_classify(&test_ic, m, &test_node) == 0);
			CHECK(m->ac ==
			      (priority[vlan] > priority[ac] ? vlan : ac));
			m_freem(m);
		}
	test_node.ni_vlan = 0;
	{
		struct mbuf *m = ethernet(0x30, ETHERTYPE_IP),
			    *tail = newbuf(sizeof(struct ip));
		assert(tail);
		memcpy(tail->m_data, m->m_data + sizeof(struct ether_header),
		       sizeof(struct ip));
		memset(m->m_data + sizeof(struct ether_header), 0,
		       sizeof(struct ip));
		m->m_len = sizeof(struct ether_header);
		m->m_next = tail;
		CHECK(ieee80211_classify(&test_ic, m, &test_node) == 0);
		CHECK(m->ac == WME_AC_VO);
		m_freem(m);
	}
	{
		struct mbuf *m = ethernet(0, ETHERTYPE_IP);
		m->m_len = m->m_pkthdr.len = sizeof(struct ether_header) + 1;
		CHECK(ieee80211_classify(&test_ic, m, &test_node) != 0);
		m_freem(m);
	}
}
static unsigned
tx_data(int ac, uint16_t type)
{
	struct mbuf *m = ethernet(0, type), *out;
	unsigned seq;
	m->ac = ac;
	out = ieee80211_encap(&test_ic, m, &test_node);
	CHECK(out != NULL);
	assert(out);
	seq =
	    le16toh(*(uint16_t *)mtod(out, struct ieee80211_frame *)->i_seq) >>
	    IEEE80211_SEQ_SEQ_SHIFT;
	if (type == ETHERTYPE_PAE)
		CHECK(!IEEE80211_QOS_HAS_SEQ(
		    mtod(out, struct ieee80211_frame *)));
	else {
		static const uint8_t tid[4] = {0, 1, 5, 6};
		CHECK(
		    IEEE80211_QOS_HAS_SEQ(mtod(out, struct ieee80211_frame *)));
		CHECK((mtod(out, struct ieee80211_qosframe *)->i_qos[0] &
		       IEEE80211_QOS_TID) == tid[ac]);
	}
	m_freem(out);
	return seq;
}
static struct mbuf *
beacon(unsigned count)
{
	struct mbuf *m =
	    newbuf(sizeof(struct ieee80211_frame) + 12 + 2 + 3 + 26);
	uint8_t *p;
	struct ieee80211_frame *wh;
	assert(m);
	wh = mtod(m, struct ieee80211_frame *);
	wh->i_fc[0] = IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_BEACON;
	memset(wh->i_addr1, 255, 6);
	memcpy(wh->i_addr2, test_node.ni_macaddr, 6);
	memcpy(wh->i_addr3, test_node.ni_bssid, 6);
	p = (uint8_t *)(wh + 1);
	p[8] = 100;
	p[10] = IEEE80211_CAPINFO_ESS;
	p += 12;
	*p++ = IEEE80211_ELEMID_SSID;
	*p++ = 0;
	*p++ = IEEE80211_ELEMID_RATES;
	*p++ = 1;
	*p++ = 2;
	test_ic.ic_wme.wme_bssChanParams.cap_info = (uint8_t)count;
	CHECK(ieee80211_add_wme_param(p, &test_ic.ic_wme) ==
	      m->m_data + m->m_len);
	return m;
}
static void
deliver_beacon(struct mbuf *m)
{
	ieee80211_recv_mgmt_beacon(&test_ic, m, &test_node,
				   IEEE80211_FC0_SUBTYPE_BEACON, 0, 0);
	m_freem(m);
}
static unsigned quiet_calls, quiet_state;
static void
test_quiet(struct ieee80211com *ic)
{
	/* A real provider closes producers here. It must precede INIT. */
	CHECK(ic->ic_state == quiet_state);
	quiet_calls++;
}
static void
test_csa_beacon_stop(void)
{
	static const struct {
		size_t len;
		uint8_t data[20];
	} announcements[] = {
		{5,{37,3,0,64,255}}, {5,{37,3,1,64,0}},
		{6,{60,4,1,118,64,1}}, {4,{37,2,1,64}},
		{5,{37,3,2,64,1}}, {5,{37,3,1,0,1}},
		{4,{37,3,1,64}}, {10,{37,3,1,64,1,37,3,0,36,0}},
		{6,{37,3,1,64,1,127}}, {1,{127}},
		{8,{40,6,0,1,10,0,20,0}}, {8,{40,6,255,0,0,0,0,0}},
		{7,{40,5,0,1,10,0,20}},
		{16,{40,6,0,1,10,0,20,0,40,6,1,0,0,0,0,0}}
	};
	for (unsigned i = 0; i < sizeof(announcements) / sizeof(announcements[0]); i++) {
		for (unsigned context = 0; context < 12; context++) {
			struct ieee80211_node other;
			setup();
			test_ic.ic_state = IEEE80211_S_RUN;
			test_node.ni_associd = 1;
			test_ic.ic_sta_quiet = test_quiet;
			quiet_calls = 0;
			struct mbuf *m = beacon(0);
			struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
			/* Replace WME after the MAC header, fixed body, SSID and rates. */
			size_t offset = sizeof(*wh) + 12 + 2 + 3;
			memcpy(m->m_data + offset, announcements[i].data, announcements[i].len);
			m->m_len = m->m_pkthdr.len = offset + announcements[i].len;
			if (context == 1) wh->i_addr2[5] ^= 1;
			if (context == 2) wh->i_addr3[5] ^= 1;
			if (context == 3) test_ic.ic_sta_quiet = NULL;
			if (context == 4) test_ic.ic_opmode = IEEE80211_M_IBSS;
			if (context == 5) test_ic.ic_state = IEEE80211_S_SCAN;
			if (context == 6) test_ic.ic_curchan = NULL;
			if (context == 7) test_node.ni_chan = NULL;
			if (context == 8) { other = test_node; test_ic.ic_bss = &other; }
			if (context == 9) test_ic.ic_state = IEEE80211_S_INIT;
			if (context == 10) test_ic.ic_state = IEEE80211_S_AUTH;
			if (context == 11) test_ic.ic_state = IEEE80211_S_ASSOC;
			quiet_state = test_ic.ic_state;
			deliver_beacon(m);
			unsigned want = context == 0 || context >= 10;
			CHECK(quiet_calls == want);
			if (want) CHECK(test_ic.ic_state == IEEE80211_S_INIT);
		}
	}
	/* Probe responses share the consumer. Short fixed bodies cannot hide CSA. */
	for (unsigned kind = 0; kind < 2; kind++) {
		setup();
		test_ic.ic_state = IEEE80211_S_RUN;
		test_node.ni_associd = 1;
		test_ic.ic_sta_quiet = test_quiet;
		quiet_calls = 0;
		quiet_state = test_ic.ic_state;
		test_node.ni_ht20.valid = 1;
		struct mbuf *m = beacon(0);
		if (kind == 0) {
			m->m_len = m->m_pkthdr.len = sizeof(struct ieee80211_frame) + 11;
		} else {
			const uint8_t csa[] = {37,3,1,64,0};
			memcpy(m->m_data + 41, csa, sizeof(csa));
			m->m_len = m->m_pkthdr.len = 41 + sizeof(csa);
		}
		ieee80211_recv_mgmt_beacon(&test_ic, m, &test_node,
		    kind ? IEEE80211_FC0_SUBTYPE_PROBE_RESP : IEEE80211_FC0_SUBTYPE_BEACON, 0, 0);
		m_freem(m);
		CHECK(quiet_calls == 1 && test_ic.ic_state == IEEE80211_S_INIT);
		CHECK(!test_node.ni_ht20.valid);
	}
	setup();
	test_ic.ic_state = IEEE80211_S_RUN;
	test_node.ni_associd = 1;
	test_ic.ic_sta_quiet = test_quiet;
	quiet_calls = 0;
	quiet_state = test_ic.ic_state;
	deliver_beacon(beacon(0));
	CHECK(quiet_calls == 0 && test_ic.ic_state == IEEE80211_S_RUN);
}

static void
test_beacon_ie_bounds(void)
{
	/* The complete native management consumer must reject a short TIM. */
	for (unsigned length = 0; length < 4; length++) {
		setup();
	test_ic.ic_flags |= IEEE80211_F_SCAN;
		struct mbuf *m = beacon(1);
		unsigned before = test_ic.ic_stats.is_rx_elem_toosmall;
		uint8_t *end = m->m_data + m->m_len;
		end[0] = IEEE80211_ELEMID_TIM;
		end[1] = length;
		memset(end + 2, 0, length);
		m->m_len += length + 2;
		deliver_beacon(m);
		CHECK(test_ic.ic_stats.is_rx_elem_toosmall == before + 1);
	}
	setup();
	test_ic.ic_flags |= IEEE80211_F_SCAN;
	struct mbuf *m = beacon(1);
	unsigned before = test_ic.ic_stats.is_rx_elem_toosmall;
	m->m_data[m->m_len++] = 127; /* Incomplete trailing IE header. */
	deliver_beacon(m);
	CHECK(test_ic.ic_stats.is_rx_elem_toosmall == before + 1);
}
static void
test_beacon_update(void)
{
	int variant;
	for (variant = 0; variant < 4; variant++) {
		struct mbuf *m;
		struct ieee80211_frame *wh;
		unsigned before;
		struct chanAccParams saved;
		associate_with_acm(0);
		before = applies;
		saved = test_ic.ic_wme.wme_wmeChanParams;
		m = beacon(2);
		wh = mtod(m, struct ieee80211_frame *);
		if (variant == 0)
			wh->i_addr2[0] ^= 2;
		else if (variant == 1)
			wh->i_addr3[0] ^= 2;
		else if (variant == 2) {
			uint8_t *p = m->m_data + m->m_len - 26;
			memcpy(p + 26, p, 26);
			m->m_len += 26;
			m->m_pkthdr.len += 26;
		} else
			m->m_data[m->m_len - 3] = 0x34;
		deliver_beacon(m);
		CHECK(applies == before);
		CHECK(memcmp(&saved, &test_ic.ic_wme.wme_wmeChanParams,
			     sizeof(saved)) == 0);
	}
	associate_with_acm(0);
	unsigned before = applies;
	deliver_beacon(beacon(15));
	CHECK(applies == before + 1);
	deliver_beacon(beacon(15));
	CHECK(applies == before + 1);
	deliver_beacon(beacon(0));
	CHECK(applies == before + 2);
	setup();
	test_ic.ic_flags |= IEEE80211_F_SCAN;
	deliver_beacon(beacon(1));
	CHECK((test_node.ni_flags & IEEE80211_NODE_QOS) == 0);
	CHECK(applies == 0);
}
static void
test_sequence(void)
{
	struct ieee80211_frame wh;
	associate_with_acm(0);
	CHECK(test_node.ni_txseqs[0] == 1);
	CHECK(tx_data(WME_AC_BE, ETHERTYPE_PAE) == 1);
	CHECK(test_node.ni_txseqs[0] == 2);
	CHECK(tx_data(WME_AC_BE, ETHERTYPE_IP) == 0);
	CHECK(test_node.ni_txseqs[1] == 1);
	CHECK(test_node.ni_txseqs[0] == 2);
	CHECK(tx_data(WME_AC_BK, ETHERTYPE_IP) == 0);
	CHECK(test_node.ni_txseqs[2] == 1);
	ieee80211_send_setup(
	    &test_ic, &test_node, &wh,
	    IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_REQ,
	    test_ic.ic_myaddr, test_node.ni_macaddr, test_node.ni_bssid);
	CHECK((le16toh(*(uint16_t *)wh.i_seq) >> IEEE80211_SEQ_SEQ_SHIFT) == 2);
	CHECK(tx_data(WME_AC_BE, ETHERTYPE_IP) == 1);
	CHECK(test_node.ni_txseqs[1] == 2);
	CHECK(tx_data(WME_AC_VI, ETHERTYPE_IP) == 0);
	CHECK(test_node.ni_txseqs[6] == 1);
	CHECK(tx_data(WME_AC_VO, ETHERTYPE_IP) == 0);
	CHECK(test_node.ni_txseqs[7] == 1);
	{
		struct mbuf *m = ethernet(0, ETHERTYPE_IP);
		adjust_failure = 1;
		CHECK(ieee80211_encap(&test_ic, m, &test_node) == NULL);
		adjust_failure = 0;
	}
	test_node.ni_txseqs[1] = 4095;
	CHECK(tx_data(WME_AC_BE, ETHERTYPE_IP) == 4095);
	CHECK(tx_data(WME_AC_BE, ETHERTYPE_IP) == 0);
	{
		struct mbuf *m = ethernet(0, ETHERTYPE_IP);
		m->ac = 4;
		struct mbuf *out = ieee80211_encap(&test_ic, m, &test_node);
		CHECK(out == NULL);
		if (out)
			m_freem(out);
	}
}
static void
test_rt2661_sequence_consumer(void)
{
	int ac;
	associate_with_acm(0);
	for (ac = 0; ac < 4; ac++) {
		static const unsigned tid[4] = {0, 1, 5, 6};
		struct mbuf *m, *out;
		memset(test_node.ni_txseqs, 0, sizeof(test_node.ni_txseqs));
		test_node.ni_txseqs[tid[ac]] = 8;
		m = ethernet(0, ETHERTYPE_IP);
		m->ac = ac;
		out = ieee80211_encap(&test_ic, m, &test_node);
		assert(out);
		CHECK(native_rt2661_burst(&test_ic, out, &test_node) ==
		      RT2661_TX_IFS_SIFS);
		m_freem(out);
	}
	{
		struct mbuf *m, *out;
		memset(test_node.ni_txseqs, 0, sizeof(test_node.ni_txseqs));
		test_node.ni_txseqs[0] = 7;
		test_node.ni_txseqs[6] = 1;
		m = ethernet(0, ETHERTYPE_PAE);
		m->ac = WME_AC_VO;
		out = ieee80211_encap(&test_ic, m, &test_node);
		assert(out);
		CHECK(native_rt2661_burst(&test_ic, out, &test_node) == 0);
		m_freem(out);
	}
}
static void
install_strict(void)
{
#ifdef NATIVE_STA_APPLY
	test_ic.ic_wme.wme_sta_apply = strict_update;
#else
	(void)strict_update;
#endif
}
static void
test_invalid_apply_candidate(void)
{
#ifdef NATIVE_STA_APPLY
	setup();
	install_strict();
	struct chanAccParams candidate = test_ic.ic_wme.wme_wmeBssChanParams;
	for (unsigned ac = 0; ac < 4; ac++) {
		for (unsigned field = 0; field < 7; field++) {
			struct chanAccParams bad = candidate;
			struct wmeParams *p = &bad.cap_wmeParams[ac];
			switch (field) {
			case 0:
				p->wmep_logcwmin = 5;
				break;
			case 1:
				p->wmep_logcwmin = 0;
				break;
			case 2:
				p->wmep_logcwmax = 16;
				break;
			case 3:
				p->wmep_aifsn = 1;
				break;
			case 4:
				p->wmep_aifsn = 16;
				break;
			case 5:
				p->wmep_acm = 2;
				break;
			case 6:
				p->wmep_noackPolicy = 2;
				break;
			}
			CHECK(ieee80211_wme_apply_sta(&test_ic, &test_node,
						      &bad, 20) == EINVAL);
			CHECK(applies == 0);
		}
	}
	CHECK(ieee80211_wme_apply_sta(&test_ic, NULL, &candidate, 20) ==
	      EINVAL);
	CHECK(ieee80211_wme_apply_sta(&test_ic, &test_node, NULL, 20) ==
	      EINVAL);
	CHECK(ieee80211_wme_apply_sta(&test_ic, &test_node, &candidate, 10) ==
	      EINVAL);
	test_ic.ic_curmode = IEEE80211_MODE_11A;
	CHECK(ieee80211_wme_apply_sta(&test_ic, &test_node, &candidate, 20) ==
	      EINVAL);
	CHECK(applies == 0);

#endif
}
static void
test_strict_complete_views(void)
{
	setup();
	install_strict();
	for (unsigned ac = 0; ac < 4; ac++) {
		test_ic.ic_wme.wme_wmeChanParams.cap_wmeParams[ac].wmep_noackPolicy = ac & 1;
		test_ic.ic_wme.wme_bssChanParams.cap_wmeParams[ac].wmep_acm = (ac + 1) & 1;
		test_ic.ic_wme.wme_bssChanParams.cap_wmeParams[ac].wmep_txopLimit = 65535;
	}
	test_ic.ic_wme.wme_wmeChanParams.cap_info = 7;
	expect_uncommitted = 1;
	CHECK(ieee80211_send_mgmt(&test_ic, &test_node,
	    IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 0) == 0);
	deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
	    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(test_ic.ic_state == IEEE80211_S_RUN);
	CHECK(test_ic.ic_wme.wme_wmeChanParams.cap_info == 0);
	for (unsigned ac = 0; ac < 4; ac++) {
		CHECK(applied_params.cap_wmeParams[ac].wmep_acm == ((ac + 1) & 1));
		CHECK(applied_params.cap_wmeParams[ac].wmep_noackPolicy == (ac & 1));
		CHECK(applied_params.cap_wmeParams[ac].wmep_txopLimit == 65535);
	}
	CHECK(memcmp(test_ic.ic_wme.wme_params, applied_params.cap_wmeParams,
	    sizeof(test_ic.ic_wme.wme_params)) == 0);
}
static int
test_wme_params_equal(const struct wmeParams *a, const struct wmeParams *b)
{
	return a->wmep_acm == b->wmep_acm &&
	    a->wmep_aifsn == b->wmep_aifsn &&
	    a->wmep_logcwmin == b->wmep_logcwmin &&
	    a->wmep_logcwmax == b->wmep_logcwmax &&
	    a->wmep_txopLimit == b->wmep_txopLimit &&
	    a->wmep_noackPolicy == b->wmep_noackPolicy;
}
static int
test_chan_params_equal(const struct chanAccParams *a,
    const struct chanAccParams *b)
{
	unsigned ac;

	if (a->cap_info != b->cap_info)
		return 0;
	for (ac = 0; ac < WME_NUM_AC; ac++)
		if (!test_wme_params_equal(&a->cap_wmeParams[ac],
		    &b->cap_wmeParams[ac]))
			return 0;
	return 1;
}
static int
test_wme_state_equal(const struct ieee80211_wme_state *a,
    const struct ieee80211_wme_state *b)
{
	unsigned ac;

	if (a->wme_flags != b->wme_flags ||
	    a->wme_hipri_traffic != b->wme_hipri_traffic ||
	    a->wme_hipri_switch_thresh != b->wme_hipri_switch_thresh ||
	    a->wme_hipri_switch_hysteresis != b->wme_hipri_switch_hysteresis ||
	    a->wme_update != b->wme_update ||
	    a->wme_sta_apply != b->wme_sta_apply ||
	    a->wme_sta_error != b->wme_sta_error)
		return 0;
	for (ac = 0; ac < WME_NUM_AC; ac++)
		if (!test_wme_params_equal(&a->wme_params[ac],
		    &b->wme_params[ac]))
			return 0;
	return test_chan_params_equal(&a->wme_wmeChanParams,
	    &b->wme_wmeChanParams) &&
	    test_chan_params_equal(&a->wme_wmeBssChanParams,
	    &b->wme_wmeBssChanParams) &&
	    test_chan_params_equal(&a->wme_chanParams, &b->wme_chanParams) &&
	    test_chan_params_equal(&a->wme_bssChanParams, &b->wme_bssChanParams);
}
/* Each stale mode changes one selected callback owner value. */
enum apply_change {
	APPLY_UNCHANGED, APPLY_WME_RESET, APPLY_BSS, APPLY_MAC, APPLY_BSSID,
	APPLY_CURRENT_CHANNEL, APPLY_NODE_CHANNEL, APPLY_STATE, APPLY_OPMODE,
	APPLY_CIPHER, APPLY_PRIVACY, APPLY_WME_ENABLE, APPLY_WME_CAPABILITY,
	APPLY_SHORT_SLOT, APPLY_MODE, APPLY_CALLBACK, APPLY_ATTEMPT,
	APPLY_HT_RESET, APPLY_UNRELATED_FLAG
};
static enum apply_change apply_change;
static struct ieee80211_node replacement_node, after_apply_node;
static struct ieee80211_channel replacement_channel = {2437, 0};
static struct ieee80211_wme_state after_apply_wme;
static struct ieee80211com after_apply_ic;

static int
replacement_apply(struct ieee80211com *ic, const struct chanAccParams *params,
    unsigned slot)
{
	(void)ic;
	(void)params;
	(void)slot;
	abort(); /* Replacement must not be called by the completing transaction. */
}

static void
change_apply_owner(struct ieee80211com *ic)
{
	struct ieee80211_node *ni = ic->ic_bss;

	assert(beacon_lock);
	switch (apply_change) {
	case APPLY_WME_RESET:
		ieee80211_wme_reset(ni);
		break;
	case APPLY_BSS:
		replacement_node = *ni;
		ic->ic_bss = &replacement_node;
		break;
	case APPLY_MAC:
		ni->ni_macaddr[5] ^= 1;
		break;
	case APPLY_BSSID:
		ni->ni_bssid[5] ^= 1;
		break;
	case APPLY_CURRENT_CHANNEL:
		ic->ic_curchan = &replacement_channel;
		break;
	case APPLY_NODE_CHANNEL:
		ni->ni_chan = &replacement_channel;
		break;
	case APPLY_STATE:
		ic->ic_state = IEEE80211_S_AUTH;
		break;
	case APPLY_OPMODE:
		ic->ic_opmode = IEEE80211_M_HOSTAP;
		break;
	case APPLY_CIPHER:
		ni->ni_rsn.rsn_ucastcipher = IEEE80211_CIPHER_AES_CCM;
		break;
	case APPLY_PRIVACY:
		ic->ic_flags ^= IEEE80211_F_PRIVACY;
		break;
	case APPLY_WME_ENABLE:
		ic->ic_flags ^= IEEE80211_F_WME;
		break;
	case APPLY_WME_CAPABILITY:
		ic->ic_caps ^= IEEE80211_C_WME;
		break;
	case APPLY_SHORT_SLOT:
		ic->ic_flags ^= IEEE80211_F_SHSLOT;
		break;
	case APPLY_MODE:
		ic->ic_curmode = IEEE80211_MODE_11B;
		break;
	case APPLY_CALLBACK:
		ic->ic_wme.wme_sta_apply = replacement_apply;
		break;
	case APPLY_ATTEMPT:
		ni->ni_wme_attempt++;
		break;
	case APPLY_HT_RESET:
		ieee80211_ht20_reset(ni);
		break;
	case APPLY_UNRELATED_FLAG:
		ic->ic_flags ^= IEEE80211_F_WMEUPDATE;
		break;
	case APPLY_UNCHANGED:
		break;
	}
	if (apply_change >= APPLY_WME_RESET &&
	    apply_change <= APPLY_ATTEMPT) {
		/* A callback can replace live tables; rejection must retain them. */
		ic->ic_wme.wme_wmeChanParams.cap_info = 11;
		ic->ic_wme.wme_chanParams.cap_info = 12;
		ic->ic_wme.wme_bssChanParams.cap_info = 13;
		ic->ic_wme.wme_wmeBssChanParams.cap_info = 14;
		for (unsigned ac = 0; ac < WME_NUM_AC; ac++)
			ic->ic_wme.wme_params[ac].wmep_txopLimit = 600 + ac;
	}
	after_apply_wme = ic->ic_wme;
	after_apply_node = *ni;
	after_apply_ic = *ic;
}

static void
test_apply_callback_owner(void)
{
	const int errors[] = {0, EIO, EOPNOTSUPP};

	for (unsigned e = 0; e < sizeof(errors) / sizeof(errors[0]); e++) {
		for (enum apply_change change = APPLY_WME_RESET;
		    change <= APPLY_ATTEMPT; change++) {
			struct chanAccParams candidate;
			int error;

			setup();
			install_strict();
			candidate = test_ic.ic_wme.wme_wmeBssChanParams;
			candidate.cap_info = 3;
			test_node.ni_wme_applied_chan = &replacement_channel;
			test_node.ni_wme_applied_slot = 77;
			apply_change = change;
			apply_error = errors[e];
			during_apply = change_apply_owner;
			error = ieee80211_wme_apply_sta(&test_ic, &test_node,
			    &candidate, 20);
			if (error != (errors[e] != 0 ? errors[e] : EINVAL))
				fprintf(stderr, "callback owner mode %u error %d: %d\n",
				    (unsigned)change, errors[e], error);
			CHECK(error == (errors[e] != 0 ? errors[e] : EINVAL));
			CHECK(applies == 1 && !beacon_lock);
			after_apply_wme.wme_sta_error = error;
			CHECK(test_wme_state_equal(&test_ic.ic_wme, &after_apply_wme));
			CHECK(test_node.ni_flags == after_apply_node.ni_flags);
			CHECK(test_node.ni_wme_applied_chan ==
			    after_apply_node.ni_wme_applied_chan);
			CHECK(test_node.ni_wme_applied_slot ==
			    after_apply_node.ni_wme_applied_slot);
			/* The postcheck only owns its error and prepared publication. */
			CHECK(test_ic.ic_bss == after_apply_ic.ic_bss);
			CHECK(test_ic.ic_curchan == after_apply_ic.ic_curchan);
			CHECK(test_ic.ic_state == after_apply_ic.ic_state);
			CHECK(test_ic.ic_opmode == after_apply_ic.ic_opmode);
			CHECK(test_ic.ic_curmode == after_apply_ic.ic_curmode);
			CHECK(test_ic.ic_flags == after_apply_ic.ic_flags);
			CHECK(test_ic.ic_caps == after_apply_ic.ic_caps);
			CHECK(test_node.ni_wme_attempt == after_apply_node.ni_wme_attempt);
			CHECK(IEEE80211_ADDR_EQ(test_node.ni_macaddr, after_apply_node.ni_macaddr));
			CHECK(IEEE80211_ADDR_EQ(test_node.ni_bssid, after_apply_node.ni_bssid));
			CHECK(test_node.ni_chan == after_apply_node.ni_chan);
			CHECK(test_node.ni_rsn.rsn_ucastcipher ==
			    after_apply_node.ni_rsn.rsn_ucastcipher);
		}
	}
	for (enum apply_change change = APPLY_UNCHANGED;
	    change <= APPLY_UNRELATED_FLAG; change++) {
		struct chanAccParams candidate;

		if (change != APPLY_UNCHANGED && change != APPLY_HT_RESET &&
		    change != APPLY_UNRELATED_FLAG)
			continue;
		setup();
		install_strict();
		candidate = test_ic.ic_wme.wme_wmeBssChanParams;
		candidate.cap_info = 3;
		apply_change = change;
		during_apply = change_apply_owner;
		CHECK(ieee80211_wme_apply_sta(&test_ic, &test_node,
		    &candidate, 20) == 0);
		CHECK(applies == 1 && !beacon_lock);
		CHECK(test_ic.ic_wme.wme_sta_error == 0);
		CHECK(test_chan_params_equal(&test_ic.ic_wme.wme_wmeChanParams,
		    &candidate));
		CHECK(test_chan_params_equal(&test_ic.ic_wme.wme_chanParams,
		    &applied_params));
		CHECK(test_node.ni_wme_applied_chan == &test_channel);
		CHECK(test_node.ni_wme_applied_slot == 20);
		CHECK((test_node.ni_flags &
		    (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)) ==
		    (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID));
		if (change == APPLY_HT_RESET)
			CHECK(test_node.ni_ht20.attempt == 1);
		if (change == APPLY_UNRELATED_FLAG)
			CHECK(test_ic.ic_flags & IEEE80211_F_WMEUPDATE);
	}
}

static void
test_wme_only_callback_cancel(void)
{
	for (enum apply_change change = APPLY_WME_RESET;
	    change <= APPLY_ATTEMPT; change++) {
		setup();
		install_strict();
		CHECK(request(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) == 0);
		CHECK(offered_on_wire(IEEE80211_FC0_SUBTYPE_ASSOC_REQ));
		CHECK(!test_node.ni_ht20.offered);
		apply_change = change;
		during_apply = change_apply_owner;
		deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
		    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK(applies == 1 && !beacon_lock);
		CHECK(test_ic.ic_wme.wme_sta_error == EINVAL);
		CHECK(test_ic.ic_state == IEEE80211_S_SCAN);
		CHECK(test_node.ni_associd == 0);
		CHECK(!(test_node.ni_flags &
		    (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)));
	}
}

static void
test_strict_ioctl_reject(void)
{
	setup();
	install_strict();
	struct ieee80211req req = {
	    .i_type = IEEE80211_IOC_WME_TXOPLIMIT, .i_val = 123, .i_len = 0};
	struct ieee80211_wme_state saved = test_ic.ic_wme;
	CHECK(ieee80211_ioctl_setwmeparam(&test_ic, &req) == EOPNOTSUPP);
	CHECK(test_wme_state_equal(&saved, &test_ic.ic_wme));
	CHECK(applies == 0);
}
static void
test_strict_reject_legacy_void_update(void)
{
	setup();
	install_strict();
	struct chanAccParams saved = test_ic.ic_wme.wme_chanParams;
	ieee80211_wme_updateparams(&test_ic);
	CHECK(applies == 0);
	CHECK(memcmp(&saved, &test_ic.ic_wme.wme_chanParams, sizeof(saved)) ==
	      0);
#ifdef NATIVE_STA_APPLY
	CHECK(test_ic.ic_wme.wme_sta_error == EOPNOTSUPP);
#endif
}
static void
test_legacy_ap_contract(void)
{
	setup();
	install_strict();
	test_ic.ic_opmode = IEEE80211_M_HOSTAP;
	apply_error = EIO;
	ieee80211_wme_initparams(&test_ic);
	CHECK(applies == 1);
	CHECK(test_ic.ic_flags & IEEE80211_F_WMEUPDATE);
	CHECK(memcmp(test_ic.ic_wme.wme_params,
		     test_ic.ic_wme.wme_chanParams.cap_wmeParams,
		     sizeof(test_ic.ic_wme.wme_params)) == 0);
	for (unsigned iteration = 0; iteration < 18; iteration++) {
		unsigned count = test_ic.ic_wme.wme_bssChanParams.cap_info;
		ieee80211_wme_updateparams(&test_ic);
		CHECK(test_ic.ic_wme.wme_bssChanParams.cap_info ==
		    ((count + 1) & WME_QOSINFO_COUNT));
	}

}
static void
test_strict_join_ownership(void)
{
	struct ieee80211_node old;
	setup();
	install_strict();
	old = test_node;
	old.ni_vlan = 7;
	test_ic.ic_bss = &old;
	node_refs++; /* Distinct selected node held by the caller. */
	last_freed = NULL;
	apply_error = EIO;
	CHECK(ieee80211_sta_join(&test_ic, &test_node) == 1);
	CHECK(test_ic.ic_bss == &test_node && last_freed == &old);
	CHECK(node_refs == 1);
	CHECK(test_node.ni_vlan == 7 && test_ic.ic_state == IEEE80211_S_AUTH);
	CHECK(applies == 0);
	last_freed = NULL;
}
static void
test_apply_transaction(void)
{
	setup();
	install_strict();
	ieee80211_wme_initparams(&test_ic);
	CHECK(applies == 0);
	CHECK(!(test_node.ni_flags &
		(IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)));
	int errors[] = {EOPNOTSUPP, EIO};
	for (unsigned i = 0; i < 2; i++) {
		struct chanAccParams saved;
		setup();
		install_strict();
		saved = test_ic.ic_wme.wme_wmeChanParams;
		CHECK(ieee80211_send_mgmt(&test_ic, &test_node,
					  IEEE80211_FC0_SUBTYPE_ASSOC_REQ,
					  0) == 0);
		apply_error = errors[i];
		deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
			IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
		CHECK(test_ic.ic_state != IEEE80211_S_RUN);
		CHECK(test_node.ni_associd == 0);
		CHECK((test_node.ni_flags &
		       (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)) == 0);
		CHECK(memcmp(&saved, &test_ic.ic_wme.wme_wmeChanParams,
			     sizeof(saved)) == 0);
	}
	setup();
	install_strict();
	CHECK(ieee80211_send_mgmt(&test_ic, &test_node,
				  IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 0) == 0);
	deliver(response(1, 0, IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
		IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	CHECK(test_ic.ic_state == IEEE80211_S_RUN);
	CHECK(applied_slot == 20);
	CHECK(memcmp(test_ic.ic_wme.wme_params, applied_params.cap_wmeParams,
		     sizeof(test_ic.ic_wme.wme_params)) == 0);
	{
		struct mbuf *m = beacon(0);
		unsigned before = applies;
		m->m_data[35] |=
		    (uint8_t)(IEEE80211_CAPINFO_SHORT_SLOTTIME >> 8);
		deliver_beacon(m);
		CHECK(applies == before + 1);
		CHECK(applied_slot == 9);
	}
	for (unsigned variant = 0; variant < 2; variant++) {
		struct mbuf *m = beacon(0);
		unsigned before = applies;
		if (variant == 0)
			m->m_len -= 26; /* No new WME IE. */
		else {
			m->m_data[35] |=
			    (uint8_t)(IEEE80211_CAPINFO_SHORT_SLOTTIME >> 8);
			m->m_data[48] =
			    0xff; /* Bad WME version; retain old parameters. */
		}
		deliver_beacon(m);
		CHECK(applies == before + 1);
		CHECK(applied_slot == (variant ? 9U : 20U));
		CHECK(test_ic.ic_wme.wme_wmeChanParams.cap_info == 0);
	}
	{
		struct chanAccParams saved = test_ic.ic_wme.wme_wmeChanParams;
		apply_error = EIO;
		deliver_beacon(beacon(1));
		CHECK(test_ic.ic_state != IEEE80211_S_RUN);
		CHECK(test_node.ni_associd == 0);
		CHECK((test_node.ni_flags &
		       (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)) == 0);
		CHECK(memcmp(&saved, &test_ic.ic_wme.wme_wmeChanParams,
			     sizeof(saved)) == 0);
	}
}

static unsigned probe_policy_calls;
static int probe_policy_error;
static int
probe_policy(struct ieee80211com *ic)
{
    CHECK(ic==&test_ic);probe_policy_calls++;
    return probe_policy_error;
}
static void
test_scan_probe_policy(void)
{
    for(unsigned path=0;path<4;path++) {
        setup();test_channel.ic_flags=IEEE80211_CHAN_PASSIVE;
        test_ic.ic_scan_probe_admit=probe_policy;
        probe_policy_calls=0;probe_policy_error=EACCES;
        unsigned allocated=allocations,refs=node_refs;
        if(path==0) {
            CHECK(ieee80211_send_probereq(&test_node,test_ic.ic_myaddr,
                test_node.ni_bssid,test_node.ni_bssid,test_ic.ic_des_essid,0,NULL,0)==EACCES);
        } else if(path==1)ieee80211_probe_curchan(&test_ic,1);
        else if(path==2) {
            ieee80211_probe_curchan(&test_ic,0);
            CHECK(test_ic.ic_flags_ext&IEEE80211_FEXT_PROBECHAN);
            CHECK(probe_policy_calls==0);
            ieee80211_probe_curchan(&test_ic,1);
        } else {
            test_ic.ic_flags|=IEEE80211_F_SCAN;
            test_ic.ic_flags_ext|=IEEE80211_FEXT_PROBECHAN;
            deliver_beacon(beacon(0));
        }
        CHECK(probe_policy_calls==1 && captured==NULL && node_refs==refs);
        /* The beacon input itself owns a test mbuf. Probe refusal allocates none. */
        if(path!=3)CHECK(allocations==allocated);
        CHECK(test_ic.ic_mgtq.ifq_len==0);
    }
    setup();test_channel.ic_flags=IEEE80211_CHAN_PASSIVE;
    test_ic.ic_scan_probe_admit=probe_policy;
    probe_policy_calls=0;probe_policy_error=EIO;
    CHECK(ieee80211_send_probereq(&test_node,test_ic.ic_myaddr,
        test_node.ni_bssid,test_node.ni_bssid,test_ic.ic_des_essid,0,NULL,0)==EIO);
    CHECK(probe_policy_calls==1 && captured==NULL && node_refs==1);
    release_capture();
    probe_policy_error=0;
    ieee80211_probe_curchan(&test_ic,1);
    CHECK(probe_policy_calls==2 && captured!=NULL);
    release_capture();
    test_ic.ic_scan_probe_admit=NULL;
    ieee80211_probe_curchan(&test_ic,1);
    CHECK(captured!=NULL && probe_policy_calls==2);
    release_capture();test_channel.ic_flags=0;
}

int
main(void)
{
	test_pending_lifetime();
	test_scan_probe_policy();
	test_csa_beacon_stop();
	test_strict_complete_views();
	test_strict_ioctl_reject();
	test_strict_reject_legacy_void_update();
	test_legacy_ap_contract();
	test_strict_join_ownership();
	test_invalid_apply_candidate();
	test_apply_transaction();
	test_apply_callback_owner();
	test_wme_only_callback_cancel();
	test_exchange();
	test_parser();
	test_parser_boundaries();
	test_lifetime();
	test_request_ie();
	test_vht_request_bytes();
	test_vht_assoc_response();
	test_classification();
	test_sequence();
	test_beacon_update();
	test_beacon_ie_bounds();
	test_rt2661_sequence_consumer();
	setup();
	test_ic.ic_opmode = IEEE80211_M_HOSTAP;
	test_node.ni_flags = IEEE80211_NODE_QOS;
	ieee80211_sta_leave(&test_ic, &test_node);
	CHECK(test_node.ni_flags & IEEE80211_NODE_QOS);
	printf("Native WME exchange: %u checks, %u failures\n", checks,
	       failures);
	return failures != 0;
}
