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
enum ieee80211_state {
	IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
	IEEE80211_S_ASSOC, IEEE80211_S_RUN
};
#define IEEE80211_AUTH_ALG_OPEN 0
#define ETHERTYPE_PAE 0x888e
#define IEEE80211_IS_CHAN_5GHZ(c) ((c)->ic_freq >= 4000)
#define AICWF_MM_KEY_DEL_REQ 0x16
#define AICWF_MM_KEY_DEL_CFM 0x17
#define IEEE80211_REASON_AUTH_LEAVE 3
#define IEEE80211_M_STA 1
#define IEEE80211_CHAN_ANYC NULL
#define IEEE80211_CHAN_MAX 255
#define IEEE80211_F_SCAN 0x10
#define IEEE80211_F_WPA2 2
#define IEEE80211_F_WPA 3
#define IFF_RUNNING 1
#define AICWF_TX_POWER_DBM 20
#define AICWF_HWQ_VO 3
#define AICWF_TID_NONE 255
#define AICWF_TX_MGMT 8
#define AICWF_TX_RESERVE 2
#define AICWF_BLOCK_SIZE 512
#define AICWF_CHAN_MAX 42
#define AICWF_CHAN_LEN 6
#define AICWF_HDR_LEN 4
#define AICWF_TXDESC_LEN 28
#define AICWF_TYPE_DATA 1
#define AICWF_TYPE_CFG 0x10
#define AICWF_TYPE_MSG 0x11
#define AICWF_TYPE_TX_CFM 0x12
#define AICWF_RX_MSG_OFFSET 4
#define AICWF_TX_MSG_OFFSET 8
#define AICWF_MSG_HDR_LEN 8
#define AICWF_MSG_MAX_PARAM 1032
#define AICWF_TASK(id) ((id) >> 10)
#define AICWF_TASK_DRIVER 100
#define AICWF_CMD_TIMEOUT_MS 2000
#define AICWF_SCAN_INTERVAL_MS 3000
#define AICWF_SCANU_START_REQ 0x1000
#define AICWF_SCANU_START_ACK 0x1009
#define AICWF_SCANU_CANCEL_REQ 0x100a
#define AICWF_SCANU_CANCEL_CFM 0x100b
#define PR_NOWAIT 0
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define roundup(x,y) (((x)+(y)-1)/(y)*(y))
#define isset(a,n) ((a)[(n)/8] & (1u << ((n)%8)))
#define setbit(a,n) ((a)[(n)/8] |= (1u << ((n)%8)))
#define explicit_memset memset
#define KM_SLEEP 0
#define KM_NOSLEEP 1
#define KASSERT assert
#define IEEE80211_DPRINTF(...) ((void)0)
static void device_printf(void *dev, const char *fmt, ...)
{ (void)dev; (void)fmt; }
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

struct ifnet { void *if_softc; int if_flags, ioctl_locked; };
struct ieee80211_chan { uint16_t ic_freq; };
struct ieee80211_node {
	uint8_t ni_bssid[6];
	uint8_t ni_essid[32], ni_esslen;
	struct ieee80211_chan *ni_chan;
};
struct ieee80211com {
	int ic_state, ic_opmode, ic_flags;
	unsigned ic_des_esslen; uint8_t ic_des_essid[32];
	uint8_t *ic_opt_ie; size_t ic_opt_ie_len;
	struct ieee80211_chan ic_channels[256];
	uint8_t ic_chan_active[32];
	struct { unsigned nt_scan_epoch; } ic_scan;
	uint8_t ic_myaddr[6];
	struct ieee80211_node *ic_bss;
	struct ifnet *ic_ifp;
};
struct ieee80211_frame { uint8_t bytes[24]; };
struct mbuf { int unused; };
struct work { unsigned unused; };
enum aicwf_task_cmd { AICWF_TASK_STA_MGMT, AICWF_TASK_STA_CANCEL_SCAN, AICWF_TASK_NEWSTATE, AICWF_TASK_RECOVER_SCAN };
struct aicwf_task { struct work t_work; enum aicwf_task_cmd t_cmd;
    uint64_t t_epoch, t_join_serial, t_configure_serial; enum ieee80211_state t_state; int t_arg; struct ieee80211req_sta_mgmt t_sta; };
