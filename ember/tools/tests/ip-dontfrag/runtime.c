/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 EmberBSD contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* Origin: EmberBSD native IPv4 socket DF and loopback packet regression. */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <net/bpf.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Permit this same executable to reject a kernel predating the option. */
#ifndef IP_DONTFRAG
#define IP_DONTFRAG 28
#endif

struct capture {
	int fd;
	u_int length;
	u_char *buffer;
};

static int
get_option(int fd, int option)
{
	socklen_t length = sizeof(int);
	int value = -1;

	if (getsockopt(fd, IPPROTO_IP, option, &value, &length) == -1)
		err(EXIT_FAILURE, "getsockopt IPv4 option %d", option);
	if (length != sizeof(value))
		errx(EXIT_FAILURE, "unexpected IPv4 option %d size", option);
	return value;
}

static void
set_df(int fd, int value)
{

	if (setsockopt(fd, IPPROTO_IP, IP_DONTFRAG, &value, sizeof(value)) == -1)
		err(EXIT_FAILURE, "setsockopt IP_DONTFRAG");
	if (get_option(fd, IP_DONTFRAG) != (value != 0))
		errx(EXIT_FAILURE, "IP_DONTFRAG boolean readback mismatch");
}

static void
capture_open(struct capture *capture)
{
	struct ifreq ifr;
	struct bpf_insn instructions[] = { BPF_STMT(BPF_RET | BPF_K, 128) };
	struct bpf_program filter = { 1, instructions };
	u_int immediate = 1, direction = BPF_D_OUT, dlt;
	char path[32];
	unsigned i;

	capture->fd = open("/dev/bpf", O_RDONLY | O_NONBLOCK);
	if (capture->fd == -1) {
		for (i = 0; i < 32; i++) {
			snprintf(path, sizeof(path), "/dev/bpf%u", i);
			capture->fd = open(path, O_RDONLY | O_NONBLOCK);
			if (capture->fd != -1)
				break;
			if (errno != EBUSY && errno != ENOENT)
				break;
		}
	}
	if (capture->fd == -1)
		err(EXIT_FAILURE, "open BPF (root and BPF device required)");
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, "lo0", sizeof(ifr.ifr_name));
	if (ioctl(capture->fd, BIOCSETIF, &ifr) == -1 ||
	    ioctl(capture->fd, BIOCGDLT, &dlt) == -1 ||
	    ioctl(capture->fd, BIOCIMMEDIATE, &immediate) == -1 ||
	    ioctl(capture->fd, BIOCSDIRECTION, &direction) == -1 ||
	    ioctl(capture->fd, BIOCSETF, &filter) == -1 ||
	    ioctl(capture->fd, BIOCGBLEN, &capture->length) == -1)
		err(EXIT_FAILURE, "configure BPF lo0");
	if (dlt != DLT_NULL)
		errx(EXIT_FAILURE, "lo0 has DLT %u, expected DLT_NULL", dlt);
	capture->buffer = malloc(capture->length);
	if (capture->buffer == NULL)
		err(EXIT_FAILURE, "allocate BPF buffer");
}

