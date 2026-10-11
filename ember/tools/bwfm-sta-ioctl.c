/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Derived from the BSD-2-Clause station-management ABI contract.
 * Source revision: 8a281fde1778034f7115e528f9e0a46b9e073696.
 * Check the running EmberBSD station-management ABI without changing its state.
 * Run as root after the matching supplicant has enabled station management.
 * Build with matching EmberBSD headers. Usage: bwfm-sta-ioctl bwfm0
 */
#include <sys/types.h>
#include <sys/endian.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <net/if.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>
#include <errno.h>
#include <grp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fd = -1;
static unsigned checks;
static struct ieee80211req ireq;
static struct ieee80211req_sta_mgmt req, before;

static void
check(bool pass, const char *name)
{

	checks++;
	if (!pass) {
		fprintf(stderr, "FAIL: %s (errno %d)\n", name, errno);
		if (fd != -1)
			close(fd);
		exit(EXIT_FAILURE);
	}
}

static bool
all_zero(const uint8_t *data, size_t len)
{

	while (len-- != 0)
		if (*data++ != 0)
			return false;
	return true;
}

static bool
ies_valid(const uint8_t *ie, size_t len)
{

	while (len != 0) {
		if (len < 2 || ie[1] > len - 2)
			return false;
		len -= ie[1] + 2;
		ie += ie[1] + 2;
	}
	return true;
}

static void
reset(uint16_t op)
{

	memset(&req, 0, sizeof(req));
	req.version = IEEE80211_STA_VERSION;
	req.op = op;
	req.cookie = 1;
	req.generation = before.generation;
	req.freq = before.freq;
	memcpy(req.bssid, before.bssid, sizeof(req.bssid));
	ireq.i_len = sizeof(req);
	ireq.i_data = &req;
}

static void
reject(unsigned long command, int expected, const char *name)
{

	errno = 0;
	check(ioctl(fd, command, &ireq) == -1 && errno == expected, name);
}

static void
child_expect(const char *name, int result, int expected_result,
    int expected_errno)
{
	int saved_errno = result < 0 ? errno : 0;

	if (result == expected_result && saved_errno == expected_errno)
		return;
	dprintf(STDERR_FILENO,
	    "FAIL: authorization child %s: result=%d errno=%d (%s);"
	    " expected result=%d errno=%d; uid=%lu euid=%lu gid=%lu egid=%lu\n",
	    name, result, saved_errno, strerror(saved_errno), expected_result,
	    expected_errno, (unsigned long)getuid(), (unsigned long)geteuid(),
	    (unsigned long)getgid(), (unsigned long)getegid());
	_exit(EXIT_FAILURE);
}

static void
get_info(void)
{
	const uint32_t caps = IEEE80211_STA_CAP_BTM | IEEE80211_STA_CAP_RRM;
	const uint32_t flags = caps | IEEE80211_STA_ASSOCIATED |
	    IEEE80211_STA_ASSOC_IES;
	size_t request, response;

	reset(IEEE80211_STA_GET);
	check(ioctl(fd, SIOCG80211, &ireq) == 0, "GET station information");
	check(req.version == IEEE80211_STA_VERSION && req.op == IEEE80211_STA_GET,
	    "GET ABI version and operation");
	check(req.reserved == 0 && req.cookie == 0, "GET reserved fields");
	check((req.flags & caps) == caps && (req.flags & ~flags) == 0,
	    "GET capabilities and flags");
	check(req.len >= IEEE80211_ADDR_LEN && req.len <= sizeof(req.data),
	    "GET payload bounds");
	check(!all_zero(req.data, IEEE80211_ADDR_LEN) && (req.data[0] & 1) == 0,
	    "GET local unicast address");
	check(all_zero(req.data + req.len, sizeof(req.data) - req.len),
	    "GET unused payload is zero");
	check((req.flags & IEEE80211_STA_ASSOCIATED) != 0,
	    "host-managed association is ready");
	check(req.generation != 0 && req.freq >= 2300 && req.freq <= 6000 &&
	    !all_zero(req.bssid, sizeof(req.bssid)) && (req.bssid[0] & 1) == 0,
	    "GET association epoch, frequency, and peer");
	if ((req.flags & IEEE80211_STA_ASSOC_IES) == 0) {
		check(req.len == IEEE80211_ADDR_LEN, "GET without association IEs");
		return;
	}
	check(req.len >= 10, "GET association IE header bounds");
	request = le16dec(req.data + 6);
	response = le16dec(req.data + 8);
	check(request != 0 && response != 0 &&
	    request + response == req.len - 10U, "GET association IE lengths");
	check(ies_valid(req.data + 10, request) &&
	    ies_valid(req.data + 10 + request, response), "GET association IE chains");
}

