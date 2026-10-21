/* SPDX-License-Identifier: BSD-2-Clause */
/* AI-assisted host model. Production function bodies and kernel switch arms
 * are extracted unchanged by the companion runner. No target headers are used.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define SIOCSIFMEDIA 1
#define SIOCGIFMEDIA 2
#define SIOCS80211 3
#define SIOCS80211NWID 4
#define SIOCSIFFLAGS 5
#define SIOCADDMULTI 6
#define SIOCDELMULTI 7
#define IEEE80211_IOC_ROAMING 12
#define IEEE80211_IOC_SCAN_REQ 23
#define IEEE80211_ROAMING_DEVICE 0
#define IEEE80211_ROAMING_AUTO 1
#define IEEE80211_ROAMING_MANUAL 2
#define IEEE80211_M_STA 1
#define IEEE80211_M_HOSTAP 2
#define IEEE80211_S_INIT 0
#define IEEE80211_S_SCAN 1
#define IEEE80211_S_RUN 4
#define IEEE80211_CHAN_BYTES 32
#define IEEE80211_NWID_LEN 32
#define IFF_UP 1
#define IFF_RUNNING 64
#define IFM_OMASK 0xFF00
#define IFM_AUTO 0
#define MSG_DEBUG 0
#define MSG_ERROR 1
#define IS_UP(ic)                                                              \
	(((ic)->ic_ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==                  \
	 (IFF_UP | IFF_RUNNING))
#define IS_UP_AUTO(ic) (IS_UP(ic) && (ic)->ic_roaming == IEEE80211_ROAMING_AUTO)
#define wpa_printf(...) ((void)0)
#define ioctl model_ioctl
#define os_memset memset
#define os_memcpy memcpy
#define os_strlcpy model_strlcpy

typedef unsigned char u8;
struct ifnet {
	void *if_softc;
	unsigned if_flags;
};
struct ieee80211com {
	struct ifnet *ic_ifp;
	int ic_state, ic_roaming, ic_opmode;
	unsigned char ic_chan_avail[IEEE80211_CHAN_BYTES];
	unsigned char ic_chan_active[IEEE80211_CHAN_BYTES];
	unsigned char ic_des_essid[IEEE80211_NWID_LEN];
	unsigned ic_des_esslen;
};
struct rtw88_net {
	struct ieee80211com ic;
};
struct ieee80211_nwid {
	unsigned i_len;
	unsigned char i_nwid[IEEE80211_NWID_LEN];
};
struct ifreq {
	char ifr_name[16];
	void *ifr_data;
	int ifr_media;
};
struct ifmediareq {
	char ifm_name[16];
	int ifm_current;
};
struct ieee80211req {
	char i_name[16];
	int i_type, i_val;
	const void *i_data;
	int i_len;
};
struct bsd_global {
	int sock;
};
struct bsd_driver_data {
	struct bsd_global *global;
	char ifname[16];
	int ifindex, if_removed;
	unsigned flags;
};
struct scan_ssid {
	const unsigned char *ssid;
	size_t ssid_len;
};
struct wpa_driver_scan_params {
	size_t num_ssids;
	struct scan_ssid ssids[1];
};
static struct rtw88_net net;
static struct ifnet interface;
static unsigned checks, failures, cold_starts, state_requests, media_sets,
    media_gets, nwid_calls, scan_calls;
static int configured_media, get_error, set_error, copy_error, state_error,
    saw_init;
#define CHECK(expr)                                                            \
	do {                                                                   \
		checks++;                                                      \
		if (!(expr)) {                                                 \
			failures++;                                            \
			fprintf(stderr, "FAIL %u: %s\n", (unsigned)__LINE__,   \
				#expr);                                        \
		}                                                              \
	} while (0)
static size_t model_strlcpy(char *d, const char *s, size_t n)
{
	size_t l = strlen(s);
	if (n) {
		size_t k = l < n - 1 ? l : n - 1;
		memcpy(d, s, k);
		d[k] = 0;
	}
	return l;
}
static int copyin(const void *s, void *d, size_t n)
{
	if (copy_error)
		return copy_error;
	memcpy(d, s, n);
	return 0;
}
static int ieee80211_new_state(struct ieee80211com *ic, int state, int arg)
{
	(void)arg;
	state_requests++;
	saw_init = ic->ic_state == IEEE80211_S_INIT;
	if (!state_error)
		ic->ic_state = state;
	return state_error;
}
static int ifioctl_common(struct ifnet *ifp, u_long cmd, void *data)
{
	(void)ifp;
	(void)cmd;
	(void)data;
	return 0;
}
static int rtw88_net_init(struct ifnet *ifp)
{
	(void)ifp;
	cold_starts++;
	return 0;
}
static void rtw88_net_stop(struct ifnet *ifp, int disable)
{
	(void)ifp;
	(void)disable;
}
static int ieee80211_ioctl(struct ieee80211com *, u_long, void *);
static int model_ioctl(int, u_long, void *);
static int wpa_driver_bsd_set_wpa(void *priv, int enabled)
{
	(void)priv;
	(void)enabled;
	return 0;
}
#include "actual.inc"
static int ieee80211_ioctl(struct ieee80211com *ic, u_long cmd, void *data)
{
	if (cmd == SIOCS80211NWID) {
		nwid_calls++;
		return kernel_nwid(ic, data);
	}
	if (cmd == SIOCS80211) {
		struct ieee80211req *r = data;
		if (r->i_type == IEEE80211_IOC_SCAN_REQ)
			scan_calls++;
		return kernel_set(ic, r);
	}
	return EINVAL;
}
static int model_ioctl(int fd, u_long cmd, void *data)
{
	int error;
	(void)fd;
	if (cmd == SIOCGIFMEDIA) {
		media_gets++;
		if (get_error) {
			errno = get_error;
			return -1;
		}
		((struct ifmediareq *)data)->ifm_current = configured_media;
		return 0;
	}
	if (cmd == SIOCSIFMEDIA) {
		media_sets++;
		if (set_error) {
			errno = set_error;
			return -1;
		}
		configured_media = ((struct ifreq *)data)->ifr_media;
		return 0;
	}
	error = rtw88_net_ioctl(&interface, cmd, data);
	if (error) {
		errno = error;
		return -1;
	}
	return 0;
}
static void reset(void)
{
	memset(&net, 0, sizeof(net));
	interface = (struct ifnet){&net, IFF_UP | IFF_RUNNING};
	net.ic.ic_ifp = &interface;
	net.ic.ic_state = IEEE80211_S_RUN;
	net.ic.ic_roaming = IEEE80211_ROAMING_MANUAL;
	net.ic.ic_opmode = IEEE80211_M_STA;
	memset(net.ic.ic_chan_avail, 0x5a, sizeof(net.ic.ic_chan_avail));
	configured_media = 0;
	get_error = set_error = copy_error = state_error = saw_init = 0;
	cold_starts = state_requests = media_sets = media_gets = nwid_calls =
	    scan_calls = 0;
}
int main(void)
{
	struct bsd_global global = {0};
	struct bsd_driver_data drv = {&global, "test0", 1, 0, IFF_UP};
	static const unsigned char ssid[] = {0x61, 0, 0x62};
	struct wpa_driver_scan_params scan = {1, {{ssid, sizeof(ssid)}}};
	struct ieee80211_nwid nwid = {3, {0x61, 0, 0x62}};
	struct ifreq request = {{0}, &nwid, 0};
	reset();
	CHECK(wpa_driver_bsd_scan(&drv, &scan) == 0);
	CHECK(media_gets == 1 && media_sets == 0);
	CHECK(cold_starts == 0);
	CHECK(nwid_calls == 1 && scan_calls == 1 && state_requests == 1 &&
	      saw_init);
	CHECK(net.ic.ic_state == IEEE80211_S_SCAN && net.ic.ic_des_esslen == 3);
	CHECK(memcmp(net.ic.ic_des_essid, ssid, 3) == 0);
	CHECK(memcmp(net.ic.ic_chan_active, net.ic.ic_chan_avail,
		     IEEE80211_CHAN_BYTES) == 0);
	reset();
	CHECK(kernel_nwid(&net.ic, &request) == 0);
	CHECK(net.ic.ic_state == IEEE80211_S_RUN);
	net.ic.ic_roaming = IEEE80211_ROAMING_AUTO;
	CHECK(kernel_nwid(&net.ic, &request) == ENETRESET);
	net.ic.ic_roaming = IEEE80211_ROAMING_DEVICE;
	CHECK(kernel_nwid(&net.ic, &request) == ENETRESET);
	reset();
	copy_error = EFAULT;
	CHECK(kernel_nwid(&net.ic, &request) == EFAULT);
	CHECK(net.ic.ic_des_esslen == 0);
	copy_error = 0;
	nwid.i_len = 33;
	CHECK(kernel_nwid(&net.ic, &request) == EINVAL);
	CHECK(net.ic.ic_des_esslen == 0);
	nwid.i_len = 3;
	reset();
	configured_media = 0x100;
	CHECK(bsd_set_mediaopt(&drv, IFM_OMASK, 0) == 0);
	CHECK(media_sets == 1 && configured_media == 0);
	reset();
	configured_media = 7;
	CHECK(bsd_set_mediaopt(&drv, IFM_OMASK, 0x200) == 0);
	CHECK(media_sets == 1 && configured_media == 0x207);
	reset();
	configured_media = 0x8007;
	CHECK(bsd_set_mediaopt(&drv, IFM_OMASK, 0x100) == 0);
	CHECK(media_sets == 1 && configured_media == 0x107);
	reset();
	set_error = EIO;
	CHECK(bsd_set_mediaopt(&drv, IFM_OMASK, 0x100) == -1);
	CHECK(media_sets == 1 && errno == EIO);
	reset();
	get_error = ENXIO;
	CHECK(wpa_driver_bsd_scan(&drv, &scan) == -1);
	CHECK(media_sets == 0 && nwid_calls == 0 && scan_calls == 0);
	reset();
	copy_error = EFAULT;
	CHECK(wpa_driver_bsd_scan(&drv, &scan) == -1);
	CHECK(scan_calls == 0 && state_requests == 0);
	reset();
	interface.if_flags = IFF_UP;
	CHECK(wpa_driver_bsd_scan(&drv, &scan) == -1);
	CHECK(scan_calls == 1 && state_requests == 0);
	reset();
	state_error = EBUSY;
	CHECK(wpa_driver_bsd_scan(&drv, &scan) == -1);
	CHECK(scan_calls == 1 && state_requests == 1 && saw_init);
	reset();
	scan.num_ssids = 0;
	CHECK(wpa_driver_bsd_scan(&drv, &scan) == 0);
	CHECK(nwid_calls == 0 && scan_calls == 1 && state_requests == 1);
	CHECK(cold_starts == 0 && media_sets == 0);
	reset();
	nwid.i_len = 0;
	memset(net.ic.ic_des_essid, 0xff, sizeof(net.ic.ic_des_essid));
	CHECK(kernel_nwid(&net.ic, &request) == 0);
	CHECK(net.ic.ic_des_esslen == 0 && net.ic.ic_des_essid[0] == 0);
	printf("Native manual scan: %u checks, %u failures\n", checks,
	       failures);
	return failures ? 1 : 0;
}
