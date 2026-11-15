/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the production MLME request/result path and reference lifetime. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) abort(); } while (0)

typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_NWID_LEN 32
#define IEEE80211_M_STA 1
#define IEEE80211_M_HOSTAP 2
#define IEEE80211_S_INIT 0
#define IEEE80211_ASSOC_EVENT_VERSION 1
#define IEEE80211_MLME_VERSION 1
#define IEEE80211_NO_HOSTAP 1

struct epoch_lock { int held; };
#define IEEE80211_ASSOC_EPOCH_LOCK(ic) do { \
	CHECK(!(ic)->lock.held); \
	(ic)->lock.held = 1; \
} while (0)
#define IEEE80211_ASSOC_EPOCH_UNLOCK(ic) do { \
	CHECK((ic)->lock.held); \
	(ic)->lock.held = 0; \
} while (0)
#define IEEE80211_ADDR_COPY(dst, src) memcpy((dst), (src), 6)

struct ieee80211_node {
	uint64_t ni_assoc_epoch;
	uint8_t ni_bssid[6];
	uint8_t ni_macaddr[6];
	int refs;
};

struct ieee80211com {
	int ic_opmode;
	uint64_t ic_assoc_epoch;
	struct epoch_lock lock;
	struct ieee80211_node *ic_bss;
	int ic_scan;
	int ic_sta;
};

struct ieee80211_assoc_event {
	uint32_t iae_version;
	uint32_t iae_reserved;
	uint64_t iae_assoc_epoch;
	uint8_t iae_bssid[6];
	uint8_t iae_reserved2[2];
};

struct ieee80211req { size_t i_len; void *i_data; };

/* MLME_ABI */
_Static_assert(sizeof(struct ieee80211req_mlme) == 56,
	"MLME request ABI layout");

struct ieee80211_node *ieee80211_find_node(void *, const uint8_t *);
struct ieee80211_node *ieee80211_find_node_with_ssid(void *,
	const uint8_t *, uint8_t, const uint8_t *);
struct ieee80211_node *ieee80211_ref_node(struct ieee80211_node *);
void ieee80211_free_node(struct ieee80211_node *);
int ieee80211_sta_join(struct ieee80211com *, struct ieee80211_node *);
int ieee80211_new_state(struct ieee80211com *, int, int);
void ieee80211_node_authorize(struct ieee80211_node *);
void ieee80211_node_unauthorize(struct ieee80211_node *);
int copyin(const void *, void *, size_t);
int copyout(const void *, void *, size_t);

static struct ieee80211com *active_ic;
static struct ieee80211_node *candidate;
static struct ieee80211_node *replacement;
static int replace_during_join;
static int copyout_mode;
static int new_state_count;

#define COPYOUT_OK 0
#define COPYOUT_FAIL 1
#define COPYOUT_REPLACE 2

/* NODE_HELPERS */

/* IOCTL_HELPERS */

int
copyin(const void *src, void *dst, size_t len)
{
	memcpy(dst, src, len);
	return 0;
}

int
copyout(const void *src, void *dst, size_t len)
{
	if (copyout_mode == COPYOUT_REPLACE) {
		struct ieee80211_node *old = active_ic->ic_bss;
		active_ic->ic_bss = replacement;
		CHECK(ieee80211_assoc_epoch_assign(active_ic, replacement) == 0);
		ieee80211_assoc_epoch_clear(active_ic, old);
		ieee80211_free_node(old);
		return EFAULT;
	}
	if (copyout_mode == COPYOUT_FAIL)
		return EFAULT;
	memcpy(dst, src, len);
	return 0;
}

struct ieee80211_node *
ieee80211_find_node(void *table, const uint8_t *addr)
{
	(void)table;
	(void)addr;
	candidate->refs++;
	return candidate;
}

struct ieee80211_node *
ieee80211_find_node_with_ssid(void *table, const uint8_t *addr,
	uint8_t len, const uint8_t *ssid)
{
	(void)table;
	(void)addr;
	(void)len;
	(void)ssid;
	candidate->refs++;
	return candidate;
}

struct ieee80211_node *
ieee80211_ref_node(struct ieee80211_node *ni)
{
	ni->refs++;
	return ni;
}

void
ieee80211_free_node(struct ieee80211_node *ni)
{
	CHECK(ni->refs > 0);
	ni->refs--;
}

