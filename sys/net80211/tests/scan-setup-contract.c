/* SPDX-License-Identifier: BSD-2-Clause */
/* ioctl.py inserts the production ieee80211_setupscan implementation. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u_int8_t;
#define IEEE80211_CHAN_BYTES 2
#define IEEE80211_M_STA 1
#define IEEE80211_S_INIT 0
#define IEEE80211_S_SCAN 1
#define IEEE80211_S_AUTH 2
#define IEEE80211_S_ASSOC 3
#define IEEE80211_S_RUN 4
#define IFF_UP 1
#define IS_UP(ic) (((ic)->ic_ifp->if_flags & IFF_UP) != 0)

struct ifnet {
	int if_flags;
};

struct ieee80211_node {
	int present;
};

struct ieee80211com {
	int ic_opmode;
	int ic_state;
	struct ifnet *ic_ifp;
	struct ieee80211_node *ic_bss;
	u_int8_t ic_chan_active[IEEE80211_CHAN_BYTES];
};

static unsigned leave_calls, newstate_calls;
static int newstate_error;

static int
ieee80211_new_state(struct ieee80211com *ic, int state, int arg)
{
	assert(arg == -1);
	newstate_calls++;
	if (newstate_error != 0)
		return newstate_error;
	ic->ic_state = state;
	return 0;
}

static void
ieee80211_sta_leave(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	assert(ic != NULL && ni != NULL && ni->present);
	leave_calls++;
}

/* SETUPSCAN */

int
main(void)
{
	struct ifnet ifp = { .if_flags = IFF_UP };
	struct ieee80211_node bss = { .present = 1 };
	struct ieee80211com ic = {
		.ic_opmode = IEEE80211_M_STA,
		.ic_state = IEEE80211_S_AUTH,
		.ic_ifp = &ifp,
		.ic_bss = &bss,
		.ic_chan_active = { 0x11, 0x22 }
	};
	const u_int8_t requested[IEEE80211_CHAN_BYTES] = { 0x33, 0x44 };
	const u_int8_t original[IEEE80211_CHAN_BYTES] = { 0x11, 0x22 };

	for (int state = IEEE80211_S_AUTH; state <= IEEE80211_S_ASSOC; state++) {
		ic.ic_state = state;
		assert(ieee80211_startscan(&ic, requested) == EBUSY);
		assert(ic.ic_state == state);
		assert(memcmp(ic.ic_chan_active, original,
		    sizeof(ic.ic_chan_active)) == 0);
		assert(leave_calls == 0 && newstate_calls == 0);
	}

	ic.ic_state = IEEE80211_S_INIT;
	assert(ieee80211_startscan(&ic, requested) == 0);
	assert(ic.ic_state == IEEE80211_S_SCAN && newstate_calls == 1);
	assert(memcmp(ic.ic_chan_active, requested,
	    sizeof(ic.ic_chan_active)) == 0);
	assert(leave_calls == 0);

	memcpy(ic.ic_chan_active, original, sizeof(original));
	ic.ic_state = IEEE80211_S_SCAN;
	assert(ieee80211_startscan(&ic, requested) == 0);
	assert(ic.ic_state == IEEE80211_S_SCAN && newstate_calls == 2);
	assert(leave_calls == 0);

	memcpy(ic.ic_chan_active, original, sizeof(original));
	ic.ic_state = IEEE80211_S_RUN;
	assert(ieee80211_startscan(&ic, requested) == 0);
	assert(ic.ic_state == IEEE80211_S_SCAN && newstate_calls == 3);
	assert(leave_calls == 1);

	/* Channel-list configuration copies channels but is not a scan. */
	memcpy(ic.ic_chan_active, original, sizeof(original));
	ic.ic_state = IEEE80211_S_AUTH;
	assert(ieee80211_cfgset_channel_list(&ic, requested) == ENETRESET);
	assert(ic.ic_state == IEEE80211_S_AUTH);
	assert(memcmp(ic.ic_chan_active, requested,
	    sizeof(ic.ic_chan_active)) == 0);
	assert(leave_calls == 1 && newstate_calls == 3);

	return 0;
}
