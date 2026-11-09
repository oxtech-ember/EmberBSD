/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD Realtek Bluetooth patch loader, AI-assisted. */
/*
 * Realtek Bluetooth patch image format ("Realtech" v1).
 *
 * Layout of a v1 file, all fields little-endian:
 *   signature[8] = "Realtech", fw_version u32, num_patches u16,
 *   chip_id u16[num_patches], patch_len u16[num_patches],
 *   patch_offset u32[num_patches], patch data ...,
 *   extension records ..., ext_sig[4] = 51 04 fd 77.
 * Extension records are read backwards from ext_sig: opcode, len, data[len].
 * Record opcode 0 with len 1 holds the project id. The project id selects the
 * LMP subversion that the file supports.
 * The patch for a controller is the entry with chip_id == rom_version + 1.
 * Its last four bytes are replaced by fw_version. The config file is appended
 * unchanged. The "RTBTCore" v2 format is not supported.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "rtlbt_fw.h"

#define RTLBT_LMP_8723A		0x1200
#define RTLBT_LMP_8723B		0x8723
#define RTLBT_LMP_8821A		0x8821
#define RTLBT_LMP_8761A		0x8761
#define RTLBT_LMP_8703B		0x8703
#define RTLBT_LMP_8822B		0x8822
#define RTLBT_LMP_8852A		0x8852
#define RTLBT_LMP_8851B		0x8851
#define RTLBT_LMP_8922A		0x8922

#define RTLBT_V1_HEADER_LEN	14
#define RTLBT_EXT_SIG_LEN	4
#define RTLBT_PATCH_TAIL_LEN	4

/*
 * ROM identities that accept a patch. Patched firmware reports another one.
 * The RTL8723A ROM takes a raw image without the "Realtech" header; it is
 * not in this table because the loader does not support that format.
 */
static const struct rtlbt_chip rtlbt_chips[] = {
	{ RTLBT_LMP_8723B, 0x000b, 0x06, 0, "rtl8723b", "_fw.bin" },
	{ RTLBT_LMP_8723B, 0x000d, 0x08, RTLBT_CHIP_CONFIG, "rtl8723d",
	    "_fw.bin" },
	{ RTLBT_LMP_8821A, 0x000a, 0x06, 0, "rtl8821a", "_fw.bin" },
	{ RTLBT_LMP_8821A, 0x000c, 0x08, 0, "rtl8821c", "_fw.bin" },
	{ RTLBT_LMP_8761A, 0x000a, 0x06, 0, "rtl8761a", "_fw.bin" },
	{ RTLBT_LMP_8761A, 0x000b, 0x0a, 0, "rtl8761bu", "_fw.bin" },
	{ RTLBT_LMP_8822B, 0x000c, 0x0a, 0, "rtl8822cu", "_fw.bin" },
	{ RTLBT_LMP_8822B, 0x000b, 0x07, RTLBT_CHIP_CONFIG, "rtl8822b",
	    "_fw.bin" },
	{ RTLBT_LMP_8852A, 0x000a, 0x0b, 0, "rtl8852au", "_fw.bin" },
	{ RTLBT_LMP_8852A, 0x000b, 0x0b, 0, "rtl8852bu", "_fw.bin" },
	{ RTLBT_LMP_8852A, 0x000c, 0x0c, 0, "rtl8852cu", "_fw_v2.bin" },
	{ RTLBT_LMP_8851B, 0x000b, 0x0c, 0, "rtl8851bu", "_fw.bin" },
	{ RTLBT_LMP_8922A, 0x000a, 0x0c, 0, "rtl8922au", "_fw.bin" },
	{ RTLBT_LMP_8852A, 0x0087, 0x0c, 0, "rtl8852btu", "_fw.bin" },
};