int
ieee80211_sta_join(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	if (ieee80211_assoc_epoch_assign(ic, ni) != 0)
		return 0;
	ic->ic_bss = ni;
	if (replace_during_join) {
		ic->ic_bss = replacement;
		CHECK(ieee80211_assoc_epoch_assign(ic, replacement) == 0);
		ieee80211_assoc_epoch_clear(ic, ni);
		ieee80211_free_node(ni); /* callback releases the former BSS ref */
	}
	return 1;
}

int
ieee80211_new_state(struct ieee80211com *ic, int state, int arg)
{
	(void)state;
	(void)arg;
	new_state_count++;
	if (ic->ic_bss != NULL) {
		struct ieee80211_node *old = ic->ic_bss;
		ic->ic_bss = NULL;
		ieee80211_assoc_epoch_clear(ic, old);
		ieee80211_free_node(old);
	}
	return 0;
}

void ieee80211_node_authorize(struct ieee80211_node *ni) { (void)ni; }
void ieee80211_node_unauthorize(struct ieee80211_node *ni) { (void)ni; }

static void
reset_case(struct ieee80211com *ic, struct ieee80211_node *ni,
	struct ieee80211_node *next, struct ieee80211req_mlme *mlme,
	struct ieee80211req *req)
{
	memset(ic, 0, sizeof(*ic));
	memset(ni, 0, sizeof(*ni));
	memset(next, 0, sizeof(*next));
	ni->ni_bssid[0] = 0x02;
	ni->ni_bssid[5] = 1;
	memcpy(ni->ni_macaddr, ni->ni_bssid, 6);
	next->ni_bssid[0] = 0x02;
	next->ni_bssid[5] = 2;
	memcpy(next->ni_macaddr, next->ni_bssid, 6);
	ic->ic_opmode = IEEE80211_M_STA;
	active_ic = ic;
	candidate = ni;
	replacement = next;
	replace_during_join = 0;
	copyout_mode = COPYOUT_OK;
	*mlme = (struct ieee80211req_mlme){
		.im_op = IEEE80211_MLME_ASSOC,
		.im_version = IEEE80211_MLME_VERSION,
	};
	memcpy(mlme->im_macaddr, ni->ni_bssid, 6);
	*req = (struct ieee80211req){ sizeof(*mlme), mlme };
}

int
main(void)
{
	struct ieee80211com ic;
	struct ieee80211_node node, newer;
	struct ieee80211req_mlme mlme;
	struct ieee80211req req;
	int error;

	reset_case(&ic, &node, &newer, &mlme, &req);
	CHECK(ieee80211_ioctl_setmlme(&ic, &req) == 0);
	CHECK(ic.ic_bss == &node && node.refs == 1);
	CHECK(mlme.im_assoc_epoch == node.ni_assoc_epoch &&
	    memcmp(mlme.im_macaddr, node.ni_bssid, 6) == 0);

	/* Synchronous newstate replacement is safe across the local reference. */
	reset_case(&ic, &node, &newer, &mlme, &req);
	replace_during_join = 1;
	error = ieee80211_ioctl_setmlme(&ic, &req);
	CHECK(error == ESTALE && ic.ic_bss == &newer);
	CHECK(node.refs == 0 && newer.refs == 0);

	/* Replacement during copyout failure must not INIT the newer selection. */
	reset_case(&ic, &node, &newer, &mlme, &req);
	copyout_mode = COPYOUT_REPLACE;
	error = ieee80211_ioctl_setmlme(&ic, &req);
	CHECK(error == EFAULT && ic.ic_bss == &newer);
	CHECK(new_state_count == 0 && node.refs == 0 && newer.refs == 0);

	/* A copyout failure for the still-current selection cancels it. */
	reset_case(&ic, &node, &newer, &mlme, &req);
	copyout_mode = COPYOUT_FAIL;
	error = ieee80211_ioctl_setmlme(&ic, &req);
	CHECK(error == EFAULT && ic.ic_bss == NULL);
	CHECK(new_state_count == 1 && node.refs == 0);

	/* Version and caller-supplied result values are validated before lookup. */
	reset_case(&ic, &node, &newer, &mlme, &req);
	mlme.im_version++;
	CHECK(ieee80211_ioctl_setmlme(&ic, &req) == EINVAL && node.refs == 0);
	reset_case(&ic, &node, &newer, &mlme, &req);
	mlme.im_assoc_epoch = 5;
	CHECK(ieee80211_ioctl_setmlme(&ic, &req) == EINVAL && node.refs == 0);
	active_ic = NULL;
	candidate = NULL;
	replacement = NULL;
	return 0;
}
