/* Origin: EmberBSD; contract for the version-2 packed A733 compute file. */
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * mode "self": structural validation, role table, and parse mutations.
 * mode "check FILE BVNC HEXDIGEST": parse a packed file, serialize with the
 * production inputs from the role table, and require the exact image digest.
 * The payload digest itself is verified by the kernel stage, not here.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "a733-sha256.h"
#include "sun60i_a733_gpu_fwcompute.h"
#include "sun60i_a733_gpu_fwcompute_file.h"

static int checks;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
		exit(1); \
	} \
	checks++; \
} while (0)

#define BVNC	UINT64_C(0x00240038006800b7)

/* The pinned r2 nine-resource layout used by the packer. */
static const struct {
	uint16_t role, flags;
	uint32_t offset, bytes;
	uint64_t va;
} table[] = {
	{ A733_GPU_FWCOMPUTE_SCRATCH, A733_GPU_FWCOMPUTE_FLAG_BYPASS,
	  0, 4, UINT64_C(0x8000000000) },
	{ A733_GPU_FWCOMPUTE_RESUME, A733_GPU_FWCOMPUTE_FLAG_BYPASS,
	  4096, 512, UINT64_C(0x8000001000) },
	{ A733_GPU_FWCOMPUTE_IDFWDF_TEXTURE, 0, 8192, 128,
	  UINT64_C(0x8000002000) },
	{ A733_GPU_FWCOMPUTE_IDFWDF_SHAREDS, A733_GPU_FWCOMPUTE_FLAG_RO,
	  12288, 40, UINT64_C(0x8000003000) },
	{ A733_GPU_FWCOMPUTE_CDM_STREAM, A733_GPU_FWCOMPUTE_FLAG_RO,
	  16384, 116, UINT64_C(0x8000004000) },
	{ A733_GPU_FWCOMPUTE_CTX_STATE_A, 0, 16500, 16432,
	  UINT64_C(0xda00000000) },
	{ A733_GPU_FWCOMPUTE_CTX_STATE_B, 0, 16500 + 20480, 16432,
	  UINT64_C(0xda00005000) },
	{ A733_GPU_FWCOMPUTE_PDS_PROGRAMS, A733_GPU_FWCOMPUTE_FLAG_RO,
	  16500 + 40960, 720, UINT64_C(0xda0000a000) },
	{ A733_GPU_FWCOMPUTE_USC_PROGRAMS, A733_GPU_FWCOMPUTE_FLAG_RO,
	  16500 + 41680, 1744, UINT64_C(0xe000000000) },
};
#define PAYLOAD_BYTES	(16500 + 41680 + 1744 + 56)
#define STATIC_OFFSET	(16500 + 41680 + 1744)

static void
build(uint8_t *file)
{
	struct a733_gpu_fwcompute_file_header header;
	struct sha256_ctx ctx;
	uint8_t digest[32];
	size_t i;

	memset(file, 0xa5, A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY +
	    PAYLOAD_BYTES);
	memset(&header, 0, sizeof(header));
	header.magic = A733_GPU_FWCOMPUTE_FILE_MAGIC;
	header.version = A733_GPU_FWCOMPUTE_FILE_VERSION;
	header.bvnc = BVNC;
	header.payload_bytes = PAYLOAD_BYTES;
	header.header_bytes = A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY;
	header.resources = A733_GPU_FWCOMPUTE_RESOURCES;
	header.static_offset = STATIC_OFFSET;
	header.static_bytes = A733_GPU_FWCOMPUTE_STATE_BYTES;
	header.priority = 0;
	header.pid = 1;
	header.context_id = 0;
	header.job_id = 1;
	header.context_state_va = UINT64_C(0x8000001000);
	/* The static state needs nonzero context-switch PDS registers. */
	for (i = 0; i < 7; i++) {
		uint32_t reg = UINT32_C(0x0da00000) + (uint32_t)(16 * i);

		memcpy(file + header.header_bytes + STATIC_OFFSET + 4 +
		    4 * i, &reg, sizeof(reg));
	}
	sha256_init(&ctx);
	sha256_update(&ctx, file + header.header_bytes, PAYLOAD_BYTES);
	sha256_final(&ctx, digest);
	memcpy(header.payload_digest, digest, 32);
	memcpy(file, &header, sizeof(header));
	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		struct a733_gpu_fwcompute_file_entry entry;

		memset(&entry, 0, sizeof(entry));
		entry.va = table[i].va;
		entry.offset = table[i].offset;
		entry.bytes = table[i].bytes;
		entry.role = table[i].role;
		entry.flags = table[i].flags;
		memcpy(file + sizeof(header) + i * sizeof(entry), &entry,
		    sizeof(entry));
	}
}

