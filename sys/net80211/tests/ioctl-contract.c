/* SPDX-License-Identifier: BSD-2-Clause */
/* Kernel services are fixtures. ioctl.py inserts the actual ABI and helpers. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef unsigned long u_long;
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_IS_MULTICAST(p) ((p)[0] & 1)
#define IEEE80211_M_STA 1
#define IEEE80211_S_AUTH 2
#define IEEE80211_ADDR_EQ(a, b) (memcmp(a, b, 6) == 0)
#define SIOCG80211 1
#define SIOCS80211 2
#define KM_SLEEP 0
#define M_TEMP 0
#define M_WAITOK 1
#define M_ZERO 2
#define KAUTH_NETWORK_INTERFACE 0
#define KAUTH_REQ_NETWORK_INTERFACE_SETPRIV 0
/* PRODUCTION_ABI */
struct ieee80211req { int i_len, i_val; void *i_data; };
struct ifnet { void *if_softc; };
struct ieee80211_node {
	uint8_t ni_bssid[6], ni_essid[32], ni_esslen;
	void *ni_chan;
};
struct ieee80211com {
	int ic_opmode, ic_state;
	uint32_t sae_caps;
	struct ifnet *ic_ifp;
	struct ieee80211_node *ic_bss;
	uint8_t ic_myaddr[6];
	int (*ic_sta_mgmt)(struct ieee80211com *, struct ieee80211req_sta_mgmt *);
};
static unsigned checks, allocations, releases, calls, authorizations;
static int auth_error, input_error, output_error, callback_error;
static void *allocation;
static size_t allocation_size;
static const void *user_request;
static void (*input_hook)(void);
static int net_ipl;
#define CHECK(condition) do { checks++; assert(condition); } while (0)

static void *
allocate(size_t n)
{
	CHECK(allocation == NULL);
	allocation = calloc(1, n);
	CHECK(allocation != NULL);
	allocation_size = n;
	allocations++;
	return allocation;
}

static void
release(void *p, size_t n)
{
	CHECK(p == allocation && n == allocation_size);
	for (size_t i = 0; i < n; i++)
		assert(((uint8_t *)p)[i] == 0);
	free(p);
	allocation = NULL;
	releases++;
}

static void
wipe(void *p, int c, size_t n)
{
	volatile uint8_t *out = p;
	while (n-- != 0)
		*out++ = c;
}

static int
copyin(const void *src, void *dst, size_t n)
{
	/* A fault can follow a partial copy of key material. */
	CHECK(net_ipl == 0);
	if (input_hook != NULL)
		input_hook();
	memcpy(dst, src, input_error ? n / 2 : n);
	return input_error;
}

static int
copyout(const void *src, void *dst, size_t n)
{
	if (output_error == 0)
		memcpy(dst, src, n);
	return output_error;
}

static int
authorize(void)
{
	authorizations++;
	return auth_error;
}
#define kauth_cred_get() 0
#define kauth_authorize_network(...) authorize()
#define kmem_zalloc(n, flags) allocate(n)
#define kmem_free(p, n) release(p, n)
#define malloc(n, type, flags) allocate(n)
#define free(p, type) release(p, allocation_size)
#define explicit_memset wipe
/* PRODUCTION_FUNCTIONS */
#undef malloc
#undef free

static int
sae_callback(struct ieee80211com *ic, const struct ieee80211req_sae *req)
{
	CHECK(req == allocation && req != user_request);
	if (req->op == IEEE80211_SAE_CONFIGURE) {
		int error = ieee80211_sae_configure_check(ic, ic->sae_caps,
		    req->data[0] != 0);
		if (error != 0)
			return error;
	}
	calls++;
	return callback_error;
}

static int
sta_callback(struct ieee80211com *ic, struct ieee80211req_sta_mgmt *req)
{
	(void)ic;
	CHECK(req == allocation && req != user_request);
	calls++;
	if (req->op == IEEE80211_STA_GET) {
		CHECK(req->version == IEEE80211_STA_VERSION && req->flags == 0);
		memset(req->data, 0x5a, sizeof(req->data));
		req->flags = IEEE80211_STA_CAP_BTM;
		req->len = sizeof(req->data);
	}
	return callback_error;
}