struct aicwf_softc {
	struct ieee80211com sc_ic;
	struct ifnet sc_if;
	int sc_lock, sc_epoch_lock, sc_bus_lock;
	uint8_t sc_ptk[16];
	bool sc_if_attached, sc_connected, sc_sta_enabled;
	bool sc_sae_enabled, sc_sae_pending, sc_sae_authenticated, sc_ptk_valid;
	uint8_t sc_sae_bssid[6], sc_hwkey[5], sc_vif;
	uint32_t sc_sae_generation;
	uint64_t sc_sae_epoch, sc_pmf_rx_pn, sc_join_serial, sc_configure_serial;
	uint64_t sc_alive_epoch, sc_alive_join_serial;
	bool sc_alive_queued; unsigned sc_alive_misses;
	int (*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);
	uint8_t sc_igtk[2];
	u_int sc_alive_rx;
	/* Keep the baseline layout usable for pre-fix regression checks. */
	bool sc_sta_tx_pending;
	uint32_t sc_sta_cookie, sc_sta_generation;
	uint32_t sc_fw_version, sc_sta_token, sc_sta_scan_serial;
	uint16_t sc_sae_caps, sc_sta_assoc_len;
	uint8_t sc_sta_assoc[804], sc_ap, sc_txbuf[2048];
	bool sc_connecting, sc_scanning, sc_sta_tx_sent, sc_sta_tx_mismatch_reported;
	bool sc_sta_scan_pending, sc_sta_scan_started, sc_scan_published;
	bool sc_sta_scan_draining, sc_sta_cancel_queued;
	struct ieee80211req_sta_mgmt sc_sta_tx, sc_sta_scan;
	int sc_scan_ticks, sc_cv;
	void *sc_dev, *sc_taskpool, *sc_taskq;
	bool sc_cmd_busy, sc_cfm_done;
	uint16_t sc_cfm_len, sc_cfm_id;
	uint8_t sc_cfm[AICWF_MSG_MAX_PARAM];
};
static struct aicwf_softc sc;
static struct ieee80211_node ni;
static struct ieee80211_chan chan;
static unsigned checks, deliveries, disconnects, allocs;
static unsigned writes, tasks_count, scans_cancelled, scans_ended, state_calls;
static struct aicwf_task *tasks[64];
static int write_error, pre_fifo_error, reply_len = 1, reply_status;
static uint8_t written[2048];
static size_t written_len;
static void (*wait_hook)(void), (*reply_hook)(void), (*unlock_hook)(void);
static void ifnet_lock(struct ifnet *ifp) { assert(!ifp->ioctl_locked); ifp->ioctl_locked = 1; }
static void ifnet_unlock(struct ifnet *ifp)
{ assert(ifp->ioctl_locked); ifp->ioctl_locked = 0; if (unlock_hook) { void (*hook)(void) = unlock_hook; unlock_hook = NULL; hook(); } }
#define IFNET_LOCK ifnet_lock
#define IFNET_UNLOCK ifnet_unlock
#define IFNET_LOCKED(ifp) ((ifp)->ioctl_locked)
static bool failalloc, retire_on_alloc, failtask;
static int event_type;
static uint8_t event_data[sizeof(struct ieee80211req_sae)];
static const u_int ticks = 123;
static void mutex_enter(int *lock) { assert(*lock == 0); *lock = 1; }
static void mutex_exit(int *lock) { assert(*lock == 1); *lock = 0; }
#define mutex_spin_enter mutex_enter
#define mutex_spin_exit mutex_exit
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
static void le16enc(void *v, uint16_t x)
{ uint8_t *p=v; p[0]=x; p[1]=x>>8; }
static void be16enc(void *v, uint16_t x)
{ uint8_t *p=v; p[0]=x>>8; p[1]=x; }
#include "fullmac_sae.h"
#include "if_aicwf_vht.h"
#include "if_aicwf_sta.h"
static int splnet(void) { return 0; }
static void splx(int s) { (void)s; }
static int getticks(void) { return ticks; }
static int mstohz(int n) { return n; }
static unsigned ieee80211_mhz2ieee(unsigned f, int flags)
{ (void)flags; return f < 3000 ? (f-2407)/5 : (f-5000)/5; }
static void ieee80211_cancel_scan(struct ieee80211com *ic)
{ scans_cancelled++; ic->ic_flags &= ~IEEE80211_F_SCAN; }
static void ieee80211_end_scan(struct ieee80211com *ic)
{ (void)ic; scans_ended++; }
static int aicwf_newstate(struct ieee80211com *, enum ieee80211_state, int);
static int net80211_state(struct ieee80211com *ic, enum ieee80211_state state, int arg)
{ (void)arg; assert(sc.sc_if.ioctl_locked); state_calls++; ic->ic_state = state; return 0; }
static void ieee80211_sta_leave(struct ieee80211com *ic, struct ieee80211_node *node)
{ assert(ic->ic_bss == node); }
static void kpause(const char *name, bool intr, int delay, void *lock)
{ (void)name; (void)intr; (void)delay; (void)lock; }
static void aicwf_reorder_flush(struct aicwf_softc *s, int tid, bool deliver)
{ (void)s; assert(tid == -1 && !deliver); }
static void *pool_cache_get(void *pool, int flags)
{ (void)pool;assert(flags==PR_NOWAIT);return failtask?NULL:calloc(1,sizeof(struct aicwf_task)); }
static void workqueue_enqueue(void *q, struct work *w, void *arg)
{ (void)q;(void)arg;assert(tasks_count<__arraycount(tasks));tasks[tasks_count++]=(void *)w; }
static uint8_t aicwf_crc8(const void *p, size_t n)
{ (void)p;(void)n;return 0; }
static unsigned aicwf_chan_list(struct aicwf_softc *s, uint8_t *two,
    uint8_t *five, unsigned *n5g)
{ (void)s; (void)two; le16enc(five, 5180); *n5g = 1; return 0; }
static int aicwf_tx_wait(struct aicwf_softc *s, size_t n, unsigned reserve)
{
	assert(s == &sc && s->sc_lock && n == 512);
	assert(reserve == (s->sc_txbuf[2] == AICWF_TYPE_MSG ? 0 : 2));
	return pre_fifo_error;
}
static int aicwf_fifo(struct aicwf_softc *s, bool write, uint8_t *data, size_t n)
{
	assert(s == &sc && s->sc_lock && s->sc_bus_lock && write && n == 512);
	writes++;
	memcpy(written, data, n);
	written_len = le16dec(data) + 4;
	if (data[2] == AICWF_TYPE_MSG) {
		s->sc_cfm_done = reply_len >= 0;
		s->sc_cfm_len = reply_len >= 0 ? reply_len : 0;
		memset(s->sc_cfm, 0, sizeof(s->sc_cfm));
		s->sc_cfm[0] = reply_status;
	}
	return write_error;
}
static void cv_wait(int *cv, int *lock)
{ (void)cv;assert(*lock && wait_hook);mutex_exit(lock);wait_hook();mutex_enter(lock);sc.sc_cmd_busy=false; }
static int cv_timedwait(int *cv, int *lock, int delay)
{
	(void)cv; assert(*lock && delay == 2000);
	if (reply_hook != NULL) {
		void (*hook)(void) = reply_hook; reply_hook = NULL;
		mutex_exit(lock); hook(); mutex_enter(lock);
		sc.sc_cfm_done = true; sc.sc_cfm_len = 1;
		return 0;
	}
	return ETIMEDOUT;
}
static void cv_broadcast(int *cv) { (void)cv; }
static void aicwf_rx_data(struct aicwf_softc *, const uint8_t *, size_t);
static void aicwf_scan_done(struct aicwf_softc *, const uint8_t *, size_t);
static void aicwf_rx_msg(struct aicwf_softc *s, const uint8_t *msg, size_t len)
{
    if(len>=15 && le16dec(msg)==0x1001 && le16dec(msg+6)>=3 && le16dec(msg+6)<=len-12)
        aicwf_scan_done(s,msg+12,le16dec(msg+6));
}
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
	for (unsigned i=0;i<tasks_count;i++) free(tasks[i]);
	tasks_count=0;
	memset(&sc, 0, sizeof(sc));
	static uint8_t rsn[]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
	rsn[19]=2;rsn[20]=0;sc.sc_ic.ic_opt_ie=rsn;sc.sc_ic.ic_opt_ie_len=sizeof(rsn);
	sc.sc_ic.ic_flags=IEEE80211_F_WPA2;sc.sc_ic.ic_opmode=IEEE80211_M_STA;
	sc.sc_if.if_flags=IFF_RUNNING;
	sc.sc_fw_version=0x06090101;sc.sc_sae_caps=IEEE80211_SAE_CAP_PMF;
	sc.sc_sta_assoc_len=8;le16enc(sc.sc_sta_assoc,2);le16enc(sc.sc_sta_assoc+2,2);
	sc.sc_sta_assoc[4]=0;sc.sc_sta_assoc[6]=0;
	sc.sc_ic.ic_channels[36].ic_freq=5180;setbit(sc.sc_ic.ic_chan_active,36);
	sc.sc_ic.ic_channels[40].ic_freq=5200;setbit(sc.sc_ic.ic_chan_active,40);
	writes=write_error=pre_fifo_error=reply_status=0;reply_len=1;failtask=false;wait_hook=reply_hook=unlock_hook=NULL;
	memset(sc.sc_igtk, AICWF_HWKEY_NONE, sizeof(sc.sc_igtk));
	scans_cancelled=scans_ended=state_calls=0;
	chan.ic_freq = 5180;
	ni.ni_chan = &chan;
	ni.ni_esslen = 4; memcpy(ni.ni_essid, "test", 4);
	sc.sc_newstate = net80211_state;
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
    struct bsd_driver_data drv={0};
    const unsigned caps=IEEE80211_STA_CAP_BTM|IEEE80211_STA_CAP_RRM;
    reset();
    CHECK(bsd_sta_info(&drv,&req)==0 && req.len==14 && req.generation==39);
    CHECK(memcmp(req.data,sc.sc_ic.ic_myaddr,6)==0);
    CHECK(req.flags==(caps|IEEE80211_STA_ASSOCIATED|IEEE80211_STA_ASSOC_IES));
    CHECK(req.freq==5180 && memcmp(req.bssid,ni.ni_bssid,6)==0);
    CHECK(le16dec(req.data+6)==2 && le16dec(req.data+8)==2);
    bsd_sta_init(&drv);
    CHECK(drv.sta_btm && drv.sta_rrm && drv.capa.rrm_flags);
    CHECK(memcmp(drv.sta_addr,sc.sc_ic.ic_myaddr,6)==0);
    memset(&req,0,sizeof(req));req.version=IEEE80211_STA_VERSION;req.op=IEEE80211_STA_ENABLE;
    CHECK(aicwf_sta_mgmt(&sc.sc_ic,&req)==0 && sc.sc_sta_enabled);
    req.op=IEEE80211_STA_DISABLE;
    CHECK(aicwf_sta_mgmt(&sc.sc_ic,&req)==0 && !sc.sc_sta_enabled);
    sc.sc_connected=false;sc.sc_sae_epoch++;
    CHECK(bsd_sta_info(&drv,&req)==0 && req.flags==caps && req.generation==40 && req.freq==0);
    CHECK(req.len==6 && memcmp(req.bssid,"\0\0\0\0\0\0",6)==0);
    req.version++;CHECK(aicwf_sta_mgmt(&sc.sc_ic,&req)==EINVAL);req.version--;
    req.reserved=1;CHECK(aicwf_sta_mgmt(&sc.sc_ic,&req)==EINVAL);req.reserved=0;
    sc.sc_fw_version=0;CHECK(aicwf_sta_mgmt(&sc.sc_ic,&req)==EOPNOTSUPP);
    /* Unknown operations retain the shared backend's identity-check order. */
    reset();memset(&req,0,sizeof(req));req.version=IEEE80211_STA_VERSION;
    req.op=UINT16_MAX;req.generation=sc.sc_sae_epoch;
    memcpy(req.bssid,ni.ni_bssid,sizeof(req.bssid));
    CHECK(aicwf_sta_mgmt(&sc.sc_ic,&req)==EOPNOTSUPP);
    req.generation++;CHECK(aicwf_sta_mgmt(&sc.sc_ic,&req)==ESTALE);req.generation--;
    req.bssid[5]^=1;CHECK(aicwf_sta_mgmt(&sc.sc_ic,&req)==ESTALE);req.bssid[5]^=1;
    req.flags=1;CHECK(aicwf_sta_mgmt(&sc.sc_ic,&req)==EINVAL);
    CHECK(writes==0 && tasks_count==0 && deliveries==0 && !sc.sc_sta_enabled);
    reset();sc.sc_ic.ic_opt_ie=NULL;sc.sc_ic.ic_opt_ie_len=0;
    CHECK(bsd_sta_info(&drv,&req)==0 && !(req.flags&caps));
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
	sc.sc_sta_enabled = sc.sc_sae_enabled = true; sc.sc_ic.ic_opt_ie[19] = 8; sc.sc_ic.ic_opt_ie[20] = 0xc0;
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

