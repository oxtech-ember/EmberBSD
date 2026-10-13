/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute production BSD association and scan callbacks with I/O fixtures. */
#include "utils/includes.h"
#include "utils/common.h"
#include "drivers/driver.h"

/* Select the target branches after loading the host C library headers. */
#ifndef __NetBSD__
#define __NetBSD__ 1
#endif
#define CONFIG_SAE 1
#define IEEE80211_ADDR_LEN ETH_ALEN
#define IEEE80211_NWID_LEN SSID_MAX_LEN
#include "assoc-abi.h"
#include "assoc-routes.h"

/* These flags reach fixture media/legacy-ioctl endpoints, not an OS device. */
#define IFM_OMASK 0x0000ff00
#define IFM_IEEE80211_HOSTAP 0x00000200
#define IFM_IEEE80211_IBSS 0x00001000
#define IEEE80211_ROAMING_MANUAL 2
#ifndef IFF_UP
#define IFF_UP 1
#endif
#define ELOOP_ALL_CTX ((void *)-1)

struct bsd_driver_data {
	void *ctx, *sock_xmit;
	char ifname[16];
	int is_ap, flags, sta_btm, sta_rrm, sta_enabled, sta_scan_pending;
	struct wpa_driver_capa capa;
	u32 sae_generation, sta_scan_cookie, sta_scan_epoch;
	uintptr_t sta_scan_generation;
	u8 sae_bssid[ETH_ALEN], sta_scan_bssid[ETH_ALEN];
	int sae_active;
};

static unsigned checks, failures, set_calls, mlme_calls, assoc_events;
static unsigned clear_tx_calls, cancel_calls, timer_calls, media_calls;
static unsigned legacy_scans, sae_calls;
static int info_error, set_error, mlme_error, timer_error, sae_error;
static int media_error, privacy_error, wpa_error, ie_error, drop_error;
static unsigned timer_sec;
static void *timer_arg;
static void (*timer_fn)(void *, void *);
static struct ieee80211req_sta_mgmt current, submitted[4];
static struct ieee80211req_mlme selected;
static u8 supplied_ies[256], received_req[1024], received_resp[1024];
static size_t supplied_len, received_req_len, received_resp_len;
static unsigned received_freq;
static int received_req_present, received_resp_present;
static u8 configured_sae;
static char order[32];
static size_t order_len;

static void
check(bool pass, const char *why)
{
	checks++;
	if (!pass) {
		fprintf(stderr, "FAIL: %s\n", why);
		failures++;
	}
}

static void
step(char c)
{
	if (order_len + 1 >= sizeof(order))
		abort();
	order[order_len++] = c;
	order[order_len] = '\0';
}

void
wpa_printf(int level, const char *fmt, ...)
{
}

static int
bsd_sta_info(struct bsd_driver_data *drv, struct ieee80211req_sta_mgmt *req)
{
	*req = current;
	return info_error;
}

static int
bsd_sta_set(struct bsd_driver_data *drv, struct ieee80211req_sta_mgmt *req,
    unsigned op)
{
	if (set_calls >= sizeof(submitted) / sizeof(submitted[0]))
		abort();
	submitted[set_calls] = *req;
	submitted[set_calls++].op = op;
	step(op == IEEE80211_STA_ENABLE ? 'E' :
	    op == IEEE80211_STA_DISABLE ? 'D' :
	    op == IEEE80211_STA_SCAN ? 'S' : 'C');
	return set_error;
}

static void
bsd_sta_tx_status(struct bsd_driver_data *drv, int ack, int notify)
{
	check(ack == 0 && notify == 0, "association retires TX without an ACK");
	clear_tx_calls++;
}

static void
eloop_cancel_timeout(void (*fn)(void *, void *), void *a, void *b)
{
	cancel_calls++;
}

