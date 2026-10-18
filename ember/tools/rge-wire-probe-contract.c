/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual-source frame and native BPF record contract. */
#define RGE_WIRE_TEST 1
#include "rge-wire-probe.c"
#include "wire-filter.inc"

static unsigned int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); } } while (0)

static const uint8_t mac0[6] = { 2, 0, 0, 0, 0, 1 };
static const uint8_t mac1[6] = { 2, 0, 0, 0, 0, 2 };

static size_t
record(uint8_t *p, const uint8_t *frame, size_t len)
{
	struct bpf_hdr h;
	uint32_t crc;
	size_t n;

	memset(&h, 0, sizeof(h));
	h.bh_hdrlen = sizeof(h);
	h.bh_caplen = h.bh_datalen = (uint32_t)len + 4;
	memcpy(p, &h, sizeof(h));
	memcpy(p + sizeof(h), frame, len);
	crc = wire_crc(frame, len);
	for (unsigned int i = 0; i < 4; i++)
		p[sizeof(h) + len + i] = (uint8_t)(crc >> (8 * i));
	n = BPF_WORDALIGN(sizeof(h) + len + 4);
	memset(p + sizeof(h) + len + 4, 0, n - sizeof(h) - len - 4);
	return n;
}

int
main(void)
{
	static uint8_t frame[9014], other[9014], buf[20000];
	static const size_t lengths[] = {
	    60, 60, 1514, 1514, 1515, 1515, 9014, 9014
	};
	static const uint8_t header[] = {
	    2,0,0,0,0,2, 2,0,0,0,0,1, 0x88,0xb5,
	    'E','M','B','R','W','I','R','E', 1,2,3,4,5,6,7,8,
	    0,0,0,0, 0,60, 0,1
	};
	struct bpf_insn program[18];
	unsigned int count, value;
	uint64_t deadline;
	int ms;
	size_t n;

	CHECK(wire_crc((const uint8_t *)"123456789", 9) == 0xcbf43926);
	CHECK(wire_build(frame, 60, mac0, mac1, UINT64_C(0x0102030405060708), 0) == 0);
	CHECK(memcmp(frame, header, sizeof(header)) == 0);
	CHECK(frame[38] == 0x4c && frame[39] == 0x6a);
	CHECK(wire_build(other, 60, mac0, mac1, UINT64_C(0x0102030405060708), 2) == 0);
	CHECK(memcmp(frame + 38, other + 38, 22) != 0);
	for (unsigned int seq = 0; seq < 32; seq++) {
		size_t len = lengths[seq % 8];
		CHECK(wire_size(seq) == len);
		const uint8_t *src = seq % 2 ? mac1 : mac0;
		const uint8_t *dst = seq % 2 ? mac0 : mac1;
		CHECK(wire_build(frame, len, src, dst, 99, seq) == 0);
		CHECK(frame[36] == seq % 2);
		n = record(buf, frame, len);
		count = 77;
		CHECK(wire_records(buf, n, frame, len, &count) == 0 && count == 1);
		/* A corrupt payload or FCS must fail without publishing count. */
		buf[sizeof(struct bpf_hdr) + 40] ^= 1;
		count = 77;
		CHECK(wire_records(buf, n, frame, len, &count) != 0 && count == 77);
		buf[sizeof(struct bpf_hdr) + 40] ^= 1;
		buf[sizeof(struct bpf_hdr) + len] ^= 1;
		CHECK(wire_records(buf, n, frame, len, &count) != 0 && count == 77);
	}
	CHECK(wire_build(frame, 60, mac0, mac1, 99, 0) == 0);
	wire_program(frame, program);
	CHECK(bpf_filter(program, frame, 60, 60) == 9018);
	for (unsigned int i = 0; i < 30; i++) {
		frame[i] ^= 1;
		CHECK(bpf_filter(program, frame, 60, 60) == 0);
		frame[i] ^= 1;
	}
	for (unsigned int i = 0; i < 30; i++)
		CHECK(bpf_filter(program, frame, i, i) == 0);
	CHECK(wire_build(frame, 59, mac0, mac1, 99, 0) == EINVAL);
	CHECK(wire_build(frame, 9015, mac0, mac1, 99, 0) == EINVAL);
	CHECK(wire_build(frame, 60, mac0, mac1, 99, 0) == 0);
	(void)record(buf, frame, 60);
	for (size_t i = 0; i < sizeof(struct bpf_hdr) + 64; i++) {
		count = 77;
		CHECK(wire_records(buf, i, frame, 60, &count) != 0 && count == 77);
	}
	for (unsigned int kind = 0; kind < 6; kind++) {
		struct bpf_hdr h;
		n = record(buf, frame, 60);
		memcpy(&h, buf, sizeof(h));
		switch (kind) {
		case 0: h.bh_hdrlen = 0; break;
		case 1: h.bh_hdrlen = UINT16_MAX; break;
		case 2: h.bh_caplen = 60; break;
		case 3: h.bh_datalen = 60; break;
		case 4: h.bh_caplen = UINT32_MAX; break;
		case 5: h.bh_datalen = UINT32_MAX; break;
		}
		memcpy(buf, &h, sizeof(h));
		count = 77;
		CHECK(wire_records(buf, n, frame, 60, &count) != 0 && count == 77);
	}
	n = record(buf, frame, 60);
	memcpy(buf + n, buf, n);
	count = 77;
	CHECK(wire_records(buf, n * 2, frame, 60, &count) != 0 && count == 77);
	/* Real record parser must tolerate an unaligned read buffer. */
	n = record(buf + 1, frame, 60);
	CHECK(wire_records(buf + 1, n, frame, 60, &count) == 0 && count == 1);
	(void)record(buf, frame, 60);
	/* Native Ethernet bpf_hdrlen is 34 on this LP64 profile. */
	memmove(buf + 34, buf + sizeof(struct bpf_hdr), 64);
	{
		struct bpf_hdr h;
		memcpy(&h, buf, sizeof(h));
		h.bh_hdrlen = 34;
		memcpy(buf, &h, sizeof(h));
	}
	CHECK(wire_records(buf, 98, frame, 60, &count) == 0 && count == 1);
	/* A plausible short header must fail even with matching packet bytes. */
	memmove(buf + 31, buf + 34, 64);
	{
		struct bpf_hdr h;
		memcpy(&h, buf, sizeof(h));
		h.bh_hdrlen = 31;
		memcpy(buf, &h, offsetof(struct bpf_hdr, bh_hdrlen) + 2);
	}
	count = 77;
	CHECK(wire_records(buf, 95, frame, 60, &count) == EPROTO && count == 77);
	CHECK(wire_final(16, 0, 16, 10, 20, 10, 20) == 0);
	CHECK(wire_final(15, 0, 16, 10, 20, 10, 20) == ENOBUFS);
	CHECK(wire_final(17, 0, 16, 10, 20, 10, 20) == ENOBUFS);
	CHECK(wire_final(16, 1, 16, 10, 20, 10, 20) == ENOBUFS);
	CHECK(wire_final(16, 0, 16, 10, 20, 11, 20) == EIO);
	CHECK(wire_final(16, 0, 16, 10, 20, 10, 21) == EIO);
	CHECK(wire_final(16, 0, 16, UINT64_MAX, 20, 0, 20) == EIO);
	CHECK(wire_end(100, 3000000000ULL, &deadline) == 0 && deadline == 3000000100ULL);
	deadline = 77;
	CHECK(wire_end(UINT64_MAX - 2, 3, &deadline) == EOVERFLOW && deadline == 77);
	CHECK(wire_wait_ms(100, 100, 200, &ms) == 0 && ms == 1);
	CHECK(wire_wait_ms(199, 100, 200, &ms) == 0 && ms == 1);
	ms = 77;
	CHECK(wire_wait_ms(99, 100, 200, &ms) == EIO && ms == 77);
	CHECK(wire_wait_ms(200, 100, 200, &ms) == ETIMEDOUT && ms == 77);
	CHECK(wire_count("8", &value) == 0 && value == 8);
	CHECK(wire_count("65536", &value) == 0 && value == 65536);
	for (unsigned int i = 0; i < 7; i++) {
		static const char *bad[] = { "0", "7", "9", "65544", "-8", "8x", "999999999999999999999" };
		value = 77;
		CHECK(wire_count(bad[i], &value) == EINVAL && value == 77);
	}
	printf("rge-wire: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