/* BWFM_KEY */
struct bwfm_softc {
	struct ieee80211com sc_ic;
	bool sc_detaching, sc_sae_enabled;
	uint32_t sc_sae_generation, sc_sae_caps, sc_sae_packet_id, sc_sae_sent;
	uint8_t sc_sae_bssid[6];
};
static unsigned firmware_calls, net_entries;
static struct bwfm_softc *active_sc;
static int splnet(void) { int old = net_ipl; net_ipl++; net_entries++; return old; }
static void splx(int old) { CHECK(net_ipl == old + 1); net_ipl = old; }
static uint16_t le16dec(const void *v) { const uint8_t *p=v; return p[0] | (uint16_t)p[1]<<8; }
static uint32_t le32dec(const void *v) { const uint8_t *p=v; return le16dec(p) | (uint32_t)le16dec(p+2)<<16; }
static void le16enc(void *v, uint16_t n) { uint8_t *p=v; p[0]=n; p[1]=n>>8; }
static void le32enc(void *v, uint32_t n) { uint8_t *p=v; le16enc(p,n); le16enc(p+2,n>>16); }
#ifndef htole32
#define htole32(n) (n)
#define htole16(n) (n)
#endif
static unsigned ieee80211_chan2ieee(struct ieee80211com *ic, void *chan)
{ (void)ic; (void)chan; return 1; }
static int bwfm_fwvar_var_set_data(struct bwfm_softc *sc, const char *name,
    const void *data, size_t size)
{ (void)sc; (void)name; (void)data; (void)size; CHECK(net_ipl == 1); firmware_calls++; return 0; }
/* BWFM_FUNCTIONS */

static void
stop_during_copy(void)
{
	CHECK(net_ipl == 0);
	int old = splnet();
	active_sc->sc_detaching = true;
	splx(old);
}

static void
reconfigure_during_copy(void)
{
	CHECK(net_ipl == 0);
	int old = splnet();
	active_sc->sc_sae_generation++;
	splx(old);
}

static void
drop_caps_during_copy(void)
{
	CHECK(net_ipl == 0);
	int old = splnet();
	active_sc->sc_sae_caps = 0;
	splx(old);
}

static void
raise_caps_during_copy(void)
{
	CHECK(net_ipl == 0);
	int old = splnet();
	active_sc->sc_sae_caps = 3;
	splx(old);
}

static void
mode_during_copy(void)
{
	CHECK(net_ipl == 0);
	int old = splnet();
	active_sc->sc_ic.ic_opmode = 99;
	splx(old);
}

static struct ieee80211req_sae
configure(void)
{
	struct ieee80211req_sae req = {
		.version = IEEE80211_SAE_VERSION, .generation = 1,
		.op = IEEE80211_SAE_CONFIGURE, .len = 1,
		.bssid = { 2, 0, 0, 0, 0, 1 }, .data = { 1 }
	};
	return req;
}

static void
sae_check(struct ieee80211com *ic, struct ieee80211req_sae *req,
    uint32_t caps, int expected, bool invoked)
{
	struct ieee80211req ireq = { .i_len = sizeof(*req), .i_data = req };
	struct ieee80211req_sae before = *req;
	unsigned oldcalls = calls, oldalloc = allocations;

	user_request = req;
	ic->sae_caps = caps;
	CHECK(ieee80211_ioctl_sae(ic, SIOCS80211, &ireq, caps, sae_callback) == expected);
	CHECK(calls == oldcalls + invoked);
	CHECK(allocations == oldalloc + (auth_error == 0));
	CHECK(allocation == NULL && releases == allocations);
	CHECK(memcmp(req, &before, sizeof(*req)) == 0);
}

