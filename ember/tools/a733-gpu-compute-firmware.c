/* Origin: EmberBSD; pack the A733 compute image into the v2 firmware file. */
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Reads the four r2 compiler artifacts, writes the version-2 packed
 * little-endian firmware file with the pinned nine-resource manifest
 * table and payload digest, and prints the file digest, the expected
 * serialized-image digest, and the VM-encoding check. The serializer
 * and the VM encoder run here exactly as the kernel stages run them.
 *
 * usage: a733-gpu-compute-firmware OUT BVNC GENERAL.bin PDS.bin USC.bin
 *          STATE.bin CTX_VA_HEX [PRIORITY=N] [PID=N] [CONTEXT_ID=N]
 *          [JOB_ID=N]
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "a733-sha256.h"
#include "sun60i_a733_gpu_fwcompute.h"
#include "sun60i_a733_gpu_fwcompute_file.h"
#include "sun60i_a733_gpu_fwcompute_vm.h"

/* The pinned r2 manifest: artifact area, byte range, GPU address, policy. */
struct pack_resource {
	uint16_t role, flags;
	uint32_t area, offset, bytes;
	uint64_t va;
};
static const struct pack_resource table[] = {
	{ A733_GPU_FWCOMPUTE_SCRATCH, A733_GPU_FWCOMPUTE_FLAG_BYPASS,
	  0, 0, 4, UINT64_C(0x8000000000) },
	{ A733_GPU_FWCOMPUTE_RESUME, A733_GPU_FWCOMPUTE_FLAG_BYPASS,
	  0, 4096, 512, UINT64_C(0x8000001000) },
	{ A733_GPU_FWCOMPUTE_IDFWDF_TEXTURE, 0,
	  0, 8192, 128, UINT64_C(0x8000002000) },
	{ A733_GPU_FWCOMPUTE_IDFWDF_SHAREDS, A733_GPU_FWCOMPUTE_FLAG_RO,
	  0, 12288, 40, UINT64_C(0x8000003000) },
	{ A733_GPU_FWCOMPUTE_CDM_STREAM, A733_GPU_FWCOMPUTE_FLAG_RO,
	  0, 16384, 116, UINT64_C(0x8000004000) },
	{ A733_GPU_FWCOMPUTE_CTX_STATE_A, 0,
	  1, 0, 16432, UINT64_C(0xda00000000) },
	{ A733_GPU_FWCOMPUTE_CTX_STATE_B, 0,
	  1, 20480, 16432, UINT64_C(0xda00005000) },
	{ A733_GPU_FWCOMPUTE_PDS_PROGRAMS, A733_GPU_FWCOMPUTE_FLAG_RO,
	  1, 40960, 720, UINT64_C(0xda0000a000) },
	{ A733_GPU_FWCOMPUTE_USC_PROGRAMS, A733_GPU_FWCOMPUTE_FLAG_RO,
	  2, 0, 1744, UINT64_C(0xe000000000) },
};

static void
die(const char *what)
{
	fprintf(stderr, "a733-gpu-compute-firmware: %s\n", what);
	exit(2);
}

static void
die_errno(const char *what)
{
	perror(what);
	exit(2);
}

static uint8_t *
read_file(const char *path, size_t *size_out)
{
	FILE *f = fopen(path, "rb");
	long length = 0;
	uint8_t *bytes;

	if (f == NULL)
		die_errno(path);
	if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0 ||
	    fseek(f, 0, SEEK_SET) != 0)
		die_errno(path);
	bytes = malloc(length > 0 ? (size_t)length : 1);
	if (bytes == NULL)
		die_errno("malloc");
	if (length > 0 &&
	    fread(bytes, 1, (size_t)length, f) != (size_t)length)
		die_errno(path);
	fclose(f);
	*size_out = (size_t)length;
	return bytes;
}

static void
hex_digest(const uint8_t digest[32], char out[65])
{
	int i;

	for (i = 0; i < 32; i++)
		snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

static const struct pack_resource *
find_role(uint16_t role)
{
	size_t i;

	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (table[i].role == role)
			return &table[i];
	}
	die("internal role table");
	return NULL;
}