static int
eloop_register_timeout(unsigned sec, unsigned usec,
    void (*fn)(void *, void *), void *a, void *b)
{
	check(usec == 0, "scan timer uses whole seconds");
	timer_calls++;
	timer_sec = sec;
	timer_fn = fn;
	timer_arg = b;
	return timer_error;
}

static void
bsd_sta_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	/* The callback's timeout and completion bodies have a separate fixture. */
}

static int
bsd_set_mediaopt(struct bsd_driver_data *drv, int clear, int mode)
{
	media_calls++;
	step('M');
	return media_error;
}

static void
handle_read(void *ctx, const u8 *src_addr, const u8 *buf, size_t len)
{
}

static void *
l2_packet_init(const char *name, const u8 *addr, unsigned short proto,
    void (*receive)(void *, const u8 *, const u8 *, size_t), void *ctx,
    int header)
{
	return NULL;
}

static int
wpa_driver_bsd_set_drop_unencrypted(struct bsd_driver_data *drv, int enabled)
{
	return drop_error;
}

static int
wpa_driver_bsd_set_auth_alg(struct bsd_driver_data *drv, int alg)
{
	return 0;
}

static int
wpa_driver_bsd_set_wpa_ie(struct bsd_driver_data *drv, const u8 *ie, size_t len)
{
	if (len > sizeof(supplied_ies))
		abort();
	if (len)
		memcpy(supplied_ies, ie, len);
	supplied_len = len;
	return ie_error;
}

static int
set80211param(struct bsd_driver_data *drv, int op, int value)
{
	if (op == IEEE80211_IOC_SCAN_REQ)
		legacy_scans++;
	if (op == IEEE80211_IOC_PRIVACY)
		return privacy_error;
	if (op == IEEE80211_IOC_WPA)
		return wpa_error;
	return 0;
}

static int
set80211var(struct bsd_driver_data *drv, int op, const void *data, size_t len)
{
	check(op == IEEE80211_IOC_MLME && len == sizeof(selected),
	    "directed association uses the native MLME payload");
	memcpy(&selected, data, sizeof(selected));
	mlme_calls++;
	step('A');
	return mlme_error;
}

static int
bsd_sae_request(struct bsd_driver_data *drv, u16 op, const void *data,
    size_t len)
{
	check(op == IEEE80211_SAE_CONFIGURE && len == 1,
	    "association configures the external SAE transport");
	configured_sae = *(const u8 *)data;
	sae_calls++;
	step('K');
	return sae_error;
}

static int
wpa_driver_bsd_set_wpa(struct bsd_driver_data *drv, int enabled)
{
	return 0;
}

static int
bsd_set_ssid(struct bsd_driver_data *drv, const u8 *ssid, size_t len)
{
	return 0;
}

void
wpa_supplicant_event(void *ctx, enum wpa_event_type event,
    union wpa_event_data *data)
{
	check(event == EVENT_ASSOC, "association route event reaches the client");
	assoc_events++;
	received_freq = data->assoc_info.freq;
	received_req_present = data->assoc_info.req_ies != NULL;
	received_resp_present = data->assoc_info.resp_ies != NULL;
	received_req_len = data->assoc_info.req_ies_len;
	received_resp_len = data->assoc_info.resp_ies_len;
	if (received_req_len > sizeof(received_req) ||
	    received_resp_len > sizeof(received_resp))
		abort();
	/* Consume the actual stack-backed payload before the callback returns. */
	if (received_req_len)
		memcpy(received_req, data->assoc_info.req_ies, received_req_len);
	if (received_resp_len)
		memcpy(received_resp, data->assoc_info.resp_ies, received_resp_len);
}

static void
#include "bsd_sta_scan_clear.h"
static int
#include "bsd_sta_associate.h"
static int
#include "wpa_driver_bsd_associate.h"
static int
#include "wpa_driver_bsd_scan.h"

static void
association_event(struct bsd_driver_data *drv, unsigned type)
{
	union wpa_event_data event;

	memset(&event, 0, sizeof(event));
	switch (type) {
#include "bsd-assoc-event.h"
	default:
		abort();
	}
}