/* Project id of the firmware file to the LMP subversion it supports. */
static const uint16_t rtlbt_project_lmp[] = {
	[0] = RTLBT_LMP_8723A,
	[1] = RTLBT_LMP_8723B,
	[2] = RTLBT_LMP_8821A,
	[3] = RTLBT_LMP_8761A,
	[7] = RTLBT_LMP_8703B,
	[8] = RTLBT_LMP_8822B,
	[9] = RTLBT_LMP_8723B,
	[10] = RTLBT_LMP_8821A,
	[13] = RTLBT_LMP_8822B,
	[14] = RTLBT_LMP_8761A,
	[18] = RTLBT_LMP_8852A,
	[19] = RTLBT_LMP_8723B,
	[20] = RTLBT_LMP_8852A,
	[25] = RTLBT_LMP_8852A,
	[33] = RTLBT_LMP_8822B,
	[36] = RTLBT_LMP_8851B,
	[44] = RTLBT_LMP_8922A,
	[47] = RTLBT_LMP_8852A,
};

static const uint8_t rtlbt_sig_v1[8] =
    { 'R', 'e', 'a', 'l', 't', 'e', 'c', 'h' };
static const uint8_t rtlbt_sig_v2[8] =
    { 'R', 'T', 'B', 'T', 'C', 'o', 'r', 'e' };
static const uint8_t rtlbt_ext_sig[RTLBT_EXT_SIG_LEN] =
    { 0x51, 0x04, 0xfd, 0x77 };

#define RTLBT_NITEMS(a)	(sizeof(a) / sizeof((a)[0]))

/* Config files are small register lists; this bound rejects wrong files. */
#define RTLBT_CONFIG_MAX	4096

