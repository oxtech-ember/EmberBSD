/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD experimental GE8300 metadata policy, AI-assisted. */
#include "pvr_ge8300.h"
#include "pvr_fw_private.h"
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/sha2.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <openssl/sha.h>
#include <string.h>
#endif

int
pvr_ge8300_fwinfo_parse(const void *bytes, size_t size, uint64_t bvnc,
    struct pvr_fwinfo *out)
{
	static const uint8_t pin[32] = {
		0xb5, 0x71, 0xcd, 0xd9, 0x03, 0x12, 0xc2, 0x0f,
		0xe8, 0x7f, 0x14, 0xaa, 0xe4, 0x34, 0x84, 0xf7,
		0x27, 0x98, 0x59, 0x92, 0x1a, 0xe3, 0xab, 0xe5,
		0x8e, 0x93, 0x54, 0x12, 0x04, 0x0c, 0x7f, 0x98
	};
	uint8_t digest[32];
	struct pvr_fwinfo info;
	int error;
#ifdef _KERNEL
	SHA256_CTX ctx;
#endif
	if (bytes == NULL || out == NULL || size != 118784)
		return EINVAL;
	if (bvnc != PVR_GE8300_BVNC)
		return ENODEV;
#ifdef _KERNEL
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, bytes, size);
	SHA256_Final(digest, &ctx);
#else
	if (SHA256(bytes, size, digest) == NULL)
		return EINVAL;
#endif
	if (memcmp(pin, digest, sizeof(pin)) != 0)
		return EOPNOTSUPP;
	error = pvr_fwinfo_parse_profile(bytes, size, bvnc, &info, 1);
	if (error != 0)
		return error;
	if (info.version_major != 1 || info.version_minor != 1 ||
	    info.version_build != 6976702 || info.layout_count != 6 ||
	    info.brn_mask != UINT64_C(0x54000610) ||
	    info.ern_mask != UINT64_C(0x1a5) ||
	    info.feature_mask[0] != UINT64_C(0x9219dd57fdb6005b) ||
	    info.feature_mask[1] != UINT64_C(0x4000000004) ||
	    info.array[0].count != 1 || info.array[1].count != 1 ||
	    info.array[2].count != 2 || info.array[3].count != 22)
		return EOPNOTSUPP;
	*out = info;
	return 0;
}

int
pvr_ge8300_fwimage_prepare(const void *bytes, size_t size, uint64_t bvnc,
    void *code, size_t code_size, void *data, size_t data_size,
    struct pvr_fwimage *out)
{
	return pvr_fwimage_prepare_profile(bytes, size, bvnc, code, code_size,
	    data, data_size, out, pvr_ge8300_fwinfo_parse);
}

int
pvr_ge8300_mips_boot_data(void *out, size_t size, uint64_t stack,
    uint64_t regs, const uint64_t pages[PVR_MIPS_PT_PAGES])
{
	if (regs != UINT64_C(0x01800000))
		return EINVAL;
	return pvr_mips_boot_data(out, size, stack, regs, pages);
}