static void
fixture(struct bsd_driver_data *drv)
{
	memset(drv, 0, sizeof(*drv));
	memset(&current, 0, sizeof(current));
	memset(submitted, 0, sizeof(submitted));
	memset(&selected, 0, sizeof(selected));
	drv->sta_btm = drv->sta_rrm = 1;
	drv->sta_enabled = 1;
	drv->flags = IFF_UP;
	drv->sta_scan_cookie = 40;
	drv->sta_scan_generation = 50;
	current.version = IEEE80211_STA_VERSION;
	current.flags = IEEE80211_STA_ASSOCIATED |
	    IEEE80211_STA_CAP_BTM | IEEE80211_STA_CAP_RRM;
	current.generation = 60;
	current.freq = 5180;
	current.bssid[0] = 2;
	current.bssid[5] = 9;
	set_calls = mlme_calls = assoc_events = clear_tx_calls = 0;
	cancel_calls = timer_calls = media_calls = legacy_scans = sae_calls = 0;
	info_error = set_error = mlme_error = timer_error = sae_error = 0;
	media_error = privacy_error = wpa_error = ie_error = drop_error = 0;
	supplied_len = received_req_len = received_resp_len = received_freq = 0;
	received_req_present = received_resp_present = 0;
	timer_fn = NULL;
	timer_arg = NULL;
	timer_sec = 0;
	configured_sae = 0;
	order_len = 0;
	order[0] = '\0';
}

static struct wpa_driver_associate_params
parameters(u8 *ies, size_t len)
{
	static const u8 ssid[] = "station-contract";
	static const u8 peer[ETH_ALEN] = { 2, 0, 0, 0, 0, 7 };
	struct wpa_driver_associate_params p;

	memset(&p, 0, sizeof(p));
	p.mode = IEEE80211_MODE_INFRA;
	p.ssid = ssid;
	p.ssid_len = sizeof(ssid) - 1;
	p.bssid = peer;
	p.wpa_ie = ies;
	p.wpa_ie_len = len;
	p.pairwise_suite = p.group_suite = WPA_CIPHER_CCMP;
	p.key_mgmt_suite = WPA_KEY_MGMT_PSK;
	return p;
}

