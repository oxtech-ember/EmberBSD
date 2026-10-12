/* SPDX-License-Identifier: BSD-2-Clause */
/* Production AIC admission and BSD client functions with bounded OS fixtures. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef unsigned int u_int;
typedef uint8_t u8;
#define IEEE80211_WEP_NKID 4
#define IEEE80211_ADDR_LEN 6
#define ETHER_ADDR_LEN 6
#define ETH_ALEN 6
#define IEEE80211_ADDR_EQ(a, b) (memcmp((a), (b), 6) == 0)
#define IEEE80211_ADDR_COPY(d, s) memcpy((d), (s), 6)
#define IEEE80211_S_RUN 4
#define KM_SLEEP 0
#define KM_NOSLEEP 1
#define KASSERT assert
#define IEEE80211_DPRINTF(...) ((void)0)
#define device_printf(...) ((void)0)
#define IEEE80211_FC0_TYPE_MASK 0x0c
#define IEEE80211_FC0_TYPE_MGT 0
#define IEEE80211_FC0_SUBTYPE_DEAUTH 0xc0
#define IEEE80211_FC0_SUBTYPE_DISASSOC 0xa0
#define IEEE80211_FC0_SUBTYPE_AUTH 0xb0
#define __BITS(hi, lo) (((1u << ((hi) - (lo) + 1)) - 1) << (lo))
#define __BIT(n) (1u << (n))
#define __SHIFTOUT(v, m) (((v) & (m)) >> __builtin_ctz(m))
#include "abi.h"
#include "routes.h"
#include "actions.h"
#include "aicwf-defs.h"
#include "if_aicwf_sae.h"

struct ifnet { void *if_softc; };
struct ieee80211_chan { uint16_t ic_freq; };
struct ieee80211_node {
	uint8_t ni_bssid[6];
	struct ieee80211_chan *ni_chan;
};
struct ieee80211com {
	int ic_state;
	uint8_t ic_myaddr[6];
	struct ieee80211_node *ic_bss;
	struct ifnet *ic_ifp;
};
struct ieee80211_frame { uint8_t bytes[24]; };
struct mbuf { int unused; };
struct aicwf_softc {
	struct ieee80211com sc_ic;
	struct ifnet sc_if;
	int sc_lock;
	bool sc_if_attached, sc_connected, sc_sta_enabled;
	bool sc_sae_enabled, sc_sae_authenticated, sc_ptk_valid;
	uint8_t sc_sae_bssid[6], sc_hwkey[5], sc_vif;
	uint32_t sc_sae_generation;
	uint64_t sc_sae_epoch, sc_pmf_rx_pn;
	u_int sc_alive_rx;
	/* Keep the baseline layout usable for pre-fix regression checks. */
	bool sc_sta_tx_pending;
	uint32_t sc_sta_cookie, sc_sta_generation;
};
static struct aicwf_softc sc;
static struct ieee80211_node ni;
static struct ieee80211_chan chan;
static unsigned checks, deliveries, disconnects, allocs;
static bool failalloc, retire_on_alloc;
static int event_type;
static uint8_t event_data[sizeof(struct ieee80211req_sae)];
static const u_int ticks = 123;
static void mutex_enter(int *lock) { assert(*lock == 0); *lock = 1; }
static void mutex_exit(int *lock) { assert(*lock == 1); *lock = 0; }
static bool __attribute__((unused)) mutex_owned(const int *lock) { return *lock == 1; }
static void *kmem_zalloc(size_t n, int flags)
{
	(void)flags;
	allocs++;
	if (retire_on_alloc) {
		sc.sc_sae_epoch++;
		retire_on_alloc = false;
	}
	return failalloc ? NULL : calloc(1, n);
}
static void kmem_free(void *p, size_t n) { (void)n; free(p); }
static void rt_ieee80211msg(struct ifnet *ifp, int code, void *p, size_t n)
{
	assert(ifp == &sc.sc_if && n <= sizeof(event_data));
	assert(!sc.sc_lock);
	deliveries++;
	event_type = code;
	memset(event_data, 0, sizeof(event_data));
	memcpy(event_data, p, n);
}
static void aicwf_alive_lost(struct aicwf_softc *s)
{
	assert(s == &sc && !s->sc_lock);
	disconnects++;
}
static void aicwf_sae_event(struct aicwf_softc *s, bool start,
    const uint8_t *data, size_t len) { assert(false); }
