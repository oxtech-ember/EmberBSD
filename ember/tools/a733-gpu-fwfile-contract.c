/* Origin: EmberBSD; contract for the packed A733 compute firmware file. */
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * mode "self": span-layout checks and parse rejection mutations.
 * mode "check FILE BVNC HEXDIGEST": parse a packed file, serialize with the
 * production inputs, and require the exact image digest. All checks use the
 * production parser, serializer, and span derivation.
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

static uint32_t
round_up8(uint32_t n)
{
	return (n + 7U) & ~7U;
}

static void
self(void)
{
	struct sun60i_a733_gpu_fwcompute_fwspan fw[A733_GPU_FWCOMPUTE_OBJECTS];
	static const uint32_t sizes[A733_GPU_FWCOMPUTE_OBJECTS] = { 232, 32,
		32768, 4, 4, 32 };
	struct a733_gpu_fwcompute_file_header header;
	struct a733_gpu_fwcompute_file_spans spans;
	uint8_t file[A733_GPU_FWCOMPUTE_FILE_MAX];
	uint32_t payload = 1744 + 720 + 116 + 56;
	uint32_t offset = 0;
	size_t i;

	sun60i_a733_gpu_fwcompute_file_fwspans(fw);
	for (i = 0; i < A733_GPU_FWCOMPUTE_OBJECTS; i++) {
		CHECK(fw[i].size == sizes[i]);
		CHECK(fw[i].addr % 8 == 0);
		CHECK(fw[i].addr == A733_GPU_FWCOMPUTE_SPAN_BASE + offset);
		offset = round_up8(offset + sizes[i]);
	}
	CHECK(fw[0].addr == A733_GPU_FWCOMPUTE_SPAN_BASE);
	CHECK(fw[5].addr + fw[5].size ==
	    A733_GPU_FWCOMPUTE_SPAN_BASE + A733_GPU_FWCOMPUTE_SPAN_BYTES);
	for (i = 0; i < A733_GPU_FWCOMPUTE_OBJECTS; i++) {
		size_t j2;

		for (j2 = 0; j2 < i; j2++) {
			CHECK(fw[i].addr >= fw[j2].addr + fw[j2].size);
			CHECK((uint64_t)fw[i].addr + fw[i].size <= UINT64_C(0x100000000));
		}
	}

	memset(&header, 0, sizeof(header));
	header.magic = A733_GPU_FWCOMPUTE_FILE_MAGIC;
	header.version = A733_GPU_FWCOMPUTE_FILE_VERSION;
	header.bvnc = BVNC;
	header.payload_bytes = payload;
	header.header_bytes = A733_GPU_FWCOMPUTE_FILE_HEADER;
	header.usc_offset = 0;
	header.usc_bytes = 1744;
	header.pds_offset = 1744;
	header.pds_bytes = 720;
	header.stream_offset = 2464;
	header.stream_bytes = 116;
	header.state_offset = 2580;
	header.usc_va = UINT64_C(0xe000000000);
	header.pds_va = UINT64_C(0xda0000a000);
	header.stream_va = UINT64_C(0x8000004000);
	header.context_state_va = UINT64_C(0xda00000000);
	header.priority = 0;
	header.pid = 1;
	header.context_id = 0;
	header.job_id = 1;
	memset(file, 0xa5, sizeof(file));
	memcpy(file, &header, sizeof(header));

	CHECK(sun60i_a733_gpu_fwcompute_file_parse(file,
	    A733_GPU_FWCOMPUTE_FILE_HEADER + payload, BVNC, &spans) == 0);
	CHECK(spans.usc_offset == 0 && spans.usc_bytes == 1744);
	CHECK(spans.pds_offset == 1744 && spans.pds_bytes == 720);
	CHECK(spans.stream_offset == 2464 && spans.stream_bytes == 116);
	CHECK(spans.state_offset == 2580);
	CHECK(spans.usc_va == UINT64_C(0xe000000000));
	CHECK(spans.pds_va == UINT64_C(0xda0000a000));
	CHECK(spans.stream_va == UINT64_C(0x8000004000));
	CHECK(spans.context_state_va == UINT64_C(0xda00000000));
	CHECK(spans.priority == 0 && spans.pid == 1);
	CHECK(spans.context_id == 0 && spans.job_id == 1);

	/* Every mutation must fail without touching the output spans. */
	{
		struct a733_gpu_fwcompute_file_spans keep;

		memset(&keep, 0x5a, sizeof(keep));
		memcpy(&spans, &keep, sizeof(spans));
#define REJECT(label) do { \
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(file, \
	    A733_GPU_FWCOMPUTE_FILE_HEADER + payload, BVNC, &spans) == \
	    EINVAL); \
	CHECK(memcmp(&spans, &keep, sizeof(spans)) == 0); \
	checks++; \
} while (0)
		header.magic++;
		memcpy(file, &header, sizeof(header));
		REJECT("magic");
		header.magic--;
		header.version = 2;
		memcpy(file, &header, sizeof(header));
		REJECT("version");
		header.version = 1;
		header.header_bytes = 96;
		memcpy(file, &header, sizeof(header));
		REJECT("header bytes");
		header.header_bytes = A733_GPU_FWCOMPUTE_FILE_HEADER;
		header.reserved = 1;
		memcpy(file, &header, sizeof(header));
		REJECT("reserved");
		header.reserved = 0;
		header.bvnc ^= 1;
		memcpy(file, &header, sizeof(header));
		REJECT("bvnc");
		header.bvnc = BVNC;
		header.payload_bytes--;
		memcpy(file, &header, sizeof(header));
		REJECT("payload size");
		header.payload_bytes = payload;
		header.usc_bytes = 1743;
		memcpy(file, &header, sizeof(header));
		REJECT("usc length alignment");
		header.usc_bytes = 1744;
		header.usc_offset = 2;
		memcpy(file, &header, sizeof(header));
		REJECT("usc offset alignment");
		header.usc_offset = 0;
		header.usc_bytes = payload;
		memcpy(file, &header, sizeof(header));
		REJECT("usc length range");
		header.usc_bytes = 1744;
		header.pds_offset = 1000;
		memcpy(file, &header, sizeof(header));
		REJECT("file span overlap");
		header.pds_offset = 1744;
		header.usc_va = 0;
		memcpy(file, &header, sizeof(header));
		REJECT("usc address zero");
		header.usc_va = UINT64_C(0xe000000004);
		memcpy(file, &header, sizeof(header));
		REJECT("usc address alignment");
		header.usc_va = UINT64_C(1) << 40;
		memcpy(file, &header, sizeof(header));
		REJECT("usc address range");
		header.usc_va = UINT64_C(0xe000000000);
		header.usc_va = ((UINT64_C(1) << 40) - 4) & ~UINT64_C(3);
		memcpy(file, &header, sizeof(header));
		REJECT("usc address overflow");
		header.usc_va = UINT64_C(0xe000000000);
		header.stream_va = header.usc_va;
		memcpy(file, &header, sizeof(header));
		REJECT("gpu span overlap");
		header.stream_va = UINT64_C(0x8000004002);
		memcpy(file, &header, sizeof(header));
		REJECT("stream address alignment");
		header.stream_va = UINT64_C(0x8000004000);
		header.context_state_va = 0;
		memcpy(file, &header, sizeof(header));
		REJECT("context-state zero");
		header.context_state_va = UINT64_C(0xda00000008);
		memcpy(file, &header, sizeof(header));
		REJECT("context-state alignment");
		header.context_state_va = UINT64_C(0xda00000000) + 116;
		memcpy(file, &header, sizeof(header));
		REJECT("context-state overlap");
		header.context_state_va = UINT64_C(0xda00000000);
		header.priority = 3;
		memcpy(file, &header, sizeof(header));
		REJECT("priority");
		header.priority = 0;
		CHECK(sun60i_a733_gpu_fwcompute_file_parse(file,
		    A733_GPU_FWCOMPUTE_FILE_HEADER, BVNC, &spans) == EINVAL);
		checks++;
		CHECK(sun60i_a733_gpu_fwcompute_file_parse(file, 0, BVNC,
		    &spans) == EINVAL);
		checks++;
		CHECK(sun60i_a733_gpu_fwcompute_file_parse(file,
		    A733_GPU_FWCOMPUTE_FILE_MAX + 4, BVNC, &spans) == EINVAL);
		checks++;
	}
	printf("PASS: %d checks; production parser and span derivation\n",
	    checks);
}