static void
association_requests(void)
{
	struct bsd_driver_data drv;
	struct wpa_driver_associate_params p;
	u8 ies[] = { WLAN_EID_RSN, 2, 1, 0, WLAN_EID_EXT_CAPAB, 3, 0, 0, 8 };
	u8 malformed[] = { WLAN_EID_RSN, 0, WLAN_EID_EXT_CAPAB, 3, 0, 0 };
	u8 trailing[] = { WLAN_EID_RSN, 2, 1, 0, WLAN_EID_EXT_CAPAB, 3, 0, 0, 8, 0xfe };
	unsigned i;

	fixture(&drv);
	p = parameters(ies, sizeof(ies));
	drv.sta_enabled = 0;
	drv.sta_scan_pending = 1;
	check(bsd_sta_associate(&drv, &p) == 0 && set_calls == 1 &&
	    submitted[0].op == IEEE80211_STA_ENABLE && drv.sta_enabled,
	    "RSN/CCMP and requested BTM enable the station transport");
	check(clear_tx_calls == 1 && cancel_calls == 1 &&
	    !drv.sta_scan_pending && drv.sta_scan_generation == 51,
	    "new association retires old TX and connected scan state");
	for (i = 0; i < 10; i++) {
		fixture(&drv);
		p = parameters(ies, sizeof(ies));
		switch (i) {
		case 0: p.wpa_ie = malformed; p.wpa_ie_len = sizeof(malformed); break;
		case 1: p.wpa_ie_len--; break;
		case 2: p.wpa_ie_len = 0; p.wpa_ie = NULL; break;
		case 3: p.pairwise_suite = WPA_CIPHER_TKIP; break;
		case 4: p.group_suite = WPA_CIPHER_TKIP; break;
		case 5: p.key_mgmt_suite = WPA_KEY_MGMT_NONE; break;
		case 6: p.key_mgmt_suite = WPA_KEY_MGMT_SAE; break;
		case 7: drv.sta_btm = 0; break;
		case 8: p.wpa_ie = ies + 4; p.wpa_ie_len = sizeof(ies) - 4; break;
		case 9: p.wpa_ie = trailing; p.wpa_ie_len = sizeof(trailing); break;
		}
		check(bsd_sta_associate(&drv, &p) == 0 && set_calls == 1 &&
		    submitted[0].op == IEEE80211_STA_DISABLE && !drv.sta_enabled,
		    "malformed, unsupported or absent negotiation disables transport");
	}
	fixture(&drv);
	p = parameters(ies, 4);
	p.rrm_used = 1;
	check(bsd_sta_associate(&drv, &p) == 0 &&
	    submitted[0].op == IEEE80211_STA_ENABLE,
	    "negotiated Neighbor Reports work independently of BTM");
	fixture(&drv);
	p = parameters(ies, sizeof(ies));
	p.key_mgmt_suite = WPA_KEY_MGMT_SAE;
	p.mgmt_frame_protection = MGMT_FRAME_PROTECTION_REQUIRED;
	check(bsd_sta_associate(&drv, &p) == 0 &&
	    submitted[0].op == IEEE80211_STA_ENABLE,
	    "SAE with required PMF enables the same transport");
	fixture(&drv);
	drv.sta_btm = drv.sta_rrm = 0;
	check(bsd_sta_associate(&drv, &p) == 0 && set_calls == 0 &&
	    clear_tx_calls == 0, "old drivers do not receive new station ioctls");
	fixture(&drv);
	set_error = -1;
	check(bsd_sta_associate(&drv, &p) == -1,
	    "station enable failure propagates to the caller");
}

static void
directed_association(void)
{
	struct bsd_driver_data drv;
	struct wpa_driver_associate_params p;
	u8 ies[] = { WLAN_EID_RSN, 0, WLAN_EID_EXT_CAPAB, 3, 0, 0, 8 };
	unsigned i;

	fixture(&drv);
	p = parameters(ies, sizeof(ies));
	check(wpa_driver_bsd_associate(&drv, &p) == 0,
	    "selected WPA2 candidate reaches the directed association callback");
	check(mlme_calls == 1 && selected.im_op == IEEE80211_MLME_ASSOC &&
	    selected.im_ssid_len == p.ssid_len &&
	    !memcmp(selected.im_ssid, p.ssid, p.ssid_len) &&
	    !memcmp(selected.im_macaddr, p.bssid, ETH_ALEN),
	    "MLME carries the selected SSID and new BSSID unchanged");
	check(supplied_len == sizeof(ies) &&
	    !memcmp(supplied_ies, ies, sizeof(ies)) && !strcmp(order, "MEA"),
	    "association IEs and station enable precede directed MLME");
	fixture(&drv);
	drv.capa.key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_SAE;
	drv.sae_generation = UINT32_MAX;
	p.key_mgmt_suite = WPA_KEY_MGMT_SAE;
	p.mgmt_frame_protection = MGMT_FRAME_PROTECTION_REQUIRED;
	check(wpa_driver_bsd_associate(&drv, &p) == 0 &&
	    sae_calls == 1 && configured_sae == 1 && drv.sae_active &&
	    drv.sae_generation == 1 && !memcmp(drv.sae_bssid, p.bssid, ETH_ALEN) &&
	    !strcmp(order, "MKEA"),
	    "SAE transition binds a fresh nonzero generation before association");
	for (i = 0; i < 9; i++) {
		fixture(&drv);
		drv.capa.key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_SAE;
		p = parameters(ies, sizeof(ies));
		p.key_mgmt_suite = WPA_KEY_MGMT_SAE;
		p.mgmt_frame_protection = MGMT_FRAME_PROTECTION_REQUIRED;
		switch (i) {
		case 0: p.mgmt_frame_protection = NO_MGMT_FRAME_PROTECTION; break;
		case 1: p.bssid = NULL; break;
		case 2: sae_error = -1; break;
		case 3: set_error = -1; break;
		case 4: media_error = -1; break;
		case 5: privacy_error = -1; break;
		case 6: wpa_error = -1; break;
		case 7: ie_error = -1; break;
		case 8: drop_error = -1; break;
		}
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && mlme_calls == 0,
		    "failed security or transport preparation cannot start MLME");
		if (i == 2)
			check(!drv.sae_active, "failed SAE setup clears client activity");
	}
	fixture(&drv);
	p = parameters(ies, sizeof(ies));
	mlme_error = -1;
	check(wpa_driver_bsd_associate(&drv, &p) == -1 && mlme_calls == 1,
	    "directed MLME failure is not reported as success");
}