static struct ieee80211req_sta_mgmt
request_make(unsigned op, unsigned cookie)
{
	struct ieee80211req_sta_mgmt req = {0};
	req.version = IEEE80211_STA_VERSION;
	req.op = op;
	req.generation = (uint32_t)sc.sc_sae_epoch;
	req.cookie = cookie;
	req.freq = ni.ni_chan->ic_freq;
	memcpy(req.bssid, ni.ni_bssid, 6);
	if (op == IEEE80211_STA_TX) {
		req.len = 3;
		req.data[0] = 5;
		req.data[1] = 4;
		req.data[2] = 19;
	}
	return req;
}

static void
ioctl_rejection_checks(void)
{
	struct ieee80211req_sta_mgmt req;

	/* Driver-level cases from the shared live bwfm-sta-ioctl checker. */
	reset(); sc.sc_sta_enabled = true;
	req = request_make(IEEE80211_STA_TX, 1); req.version++;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_TX, 1); req.reserved = 1;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_TX, 1); req.flags = UINT32_C(0x80000000);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_TX, 1); req.len = sizeof(req.data) + 1;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(UINT16_MAX, 1);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EOPNOTSUPP);
	req = request_make(IEEE80211_STA_TX, 1); req.generation ^= UINT32_C(0x80000000);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == ESTALE);
	req = request_make(IEEE80211_STA_TX, 1); req.bssid[5] ^= 1;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == ESTALE);
	req = request_make(IEEE80211_STA_TX, 1); req.len = 2;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_TX, 1); req.len = 4;
	req.data[0] = 10; req.data[1] = 7;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_TX, 1); req.len = 10;
	req.data[0] = 10; req.data[1] = 8; req.data[3] = 0;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_ENABLE, 1); req.len = 1;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_DISABLE, 1); req.len = 1;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_CANCEL_TX, 1); req.len = 1;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_SCAN, 1); req.len = 1;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_SCAN, 1);
	req.len = 2 * (IEEE80211_STA_MAX_CHANNELS + 1);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req = request_make(IEEE80211_STA_CANCEL_SCAN, 0); req.len = 1;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == ESTALE);
	CHECK(writes == 0 && tasks_count == 0 && deliveries == 0);
	CHECK(sc.sc_sta_enabled && sc.sc_connected && sc.sc_ptk_valid &&
	    sc.sc_sae_epoch == 39);
	/* A valid scan cookie must still reject a nonempty cancellation. */
	sc.sc_sta_scan_pending = true; sc.sc_sta_scan.cookie = 1; req.cookie = 1;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	CHECK(sc.sc_sta_scan_pending && deliveries == 0 && tasks_count == 0);
}