static bool
packet_match(const u_char *packet, size_t length, in_port_t port,
    uint32_t sequence, bool df)
{
	struct ip ip;
	struct udphdr udp;
	uint32_t family, payload[2];
	size_t hlen;

	if (length < sizeof(family) + sizeof(ip))
		return false;
	memcpy(&family, packet, sizeof(family));
	if (family != AF_INET)
		return false;
	packet += sizeof(family);
	length -= sizeof(family);
	memcpy(&ip, packet, sizeof(ip));
	hlen = (size_t)ip.ip_hl * 4;
	if (ip.ip_v != 4 || ip.ip_p != IPPROTO_UDP || hlen < sizeof(ip) ||
	    length < hlen + sizeof(udp) + sizeof(payload))
		return false;
	memcpy(&udp, packet + hlen, sizeof(udp));
	memcpy(payload, packet + hlen + sizeof(udp), sizeof(payload));
	if (udp.uh_dport != port || payload[0] != htonl(0x44465453) ||
	    payload[1] != htonl(sequence))
		return false;
	if (ip.ip_src.s_addr != htonl(INADDR_LOOPBACK) ||
	    ip.ip_dst.s_addr != htonl(INADDR_LOOPBACK))
		errx(EXIT_FAILURE, "test datagram has non-loopback addresses");
	if (((ntohs(ip.ip_off) & IP_DF) != 0) != df)
		errx(EXIT_FAILURE, "packet %u has incorrect DF bit", sequence);
	if (ntohs(ip.ip_off) & (IP_MF | IP_OFFMASK))
		errx(EXIT_FAILURE, "small test datagram was fragmented");
	return true;
}

static void
capture_df(struct capture *capture, in_port_t port, uint32_t sequence, bool df)
{
	struct timespec start, now;
	struct pollfd pfd = { .fd = capture->fd, .events = POLLIN };
	struct bpf_hdr header;
	size_t offset, record;
	ssize_t length;
	int ready;

	if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
		err(EXIT_FAILURE, "clock_gettime");
	for (;;) {
		if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
			err(EXIT_FAILURE, "clock_gettime");
		if (now.tv_sec - start.tv_sec >= 5)
			errx(EXIT_FAILURE, "BPF timeout for packet %u", sequence);
		ready = poll(&pfd, 1, 200);
		if (ready == -1 && errno == EINTR)
			continue;
		if (ready == -1)
			err(EXIT_FAILURE, "poll BPF");
		if (ready == 0)
			continue;
		length = read(capture->fd, capture->buffer, capture->length);
		if (length == -1 && (errno == EAGAIN || errno == EINTR))
			continue;
		if (length <= 0)
			err(EXIT_FAILURE, "read BPF");
		for (offset = 0; offset < (size_t)length; offset += record) {
			if ((size_t)length - offset < sizeof(header))
				errx(EXIT_FAILURE, "truncated BPF header");
			memcpy(&header, capture->buffer + offset, sizeof(header));
			if (header.bh_hdrlen < offsetof(struct bpf_hdr, bh_hdrlen) +
			    sizeof(header.bh_hdrlen) ||
			    header.bh_hdrlen > (size_t)length - offset ||
			    header.bh_caplen > (size_t)length - offset - header.bh_hdrlen)
				errx(EXIT_FAILURE, "invalid BPF record length");
			record = BPF_WORDALIGN(header.bh_hdrlen + header.bh_caplen);
			if (packet_match(capture->buffer + offset + header.bh_hdrlen,
			    header.bh_caplen, port, sequence, df))
				return;
		}
	}
}

static void
small_packet(int sender, int receiver, struct capture *capture,
    in_port_t port, uint32_t sequence, bool df)
{
	uint32_t payload[2] = { htonl(0x44465453), htonl(sequence) };
	uint32_t received[2];

	if (send(sender, payload, sizeof(payload), 0) != sizeof(payload))
		err(EXIT_FAILURE, "send small UDP datagram");
	if (recv(receiver, received, sizeof(received), 0) != sizeof(received))
		err(EXIT_FAILURE, "receive small UDP datagram");
	if (memcmp(payload, received, sizeof(payload)) != 0)
		errx(EXIT_FAILURE, "small UDP payload mismatch");
	capture_df(capture, port, sequence, df);
}