int
main(void)
{
	struct ieee80211com ic = { .ic_opmode = IEEE80211_M_STA };
	struct ieee80211req_sae req = configure();
	struct ieee80211req ireq = { .i_len = 0, .i_data = &req };
	struct ieee80211req_sta_mgmt sta;
	unsigned before;

	CHECK(ieee80211_ioctl_sae(&ic, SIOCG80211, &ireq, 7, NULL) == EOPNOTSUPP);
	CHECK(allocations == 0 && authorizations == 0);
	CHECK(ieee80211_ioctl_sae(&ic, SIOCG80211, &ireq, 7, sae_callback) == 0);
	CHECK(ireq.i_val == 7 && allocations == 0 && calls == 0);
	CHECK(ieee80211_ioctl_sae(&ic, SIOCG80211, &ireq, 0, sae_callback) == 0);
	CHECK(ireq.i_val == 0);
	ireq.i_len = 1;
	CHECK(ieee80211_ioctl_sae(&ic, SIOCG80211, &ireq, 7, sae_callback) == EINVAL);
	CHECK(ieee80211_ioctl_sae(&ic, 123, &ireq, 7, sae_callback) == EINVAL);
	CHECK(ieee80211_ioctl_sae(&ic, SIOCS80211, &ireq, 7, sae_callback) == EINVAL);
	CHECK(allocations == 0 && calls == 0);
	auth_error = EPERM;
	sae_check(&ic, &req, 7, EPERM, false);
	auth_error = 0;
	input_error = EFAULT;
	memset(req.data + 1, 0xa5, sizeof(req.data) - 1);
	sae_check(&ic, &req, 7, EFAULT, false);
	input_error = 0;
	for (unsigned field = 0; field < 9; field++) {
		req = configure();
		switch (field) {
		case 0: req.version++; break;
		case 1: req.reserved[0] = 1; break;
		case 2: req.reserved[1] = 1; break;
		case 3: req.len = sizeof(req.data) + 1; break;
		case 4: req.len = 0; break;
		case 5: req.data[0] = 2; break;
		case 6: req.generation = 0; break;
		case 7: req.bssid[0] = 1; break;
		case 8: memset(req.bssid, 0, 6); break;
		}
		sae_check(&ic, &req, 7, EINVAL, false);
	}
	req = configure();
	sae_check(&ic, &req, 0, EOPNOTSUPP, false);
	ic.ic_opmode = 99;
	sae_check(&ic, &req, 7, EOPNOTSUPP, false);
	req.data[0] = 0;
	sae_check(&ic, &req, 0, 0, true);
	ic.ic_opmode = IEEE80211_M_STA;
	req = configure();
	sae_check(&ic, &req, 7, 0, true);
	callback_error = EIO;
	sae_check(&ic, &req, 7, EIO, true);
	callback_error = ESTALE;
	req.op = IEEE80211_SAE_SET_IGTK;
	req.len = 24;
	memset(req.data, 0x5a, sizeof(req.data));
	sae_check(&ic, &req, 7, ESTALE, true);
	callback_error = 0;

	ireq.i_len = sizeof(sta); ireq.i_data = &sta;
	CHECK(ieee80211_ioctl_sta_mgmt(&ic, &ireq, true) == EOPNOTSUPP);
	ic.ic_sta_mgmt = sta_callback;
	ireq.i_len--;
	CHECK(ieee80211_ioctl_sta_mgmt(&ic, &ireq, true) == EINVAL);
	ireq.i_len++;
	for (unsigned field = 0; field < 7; field++) {
		memset(&sta, 0, sizeof(sta));
		sta.version = IEEE80211_STA_VERSION;
		sta.op = IEEE80211_STA_ENABLE;
		switch (field) {
		case 0: input_error = EFAULT; break;
		case 1: sta.version++; break;
		case 2: sta.reserved = 1; break;
		case 3: sta.flags = 1; break;
		case 4: sta.len = sizeof(sta.data) + 1; break;
		case 5: sta.op = IEEE80211_STA_GET; break;
		case 6: callback_error = EIO; break;
		}
		memset(sta.data, 0xa5, sizeof(sta.data));
		before = calls;
		user_request = &sta;
		CHECK(ieee80211_ioctl_sta_mgmt(&ic, &ireq, true) ==
		    (field == 0 ? EFAULT : field == 6 ? EIO : EINVAL));
		CHECK(calls == before + (field == 6));
		CHECK(allocation == NULL && releases == allocations);
		input_error = callback_error = 0;
	}
	CHECK(ieee80211_ioctl_sta_mgmt(&ic, &ireq, false) == 0);
	CHECK(sta.flags == IEEE80211_STA_CAP_BTM && sta.data[0] == 0x5a);
	output_error = EFAULT;
	CHECK(ieee80211_ioctl_sta_mgmt(&ic, &ireq, false) == EFAULT);
	CHECK(allocation == NULL && releases == allocations);
	/* Exercise the real chip callback after an interrupted shared copyin. */
	struct bwfm_softc sc = {0};
	struct ifnet ifp = { .if_softc = &sc };
	struct ieee80211_node node = { .ni_bssid = {2, 0, 0, 0, 0, 1} };
	sc.sc_ic.ic_ifp = &ifp;
	sc.sc_ic.ic_bss = &node;
	sc.sc_ic.ic_opmode = IEEE80211_M_STA;
	sc.sc_ic.ic_state = IEEE80211_S_AUTH;
	sc.sc_sae_caps = 3;
	active_sc = &sc;
	req = configure();
	ireq.i_data = &req; ireq.i_len = sizeof(req);
	before = net_entries;
	CHECK(bwfm_sae_ioctl(&sc, SIOCS80211, &ireq) == 0);
	CHECK(net_entries == before + 1 && net_ipl == 0);
	CHECK(sc.sc_sae_enabled && sc.sc_sae_generation == 1);
	sc.sc_sae_enabled = false;
	input_hook = drop_caps_during_copy;
	CHECK(bwfm_sae_ioctl(&sc, SIOCS80211, &ireq) == EOPNOTSUPP);
	CHECK(!sc.sc_sae_enabled && net_ipl == 0);
	input_hook = raise_caps_during_copy;
	CHECK(bwfm_sae_ioctl(&sc, SIOCS80211, &ireq) == 0);
	CHECK(sc.sc_sae_enabled && net_ipl == 0);
	sc.sc_sae_enabled = false;
	input_hook = mode_during_copy;
	CHECK(bwfm_sae_ioctl(&sc, SIOCS80211, &ireq) == EOPNOTSUPP);
	CHECK(!sc.sc_sae_enabled && net_ipl == 0);
	input_hook = NULL;
	sc.sc_ic.ic_opmode = IEEE80211_M_STA;
	CHECK(bwfm_sae_ioctl(&sc, SIOCS80211, &ireq) == 0);
	req.op = IEEE80211_SAE_AUTH_STATUS; req.len = 2; req.data[0] = 0;
	CHECK(bwfm_sae_ioctl(&sc, SIOCS80211, &ireq) == 0);
	CHECK(firmware_calls == 1 && net_ipl == 0);
	input_hook = reconfigure_during_copy;
	CHECK(bwfm_sae_ioctl(&sc, SIOCS80211, &ireq) == ESTALE);
	CHECK(firmware_calls == 1 && net_ipl == 0);
	input_hook = stop_during_copy;
	CHECK(bwfm_sae_ioctl(&sc, SIOCS80211, &ireq) == ENXIO);
	CHECK(firmware_calls == 1 && net_ipl == 0);
	input_hook = NULL;
	sc.sc_detaching = false; sc.sc_ic.ic_bss = NULL;
	CHECK(bwfm_sae_ioctl(&sc, SIOCS80211, &ireq) == ESTALE);
	CHECK(firmware_calls == 1 && releases == allocations && net_ipl == 0);
	printf("PASS: %u common SAE/STA ioctl and bwfm lifetime checks\n", checks);
	return 0;
}