static void
run_task(void)
{
	CHECK(tasks_count != 0);
	struct aicwf_task *t = tasks[0];
	memmove(tasks, tasks + 1, --tasks_count * sizeof(*tasks));
	if (t->t_cmd == AICWF_TASK_STA_MGMT)
		aicwf_sta_mgmt_cb(&sc, t);
	else if (t->t_cmd == AICWF_TASK_STA_CANCEL_SCAN)
		aicwf_sta_cancel_scan_cb(&sc, t->t_sta.flags);
	else if (t->t_cmd == AICWF_TASK_RECOVER_SCAN)
		aicwf_recover_scan_cb(&sc, t);
	else
		aicwf_state_cb(&sc, t);
	free(t);
}

static void
cfm_receive(uint32_t status, uint32_t token, size_t len)
{
	uint8_t packet[12] = {0};
	le16enc(packet, len);
	packet[2] = AICWF_TYPE_TX_CFM;
	le32enc(packet + 4, status);
	le32enc(packet + 8, token);
	aicwf_rx(&sc, packet, sizeof(packet));
}

static void
scan_complete(unsigned vif, unsigned status, size_t len)
{
	const uint8_t payload[3] = {vif, status, 2};
	aicwf_scan_done(&sc, payload, len);
}

static void
tx_checks(void)
{
	const uint32_t statuses[] = {0, 1, 3, 5, 8, 9};
	struct ieee80211req_sta_mgmt req, cancel, ev;
	for (unsigned i = 0; i < __arraycount(statuses); i++) {
		reset(); sc.sc_sta_enabled = true;
		req = request_make(IEEE80211_STA_TX, 71);
		CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0);
		CHECK(writes == 0 && deliveries == 0 && sc.sc_sta_tx_pending);
		CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EBUSY);
		cfm_receive(statuses[i], 1, 8);
		CHECK(deliveries == 0); /* An unsent task cannot complete. */
		run_task();
		CHECK(writes == 1 && deliveries == 0 && sc.sc_sta_tx_sent);
		CHECK(written_len == 59 && written[2] == AICWF_TYPE_DATA);
		CHECK(le32dec(written + 8) == (AICWF_STA_CFM_REQUEST | 1));
		CHECK(le16dec(written + 30) == AICWF_TX_MGMT);
		CHECK(memcmp(written + 56, req.data, 3) == 0);
		cfm_receive(statuses[i], 2, 8);
		CHECK(sc.sc_sta_tx_mismatch_reported);
		cfm_receive(statuses[i], 1, 7);
		CHECK(deliveries == 0 && sc.sc_sta_tx_pending);
		cfm_receive(statuses[i], 1 | (i & 1 ? 0x80000000U : 0), 8);
		CHECK(deliveries == 1 && !sc.sc_sta_tx_pending && !sc.sc_sta_tx_sent);
		memcpy(&ev, event_data, sizeof(ev));
		CHECK(event_type == RTM_IEEE80211_STA_TX && ev.cookie == 71 && ev.generation == 39);
		CHECK(ev.len == 0 && ev.flags == ((statuses[i] & 8) ? IEEE80211_STA_TX_ACK : 0));
		cfm_receive(statuses[i], 1, 8);
		CHECK(deliveries == 1);
	}
	/* Cancel before the workqueue writes, then reuse the client cookie. */
	reset(); sc.sc_sta_enabled = true;
	req = request_make(IEEE80211_STA_TX, 72);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0);
	cancel = request_make(IEEE80211_STA_CANCEL_TX, 99);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &cancel) == ESTALE);
	cancel.cookie = 72;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &cancel) == 0 && deliveries == 1);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0 && sc.sc_sta_token == 2);
	run_task(); CHECK(writes == 0);
	run_task(); CHECK(writes == 1);
	cfm_receive(9, 1, 8); CHECK(deliveries == 1);
	cfm_receive(9, 2, 8); CHECK(deliveries == 2);
	/* Cancel after publication and ignore the later firmware ACK. */
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0); run_task();
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &cancel) == 0 && deliveries == 3);
	cfm_receive(9, 3, 8); CHECK(deliveries == 3);
	sc.sc_sta_token = AICWF_STA_TOKEN_MAX;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EOVERFLOW);
	reset(); sc.sc_sta_enabled = true;
	req = request_make(IEEE80211_STA_TX, 73);
	write_error = EIO;
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0); run_task();
	CHECK(deliveries == 1 && !sc.sc_sta_tx_pending);
	memcpy(&ev, event_data, sizeof(ev)); CHECK(ev.flags == 0);
	reset(); sc.sc_sta_enabled = sc.sc_sae_enabled = true;
	sc.sc_ic.ic_opt_ie[19] = 8; sc.sc_ic.ic_opt_ie[20] = 0xc0;
	req = request_make(IEEE80211_STA_TX, 74);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0); run_task();
	CHECK(le16dec(written + 30) == (AICWF_TX_MGMT | 0x80));
	cfm_receive(9, 1, 8); CHECK(deliveries == 1);
}

