/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted).
 * Run unchanged dhcp.c bodies with deterministic OS and I/O fixtures.
 * Model NetBSD kernel DAD. Do not claim DHCP packet or kernel coverage.
 */
#include <arpa/inet.h>
#ifdef NDEBUG
#error This test requires enabled assertions.
#endif
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "states.h"
#include "options.h"

#define ARP 1
#define IPV4LL 1
#define KERNEL_RFC5227 1
#define DHCP_INFINITE_LIFETIME UINT32_MAX
#define DHCP_MIN_LEASE 20
#define T1 0.5
#define T2 0.875
/* Fixture flags only. These do not assert a kernel ABI. */
#define IN_IFF_TENTATIVE 1
#define IN_IFF_DUPLICATED 2
#define IN_IFF_DETACHED 4
#define IN_IFF_NOTUSEABLE 7
#define IFF_POINTOPOINT 1
#define RTM_NEWADDR 1
#define RTM_DELADDR 2
#define DHO_ROUTER 3
#ifndef IN_LINKLOCAL
#define IN_LINKLOCAL(a) (((a) & 0xffff0000U) == 0xa9fe0000U)
#endif
#define D_STATE(ifp) ((ifp)->state)
#ifndef IN_ARE_ADDR_EQUAL
#define IN_ARE_ADDR_EQUAL(a, b) ((a)->s_addr == (b)->s_addr)
#endif

struct interface;
struct bootp { uint32_t yiaddr, ciaddr; };
struct dhcp_lease {
	struct in_addr addr, mask, brd, server;
	uint32_t leasetime, rebindtime, renewaltime;
	bool frominfo;
};
struct ipv4_addr {
	struct interface *iface;
	struct in_addr addr, mask, brd;
	unsigned int addr_flags;
	char saddr[32];
};
struct dhcp_state {
	enum DHS state;
	struct bootp *offer, *new, *old;
	size_t offer_len, new_len, old_len;
	char leasefile[32];
	struct dhcp_lease lease;
	struct ipv4_addr *addr;
	unsigned int added, interval;
	const char *reason;
};
struct if_options {
	unsigned long long options;
	const char *fallback;
	unsigned int fallback_time, ipv4ll_time;
	struct in_addr req_addr;
	uint8_t dstmask[32];
};
struct dhcpcd_ctx { unsigned long long options; void *eloop; };
struct interface {
	struct dhcp_state *state;
	struct if_options *options;
	struct dhcpcd_ctx *ctx;
	unsigned int flags;
	const char *name;
};

static struct dhcp_state fixture_state;
static struct if_options options;
static struct dhcpcd_ctx fixture_ctx;
static struct interface iface;
static struct bootp offer;
static struct ipv4_addr address;
static struct in_addr other;
static unsigned int checks, failures, binds, informs, drops, adds;
static unsigned int duplicates, discoveries, timer_cancels, fallback_starts;
static unsigned int script_calls;
static bool address_exists, fallback_pending, fallback_active, apply_fails;
static unsigned int apply_calls, lease_timers, lease_writes, exits, daemonises;
static int last_exit;

void dhcp_bind(struct interface *);
static void dhcp_inform(const struct interface *);
static void dhcp_start1(void *);
static void dhcp_fallback(void *);
static void ipv4ll_start(void *);
static void dhcp_discover(void *);
static void dhcp_finish_dad(struct interface *, struct in_addr *);
static void dhcp_arp_bind(struct interface *);
static void dhcp_startrenew(void *);
static void dhcp_rebind(void *);
static void dhcp_expire(void *);

static void
check(bool ok, const char *name)
{
	checks++;
	if (!ok) {
		failures++;
		fprintf(stderr, "FAIL: %s\n", name);
	}
}

static void
logdebugx(const char *format, ...)
{
	(void)format;
}

#define loginfox logdebugx
#define logwarnx logdebugx
#define logerr logdebugx

static int
eloop_timeout_delete(void *loop, void (*callback)(void *), const void *arg)
{
	(void)loop;
	assert(arg == &iface);
	assert(callback == NULL);
	timer_cancels++;
	fallback_pending = false;
	return 0;
}

