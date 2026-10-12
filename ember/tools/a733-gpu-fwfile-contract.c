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
	header.context_state_va = UINT64_C(0xda00000000);
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
self(void)
{
	struct a733_gpu_fwcompute_file parsed, keep;
	uint8_t *file = malloc(file_size());
	size_t i;

	CHECK(file != NULL);
	build(file);
	memset(&keep, 0x5a, sizeof(keep));
#define REJECT() do { \
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(file, file_size(), BVNC, \
	    &parsed) == EINVAL); \
	CHECK(memcmp(&parsed, &keep, sizeof(parsed)) == 0); \
} while (0)

	CHECK(sun60i_a733_gpu_fwcompute_file_parse(file, file_size(), BVNC,
	    &parsed) == 0);
	CHECK(parsed.payload_bytes == PAYLOAD_BYTES);
	CHECK(parsed.static_offset == STATIC_OFFSET);
	CHECK(parsed.priority == 0 && parsed.pid == 1);
	CHECK(parsed.context_state_va == UINT64_C(0xda00000000));
	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		const struct a733_gpu_fwcompute_file_entry *e =
		    find(&parsed, table[i].role);

		CHECK(e != NULL);
		CHECK(e->va == table[i].va && e->bytes == table[i].bytes);
		CHECK(e->offset == table[i].offset);
		CHECK(e->flags == table[i].flags);
	}
	memcpy(&keep, &parsed, sizeof(keep));

	/* Mutations: every one must fail without touching the output. */
	((uint32_t *)file)[0]++;
	REJECT();
	((uint32_t *)file)[0]--;
	((uint32_t *)file)[1] = 1;
	REJECT();
	((uint32_t *)file)[1] = 2;
	((uint32_t *)file)[4] = 100;
	REJECT();
	((uint32_t *)file)[4] = A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY;
	((uint32_t *)file)[5] = 8;
	REJECT();
	((uint32_t *)file)[5] = 9;
	((uint16_t *)(file + 96 + 2 * 24))[0] = /* duplicate scratch role */
	    A733_GPU_FWCOMPUTE_SCRATCH;
	REJECT();
	((uint16_t *)(file + 96 + 2 * 24))[0] = A733_GPU_FWCOMPUTE_RESUME;
	((uint64_t *)(file + 96 + 1 * 24))[0] = /* resume overlaps scratch */
	    UINT64_C(0x8000000000);
	REJECT();
	((uint64_t *)(file + 96 + 1 * 24))[0] = UINT64_C(0x8000001000);
	((uint32_t *)(file + 96 + 1 * 24 + 8))[0] = 4092;
	REJECT();
	((uint32_t *)(file + 96 + 1 * 24 + 8))[0] = 4096;
	((uint64_t *)(file + 96 + 3 * 24))[0] = UINT64_C(0x8000003004);
	REJECT();
	((uint64_t *)(file + 96 + 3 * 24))[0] = UINT64_C(0x8000003000);
	((uint16_t *)(file + 96 + 3 * 24 + 20))[0] = 0x0004;
	REJECT();
	((uint16_t *)(file + 96 + 3 * 24 + 20))[0] =
	    A733_GPU_FWCOMPUTE_FLAG_RO;
	/* Payload span overlap between two resources. */
	{
		uint32_t save = ((uint32_t *)(file + 96 + 2 * 24 + 8))[0];

		((uint32_t *)(file + 96 + 2 * 24 + 8))[0] =
		    ((uint32_t *)(file + 96 + 3 * 24 + 8))[0];
		REJECT();
		/* cppcheck-suppress selfAssignment restore after the mutation */
		((uint32_t *)(file + 96 + 2 * 24 + 8))[0] = save;
	}
	((uint32_t *)file)[9] = 55;
	REJECT();
	((uint32_t *)file)[9] = 56;
	((uint32_t *)file)[8] = 3;
	REJECT();
	((uint32_t *)file)[8] = 0;
	((uint64_t *)file)[7] = 16;
	REJECT();
	((uint64_t *)file)[7] = UINT64_C(0xda00000000);
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(file, file_size() - 4,
	    BVNC, &parsed) == EINVAL);
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(file, 0, BVNC,
	    &parsed) == EINVAL);
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(NULL, file_size(), BVNC,
	    &parsed) == EINVAL);
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(file, file_size(), BVNC ^ 1,
	    &parsed) == EINVAL);
	printf("PASS: %d checks; version-2 parser and pinned role table\n",
	    checks);
	free(file);
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
	if (argc == 3 && strcmp(argv[1], "artifacts") == 0) {
		artifacts(argv[2]);
		return 0;
	}
	if (argc == 5 && strcmp(argv[1], "check") == 0) {
		check(argv[2], strtoull(argv[3], NULL, 0), argv[4]);
		return 0;
	}
	fprintf(stderr, "usage: %s self | artifacts DIR | check FILE BVNC HEXDIGEST\n",
	    argv[0]);
	return 2;
}
