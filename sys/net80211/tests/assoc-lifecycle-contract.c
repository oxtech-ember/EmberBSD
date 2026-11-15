/* SPDX-License-Identifier: BSD-2-Clause */
/* Common station association lifecycle helper contract. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) abort(); } while (0)

#define IEEE80211_M_STA 1
#define IEEE80211_ASSOC_EVENT_VERSION 1
#define IEEE80211_MLME_VERSION 1
#define IEEE80211_S_INIT 0

struct epoch_lock { int held; };
#define IEEE80211_ASSOC_EPOCH_LOCK(ic) do { \
	CHECK(!(ic)->ic_assoc_epoch_lock.held); \
	(ic)->ic_assoc_epoch_lock.held = 1; \
} while (0)
#define IEEE80211_ASSOC_EPOCH_UNLOCK(ic) do { \
	CHECK((ic)->ic_assoc_epoch_lock.held); \
	(ic)->ic_assoc_epoch_lock.held = 0; \
} while (0)
#define IEEE80211_ADDR_COPY(dst, src) memcpy((dst), (src), 6)

struct ieee80211_node {
	uint64_t ni_assoc_epoch;
	uint8_t ni_bssid[6];
};

struct ieee80211com {
	int ic_opmode;
	uint64_t ic_assoc_epoch;
	struct epoch_lock ic_assoc_epoch_lock;
	struct ieee80211_node *ic_bss;
};

/* ASSOCIATION_EVENT_ABI */

_Static_assert(sizeof(struct ieee80211_assoc_event) == 24,
	"versioned association event ABI");

struct ieee80211req { void *i_data; };
struct ieee80211req_mlme {
	uint32_t im_version;
	uint64_t im_assoc_epoch;
	uint8_t im_macaddr[6];
};

static int copyout_error;
static int new_state_count;

static int
copyout(const void *src, void *dst, size_t len)
{
	if (copyout_error != 0)
		return copyout_error;
	CHECK(src != dst);
	memcpy(dst, src, len);
	return 0;
}

static int
ieee80211_new_state(struct ieee80211com *ic, int state, int arg)
{
	(void)ic;
	(void)state;
	(void)arg;
	new_state_count++;
	return 0;
}

/* LIFECYCLE_HELPERS */

/* IOCTL_RESULT */

/* MLME_VALID */

int
main(void)
{
	struct ieee80211com ic = { .ic_opmode = IEEE80211_M_STA };
	struct ieee80211_node node = { .ni_bssid = { 0x02, 1, 2, 3, 4, 5 } };
	struct ieee80211_assoc_event event, saved;
	struct ieee80211req_mlme mlme = {
		.im_version = IEEE80211_MLME_VERSION
	};
	struct ieee80211req_mlme user_result;
	struct ieee80211req ireq = { .i_data = &user_result };
	struct ieee80211_node newer = { .ni_bssid = { 0x02, 6, 7, 8, 9, 10 } };
	ic.ic_bss = &node;

	CHECK(ieee80211_mlme_request_valid(&mlme));
	mlme.im_version++;
	CHECK(!ieee80211_mlme_request_valid(&mlme));
	mlme.im_version = IEEE80211_MLME_VERSION;
	mlme.im_assoc_epoch = 9;
	CHECK(!ieee80211_mlme_request_valid(&mlme));
	mlme.im_assoc_epoch = 0;

	CHECK(ieee80211_assoc_epoch_assign(&ic, &node) == 0);
	CHECK(ic.ic_assoc_epoch == 1 && node.ni_assoc_epoch == 1);
	CHECK(ieee80211_assoc_event_snapshot(&ic, &node, &event));
	CHECK(event.iae_version == IEEE80211_ASSOC_EVENT_VERSION);
	CHECK(event.iae_reserved == 0 && event.iae_reserved2[0] == 0 &&
	    event.iae_reserved2[1] == 0);
	CHECK(event.iae_assoc_epoch == 1 &&
	    memcmp(event.iae_bssid, "\x02\x01\x02\x03\x04\x05", 6) == 0);
	saved = event;

	/* Leave retires the old epoch; same-BSSID retry receives a new one. */
	ieee80211_assoc_epoch_clear(&ic, &node);
	CHECK(node.ni_assoc_epoch == 0);
	CHECK(ieee80211_assoc_epoch_assign(&ic, &node) == 0);
	CHECK(ic.ic_assoc_epoch == 2 && node.ni_assoc_epoch == 2);
	CHECK(ieee80211_assoc_event_snapshot(&ic, &node, &event));
	CHECK(event.iae_assoc_epoch == 2 && saved.iae_assoc_epoch == 1);
	CHECK(memcmp(event.iae_bssid, saved.iae_bssid, 6) == 0);

	/* The event snapshot remains valid after node cleanup/reuse. */
	memset(&node, 0, sizeof(node));
	CHECK(saved.iae_assoc_epoch == 1 && saved.iae_bssid[0] == 0x02);
	CHECK(!ieee80211_assoc_event_snapshot(&ic, &node, &event));
	CHECK(event.iae_version == 0 && event.iae_assoc_epoch == 0);

	/* Epoch exhaustion is fail-closed and leaves both values untouched. */
	ic.ic_assoc_epoch = UINT64_MAX;
	node.ni_assoc_epoch = 77;
	CHECK(ieee80211_assoc_epoch_assign(&ic, &node) == EOVERFLOW);
	CHECK(ic.ic_assoc_epoch == UINT64_MAX && node.ni_assoc_epoch == 77);

	/* Non-STA node joins do not allocate station identity. */
	ic.ic_opmode = 2;
	ic.ic_assoc_epoch = 9;
	CHECK(ieee80211_assoc_epoch_assign(&ic, &node) == 0);
	CHECK(ic.ic_assoc_epoch == 9 && node.ni_assoc_epoch == 0);
	CHECK(!ieee80211_assoc_event_snapshot(&ic, &node, &event));

	/* An accepted STA ASSOC returns its common selection epoch. */
	ic.ic_opmode = IEEE80211_M_STA;
	CHECK(ieee80211_assoc_epoch_assign(&ic, &node) == 0);
	ic.ic_bss = &node;
	memset(&user_result, 0, sizeof(user_result));
	CHECK(ieee80211_ioctl_assoc_result(&ic, &ireq, &mlme, &node) == 0);
	CHECK(mlme.im_assoc_epoch == node.ni_assoc_epoch);
	CHECK(memcmp(mlme.im_macaddr, node.ni_bssid, 6) == 0);
	CHECK(user_result.im_version == IEEE80211_MLME_VERSION &&
	    user_result.im_assoc_epoch == node.ni_assoc_epoch &&
	    memcmp(user_result.im_macaddr, node.ni_bssid, 6) == 0);

	/* Copyout failure cancels only the still-current selection. */
	copyout_error = EFAULT;
	CHECK(ieee80211_ioctl_assoc_result(&ic, &ireq, &mlme, &node) == EFAULT);
	CHECK(new_state_count == 1 &&
	    user_result.im_assoc_epoch == node.ni_assoc_epoch);

	/* A now-stale node result cannot cancel a newer association. */
	CHECK(ieee80211_assoc_epoch_assign(&ic, &newer) == 0);
	ic.ic_bss = &newer;
	CHECK(ieee80211_ioctl_assoc_result(&ic, &ireq, &mlme, &node) == ESTALE);
	CHECK(new_state_count == 1 &&
	    user_result.im_assoc_epoch == node.ni_assoc_epoch);
	return 0;
}
