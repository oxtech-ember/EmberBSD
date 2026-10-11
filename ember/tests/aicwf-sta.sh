#!/bin/sh
# Host-managed k/v exchange of the production aicwf driver: ABI decisions,
# action validity, RX event admission and generation discipline.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-sta.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '
/^aicwf_sta_action_valid\(/ {copy=1; print "static bool"}
/^aicwf_sta_rx_event\(/ {copy=1; print "static void"}
copy {print} copy && /^}/ {copy=0}
' "$src/sys/dev/sdmmc/if_aicwf.c" > "$work/sta.h"
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#define KM_SLEEP 0
#define IEEE80211_STA_VERSION 2
#define IEEE80211_STA_GET 0
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_ADDR_EQ(a, b) (memcmp((a), (b), 6) == 0)
#define IEEE80211_ADDR_COPY(d, s) memcpy((d), (s), 6)
struct ifnet;
#define RTM_IEEE80211_STA_RX 112
static struct {
	struct ifnet *ifp; int code; size_t len;
	uint8_t bssid[6]; uint32_t generation, freq; uint16_t op;
} lastmsg;
static void *kmem_zalloc(size_t n, int flags) { return calloc(1, n); }
static void kmem_free(void *p, size_t n) { free(p); }
struct ieee80211req_sta_mgmt {
	uint16_t version; uint16_t op; uint32_t cookie;
	uint32_t generation; uint32_t flags; uint16_t freq; uint16_t len;
	uint8_t bssid[6]; uint16_t reserved; uint8_t data[1024];
};
#define offsetof(s, m) __builtin_offsetof(s, m)
static void rt_ieee80211msg(struct ifnet *ifp, int code, void *data, size_t n)
{
	const struct ieee80211req_sta_mgmt *ev = data;
	(void)n;
	lastmsg.ifp = ifp; lastmsg.code = code;
	lastmsg.generation = ev->generation; lastmsg.freq = ev->freq;
	lastmsg.op = ev->op; lastmsg.len = ev->len;
	memcpy(lastmsg.bssid, ev->bssid, 6);
}
struct ieee80211_chan { uint16_t ic_freq; };
struct ieee80211_node {
	uint8_t ni_bssid[6];
	struct ieee80211_chan *ni_chan;
};
struct ieee80211com {
	int ic_state;
	uint8_t ic_myaddr[6];
	struct ieee80211_node *ic_bss;
};
struct ifnet { int dummy; };
struct aicwf_softc {
	struct ieee80211com sc_ic;
	struct ifnet sc_if;
	bool sc_sta_enabled, sc_connected;
	uint32_t sc_sta_generation;
};
static struct aicwf_softc sc;
static struct ieee80211_node ni;
static struct ieee80211_chan chan;
#include "sta.h"
int
main(void)
{
	unsigned checks = 0;
	uint8_t btm_req[8] = {0x0a, 5, 1, 0, 0, 0, 0, 0};
	uint8_t btm_query[4] = {0x0a, 6, 1, 0};
	uint8_t btm_resp[5] = {0x0a, 7, 1, 0, 0};
	uint8_t nr_req[3] = {0x05, 5, 1};
	uint8_t nr_resp[3] = {0x05, 6, 1};
	uint8_t bad[3] = {0x0a, 9, 1};
	uint8_t wnm_short[2] = {0x0a, 5};
	uint8_t sa_query[4] = {0x08, 0, 0, 0};

	/* To-AP bodies: queries, responses and requests; nothing else. */
	assert(aicwf_sta_action_valid(btm_req, 8, false));
	assert(!aicwf_sta_action_valid(wnm_short, 2, false));
	assert(aicwf_sta_action_valid(btm_query, 4, true));
	assert(aicwf_sta_action_valid(btm_resp, 5, true));
	assert(aicwf_sta_action_valid(nr_req, 3, true));
	assert(aicwf_sta_action_valid(nr_resp, 3, false));
	assert(!aicwf_sta_action_valid(bad, 3, true));
	assert(!aicwf_sta_action_valid(sa_query, 4, true));
	assert(!aicwf_sta_action_valid(nr_req, 1, true));
	checks += 9;

	/* RX events carry the association generation, peer and frequency. */
	memset(&sc, 0, sizeof(sc));
	sc.sc_sta_enabled = true; sc.sc_connected = true;
	sc.sc_ic.ic_state = 3 /* RUN */;
	chan.ic_freq = 5300; ni.ni_chan = &chan;
	memset(ni.ni_bssid, 0x5a, 6);
	sc.sc_ic.ic_bss = &ni;
	sc.sc_sta_generation = 7;
	aicwf_sta_rx_event(&sc, btm_req, sizeof(btm_req));
	assert(lastmsg.code == RTM_IEEE80211_STA_RX);
	assert(lastmsg.generation == 7 && lastmsg.freq == 5300);
	assert(IEEE80211_ADDR_EQ(lastmsg.bssid, ni.ni_bssid));
	assert(lastmsg.len == sizeof(btm_req));
	checks += 4;

	printf("PASS: %u aicwf station k/v checks\n", checks);
	return 0;
}
C
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${AICWF_STA_CFLAGS:-} -I"$work" "$work/check.c" -o "$work/check"
"$work/check"
