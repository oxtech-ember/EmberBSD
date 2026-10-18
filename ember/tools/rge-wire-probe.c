/* SPDX-License-Identifier: BSD-2-Clause */
/* AI-assisted bounded native RGE raw-wire correctness probe. */
#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#ifdef RGE_WIRE_TEST
#include "wire-bpf.h"
#else
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <poll.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#endif

#define WIRE_MAX 9014
#define WIRE_BUFFER 65536
#define WIRE_LIMIT 65536
#define WIRE_TIMEOUT UINT64_C(3000000000)

static uint32_t
wire_crc(const uint8_t *p, size_t n)
{
	uint32_t crc = UINT32_MAX;

	for (size_t i = 0; i < n; i++) {
		crc ^= p[i];
		for (unsigned int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
	}
	return ~crc;
}

static void
wire_put(uint8_t *p, uint64_t v, unsigned int n)
{

	for (unsigned int i = 0; i < n; i++)
		p[i] = (uint8_t)(v >> ((n - i - 1) * 8));
}

static int
wire_build(uint8_t *p, size_t n, const uint8_t *src, const uint8_t *dst,
    uint64_t nonce, uint32_t seq)
{

	if (n < 60 || n > WIRE_MAX)
		return EINVAL;
	memcpy(p, dst, 6);
	memcpy(p + 6, src, 6);
	p[12] = 0x88;
	p[13] = 0xb5;
	memcpy(p + 14, "EMBRWIRE", 8);
	wire_put(p + 22, nonce, 8);
	wire_put(p + 30, seq, 4);
	wire_put(p + 34, n, 2);
	p[36] = seq % 2;
	p[37] = 1;
	for (size_t i = 38; i < n; i++)
		p[i] = (uint8_t)((nonce >> ((i % 8) * 8)) ^
		    (seq * 17U + i * 29U + i / 251));
	return 0;
}

/* Publish count only after the complete read has exactly one valid record. */
static int
wire_records(const uint8_t *p, size_t n, const uint8_t *frame, size_t len,
    unsigned int *count)
{
	struct bpf_hdr h;
	unsigned int found = 0;

	if (len < 60 || len > WIRE_MAX || n == 0)
		return EPROTO;
	while (n != 0) {
		uint32_t crc, got;
		size_t used, stride;

		if (n < sizeof(h))
			return EPROTO;
		memcpy(&h, p, sizeof(h));
		if (h.bh_hdrlen < sizeof(h) || h.bh_hdrlen > n ||
		    h.bh_caplen != len + 4 || h.bh_datalen != len + 4 ||
		    h.bh_caplen > n - h.bh_hdrlen)
			return EPROTO;
		used = h.bh_hdrlen + (size_t)h.bh_caplen;
		if (used > SIZE_MAX - (BPF_ALIGNMENT - 1))
			return EPROTO;
		stride = BPF_WORDALIGN(used);
		/* The last record need not have alignment padding. */
		if (used == n)
			stride = used;
		if (stride > n || ++found != 1)
			return EPROTO;
		if (memcmp(p + h.bh_hdrlen, frame, len) != 0)
			return EBADMSG;
		crc = wire_crc(frame, len);
		got = 0;
		for (unsigned int i = 0; i < 4; i++)
			got |= (uint32_t)p[h.bh_hdrlen + len + i] << (i * 8);
		if (got != crc)
			return EBADMSG;
		p += stride;
		n -= stride;
	}
	*count = found;
	return 0;
}

static int
wire_end(uint64_t now, uint64_t duration, uint64_t *end)
{

	if (duration > UINT64_MAX - now)
		return EOVERFLOW;
	*end = now + duration;
	return 0;
}

static int
wire_wait_ms(uint64_t now, uint64_t last, uint64_t end, int *ms)
{
	uint64_t left;

	if (now < last)
		return EIO;
	if (now >= end)
		return ETIMEDOUT;
	left = end - now;
	left = left / 1000000 + (left % 1000000 != 0);
	*ms = left > INT_MAX ? INT_MAX : (int)left;
	return 0;
}

static int
wire_count(const char *s, unsigned int *out)
{
	unsigned int value = 0;

	if (*s == '\0')
		return EINVAL;
	for (; *s != '\0'; s++) {
		if (*s < '0' || *s > '9' || value > WIRE_LIMIT / 10)
			return EINVAL;
		value = value * 10 + (unsigned int)(*s - '0');
		if (value > WIRE_LIMIT)
			return EINVAL;
	}
	if (value < 8 || value % 8 != 0)
		return EINVAL;
	*out = value;
	return 0;
}

static size_t
wire_size(uint32_t seq)
{
	static const size_t sizes[] = { 60, 1514, 1515, 9014 };

	return sizes[(seq / 2) % 4];
}

static int
wire_final(uint64_t captured, uint64_t dropped, unsigned int expected,
    uint64_t before_i, uint64_t before_o, uint64_t after_i, uint64_t after_o)
{

	if (captured != expected || dropped != 0)
		return ENOBUFS;
	/* Any change, including wrap or reset, fails this bounded observation. */
	if (before_i != after_i || before_o != after_o)
		return EIO;
	return 0;
}

static uint32_t
wire_be(const uint8_t *p, unsigned int n)
{
	uint32_t value = 0;

	for (unsigned int i = 0; i < n; i++)
		value = (value << 8) | p[i];
	return value;
}

/* Closed ingress MAC/EtherType/magic/nonce selector. */
static void
wire_program(const uint8_t *frame, struct bpf_insn *insns)
{
	static const unsigned int offsets[] = { 0, 4, 8, 12, 14, 18, 22, 26 };

	memset(insns, 0, 18 * sizeof(*insns));
	for (unsigned int i = 0; i < 8; i++) {
		unsigned int width = i == 3 ? 2 : 4;
		insns[i * 2].code = BPF_LD | BPF_ABS |
		    (width == 2 ? BPF_H : BPF_W);
		insns[i * 2].k = offsets[i];
		insns[i * 2 + 1].code = BPF_JMP | BPF_JEQ | BPF_K;
		insns[i * 2 + 1].k = wire_be(frame + offsets[i], width);
		insns[i * 2 + 1].jf = (uint8_t)(15 - i * 2);
	}
	insns[16].code = BPF_RET | BPF_K;
	insns[16].k = WIRE_MAX + 4;
	insns[17].code = BPF_RET | BPF_K;
}

#ifndef RGE_WIRE_TEST
struct wire_port {
	int fd;
	uint8_t mac[6];
	u_int buflen;
	struct if_data before;
	uint8_t buffer[WIRE_BUFFER];
};

static int
wire_now(uint64_t *now)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		return errno;
	if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000 ||
	    (uint64_t)ts.tv_sec > (UINT64_MAX - (uint64_t)ts.tv_nsec) /
	    1000000000)
		return EOVERFLOW;
	*now = (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
	return 0;
}

