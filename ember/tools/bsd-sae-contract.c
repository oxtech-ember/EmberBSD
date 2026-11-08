/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual callbacks; only kernel ioctls, entropy and unrelated OS work modeled. */
/* Request external declarations for unrelated optional helpers, avoiding their
 * unused-argument inline stubs. These helpers are not linked or exercised. */
#define WPA_TRACE_BFD
#define CONFIG_TESTING_OPTIONS
#define CONFIG_SUITEB
#define CONFIG_SUITEB192
#include "utils/includes.h"
#include "utils/common.h"
#include "utils/list.h"
#include "drivers/driver.h"
#include "common/ieee802_11_defs.h"
#undef WPA_TRACE_BFD
#undef CONFIG_TESTING_OPTIONS
#undef CONFIG_SUITEB
#undef CONFIG_SUITEB192
#include <net/if.h>

#ifdef WPA_DRIVER_FLAGS2_P2P_FEATURE_V2
#define BSD_TEST_LINK_ARG , -1
#else
#define BSD_TEST_LINK_ARG
#endif

#define IEEE80211_ADDR_LEN 6
#define IEEE80211_NWID_LEN 32
enum ieee80211_opmode { IEEE80211_M_STA, IEEE80211_M_IBSS, IEEE80211_M_AHDEMO };
#define IEEE80211_AUTH_OPEN 1
#define IEEE80211_AUTH_SHARED 2
#define IEEE80211_AUTH_AUTO 4
#define IFM_OMASK 0xff
#define IFM_IEEE80211_IBSS 1
#define IFM_IEEE80211_HOSTAP 2
#define SIOCG80211 1
#define SIOCGIFFLAGS 2
#define SIOCSIFFLAGS 3
#define RTM_IEEE80211_DISASSOC 1
typedef u_int16_t ieee80211_keyix;
/* ABI */
#undef IEEE80211_KEY_COMMON
#undef IEEE80211_IOC_DEVCAPS
#undef IEEE80211_IOC_SCAN_MAX_SSID
#ifdef BSD_TEST_OLD_ABI
#undef IEEE80211_SAE_CAP_KEY_EPOCH
#undef IEEE80211_SAE_SET_KEY
#undef IEEE80211_SAE_DELETE_KEY
#undef IEEE80211_SAE_SET_PORT
#undef IEEE80211_IOC_STA_PORT
#endif
#ifdef BSD_TEST_NETBSD
/* Define after host headers: exercise NetBSD driver branches on the host. */
#define __NetBSD__ 1
#endif
/* STATE */

struct request { int op, value, len; unsigned char data[2048]; };
static struct request requests[64];
static unsigned calls, checks, failures;
static int fail_op, fail_sae_op, fail_configure_off, caps, entropy_error;
static int iface_flags;
#ifndef HOSTAPD
static int fail_get_flags, fail_set_flags, ignore_set_flags, async_init;
#ifdef IEEE80211_IOC_STA_PORT
static int port_supported;
#endif
#endif
#ifdef CONFIG_SAE
static int native_configured;
#endif
static unsigned random_value = 0x23456789;