static int
eloop_timeout_add_sec(void *loop, unsigned int seconds,
    void (*callback)(void *), const void *arg)
{
	(void)loop;
	(void)seconds;
	assert(arg == &iface);
	if (callback == ipv4ll_start || callback == dhcp_fallback) {
		fallback_pending = true;
		fallback_starts++;
	} else {
		assert(callback == dhcp_startrenew || callback == dhcp_rebind ||
		    callback == dhcp_expire);
		lease_timers++;
	}
	return 0;
}

static void
dhcp_new_xid(const struct interface *ifp)
{
	assert(ifp == &iface);
}

static void
send_discover(const void *arg)
{
	assert(arg == &iface);
	discoveries++;
}

static void
dhcp_fallback(void *arg)
{
	(void)arg;
	assert(!"fixture must not execute fallback hook");
}

static void
ipv4ll_start(void *arg)
{
	(void)arg;
	assert(!"fixture must not execute IPv4LL I/O");
}

static struct ipv4_addr *
ipv4_iffindaddr(const struct interface *ifp, const struct in_addr *addr,
    const struct in_addr *mask)
{
	assert(ifp == &iface);
	(void)mask;
	return address_exists && addr->s_addr == address.addr.s_addr ?
	    &address : NULL;
}

static void
get_lease(const struct interface *ifp, struct dhcp_lease *lease,
    const struct bootp *message, size_t size)
{
	assert(ifp == &iface && size == sizeof(offer));
	*lease = fixture_state.lease;
	lease->addr.s_addr = message->yiaddr != INADDR_ANY ?
	    message->yiaddr : message->ciaddr;
}

static struct ipv4_addr *
ipv4_addaddr(const struct interface *ifp, const struct in_addr *addr,
    const struct in_addr *mask, const struct in_addr *bcast,
    uint32_t lifetime, uint32_t preferred)
{
	assert(ifp == &iface);
	(void)lifetime;
	(void)preferred;
	adds++;
	address.addr = *addr;
	address.mask = *mask;
	address.brd = *bcast;
	address.addr_flags = IN_IFF_TENTATIVE;
	address_exists = true;
	return &address;
}

static bool
dhcp_addr_duplicated(const struct interface *ifp, const struct in_addr *addr)
{
	assert(ifp == &iface && addr->s_addr == address.addr.s_addr);
	duplicates++;
	return false;
}

static int
inet_ntocidr(struct in_addr mask)
{
	(void)mask;
	return 24;
}

static void
dhcp_startrenew(void *arg)
{
	(void)arg;
	assert(!"fixture must not run a renewal timer");
}

static void
dhcp_rebind(void *arg)
{
	(void)arg;
	assert(!"fixture must not run a rebind timer");
}

static void
dhcp_expire(void *arg)
{
	(void)arg;
	assert(!"fixture must not run an expiry timer");
}

static void
eloop_exit(void *loop, int result)
{
	(void)loop;
	exits++;
	last_exit = result;
}

static int
dhcp_writefile(struct dhcpcd_ctx *context, const char *path, int mode,
    const struct bootp *message, size_t length)
{
	(void)context;
	(void)path;
	(void)mode;
	(void)message;
	(void)length;
	lease_writes++;
	return 0;
}

static struct ipv4_addr *
ipv4_applyaddr(struct interface *ifp)
{
	struct dhcp_state *current = ifp->state;

	apply_calls++;
	if (apply_fails)
		return NULL;
	current->addr = &address;
	current->added = STATE_ADDED;
	return &address;
}

static void
dhcpcd_daemonise(struct dhcpcd_ctx *context)
{
	(void)context;
	daemonises++;
}

static void
dhcp_inform(const struct interface *ifp)
{
	assert(ifp == &iface);
	informs++;
	fixture_state.state = DHS_INFORM;
}

static void
ipv4ll_drop(const struct interface *ifp)
{
	assert(ifp == &iface);
	drops++;
	fallback_active = false;
}

/* The following I/O branches are outside this fixture's DAD contract. */
static void
dhcp_close(struct interface *ifp)
{
	(void)ifp;
	assert(!"unexpected close");
}

static void
dhcp_drop(struct interface *ifp, const char *reason)
{
	(void)ifp;
	(void)reason;
	assert(!"unexpected lease drop");
}

static void
dhcp_start1(void *arg)
{
	(void)arg;
	assert(!"unexpected DHCP restart");
}

static void
dhcp_bound(struct interface *ifp, uint8_t old_state)
{
	(void)ifp;
	(void)old_state;
	binds++;
}