static int
wire_poll(int fd, short events, uint64_t end, uint64_t *last)
{
	for (unsigned int tries = 0; tries < 4096; tries++) {
		struct pollfd pfd;
		uint64_t now;
		int error, ms, result;

		error = wire_now(&now);
		if (error != 0)
			return error;
		error = wire_wait_ms(now, *last, end, &ms);
		if (error != 0)
			return error;
		*last = now;
		pfd.fd = fd;
		pfd.events = events;
		pfd.revents = 0;
		result = poll(&pfd, 1, ms);
		if (result == -1) {
			if (errno == EINTR)
				continue;
			return errno;
		}
		if (result == 0)
			continue;
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
			return EIO;
		if (pfd.revents & events) {
			error = wire_now(&now);
			if (error != 0)
				return error;
			error = wire_wait_ms(now, *last, end, &ms);
			*last = now;
			return error;
		}
		return EIO;
	}
	return ETIMEDOUT;
}

static int
wire_mac(const char *name, uint8_t *mac)
{
	struct ifaddrs *all;
	const struct ifaddrs *ifa;
	const struct sockaddr_dl *sdl;
	size_t prefix = offsetof(struct sockaddr_dl, sdl_data);
	unsigned int found = 0;
	int error = ENXIO;
	uint8_t nonzero = 0;

	if (getifaddrs(&all) == -1)
		return errno;
	for (ifa = all; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || strcmp(ifa->ifa_name, name) != 0 ||
		    ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		sdl = (const struct sockaddr_dl *)ifa->ifa_addr;
		if (++found != 1 || sdl->sdl_len < prefix ||
		    sdl->sdl_type != IFT_ETHER || sdl->sdl_alen != 6 ||
		    sdl->sdl_nlen > sdl->sdl_len - prefix ||
		    6 > sdl->sdl_len - prefix - sdl->sdl_nlen) {
			error = EINVAL;
			break;
		}
		memcpy(mac, LLADDR(sdl), 6);
		for (unsigned int i = 0; i < 6; i++)
			nonzero |= mac[i];
		error = nonzero == 0 || (mac[0] & 1) ? EINVAL : 0;
	}
	freeifaddrs(all);
	return error;
}