static void check(bool ok, const char *why)
{
	checks++;
	if (!ok) { fprintf(stderr, "FAIL: %s\n", why); failures++; }
}
#ifndef HOSTAPD
static void *test_alloc(size_t size)
{
	void *memory = malloc(size);
	if (!memory) abort();
	return memory;
}
#endif
void wpa_printf(int level, const char *fmt, ...) { (void)level; (void)fmt; }
void wpa_supplicant_event(void *ctx, enum wpa_event_type event, union wpa_event_data *data)
{ (void)ctx; (void)event; (void)data; }
void forced_memzero(void *p, size_t n) { memset(p, 0, n); }
size_t os_strlcpy(char *dst, const char *src, size_t n)
{
	size_t len = strlen(src);
	if (n) { size_t copy = len < n - 1 ? len : n - 1; memcpy(dst, src, copy); dst[copy] = 0; }
	return len;
}
int os_get_random(unsigned char *buf, size_t len)
{
	if (entropy_error) return -1;
	memset(buf, 0, len);
	memcpy(buf, &random_value, len < sizeof(random_value) ? len : sizeof(random_value));
	random_value++;
	return 0;
}
#ifndef HOSTAPD
static int ioctl(int fd, unsigned long cmd, void *data)
{
	(void)fd;
	if (!data) return -1;
	if (cmd == SIOCGIFFLAGS) {
		struct ifreq *ifr = data;
		if (fail_get_flags) return -1;
		ifr->ifr_flags = iface_flags;
		return 0;
	}
	if (cmd == SIOCSIFFLAGS) {
		struct ifreq *ifr = data;
		struct request *out;
		if (calls == 64) abort();
		out = &requests[calls++]; out->op = -SIOCSIFFLAGS; out->value = ifr->ifr_flags;
		if (fail_set_flags) return -1;
		if (!ignore_set_flags) {
			iface_flags = ifr->ifr_flags;
			if ((iface_flags & IFF_UP) && !async_init) iface_flags |= IFF_RUNNING;
		}
		/* Driver init begins with stop: a configuration installed before UP
		 * is retired. The callback must establish UP before CONFIGURE. */
#ifdef CONFIG_SAE
		native_configured = 0;
#endif
		return 0;
	}
	if (cmd == SIOCG80211) {
		struct ieee80211req *req = data;
		if (req->i_type == IEEE80211_IOC_SAE) { req->i_val = caps; return caps < 0 ? -1 : 0; }
#ifdef IEEE80211_IOC_STA_PORT
		if (req->i_type == IEEE80211_IOC_STA_PORT) { req->i_val = port_supported; return port_supported < 0 ? -1 : 0; }
#endif
	}
	return -1;
}
#endif
static int set80211var(void *priv, int op, const void *data, int len)
{
	struct request *out;
	struct ieee80211req_sae sae;
#ifdef CONFIG_SAE
	struct ieee80211req_mlme mlme;
#endif
	(void)priv;
	/* NULL is valid for scalar ioctls and empty IE clears, never copyin. */
	if (len < 0 || (size_t)len > sizeof(requests[0].data) || (len && !data))
		return -1;
	/* The kernel requires exact SAE/MLME envelopes, then copyin to aligned
	 * local storage before inspecting fields. Do not dereference user data. */
	if (op == IEEE80211_IOC_SAE) {
		if (!data || len != sizeof(sae)) return -1;
		memcpy(&sae, data, sizeof(sae));
	}
	if (op == IEEE80211_IOC_MLME) {
		if (!data || len != sizeof(struct ieee80211req_mlme)) return -1;
#ifdef CONFIG_SAE
		memcpy(&mlme, data, sizeof(mlme));
#endif
	}
	if (calls == 64) abort();
	out = &requests[calls++];
	out->op = op; out->len = len;
	if (len) memcpy(out->data, data, len);
	if (op == IEEE80211_IOC_SAE &&
	    (sae.op == fail_sae_op || (sae.op == IEEE80211_SAE_CONFIGURE &&
	    sae.data[0] == 0 && fail_configure_off))) return -1;
#ifdef CONFIG_SAE
	if (op == IEEE80211_IOC_SAE && sae.op == IEEE80211_SAE_CONFIGURE)
		native_configured = sae.data[0];
	if (op == IEEE80211_IOC_MLME && (caps & 8) &&
	    ((struct bsd_driver_data *)priv)->sae_active &&
	    mlme.im_op == IEEE80211_MLME_ASSOC) {
		if (!(iface_flags & IFF_UP)) { iface_flags |= IFF_UP; native_configured = 0; }
		if (!native_configured) return -1;
	}
#endif
	return op == fail_op ? -1 : 0;
}
#ifndef HOSTAPD
static int set80211param(void *priv, int op, int value)
{
	unsigned recorded = calls;
	int ret = set80211var(priv, op, NULL, 0);
	if (calls > recorded)
		requests[recorded].value = value;
	return ret;
}
static int bsd_set_mediaopt(void *priv, unsigned mask, unsigned mode)
{ (void)priv; (void)mask; (void)mode; return fail_op == -4 ? -1 : 0; }
static void handle_read(void *ctx, const u8 *src, const u8 *buf, size_t len)
{ (void)ctx; (void)src; (void)buf; (void)len; }
static void *l2_packet_init(const char *name, const u8 *addr, unsigned short proto,
	void (*callback)(void *, const u8 *, const u8 *, size_t), void *ctx, int hdr)
{
	static u8 endpoint;
	(void)name; (void)addr; (void)proto; (void)callback; (void)ctx; (void)hdr;
	return fail_op == -3 ? NULL : &endpoint;
}
static void l2_packet_deinit(void *p) { (void)p; }
#ifdef BSD_TEST_NETBSD
static int bsd_sta_associate(struct bsd_driver_data *drv,
	struct wpa_driver_associate_params *params)
{ (void)drv; (void)params; return fail_op == -2 ? -1 : 0; }
static void bsd_sta_scan_clear(struct bsd_driver_data *drv) { (void)drv; }
static void bsd_sta_tx_status(struct bsd_driver_data *drv, int ack, int aborted)
{ (void)drv; (void)ack; (void)aborted; }
static int bsd_sta_set(struct bsd_driver_data *drv,
	void *req, int op) { (void)drv; (void)req; (void)op; return 0; }
#endif
#endif