static void
check(const char *path, uint64_t bvnc, const char *expected)
{
	struct a733_gpu_fwcompute_file_spans spans;
	struct sun60i_a733_gpu_fwcompute_inputs inputs;
	struct sun60i_a733_gpu_fwcompute_image *image;
	uint8_t *bytes, digest[32];
	char body[2 * 32 + 1];
	struct sha256_ctx ctx;
	size_t size, i;

	bytes = NULL;
	{
		FILE *f = fopen(path, "rb");
		long length = 0;

		CHECK(f != NULL);
		CHECK(fseek(f, 0, SEEK_END) == 0);
		length = ftell(f);
		CHECK(length > 0);
		CHECK(fseek(f, 0, SEEK_SET) == 0);
		bytes = malloc((size_t)length);
		CHECK(bytes != NULL);
		CHECK(fread(bytes, 1, (size_t)length, f) == (size_t)length);
		fclose(f);
		size = (size_t)length;
	}
	CHECK(sun60i_a733_gpu_fwcompute_file_parse(bytes, size, bvnc,
	    &spans) == 0);
	image = malloc(sizeof(*image));
	CHECK(image != NULL);
	memset(&inputs, 0, sizeof(inputs));
	sun60i_a733_gpu_fwcompute_file_fwspans(inputs.fw);
	inputs.program[A733_GPU_FWCOMPUTE_USC].bytes = bytes +
	    A733_GPU_FWCOMPUTE_FILE_HEADER + spans.usc_offset;
	inputs.program[A733_GPU_FWCOMPUTE_USC].size = spans.usc_bytes;
	inputs.program[A733_GPU_FWCOMPUTE_USC].gpu_va = spans.usc_va;
	inputs.program[A733_GPU_FWCOMPUTE_PDS].bytes = bytes +
	    A733_GPU_FWCOMPUTE_FILE_HEADER + spans.pds_offset;
	inputs.program[A733_GPU_FWCOMPUTE_PDS].size = spans.pds_bytes;
	inputs.program[A733_GPU_FWCOMPUTE_PDS].gpu_va = spans.pds_va;
	inputs.program[A733_GPU_FWCOMPUTE_STREAM].bytes = bytes +
	    A733_GPU_FWCOMPUTE_FILE_HEADER + spans.stream_offset;
	inputs.program[A733_GPU_FWCOMPUTE_STREAM].size = spans.stream_bytes;
	inputs.program[A733_GPU_FWCOMPUTE_STREAM].gpu_va = spans.stream_va;
	memcpy(&inputs.static_state, bytes +
	    A733_GPU_FWCOMPUTE_FILE_HEADER + spans.state_offset,
	    A733_GPU_FWCOMPUTE_STATE_BYTES);
	inputs.command.regs.cdm_ctrl_stream_base = spans.stream_va;
	inputs.command.regs.cdm_context_state_base_addr =
	    spans.context_state_va;
	inputs.priority = spans.priority;
	inputs.pid = spans.pid;
	inputs.context_id = spans.context_id;
	inputs.job_id = spans.job_id;
	CHECK(sun60i_a733_gpu_fwcompute_prepare(&inputs, image,
	    sizeof(*image)) == 0);
	CHECK(image->used_bytes == A733_GPU_FWCOMPUTE_USED_BYTES);
	sha256_init(&ctx);
	sha256_update(&ctx, image, sizeof(*image));
	sha256_final(&ctx, digest);
	for (i = 0; i < sizeof(digest); i++)
		snprintf(body + 2 * i, 3, "%02x", digest[i]);
	CHECK(strcmp(body, expected) == 0);
	printf("PASS: %d checks; packed file serializes to digest %s\n",
	    checks, body);
	free(image);
	free(bytes);
}

/* Deterministic synthetic compiler artifacts for the packer run. */
static void
artifacts(const char *dir)
{
	struct { const char *name; size_t size; uint8_t seed; } list[] = {
		{ "usc.bin", 1744, 3 }, { "pds.bin", 41680, 11 },
		{ "general.bin", 16500, 1 }
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
			uint8_t byte = (uint8_t)(j * (i + 3) + list[i].seed);

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
	fprintf(stderr, "usage: %s self | artifacts DIR | "
	    "check FILE BVNC HEXDIGEST\n", argv[0]);
	return 2;
}
