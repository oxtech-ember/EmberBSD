/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * sta-left.py inserts the production STA_LEFT transition rule and the station
 * scan guard. The model newstate applies the rule as ieee80211_newstate does;
 * the deauthentication/disassociation branch in RUN sets the flag.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u_int8_t;
enum ieee80211_state {
	IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
	IEEE80211_S_ASSOC, IEEE80211_S_RUN
};
#define IEEE80211_CHAN_BYTES 2
#define IEEE80211_M_STA 1
#define IFF_UP 1
#define IS_UP(ic) (((ic)->ic_ifp->if_flags & IFF_UP) != 0)

struct ifnet { int if_flags; };
struct ieee80211_node { int present; };
struct ieee80211com {
	int ic_opmode;
	enum ieee80211_state ic_state;
	uint32_t ic_flags_ext;
	int ic_mgt_timer;
	struct ifnet *ic_ifp;
	struct ieee80211_node *ic_bss;
	u_int8_t ic_chan_active[IEEE80211_CHAN_BYTES];
};

static void ieee80211_sta_leave(struct ieee80211com *, struct ieee80211_node *);
static int ieee80211_new_state(struct ieee80211com *, int, int);

/* PRODUCTION */

static void
ieee80211_sta_leave(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	assert(ic != NULL && ni != NULL);
}

/* Model of ieee80211_newstate: transition, then the production rule. */
static int
ieee80211_new_state(struct ieee80211com *ic, int nstate, int arg)
{
	(void)arg;
	ic->ic_state = nstate;
	ieee80211_sta_left_transition(ic, nstate);
	if (nstate == IEEE80211_S_INIT || nstate == IEEE80211_S_RUN ||
	    nstate == IEEE80211_S_SCAN)
		ic->ic_mgt_timer = 0;
	return 0;
}

/* RUN, then a deauthentication (AUTH) or disassociation (ASSOC) with manual
 * roaming: the production branch leaves the BSS and sets STA_LEFT. */
static void
leave_run(struct ieee80211com *ic, enum ieee80211_state to)
{
	ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	assert((ic->ic_flags_ext & IEEE80211_FEXT_STA_LEFT) == 0);
	ieee80211_new_state(ic, to, 0);
	ic->ic_flags_ext |= IEEE80211_FEXT_STA_LEFT;
}

static int
scan(struct ieee80211com *ic)
{
	const u_int8_t chans[IEEE80211_CHAN_BYTES] = { 1, 2 };

	return ieee80211_startscan(ic, chans);
}

int
main(void)
{
	struct ifnet ifp = { .if_flags = IFF_UP };
	struct ieee80211_node bss = { .present = 1 };
	struct ieee80211com ic = {
		.ic_opmode = IEEE80211_M_STA, .ic_ifp = &ifp, .ic_bss = &bss
	};

	/* Deauthentication, then a second deauthentication in AUTH. */
	leave_run(&ic, IEEE80211_S_AUTH);
	ieee80211_new_state(&ic, IEEE80211_S_AUTH, 0);
	assert(scan(&ic) == 0 && ic.ic_state == IEEE80211_S_SCAN);
	assert((ic.ic_flags_ext & IEEE80211_FEXT_STA_LEFT) == 0);

	/* Disassociation, then a deauthentication and a disassociation. */
	leave_run(&ic, IEEE80211_S_ASSOC);
	ieee80211_new_state(&ic, IEEE80211_S_AUTH, 0);
	ieee80211_new_state(&ic, IEEE80211_S_ASSOC, 0);
	assert(scan(&ic) == 0 && ic.ic_state == IEEE80211_S_SCAN);

	/* A disassociation in AUTH sends ASSOC_REQ: that exchange is busy,
	 * and its timeout to SCAN ends the wait. */
	leave_run(&ic, IEEE80211_S_AUTH);
	ieee80211_new_state(&ic, IEEE80211_S_ASSOC, 0);
	ic.ic_mgt_timer = 1;
	assert(scan(&ic) == EBUSY && ic.ic_state == IEEE80211_S_ASSOC);
	ieee80211_new_state(&ic, IEEE80211_S_SCAN, 0);
	assert((ic.ic_flags_ext & IEEE80211_FEXT_STA_LEFT) == 0);

	/* INIT and RUN end the wait; a normal join stays protected. */
	leave_run(&ic, IEEE80211_S_AUTH);
	ieee80211_new_state(&ic, IEEE80211_S_INIT, -1);
	assert((ic.ic_flags_ext & IEEE80211_FEXT_STA_LEFT) == 0);
	ieee80211_new_state(&ic, IEEE80211_S_AUTH, -1);
	assert(scan(&ic) == EBUSY);
	puts("PASS: STA_LEFT survives AUTH/ASSOC frames and ends at INIT/SCAN/RUN");
	return 0;
}
