/* Origin: EmberBSD; pack the A733 compute image into the kernel firmware file. */
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Reads compiler artifacts, writes the packed little-endian firmware file
 * consumed by diagnostic stage 18, and prints the file digest plus the
 * expected serialized-image digest. The serializer runs here with the same
 * deterministic firmware spans and command fields as the kernel stage, so
 * both digests must match the board observation exactly.
 *
 * usage: a733-gpu-compute-firmware OUT BVNC
 *          USC=FILE:OFF:LEN:HEXVA PDS=FILE:OFF:LEN:HEXVA
 *          STREAM=FILE:OFF:LEN:HEXVA STATE=FILE CTX_VA=HEX
 *          [PRIORITY=N] [PID=N] [CONTEXT_ID=N] [JOB_ID=N]
 * A span selects a byte range inside its artifact file. The kernel-owned
 * typed command carries only the stream and context-state addresses.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sun60i_a733_gpu_fwcompute.h"
#include "sun60i_a733_gpu_fwcompute_file.h"

#include "a733-sha256.h"

struct artifact {
	uint8_t *bytes;
	size_t size;
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

static void
read_file(const char *path, size_t name_len, struct artifact *file)
{
	char name[512];
	FILE *f;
	long length = 0;

	if (name_len == 0 || name_len >= sizeof(name))
		die("artifact file name missing or too long");
	memcpy(name, path, name_len);
	name[name_len] = '\0';
	f = fopen(name, "rb");
	if (f == NULL)
		die_errno(name);
	if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0 ||
	    fseek(f, 0, SEEK_SET) != 0)
		die_errno(name);
	file->bytes = malloc(length > 0 ? (size_t)length : 1);
	if (file->bytes == NULL)
		die_errno("malloc");
	if (length > 0 &&
	    fread(file->bytes, 1, (size_t)length, f) != (size_t)length)
		die_errno(name);
	fclose(f);
	file->size = (size_t)length;
}

/* Slice FILE:OFF:LEN:HEXVA out of the already loaded artifact. */
static void
parse_span(const char *spec, const struct artifact *file, struct artifact *span,
    uint64_t *va_out, uint64_t alignment)
{
	unsigned long long off, len, va;
	size_t name_len = 0;
	int used = 0;

	while (spec[name_len] != '\0' && spec[name_len] != ':')
		name_len++;
	if (name_len == 0 || spec[name_len] == '\0')
		die("span must be FILE:OFF:LEN:HEXVA");
	if (sscanf(spec + name_len + 1, "%llu:%llu:%llx%n", &off, &len, &va,
	    &used) != 3 ||
	    (size_t)used != strlen(spec + name_len + 1))
		die("span must be FILE:OFF:LEN:HEXVA");
	if (len == 0 || len % 4 != 0 || off % 4 != 0 || off > file->size ||
	    len > file->size || off + len > file->size)
		die("span range invalid or not four-byte aligned");
	if (va == 0 || va >= (1ULL << 40) || (va & (alignment - 1)) != 0)
		die("span GPU address invalid");
	span->bytes = file->bytes + off;
	span->size = (size_t)len;
	*va_out = va;
}