static void test_ioctl_boundaries(void)
{
	struct bsd_driver_data drv;
	struct ieee80211req_sae req;
	u8 short_data = 0;
	unsigned recorded;
	memset(&drv, 0, sizeof(drv)); memset(&req, 0, sizeof(req));
#ifndef HOSTAPD
	check(set80211param(&drv, IEEE80211_IOC_SAE, 17) == -1 && calls == 0,
	    "rejected scalar SAE request cannot underflow an empty request log");
#endif
	req.version = IEEE80211_SAE_VERSION; req.generation = 42;
	req.op = IEEE80211_SAE_AUTH_STATUS; req.len = 2; req.bssid[0] = 2;
	check(set80211var(&drv, IEEE80211_IOC_SAE, &req, sizeof(req)) == 0 && calls == 1 &&
	    requests[0].len == sizeof(req) && memcmp(requests[0].data, &req, sizeof(req)) == 0,
	    "modeled SAE service accepts and records a complete envelope");
	recorded = calls;
	check(set80211var(&drv, IEEE80211_IOC_SAE, &req, sizeof(req) - 1) == -1 && calls == recorded,
	    "modeled SAE service rejects a truncated envelope before recording");
	check(set80211var(&drv, IEEE80211_IOC_SAE, NULL, 0) == -1 && calls == recorded,
	    "modeled SAE service rejects a NULL empty request before recording");
	check(set80211var(&drv, IEEE80211_IOC_SAE, NULL, sizeof(req)) == -1 && calls == recorded,
	    "modeled SAE service rejects a NULL full-sized request before copyin");
	check(set80211var(&drv, IEEE80211_IOC_SAE, &short_data, sizeof(short_data)) == -1 && calls == recorded,
	    "modeled SAE service rejects a short header before payload access");
	check(set80211var(&drv, IEEE80211_IOC_SAE, &req, sizeof(req) + 1) == -1 && calls == recorded,
	    "modeled SAE service requires exact envelope size before copying");
#ifndef HOSTAPD
	check(set80211param(&drv, IEEE80211_IOC_SAE, 17) == -1 && calls == recorded &&
	    requests[0].value == 0,
	    "rejected scalar SAE request cannot overwrite a preceding request record");
	check(set80211param(&drv, IEEE80211_IOC_PRIVACY, 1) == 0 && calls == recorded + 1 &&
	    requests[recorded].op == IEEE80211_IOC_PRIVACY && requests[recorded].value == 1 &&
	    requests[recorded].len == 0,
	    "ordinary scalar ioctl still records its value without a payload");
#endif
	calls = 0; memset(requests, 0, sizeof(requests));
}

static void test_payload_copy_boundaries(void)
{
	struct bsd_driver_data drv;
	struct ieee80211req_mlme mlme;
	struct ieee80211req_sae sae;
	union {
		struct ieee80211req_sae align;
		u8 bytes[sizeof(struct ieee80211req_sae) + 1];
	} unaligned;
	const u8 short_data = 0, options[3] = { 48, 1, 0 };
	int saved_caps = caps;
	memset(&drv, 0, sizeof(drv)); memset(&mlme, 0, sizeof(mlme));
	memset(&sae, 0, sizeof(sae));
	/* Exercise the same provider-state branch as Linux GCC's OLD_ABI path. */
	caps = 8;
#ifdef CONFIG_SAE
	drv.sae_active = 1;
#endif
	mlme.im_op = IEEE80211_MLME_DEAUTH;
	check(set80211var(&drv, IEEE80211_IOC_MLME, &mlme, sizeof(mlme) - 1) == -1 && calls == 0,
	    "MLME rejects a truncated envelope before recording or reading im_op");
#ifndef HOSTAPD
	check(set80211param(&drv, IEEE80211_IOC_MLME, 0) == -1 && calls == 0,
	    "active SAE scalar MLME request cannot dereference NULL or mutate the request log");
#endif
	check(set80211var(&drv, IEEE80211_IOC_MLME, NULL, 0) == -1 && calls == 0,
	    "MLME rejects an empty NULL envelope even without an active SAE provider");
	check(set80211var(&drv, IEEE80211_IOC_MLME, NULL, sizeof(mlme)) == -1 && calls == 0,
	    "MLME rejects a NULL complete-sized envelope before copyin");
	check(set80211var(&drv, IEEE80211_IOC_MLME, &short_data, 1) == -1 && calls == 0,
	    "MLME rejects a short header before copying or reading fields");
	check(set80211var(&drv, IEEE80211_IOC_MLME, &mlme, sizeof(mlme) + 1) == -1 && calls == 0,
	    "MLME requires the exact kernel envelope size");
	check(set80211var(&drv, IEEE80211_IOC_OPTIE, NULL, 1) == -1 && calls == 0,
	    "generic nonempty copyin rejects NULL before recording");
	check(set80211var(&drv, IEEE80211_IOC_OPTIE, &short_data, -1) == -1 && calls == 0,
	    "generic copyin rejects negative lengths without recording");
	check(set80211var(&drv, IEEE80211_IOC_OPTIE, &short_data, sizeof(requests[0].data) + 1) == -1 && calls == 0,
	    "generic copyin rejects lengths beyond the modeled destination before copying");
#ifndef HOSTAPD
	check(ioctl(0, SIOCGIFFLAGS, NULL) == -1 && ioctl(0, SIOCSIFFLAGS, NULL) == -1 &&
	    ioctl(0, SIOCG80211, NULL) == -1 && calls == 0,
	    "modeled ioctl rejects NULL envelopes before reading or writing fields");
	check(ioctl(0, 0, (void *)&short_data) == -1 && calls == 0,
	    "unknown ioctl commands cannot reinterpret an unrelated payload");
#endif
	check(set80211var(&drv, IEEE80211_IOC_OPTIE, NULL, 0) == 0 && calls == 1 && requests[0].len == 0,
	    "zero-length NULL optional IE still clears the element");
	check(set80211var(&drv, IEEE80211_IOC_OPTIE, options, sizeof(options)) == 0 && calls == 2 &&
	    requests[1].len == sizeof(options) && memcmp(requests[1].data, options, sizeof(options)) == 0,
	    "valid variable-length payload preserves its bytes");
	memcpy(unaligned.bytes + 1, &mlme, sizeof(mlme));
	check(set80211var(&drv, IEEE80211_IOC_MLME, unaligned.bytes + 1, sizeof(mlme)) == 0 && calls == 3 &&
	    memcmp(requests[2].data, &mlme, sizeof(mlme)) == 0,
	    "MLME copyin accepts an unaligned user buffer without a typed user-pointer read");
	sae.version = IEEE80211_SAE_VERSION; sae.generation = 42;
	sae.op = IEEE80211_SAE_AUTH_STATUS; sae.len = 2; sae.bssid[0] = 2;
	memcpy(unaligned.bytes + 1, &sae, sizeof(sae));
	check(set80211var(&drv, IEEE80211_IOC_SAE, unaligned.bytes + 1, sizeof(sae)) == 0 && calls == 4 &&
	    memcmp(requests[3].data, &sae, sizeof(sae)) == 0,
	    "SAE copyin accepts an unaligned user buffer without a typed user-pointer read");
	caps = saved_caps; calls = 0; memset(requests, 0, sizeof(requests));
}

