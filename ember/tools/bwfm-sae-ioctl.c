/* SPDX-License-Identifier: BSD-2-Clause */
/* Run as root with wpa_supplicant stopped: bwfm-sae-ioctl bwfm0 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;

static void
check(int pass, const char *name)
{
	checks++;
	if (!pass) {
		fprintf(stderr, "FAIL: %s (errno %d)\n", name, errno);
		exit(EXIT_FAILURE);
	}
}

int
main(int argc, char **argv)
{
	struct ieee80211req ireq;
	struct ieee80211req_sae req;
	int fd, status;
	pid_t child;

	if (argc != 2 || geteuid() != 0) {
		fprintf(stderr, "usage (root, supplicant stopped): %s interface\n", argv[0]);
		return EXIT_FAILURE;
	}
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	check(fd >= 0, "socket");
	memset(&ireq, 0, sizeof(ireq));
	check(strlcpy(ireq.i_name, argv[1], sizeof(ireq.i_name)) < sizeof(ireq.i_name),
	    "interface name");
	ireq.i_type = IEEE80211_IOC_SAE;
	check(ioctl(fd, SIOCG80211, &ireq) == 0, "capability query");
	printf("SAE capabilities: 0x%x\n", ireq.i_val);
	ireq.i_len = 1;
	check(ioctl(fd, SIOCG80211, &ireq) == -1 && errno == EINVAL,
	    "get does not expose data");
	memset(&req, 0, sizeof(req));
	req.version = IEEE80211_SAE_VERSION;
	req.op = IEEE80211_SAE_CONFIGURE;
	req.len = 1;
	ireq.i_data = &req;
	ireq.i_len = sizeof(req) - 1;
	check(ioctl(fd, SIOCS80211, &ireq) == -1 && errno == EINVAL,
	    "short request");
	ireq.i_len = sizeof(req);
	req.version++;
	check(ioctl(fd, SIOCS80211, &ireq) == -1 && errno == EINVAL,
	    "unknown ABI version");
	req.version = IEEE80211_SAE_VERSION;
	req.reserved[0] = 1;
	check(ioctl(fd, SIOCS80211, &ireq) == -1 && errno == EINVAL,
	    "reserved bits");
	req.reserved[0] = 0;
	req.len = sizeof(req.data) + 1;
	check(ioctl(fd, SIOCS80211, &ireq) == -1 && errno == EINVAL,
	    "oversized payload");
	req.len = 1;
	req.data[0] = 1;
	check(ioctl(fd, SIOCS80211, &ireq) == -1 && errno == EINVAL,
	    "zero generation and peer");
	req.generation = 1;
	check(ioctl(fd, SIOCS80211, &ireq) == -1 && errno == EINVAL,
	    "zero peer with nonzero generation");
	req.bssid[0] = 1;
	check(ioctl(fd, SIOCS80211, &ireq) == -1 && errno == EINVAL,
	    "multicast peer");
	req.bssid[0] = 2;
	req.generation = 0;
	check(ioctl(fd, SIOCS80211, &ireq) == -1 && errno == EINVAL,
	    "zero generation with unicast peer");
	memset(req.bssid, 0, sizeof(req.bssid));
	req.data[0] = 0;
	check(ioctl(fd, SIOCS80211, &ireq) == 0, "disable SAE");
	req.op = IEEE80211_SAE_AUTH_STATUS;
	req.len = 2;
	check(ioctl(fd, SIOCS80211, &ireq) == -1 && errno == ESTALE,
	    "status cannot authorize an inactive join");
	req.op = IEEE80211_SAE_CONFIGURE;
	req.len = 1;
	child = fork();
	check(child >= 0, "fork");
	if (child == 0) {
		if (setuid(65534) != 0)
			_exit(2);
		_exit(ioctl(fd, SIOCS80211, &ireq) == -1 && errno == EPERM ? 0 : 3);
	}
	check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
	    WEXITSTATUS(status) == 0, "unprivileged configuration denied");
	close(fd);
	printf("PASS: %u live SAE ioctl checks\n", checks);
	return EXIT_SUCCESS;
}