static int __attribute__((unused)) aicwf_sae_tx(struct aicwf_softc *s,
    const uint8_t *f, size_t len, uint64_t epoch, bool query) { return 0; }
#ifndef __NetBSD__
static uint16_t __attribute__((unused)) le16dec(const void *v)
{
	const uint8_t *p = v;
	return p[0] | (uint16_t)p[1] << 8;
}
static uint32_t le32dec(const void *v)
{
	const uint8_t *p = v;
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}
static void le32enc(void *v, uint32_t x)
{
	uint8_t *p = v;
	p[0] = x; p[1] = x >> 8; p[2] = x >> 16; p[3] = x >> 24;
}
#else
#include <sys/endian.h>
#endif
#include "driver.h"

struct bsd_driver_data {
	int sta_btm, sta_rrm;
	uint8_t sta_addr[6], sta_extcap[4], sta_extcap_mask[4];
	struct {
		uint8_t *extended_capa, *extended_capa_mask;
		size_t extended_capa_len;
		unsigned rrm_flags;
	} capa;
};
#define os_memcpy memcpy
#define MSG_DEBUG 0
#define wpa_printf(...) ((void)0)
#define WPA_DRIVER_FLAGS_SUPPORT_RRM 1
#define WPA_DRIVER_FLAGS_RRM_NEIGHBOR_REPORT_ONLY 2
static int bsd_sta_info(struct bsd_driver_data *drv,
    struct ieee80211req_sta_mgmt *req)
{
	memset(req, 0, sizeof(*req));
	req->version = IEEE80211_STA_VERSION;
	req->op = IEEE80211_STA_GET;
	return aicwf_sta_mgmt(&sc.sc_ic, req) ? -1 : 0;
}
#include "client.h"
#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #e); exit(1); \
} checks++; } while (0)

static void
reset(void)
{
	memset(&sc, 0, sizeof(sc));
	chan.ic_freq = 5180;
	ni.ni_chan = &chan;
	memcpy(ni.ni_bssid, "\x02\x01\x02\x03\x04\x05", 6);
	sc.sc_if.if_softc = &sc;
	sc.sc_ic.ic_ifp = &sc.sc_if;
	sc.sc_ic.ic_bss = &ni;
	sc.sc_ic.ic_state = IEEE80211_S_RUN;
	memcpy(sc.sc_ic.ic_myaddr, "\x02\x05\x04\x03\x02\x01", 6);
	memcpy(sc.sc_sae_bssid, ni.ni_bssid, 6);
	sc.sc_connected = sc.sc_if_attached = true;
	sc.sc_sae_authenticated = sc.sc_ptk_valid = true;
	sc.sc_sae_epoch = 39;
	sc.sc_sae_generation = 41;
	sc.sc_vif = 1;
	sc.sc_hwkey[AICWF_KEY_SLOT_PAIRWISE] = 5;
	deliveries = disconnects = allocs = 0;
	failalloc = retire_on_alloc = false;
}

static void
abi_checks(void)
{
	struct ieee80211req_sta_mgmt req;
	struct bsd_driver_data drv = {0};

	reset();
	CHECK(bsd_sta_info(&drv, &req) == 0);
	CHECK(req.len == ETH_ALEN);
	CHECK(memcmp(req.data, sc.sc_ic.ic_myaddr, ETH_ALEN) == 0);
	CHECK(req.flags == IEEE80211_STA_ASSOCIATED && req.freq == 5180);
	CHECK(memcmp(req.bssid, ni.ni_bssid, ETH_ALEN) == 0);
	CHECK(req.generation == 39);
	bsd_sta_init(&drv);
	CHECK(memcmp(drv.sta_addr, sc.sc_ic.ic_myaddr, ETH_ALEN) == 0);
	CHECK(!drv.sta_btm && !drv.sta_rrm && drv.capa.rrm_flags == 0);
	CHECK(drv.sta_extcap_mask[2] == 0x0a && drv.sta_extcap_mask[3] == 2);
	/* A stale internal enable bit must not advertise an incomplete stack. */
	sc.sc_sta_enabled = sc.sc_sta_tx_pending = true;
	CHECK(bsd_sta_info(&drv, &req) == 0);
	CHECK(req.flags == IEEE80211_STA_ASSOCIATED);
	for (unsigned op = IEEE80211_STA_ENABLE; op <= IEEE80211_STA_CANCEL_SCAN; op++) {
		req.op = op;
		CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) ==
		    (op == IEEE80211_STA_DISABLE ? 0 : EOPNOTSUPP));
	}
	CHECK(!sc.sc_sta_enabled);
	sc.sc_connected = false;
	sc.sc_sae_epoch++;
	CHECK(bsd_sta_info(&drv, &req) == 0);
	CHECK(req.flags == 0 && req.freq == 0 && req.generation == 40);
	CHECK(memcmp(req.bssid, "\0\0\0\0\0\0", 6) == 0);
	CHECK(req.len == ETH_ALEN);
	req.version++;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req.version--;
	req.reserved = 1;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
}

