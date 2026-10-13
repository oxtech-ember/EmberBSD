/* Origin: EmberBSD; pack the A733 compute image into the v3 firmware file. */
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Reads the six compiler artifacts, writes the version-3 packed
 * little-endian firmware file with the pinned ten-resource manifest
 * table and payload digest, and prints the file digest, the expected
 * serialized-image digest, and the VM-encoding check. The serializer
 * and the VM encoder run here exactly as the kernel stages run them.
 *
 * usage: a733-gpu-compute-firmware OUT BVNC GENERAL.bin PDS.bin USC.bin
 *          STATE.bin BORDER.bin COMMAND.bin CTX_VA_HEX [PRIORITY=N] [PID=N] [CONTEXT_ID=N]
 *          [JOB_ID=N]
 * CTX_VA_HEX asserts the RESUME resource address. It cannot select another
 * resource. The packer derives the command address from its role table.
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

/* The pinned manifest: artifact area, byte range, GPU address, policy. */
struct pack_resource {
	uint16_t role, flags;
	uint32_t area, offset, bytes;
	uint64_t va;
};
static struct pack_resource table[] = {
	{ A733_GPU_FWCOMPUTE_SCRATCH, A733_GPU_FWCOMPUTE_FLAG_BYPASS,
	  0, 0, 4, UINT64_C(0x8000000000) },
	{ A733_GPU_FWCOMPUTE_RESUME, A733_GPU_FWCOMPUTE_FLAG_BYPASS,
	  0, 4096, A733_GPU_FWCOMPUTE_RESUME_BYTES, UINT64_C(0x8000001000) },
	{ A733_GPU_FWCOMPUTE_IDFWDF_TEXTURE, 0,
	  0, 8192, 128, UINT64_C(0x8000002000) },
	{ A733_GPU_FWCOMPUTE_IDFWDF_SHAREDS, A733_GPU_FWCOMPUTE_FLAG_RO,
	  0, 12288, 40, UINT64_C(0x8000003000) },
	{ A733_GPU_FWCOMPUTE_CDM_STREAM, A733_GPU_FWCOMPUTE_FLAG_RO,
	  0, 16384, 0, UINT64_C(0x8000004000) },
	{ A733_GPU_FWCOMPUTE_CTX_STATE_A, 0,
	  1, 0, 16432, UINT64_C(0xda00000000) },
	{ A733_GPU_FWCOMPUTE_CTX_STATE_B, 0,
	  1, 20480, 16432, UINT64_C(0xda00005000) },
	{ A733_GPU_FWCOMPUTE_PDS_PROGRAMS, A733_GPU_FWCOMPUTE_FLAG_RO,
	  1, 40960, 0, UINT64_C(0xda0000a000) },
	{ A733_GPU_FWCOMPUTE_USC_PROGRAMS, A733_GPU_FWCOMPUTE_FLAG_RO,
	  2, 0, 0, UINT64_C(0xe000000000) },
	{ A733_GPU_FWCOMPUTE_BORDER_TABLE, A733_GPU_FWCOMPUTE_FLAG_RO,
	  3, 0, A733_GPU_FWCOMPUTE_BORDER_BYTES, UINT64_C(0x8000010000) },
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
	if ((unsigned long)length > A733_GPU_FWCOMPUTE_FILE_MAX)
		die("compiler artifact exceeds supported file size");
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

static struct pack_resource *
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
	struct a733_gpu_fwcompute_vm_region regions[A733_GPU_FWCOMPUTE_VM_REGIONS];
	struct a733_gpu_fwcompute_vm_summary summary;
	struct a733_gpu_fwcompute_file parsed;
	struct sha256_ctx ctx;
	uint8_t digest[32], *payload, *whole, state_bytes[56], *command;
	uint8_t vm_pc[4096], vm_pd[5][4096], vm_pt[5][4096], vm_fwc[32];
	void *vm_pdv[5] = { vm_pd[0], vm_pd[1], vm_pd[2], vm_pd[3], vm_pd[4] };
	void *vm_ptv[5] = { vm_pt[0], vm_pt[1], vm_pt[2], vm_pt[3], vm_pt[4] };
	uint64_t vm_pdpa[5] = { 0x500000, 0x501000, 0x502000, 0x503000, 0x504000 };
	uint64_t vm_ptpa[5] = { 0x510000, 0x511000, 0x512000, 0x513000, 0x514000 };
	uint64_t bvnc, ctx_va, next_pa = UINT64_C(0x40000000);
	uint32_t priority = 0, pid = 1, context_id = 0, job_id = 1;
	uint32_t area_base[4], payload_bytes, expected_leaves = 0;
	size_t area_size[4], command_size, total, i;
	uint8_t *area[4];
	FILE *out, *state;
	char hex[65];
	char *ctx_end;

