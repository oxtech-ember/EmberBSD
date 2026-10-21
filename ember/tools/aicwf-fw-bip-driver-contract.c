/* SPDX-License-Identifier: BSD-2-Clause */
/* AI-assisted actual-source WLAN command and start-order regression. */
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <openssl/evp.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(1); } checks++; } while (0)
#ifndef __BIT
#define __BIT(n) (1U << (n))
#endif
#ifndef __BITS
#define __BITS(a,b) ((~0U << (b)) & (~0U >> (31 - (a))))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define ETHER_ADDR_LEN 6
#define IEEE80211_SAE_CAP_EXTERNAL 1
#define IEEE80211_SAE_CAP_PMF 2
#define IEEE80211_SAE_CAP_SA_QUERY 4
#define explicit_memset memset
static unsigned checks;
static uint32_t
check_le32dec(const void *p)
{
	const uint8_t *b = p;
	return (uint32_t)b[0] | (uint32_t)b[1] << 8 |
	    (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static uint16_t
check_le16dec(const void *p)
{
	const uint8_t *b = p;
	return (uint16_t)b[0] | (uint16_t)b[1] << 8;
}
static void
check_le32enc(void *p, uint32_t v)
{
	uint8_t *b = p;
	unsigned n;
	for (n = 0; n < 4; n++)
		b[n] = v >> (n * 8);
}
static void
check_le16enc(void *p, uint16_t v)
{
	uint8_t *b = p;
	b[0] = v;
	b[1] = v >> 8;
}
/* Adapt the actual kernel hash function to a native independent provider. */
typedef struct { EVP_MD_CTX *p; } SHA256_CTX;
static void
SHA256_Init(SHA256_CTX *p)
{
	p->p = EVP_MD_CTX_new();
	CHECK(p->p != NULL);
	CHECK(EVP_DigestInit_ex(p->p, EVP_sha256(), NULL) == 1);
}
static void
SHA256_Update(SHA256_CTX *p, const uint8_t *b, size_t n)
{
	CHECK(EVP_DigestUpdate(p->p, b, n) == 1);
}
static void
SHA256_Final(uint8_t out[32], SHA256_CTX *p)
{
	unsigned n = 0;
	CHECK(EVP_DigestFinal_ex(p->p, out, &n) == 1 && n == 32);
	EVP_MD_CTX_free(p->p);
}
#define le32dec check_le32dec
#define le16dec check_le16dec
#define le32enc check_le32enc
#define le16enc check_le16enc
#include "if_aicwf_fw_bip.h"
#include "driver-values.h"
struct aicwf_softc {
	void *sc_dev;
	bool sc_fw_bip_pending, sc_fw_bip_verified, sc_fw_bip_ready;
	bool sc_fw_bip_wstatus_valid, sc_fw_bip_bootstatus_valid;
	uint32_t sc_fw_bip_wstatus, sc_fw_bip_bootstatus;
	bool sc_5ghz;
	uint16_t sc_sae_caps;
	uint32_t sc_fw_version;
	uint8_t sc_enaddr[6], sc_vif;
};
static uint8_t original[341200], ram[341200];
static uint32_t descriptor[16], pairs[4];
static bool seen[181];
static unsigned code_reads, starts, mem_writes, command_count, strict_calls;
static uint16_t short_id, error_id;
static uint32_t bad_addr, bad_echo, bad_write, corrupt_code;
static uint32_t mac_version, features;
static bool add_if_error, unknown_image;
static uint8_t add_if_vif;

static void
print(void *dev, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
}
#define aprint_normal_dev print
#define aprint_verbose_dev print
#define aprint_error_dev print
static const char *
ether_sprintf(const uint8_t *p)
{
	(void)p;
	return "not logged";
}
static uint32_t *
metadata(uint32_t a)
{
	if (a >= 0x174000 && a < 0x174040 && (a & 3) == 0)
		return &descriptor[(a - 0x174000) / 4];
	if (a >= 0x179718 && a < 0x179728 && (a & 3) == 0)
		return &pairs[(a - 0x179718) / 4];
	CHECK(false);
	return NULL;
}
static uint32_t
memory_read(uint32_t a)
{
	unsigned n;

	if (a >= 0x120000 && a <= 0x1734cc && (a & 3) == 0) {
		if (a >= 0x142be0 && a < 0x142d34) {
			n = (a - 0x142be0) / 4;
			CHECK(!seen[n]);
			seen[n] = true;
			code_reads++;
		} else if (a >= 0x165e48 && a < 0x165fc8) {
			n = 85 + (a - 0x165e48) / 4;
			CHECK(!seen[n]);
			seen[n] = true;
			code_reads++;
		}
		return check_le32dec(ram + (a - 0x120000)) ^ (a == bad_addr);
	}
	return *metadata(a) ^ (a == bad_addr);
}
static int
reply(struct aicwf_softc *sc, uint16_t id, const void *p, size_t len,
    uint16_t cfm_id, void *cfm, size_t cfm_len, int mode, uint64_t epoch)
{
	const uint8_t *b = p;
	uint32_t a, n, value;
	unsigned i;

	(void)cfm_id;
	CHECK(epoch == 0);
	command_count++;
	if (sc->sc_fw_bip_pending && (id == AICWF_DBG_MEM_READ_REQ ||
	    id == AICWF_DBG_MEM_WRITE_REQ || id == AICWF_DBG_MEM_BLOCK_WRITE_REQ ||
	    id == AICWF_DBG_START_APP_REQ || id == AICWF_MM_VERSION_REQ ||
	    id == AICWF_MM_GET_MAC_ADDR_REQ || id == AICWF_MM_ADD_IF_REQ)) {
		CHECK(mode == 2);
		strict_calls++;
	}
	if (id == error_id)
		return EIO;
	if (id == short_id) {
		CHECK(mode == 2);
		return EPROTO;
	}
	if (cfm_len != 0)
		memset(cfm, 0, cfm_len);
	if (id == AICWF_DBG_MEM_BLOCK_WRITE_REQ) {
		CHECK(len == 1032 && cfm_len == 4 && code_reads == 0);
		a = check_le32dec(b);
		n = check_le32dec(b + 4);
		CHECK(a >= 0x120000 && n <= 1024 && a - 0x120000 <= 341200 - n);
		memcpy(ram + (a - 0x120000), b + 8, n);
		if (corrupt_code >= a && corrupt_code - a < n) {
			size_t offset;

			CHECK(corrupt_code >= 0x120000);
			offset = (size_t)corrupt_code - 0x120000;
			CHECK(offset < sizeof(ram));
			ram[offset] ^= 1;
		}
		check_le32enc(cfm, 0xdeadbeef); /* No undocumented errno assumption. */
	} else if (id == AICWF_DBG_MEM_READ_REQ) {
		CHECK(len == 4 && cfm_len == 8);
		a = check_le32dec(b);
		check_le32enc(cfm, a ^ (a == bad_echo));
		check_le32enc((uint8_t *)cfm + 4, memory_read(a));
	} else if (id == AICWF_DBG_MEM_WRITE_REQ) {
		CHECK(len == 8 && cfm_len == 8 && code_reads == 0);
		a = check_le32dec(b);
		value = check_le32dec(b + 4);
		*metadata(a) = value;
		mem_writes++;
		check_le32enc(cfm, a);
		check_le32enc((uint8_t *)cfm + 4, value ^ (a == bad_write));
	} else if (id == AICWF_DBG_START_APP_REQ) {
		CHECK(len == 8 && cfm_len == 4);
		CHECK(check_le32dec(b) == 0x120000 && check_le32dec(b + 4) == 1);
		if (sc->sc_fw_bip_pending) {
			CHECK(code_reads == 181 && mem_writes == 12);
			CHECK(sc->sc_fw_bip_verified);
			for (i = 0; i < 181; i++)
				CHECK(seen[i]);
			for (i = 12; i < 16; i++)
				CHECK(descriptor[i] == 0);
		}
		starts++;
		check_le32enc(cfm, 0xabcdef01); /* Low byte can be hardware information. */
	} else if (id == AICWF_MM_VERSION_REQ) {
		CHECK(cfm_len == 28 && starts == 1);
		check_le32enc(cfm, mac_version);
		check_le32enc((uint8_t *)cfm + 20, features);
	} else if (id == AICWF_MM_ADD_IF_REQ) {
		CHECK(cfm_len == 2);
		((uint8_t *)cfm)[0] = add_if_error;
		((uint8_t *)cfm)[1] = add_if_vif;
	}
	return 0;
}
static int
aicwf_cmd(struct aicwf_softc *s, uint16_t id, const void *p,
    size_t n, uint16_t c, void *b, size_t z)
{
	return reply(s, id, p, n, c, b, z, 0, 0);
}
static int
aicwf_cmd_reply(struct aicwf_softc *s, uint16_t id, const void *p,
    size_t n, uint16_t c, void *b, size_t z, int mode, uint64_t epoch)
{
	return reply(s, id, p, n, c, b, z, mode, epoch);
}
static int
aicwf_mem_read(struct aicwf_softc *s, uint32_t a, uint32_t *v)
{
	(void)s;
	*v = memory_read(a);
	return 0;
}
static int
aicwf_mem_write(struct aicwf_softc *s, uint32_t a, uint32_t v)
{
	(void)s;
	*metadata(a) = v;
	mem_writes++;
	return 0;
}
static int
aicwf_fw_read(struct aicwf_softc *s, const char *name,
    uint8_t **p, size_t *n)
{
	(void)s;
	(void)name;
	*p = malloc(sizeof(original));
	CHECK(*p != NULL);
	memcpy(*p, original, sizeof(original));
	if (unknown_image)
		(*p)[0] ^= 1;
	*n = sizeof(original);
	return 0;
}
static void
firmware_free(void *p, size_t n)
{
	CHECK(n == sizeof(original));
	free(p);
}
static bool
aicwf_me_config_build(uint8_t *p, uint32_t a, uint32_t b,
    uint32_t c, bool d)
{
	(void)p;
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	return false;
}
static unsigned
aicwf_chan_list(struct aicwf_softc *s, uint8_t *a,
    uint8_t *b, unsigned *n)
{
	(void)s;
	(void)a;
	(void)b;
	*n = 0;
	return 0;
}
#include "driver-body.h"
static void
reset(void)
{
	memset(ram, 0, sizeof(ram));
	memset(descriptor, 0xa5, sizeof(descriptor));
	memset(pairs, 0xa5, sizeof(pairs));
	memset(seen, 0, sizeof(seen));
	code_reads = starts = mem_writes = command_count = strict_calls = 0;
	short_id = error_id = 0xffff;
	bad_addr = bad_echo = bad_write = corrupt_code = 0;
	mac_version = 0x06090101;
	features = AICWF_FEATURE_MFP;
	add_if_error = unknown_image = false;
	add_if_vif = 1;
}
static void
refusal(uint16_t short_reply, uint32_t bad_word, uint32_t echo,
    uint32_t write_echo, uint32_t corrupt, uint16_t transport)
{
	struct aicwf_softc sc = {0};

	reset();
	short_id = short_reply;
	bad_addr = bad_word;
	bad_echo = echo;
	bad_write = write_echo;
	corrupt_code = corrupt;
	error_id = transport;
	CHECK(aicwf_wlan_load(&sc, false) != 0);
	CHECK(starts == 0 && !sc.sc_fw_bip_ready);
}
int
main(int argc, char **argv)
{
	struct aicwf_softc sc;
	FILE *f;
	unsigned n;

	CHECK(argc == 2);
	f = fopen(argv[1], "rb");
	CHECK(f != NULL);
	CHECK(fread(original, 1, sizeof(original), f) == sizeof(original));
	CHECK(fgetc(f) == EOF && fclose(f) == 0);
	reset();
	memset(&sc, 0, sizeof(sc));
	CHECK(aicwf_wlan_load(&sc, false) == 0);
	CHECK(starts == 1 && code_reads == 181 && !sc.sc_fw_bip_ready);
	CHECK(aicwf_fw_init(&sc) == 0 && sc.sc_fw_bip_ready);
	CHECK(sc.sc_fw_bip_wstatus_valid && sc.sc_fw_bip_wstatus == 0xdeadbeef);
	CHECK(sc.sc_fw_bip_bootstatus_valid && sc.sc_fw_bip_bootstatus == 0xabcdef01);
	CHECK(strict_calls > 181);
	for (n = 0; n < 181; n++) {
		uint32_t a, v;
		CHECK(aicwf_fw_bip_word(n, &a, &v));
		refusal(0xffff, a, 0, 0, 0, 0xffff);
	}
	refusal(AICWF_DBG_MEM_READ_REQ, 0, 0, 0, 0, 0xffff);
	refusal(AICWF_DBG_MEM_WRITE_REQ, 0, 0, 0, 0, 0xffff);
	refusal(AICWF_DBG_MEM_BLOCK_WRITE_REQ, 0, 0, 0, 0, 0xffff);
	refusal(0xffff, AICWF_FW_PATCH_DESC, 0, 0, 0, 0xffff);
	refusal(0xffff, 0, AICWF_FW_CONFIG_BASE, 0, 0, 0xffff);
	refusal(0xffff, 0, 0, 0x174000, 0, 0xffff);
	refusal(0xffff, 0, 0, 0, 0x142be0, 0xffff);
	refusal(0xffff, 0, 0, 0, 0, AICWF_DBG_MEM_WRITE_REQ);
	for (n = 0; n < 9; n++) {
		reset();
		memset(&sc, 0, sizeof(sc));
		CHECK(aicwf_wlan_load(&sc, false) == 0);
		if (n == 0)
			features = 0;
		if (n == 1)
			mac_version ^= 1;
		if (n == 2)
			short_id = AICWF_MM_VERSION_REQ;
		if (n == 3)
			add_if_error = true;
		if (n == 4)
			short_id = AICWF_MM_ADD_IF_REQ;
		if (n == 5)
			short_id = AICWF_MM_GET_MAC_ADDR_REQ;
		if (n == 6)
			sc.sc_fw_bip_verified = false;
		if (n == 7)
			sc.sc_fw_bip_pending = false;
		if (n == 8)
			add_if_vif = 4;
		CHECK((aicwf_fw_init(&sc) != 0) == (n <= 5 || n == 8));
		CHECK(!sc.sc_fw_bip_ready);
	}
	for (n = 0; n < 2; n++) {
		reset();
		memset(&sc, 0, sizeof(sc));
		unknown_image = n == 1;
		CHECK(aicwf_wlan_load(&sc, n == 0) == 0);
		CHECK(!sc.sc_fw_bip_pending && !sc.sc_fw_bip_verified);
		CHECK(code_reads == 0 && strict_calls == 0 && starts == 1);
		CHECK(aicwf_fw_init(&sc) == 0 && !sc.sc_fw_bip_ready);
	}
	printf("PASS: %u actual-source loader/order/CFM checks\n", checks);
	return 0;
}