static void
action_checks(void)
{
	struct bsd_driver_data drv = {.sta_btm = 1, .sta_rrm = 1};
	uint8_t b[IEEE80211_STA_MAX_BODY + 1] = {0};
	const unsigned tx[] = {WNM_BSS_TRANS_MGMT_QUERY, WNM_BSS_TRANS_MGMT_RESP};
	const unsigned rx[] = {WLAN_RRM_NEIGHBOR_REPORT_RESPONSE,
	    WLAN_RRM_RADIO_MEASUREMENT_REQUEST};

	b[0] = WLAN_ACTION_WNM;
	b[1] = WNM_BSS_TRANS_MGMT_REQ;
	CHECK(bsd_sta_rx_body(&drv, b, 7) && aicwf_sta_action_valid(b, 7, false));
	CHECK(!aicwf_sta_action_valid(b, 6, false));
	CHECK(!aicwf_sta_action_valid(b, 7, true));
	for (size_t i = 0; i < sizeof(tx) / sizeof(tx[0]); i++) {
		b[1] = tx[i];
		CHECK(bsd_sta_tx_body(&drv, b, 11) && aicwf_sta_action_valid(b, 11, true));
		CHECK(!aicwf_sta_action_valid(b, 11, false));
	}
	b[1] = WNM_BSS_TRANS_MGMT_RESP;
	CHECK(!aicwf_sta_action_valid(b, 5, true)); /* acceptance needs target */
	b[3] = 1;
	CHECK(aicwf_sta_action_valid(b, 5, true));
	CHECK(!aicwf_sta_action_valid(b, 4, true));
	b[0] = WLAN_ACTION_RADIO_MEASUREMENT;
	b[1] = WLAN_RRM_NEIGHBOR_REPORT_REQUEST;
	CHECK(bsd_sta_tx_body(&drv, b, 3) && aicwf_sta_action_valid(b, 3, true));
	b[1] = WLAN_RRM_RADIO_MEASUREMENT_REPORT;
	CHECK(bsd_sta_tx_body(&drv, b, 3) && aicwf_sta_action_valid(b, 3, true));
	for (size_t i = 0; i < sizeof(rx) / sizeof(rx[0]); i++) {
		b[1] = rx[i];
		CHECK(bsd_sta_rx_body(&drv, b, 5) && aicwf_sta_action_valid(b, 5, false));
	}
	CHECK(!aicwf_sta_action_valid(b, 4, false));
	CHECK(!aicwf_sta_action_valid(b, sizeof(b), false));
	CHECK(!aicwf_sta_action_valid(b, 2, false));
}

static size_t
frame_make(uint8_t *f, bool pmf, uint8_t subtype, uint8_t category, uint8_t action)
{
	const size_t hdr = pmf ? 32 : 24;
	memset(f, 0, 1600);
	f[0] = subtype;
	f[1] = pmf ? 0x40 : 0;
	memcpy(f + 4, sc.sc_ic.ic_myaddr, 6);
	memcpy(f + 10, ni.ni_bssid, 6);
	memcpy(f + 16, ni.ni_bssid, 6);
	if (pmf) {
		f[24] = 1;
		f[27] = 0x20;
	}
	f[hdr] = category;
	f[hdr + 1] = action;
	return hdr;
}