static size_t
file_size(void)
{
	return A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY +
	    PAYLOAD_BYTES;
}

static const struct a733_gpu_fwcompute_file_entry *
find(const struct a733_gpu_fwcompute_file *file, uint16_t role)
{
	size_t i;

	for (i = 0; i < A733_GPU_FWCOMPUTE_RESOURCES; i++) {
		if (file->resource[i].role == role)
			return &file->resource[i];
	}
	return NULL;
}

static void
reject(const uint8_t *bytes, size_t size)
{
	struct a733_gpu_fwcompute_file parsed, keep;

	memset(&keep, 0x5a, sizeof(keep));
	memcpy(&parsed, &keep, sizeof(parsed));
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(bytes, size, BVNC,
	    &parsed) == EINVAL);
	CHECK(memcmp(&parsed, &keep, sizeof(parsed)) == 0);
}

static void
self(void)
{
	struct a733_gpu_fwcompute_file parsed;
	struct a733_gpu_fwcompute_file_header h;
	struct a733_gpu_fwcompute_file_entry e, saved;
	const size_t entry_base = sizeof(h);
	const size_t resume_offset = entry_base + sizeof(e);
	uint8_t *file = malloc(file_size());
	size_t i;

	CHECK(file != NULL);
	build(file);
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(file, file_size(), BVNC,
	    &parsed) == 0);
	CHECK(parsed.payload_bytes == PAYLOAD_BYTES);
	CHECK(parsed.static_offset == STATIC_OFFSET);
	CHECK(parsed.priority == 0 && parsed.pid == 1);
	CHECK(parsed.context_state_va == UINT64_C(0x8000001000));
	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		const struct a733_gpu_fwcompute_file_entry *r =
		    find(&parsed, table[i].role);

		CHECK(r != NULL);
		CHECK(r->va == table[i].va && r->bytes == table[i].bytes);
		CHECK(r->offset == table[i].offset);
		CHECK(r->flags == table[i].flags);
	}

	/* Isolate each header mutation so no earlier error masks it. */
	for (i = 0; i < 15; i++) {
		build(file);
		memcpy(&h, file, sizeof(h));
		switch (i) {
		case 0: h.magic++; break;
		case 1: h.version = 1; break;
		case 2: h.payload_bytes -= 4; break;
		case 3: h.header_bytes -= 4; break;
		case 4: h.resources--; break;
		case 5: h.static_offset = PAYLOAD_BYTES; break;
		case 6: h.static_bytes--; break;
		case 7: h.priority = 3; break;
		case 8: h.reserved = 1; break;
		case 9: h.context_state_va = 0; break;
		case 10: h.context_state_va++; break;
		case 11: h.context_state_va = UINT64_C(1) << 40; break;
		case 12: h.context_state_va = UINT64_C(0x9000000000); break;
		case 13: h.context_state_va += 64; break;
		case 14: h.static_offset++; break;
		}
		memcpy(file, &h, sizeof(h));
		reject(file, file_size());
	}

	/* A mapped shared-register buffer is not the CDM resume buffer. */
	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (table[i].role == A733_GPU_FWCOMPUTE_RESUME)
			continue;
		build(file);
		memcpy(&h, file, sizeof(h));
		h.context_state_va = table[i].va;
		memcpy(file, &h, sizeof(h));
		reject(file, file_size());
	}

	/* Every entry mutation starts from an otherwise valid image. */
	for (i = 0; i < 13; i++) {
		build(file);
		memcpy(&e, file + resume_offset, sizeof(e));
		switch (i) {
		case 0: e.role = A733_GPU_FWCOMPUTE_SCRATCH; break;
		case 1: e.role = A733_GPU_FWCOMPUTE_RESOURCES; break;
		case 2: e.flags |= 4; break;
		case 3: e.flags |= A733_GPU_FWCOMPUTE_FLAG_RO; break;
		case 4: e.flags = 0; break;
		case 5: e.bytes = 508; break;
		case 6: e.bytes = 0; break;
		case 7: e.bytes--; break;
		case 8: e.offset++; break;
		case 9: e.offset = 0; break;
		case 10: e.va = UINT64_C(0x8000000000); break;
		case 11: e.va++; break;
		case 12: e.offset = STATIC_OFFSET; break;
		}
		memcpy(file + resume_offset, &e, sizeof(e));
		reject(file, file_size());
	}

	/* Bind by resource role, not table position or a fixed GPU address. */
	build(file);
	memcpy(&h, file, sizeof(h));
	memcpy(&e, file + resume_offset, sizeof(e));
	e.va = UINT64_C(0x9000001000);
	e.bytes = 516;
	h.context_state_va = e.va;
	memcpy(file, &h, sizeof(h));
	memcpy(&saved, file + entry_base, sizeof(saved));
	memcpy(file + entry_base, &e, sizeof(e));
	memcpy(file + resume_offset, &saved, sizeof(saved));
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(file, file_size(), BVNC,
	    &parsed) == 0);
	CHECK(parsed.context_state_va == e.va);
	CHECK(find(&parsed, A733_GPU_FWCOMPUTE_RESUME)->va == e.va);

	build(file);
	reject(file, file_size() - 4);
	reject(file, 0);
	reject(NULL, file_size());
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(file, file_size(), BVNC ^ 1,
	    &parsed) == EINVAL);
	printf("PASS: %d checks; version-2 parser and RESUME role contract\n",
	    checks);
	free(file);
}