static void
hex_digest(const uint8_t digest[32], char out[65])
{
	int i;

	for (i = 0; i < 32; i++)
		snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

int
main(int argc, char **argv)
{
	struct a733_gpu_fwcompute_file_header header;
	struct sun60i_a733_gpu_fwcompute_inputs inputs;
	struct sun60i_a733_gpu_fwcompute_image *image;
	struct artifact usc, pds, stream, state;
	struct artifact usc_span, pds_span, stream_span;
	struct sha256_ctx ctx;
	struct artifact whole;
	uint8_t digest[32], *payload;
	char hex[65];
	uint64_t bvnc, usc_va = 0, pds_va = 0, stream_va = 0, ctx_va = 0;
	uint32_t priority = 0, pid = 1, context_id = 0, job_id = 1;
	uint32_t payload_bytes, cursor;
	FILE *out;
	int i;

	if (argc < 8 || argc > 12)
		die("usage: OUT BVNC USC=F:P PDS=F:P STREAM=F:P STATE=FILE "
		    "CTX_VA=HEX [PRIORITY=N] [PID=N] [CONTEXT_ID=N] [JOB_ID=N]");
	if (strncmp(argv[3], "USC=", 4) != 0 ||
	    strncmp(argv[4], "PDS=", 4) != 0 ||
	    strncmp(argv[5], "STREAM=", 7) != 0 ||
	    strncmp(argv[6], "STATE=", 6) != 0 ||
	    strncmp(argv[7], "CTX_VA=", 7) != 0)
		die("expected USC= PDS= STREAM= STATE= CTX_VA= labels");
	for (i = 8; i < argc; i++) {
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

	read_file(argv[3] + 4, strcspn(argv[3] + 4, ":"), &usc);
	read_file(argv[4] + 4, strcspn(argv[4] + 4, ":"), &pds);
	read_file(argv[5] + 7, strcspn(argv[5] + 7, ":"), &stream);
	read_file(argv[6] + 6, strlen(argv[6] + 6), &state);
	if (state.size != A733_GPU_FWCOMPUTE_STATE_BYTES)
		die("static state file must hold exactly 56 bytes");
	parse_span(argv[3] + 4, &usc, &usc_span, &usc_va, 16);
	parse_span(argv[4] + 4, &pds, &pds_span, &pds_va, 16);
	parse_span(argv[5] + 7, &stream, &stream_span, &stream_va, 4);
	ctx_va = strtoull(argv[7] + 7, NULL, 0);
	if (ctx_va == 0 || ctx_va >= (1ULL << 40) || (ctx_va & 15) != 0)
		die("context-state GPU address invalid");

	payload_bytes = (uint32_t)(usc_span.size + pds_span.size +
	    stream_span.size + state.size);
	memset(&header, 0, sizeof(header));
	header.magic = A733_GPU_FWCOMPUTE_FILE_MAGIC;
	header.version = A733_GPU_FWCOMPUTE_FILE_VERSION;
	header.bvnc = bvnc;
	header.payload_bytes = payload_bytes;
	header.header_bytes = A733_GPU_FWCOMPUTE_FILE_HEADER;
	header.usc_offset = 0;
	header.usc_bytes = (uint32_t)usc_span.size;
	header.pds_offset = header.usc_bytes;
	header.pds_bytes = (uint32_t)pds_span.size;
	header.stream_offset = header.pds_offset + header.pds_bytes;
	header.stream_bytes = (uint32_t)stream_span.size;
	header.state_offset = header.stream_offset + header.stream_bytes;
	header.usc_va = usc_va;
	header.pds_va = pds_va;
	header.stream_va = stream_va;
	header.context_state_va = ctx_va;
	header.priority = priority;
	header.pid = pid;
	header.context_id = context_id;
	header.job_id = job_id;

	payload = malloc(payload_bytes);
	if (payload == NULL)
		die_errno("malloc");
	cursor = 0;
	memcpy(payload + cursor, usc_span.bytes, usc_span.size);
	cursor += (uint32_t)usc_span.size;
	memcpy(payload + cursor, pds_span.bytes, pds_span.size);
	cursor += (uint32_t)pds_span.size;
	memcpy(payload + cursor, stream_span.bytes, stream_span.size);
	cursor += (uint32_t)stream_span.size;
	memcpy(payload + cursor, state.bytes, state.size);
	cursor += (uint32_t)state.size;
	if (cursor != payload_bytes)
		die("payload size accounting failed");

	out = fopen(argv[1], "wb");
	if (out == NULL)
		die_errno(argv[1]);
	if (fwrite(&header, sizeof(header), 1, out) != 1 ||
	    fwrite(payload, payload_bytes, 1, out) != 1 ||
	    fclose(out) != 0)
		die_errno(argv[1]);

	/* Validate the packed bytes through the production parser. */
	whole.size = sizeof(header) + payload_bytes;
	whole.bytes = malloc(whole.size);
	if (whole.bytes == NULL)
		die_errno("malloc");
	memcpy(whole.bytes, &header, sizeof(header));
	memcpy(whole.bytes + sizeof(header), payload, payload_bytes);
	{
		struct a733_gpu_fwcompute_file_spans spans;

		if (sun60i_a733_gpu_fwcompute_file_parse(whole.bytes,
		    whole.size, bvnc, &spans) != 0)
			die("packed file failed production validation");
	}

	/* Serialize with the exact kernel-stage inputs and digest the image. */
	image = malloc(sizeof(*image));
	if (image == NULL)
		die_errno("malloc");
	memset(&inputs, 0, sizeof(inputs));
	sun60i_a733_gpu_fwcompute_file_fwspans(inputs.fw);
	inputs.program[A733_GPU_FWCOMPUTE_USC].bytes = payload;
	inputs.program[A733_GPU_FWCOMPUTE_USC].size = usc_span.size;
	inputs.program[A733_GPU_FWCOMPUTE_USC].gpu_va = usc_va;
	inputs.program[A733_GPU_FWCOMPUTE_PDS].bytes = payload +
	    header.pds_offset;
	inputs.program[A733_GPU_FWCOMPUTE_PDS].size = pds_span.size;
	inputs.program[A733_GPU_FWCOMPUTE_PDS].gpu_va = pds_va;
	inputs.program[A733_GPU_FWCOMPUTE_STREAM].bytes = payload +
	    header.stream_offset;
	inputs.program[A733_GPU_FWCOMPUTE_STREAM].size = stream_span.size;
	inputs.program[A733_GPU_FWCOMPUTE_STREAM].gpu_va = stream_va;
	memcpy(&inputs.static_state, state.bytes, state.size);
	inputs.command.regs.cdm_ctrl_stream_base = stream_va;
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

	sha256_init(&ctx);
	sha256_update(&ctx, &header, sizeof(header));
	sha256_update(&ctx, payload, payload_bytes);
	sha256_final(&ctx, digest);
	hex_digest(digest, hex);
	printf("file_sha256=%s bytes=%u\n", hex,
	    (unsigned)whole.size);
	free(image);
	free(whole.bytes);
	free(payload);
	free(state.bytes);
	free(stream.bytes);
	free(pds.bytes);
	free(usc.bytes);
	return 0;
}