static void
receive(const uint8_t *f, size_t len, uint32_t status, uint32_t flags)
{
	uint8_t pkt[AICWF_RXHDR_LEN + 1600] = {0};
	assert(len <= 1600);
	memcpy(pkt + AICWF_RXHDR_LEN, f, len);
	le32enc(pkt + AICWF_RXHDR_STATUS, status);
	le32enc(pkt + AICWF_RXHDR_FLAGS, flags);
	aicwf_rx_data(&sc, pkt, len);
}

static void
rx_checks(void)
{
	const uint32_t success = 0x6000;
	const uint32_t ccmp = success | 0x02000000 | 12 | (5u << 15);
	const uint32_t vif = 0x100 | AICWF_RX_FLAG_UPLOAD;
	uint8_t f[1600];
	size_t hdr;
	struct ieee80211req_sta_mgmt ev;

	reset();
	hdr = frame_make(f, false, 0xd0, WLAN_ACTION_WNM, WNM_BSS_TRANS_MGMT_REQ);
	receive(f, hdr + 7, success, vif);
	CHECK(disconnects == 0 && deliveries == 0); /* no in-driver BTM policy */
	/* Exercise the preparatory receive path without advertising support. */
	sc.sc_sta_enabled = true;
	receive(f, hdr + 7, success, vif);
	CHECK(disconnects == 0 && deliveries == 1);
	memcpy(&ev, event_data, sizeof(ev));
	CHECK(event_type == RTM_IEEE80211_STA_RX && ev.generation == 39);
	CHECK(ev.len == 7 && ev.freq == 5180 && ev.cookie == 0 && ev.flags == 0);
	CHECK(memcmp(ev.data, f + hdr, 7) == 0 && memcmp(ev.bssid, ni.ni_bssid, 6) == 0);
	const unsigned before = deliveries;
	f[10] ^= 2; receive(f, hdr + 7, success, vif); f[10] ^= 2;
	CHECK(deliveries == before);
	f[4] ^= 2; receive(f, hdr + 7, success, vif); f[4] ^= 2;
	CHECK(deliveries == before);
	f[16] ^= 2; receive(f, hdr + 7, success, vif); f[16] ^= 2;
	CHECK(deliveries == before);
	receive(f, hdr + 7, success, vif ^ 0x100);
	CHECK(deliveries == before);
	receive(f, hdr + 7, success | 0x100, vif);
	CHECK(deliveries == before);
	f[22] = 1; receive(f, hdr + 7, success, vif); f[22] = 0;
	CHECK(deliveries == before);
	retire_on_alloc = true; receive(f, hdr + 7, success, vif);
	CHECK(deliveries == before && sc.sc_sae_epoch == 40);
	failalloc = true; receive(f, hdr + 7, success, vif); failalloc = false;
	CHECK(deliveries == before);
	/* Both maximum accepted length and one-byte overflow cross the raw RX path. */
	f[25] = WNM_BSS_TRANS_MGMT_REQ;
	receive(f, hdr + IEEE80211_STA_MAX_BODY, success, vif);
	CHECK(deliveries == before + 1);
	receive(f, hdr + IEEE80211_STA_MAX_BODY + 1, success, vif);
	CHECK(deliveries == before + 1);

	reset();
	sc.sc_sta_enabled = sc.sc_sae_enabled = true;
	hdr = frame_make(f, true, 0xd0, WLAN_ACTION_WNM, WNM_BSS_TRANS_MGMT_REQ);
	receive(f, hdr + 7, ccmp, vif);
	CHECK(deliveries == 1 && disconnects == 0 && sc.sc_pmf_rx_pn == 1);
	memcpy(&ev, event_data, sizeof(ev));
	CHECK(ev.len == 7 && memcmp(ev.data, f + 32, 7) == 0); /* retained CCMP8 */
	receive(f, hdr + 7, ccmp, vif);
	CHECK(deliveries == 1 && sc.sc_pmf_rx_pn == 1); /* replay */
	f[24] = 2;
	receive(f, hdr + 7, ccmp ^ (1u << 15), vif);
	CHECK(deliveries == 1 && sc.sc_pmf_rx_pn == 1); /* wrong key */
	receive(f, hdr + 7, ccmp | 0x100, vif);
	CHECK(deliveries == 1 && sc.sc_pmf_rx_pn == 1); /* MIC/error */
	receive(f, hdr + 7, ccmp & ~0x02000000u, vif);
	CHECK(deliveries == 1 && sc.sc_pmf_rx_pn == 1);
	f[27] = 0; receive(f, hdr + 7, ccmp, vif); f[27] = 0x20;
	CHECK(deliveries == 1 && sc.sc_pmf_rx_pn == 1);
	sc.sc_ptk_valid = false; receive(f, hdr + 7, ccmp, vif); sc.sc_ptk_valid = true;
	CHECK(deliveries == 1 && sc.sc_pmf_rx_pn == 1);
	sc.sc_sae_authenticated = false; receive(f, hdr + 7, ccmp, vif); sc.sc_sae_authenticated = true;
	CHECK(deliveries == 1 && sc.sc_pmf_rx_pn == 1);
	failalloc = true; receive(f, hdr + 7, ccmp, vif); failalloc = false;
	CHECK(deliveries == 1 && sc.sc_pmf_rx_pn == 1);
	receive(f, hdr + IEEE80211_STA_MAX_BODY, ccmp, vif);
	CHECK(deliveries == 2 && sc.sc_pmf_rx_pn == 2);
	f[24] = 3;
	receive(f, hdr + IEEE80211_STA_MAX_BODY + 1, ccmp, vif);
	CHECK(deliveries == 2 && sc.sc_pmf_rx_pn == 2);
	hdr = frame_make(f, false, 0xd0, WLAN_ACTION_WNM, WNM_BSS_TRANS_MGMT_REQ);
	receive(f, hdr + 7, success, vif);
	CHECK(deliveries == 2 && disconnects == 0); /* PMF downgrade */

	/* A deauthentication must pass the same peer/key/PN admission. */
	hdr = frame_make(f, true, 0xc0, 3, 0);
	f[24] = 3;
	receive(f, hdr + 2, ccmp | 0x100, vif);
	CHECK(disconnects == 0);
	receive(f, hdr + 2, ccmp, vif);
	CHECK(disconnects == 1 && sc.sc_pmf_rx_pn == 3);
	receive(f, hdr + 2, ccmp, vif);
	CHECK(disconnects == 1);
	/* Keep the actual existing SA Query receiver in the dispatcher test. */
	hdr = frame_make(f, false, 0xc0, 7, 0);
	receive(f, hdr + 2, 0, vif);
	CHECK(deliveries == 3 && event_type == RTM_IEEE80211_SAE && disconnects == 1);
	hdr = frame_make(f, true, 0xd0, 8, 1); f[24] = 4;
	receive(f, hdr + 4, ccmp, vif);
	CHECK(deliveries == 4 && event_type == RTM_IEEE80211_SAE && sc.sc_pmf_rx_pn == 4);
	receive(f, hdr + 4, ccmp, vif);
	CHECK(deliveries == 4);
	reset();
	hdr = frame_make(f, false, 0xc0, 3, 0);
	f[10] ^= 2; receive(f, hdr + 2, success, vif); f[10] ^= 2;
	CHECK(disconnects == 0);
	receive(f, hdr + 2, success, vif);
	CHECK(disconnects == 1);
}

static void
oversize_check(void)
{
	uint8_t f[1600];
	reset();
	sc.sc_sta_enabled = true;
	/* The old parser accepted WNM action5 and copied beyond data[1024]. */
	const size_t hdr = frame_make(f, false, 0xd0, WLAN_ACTION_WNM, 5);
	receive(f, hdr + IEEE80211_STA_MAX_BODY + 1, 0x02006000,
	    0x100 | AICWF_RX_FLAG_UPLOAD);
	CHECK(deliveries == 0 && disconnects == 0);
}

int
main(int argc, char **argv)
{
	const char *group = argc > 1 ? argv[1] : "all";
	if (!strcmp(group, "all") || !strcmp(group, "abi")) abi_checks();
	if (!strcmp(group, "all") || !strcmp(group, "actions")) action_checks();
	if (!strcmp(group, "all") || !strcmp(group, "rx")) rx_checks();
	if (!strcmp(group, "all") || !strcmp(group, "oversize")) oversize_check();
	CHECK(checks != 0);
	printf("PASS: %u AIC station/client and management RX checks\n", checks);
	return 0;
}