static void
association_payloads(void)
{
	struct bsd_driver_data drv;
	static const u8 request[] = { WLAN_EID_RRM_ENABLED_CAPABILITIES, 5, 2, 0, 0, 0, 0 };
	static const u8 response[] = { WLAN_EID_EXT_CAPAB, 3, 0, 0, 8 };
	unsigned i;

	for (i = 0; i < 9; i++) {
		fixture(&drv);
		current.flags |= IEEE80211_STA_ASSOC_IES;
		current.len = ETH_ALEN + 4 + sizeof(request) + sizeof(response);
		WPA_PUT_LE16(current.data + ETH_ALEN, sizeof(request));
		WPA_PUT_LE16(current.data + ETH_ALEN + 2, sizeof(response));
		memcpy(current.data + ETH_ALEN + 4, request, sizeof(request));
		memcpy(current.data + ETH_ALEN + 4 + sizeof(request), response, sizeof(response));
		switch (i) {
		case 2: current.len--; break;
		case 3: current.len = ETH_ALEN + 3; break;
		case 4: current.flags &= ~IEEE80211_STA_ASSOC_IES; break;
		case 5: current.flags &= ~IEEE80211_STA_ASSOCIATED; break;
		case 6: info_error = -1; break;
		case 7: WPA_PUT_LE16(current.data + ETH_ALEN, UINT16_MAX); break;
		case 8: drv.is_ap = 1; break;
		}
		association_event(&drv, i == 1 ? RTM_IEEE80211_REASSOC : RTM_IEEE80211_ASSOC);
		if (i == 8) {
			check(assoc_events == 0, "AP events are not station associations");
			continue;
		}
		check(assoc_events == 1, "each station association dispatches one event");
		if (i < 2) {
			check(received_freq == 5180 && received_req_present && received_resp_present &&
			    received_req_len == sizeof(request) && received_resp_len == sizeof(response) &&
			    !memcmp(received_req, request, sizeof(request)) &&
			    !memcmp(received_resp, response, sizeof(response)),
			    "actual request and response IEs reach synchronous association callback");
		} else {
			check(!received_req_present && !received_resp_present &&
			    received_req_len == 0 && received_resp_len == 0,
			    "absent or malformed association payload cannot invent negotiated IEs");
			check(received_freq == (i == 5 || i == 6 ? 0U : 5180U),
			    "only a successful associated GET supplies frequency");
		}
	}
}