/* PRODUCTION */
static const struct wpa_driver_ops key_ops = { /* KEY_CALLBACK */
#ifdef HOSTAPD
/* DISASSOC_CALLBACK */
#endif
};
#if !defined(HOSTAPD) && !defined(BSD_TEST_OLD_ABI)
static const struct wpa_driver_ops port_ops = { /* PORT_CALLBACK */
#ifdef CONFIG_SAE
/* ACTION_CALLBACK */
#endif
};

static void clear_calls(void)
{
	calls = 0; fail_op = fail_sae_op = fail_configure_off = entropy_error = 0;
	fail_get_flags = fail_set_flags = ignore_set_flags = async_init = 0;
	memset(requests, 0, sizeof(requests));
}
static struct bsd_driver_data fixture(int capability)
{
	static struct bsd_driver_global global;
	struct bsd_driver_data drv;
	memset(&drv, 0, sizeof(drv)); drv.global = &global; drv.ifindex = 1;
	strcpy(drv.ifname, "test0"); caps = capability; port_supported = -1;
	iface_flags = IFF_UP | IFF_RUNNING;
#ifdef CONFIG_SAE
	native_configured = 0;
#endif
	clear_calls();
	(void)wpa_driver_bsd_capa(&drv);
	return drv;
}
static const u8 test_bssid[6] = { 2, 1, 2, 3, 4, 5 };
#ifdef CONFIG_SAE
static const u8 broadcast[6] = { 255, 255, 255, 255, 255, 255 };
#endif
static struct wpa_driver_associate_params association(int akm)
{
	static const u8 rsn[] = { 48, 2, 1, 0 };
	struct wpa_driver_associate_params p;
	memset(&p, 0, sizeof(p)); p.mode = IEEE80211_MODE_INFRA;
	p.key_mgmt_suite = akm; p.bssid = test_bssid; p.ssid = (const u8 *)"test"; p.ssid_len = 4;
	p.pairwise_suite = p.group_suite = WPA_CIPHER_CCMP;
	p.mgmt_frame_protection = MGMT_FRAME_PROTECTION_REQUIRED;
	p.wpa_ie = rsn; p.wpa_ie_len = sizeof(rsn);
	return p;
}
#ifdef CONFIG_SAE
static const struct ieee80211req_sae *last_sae(unsigned op)
{
	unsigned i;
	for (i = calls; i > 0; i--) if (requests[i - 1].op == IEEE80211_IOC_SAE) {
		const struct ieee80211req_sae *s = (const void *)requests[i - 1].data;
		if (s->op == op) return s;
	}
	return NULL;
}
#endif
static bool has_op(int op)
{
	unsigned i; for (i = 0; i < calls; i++) if (requests[i].op == op) return true;
	return false;
}