	if (argc < 10 || argc > 14)
		die("usage: OUT BVNC GENERAL PDS USC STATE BORDER COMMAND CTX_VA [KEY=N ...]");
	for (i = 10; (int)i < argc; i++) {
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
	ctx_va = find_role(A733_GPU_FWCOMPUTE_RESUME)->va;
	errno = 0;
	if (strtoull(argv[9], &ctx_end, 0) != ctx_va || errno != 0 ||
	    ctx_end == argv[9] || *ctx_end != '\0')
		die("context-state GPU address must equal the RESUME resource");

	area[0] = read_file(argv[3], &area_size[0]);
	area[1] = read_file(argv[4], &area_size[1]);
	area[2] = read_file(argv[5], &area_size[2]);
	area[3] = read_file(argv[7], &area_size[3]);
	command = read_file(argv[8], &command_size);
	if (command_size != A733_GPU_FWCOMPUTE_COMMAND_BYTES ||
	    area_size[3] != A733_GPU_FWCOMPUTE_BORDER_BYTES)
		die("unsupported command or border artifact size");
	for (i = 0; i < 4; i++) {
		if (area_size[i] < 4 || area_size[i] % 4 != 0 ||
		    area_size[i] > A733_GPU_FWCOMPUTE_FILE_MAX)
			die("invalid compiler artifact size");
	}
	if (area_size[0] <= 16384 || area_size[1] <= 40960)
		die("missing compiler program area");
	find_role(A733_GPU_FWCOMPUTE_CDM_STREAM)->bytes =
	    (uint32_t)(area_size[0] - 16384);
	find_role(A733_GPU_FWCOMPUTE_PDS_PROGRAMS)->bytes =
	    (uint32_t)(area_size[1] - 40960);
	find_role(A733_GPU_FWCOMPUTE_USC_PROGRAMS)->bytes =
	    (uint32_t)area_size[2];
	state = fopen(argv[6], "rb");
	if (state == NULL || fseek(state, 0, SEEK_END) != 0 ||
	    ftell(state) != 56 || fseek(state, 0, SEEK_SET) != 0 ||
	    fread(state_bytes, 1, 56, state) != 56)
		die("static state file must hold exactly 56 bytes");
	fclose(state);

	total = A733_GPU_FWCOMPUTE_STATE_BYTES + command_size;
	for (i = 0; i < 4; i++)
		total += area_size[i];
	if (total > A733_GPU_FWCOMPUTE_FILE_MAX -
	    A733_GPU_FWCOMPUTE_FILE_PREFIX -
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY)
		die("packed image exceeds the supported file size");
	payload_bytes = (uint32_t)total;
	payload = malloc(payload_bytes);
	whole = malloc(A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY +
	    payload_bytes);
	if (payload == NULL || whole == NULL)
		die_errno("malloc");
	total = 0;
	for (i = 0; i < 4; i++) {
		area_base[i] = (uint32_t)total;
		memcpy(payload + total, area[i], area_size[i]);
		total += area_size[i];
	}
	memcpy(payload + total, state_bytes, sizeof(state_bytes));
	memcpy(payload + total + sizeof(state_bytes), command, command_size);

	memset(&header, 0, sizeof(header));
	header.magic = A733_GPU_FWCOMPUTE_FILE_MAGIC;
	header.version = A733_GPU_FWCOMPUTE_FILE_VERSION;
	header.bvnc = bvnc;
	header.payload_bytes = payload_bytes;
	header.header_bytes = A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY;
	header.resources = A733_GPU_FWCOMPUTE_RESOURCES;
	header.static_offset = (uint32_t)total;
	header.command_offset = header.static_offset + sizeof(state_bytes);
	header.command_bytes = (uint32_t)command_size;
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
	if (sun60i_a733_gpu_fwcompute_file_parse(whole,
	    header.header_bytes + payload_bytes, bvnc, &parsed) != 0)
		die("packed file failed production validation");

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

	inputs.command = parsed.command;
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
		regions[i].pa = next_pa;
		regions[i].pages = (table[i].bytes + 4095) / 4096;
		next_pa += (uint64_t)regions[i].pages * 4096;
		expected_leaves += regions[i].pages;
		regions[i].flags =
		    ((table[i].flags & A733_GPU_FWCOMPUTE_FLAG_RO) != 0 ?
		    A733_GPU_FWCOMPUTE_VM_RO : 0) |
		    ((table[i].flags & A733_GPU_FWCOMPUTE_FLAG_BYPASS) != 0 ?
		    A733_GPU_FWCOMPUTE_VM_BYPASS : 0);
	}
	{
		const uint16_t aliases[3] = { A733_GPU_FWCOMPUTE_PDS_PROGRAMS,
		    A733_GPU_FWCOMPUTE_USC_PROGRAMS,
		    A733_GPU_FWCOMPUTE_USC_PROGRAMS };
		const uint64_t va[3] = { UINT64_C(0x1000a000),
		    UINT64_C(0x2000), UINT64_C(0xe000002000) };

		for (i = 0; i < 3; i++) {
			size_t source = (size_t)(find_role(aliases[i]) - table);
			size_t alias = A733_GPU_FWCOMPUTE_RESOURCES + i;

			regions[alias] = regions[source];
			regions[alias].va = va[i];
			expected_leaves += regions[alias].pages;
		}
	}
	memset(vm_pc, 0, sizeof(vm_pc));
	memset(vm_pd, 0, sizeof(vm_pd));
	memset(vm_pt, 0, sizeof(vm_pt));
	memset(vm_fwc, 0, sizeof(vm_fwc));
	if (sun60i_a733_gpu_fwcompute_vm_encode(regions,
	    A733_GPU_FWCOMPUTE_VM_REGIONS, vm_pc, vm_pdv,
	    vm_ptv, vm_pdpa, vm_ptpa, 0x500000, vm_fwc, &summary) != 0 ||
	    summary.windows != 5 || summary.leaves != expected_leaves)
		die("unsupported resource or alias layout for VM encoding");
	printf("vm_check=windows=%u directories=%u leaves=%u\n",
	    summary.windows, summary.directories, summary.leaves);

	sha256_init(&ctx);
	sha256_update(&ctx, whole, header.header_bytes + payload_bytes);
	sha256_final(&ctx, digest);
	hex_digest(digest, hex);
	printf("file_sha256=%s bytes=%u\n", hex,
	    (unsigned)(header.header_bytes + payload_bytes));
	/* Publish only after the parser, serializer, and VM checks pass. */
	out = fopen(argv[1], "wb");
	if (out == NULL)
		die_errno(argv[1]);
	if (fwrite(whole, header.header_bytes + payload_bytes, 1, out) != 1 ||
	    fclose(out) != 0)
		die_errno(argv[1]);
	free(image);
	free(whole);
	free(payload);
	free(area[0]);
	free(area[1]);
	free(area[2]);
	free(area[3]);
	free(command);
	return 0;
}