static void
connected_scans(void)
{
	struct bsd_driver_data drv;
	struct wpa_driver_scan_params p;
	int freqs[IEEE80211_STA_MAX_CHANNELS + 2];
	u16 decoded;
	unsigned i;

	memset(&p, 0, sizeof(p));
	freqs[0] = 2412; freqs[1] = 5180; freqs[2] = 0;
	p.freqs = freqs;
	fixture(&drv);
	check(wpa_driver_bsd_scan(&drv, &p) == 0 && set_calls == 1 &&
	    submitted[0].op == IEEE80211_STA_SCAN && submitted[0].len == 4 &&
	    submitted[0].generation == current.generation &&
	    !memcmp(submitted[0].bssid, current.bssid, ETH_ALEN),
	    "connected scan carries current epoch, peer and directed frequencies");
	memcpy(&decoded, submitted[0].data, sizeof(decoded));
	check(decoded == 2412, "first scan frequency has native uint16 encoding");
	memcpy(&decoded, submitted[0].data + 2, sizeof(decoded));
	check(decoded == 5180 && drv.sta_scan_pending &&
	    drv.sta_scan_epoch == current.generation && drv.sta_scan_cookie == 41 &&
	    !memcmp(drv.sta_scan_bssid, current.bssid, ETH_ALEN),
	    "pending scan owns its cookie and association snapshot");
	check(timer_calls == 1 && timer_sec == 15 && timer_fn == bsd_sta_scan_timeout &&
	    (uintptr_t)timer_arg == drv.sta_scan_generation && media_calls == 0 &&
	    legacy_scans == 0 && mlme_calls == 0,
	    "connected scan arms its timer without resetting association or media");
	fixture(&drv);
	drv.sta_scan_cookie = UINT32_MAX;
	p.freqs = NULL;
	check(wpa_driver_bsd_scan(&drv, &p) == 0 && submitted[0].len == 0 &&
	    submitted[0].cookie == 1, "all-channel scan has an empty list and nonzero cookie");
	for (i = 0; i < IEEE80211_STA_MAX_CHANNELS; i++)
		freqs[i] = 2400 + i;
	freqs[IEEE80211_STA_MAX_CHANNELS] = 0;
	p.freqs = freqs;
	fixture(&drv);
	check(wpa_driver_bsd_scan(&drv, &p) == 0 &&
	    submitted[0].len == IEEE80211_STA_MAX_CHANNELS * sizeof(u16),
	    "maximum representable scan frequency list is accepted");
	for (i = 0; i < 3; i++) {
		fixture(&drv);
		freqs[0] = i == 0 ? -1 : i == 1 ? 65536 : 2412;
		freqs[IEEE80211_STA_MAX_CHANNELS] = 5200;
		freqs[IEEE80211_STA_MAX_CHANNELS + 1] = 0;
		check(wpa_driver_bsd_scan(&drv, &p) == -1 && set_calls == 0 &&
		    timer_calls == 0, "invalid or oversized frequency lists make no request");
	}
	p.freqs = NULL;
	fixture(&drv);
	drv.sta_scan_pending = 1;
	check(wpa_driver_bsd_scan(&drv, &p) == -1 && set_calls == 0,
	    "a pending connected scan cannot be overwritten");
	fixture(&drv);
	set_error = -1;
	check(wpa_driver_bsd_scan(&drv, &p) == -1 && !drv.sta_scan_pending &&
	    timer_calls == 0, "failed scan ioctl creates no pending scan or timer");
	fixture(&drv);
	timer_error = -1;
	check(wpa_driver_bsd_scan(&drv, &p) == -1 && !drv.sta_scan_pending &&
	    set_calls == 2 && submitted[1].op == IEEE80211_STA_CANCEL_SCAN &&
	    submitted[1].len == 0 && submitted[1].cookie == submitted[0].cookie &&
	    submitted[1].generation == submitted[0].generation &&
	    !memcmp(submitted[1].bssid, submitted[0].bssid, ETH_ALEN),
	    "timer allocation failure cancels the same kernel scan and clears state");
	fixture(&drv);
	current.flags = 0;
	check(wpa_driver_bsd_scan(&drv, &p) == 0 && legacy_scans == 1 &&
	    set_calls == 0, "disconnected stations retain the ordinary scan path");
	fixture(&drv);
	drv.sta_btm = drv.sta_rrm = 0;
	check(wpa_driver_bsd_scan(&drv, &p) == 0 && legacy_scans == 1 &&
	    set_calls == 0, "old drivers retain their ordinary scan path");
	for (i = 0; i < 2; i++) {
		fixture(&drv);
		current.flags = IEEE80211_STA_ASSOCIATED |
		    (i == 0 ? IEEE80211_STA_CAP_BTM : IEEE80211_STA_CAP_RRM);
		check(wpa_driver_bsd_scan(&drv, &p) == 0 && set_calls == 1 &&
		    submitted[0].op == IEEE80211_STA_SCAN && legacy_scans == 0,
		    "either current station capability permits a connected scan");
	}
}