int main(void)
{
	struct bsd_driver_data drv = fixture(15);
	struct wpa_driver_associate_params p = association(WPA_KEY_MGMT_PSK);
	u8 key[32] = { 7 }, seq[8] = { 1, 2, 3, 4, 5, 6 };
	struct wpa_driver_set_key_params k;
	memset(&k, 0, sizeof(k)); k.alg = WPA_ALG_CCMP; k.addr = test_bssid;
	k.link_id = -1;
	k.key = key; k.key_len = 16; k.seq = seq; k.seq_len = 6; k.set_tx = 1;
	test_ioctl_boundaries();
	test_payload_copy_boundaries();
	check(wpa_driver_bsd_associate(&drv, &p) == 0, "PSK selection remains available on transition-capable provider");
	clear_calls();
	check(key_ops.set_key && key_ops.set_key(&drv, &k) == 0 && has_op(IEEE80211_IOC_WPAKEY), "PSK uses legacy key ABI");
	check(port_ops.set_supp_port != NULL, "supplicant completion callback is wired");
#ifdef CONFIG_SAE
	{
		const struct ieee80211req_sae *s;
		unsigned generation;
		drv = fixture(15); p = association(WPA_KEY_MGMT_SAE); iface_flags = IFF_BROADCAST;
		check(wpa_driver_bsd_associate(&drv, &p) == 0 && native_configured && drv.sae_active,
		    "native down-to-up initialization completes before SAE configuration survives MLME");
		check(has_op(-SIOCSIFFLAGS) && drv.flags == (IFF_UP | IFF_RUNNING | IFF_BROADCAST),
		    "native interface-up preserves other flags and refreshes driver cache");
		{
			unsigned up = 64, configure = 64, i;
			for (i = 0; i < calls; i++) {
				if (requests[i].op == -SIOCSIFFLAGS) up = i;
				if (requests[i].op == IEEE80211_IOC_SAE &&
				    ((struct ieee80211req_sae *)(void *)requests[i].data)->op == IEEE80211_SAE_CONFIGURE)
					configure = i;
			}
			check(up < configure, "UP ioctl precedes CONFIGURE-on");
		}
		drv = fixture(15); iface_flags = IFF_UP | IFF_BROADCAST;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sae_active &&
		    !last_sae(IEEE80211_SAE_CONFIGURE) && !has_op(IEEE80211_IOC_MLME) && !has_op(-SIOCSIFFLAGS),
		    "UP without RUNNING stops before SAE configuration and MLME without restarting initialization");
		clear_calls(); iface_flags |= IFF_RUNNING;
		check(wpa_driver_bsd_associate(&drv, &p) == 0 && drv.sae_active && native_configured &&
		    !has_op(-SIOCSIFFLAGS) && last_sae(IEEE80211_SAE_CONFIGURE) && has_op(IEEE80211_IOC_MLME),
		    "retry after asynchronous initialization reaches RUNNING configures SAE and submits MLME");
		drv = fixture(15); iface_flags = IFF_BROADCAST; async_init = 1;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sae_active &&
		    has_op(-SIOCSIFFLAGS) && iface_flags == (IFF_UP | IFF_BROADCAST) &&
		    !last_sae(IEEE80211_SAE_CONFIGURE) && !has_op(IEEE80211_IOC_MLME),
		    "cold asynchronous UP acknowledges before RUNNING and cannot configure SAE yet");
		clear_calls(); iface_flags |= IFF_RUNNING;
		check(wpa_driver_bsd_associate(&drv, &p) == 0 && drv.sae_active && native_configured &&
		    !has_op(-SIOCSIFFLAGS),
		    "cold initialization completion permits a later retry without another UP ioctl");
		drv = fixture(15); iface_flags = 0; fail_set_flags = 1;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sae_active &&
		    !last_sae(IEEE80211_SAE_CONFIGURE) && !has_op(IEEE80211_IOC_MLME),
		    "failed native UP ioctl stops before configuration and MLME");
		drv = fixture(15); iface_flags = 0; fail_get_flags = 1;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !last_sae(IEEE80211_SAE_CONFIGURE),
		    "failed native flag refresh stops before configuration");
		drv = fixture(15); iface_flags = 0; ignore_set_flags = 1;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !last_sae(IEEE80211_SAE_CONFIGURE),
		    "native UP acknowledgement must be confirmed by current flags");
		drv = fixture(3); iface_flags = 0;
		check(wpa_driver_bsd_associate(&drv, &p) == 0 && !has_op(-SIOCSIFFLAGS),
		    "old FullMAC association does not gain implicit interface-up behavior");
		drv = fixture(15); iface_flags = 0; p = association(WPA_KEY_MGMT_PSK);
		check(wpa_driver_bsd_associate(&drv, &p) == 0 && !has_op(-SIOCSIFFLAGS),
		    "WPA2 association does not gain implicit interface-up behavior");
		drv = fixture(15); p = association(WPA_KEY_MGMT_SAE);
		check(wpa_driver_bsd_associate(&drv, &p) == 0 && drv.sae_active, "SAE selected from WPA3 or transition profile enters external authentication");
		generation = drv.sae_generation;
		check(generation != 0 && generation != 1, "restart does not deterministically reuse generation one");
		check(port_ops.send_action != NULL, "SA Query callback matches the public action callback type");
#ifdef WPA_DRIVER_FLAGS2_P2P_FEATURE_V2
		{
			const u8 query[4] = { WLAN_ACTION_SA_QUERY, 0, 1, 2 };
			clear_calls();
			check(port_ops.send_action && port_ops.send_action(&drv, 0, 0, test_bssid, test_bssid, test_bssid, query, sizeof(query), 0, 0) == -1 && calls == 0,
			    "WPA 2.12 MLO link action is rejected before ioctl");
		}