int
main(int argc, char **argv)
{
	struct a733_gpu_fwcompute_file_header header;
	struct sun60i_a733_gpu_fwcompute_inputs inputs;
	struct sun60i_a733_gpu_fwcompute_image *image;
	struct a733_gpu_fwcompute_vm_region regions[10];
	struct a733_gpu_fwcompute_vm_summary summary;
	struct sha256_ctx ctx;
	uint8_t digest[32], *payload, *whole, state_bytes[56];
	uint8_t vm_pc[4096], vm_pd[4][4096], vm_pt[4][4096], vm_fwc[32];
	void *vm_pdv[4] = { vm_pd[0], vm_pd[1], vm_pd[2], vm_pd[3] };
	void *vm_ptv[4] = { vm_pt[0], vm_pt[1], vm_pt[2], vm_pt[3] };
	uint64_t vm_pdpa[4] = { 0x500000, 0x501000, 0x502000, 0x503000 };
	uint64_t vm_ptpa[4] = { 0x510000, 0x511000, 0x512000, 0x513000 };
	uint64_t bvnc, ctx_va;
	uint32_t priority = 0, pid = 1, context_id = 0, job_id = 1;
	uint32_t area_base[3], payload_bytes;
	size_t area_size[3], i;
	uint8_t *area[3];
	FILE *out, *state;
	char hex[65];

	if (argc < 8 || argc > 12)
		die("usage: OUT BVNC GENERAL PDS USC STATE CTX_VA [KEY=N ...]");
	for (i = 8; (int)i < argc; i++) {
		if (strncmp(argv[i], "PRIORITY=", 9) == 0)
			priority = (uint32_t)strtoul(argv[i] + 9, NULL, 0);
		else if (strncmp(argv[i], "PID=", 4) == 0)
			pid = (uint32_t)strtoul(argv[i] + 4, NULL, 0);
		else if (strncmp(argv[i], "CONTEXT_ID=", 11) == 0)
			context_id = (uint32_t)strtoul(argv[i] + 11, NULL, 0);
		else if (strncmp(argv[i], "JOB_ID=", 7) == 0)
			job_id = (uint32_t)strtoul(argv[i] + 7, NULL, 0);
		else
			die("unknown KEY=VALUE argument");
	}
	if (priority > 2)
		die("priority must be 0..2");
	bvnc = strtoull(argv[2], NULL, 0);
	ctx_va = strtoull(argv[7], NULL, 0);
	if (ctx_va == 0 || ctx_va >= (1ULL << 40) || (ctx_va & 15) != 0)
		die("context-state GPU address invalid");

	area[0] = read_file(argv[3], &area_size[0]);
	area[1] = read_file(argv[4], &area_size[1]);
	area[2] = read_file(argv[5], &area_size[2]);
	if (area_size[2] != 1744)
		die("USC artifact must hold exactly 1744 bytes");
	state = fopen(argv[6], "rb");
	if (state == NULL || fseek(state, 0, SEEK_END) != 0 ||
	    ftell(state) != 56 || fseek(state, 0, SEEK_SET) != 0 ||
	    fread(state_bytes, 1, 56, state) != 56)
		die("static state file must hold exactly 56 bytes");
	fclose(state);

	payload_bytes = (uint32_t)(area_size[0] + area_size[1] + area_size[2] +
	    A733_GPU_FWCOMPUTE_STATE_BYTES);
	payload = malloc(payload_bytes);
	whole = malloc(A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY +
	    payload_bytes);
	if (payload == NULL || whole == NULL)
		die_errno("malloc");
	area_base[0] = 0;
	area_base[1] = (uint32_t)area_size[0];
	area_base[2] = area_base[1] + (uint32_t)area_size[1];
	memcpy(payload + area_base[0], area[0], area_size[0]);
	memcpy(payload + area_base[1], area[1], area_size[1]);
	memcpy(payload + area_base[2], area[2], area_size[2]);
	memcpy(payload + area_base[2] + (uint32_t)area_size[2], state_bytes,
	    sizeof(state_bytes));

	memset(&header, 0, sizeof(header));
	header.magic = A733_GPU_FWCOMPUTE_FILE_MAGIC;
	header.version = A733_GPU_FWCOMPUTE_FILE_VERSION;
	header.bvnc = bvnc;
	header.payload_bytes = payload_bytes;
	header.header_bytes = A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY;
	header.resources = A733_GPU_FWCOMPUTE_RESOURCES;
	header.static_offset = area_base[2] + (uint32_t)area_size[2];
	header.static_bytes = A733_GPU_FWCOMPUTE_STATE_BYTES;
	header.priority = priority;
	header.pid = pid;
	header.context_id = context_id;
	header.job_id = job_id;
	header.context_state_va = ctx_va;
	sha256_init(&ctx);
	sha256_update(&ctx, payload, payload_bytes);
	sha256_final(&ctx, digest);
	memcpy(header.payload_digest, digest, 32);
	memcpy(whole, &header, sizeof(header));
	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		const struct pack_resource *r = &table[i];
		struct a733_gpu_fwcompute_file_entry entry;

		if (r->offset + r->bytes > area_size[r->area])
			die("resource range outside its artifact");
		memset(&entry, 0, sizeof(entry));
		entry.va = r->va;
		entry.offset = area_base[r->area] + r->offset;
		entry.bytes = r->bytes;
		entry.role = r->role;
		entry.flags = r->flags;
		memcpy(whole + sizeof(header) + i * sizeof(entry), &entry,
		    sizeof(entry));
	}
	memcpy(whole + header.header_bytes, payload, payload_bytes);
	{
		struct a733_gpu_fwcompute_file parsed;

		if (sun60i_a733_gpu_fwcompute_file_parse(whole,
		    header.header_bytes + payload_bytes, bvnc, &parsed) != 0)
			die("packed file failed production validation");
	}

	out = fopen(argv[1], "wb");
	if (out == NULL)
		die_errno(argv[1]);
	if (fwrite(whole, header.header_bytes + payload_bytes, 1, out) != 1 ||
	    fclose(out) != 0)
		die_errno(argv[1]);

	/* The kernel stages reproduce exactly these checks and digests. */
	image = malloc(sizeof(*image));
	if (image == NULL)
		die_errno("malloc");
	memset(&inputs, 0, sizeof(inputs));
	sun60i_a733_gpu_fwcompute_file_fwspans(inputs.fw);
	{
		const uint16_t roles[3] = { A733_GPU_FWCOMPUTE_USC_PROGRAMS,
			A733_GPU_FWCOMPUTE_PDS_PROGRAMS,
			A733_GPU_FWCOMPUTE_CDM_STREAM };

		for (i = 0; i < 3; i++) {
			const struct pack_resource *r = find_role(roles[i]);

			inputs.program[i].bytes = whole + header.header_bytes +
			    area_base[r->area] + r->offset;
			inputs.program[i].size = r->bytes;
			inputs.program[i].gpu_va = r->va;
		}
	}
	memcpy(&inputs.static_state, whole + header.header_bytes +
	    header.static_offset, A733_GPU_FWCOMPUTE_STATE_BYTES);
	sun60i_a733_gpu_fwcompute_file_relocate(
	    (uint8_t *)&inputs.static_state, UINT64_C(0x10000000));

	inputs.command.regs.cdm_ctrl_stream_base =
	    find_role(A733_GPU_FWCOMPUTE_CDM_STREAM)->va;
	inputs.command.regs.cdm_context_state_base_addr = ctx_va;
	inputs.priority = priority;
	inputs.pid = pid;
	inputs.context_id = context_id;
	inputs.job_id = job_id;
	if (sun60i_a733_gpu_fwcompute_prepare(&inputs, image,
	    sizeof(*image)) != 0)
		die("real compiler spans failed serializer validation");
	sha256_init(&ctx);
	sha256_update(&ctx, image, sizeof(*image));
	sha256_final(&ctx, digest);
	hex_digest(digest, hex);
	printf("image_sha256=%s used=%u\n", hex, (unsigned)image->used_bytes);

	memset(regions, 0, sizeof(regions));
	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		regions[i].va = table[i].va;
		/* Distinct synthetic pages keep the encoding check honest. */
		regions[i].pa = UINT64_C(0x40000000) + (uint64_t)i * 0x10000;
		regions[i].pages = (table[i].bytes + 4095) / 4096;
		regions[i].flags =
		    ((table[i].flags & A733_GPU_FWCOMPUTE_FLAG_RO) != 0 ?
		    A733_GPU_FWCOMPUTE_VM_RO : 0) |
		    ((table[i].flags & A733_GPU_FWCOMPUTE_FLAG_BYPASS) != 0 ?
		    A733_GPU_FWCOMPUTE_VM_BYPASS : 0);
	}
	regions[9].va = UINT64_C(0x1000a000);
	regions[9].pa = regions[7].pa;
	regions[9].pages = 1;
	regions[9].flags = regions[7].flags;
	memset(vm_pc, 0, sizeof(vm_pc));
	memset(vm_pd, 0, sizeof(vm_pd));
	memset(vm_pt, 0, sizeof(vm_pt));
	memset(vm_fwc, 0, sizeof(vm_fwc));
	if (sun60i_a733_gpu_fwcompute_vm_encode(regions, 10, vm_pc, vm_pdv,
	    vm_ptv, vm_pdpa, vm_ptpa, 0x500000, vm_fwc, &summary) != 0 ||
	    summary.windows != 4 || summary.leaves != 18)
		die("resource table failed VM encoding");
	printf("vm_check=windows=%u directories=%u leaves=%u\n",
	    summary.windows, summary.directories, summary.leaves);

	sha256_init(&ctx);
	sha256_update(&ctx, whole, header.header_bytes + payload_bytes);
	sha256_final(&ctx, digest);
	hex_digest(digest, hex);
	printf("file_sha256=%s bytes=%u\n", hex,
	    (unsigned)(header.header_bytes + payload_bytes));
	free(image);
	free(whole);
	free(payload);
	free(area[0]);
	free(area[1]);
	free(area[2]);
	return 0;
}
