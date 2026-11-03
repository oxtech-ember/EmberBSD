/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD GE8300 metadata and image contract, AI-assisted. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pvr_ge8300.h"
#include "sun60i_a733_gpu_fwinfo.h"

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)

int
main(int argc, char **argv)
{
	struct pvr_fwinfo info, saved;
	struct pvr_fwimage result, prior;
	unsigned char *image, *code, *data;
	uint8_t boot[64], boot_saved[64];
	uint64_t pages[4] = { 0x200000, 0x201000, 0x202000, 0x203000 };
	FILE *f;
	long n;
	size_t size, code_size, data_size;

	CHECK(argc == 2);
	CHECK((f = fopen(argv[1], "rb")) != NULL);
	CHECK(fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0);
	CHECK(fseek(f, 0, SEEK_SET) == 0);
	size = (size_t)n;
	CHECK((image = malloc(size)) != NULL);
	CHECK(fread(image, 1, size, f) == size);
	CHECK(fclose(f) == 0);
	CHECK(sun60i_a733_gpu_fwinfo_parse(image, size, PVR_GE8300_BVNC,
	    &info) == EOPNOTSUPP);
	CHECK(pvr_ge8300_fwinfo_parse(image, size, PVR_GE8300_BVNC, &info) == 0);
	CHECK(info.layout_count == 6 && info.array[PVR_FW_FEATURE_PARAMS].count == 22);
	CHECK(info.brn_mask == UINT64_C(0x54000610) && info.ern_mask == 0x1a5);
	CHECK(info.feature_mask[1] == UINT64_C(0x4000000004));
	code_size = info.allocation_size[PVR_FW_CODE];
	data_size = info.allocation_size[PVR_FW_DATA];
	CHECK((code = malloc(code_size)) != NULL && (data = malloc(data_size)) != NULL);
	CHECK(pvr_ge8300_fwimage_prepare(image, size, PVR_GE8300_BVNC,
	    code, code_size, data, data_size, &result) == 0);
	CHECK(result.load_count > 0);
	memset(&info, 0xa5, sizeof(info)); saved = info;
	CHECK(pvr_ge8300_fwinfo_parse(image, size, 1, &info) == ENODEV);
	CHECK(memcmp(&info, &saved, sizeof(info)) == 0);
	for (size_t offset = 0; offset < size; offset += 4096) {
		image[offset] ^= 1;
		CHECK(pvr_ge8300_fwinfo_parse(image, size, PVR_GE8300_BVNC,
		    &info) == EOPNOTSUPP);
		CHECK(memcmp(&info, &saved, sizeof(info)) == 0);
		image[offset] ^= 1;
	}
	/* Reject edits to BVNC, ABI, layout, masks and parameter arrays. */
	const size_t mutations[] = { size - 4096 + 16, size - 4096 + 32,
	    size - 4096 + 36, size - 4096 + 48 + 8, size - 8192 + 32,
	    size - 8192 + 40, size - 8192 + 48, size - 8192 + 56,
	    size - 8192 + 64 };
	for (size_t i = 0; i < sizeof(mutations) / sizeof(mutations[0]); i++) {
		image[mutations[i]] ^= 1;
		CHECK(pvr_ge8300_fwinfo_parse(image, size, PVR_GE8300_BVNC,
		    &info) == EOPNOTSUPP);
		CHECK(memcmp(&info, &saved, sizeof(info)) == 0);
		image[mutations[i]] ^= 1;
	}
	memset(code, 0x5a, code_size); memset(data, 0x6b, data_size);
	memset(&result, 0xa5, sizeof(result)); prior = result;
	image[0] ^= 1;
	CHECK(pvr_ge8300_fwimage_prepare(image, size, PVR_GE8300_BVNC,
	    code, code_size, data, data_size, &result) == EOPNOTSUPP);
	CHECK(memcmp(&result, &prior, sizeof(result)) == 0);
	for (size_t i = 0; i < code_size; i++) CHECK(code[i] == 0x5a);
	for (size_t i = 0; i < data_size; i++) CHECK(data[i] == 0x6b);
	image[0] ^= 1;
	CHECK(pvr_ge8300_fwimage_prepare(image, size, PVR_GE8300_BVNC,
	    code, code_size - 1, data, data_size, &result) == ENOSPC);
	CHECK(memcmp(&result, &prior, sizeof(result)) == 0);
	for (size_t i = 0; i < code_size; i++) CHECK(code[i] == 0x5a);
	for (size_t i = 0; i < data_size; i++) CHECK(data[i] == 0x6b);
	CHECK(pvr_ge8300_fwimage_prepare(image, size, PVR_GE8300_BVNC,
	    code, code_size, code, data_size, &result) == EINVAL);
	CHECK(memcmp(&result, &prior, sizeof(result)) == 0);
	memset(boot, 0xa5, sizeof(boot)); memcpy(boot_saved, boot, sizeof(boot));
	CHECK(pvr_ge8300_mips_boot_data(boot, sizeof(boot), 0x300000,
	    0x1900000, pages) == EINVAL);
	CHECK(memcmp(boot, boot_saved, sizeof(boot)) == 0);
	CHECK(pvr_mips_boot_data(boot, sizeof(boot), 0x300000,
	    0x1900001, pages) == EINVAL);
	CHECK(memcmp(boot, boot_saved, sizeof(boot)) == 0);
	CHECK(pvr_mips_boot_data(boot, sizeof(boot), 0x300000,
	    UINT64_C(1) << 36, pages) == EINVAL);
	CHECK(memcmp(boot, boot_saved, sizeof(boot)) == 0);
	CHECK(pvr_mips_boot_data(boot, sizeof(boot), 0x300000,
	    0x1900000, pages) == 0);
	CHECK(pvr_ge8300_mips_boot_data(boot, sizeof(boot), 0x300000,
	    0x1800000, pages) == 0);
	memcpy(boot_saved, boot, sizeof(boot));
	CHECK(pvr_ge8300_mips_boot_data(boot, sizeof(boot), 0x200000,
	    0x1800000, pages) == EEXIST);
	CHECK(memcmp(boot, boot_saved, sizeof(boot)) == 0);
	free(code); free(data); free(image);
	puts("GE8300 exact image metadata and CPU image preparation: PASS");
	return 0;
}