#endif
		check(last_sae(IEEE80211_SAE_SET_PORT) == NULL, "association does not authorize port");
		clear_calls();
		check(bsd_set_key(&drv, &k) == 0, "CCMP data key installation accepted");
		s = last_sae(IEEE80211_SAE_SET_KEY);
		check(s && s->generation == generation && memcmp(s->bssid, test_bssid, 6) == 0 &&
		    s->len == sizeof(struct ieee80211req_key), "native PTK carries current generation and BSSID");
		check(!has_op(IEEE80211_IOC_WPAKEY), "native SAE never falls back to untagged key ioctl");
		if (s) { struct ieee80211req_key wk; memcpy(&wk, s->data, sizeof(wk));
			check(wk.ik_type == IEEE80211_CIPHER_AES_CCM && wk.ik_keylen == 16 &&
			    wk.ik_keyix == IEEE80211_KEYIX_NONE && memcmp(wk.ik_keydata, key, 16) == 0 &&
			    wk.ik_flags == (IEEE80211_KEY_RECV | IEEE80211_KEY_XMIT) &&
			    wk.ik_keyrsc == UINT64_C(0x060504030201),
			    "tagged PTK preserves native key payload"); }
		clear_calls(); fail_sae_op = IEEE80211_SAE_SET_KEY;
		check(bsd_set_key(&drv, &k) == -1 && !has_op(IEEE80211_IOC_WPAKEY), "tagged key rejection cannot retry through legacy ABI");
		clear_calls(); k.alg = WPA_ALG_NONE;
		check(bsd_set_key(&drv, &k) == 0 && last_sae(IEEE80211_SAE_DELETE_KEY), "native pairwise key deletion is tagged");
		s = last_sae(IEEE80211_SAE_DELETE_KEY);
		if (s) { struct ieee80211req_del_key wk; memcpy(&wk, s->data, sizeof(wk));
			check(wk.idk_keyix == 255 && memcmp(wk.idk_macaddr, test_bssid, 6) == 0,
			    "pairwise delete retains native eight-bit sentinel and peer"); }
		clear_calls(); k.alg = WPA_ALG_CCMP; k.addr = broadcast; k.key_idx = 2; k.set_tx = 0;
		check(bsd_set_key(&drv, &k) == 0 && last_sae(IEEE80211_SAE_SET_KEY), "GTK rekey remains epoch tagged");
		s = last_sae(IEEE80211_SAE_SET_KEY);
		if (s) { struct ieee80211req_key wk; memcpy(&wk, s->data, sizeof(wk));
			check(wk.ik_keyix == 2 && wk.ik_flags == (IEEE80211_KEY_RECV | IEEE80211_KEY_GROUP),
			    "GTK rekey retains key index and group receive flags"); }
		clear_calls(); k.alg = WPA_ALG_NONE;
		check(bsd_set_key(&drv, &k) == 0 && last_sae(IEEE80211_SAE_DELETE_KEY), "broadcast GTK clear uses tagged delete");
		s = last_sae(IEEE80211_SAE_DELETE_KEY);
		if (s) { struct ieee80211req_del_key wk; memcpy(&wk, s->data, sizeof(wk));
			check(wk.idk_keyix == 2, "GTK clear retains global key index"); }
		clear_calls(); k.alg = WPA_ALG_BIP_CMAC_128; k.key_idx = 4;
		check(bsd_set_key(&drv, &k) == 0 && last_sae(IEEE80211_SAE_SET_IGTK), "IGTK retains dedicated epoch ABI");
		s = last_sae(IEEE80211_SAE_SET_IGTK);
		if (s) check(s->len == 24 && s->data[0] == 4 && s->data[1] == 0 &&
		    memcmp(s->data + 2, seq, 6) == 0 && memcmp(s->data + 8, key, 16) == 0,
		    "IGTK payload has little-endian key ID, IPN, and 128-bit key");
		clear_calls(); k.alg = WPA_ALG_NONE;
		check(bsd_set_key(&drv, &k) == 0 && last_sae(IEEE80211_SAE_DELETE_IGTK), "IGTK clear keeps dedicated epoch ABI");
		clear_calls(); k.link_id = 0;
		check(bsd_set_key(&drv, &k) == -1 && calls == 0,
		    "native SAE rejects link-specific key deletion");
		k.alg = WPA_ALG_BIP_CMAC_128;
		check(bsd_set_key(&drv, &k) == -1 && calls == 0,
		    "native SAE rejects link-specific IGTK installation");
		k.link_id = -1;
		clear_calls();
		if (port_ops.set_supp_port) {
			check(port_ops.set_supp_port(&drv, 1) == 0, "explicit completion authorizes native port");
			s = last_sae(IEEE80211_SAE_SET_PORT);
			check(s && s->len == 1 && s->data[0] == 1 && s->generation == generation &&
			    !has_op(IEEE80211_IOC_STA_PORT), "native port completion carries epoch and bypasses plain ioctl");
			clear_calls(); fail_sae_op = IEEE80211_SAE_SET_PORT;
			check(port_ops.set_supp_port(&drv, 1) == -1 && !has_op(IEEE80211_IOC_STA_PORT),
			    "failed native port completion never falls back");
			clear_calls();
			check(port_ops.set_supp_port(&drv, 0) == 0 && last_sae(IEEE80211_SAE_SET_PORT)->data[0] == 0,
			    "port closure is explicit and epoch tagged");
		}
		clear_calls(); fail_op = IEEE80211_IOC_MLME;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sae_active, "failed MLME retires SAE state");
		s = last_sae(IEEE80211_SAE_CONFIGURE);
		check(s && s->data[0] == 0, "failed association disables configured kernel session");
		drv = fixture(15); fail_op = IEEE80211_IOC_PRIVACY;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sae_active, "preconfiguration failure leaves SAE inactive");
		drv = fixture(15); fail_op = -4;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sae_active && calls == 0,
		    "failed media setup stops before SAE configuration");
		drv = fixture(15);
		check(wpa_driver_bsd_associate(&drv, &p) == 0, "lifecycle fixture starts SAE");
		generation = drv.sae_generation;
		clear_calls(); fail_configure_off = 1;
		p = association(WPA_KEY_MGMT_PSK);
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sae_active,
		    "failed retirement blocks transition to PSK and suppresses old SAE events");
		s = last_sae(IEEE80211_SAE_CONFIGURE);
		check(s && s->data[0] == 0 && s->generation == generation && memcmp(s->bssid, test_bssid, 6) == 0,
		    "retirement uses original generation and BSSID");
		clear_calls(); k.alg = WPA_ALG_CCMP; k.addr = test_bssid; k.key_idx = 0; k.set_tx = 1;
		check(bsd_set_key(&drv, &k) == -1 && calls == 0, "retirement failure cannot fall back to ordinary data key install");
		check(wpa_driver_bsd_associate(&drv, &p) == 0 && !drv.sae_active,
		    "successful retirement retry permits PSK transition");
		drv = fixture(15); p = association(WPA_KEY_MGMT_SAE);
		check(wpa_driver_bsd_associate(&drv, &p) == 0, "disconnect event fixture starts SAE");
		generation = drv.sae_generation; clear_calls();
		route_disassoc(&drv);
		check(!drv.sae_active && drv.sae_generation == generation && calls == 0,
		    "disconnect event suppresses old auth callbacks without an untagged kernel mutation");
		check(bsd_set_key(&drv, &k) == -1 && calls == 0,
		    "disconnect event blocks delayed key installation");
		drv = fixture(15); p = association(WPA_KEY_MGMT_SAE);
		check(wpa_driver_bsd_associate(&drv, &p) == 0, "deauthentication fixture starts SAE");
		clear_calls();
		check(wpa_driver_bsd_deauthenticate(&drv, test_bssid, 3) == 0 && !drv.sae_active,
		    "local deauthentication retires SAE userland state");
		check(requests[0].op == IEEE80211_IOC_MLME && last_sae(IEEE80211_SAE_CONFIGURE),
		    "local deauthentication reaches kernel before key retirement");
		clear_calls();
		check(bsd_set_key(&drv, &k) == -1 && calls == 0, "retired SAE cannot install delayed data key through legacy path");
		{
			u8 frame[30] = { 0xb0 };
			struct external_auth auth;
			memset(&auth, 0, sizeof(auth)); auth.bssid = test_bssid;
			check(bsd_sae_send_mlme(&drv, frame, sizeof(frame), 0, 0, NULL, 0, 0, 0, -1) == -1 &&
			    bsd_sae_auth_status(&drv, &auth) == -1 && calls == 0,
			    "retired SAE rejects delayed auth frame and completion");
			check(bsd_sae_send_action(&drv, 0, 0, test_bssid, test_bssid, test_bssid, frame, 4, 0 BSD_TEST_LINK_ARG) == -1 && calls == 0,
			    "retired SAE rejects delayed SA Query action");
		}
		drv = fixture(15); check(wpa_driver_bsd_associate(&drv, &p) == 0, "deinit fixture starts SAE");
		clear_calls();
		{
			struct bsd_driver_data *heap = test_alloc(sizeof(*heap));
			*heap = drv; dl_list_init(&heap->list);
			wpa_driver_bsd_deinit(heap);
		}
		s = last_sae(IEEE80211_SAE_CONFIGURE);
		check(s && s->data[0] == 0, "deinit retires configured session before freeing driver");
		drv = fixture(15); fail_sae_op = IEEE80211_SAE_CONFIGURE;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sae_active,
		    "failed atomic configure never activates local state");
		s = last_sae(IEEE80211_SAE_CONFIGURE);
		check(s && s->data[0] == 1, "failed atomic enable does not disable an uninstalled generation");