static size_t
dhcp_message_new(struct bootp **message, const struct in_addr *addr,
    const struct in_addr *mask)
{
	(void)message;
	(void)addr;
	(void)mask;
	assert(!"unexpected third-party static address");
	return 0;
}

static bool
has_option_mask(const uint8_t *mask, unsigned int option)
{
	(void)mask;
	(void)option;
	assert(!"unexpected point-to-point address");
	return false;
}

static void
dhcp_message_add_addr(struct bootp *message, unsigned int option,
    struct in_addr addr)
{
	(void)message;
	(void)option;
	(void)addr;
	assert(!"unexpected message creation");
}

static void
rt_build(struct dhcpcd_ctx *context, int af)
{
	(void)context;
	(void)af;
	assert(!"unexpected third-party route update");
}

static void
script_runreason(struct interface *ifp, const char *reason)
{
	(void)ifp;
	(void)reason;
	script_calls++;
}

#include "production.h"

static void
reset(enum DHS initial)
{
	memset(&fixture_state, 0, sizeof(fixture_state));
	memset(&options, 0, sizeof(options));
	memset(&fixture_ctx, 0, sizeof(fixture_ctx));
	memset(&iface, 0, sizeof(iface));
	memset(&offer, 0, sizeof(offer));
	memset(&address, 0, sizeof(address));
	assert(inet_pton(AF_INET, "192.0.2.10", &address.addr) == 1);
	assert(inet_pton(AF_INET, "192.0.2.11", &other) == 1);
	assert(inet_pton(AF_INET, "255.255.255.0", &address.mask) == 1);
	assert(inet_pton(AF_INET, "192.0.2.255", &address.brd) == 1);
	(void)snprintf(address.saddr, sizeof(address.saddr), "192.0.2.10/24");
	address.iface = &iface;
	offer.yiaddr = address.addr.s_addr;
	fixture_state.state = initial;
	fixture_state.offer = &offer;
	fixture_state.offer_len = sizeof(offer);
	fixture_state.lease.addr = address.addr;
	fixture_state.lease.mask = address.mask;
	fixture_state.lease.brd = address.brd;
	fixture_state.lease.leasetime = 3600;
	fixture_state.lease.rebindtime = 3000;
	fixture_state.lease.renewaltime = 1800;
	options.options = DHCPCD_CONFIGURE | DHCPCD_IPV4LL;
	options.ipv4ll_time = 5;
	fixture_ctx.options = DHCPCD_MANAGER | DHCPCD_CONFIGURE;
	iface.state = &fixture_state;
	iface.options = &options;
	iface.ctx = &fixture_ctx;
	iface.name = "test0";
	address_exists = true;
	fallback_pending = true;
	fallback_active = false;
	apply_fails = false;
	apply_calls = lease_timers = lease_writes = exits = daemonises = 0;
	last_exit = -1;
	script_calls = 0;
	binds = informs = drops = adds = duplicates = discoveries = 0;
	timer_cancels = fallback_starts = 0;
}

static void
dad_event(void)
{
	check(dhcp_handleifa(RTM_NEWADDR, &address, 0) == &address,
	    "DAD route event retains address");
}

static void
unconfirmed_tests(void)
{
	const enum DHS states[] = { DHS_REBOOT, DHS_REQUEST };
	size_t i;

	for (i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
		reset(states[i]);
		dad_event();
		check(binds == 0 && fixture_state.state == states[i],
		    "DAD cannot bind a saved lease or unacknowledged offer");
		check(fixture_state.offer == &offer && fixture_state.offer_len == sizeof(offer),
		    "DAD retains the pending DHCP exchange");
		check(drops == 0 && fallback_pending && timer_cancels == 0,
		    "unconfirmed DAD retains DHCP retries and IPv4LL fallback");
		check(fixture_state.state != DHS_BOUND && fixture_state.state != DHS_PROBE,
		    "DAD does not block later ACK or NAK handling");

		/* This is the production entry used after ACK validation.
		 * Wire validation and the NAK path are checked on the board. */
		if (fixture_state.state == states[i]) {
			dhcp_arp_bind(&iface);
			check(binds == 1 && fixture_state.state == DHS_BOUND,
			    "accepted ACK can bind an already usable address");
			check(!fallback_pending && timer_cancels == 1,
			    "accepted ACK cancels the fallback timer");
		}
	}
}