int
main(int argc, char **argv)
{
	static const struct {
		uint16_t op;
		const char *name;
	} ops[] = {
		{ IEEE80211_STA_ENABLE, "SIOCS80211 ENABLE" },
		{ IEEE80211_STA_DISABLE, "SIOCS80211 DISABLE" },
		{ IEEE80211_STA_TX, "SIOCS80211 TX" },
		{ IEEE80211_STA_CANCEL_TX, "SIOCS80211 CANCEL_TX" },
		{ IEEE80211_STA_SCAN, "SIOCS80211 SCAN" },
		{ IEEE80211_STA_CANCEL_SCAN, "SIOCS80211 CANCEL_SCAN" }
	};
	int status;
	pid_t child, waited;

	if (argc != 2 || geteuid() != 0) {
		fprintf(stderr, "usage (root, associated): %s interface\n", argv[0]);
		return EXIT_FAILURE;
	}
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	check(fd >= 0, "open ioctl socket");
	check(strlcpy(ireq.i_name, argv[1], sizeof(ireq.i_name)) < sizeof(ireq.i_name),
	    "interface name fits");
	ireq.i_type = IEEE80211_IOC_STA_MGMT;
	get_info();
	before = req;
	printf("STA ABI %u: flags 0x%x, frequency %u MHz, generation %u, IE data %u bytes\n",
	    req.version, req.flags, req.freq, req.generation,
	    (unsigned)(req.len - IEEE80211_ADDR_LEN));

	reset(IEEE80211_STA_GET); ireq.i_len--;
	reject(SIOCG80211, EINVAL, "GET short buffer");
	reset(IEEE80211_STA_GET); ireq.i_len++;
	reject(SIOCG80211, EINVAL, "GET oversized buffer");
	reset(IEEE80211_STA_GET); ireq.i_len = -1;
	reject(SIOCG80211, EINVAL, "GET negative length");
	reset(IEEE80211_STA_TX); ireq.i_len--;
	reject(SIOCS80211, EINVAL, "SET short buffer");
	reset(IEEE80211_STA_TX); ireq.i_len++;
	reject(SIOCS80211, EINVAL, "SET oversized buffer");
	reset(IEEE80211_STA_TX); ireq.i_len = -1;
	reject(SIOCS80211, EINVAL, "SET negative length");
	reset(IEEE80211_STA_TX); req.version++;
	reject(SIOCS80211, EINVAL, "SET unknown ABI version");
	reset(IEEE80211_STA_TX); req.reserved = 1;
	reject(SIOCS80211, EINVAL, "SET reserved bits");
	reset(IEEE80211_STA_TX); req.flags = UINT32_C(0x80000000);
	reject(SIOCS80211, EINVAL, "SET unknown flags");
	reset(IEEE80211_STA_TX); req.len = sizeof(req.data) + 1;
	reject(SIOCS80211, EINVAL, "SET oversized payload");
	reset(IEEE80211_STA_GET);
	reject(SIOCS80211, EINVAL, "SET cannot request GET");
	reset(UINT16_MAX);
	reject(SIOCS80211, EOPNOTSUPP, "SET unknown operation");
	reset(IEEE80211_STA_TX); req.generation ^= UINT32_C(0x80000000);
	reject(SIOCS80211, ESTALE, "SET stale association epoch");
	reset(IEEE80211_STA_TX); req.bssid[5] ^= 1;
	reject(SIOCS80211, ESTALE, "SET foreign peer");
	reset(IEEE80211_STA_TX); req.len = 2;
	reject(SIOCS80211, EINVAL, "TX short action header");
	reset(IEEE80211_STA_TX); req.len = 4; req.data[0] = 10; req.data[1] = 7;
	reject(SIOCS80211, EINVAL, "TX inbound-only BTM request");
	reset(IEEE80211_STA_TX); req.len = 10; req.data[0] = 10; req.data[1] = 8;
	reject(SIOCS80211, EINVAL, "TX accepted BTM response requires target");
	reset(IEEE80211_STA_ENABLE); req.len = 1;
	reject(SIOCS80211, EINVAL, "ENABLE requires empty payload");
	reset(IEEE80211_STA_DISABLE); req.len = 1;
	reject(SIOCS80211, EINVAL, "DISABLE requires empty payload");
	reset(IEEE80211_STA_CANCEL_TX); req.len = 1;
	reject(SIOCS80211, EINVAL, "CANCEL_TX requires empty payload");
	reset(IEEE80211_STA_SCAN); req.len = 1;
	reject(SIOCS80211, EINVAL, "SCAN rejects odd frequency length");
	reset(IEEE80211_STA_SCAN); req.len = 2 * (IEEE80211_STA_MAX_CHANNELS + 1);
	reject(SIOCS80211, EINVAL, "SCAN rejects excess channels");
	reset(IEEE80211_STA_CANCEL_SCAN); req.cookie = 0; req.len = 1;
	reject(SIOCS80211, ESTALE, "CANCEL_SCAN rejects zero cookie");

	child = fork();
	check(child >= 0, "fork authorization check");
	if (child == 0) {
		size_t i;
		gid_t group = 65534;

		child_expect("setgroups", setgroups(1, &group), 0, 0);
		child_expect("setgid", setgid(group), 0, 0);
		child_expect("setuid", setuid(65534), 0, 0);
		child_expect("getuid", (int)getuid(), 65534, 0);
		child_expect("geteuid", (int)geteuid(), 65534, 0);
		child_expect("getgid", (int)getgid(), (int)group, 0);
		child_expect("getegid", (int)getegid(), (int)group, 0);
		for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
			reset(ops[i].op);
			/* Keep each request invalid even if authorization regresses. */
			req.flags = UINT32_C(0x80000000);
			req.len = 1;
			child_expect(ops[i].name, ioctl(fd, SIOCS80211, &ireq),
			    -1, EPERM);
		}
		reset(IEEE80211_STA_GET);
		/* sys/net/if.c gates both 80211 ioctls with SETPRIV authorization. */
		child_expect("SIOCG80211 GET", ioctl(fd, SIOCG80211, &ireq),
		    -1, EPERM);
		_exit(EXIT_SUCCESS);
	}
	do {
		waited = waitpid(child, &status, 0);
	} while (waited == -1 && errno == EINTR);
	check(waited == child, "wait for authorization child");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS)
		fprintf(stderr, "FAIL: authorization child exit=%d signal=%d\n",
		    WIFEXITED(status) ? WEXITSTATUS(status) : -1,
		    WIFSIGNALED(status) ? WTERMSIG(status) : 0);
	errno = 0;
	check(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS,
	    "unprivileged GET and all six mutations return EPERM");
	get_info();
	check(req.flags == before.flags && req.freq == before.freq &&
	    req.generation == before.generation && req.len == before.len &&
	    memcmp(req.bssid, before.bssid, sizeof(req.bssid)) == 0 &&
	    memcmp(req.data, before.data, req.len) == 0,
	    "association and capabilities remain unchanged");
	check(close(fd) == 0, "close ioctl socket");
	fd = -1;
	printf("PASS: %u live STA ioctl checks; all six mutation opcodes denied\n",
	    checks);
	return EXIT_SUCCESS;
}