int
main(int argc, char **argv)
{
	struct capture capture;
	struct sockaddr_in address;
	struct ifreq ifr;
	struct timeval timeout = { .tv_sec = 5 };
	socklen_t length;
	u_char *payload, *received;
	int sender, receiver, mtu, value = 1, buffer = 131072;
	size_t i;
	ssize_t result;

	(void)argv;
	if (argc != 1)
		errx(EXIT_FAILURE, "usage: ip-dontfrag (no arguments)");
	sender = socket(AF_INET, SOCK_DGRAM, 0);
	receiver = socket(AF_INET, SOCK_DGRAM, 0);
	if (sender == -1 || receiver == -1)
		err(EXIT_FAILURE, "socket UDP");
	/* An old kernel must fail here with ENOPROTOOPT, never silently pass. */
	if (get_option(sender, IP_DONTFRAG) != 0)
		errx(EXIT_FAILURE, "new socket defaults to IP_DONTFRAG enabled");
	if (setsockopt(sender, IPPROTO_IP, IP_DONTFRAG, &value, 0) != -1 ||
	    errno != EINVAL || get_option(sender, IP_DONTFRAG) != 0)
		errx(EXIT_FAILURE, "invalid option size did not preserve disabled state");
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, "lo0", sizeof(ifr.ifr_name));
	if (ioctl(sender, SIOCGIFMTU, &ifr) == -1)
		err(EXIT_FAILURE, "query lo0 MTU");
	mtu = ifr.ifr_mtu;
	if (mtu < 128 || mtu > 65507)
		errx(EXIT_FAILURE, "lo0 MTU %d cannot exercise UDP fragmentation", mtu);
	if (setsockopt(sender, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer)) == -1 ||
	    setsockopt(receiver, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer)) == -1 ||
	    setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1)
		err(EXIT_FAILURE, "configure UDP socket buffers");
	memset(&address, 0, sizeof(address));
	address.sin_len = sizeof(address);
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(receiver, (struct sockaddr *)&address, sizeof(address)) == -1)
		err(EXIT_FAILURE, "bind UDP loopback receiver");
	length = sizeof(address);
	if (getsockname(receiver, (struct sockaddr *)&address, &length) == -1 ||
	    connect(sender, (struct sockaddr *)&address, sizeof(address)) == -1)
		err(EXIT_FAILURE, "connect UDP loopback sender");
	capture_open(&capture);
	small_packet(sender, receiver, &capture, address.sin_port, 0, false);
	set_df(sender, 1);
	small_packet(sender, receiver, &capture, address.sin_port, 1, true);
	set_df(sender, -1);
	set_df(sender, 0);
	small_packet(sender, receiver, &capture, address.sin_port, 2, false);
	close(capture.fd);
	free(capture.buffer);
	payload = malloc((size_t)mtu);
	received = malloc((size_t)mtu + 1);
	if (payload == NULL || received == NULL)
		err(EXIT_FAILURE, "allocate oversized datagram");
	for (i = 0; i < (size_t)mtu; i++)
		payload[i] = (u_char)(i ^ (i >> 8));
	set_df(sender, 1);
	errno = 0;
	result = send(sender, payload, (size_t)mtu, 0);
	if (result != -1 || errno != EMSGSIZE)
		errx(EXIT_FAILURE, "DF oversized send returned %zd, errno %d", result, errno);
	if (get_option(sender, IP_ERRORMTU) != mtu)
		errx(EXIT_FAILURE, "IP_ERRORMTU differs from lo0 MTU %d", mtu);
	set_df(sender, 0);
	if (send(sender, payload, (size_t)mtu, 0) != mtu)
		err(EXIT_FAILURE, "send fragmentable oversized datagram");
	if (recv(receiver, received, (size_t)mtu + 1, 0) != mtu)
		err(EXIT_FAILURE, "receive reassembled oversized datagram");
	if (memcmp(payload, received, (size_t)mtu) != 0)
		errx(EXIT_FAILURE, "reassembled UDP payload mismatch");
	free(payload);
	free(received);
	close(sender);
	close(receiver);
	printf("PASS: IPv4 DF default/set/get/clear, BPF packet bits, EMSGSIZE, "
	    "IP_ERRORMTU=%d and reassembly with DF disabled\n", mtu);
	return EXIT_SUCCESS;
}