static void
cancel_wait_hook(void)
{
	struct ieee80211req_sta_mgmt req = request_make(IEEE80211_STA_CANCEL_SCAN, 81);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0);
}

static void
complete_wait_hook(void)
{
	scan_complete(sc.sc_vif, 0, 3);
}

static void
scan_checks(void)
{
	struct ieee80211req_sta_mgmt req, cancel, ev;
	uint16_t frequency = 5200;
	reset(); sc.sc_sta_enabled = true;
	req = request_make(IEEE80211_STA_SCAN, 81);
	req.len = 2; memcpy(req.data, &frequency, 2);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0 && writes == 0);
	run_task();
	CHECK(sc.sc_scanning && sc.sc_sta_scan_pending && sc.sc_scan_published);
	CHECK(writes == 1 && deliveries == 0 && sc.sc_ic.ic_scan.nt_scan_epoch == 1);
	CHECK(le16dec(written + 8) == AICWF_SCANU_START_REQ);
	CHECK(written_len == 392 && le16dec(written + 16) == 5200);
	CHECK(written[16 + 3] == 1 && written[16 + 367] == 1 && written[16 + 366] == 1);
	CHECK(sc.sc_ic.ic_state == IEEE80211_S_RUN && sc.sc_connected && sc.sc_ptk_valid);
	CHECK(ni.ni_chan == &chan && sc.sc_sta_assoc_len == 8 && state_calls == 0);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EBUSY);
	scan_complete(2, 0, 3); scan_complete(1, 0, 2);
	CHECK(sc.sc_scanning && deliveries == 0);
	scan_complete(1, 0, 3);
	CHECK(!sc.sc_scanning && !sc.sc_sta_scan_started && deliveries == 1);
	CHECK(scans_cancelled == 1 && scans_ended == 0 && state_calls == 0);
	memcpy(&ev, event_data, sizeof(ev));
	CHECK(event_type == RTM_IEEE80211_STA_SCAN && ev.flags == 0 && ev.cookie == 81);
	scan_complete(1, 0, 3); CHECK(deliveries == 1);
	/* An aborted host request must drain the old untagged firmware scan. */
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0); run_task();
	cancel = request_make(IEEE80211_STA_CANCEL_SCAN, 81);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &cancel) == 0);
	memcpy(&ev, event_data, sizeof(ev));
	CHECK(ev.flags == IEEE80211_STA_SCAN_ABORTED && sc.sc_sta_scan_draining);
	CHECK(sc.sc_scanning && !sc.sc_sta_scan_pending && tasks_count == 1);
	run_task();
	CHECK(le16dec(written + 8) == AICWF_SCANU_CANCEL_REQ && written_len == 16);
	CHECK(sc.sc_scanning && sc.sc_sta_scan_draining && deliveries == 2);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EBUSY);
	scan_complete(1, 0, 3);
	CHECK(!sc.sc_scanning && deliveries == 2);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0); run_task();
	scan_complete(1, 1, 3); memcpy(&ev, event_data, sizeof(ev));
	CHECK(ev.flags == IEEE80211_STA_SCAN_ABORTED && deliveries == 3);
	/* Queued cancellation consumes no radio or firmware token. */
	reset(); sc.sc_sta_enabled = true; req = request_make(IEEE80211_STA_SCAN, 81);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0);
	cancel = request_make(IEEE80211_STA_CANCEL_SCAN, 81);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &cancel) == 0); run_task();
	CHECK(writes == 0 && deliveries == 1 && !sc.sc_scanning);
	/* No firmware event can follow a credit failure before any FIFO write. */
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0); pre_fifo_error = EBUSY; run_task();
	CHECK(writes == 0 && !sc.sc_scanning && !sc.sc_scan_published && deliveries == 2);
	CHECK(!sc.sc_sta_scan_draining && tasks_count == 0);
	pre_fifo_error = 0; deliveries = 1;
	/* Lost launch ACK is not completion. Keep ownership until the real CFM. */
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0); reply_len = -1; run_task();
	CHECK(sc.sc_scanning && sc.sc_sta_scan_draining && deliveries == 2);
	reply_len = 1; run_task();
	CHECK(sc.sc_scanning && aicwf_sta_mgmt(&sc.sc_ic, &req) == EBUSY);
	scan_complete(1, 0, 3); CHECK(!sc.sc_scanning && deliveries == 2);
	/* Cancellation while waiting for another command must stop publication. */
	reset(); sc.sc_sta_enabled = true; req = request_make(IEEE80211_STA_SCAN, 81);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0);
	sc.sc_cmd_busy = true; wait_hook = cancel_wait_hook; run_task();
	CHECK(writes == 0 && !sc.sc_scanning && deliveries == 1);
	run_task(); CHECK(writes == 0 && !sc.sc_sta_cancel_queued);
	/* Completion can arrive while CANCEL waits for the command mutex. */
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0); run_task();
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &cancel) == 0);
	sc.sc_cmd_busy = true; wait_hook = complete_wait_hook; run_task();
	CHECK(writes == 1 && !sc.sc_scanning && !sc.sc_sta_cancel_queued);
	/* Invalid and inactive frequencies never reach firmware. */
	reset(); sc.sc_sta_enabled = true; req = request_make(IEEE80211_STA_SCAN, 81);
	req.len = 1; CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	req.len = 2; frequency = 5190; memcpy(req.data, &frequency, 2);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	frequency = 5200; memcpy(req.data, &frequency, 2); memcpy(req.data + 2, &frequency, 2);
	req.len = 4; CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == EINVAL);
	CHECK(writes == 0 && tasks_count == 0);
	/* The legacy initial scan uses the same publication distinction. */
	reset(); sc.sc_connected = false; sc.sc_ic.ic_state = IEEE80211_S_SCAN;
	pre_fifo_error = EBUSY; aicwf_scan(&sc);
	CHECK(!sc.sc_scanning && !sc.sc_scan_published && writes == 0);
	pre_fifo_error = 0; write_error = EIO; aicwf_scan(&sc);
	CHECK(sc.sc_scanning && sc.sc_scan_published && writes == 1);
	aicwf_scan(&sc); CHECK(writes == 1);
	scan_complete(1, 0, 3); CHECK(!sc.sc_scanning && scans_ended == 1);
}

