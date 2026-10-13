/* SPDX-License-Identifier: BSD-2-Clause */
/* Start this probe before reassociating a station; it never changes a profile. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_netbsd.h>
#include <ifaddrs.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static time_t
seconds(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return ts.tv_sec;
}

int
main(int argc, char **argv)
{
	struct ieee80211req ireq;
	struct ieee80211req_sae tx;
	struct ifaddrs *addrs, *ifa;
	uint8_t self[6] = { 0 }, token[2];
	union {
		struct rt_msghdr align;
		uint8_t data[8192];
	} buf;
	unsigned index, attempts = 0;
	int fd, route;
	time_t deadline, next = 0;
	bool captured = false, sent = false;

	if (argc != 2 || geteuid() != 0) {
		fprintf(stderr, "usage (root): %s interface\n", argv[0]);
		return EXIT_FAILURE;
	}
	index = if_nametoindex(argv[1]);
	if (index == 0 || getifaddrs(&addrs) == -1)
		return EXIT_FAILURE;
	for (ifa = addrs; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr != NULL &&
		    ifa->ifa_addr->sa_family == AF_LINK &&
		    strcmp(ifa->ifa_name, argv[1]) == 0) {
			const struct sockaddr_dl *dl = (const void *)ifa->ifa_addr;

			if (dl->sdl_alen == sizeof(self))
				memcpy(self, LLADDR(dl), sizeof(self));
		}
	}
	freeifaddrs(addrs);
	if ((self[0] | self[1] | self[2] | self[3] | self[4] | self[5]) == 0)
		return EXIT_FAILURE;
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	route = socket(PF_ROUTE, SOCK_RAW, 0);
	if (fd == -1 || route == -1) {
		perror("socket");
		return EXIT_FAILURE;
	}
	memset(&ireq, 0, sizeof(ireq));
	if (strlcpy(ireq.i_name, argv[1], sizeof(ireq.i_name)) >=
	    sizeof(ireq.i_name))
		return EXIT_FAILURE;
	ireq.i_type = IEEE80211_IOC_SAE;
	if (ioctl(fd, SIOCG80211, &ireq) == -1 ||
	    !(ireq.i_val & IEEE80211_SAE_CAP_SA_QUERY)) {
		fprintf(stderr, "SA Query capability unavailable\n");
		return EXIT_FAILURE;
	}
	puts("Listening for a new SAE association; reassociate the station now.");
	fflush(stdout);
	deadline = seconds() + 60;
	while (seconds() < deadline) {
		const struct if_announcemsghdr *ifan;
		const struct ieee80211req_sae *event;
		const size_t hdr = sizeof(*ifan) +
		    offsetof(struct ieee80211req_sae, data);
		fd_set fds;
		struct timeval wait = { 1, 0 };
		ssize_t n;

		if (captured && attempts < 5 && seconds() >= next) {
			ireq.i_len = sizeof(tx);
			ireq.i_data = &tx;
			if (ioctl(fd, SIOCS80211, &ireq) == 0) {
				sent = true;
				printf("Protected SA Query sent (generation %u)\n",
				    tx.generation);
			} else
				printf("SA Query not sent: errno %d\n", errno);
			fflush(stdout);
			attempts++;
			next = seconds() + 3;
		}
		FD_ZERO(&fds);
		FD_SET(route, &fds);
		if (select(route + 1, &fds, NULL, NULL, &wait) <= 0)
			continue;
		n = read(route, buf.data, sizeof(buf.data));
		if (n < (ssize_t)sizeof(struct if_announcemsghdr))
			continue;
		ifan = (const void *)buf.data;
		if (ifan->ifan_version != RTM_VERSION ||
		    ifan->ifan_type != RTM_IEEE80211 ||
		    ifan->ifan_index != index ||
		    ifan->ifan_what != RTM_IEEE80211_SAE ||
		    ifan->ifan_msglen > n || ifan->ifan_msglen < hdr)
			continue;
		event = (const void *)(ifan + 1);
		if (event->version != IEEE80211_SAE_VERSION ||
		    event->len > ifan->ifan_msglen - hdr)
			continue;
		if (event->op == IEEE80211_SAE_START) {
			sent = false;
			if (getentropy(token, sizeof(token)) == -1)
				return EXIT_FAILURE;
			memset(&tx, 0, sizeof(tx));
			tx.version = IEEE80211_SAE_VERSION;
			tx.generation = event->generation;
			tx.op = IEEE80211_SAE_TX_SA_QUERY;
			tx.len = 28;
			memcpy(tx.bssid, event->bssid, 6);
			tx.data[0] = 0xd0;
			memcpy(tx.data + 4, tx.bssid, 6);
			memcpy(tx.data + 10, self, 6);
			memcpy(tx.data + 16, tx.bssid, 6);
			tx.data[24] = 8;
			memcpy(tx.data + 26, token, 2);
			captured = true;
			attempts = 0;
			next = seconds() + 5;
		} else if (captured && sent &&
		    event->op == IEEE80211_SAE_RX_SA_QUERY &&
		    event->len == 28 && event->generation == tx.generation &&
		    memcmp(event->bssid, tx.bssid, 6) == 0 && event->data[25] == 1 &&
		    memcmp(event->data + 26, token, 2) == 0) {
			puts("PASS: protected SA Query response with matching "
			    "transaction ID");
			close(route);
			close(fd);
			return EXIT_SUCCESS;
		}
	}
	fprintf(stderr, "FAIL: no matching protected response within 60 seconds\n");
	return EXIT_FAILURE;
}
