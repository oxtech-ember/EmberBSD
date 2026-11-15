/* SPDX-License-Identifier: BSD-2-Clause */
/* Send one caller-supplied STA Action body using the common STA ABI. */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <net/if.h>
#include <net/route.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_netbsd.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void
write_all(int fd, const char *buf, size_t len)
{
	while (len != 0) {
		ssize_t n = write(fd, buf, len);
		if (n == -1 && errno == EINTR)
			continue;
		if (n <= 0)
			return;
		buf += n;
		len -= (size_t)n;
	}
}

static void
report_error(const char *what)
{
	char digits[3 * sizeof(errno) + 1];
	unsigned int value = (unsigned int)errno;
	size_t n = 0;

	do {
		digits[n++] = (char)('0' + value % 10);
		value /= 10;
	} while (value != 0);
	write_all(STDERR_FILENO, what, strlen(what));
	write_all(STDERR_FILENO, ": errno ", 8);
	while (n != 0)
		write_all(STDERR_FILENO, &digits[--n], 1);
	write_all(STDERR_FILENO, "\n", 1);
}

static int
hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int
parse_body(const char *hex, uint8_t *body, size_t *len)
{
	size_t n = strlen(hex);

	if ((n & 1) != 0 || n < 6 || n / 2 > IEEE80211_STA_MAX_BODY)
		return -1;
	for (size_t i = 0; i < n / 2; i++) {
		int hi = hexval(hex[2 * i]);
		int lo = hexval(hex[2 * i + 1]);
		if (hi < 0 || lo < 0)
			return -1;
		body[i] = (uint8_t)((hi << 4) | lo);
	}
	*len = n / 2;
	return 0;
}

