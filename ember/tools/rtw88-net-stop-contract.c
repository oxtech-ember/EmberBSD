/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute the complete native ioctl consumer with modeled kernel services.
 * The ENETRESET multicast notification is ether_delmulti's list-change result;
 * these checks do not execute DHCP, net80211, DMA or RF calibration itself.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stop-constants.inc"
#define SIOCSIFFLAGS 1
#define SIOCDELMULTI 2
#define WPA_RESET 3
typedef unsigned long u_long;
struct ifnet { void *if_softc; unsigned if_flags; };
struct ieee80211com { unsigned unused; };
struct rtw88_net { struct ieee80211com ic; };
static unsigned starts, stops;
static int service_error, start_error;
static int
ifioctl_common(struct ifnet *ifp, u_long cmd, void *data)
{

	(void)cmd;
	if (service_error != 0)
		return service_error;
	ifp->if_flags = *(unsigned *)data;
	return 0;
}
static int
ieee80211_ioctl(struct ieee80211com *ic, u_long cmd, void *data)
{

	(void)ic;
	(void)cmd;
	(void)data;
	return service_error;
}
static int
rtw88_net_init(struct ifnet *ifp)
{

	starts++;
	if (start_error == 0)
		ifp->if_flags |= IFF_RUNNING;
	return start_error;
}
static void
rtw88_net_stop(struct ifnet *ifp, int disable)
{

	(void)disable;
	stops++;
	ifp->if_flags &= ~IFF_RUNNING;
}
#include "stop-native.inc"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
	fprintf(stderr, "FAIL line %u: %s\n", (unsigned)__LINE__, #c); } } while (0)

static void
test_consumer(void)
{
	struct rtw88_net net = { 0 };
	struct ifnet ifp = { &net, 0 };
	unsigned flags, before, i;
	const unsigned states[] = { 0, IFF_UP, IFF_RUNNING,
	    IFF_UP | IFF_RUNNING };

	for (i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
		ifp.if_flags = states[i];
		service_error = ENETRESET;
		before = starts;
		CHECK(rtw88_net_ioctl(&ifp, SIOCDELMULTI, NULL) == 0);
		CHECK(starts - before == (i == 3 ? 1U : 0U));
	}
	ifp.if_flags = IFF_UP | IFF_RUNNING;
	flags = IFF_RUNNING;
	service_error = 0;
	before = stops;
	CHECK(rtw88_net_ioctl(&ifp, SIOCSIFFLAGS, &flags) == 0);
	CHECK(ifp.if_flags == 0 && stops == before + 1);
	before = starts;
	service_error = ENETRESET;
	CHECK(rtw88_net_ioctl(&ifp, SIOCDELMULTI, NULL) == 0);
	CHECK(rtw88_net_ioctl(&ifp, WPA_RESET, NULL) == 0);
	CHECK(starts == before);
	service_error = EIO;
	CHECK(rtw88_net_ioctl(&ifp, SIOCDELMULTI, NULL) == EIO);
	CHECK(rtw88_net_ioctl(&ifp, SIOCSIFFLAGS, &flags) == EIO);
	CHECK(starts == before);
	ifp.if_flags = IFF_UP | IFF_RUNNING;
	service_error = ENETRESET;
	start_error = ENXIO;
	CHECK(rtw88_net_ioctl(&ifp, SIOCDELMULTI, NULL) == ENXIO);
	start_error = 0;
	service_error = 0;
	before = starts;
	CHECK(rtw88_net_ioctl(&ifp, WPA_RESET, NULL) == 0);
	CHECK(starts == before);
}

int
main(int argc, char **argv)
{
	struct rtw88_net net = { 0 };
	struct ifnet ifp = { &net, 0 };
	FILE *fp;
	unsigned flags;
	int error;

	if (argc == 1) {
		test_consumer();
		printf("Native RTL8822CE stop ioctl: %u checks, %u failures\n",
		    checks, failures);
		return failures != 0;
	}
	if (argc != 3)
		return 2;
	fp = fopen(argv[1], "r");
	if (fp == NULL)
		return 2;
	error = fscanf(fp, "%u %u", &ifp.if_flags, &starts);
	if (fclose(fp) != 0 || error != 2)
		return 2;
	if (strcmp(argv[2], "down") == 0 || strcmp(argv[2], "up") == 0) {
		flags = ifp.if_flags;
		if (strcmp(argv[2], "down") == 0)
			flags &= ~IFF_UP;
		else
			flags |= IFF_UP;
		error = rtw88_net_ioctl(&ifp, SIOCSIFFLAGS, &flags);
	} else if (strcmp(argv[2], "multicast") == 0 ||
	    strcmp(argv[2], "wpa") == 0) {
		service_error = ENETRESET;
		error = rtw88_net_ioctl(&ifp,
		    strcmp(argv[2], "multicast") == 0 ? SIOCDELMULTI : WPA_RESET,
		    NULL);
	} else
		return 2;
	if (error != 0)
		return 1;
	fp = fopen(argv[1], "w");
	if (fp == NULL)
		return 2;
	error = fprintf(fp, "%u %u\n", ifp.if_flags, starts);
	if (fclose(fp) != 0 || error < 0)
		return 2;
	return 0;
}
