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
#define ieee80211_chan2ieee(ic, c) 1U
#define ETHERTYPE_IP 0x0800
#define IFNAMSIZ 16
#define IEEE80211_MODE_MAX 7
#define IEEE80211_CHAN_MAX 255
#define IEEE80211_BINTVAL_MIN 10
#define IEEE80211_BINTVAL_MAX 1000
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
#define IEEE80211_KEY_UNDEFINED(k) 1
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
struct test_rsn { uint8_t rsn_ucastcipher; };
struct ieee80211_node {
	struct ieee80211_ht20_node ni_ht20;
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
	int (*ic_ht20_admit)(struct ieee80211com *, struct ieee80211_ht20_profile *);
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
	unsigned ic_lintval, ic_mgt_timer, ic_bmisstimeout, ic_fragthreshold;
	unsigned ic_flags_ext, ic_bmiss_count, ic_sta_assoc;
	uint8_t ic_chan_active[32];
	struct ieee80211_channel ic_channels[256];
	uint8_t ic_myaddr[6];
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
struct cipher {
	unsigned ic_header;
};
struct ieee80211_key {
	struct cipher *wk_cipher;
};
#define mtod(m, t) ((t)(m)->m_data)
#define M_SETCTX(m, n) ((m)->m_pkthdr.ctx = (n))
#define M_WME_SETAC(m, value) ((m)->ac = (value))
#define M_WME_GETAC(m) ((m)->ac)
#define IF_ENQUEUE(q, m) ((q)->ifq_len++, (q)->m = (m))
#define IF_DROP(q) ((q)->ifq_drops++)
#define IEEE80211_IS_CHAN_2GHZ(c) ((c)->ic_freq < 3000)
#define IEEE80211_IS_CHAN_FHSS(c) 0
#define EVL_VLANOFTAG(t) ((t) & 0xfff)
#define EVL_PRIOFTAG(t) (((t) >> 13) & 7)
#define vlan_has_tag(m) 1
#define vlan_get_tag(m) 1
#define ieee80211_crypto_getucastkey(...) NULL
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
static void
ieee80211_ref_node(struct ieee80211_node *ni)
{
	(void)ni;
	node_refs++;
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
static void
ieee80211_probe_curchan(struct ieee80211com *ic, int force)
{
	(void)ic;
	(void)force;
}
int
ieee80211_phy_ies_parse(struct ieee80211_phy_ies *ies, const uint8_t *p,
			size_t n)
{
	(void)p;
	(void)n;
	memset(ies, 0, sizeof(*ies));
	return 0;
}
#define IEEE80211_NODE_LOCK(nt) ((void)(nt))
#define IEEE80211_NODE_UNLOCK(nt) ((void)(nt))
#define ieee80211_fix_rate(ni, flags) ((void)(ni), (void)(flags))
#define ieee80211_chan2mode(ic, chan)                                          \
	((void)(ic), (void)(chan), IEEE80211_MODE_11G)
#define ieee80211_reset_erp(ic) ((void)(ic))
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
static void
test_strict_ioctl_reject(void)
{
	setup();
	install_strict();
	struct ieee80211req req = {
	    .i_type = IEEE80211_IOC_WME_TXOPLIMIT, .i_val = 123, .i_len = 0};
	struct ieee80211_wme_state saved = test_ic.ic_wme;
	CHECK(ieee80211_ioctl_setwmeparam(&test_ic, &req) == EOPNOTSUPP);
	CHECK(memcmp(&saved, &test_ic.ic_wme, sizeof(saved)) == 0);
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
int
main(void)
{
	test_strict_complete_views();
	test_strict_ioctl_reject();
	test_strict_reject_legacy_void_update();
	test_legacy_ap_contract();
	test_strict_join_ownership();
	test_invalid_apply_candidate();
	test_apply_transaction();
	test_exchange();
	test_parser();
	test_parser_boundaries();
	test_lifetime();
	test_request_ie();
	test_classification();
	test_sequence();
	test_beacon_update();
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