static uint16_t
rd16(const uint8_t *p)
{

	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t
rd32(const uint8_t *p)
{

	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

const struct rtlbt_chip *
rtlbt_chip_lookup(const struct rtlbt_version *v)
{
	size_t i;

	if (v->manufacturer != RTLBT_MANUFACTURER)
		return NULL;
	for (i = 0; i < RTLBT_NITEMS(rtlbt_chips); i++) {
		if (rtlbt_chips[i].lmp_subversion == v->lmp_subversion &&
		    rtlbt_chips[i].hci_revision == v->hci_revision &&
		    rtlbt_chips[i].hci_version == v->hci_version)
			return &rtlbt_chips[i];
	}
	return NULL;
}

/*
 * Find the project id record and return the LMP subversion it names.
 * Return 0 when the trailer is malformed; *why names the defect.
 */
static uint16_t
rtlbt_project_lmp_subversion(const uint8_t *fw, size_t fwlen,
    size_t header_len, const char **why)
{
	const uint8_t *p, *end;
	uint8_t opcode, oplen, project;

	if (fwlen < header_len + RTLBT_EXT_SIG_LEN + 3) {
		*why = "firmware file too small";
		return 0;
	}
	end = fw + fwlen - RTLBT_EXT_SIG_LEN;
	if (memcmp(end, rtlbt_ext_sig, RTLBT_EXT_SIG_LEN) != 0) {
		*why = "invalid extension signature";
		return 0;
	}
	p = end;
	for (;;) {
		if (p - fw < (ptrdiff_t)header_len + 2) {
			*why = "project id record not found";
			return 0;
		}
		opcode = *--p;
		oplen = *--p;
		if (p - fw < (ptrdiff_t)header_len + oplen) {
			*why = "extension record overruns header";
			return 0;
		}
		p -= oplen;
		if (opcode == 0x00) {
			if (oplen != 1) {
				*why = "invalid project id record length";
				return 0;
			}
			project = *p;
			if (project >= RTLBT_NITEMS(rtlbt_project_lmp) ||
			    rtlbt_project_lmp[project] == 0) {
				*why = "unknown project id";
				return 0;
			}
			return rtlbt_project_lmp[project];
		}
		if (opcode == 0xff) {
			*why = "project id record not found";
			return 0;
		}
	}
}

/*
 * Build the download image for one controller.
 * fw/fwlen: the firmware file. cfg/cfglen: the config file or NULL/0.
 * rom_version: result of vendor command 0xfc6d.
 * lmp_subversion: the controller's LMP subversion.
 * Return 0 and fill *out on success; otherwise return an errno value and
 * set *why to a short reason.
 */
int
rtlbt_image_build(const uint8_t *fw, size_t fwlen, const uint8_t *cfg,
    size_t cfglen, uint8_t rom_version, uint16_t lmp_subversion,
    struct rtlbt_image *out, const char **why)
{
	const uint8_t *table;
	size_t i, n, table_len, limit;
	uint32_t offset;
	uint16_t len, fw_lmp;
	uint8_t *data;

	*why = NULL;
	out->data = NULL;
	out->len = 0;
	if (cfglen > RTLBT_CONFIG_MAX) {
		*why = "config file too large";
		return EINVAL;
	}
	if (fwlen < sizeof(rtlbt_sig_v1)) {
		*why = "firmware file too small";
		return EINVAL;
	}
	if (memcmp(fw, rtlbt_sig_v2, sizeof(rtlbt_sig_v2)) == 0) {
		*why = "RTBTCore firmware format is not supported";
		return EOPNOTSUPP;
	}
	if (memcmp(fw, rtlbt_sig_v1, sizeof(rtlbt_sig_v1)) != 0) {
		*why = "unknown firmware signature";
		return EINVAL;
	}
	fw_lmp = rtlbt_project_lmp_subversion(fw, fwlen, RTLBT_V1_HEADER_LEN,
	    why);
	if (fw_lmp == 0)
		return EINVAL;
	if (fw_lmp != lmp_subversion) {
		*why = "firmware is for another chip family";
		return EINVAL;
	}

	n = rd16(fw + 12);
	table = fw + RTLBT_V1_HEADER_LEN;
	table_len = n * 8;
	if (n == 0 || fwlen < RTLBT_V1_HEADER_LEN + table_len +
	    RTLBT_EXT_SIG_LEN + 3) {
		*why = "patch table overruns file";
		return EINVAL;
	}
	for (i = 0; i < n; i++) {
		if (rd16(table + 2 * i) == (uint16_t)rom_version + 1)
			break;
	}
	if (i == n) {
		*why = "no patch for this ROM version";
		return ENOENT;
	}
	len = rd16(table + 2 * n + 2 * i);
	offset = rd32(table + 4 * n + 4 * i);
	/* The patch must sit between the table and the extension records. */
	limit = fwlen - RTLBT_EXT_SIG_LEN;
	if (len < RTLBT_PATCH_TAIL_LEN || offset < RTLBT_V1_HEADER_LEN +
	    table_len || offset > limit || len > limit - offset) {
		*why = "patch entry overruns file";
		return EINVAL;
	}

	data = malloc((size_t)len + cfglen);
	if (data == NULL) {
		*why = "out of memory";
		return ENOMEM;
	}
	memcpy(data, fw + offset, (size_t)len - RTLBT_PATCH_TAIL_LEN);
	memcpy(data + len - RTLBT_PATCH_TAIL_LEN, fw + 8, RTLBT_PATCH_TAIL_LEN);
	if (cfglen != 0)
		memcpy(data + len, cfg, cfglen);
	out->data = data;
	out->len = (size_t)len + cfglen;
	return 0;
}

/*
 * The controller accepts the image as fragments of RTLBT_FRAG_LEN bytes.
 * An image whose size is a multiple of RTLBT_FRAG_LEN ends with an empty
 * fragment. The last fragment always carries RTLBT_FRAG_LAST in its index.
 */
size_t
rtlbt_fragment_count(size_t len)
{

	return len / RTLBT_FRAG_LEN + 1;
}

/*
 * Describe fragment i of an image with len bytes. Return its payload length
 * and set *idx to the index byte and *offset to the payload offset.
 * Indices run 0, 1..0x7f, then wrap to 1.
 */
size_t
rtlbt_fragment(size_t len, size_t i, uint8_t *idx, size_t *offset)
{
	size_t count;

	count = rtlbt_fragment_count(len);
	*idx = i == 0 ? 0 : (uint8_t)((i - 1) % 0x7f + 1);
	*offset = i * RTLBT_FRAG_LEN;
	if (i + 1 == count) {
		*idx |= RTLBT_FRAG_LAST;
		return len % RTLBT_FRAG_LEN;
	}
	return RTLBT_FRAG_LEN;
}