static int
wire_info(int sock, const char *name, struct if_data *data)
{
	struct ifreq req;
	struct ifdatareq dreq;

	memset(&req, 0, sizeof(req));
	strlcpy(req.ifr_name, name, sizeof(req.ifr_name));
	if (ioctl(sock, SIOCGIFFLAGS, &req) == -1)
		return errno;
	if ((req.ifr_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING))
		return ENETDOWN;
	if (ioctl(sock, SIOCGIFMTU, &req) == -1)
		return errno;
	if (req.ifr_mtu != 9000)
		return EMSGSIZE;
	memset(&dreq, 0, sizeof(dreq));
	strlcpy(dreq.ifdr_name, name, sizeof(dreq.ifdr_name));
	if (ioctl(sock, SIOCGIFDATA, &dreq) == -1)
		return errno;
	*data = dreq.ifdr_data;
	return 0;
}

static int
wire_filter(int fd, const uint8_t *frame)
{
	struct bpf_insn insns[18];
	struct bpf_program program;

	wire_program(frame, insns);
	program.bf_len = 18;
	program.bf_insns = insns;
	return ioctl(fd, BIOCSETF, &program) == -1 ? errno : 0;
}

static int
wire_open(struct wire_port *port, const char *name, const uint8_t *other,
    uint64_t nonce)
{
	struct bpf_version version;
	struct ifreq req;
	uint8_t frame[60];
	u_int value;
	int error;

	port->fd = open("/dev/bpf", O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (port->fd == -1)
		return errno;
	if (ioctl(port->fd, BIOCVERSION, &version) == -1)
		return errno;
	if (version.bv_major != BPF_MAJOR_VERSION ||
	    version.bv_minor < BPF_MINOR_VERSION)
		return EPROTONOSUPPORT;
	value = WIRE_BUFFER;
	if (ioctl(port->fd, BIOCSBLEN, &value) == -1)
		return errno;
	(void)wire_build(frame, sizeof(frame), other, port->mac, nonce, 0);
	error = wire_filter(port->fd, frame);
	if (error != 0)
		return error;
	memset(&req, 0, sizeof(req));
	strlcpy(req.ifr_name, name, sizeof(req.ifr_name));
	if (ioctl(port->fd, BIOCSETIF, &req) == -1)
		return errno;
	memset(&req, 0, sizeof(req));
	if (ioctl(port->fd, BIOCGETIF, &req) == -1)
		return errno;
	if (strncmp(req.ifr_name, name, sizeof(req.ifr_name)) != 0)
		return EINVAL;
	if (ioctl(port->fd, BIOCGDLT, &value) == -1)
		return errno;
	if (value != DLT_EN10MB)
		return EPROTONOSUPPORT;
	if (ioctl(port->fd, BIOCGBLEN, &port->buflen) == -1)
		return errno;
	if (port->buflen < BPF_WORDALIGN(sizeof(struct bpf_hdr) + WIRE_MAX + 4) ||
	    port->buflen > sizeof(port->buffer))
		return EOVERFLOW;
	value = 1;
	if (ioctl(port->fd, BIOCSHDRCMPLT, &value) == -1 ||
	    ioctl(port->fd, BIOCGHDRCMPLT, &value) == -1)
		return errno;
	if (value != 1)
		return EPROTO;
	value = BPF_D_IN;
	if (ioctl(port->fd, BIOCSDIRECTION, &value) == -1 ||
	    ioctl(port->fd, BIOCGDIRECTION, &value) == -1)
		return errno;
	if (value != BPF_D_IN)
		return EPROTO;
	value = 0;
	if (ioctl(port->fd, BIOCSFEEDBACK, &value) == -1 ||
	    ioctl(port->fd, BIOCGFEEDBACK, &value) == -1)
		return errno;
	if (value != 0)
		return EPROTO;
	value = 1;
	if (ioctl(port->fd, BIOCIMMEDIATE, &value) == -1)
		return errno;
	return ioctl(port->fd, BIOCFLUSH) == -1 ? errno : 0;
}

static int
wire_exchange(const struct wire_port *tx, struct wire_port *rx, uint64_t nonce,
    uint32_t seq, size_t len)
{
	uint8_t frame[WIRE_MAX];
	uint64_t last, end;
	ssize_t n;
	unsigned int count;
	int error;

	(void)wire_build(frame, len, tx->mac, rx->mac, nonce, seq);
	error = wire_now(&last);
	if (error != 0)
		return error;
	error = wire_end(last, WIRE_TIMEOUT, &end);
	if (error != 0)
		return error;
	for (unsigned int tries = 0; ; tries++) {
		if (tries == 4096)
			return ETIMEDOUT;
		n = write(tx->fd, frame, len);
		if (n == (ssize_t)len)
			break;
		if (n >= 0)
			return EIO;
		if (errno != EINTR && errno != EAGAIN)
			return errno;
		error = wire_poll(tx->fd, POLLOUT, end, &last);
		if (error != 0)
			return error;
	}
	for (unsigned int tries = 0; tries < 4096; tries++) {
		error = wire_poll(rx->fd, POLLIN, end, &last);
		if (error != 0)
			return error;
		n = read(rx->fd, rx->buffer, rx->buflen);
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return errno;
		}
		if (n == 0)
			return EIO;
		return wire_records(rx->buffer, (size_t)n, frame, len, &count);
	}
	return ETIMEDOUT;
}