static void
restricted_association_scans(void)
{
	struct bsd_driver_data drv;
	struct wpa_driver_associate_params assoc;
	struct wpa_driver_scan_params scan;
	u8 ies[] = { WLAN_EID_RSN, 0, WLAN_EID_EXT_CAPAB, 3, 0, 0, 8 };
	unsigned i;

	memset(&scan, 0, sizeof(scan));
	for (i = 0; i < 2; i++) {
		fixture(&drv);
		assoc = parameters(ies, sizeof(ies));
		if (i == 0) {
			assoc.wpa_ie = NULL;
			assoc.wpa_ie_len = 0;
			assoc.pairwise_suite = assoc.group_suite = WPA_CIPHER_NONE;
			assoc.key_mgmt_suite = WPA_KEY_MGMT_NONE;
		} else {
			assoc.mgmt_frame_protection = MGMT_FRAME_PROTECTION_REQUIRED;
		}
		check(wpa_driver_bsd_associate(&drv, &assoc) == 0 && mlme_calls == 1,
		    "legacy security profiles still reach the association callback");
		/* AIC GET retains association metadata but withdraws k/v capabilities. */
		current.flags = IEEE80211_STA_ASSOCIATED | IEEE80211_STA_ASSOC_IES;
		set_calls = media_calls = 0;
		set_error = -1;
		check(wpa_driver_bsd_scan(&drv, &scan) == 0 &&
		    drv.sta_btm && drv.sta_rrm && set_calls == 0 &&
		    legacy_scans == 1 && media_calls == 1 && timer_calls == 0 &&
		    !drv.sta_scan_pending && drv.sta_scan_cookie == 40,
		    i == 0 ? "associated open security uses a legacy scan despite cached caps" :
		    "associated WPA2-PMF uses a legacy scan despite cached caps");
	}
}

static void
disabled_transport_scans(void)
{
	struct bsd_driver_data drv;
	struct wpa_driver_associate_params assoc;
	struct wpa_driver_scan_params scan;
	u8 ies[] = { WLAN_EID_RSN, 0, WLAN_EID_EXT_CAPAB, 3, 0, 0, 8 };
	unsigned i;

	memset(&scan, 0, sizeof(scan));
	for (i = 0; i < 5; i++) {
		fixture(&drv);
		assoc = parameters(ies, sizeof(ies));
		/* No BTM/RRM negotiation leaves the transport disabled. */
		if (i == 0 || i == 3 || i == 4)
			assoc.wpa_ie_len = 2;
		if (i != 0) {
			set_error = -1;
			drv.sta_enabled = i == 2 || i == 4;
		}
		check(bsd_sta_associate(&drv, &assoc) == (i == 0 ? 0 : -1) &&
		    !drv.sta_enabled && set_calls == 1,
		    "only a successful enable permits connected scans");
		set_calls = 0;
		check(wpa_driver_bsd_scan(&drv, &scan) == 0 && set_calls == 0 &&
		    legacy_scans == 1 && timer_calls == 0 && !drv.sta_scan_pending,
		    "disabled or uncertain transport uses a legacy scan despite current caps");
	}
}

int
main(void)
{
	association_requests();
	directed_association();
	association_payloads();
	connected_scans();
	restricted_association_scans();
	disabled_transport_scans();
	if (failures)
		return 1;
	printf("PASS: %u production BSD association and scan dispatch assertions\n", checks);
	return 0;
}