#ifdef BSD_TEST_NETBSD
		drv = fixture(15); fail_op = -2;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sae_active &&
		    last_sae(IEEE80211_SAE_CONFIGURE)->data[0] == 0,
		    "station management failure retires successful SAE configuration");
#endif
		drv = fixture(0);
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !has_op(IEEE80211_IOC_MLME), "SAE without capability is rejected before MLME");
		drv = fixture(3); k.alg = WPA_ALG_CCMP; k.addr = test_bssid; k.key_idx = 0; k.set_tx = 1;
		check(wpa_driver_bsd_associate(&drv, &p) == 0, "old FullMAC external SAE remains supported");
		clear_calls();
		check(bsd_set_key(&drv, &k) == 0 && has_op(IEEE80211_IOC_WPAKEY), "old FullMAC keeps legacy data-key ABI");
		drv = fixture(15); entropy_error = 1;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sae_active, "generation entropy failure is closed");
		drv = fixture(15); p.key_mgmt_suite = WPA_KEY_MGMT_SAE | WPA_KEY_MGMT_PSK;
		check(wpa_driver_bsd_associate(&drv, &p) == -1 && !has_op(IEEE80211_IOC_MLME),
		    "association requires one selected AKM, never a transition mask");
		{
			int incomplete[] = { -1, 0, 1, 2, 8, 9, 10 };
			unsigned i;
			p = association(WPA_KEY_MGMT_SAE);
			for (i = 0; i < sizeof(incomplete) / sizeof(incomplete[0]); i++) {
				drv = fixture(incomplete[i]);
				check(!(drv.capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_SAE) &&
				    wpa_driver_bsd_associate(&drv, &p) == -1 && calls == 0,
				    "absent or incomplete capabilities never enter SAE association");
			}
		}
	}