static void
reject_file(const char *path)
{
	uint8_t *bytes;
	long size;
	FILE *f;

	f = fopen(path, "rb");
	CHECK(f != NULL);
	CHECK(fseek(f, 0, SEEK_END) == 0);
	size = ftell(f);
	CHECK(size > 0 && size <= A733_GPU_FWCOMPUTE_FILE_MAX);
	CHECK(fseek(f, 0, SEEK_SET) == 0);
	bytes = malloc((size_t)size);
	CHECK(bytes != NULL);
	CHECK(fread(bytes, 1, (size_t)size, f) == (size_t)size);
	CHECK(fclose(f) == 0);
	reject(bytes, (size_t)size);
	free(bytes);
	printf("PASS: %d checks; invalid packed file rejected before mapping\n",
	    checks);
}

static void
check(const char *path, uint64_t bvnc, const char *expected)
{
	struct a733_gpu_fwcompute_file file;
	struct sun60i_a733_gpu_fwcompute_inputs inputs;
	struct sun60i_a733_gpu_fwcompute_image *image;
	const struct a733_gpu_fwcompute_file_entry *role[3];
	uint8_t *bytes, digest[32];
	struct sha256_ctx ctx;
	char body[65];
	size_t size, i;
	FILE *f;

	f = fopen(path, "rb");
	CHECK(f != NULL);
	CHECK(fseek(f, 0, SEEK_END) == 0);
	size = (size_t)ftell(f);
	CHECK(size > 312);
	CHECK(fseek(f, 0, SEEK_SET) == 0);
	bytes = malloc(size);
	CHECK(bytes != NULL);
	CHECK(fread(bytes, 1, size, f) == size);
	fclose(f);
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(bytes, size, bvnc,
	    &file) == 0);
	{
		static const uint16_t program_role[3] = {
			A733_GPU_FWCOMPUTE_USC_PROGRAMS,
			A733_GPU_FWCOMPUTE_PDS_PROGRAMS,
			A733_GPU_FWCOMPUTE_CDM_STREAM
		};

		for (i = 0; i < 3; i++) {
			role[i] = find(&file, program_role[i]);
			CHECK(role[i] != NULL);
		}
	}
	image = malloc(sizeof(*image));
	CHECK(image != NULL);
	memset(&inputs, 0, sizeof(inputs));
	sun60i_a733_gpu_fwcompute_file_fwspans(inputs.fw);
	for (i = 0; i < 3; i++) {
		inputs.program[i].bytes = bytes + file.header_bytes +
		    role[i]->offset;
		inputs.program[i].size = role[i]->bytes;
		inputs.program[i].gpu_va = role[i]->va;
	}
	memcpy(&inputs.static_state, bytes + file.header_bytes +
	    file.static_offset, A733_GPU_FWCOMPUTE_STATE_BYTES);
	sun60i_a733_gpu_fwcompute_file_relocate(
	    (uint8_t *)&inputs.static_state, UINT64_C(0x10000000));
	inputs.command.regs.cdm_ctrl_stream_base = role[2]->va;
	inputs.command.regs.cdm_context_state_base_addr =
	    file.context_state_va;
	inputs.priority = file.priority;
	inputs.pid = file.pid;
	inputs.context_id = file.context_id;
	inputs.job_id = file.job_id;
	CHECK(sun60i_a733_gpu_fwcompute_prepare(&inputs, image,
	    sizeof(*image)) == 0);
	CHECK(image->used_bytes == A733_GPU_FWCOMPUTE_USED_BYTES);
	sha256_init(&ctx);
	sha256_update(&ctx, image, sizeof(*image));
	sha256_final(&ctx, digest);
	for (i = 0; i < 32; i++)
		snprintf(body + 2 * i, 3, "%02x", digest[i]);
	CHECK(strcmp(body, expected) == 0);
	printf("PASS: %d checks; packed file serializes to digest %s\n",
	    checks, body);
	free(image);
	free(bytes);
}