int
main(int argc, char **argv)
{
	static struct wire_port ports[2];
	static const char *names[] = { "rge0", "rge1" };
	struct if_data after;
	struct bpf_stat stats;
	uint8_t frame[60];
	uint64_t nonce;
	unsigned int count = 32, completed = 0;
	int sock = -1, error = 0, saved;
	const char *stage;

	ports[0].fd = ports[1].fd = -1;
	if (argc == 2 && strcmp(argv[1], "--selftest") == 0) {
		if (wire_crc((const uint8_t *)"123456789", 9) != 0xcbf43926 ||
		    wire_build(frame, sizeof(frame), (const uint8_t *)"abcdef",
		    (const uint8_t *)"ghijkl", 1, 0) != 0)
			return 1;
		puts("rge-wire selftest: passed (no device I/O)");
		return 0;
	}
	if ((argc != 3 && argc != 4) || strcmp(argv[1], names[0]) != 0 ||
	    strcmp(argv[2], names[1]) != 0 ||
	    (argc == 4 && wire_count(argv[3], &count) != 0)) {
		fprintf(stderr, "usage: rge-wire-probe rge0 rge1 [count]\n"
		    "       rge-wire-probe --selftest\n");
		return 2;
	}
	stage = "interface identity";
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) {
		error = errno;
		goto out;
	}
	for (unsigned int i = 0; i < 2; i++) {
		error = wire_mac(names[i], ports[i].mac);
		if (error != 0)
			goto out;
		error = wire_info(sock, names[i], &ports[i].before);
		if (error != 0)
			goto out;
	}
	if (memcmp(ports[0].mac, ports[1].mac, 6) == 0) {
		error = EINVAL;
		goto out;
	}
	arc4random_buf(&nonce, sizeof(nonce));
	stage = "BPF setup";
	for (unsigned int i = 0; i < 2; i++) {
		error = wire_open(&ports[i], names[i], ports[1 - i].mac, nonce);
		if (error != 0)
			goto out;
	}
	stage = "wire exchange";
	for (unsigned int seq = 0; seq < count; seq++) {
		unsigned int direction = seq % 2;
		error = wire_exchange(&ports[direction], &ports[1 - direction],
		    nonce, seq, wire_size(seq));
		if (error != 0)
			goto out;
		completed++;
	}
	stage = "final counters";
	for (unsigned int i = 0; i < 2; i++) {
		if (ioctl(ports[i].fd, BIOCGSTATS, &stats) == -1) {
			error = errno;
			goto out;
		}
		error = wire_info(sock, names[i], &after);
		if (error != 0)
			goto out;
		error = wire_final(stats.bs_capt, stats.bs_drop, count / 2,
		    ports[i].before.ifi_ierrors, ports[i].before.ifi_oerrors,
		    after.ifi_ierrors, after.ifi_oerrors);
		if (error != 0)
			goto out;
	}
	stage = "complete";
out:
	for (unsigned int i = 0; i < 2; i++) {
		if (ports[i].fd >= 0 && close(ports[i].fd) == -1) {
			saved = errno;
			if (error == 0)
				error = saved;
		}
	}
	if (sock >= 0 && close(sock) == -1 && error == 0)
		error = errno;
	printf("rge-wire: stage=%s frames=%u/%u error=%d\n",
	    stage, completed, count, error);
	if (error == 0)
		puts("PASS: both directions, four sizes, FCS/payload exact, "
		    "BPF drops=0, interface error deltas=0");
	return error != 0;
}
#endif