static void
accepted_tests(void)
{
	reset(DHS_REQUEST);
	address_exists = false;
	dhcp_arp_bind(&iface);
	check(fixture_state.state == DHS_PROBE && binds == 0 && adds == 1,
	    "accepted lease waits for kernel DAD on a new address");
	check(!fallback_pending, "accepted lease cancels IPv4LL fallback");
	dad_event();
	check(binds == 0, "tentative address cannot complete DAD");
	address.addr_flags = 0;
	dad_event();
	check(binds == 1 && fixture_state.state == DHS_BOUND && drops == 1,
	    "successful DAD binds an accepted lease and drops IPv4LL");
	dad_event();
	check(binds == 1 && drops == 1, "repeated DAD cannot bind twice");

	reset(DHS_REBOOT);
	address.addr_flags = IN_IFF_DETACHED;
	dhcp_arp_bind(&iface);
	check(fixture_state.state == DHS_PROBE && adds == 0 && binds == 0,
	    "accepted lease waits for DAD on a detached address");
	address.addr_flags = 0;
	dad_event();
	check(binds == 1, "reattached accepted address completes DAD");

	reset(DHS_REQUEST);
	address.addr_flags = IN_IFF_DUPLICATED;
	dhcp_arp_bind(&iface);
	check(duplicates == 1 && binds == 0,
	    "duplicate accepted address cannot bind");
}

static void
fallback_tests(void)
{
	reset(DHS_REBOOT);
	address_exists = false;
	dhcp_lastlease(&iface);
	check(fixture_state.state == DHS_DISCOVER && (fixture_state.added & STATE_EXPIRED),
	    "lastlease continues discovery while DAD is pending");
	check(binds == 0 && discoveries == 1 && adds == 1,
	    "lastlease starts DAD and discovery without premature bind");
	check(fallback_starts == 0,
	    "lastlease does not schedule a second IPv4LL fallback");
	address.addr_flags = 0;
	dad_event();
	check(binds == 1 && drops == 1,
	    "lastlease still binds after DAD completes in DISCOVER");

	reset(DHS_REBOOT);
	dhcp_lastlease(&iface);
	check(binds == 1 && discoveries == 1 && fixture_state.state == DHS_DISCOVER,
	    "lastlease binds a usable address and keeps discovering");
	check((fixture_state.added & STATE_EXPIRED) != 0,
	    "lastlease preserves expired state after bind");

	reset(DHS_INIT);
	dhcp_discover(&iface);
	check(discoveries == 1 && fallback_starts == 1 && fallback_pending,
	    "no DHCP lease still schedules IPv4LL");
}

static void
configuration_tests(void)
{
	reset(DHS_INIT);
	options.options = DHCPCD_CONFIGURE | DHCPCD_STATIC;
	options.req_addr = address.addr;
	address_exists = false;
	dhcp_arp_bind(&iface);
	check(fixture_state.state == DHS_PROBE, "static address starts DAD");
	address.addr_flags = 0;
	dad_event();
	check(binds == 1 && drops == 0, "static address completes DAD");

	reset(DHS_INIT);
	options.options = DHCPCD_CONFIGURE | DHCPCD_INFORM;
	options.req_addr = address.addr;
	address_exists = false;
	check(dhcp_arp_address(&iface) == 0 && fixture_state.state == DHS_PROBE,
	    "INFORM address starts DAD");
	address.addr_flags = 0;
	dad_event();
	check(informs == 1 && binds == 0 && fixture_state.state == DHS_INFORM,
	    "INFORM continues after DAD without binding a DHCP lease");
}

static void
mismatch_tests(void)
{
	reset(DHS_PROBE);
	dhcp_finish_dad(&iface, &other);
	check(binds == 0 && drops == 0, "unrelated address cannot finish DAD");
	fixture_state.offer = NULL;
	dad_event();
	check(binds == 0 && drops == 0, "missing offer cannot finish DAD");

	reset(DHS_BOUND);
	dad_event();
	check(binds == 0 && drops == 0, "bound lease ignores stale DAD event");

	reset(DHS_NONE);
	dad_event();
	check(binds == 0 && drops == 0, "stopped DHCP ignores DAD event");

	reset(DHS_PROBE);
	assert(inet_pton(AF_INET, "169.254.1.2", &address.addr) == 1);
	offer.yiaddr = address.addr.s_addr;
	dad_event();
	check(binds == 1 && drops == 0,
	    "a link-local accepted address does not drop IPv4LL");
}