int
main(int argc, char **argv)
{
	struct ieee80211req ireq;
	struct ieee80211req_sta_mgmt req, association, event;
	union {
		struct if_announcemsghdr hdr;
		uint8_t data[sizeof(struct if_announcemsghdr) +
		    sizeof(struct ieee80211req_sta_mgmt)];
	} msg;
	uint8_t body[IEEE80211_STA_MAX_BODY];
	size_t body_len;
	int fd, route, selected;
	struct ifreq ifr;
	unsigned ifindex;
	struct timespec deadline, now;

	if (argc != 3 || geteuid() != 0 || parse_body(argv[2], body,
	    &body_len) != 0) {
		write_all(STDERR_FILENO,
		    "usage (root): wifi-sta-action-once interface action-body-hex\n",
		    sizeof("usage (root): wifi-sta-action-once interface action-body-hex\n") - 1);
		return EXIT_FAILURE;
	}
	if (strlen(argv[1]) >= sizeof(ireq.i_name)) {
		write_all(STDERR_FILENO, "invalid interface\n", 18);
		return EXIT_FAILURE;
	}
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) {
		report_error("socket");
		return EXIT_FAILURE;
	}
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, argv[1], sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFINDEX, &ifr) == -1) {
		report_error("SIOCGIFINDEX");
		return EXIT_FAILURE;
	}
	if (ifr.ifr_index <= 0) {
		errno = ENXIO;
		report_error("SIOCGIFINDEX");
		return EXIT_FAILURE;
	}
	ifindex = (unsigned)ifr.ifr_index;
	route = socket(PF_ROUTE, SOCK_RAW, 0);
	if (route == -1) {
		report_error("route socket");
		return EXIT_FAILURE;
	}
	memset(&ireq, 0, sizeof(ireq));
	memset(&req, 0, sizeof(req));
	strlcpy(ireq.i_name, argv[1], sizeof(ireq.i_name));
	ireq.i_type = IEEE80211_IOC_STA_MGMT;
	ireq.i_len = sizeof(req);
	ireq.i_data = &req;
	req.version = IEEE80211_STA_VERSION;
	req.op = IEEE80211_STA_GET;
	if (ioctl(fd, SIOCG80211, &ireq) == -1) {
		report_error("SIOCG80211 STA_GET");
		return EXIT_FAILURE;
	}
	if (req.version != IEEE80211_STA_VERSION || req.op != IEEE80211_STA_GET ||
	    (req.flags & IEEE80211_STA_ASSOCIATED) == 0 || req.generation == 0 ||
	    req.freq == 0) {
		write_all(STDERR_FILENO, "STA association is not ready\n", 29);
		return EXIT_FAILURE;
	}
	association = req;
	memset(&req, 0, sizeof(req));
	req.version = IEEE80211_STA_VERSION;
	req.op = IEEE80211_STA_ENABLE;
	if (ioctl(fd, SIOCS80211, &ireq) == -1) {
		report_error("SIOCS80211 STA_ENABLE");
		return EXIT_FAILURE;
	}
	memset(&req, 0, sizeof(req));
	req.version = IEEE80211_STA_VERSION;
	req.op = IEEE80211_STA_TX;
	req.cookie = arc4random();
	if (req.cookie == 0)
		req.cookie = 1;
	req.generation = association.generation;
	req.freq = association.freq;
	memcpy(req.bssid, association.bssid, sizeof(req.bssid));
	req.len = body_len;
	memcpy(req.data, body, body_len);
	if (clock_gettime(CLOCK_MONOTONIC, &deadline) == -1) {
		report_error("clock_gettime");
		return EXIT_FAILURE;
	}
	deadline.tv_sec += 30;
	if (ioctl(fd, SIOCS80211, &ireq) == -1) {
		report_error("SIOCS80211 STA_TX");
		return EXIT_FAILURE;
	}
	for (;;) {
		fd_set fds;
		struct timeval timeout;
		ssize_t n;
		size_t header_len = sizeof(msg.hdr) +
		    offsetof(struct ieee80211req_sta_mgmt, data);

		if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
			report_error("clock_gettime");
			return EXIT_FAILURE;
		}
		if (now.tv_sec > deadline.tv_sec ||
		    (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
			errno = ETIMEDOUT;
			report_error("STA_TX completion");
			return EXIT_FAILURE;
		}
		timeout.tv_sec = deadline.tv_sec - now.tv_sec;
		timeout.tv_usec = (deadline.tv_nsec - now.tv_nsec) / 1000;
		if (timeout.tv_usec < 0) {
			timeout.tv_sec--;
			timeout.tv_usec += 1000000;
		}
		FD_ZERO(&fds);
		FD_SET(route, &fds);
		selected = select(route + 1, &fds, NULL, NULL, &timeout);
		if (selected == -1) {
			if (errno == EINTR)
				continue;
			report_error("select");
			return EXIT_FAILURE;
		}
		if (selected == 0)
			continue;
		n = read(route, msg.data, sizeof(msg.data));
		if (n == -1) {
			if (errno == EINTR)
				continue;
			report_error("read route socket");
			return EXIT_FAILURE;
		}
		if (n == 0) {
			errno = EIO;
			report_error("read route socket");
			return EXIT_FAILURE;
		}
		if (n < (ssize_t)header_len)
			continue;
		if (msg.hdr.ifan_version != RTM_VERSION ||
		    msg.hdr.ifan_type != RTM_IEEE80211 ||
		    msg.hdr.ifan_index != ifindex ||
		    msg.hdr.ifan_what != RTM_IEEE80211_STA_TX ||
		    msg.hdr.ifan_msglen < header_len || msg.hdr.ifan_msglen > n)
			continue;
		memset(&event, 0, sizeof(event));
		memcpy(&event, msg.data + sizeof(msg.hdr),
		    offsetof(struct ieee80211req_sta_mgmt, data));
		if (event.version != IEEE80211_STA_VERSION ||
		    event.op != IEEE80211_STA_TX || event.cookie != req.cookie ||
		    event.generation != req.generation || event.len != 0 ||
		    memcmp(event.bssid, req.bssid, sizeof(req.bssid)) != 0)
			continue;
		if ((event.flags & IEEE80211_STA_TX_ACK) != 0)
			write_all(STDOUT_FILENO, "TX completion observed; ACK reported\n", 37);
		else
			write_all(STDOUT_FILENO,
			    "TX completion observed; peer ACK unconfirmed\n", 45);
		close(route);
		close(fd);
		return EXIT_SUCCESS;
	}
}