#else
	p = association(WPA_KEY_MGMT_SAE); clear_calls();
	check(wpa_driver_bsd_associate(&drv, &p) == -1 && !has_op(IEEE80211_IOC_MLME), "build without SAE rejects SAE association");
	clear_calls();
	check(wpa_driver_bsd_deauthenticate(&drv, test_bssid, 3) == 0 && has_op(IEEE80211_IOC_MLME),
	    "build without SAE retains local deauthentication");
	clear_calls(); route_disassoc(&drv);
	check(calls == 0, "build without SAE retains notification-only disconnect event");
	{
		struct bsd_driver_data *heap = test_alloc(sizeof(*heap));
		*heap = drv; dl_list_init(&heap->list);
		wpa_driver_bsd_deinit(heap);
	}
#endif
	if (port_ops.set_supp_port) {
		drv = fixture(0); clear_calls();
		check(port_ops.set_supp_port(&drv, 1) == 0 && !has_op(IEEE80211_IOC_STA_PORT), "absent ordinary port capability preserves compatibility");
		port_supported = 1; clear_calls();
		check(port_ops.set_supp_port(&drv, 1) == 0 && has_op(IEEE80211_IOC_STA_PORT), "confirmed ordinary port capability is used");
	}
	drv = fixture(0); p = association(WPA_KEY_MGMT_PSK); p.mode = IEEE80211_MODE_AP;
	fail_op = -3;
	check(wpa_driver_bsd_associate(&drv, &p) == -1 && !drv.sock_xmit,
	    "legacy AP association fails if the EAPOL endpoint cannot open");
	clear_calls();
	check(wpa_driver_bsd_associate(&drv, &p) == 0 && drv.sock_xmit && drv.is_ap,
	    "legacy AP association retains EAPOL endpoint setup");
	printf("BSD SAE callbacks: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
#else
int main(void)
{
	struct bsd_driver_data drv;
	struct wpa_driver_set_key_params key;
	u8 material[16] = { 1 };
	memset(&drv, 0, sizeof(drv)); memset(&key, 0, sizeof(key));
	test_ioctl_boundaries();
	test_payload_copy_boundaries();
	key.alg = WPA_ALG_CCMP; key.key = material; key.key_len = sizeof(material); key.link_id = -1;
	check(key_ops.set_key && key_ops.set_key(&drv, &key) == 0 && calls == 1 && requests[0].op == IEEE80211_IOC_WPAKEY,
	    "hostapd and old-header builds retain legacy key installation");
#ifdef HOSTAPD
	{
		const u8 peer[6] = { 2, 1, 2, 3, 4, 5 };
		check(key_ops.sta_disassoc && key_ops.sta_disassoc(&drv, NULL, peer, 3 BSD_TEST_LINK_ARG) == 0 &&
		    calls == 2 && requests[1].op == IEEE80211_IOC_MLME,
		    "hostapd disassociation matches the public callback and preserves MLME");
#ifdef WPA_DRIVER_FLAGS2_P2P_FEATURE_V2
		check(key_ops.sta_disassoc && key_ops.sta_disassoc(&drv, NULL, peer, 3, 0) == -1 && calls == 2,
		    "hostapd WPA 2.12 MLO link disassociation is rejected before ioctl");
#endif
	}
#endif
#ifdef BSD_TEST_OLD_ABI
	{
		struct bsd_driver_global global;
		struct wpa_driver_associate_params params;
		struct external_auth auth;
		struct bsd_driver_data *heap;
		const u8 peer[6] = { 2, 1, 2, 3, 4, 5 };
		u8 frame[30] = { 0xb0 };
		memset(&global, 0, sizeof(global)); drv.global = &global; drv.ifindex = 1;
		strcpy(drv.ifname, "test0"); caps = 3;
		(void)wpa_driver_bsd_capa(&drv);
		check((drv.capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_SAE) != 0,
		    "old headers keep optional capability discovery");
		memset(&params, 0, sizeof(params)); params.mode = IEEE80211_MODE_INFRA;
		params.key_mgmt_suite = WPA_KEY_MGMT_SAE; params.bssid = peer;
		params.pairwise_suite = params.group_suite = WPA_CIPHER_CCMP;
		params.mgmt_frame_protection = MGMT_FRAME_PROTECTION_REQUIRED;
		check(wpa_driver_bsd_associate(&drv, &params) == 0 && drv.sae_active,
		    "old headers retain FullMAC external SAE configuration");
		check(bsd_set_supp_port(&drv, 1) == 0,
		    "old headers retain port compatibility without the optional ioctl");
		route_disassoc(&drv);
		check(!drv.sae_active, "old headers retire local callbacks on disconnect");
		check(wpa_driver_bsd_deauthenticate(&drv, peer, 3) == 0,
		    "old headers retain explicit SAE retirement");
		memset(&auth, 0, sizeof(auth)); auth.bssid = peer;
		check(bsd_sae_send_mlme(&drv, frame, sizeof(frame), 0, 0, NULL, 0, 0, 0, -1) == -1 &&
		    bsd_sae_auth_status(&drv, &auth) == -1 &&
		    bsd_sae_send_action(&drv, 0, 0, peer, peer, peer, frame, 4, 0 BSD_TEST_LINK_ARG) == -1,
		    "old headers reject retired management callbacks");
		heap = test_alloc(sizeof(*heap)); *heap = drv; dl_list_init(&heap->list);
		wpa_driver_bsd_deinit(heap);
	}
#endif
	printf("BSD compatibility callbacks: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
#endif