static void
late_ack_tests(void)
{
	reset(DHS_REBOOT);
	fallback_pending = false;
	fallback_active = true;
	dad_event();
	check(fixture_state.state == DHS_REBOOT && fallback_active,
	    "DAD leaves an active fallback until DHCP confirms the lease");
	dhcp_arp_bind(&iface);
	check(binds == 1 && apply_calls == 1 && !fallback_active && drops == 1,
	    "late ACK drops active IPv4LL after applying a usable DHCP address");
	check(lease_writes == 1 && lease_timers == 3,
	    "late ACK writes the lease and schedules normal lease timers");
	dad_event();
	check(drops == 1, "later DAD event does not repeat IPv4LL cleanup");

	reset(DHS_REQUEST);
	fallback_active = true;
	apply_fails = true;
	dhcp_arp_bind(&iface);
	check(apply_calls == 1 && binds == 0 && fallback_active && drops == 0,
	    "failed DHCP address application preserves active IPv4LL");

	reset(DHS_PROBE);
	fallback_active = true;
	apply_fails = true;
	dad_event();
	check(apply_calls == 1 && binds == 0 && fallback_active && drops == 0,
	    "DAD completion cannot drop IPv4LL when address application fails");

	reset(DHS_REQUEST);
	fixture_ctx.options |= DHCPCD_ONESHOT;
	fallback_active = true;
	apply_fails = true;
	dhcp_arp_bind(&iface);
	check(exits == 1 && last_exit == EXIT_FAILURE && fallback_active,
	    "oneshot application failure keeps fallback and reports failure");

	reset(DHS_REQUEST);
	options.options &= ~DHCPCD_CONFIGURE;
	fallback_active = true;
	dhcp_arp_bind(&iface);
	check(apply_calls == 0 && binds == 1 && fallback_active && drops == 0,
	    "no-config mode does not remove an active IPv4LL address");
	check(script_calls == 1 && daemonises == 1,
	    "no-config mode retains its notification path");

	reset(DHS_REQUEST);
	options.options &= ~DHCPCD_CONFIGURE;
	fallback_active = true;
	address_exists = false;
	dhcp_bind(&iface);
	check(apply_calls == 0 && binds == 0 && fallback_active && drops == 0,
	    "no-config mode without a usable address preserves fallback");

	reset(DHS_REQUEST);
	fallback_active = true;
	assert(inet_pton(AF_INET, "169.254.1.2", &address.addr) == 1);
	offer.yiaddr = address.addr.s_addr;
	dhcp_arp_bind(&iface);
	check(binds == 1 && fallback_active && drops == 0,
	    "binding a link-local address does not drop IPv4LL");

	reset(DHS_REQUEST);
	options.options &= ~DHCPCD_IPV4LL;
	fallback_active = true;
	dhcp_arp_bind(&iface);
	check(binds == 1 && fallback_active && drops == 0,
	    "binding does not manage IPv4LL when its option is disabled");

	reset(DHS_INFORM);
	options.options |= DHCPCD_INFORM;
	fallback_active = true;
	dhcp_arp_bind(&iface);
	check(binds == 1 && !fallback_active && drops == 1,
	    "INFORM approval removes fallback after successful application");
	check(strcmp(fixture_state.reason, "INFORM") == 0 && lease_writes == 0,
	    "INFORM approval retains its reason and does not write a lease");

	reset(DHS_PROBE);
	options.options |= DHCPCD_INFORM;
	options.req_addr = address.addr;
	fallback_active = true;
	dad_event();
	check(informs == 1 && binds == 0 && drops == 1 && !fallback_active,
	    "initial INFORM DAD retains its existing IPv4LL cleanup");

	reset(DHS_REQUEST);
	fixture_ctx.options |= DHCPCD_TEST;
	fallback_active = true;
	dhcp_arp_bind(&iface);
	check(exits == 1 && last_exit == EXIT_SUCCESS && apply_calls == 0 &&
	    binds == 0 && fallback_active && drops == 0,
	    "test mode exits without changing addresses or fallback");
}

int
main(void)
{
	unconfirmed_tests();
	accepted_tests();
	fallback_tests();
	configuration_tests();
	mismatch_tests();
	late_ack_tests();
	printf("DHCP DAD: %u checks, %u failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