/* Deterministic synthetic artifacts for the packer run. */
static void
artifacts(const char *dir)
{
	struct { const char *name; size_t size; } list[] = {
		{ "general.bin", 16500 }, { "pds.bin", 41680 },
		{ "usc.bin", 1744 }
	};
	uint8_t state[56];
	char path[512];
	size_t i, j;
	FILE *f;

	for (i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, list[i].name);
		f = fopen(path, "wb");
		CHECK(f != NULL);
		for (j = 0; j < list[i].size; j++) {
			uint8_t byte = (uint8_t)(j * (i + 3) + 7);

			CHECK(fwrite(&byte, 1, 1, f) == 1);
		}
		CHECK(fclose(f) == 0);
	}
	memset(state, 0, sizeof(state));
	for (i = 0; i < 7; i++) {
		uint32_t reg = UINT32_C(0x0da00000) + (uint32_t)(16 * i);

		memcpy(state + 4 + 4 * i, &reg, sizeof(reg));
	}
	snprintf(path, sizeof(path), "%s/static-state.bin", dir);
	f = fopen(path, "wb");
	CHECK(f != NULL);
	CHECK(fwrite(state, 1, sizeof(state), f) == sizeof(state));
	CHECK(fclose(f) == 0);
	printf("PASS: synthetic artifacts written to %s\n", dir);
}

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "self") == 0) {
		self();
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "reject") == 0) {
		reject_file(argv[2]);
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "artifacts") == 0) {
		artifacts(argv[2]);
		return 0;
	}
	if (argc == 5 && strcmp(argv[1], "check") == 0) {
		check(argv[2], strtoull(argv[3], NULL, 0), argv[4]);
		return 0;
	}
	fprintf(stderr, "usage: %s self | reject FILE | artifacts DIR | check FILE BVNC HEXDIGEST\n",
	    argv[0]);
	return 2;
}