static void
retirement_checks(void)
{
	uint8_t tx[AICWF_STA_NOTICE_LEN], scan[AICWF_STA_NOTICE_LEN];
	bool tx_event, scan_event;
	struct ieee80211req_sta_mgmt req;
	reset(); sc.sc_sta_enabled = true;
	req = request_make(IEEE80211_STA_TX, 91);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0);
	mutex_enter(&sc.sc_lock);
	aicwf_sta_retire_locked(&sc, tx, &tx_event, scan, &scan_event);
	sc.sc_sae_epoch++;
	mutex_exit(&sc.sc_lock);
	CHECK(tx_event && !scan_event && sc.sc_sta_assoc_len == 0);
	run_task(); CHECK(writes == 0 && !sc.sc_sta_tx_pending);
	reset(); sc.sc_sta_enabled = true; req = request_make(IEEE80211_STA_SCAN, 92);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0); run_task();
	mutex_enter(&sc.sc_lock);
	aicwf_sta_retire_locked(&sc, tx, &tx_event, scan, &scan_event);
	sc.sc_sae_epoch++;
	mutex_exit(&sc.sc_lock);
	CHECK(!tx_event && scan_event && sc.sc_sta_scan_draining && sc.sc_scanning);
	run_task(); CHECK(sc.sc_scanning);
	scan_complete(1, 0, 3); CHECK(!sc.sc_scanning && deliveries == 0);
	reset(); sc.sc_sta_enabled = true; req = request_make(IEEE80211_STA_TX, 93);
	CHECK(aicwf_sta_mgmt(&sc.sc_ic, &req) == 0);
	aicwf_sae_advance(&sc);
	CHECK(sc.sc_sae_epoch == 40 && deliveries == 1 && !sc.sc_ptk_valid);
	CHECK(sc.sc_sta_assoc_len == 0 && !sc.sc_sta_tx_pending);
	run_task(); CHECK(writes == 0);
}

static void
assoc_checks(void)
{
	uint8_t ind[836] = {0};
	reset();
	memcpy(ind + 2, ni.ni_bssid, 6); ind[9] = sc.sc_vif;
	le16enc(ind + 14, 4); le16enc(ind + 16, 2);
	ind[20] = 0; ind[21] = 2; ind[22] = 'a'; ind[23] = 'b';
	ind[24] = 1; ind[25] = 0;
	le16enc(ind + 824, 5200); ind[822] = 1;
	aicwf_sta_assoc(&sc, ind, sizeof(ind));
	CHECK(sc.sc_sta_assoc_len == 10 && le16dec(sc.sc_sta_assoc) == 4);
	CHECK(le16dec(sc.sc_sta_assoc + 2) == 2 && memcmp(sc.sc_sta_assoc + 4, ind + 20, 6) == 0);
	CHECK(ni.ni_chan == &sc.sc_ic.ic_channels[40]);
	ind[9] = 2; aicwf_sta_assoc(&sc, ind, sizeof(ind)); CHECK(sc.sc_sta_assoc_len == 0);
	ind[9] = 1; le16enc(ind + 14, 801);
	aicwf_sta_assoc(&sc, ind, sizeof(ind)); CHECK(sc.sc_sta_assoc_len == 0);
	le16enc(ind + 14, 4); ind[25] = 2;
	aicwf_sta_assoc(&sc, ind, sizeof(ind)); CHECK(sc.sc_sta_assoc_len == 0);
}

static void
run_auth_task(void)
{
	CHECK(tasks_count != 0);
	struct aicwf_task *t = tasks[0];
	memmove(tasks, tasks + 1, --tasks_count * sizeof(*tasks));
	CHECK(t->t_cmd == AICWF_TASK_NEWSTATE && t->t_state == IEEE80211_S_AUTH);
	aicwf_state_cb(&sc, t);
	free(t);
}

static void
reassoc_departure(void)
{
	const uint8_t ind[6] = {0, 0, sc.sc_vif, 0, 1, 0};
	const uint64_t epoch = sc.sc_sae_epoch;

	sc.sc_sae_pending = sc.sc_sae_authenticated = true;
	aicwf_disconnect_ind(&sc, ind, sizeof(ind));
	CHECK(sc.sc_sae_epoch == epoch && sc.sc_connecting && !sc.sc_connected);
	CHECK(sc.sc_sae_pending && sc.sc_sae_authenticated);
}

static void
join_disconnect(void)
{
	const uint8_t ind[6] = {0, 0, sc.sc_vif, 0, 0, 0};
	aicwf_disconnect_ind(&sc, ind, sizeof(ind));
}

