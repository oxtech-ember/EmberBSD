/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD IPv4 fragmentation-control regression. */
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include "defines.h"
#include "options.h"

#define IP_DF 0x4000
#define IPVERSION 4
#define IP_FORWARDING 1
#define IP_RAWOUTPUT 2
#define IP_RETURNMTU 4
#define IP_MTUDISC 0x400
#define KASSERT assert
#define IP_STATINC(x) ((void)0)
#define in4p_errormtu(inp) ((inp)->mtu)

struct ip { uint16_t ip_off; int ip_v, ip_hl; };
struct inpcb { int inp_af, inp_flags, mtu; };
struct sockopt { int sopt_name; };

static int
set_option(struct inpcb *inp, int optval)
{
	struct sockopt option = { IP_DONTFRAG }, *sopt = &option;
	int inpflags = inp->inp_flags;
#include "family.inc"
	switch (sopt->sopt_name) {
#include "set.inc"
	default: return ENOPROTOOPT;
	}
	inp->inp_flags = inpflags;
	return 0;
}

static int
get_option(struct inpcb *inp, int *value)
{
	struct sockopt option = { IP_DONTFRAG }, *sopt = &option;
	int optval = -1, inpflags = inp->inp_flags;
#include "family.inc"
	switch (sopt->sopt_name) {
#include "get.inc"
	default: return ENOPROTOOPT;
	}
	*value = optval;
	return 0;
}

static int
header(struct ip *ip, struct inpcb *inp, int flags, bool rtmtu_nolock)
{
	int hlen = 20;
#include "header.inc"
#include "pmtu.inc"
	return flags;
}

static int
fragment(struct ip *ip, struct inpcb *inp, int flags, int mtu,
    int *fragments)
{
	int error;
#include "fragment.inc"
	++*fragments;
	return 0;
bad:
	return error;
}

int
main(void)
{
	struct inpcb inp = { AF_INET, INP_RECVTTL, 0 };
	struct inpcb ipv6 = { AF_INET6, INP_DONTFRAG, 123 };
	struct ip ip;
	int value, flags, fragments = 0;

	assert(IP_DONTFRAG == 28);
	assert(get_option(&inp, &value) == 0 && value == 0);
	assert(set_option(&inp, -1) == 0);
	assert(get_option(&inp, &value) == 0 && value == 1);
	assert(inp.inp_flags == (INP_RECVTTL | INP_DONTFRAG));
	assert(set_option(&inp, 0) == 0 && inp.inp_flags == INP_RECVTTL);
	assert(set_option(&ipv6, 1) == ENOPROTOOPT);
	assert(get_option(&ipv6, &value) == ENOPROTOOPT);

	for (int enabled = 0; enabled <= 1; enabled++) {
		assert(set_option(&inp, enabled) == 0);
		for (int locked = 0; locked <= 1; locked++) {
			for (int pmtu = 0; pmtu <= 1; pmtu++) {
				ip.ip_off = htons(0xffff);
				flags = header(&ip, &inp,
				    pmtu ? IP_MTUDISC : 0, !locked);
				bool df = enabled || (pmtu && !locked);
				assert(ntohs(ip.ip_off) == (df ? IP_DF : 0));
				assert(!!(flags & IP_RETURNMTU) == enabled);
				int old = fragments;
				assert(fragment(&ip, &inp, flags, 576,
				    &fragments) == (df ? EMSGSIZE : 0));
				assert(fragments == old + !df);
				if (enabled)
					assert(inp.mtu == 576);
			}
		}
	}

	/* Raw headers retain an explicit DF even when the socket option is off. */
	assert(set_option(&inp, 0) == 0);
	ip.ip_off = htons(IP_DF | 3);
	ip.ip_hl = 5;
	flags = header(&ip, &inp, IP_RAWOUTPUT, false);
	assert(ntohs(ip.ip_off) == (IP_DF | 3));
	assert(!(flags & IP_RETURNMTU));
	assert(fragment(&ip, &inp, flags, 1280, &fragments) == EMSGSIZE);

	/* Forwarding without a PCB and IPv4-mapped IPv6 do not use in4p storage. */
	ip.ip_off = 0;
	flags = header(&ip, NULL, IP_FORWARDING, false);
	assert(ip.ip_off == 0 && !(flags & IP_RETURNMTU));
	flags = header(&ip, &ipv6, 0, false);
	assert(ip.ip_off == 0 && !(flags & IP_RETURNMTU));
	assert(ipv6.mtu == 123);
	puts("PASS: actual DF option/header/fragment branches; route MTU locks, raw headers and PCB families");
	return 0;
}
