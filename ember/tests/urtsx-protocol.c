/* Origin: EmberBSD USB Realtek packet contracts (AI-assisted). */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "urtsx_proto.h"

static void
read_packet(void)
{
	uint8_t bytes[1024];
	struct urtsx_cmd packet;
	size_t length = 0;
	const uint8_t want[] = {
		0x52, 0x54, 0x43, 0x52, 0, 0, 1, 1,
		0x3c, 0x01, 0, 0
	};
	assert(urtsx_cmd_init(&packet, bytes, sizeof(bytes)) == 0);
	assert(urtsx_cmd_add(&packet, 0, 0xfc01, 0, 0) == 0);
	assert(urtsx_cmd_finish(&packet, URTSX_STAGE_CR, &length) == 0);
	assert(length == sizeof(want));
	assert(memcmp(bytes, want, sizeof(want)) == 0);
}

static void
command_fields(void)
{
	uint8_t bytes[32];
	struct urtsx_cmd packet;
	size_t length = 0;
	const uint8_t want[] = {
		0x52, 0x54, 0x43, 0x52, 0, 0, 3, 1,
		0x7d, 0x51, 0x04, 0x04,
		0xbd, 0xb3, 0x80, 0x80,
		0x00, 0x00, 0x12, 0x34
	};
	assert(urtsx_cmd_init(&packet, bytes, sizeof(bytes)) == 0);
	assert(urtsx_cmd_add(&packet, 1, 0xfd51, 4, 4) == 0);
	assert(urtsx_cmd_add(&packet, 2, 0xfdb3, 0x80, 0x80) == 0);
	assert(urtsx_cmd_add(&packet, 0, 0xc000, 0x12, 0x34) == 0);
	assert(urtsx_cmd_finish(&packet, URTSX_STAGE_C, &length) == 0);
	assert(length == sizeof(want));
	assert(memcmp(bytes, want, sizeof(want)) == 0);
}

static void
write_only_omits_response(void)
{
	uint8_t bytes[12];
	struct urtsx_cmd packet;
	size_t length;
	assert(urtsx_cmd_init(&packet, bytes, sizeof(bytes)) == 0);
	assert(urtsx_cmd_add(&packet, 1, 0xfd51, 4, 4) == 0);
	assert(urtsx_cmd_finish(&packet, URTSX_STAGE_CR, &length) == 0);
	assert(bytes[7] == URTSX_STAGE_C);
	assert(urtsx_cmd_response_size(&packet, &length) == 0 && length == 0);
}

static void
bulk_probe_packet(void)
{
	uint8_t bytes[24];
	struct urtsx_cmd packet;
	size_t length;
	const uint8_t want[] = {
		0x52, 0x54, 0x43, 0x52, 0, 0, 4, 1,
		0x3c, 0x01, 0, 0, 0x3d, 0x51, 0, 0,
		0x3d, 0x6f, 0, 0, 0x3d, 0x83, 0, 0
	};
	assert(urtsx_cmd_init(&packet, bytes, sizeof(bytes)) == 0);
	assert(urtsx_cmd_add(&packet, 0, 0xfc01, 0, 0) == 0);
	assert(urtsx_cmd_add(&packet, 0, 0xfd51, 0, 0) == 0);
	assert(urtsx_cmd_add(&packet, 0, 0xfd6f, 0, 0) == 0);
	assert(urtsx_cmd_response_size(&packet, &length) == 0 && length == 4);
	assert(urtsx_cmd_add(&packet, 0, 0xfd83, 0, 0) == 0);
	assert(urtsx_cmd_finish(&packet, URTSX_STAGE_CR, &length) == 0);
	assert(length == sizeof(want) && memcmp(bytes, want, length) == 0);
	assert(urtsx_cmd_response_size(&packet, &length) == 0 && length == 4);
}

static void
bounds(void)
{
	uint8_t bytes[1024], snapshot[1024];
	struct urtsx_cmd packet;
	size_t length = 123;
	assert(urtsx_cmd_init(&packet, bytes, 7) == EINVAL);
	assert(urtsx_cmd_init(&packet, bytes, 1025) == EINVAL);
	assert(urtsx_cmd_init(&packet, bytes, sizeof(bytes)) == 0);
	assert(urtsx_cmd_finish(&packet, URTSX_STAGE_C, &length) == EINVAL);
	assert(length == 123);
	for (unsigned i = 0; i < 254; i++)
		assert(urtsx_cmd_add(&packet, 0, 0xfc01, 0, 0) == 0);
	assert(urtsx_cmd_finish(&packet, URTSX_STAGE_CR, &length) == 0);
	assert(length == 1024 && bytes[5] == 0 && bytes[6] == 254);
	assert(urtsx_cmd_response_size(&packet, &length) == 0 && length == 256);
	memcpy(snapshot, bytes, sizeof(bytes));
	assert(urtsx_cmd_add(&packet, 0, 0xfc01, 0, 0) == ENOSPC);
	assert(memcmp(bytes, snapshot, sizeof(bytes)) == 0);
	assert(packet.count == 254);
	assert(urtsx_cmd_finish(&packet, 0xff, &length) == EINVAL);
	assert(memcmp(bytes, snapshot, sizeof(bytes)) == 0);

	assert(urtsx_cmd_init(&packet, bytes, 12) == 0);
	assert(urtsx_cmd_add(&packet, 0, 0xfc01, 0, 0) == 0);
	assert(urtsx_cmd_add(&packet, 0, 0xfc01, 0, 0) == ENOSPC);
	memcpy(snapshot, bytes, 12);
	assert(urtsx_cmd_add(&packet, 3, 0xfc01, 0, 0) == EINVAL);
	assert(memcmp(bytes, snapshot, 12) == 0);
	assert(packet.count == 1);
	packet.count = SIZE_MAX;
	assert(urtsx_cmd_add(&packet, 0, 0xfc01, 0, 0) == ENOSPC);
}