static void
reassoc_checks(void)
{
	uint8_t ind[6] = {0};

	reset();
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	CHECK(tasks[0]->t_epoch == 39 && tasks[0]->t_join_serial == 1);
	run_auth_task();
	CHECK(writes == 1 && le16dec(written + 8) == AICWF_SM_CONNECT_REQ);
	CHECK((le32dec(written + 16 + 48) & (1U << 5)) != 0);
	CHECK(sc.sc_connecting && sc.sc_sae_epoch == 40);
	CHECK(sc.sc_sae_generation == 41 && sc.sc_ic.ic_state == IEEE80211_S_AUTH);
	/* A delayed old-AP indication cannot clear the newly associated keys. */
	sc.sc_connecting = false; sc.sc_connected = sc.sc_ptk_valid = true;
	sc.sc_igtk[0] = 7; ind[2] = sc.sc_vif; ind[4] = 1;
	aicwf_disconnect_ind(&sc, ind, sizeof(ind) - 1);
	CHECK(sc.sc_connected && sc.sc_sae_epoch == 40);
	ind[2]++; aicwf_disconnect_ind(&sc, ind, sizeof(ind)); ind[2]--;
	CHECK(sc.sc_connected && sc.sc_sae_epoch == 40);
	aicwf_disconnect_ind(&sc, ind, sizeof(ind));
	CHECK(sc.sc_connected && sc.sc_ptk_valid && sc.sc_igtk[0] == 7);
	CHECK(sc.sc_sae_epoch == 40 && tasks_count == 0);
	/* First association must not ask the firmware to reuse an old AP. */
	reset(); sc.sc_connected = false;
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	run_auth_task();
	CHECK(writes == 1 && (le32dec(written + 16 + 48) & (1U << 5)) == 0);
	CHECK(sc.sc_connecting);
	/* SAE CONFIGURE survives both the native request and old-AP departure. */
	reset(); sc.sc_sae_enabled = true;
	sc.sc_ic.ic_opt_ie[19] = 8; sc.sc_ic.ic_opt_ie[20] = 0xc0;
	sc.sc_igtk[0] = 7; reply_len = -1; reply_hook = reassoc_departure;
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	run_auth_task();
	CHECK(writes == 1 && (le32dec(written + 16 + 48) & 0x30) == 0x30);
	CHECK(written[16 + 59] == AICWF_AUTH_SAE && sc.sc_sae_enabled);
	CHECK(sc.sc_sae_generation == 41 && memcmp(sc.sc_sae_bssid, ni.ni_bssid, 6) == 0);
	CHECK(sc.sc_sae_epoch == 40 && sc.sc_igtk[0] == AICWF_HWKEY_NONE);
	/* Admission failure leaves the old physical link and key handles owned. */
	reset(); sc.sc_igtk[0] = 7; pre_fifo_error = EBUSY;
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	run_auth_task();
	CHECK(writes == 0 && sc.sc_connected && !sc.sc_connecting && sc.sc_igtk[0] == 7);
	pre_fifo_error = 0; aicwf_disconnect(&sc);
	CHECK(writes == 2 && le16dec(written + 8) == AICWF_SM_DISCONNECT_REQ);
	CHECK(!sc.sc_connected && sc.sc_igtk[0] == AICWF_HWKEY_NONE);
	reset(); sc.sc_igtk[0] = 7; reply_status = 9;
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	run_auth_task();
	CHECK(writes == 1 && sc.sc_connected && !sc.sc_connecting && sc.sc_igtk[0] == 7);
	/* A queued AUTH cannot outlive a replacement CONFIGURE or raw MLME. */
	reset();
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	aicwf_sae_advance(&sc); sc.sc_configure_serial++; run_auth_task();
	CHECK(writes == 0 && state_calls == 0 && sc.sc_sae_epoch == 40);
	reset();
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	run_auth_task(); CHECK(writes == 0 && state_calls == 0);
	run_auth_task(); CHECK(writes == 1 && state_calls == 1);
	/* An old AP departure cannot retire a CONNECT already being published. */
	reset(); sc.sc_cmd_busy = true; wait_hook = join_disconnect;
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	run_auth_task();
	CHECK(writes == 1 && sc.sc_sae_epoch == 40 && sc.sc_connecting);
	CHECK(tasks_count == 0);
	/* Physical departure before AUTH must not discard the newer host intent. */
	reset();
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	join_disconnect(); CHECK(sc.sc_sae_epoch == 40 && tasks_count == 2);
	run_auth_task(); CHECK(writes == 1 && sc.sc_sae_epoch == 41 && sc.sc_connecting);
	run_task(); CHECK(state_calls == 1);
	/* An earlier queued SCAN cannot retire a later configured AUTH. */
	reset();
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_SCAN, -1) == 0);
	aicwf_sae_advance(&sc); sc.sc_configure_serial++;
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	run_task(); CHECK(writes == 0 && sc.sc_sae_epoch == 40);
	run_auth_task(); CHECK(writes == 1 && sc.sc_sae_epoch == 41 && sc.sc_connecting);
	/* An inadmissible join cannot suppress the queued down transition. */
	reset(); sc.sc_if.if_flags = 0;
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_INIT, -1) == 0);
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == ENETDOWN);
	CHECK(sc.sc_join_serial == 0 && tasks_count == 1);
	run_task(); CHECK(!sc.sc_connected && sc.sc_ic.ic_state == IEEE80211_S_INIT);
	/* Delayed flagged departures never retire a newer CONFIGURE or its keys. */
	reset(); sc.sc_connecting = true; sc.sc_sae_epoch = 41;
	aicwf_sae_advance(&sc);
	sc.sc_connecting = false; sc.sc_igtk[0] = 8; sc.sc_sae_pending = true;
	aicwf_disconnect_ind(&sc, ind, sizeof(ind));
	CHECK(sc.sc_sae_epoch == 42 && sc.sc_sae_pending && sc.sc_igtk[0] == 8);
	/* Invalid optional-IE state cannot dereference NULL through SAE parsing. */
	reset(); sc.sc_sae_enabled = true; sc.sc_ic.ic_opt_ie = NULL;
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	run_auth_task(); CHECK(writes == 0 && sc.sc_sae_epoch == 39);
}

static void
recovery_prepare(void)
{
	reset();
	sc.sc_alive_epoch = sc.sc_sae_epoch;
	sc.sc_alive_join_serial = sc.sc_join_serial;
	sc.sc_alive_queued = true;
}

static void
retire_wait_hook(void)
{
	aicwf_sae_advance(&sc);
}

static void
new_user_association(void)
{
	/* ioctl serialization defers CONFIGURE and MLME while recovery sleeps. */
	if (IFNET_LOCKED(&sc.sc_if)) {
		unlock_hook = new_user_association;
		return;
	}
	aicwf_sae_advance(&sc);
	sc.sc_configure_serial++;
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
}

static void
recovery_checks(void)
{
	uint8_t ind[6] = {0};
	recovery_prepare();
	CHECK(aicwf_sae_advance_if(&sc, sc.sc_sae_epoch - 1) == 0);
	CHECK(sc.sc_sae_epoch == 39 && sc.sc_ptk_valid);
	/* A raw MLME request changes intent before the queued AUTH executes. */
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	CHECK(sc.sc_join_serial == 1 && sc.sc_sae_epoch == 39);
	aicwf_alive_work(&sc);
	CHECK(writes == 0 && tasks_count == 1 && !sc.sc_alive_queued);
	CHECK(sc.sc_connected && sc.sc_ptk_valid && !sc.sc_if.ioctl_locked);
	/* An old connected flag cannot make in-progress AUTH a recovery target. */
	recovery_prepare(); sc.sc_connecting = true; aicwf_alive_work(&sc);
	CHECK(writes == 0 && tasks_count == 0 && sc.sc_ptk_valid);
	recovery_prepare(); sc.sc_ic.ic_state = IEEE80211_S_AUTH; aicwf_alive_work(&sc);
	CHECK(writes == 0 && tasks_count == 0 && sc.sc_ptk_valid);
	/* ifconfig down clears RUNNING before its queued INIT can execute. */
	recovery_prepare(); sc.sc_if.if_flags &= ~IFF_RUNNING; aicwf_alive_work(&sc);
	CHECK(writes == 0 && tasks_count == 0 && sc.sc_ptk_valid);
	recovery_prepare(); aicwf_alive_work(&sc);
	sc.sc_if.if_flags &= ~IFF_RUNNING; run_task();
	CHECK(state_calls == 0 && sc.sc_ic.ic_state == IEEE80211_S_RUN);
	/* Normal recovery writes a bound DISCONNECT and queues its own epoch. */
	recovery_prepare(); aicwf_alive_work(&sc);
	CHECK(writes == 1 && le16dec(written + 8) == AICWF_SM_DISCONNECT_REQ);
	CHECK(tasks_count == 1 && tasks[0]->t_cmd == AICWF_TASK_RECOVER_SCAN);
	CHECK(tasks[0]->t_epoch == 40 && !sc.sc_connected && sc.sc_sae_epoch == 40);
	run_task(); CHECK(state_calls == 1 && sc.sc_ic.ic_state == IEEE80211_S_SCAN);
	/* A generation change before command publication prevents DISCONNECT. */
	recovery_prepare(); sc.sc_cmd_busy = true; wait_hook = retire_wait_hook;
	aicwf_alive_work(&sc);
	CHECK(sc.sc_sae_epoch == 41 && writes == 0 && tasks_count == 0);
	CHECK(!sc.sc_if.ioctl_locked);
	/* An RX retirement during a reply wait cannot queue stale recovery. */
	recovery_prepare(); reply_len = -1; reply_hook = retire_wait_hook;
	aicwf_alive_work(&sc);
	CHECK(writes == 1 && tasks_count == 0 && sc.sc_sae_epoch == 41);
	/* Key deletion is also bound. A later DISCONNECT cannot cross epochs. */
	recovery_prepare(); sc.sc_igtk[0] = 7;
	reply_len = -1; reply_hook = retire_wait_hook;
	aicwf_alive_work(&sc);
	CHECK(writes == 1 && le16dec(written + 8) == AICWF_MM_KEY_DEL_REQ);
	CHECK(tasks_count == 0 && sc.sc_sae_epoch == 41);
	/* CONFIGURE/MLME waits for IFNET, then supersedes the queued recovery. */
	recovery_prepare(); reply_len = -1; reply_hook = new_user_association;
	aicwf_alive_work(&sc);
	CHECK(writes == 1 && tasks_count == 2 && sc.sc_sae_epoch == 41);
	CHECK(tasks[0]->t_cmd == AICWF_TASK_RECOVER_SCAN && tasks[1]->t_cmd == AICWF_TASK_NEWSTATE);
	run_task(); CHECK(state_calls == 0);
	run_task(); CHECK(state_calls == 1 && sc.sc_ic.ic_state == IEEE80211_S_AUTH);
	/* Raw MLME without CONFIGURE must also invalidate a queued SCAN. */
	recovery_prepare(); aicwf_alive_work(&sc);
	CHECK(aicwf_newstate(&sc.sc_ic, IEEE80211_S_AUTH, -1) == 0);
	CHECK(sc.sc_sae_epoch == 40 && sc.sc_join_serial == 1);
	run_task(); CHECK(state_calls == 0);
	run_task(); CHECK(state_calls == 1 && sc.sc_ic.ic_state == IEEE80211_S_AUTH);
	/* Firmware loss takes the same guarded queue, without IFNET in RX. */
	recovery_prepare(); ind[2] = sc.sc_vif;
	aicwf_disconnect_ind(&sc, ind, sizeof(ind));
	CHECK(writes == 0 && tasks_count == 1 && !sc.sc_connected);
	CHECK(tasks[0]->t_cmd == AICWF_TASK_RECOVER_SCAN && tasks[0]->t_epoch == 40);
	new_user_association();
	run_task(); CHECK(state_calls == 0);
	run_task(); CHECK(state_calls == 1 && sc.sc_ic.ic_state == IEEE80211_S_AUTH);
	/* A pre-attachment event never touches the uninitialized IFNET lock. */
	recovery_prepare(); sc.sc_if_attached = false;
	aicwf_alive_work(&sc);
	CHECK(writes == 0 && tasks_count == 0 && !sc.sc_if.ioctl_locked);
}

int
main(int argc, char **argv)
{
	const char *group = argc > 1 ? argv[1] : "all";
	if (!strcmp(group, "all") || !strcmp(group, "abi")) abi_checks();
	if (!strcmp(group, "all") || !strcmp(group, "ioctl")) ioctl_rejection_checks();
	if (!strcmp(group, "all") || !strcmp(group, "actions")) action_checks();
	if (!strcmp(group, "all") || !strcmp(group, "rx")) rx_checks();
	if (!strcmp(group, "all") || !strcmp(group, "oversize")) oversize_check();
	if (!strcmp(group, "all") || !strcmp(group, "tx")) tx_checks();
	if (!strcmp(group, "all") || !strcmp(group, "scan")) scan_checks();
	if (!strcmp(group, "all") || !strcmp(group, "retire")) retirement_checks();
	if (!strcmp(group, "all") || !strcmp(group, "assoc")) assoc_checks();
	if (!strcmp(group, "all") || !strcmp(group, "reassoc")) reassoc_checks();
	if (!strcmp(group, "all") || !strcmp(group, "recovery")) recovery_checks();
	reset();
	CHECK(checks != 0);
	printf("PASS: %u AIC station/client and management RX checks\n", checks);
	return 0;
}