static void
status_and_control(void)
{
	const uint8_t inserted[] = {1, 0};
	const uint8_t protected[] = {9, 0};
	const uint8_t endian[] = {0xa5, 0x34};
	uint16_t value = 0;
	assert(urtsx_status_decode(inserted, 2, &value) == 0 && value == 1);
	assert(urtsx_status_decode(protected, 2, &value) == 0 && value == 9);
	assert(urtsx_status_decode(endian, 2, &value) == 0 && value == 0x34a5);
	for (size_t n = 0; n <= 3; n++) {
		if (n == 2)
			continue;
		value = 0x1234;
		assert(urtsx_status_decode(inserted, n, &value) == EINVAL);
		assert(value == 0x1234);
	}
	const uint8_t raw_exist[] = {0x04, 0};
	const uint8_t raw_wp[] = {0x24, 0};
	const uint8_t raw_ocp[] = {0x24, 3};
	assert(urtsx_bulk_status_decode(raw_exist, 2, &value) == 0 && value == 1);
	assert(urtsx_bulk_status_decode(raw_wp, 2, &value) == 0 && value == 9);
	assert(urtsx_bulk_status_decode(raw_ocp, 2, &value) == 0 && value == 0x39);
	assert(urtsx_bulk_status_decode(raw_exist, 1, &value) == EINVAL);
	assert(urtsx_ep0_read_value(0xfc01) == 0x01fc);
	assert(urtsx_ep0_read_value(0xfd51) == 0x51fd);
	assert(urtsx_ep0_read_value(0x0001) == 0x0180);
}

static void
sd_host_contract(void)
{
	uint8_t bytes[1024];
	struct urtsx_cmd packet;
	size_t length;
	uint32_t response[4];
	/* CMD8, argument 000001aa, R7: USB addresses and bit order. */
	const uint8_t prefix[] = {
	    'R','T','C','R',0,0,16,1,
	    0x7d,0xa9,0xff,0x48, 0x7d,0xaa,0xff,0,
	    0x7d,0xab,0xff,0, 0x7d,0xac,0xff,1,
	    0x7d,0xad,0xff,0xaa, 0x7d,0xa1,0xff,1,
	    0x7d,0x5d,1,1, 0x7d,0xb3,0xff,0x88,
	    0xbd,0xb3,0x60,0x60
	};
	assert(urtsx_cmd_init(&packet, bytes, sizeof(bytes)) == 0);
	assert(urtsx_sd_command(&packet, 8, 0x1aa, 1, 0) == 0);
	assert(urtsx_cmd_finish(&packet, URTSX_STAGE_CR, &length) == 0);
	assert(memcmp(bytes, prefix, sizeof(prefix)) == 0);
	assert(length == 72 && packet.responses == 8);
	const uint8_t short_response[] = {0x60,8,0x12,0x34,0x56,0x78,0,0};
	assert(urtsx_sd_response(short_response, 8, 1, response) == 0);
	assert(response[0] == 0x12345678 && response[1] == 0);
	uint8_t bad[8];
	memcpy(bad, short_response, sizeof(bad));
	bad[0] = 0x50; /* transfer error */
	assert(urtsx_sd_response(bad, 8, 1, response) == EIO);
	memcpy(bad, short_response, sizeof(bad));
	bad[6] = 0x80; /* response CRC7 */
	assert(urtsx_sd_response(bad, 8, 1, response) == EIO);
	assert(urtsx_sd_response(bad, 8, 5, response) == 0); /* R3 has no CRC */
	bad[7] = 1; /* response timeout */
	assert(urtsx_sd_response(bad, 8, 5, response) == ETIMEDOUT);
	assert(urtsx_sd_response(short_response, 7, 1, response) == EINVAL);
	/* R2 reads fifteen bytes after the header. CRC byte is omitted. */
	const uint8_t long_response[] = {
	    0x60,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,0,0
	};
	assert(urtsx_sd_response(long_response, 20, 2, response) == 0);
	assert(response[0] == 0x0c0d0e0f && response[1] == 0x08090a0b);
	assert(response[2] == 0x04050607 && response[3] == 0x00010203);
	assert(urtsx_cmd_init(&packet, bytes, sizeof(bytes)) == 0);
	assert(urtsx_sd_command(&packet, 17, 0x12345678, 1, 512) == 0);
	assert(bytes[8 + 5*4] == 0x7d && bytes[8 + 5*4+1] == 0xaf);
	assert(bytes[8 + 6*4+3] == 2); /* byte count 0200 */
	assert(bytes[8 + 11*4+3] == 0x8c); /* normal read, not DMA */
	assert(urtsx_cmd_init(&packet, bytes, sizeof(bytes)) == 0);
	assert(urtsx_sd_command(&packet, 17, 0, 1, 513) == EINVAL);
	assert(packet.count == 0);
	struct urtsx_clock clock;
	assert(urtsx_clock_params(400, &clock) == 0);
	assert(clock.sd_div == 0x80 && clock.clk_div == 0x25);
	assert(clock.n == 118 && clock.depth == 1); /* 30MHz / 128 */
	assert(urtsx_clock_params(25000, &clock) == 0);
	assert(clock.sd_div == 0 && clock.clk_div == 0x14);
	assert(clock.n == 98 && clock.depth == 2); /* doubled 25MHz */
	assert(urtsx_clock_params(25001, &clock) == EINVAL);
}

int
main(void)
{
	read_packet();
	command_fields();
	bulk_probe_packet();
	write_only_omits_response();
	bounds();
	status_and_control();
	sd_host_contract();
	puts("urtsx protocol: PASS");
	return 0;
}
