/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted CPU contract tests. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_gpu_fwcompute.h"

static unsigned long checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

#ifdef FWCOMPUTE_UPSTREAM_ORACLE
void fwcompute_upstream(const uint32_t [6], const void *, const void *,
    uint32_t, uint32_t, uint32_t, uint32_t, void *, void *, void *, void *,
    void *, uint32_t *);
#endif

/* Synthetic fixture data exercises serialization. It is not GPU code. */
static void
fixture(struct sun60i_a733_gpu_fwcompute_inputs *in, uint32_t *programs)
{
	static const uint32_t sizes[] = { 232, 32, 32768, 4, 4, 32 };
	struct rogue_fwif_cdm_registers_cswitch *s;
	unsigned int i;

	memset(in, 0, sizeof(*in));
	for (i = 0; i < A733_GPU_FWCOMPUTE_OBJECTS; i++) {
		in->fw[i].addr = 0xc0200000U + i * 0x10000U;
		in->fw[i].size = sizes[i];
	}
	for (i = 0; i < A733_GPU_FWCOMPUTE_PROGRAMS; i++) {
		in->program[i].bytes = programs + 16 * i;
		in->program[i].size = 64;
		in->program[i].gpu_va = UINT64_C(0x8000100000) + i * 0x10000;
	}
	s = &in->static_state.ctxswitch_regs;
	s->cdmreg_cdm_context_pds0 = UINT64_C(0x1020304050607080);
	s->cdmreg_cdm_context_pds1 = 0x1234567;
	s->cdmreg_cdm_terminate_pds = UINT64_C(0x2132435465768798);
	s->cdmreg_cdm_terminate_pds1 = 0x7654321;
	s->cdmreg_cdm_resume_pds0 = UINT64_C(0x31425364758697a8);
	s->cdmreg_cdm_context_pds0_b = UINT64_C(0x415263748596a7b8);
	s->cdmreg_cdm_resume_pds0_b = UINT64_C(0x5162738495a6b7c8);
	in->command.regs.tpu_border_colour_table = UINT64_C(0x8000040000);
	in->command.regs.cdm_ctrl_stream_base = in->program[2].gpu_va;
	in->command.regs.cdm_context_state_base_addr = UINT64_C(0x8000050000);
	in->command.regs.cdm_resume_pds1 = 0x1abc;
	in->command.regs.tpu_tag_cdm_ctrl = 0x321;
	in->command.flags = ROGUE_COMPUTE_FLAG_PREVENT_ALL_OVERLAP;
	in->priority = 1;
	in->pid = 0x12345678;
	in->context_id = 0x11223344;
	in->job_id = 0x10203040;
}

static void
reject(const struct sun60i_a733_gpu_fwcompute_inputs *in,
    struct sun60i_a733_gpu_fwcompute_image *out, void *saved, int error)
{
	memset(out, 0xa5, sizeof(*out));
	memcpy(saved, out, sizeof(*out));
	CHECK(sun60i_a733_gpu_fwcompute_prepare(in, out, sizeof(*out)) == error);
	CHECK(memcmp(saved, out, sizeof(*out)) == 0);
}

static void
check_image(const struct sun60i_a733_gpu_fwcompute_inputs *in,
    struct sun60i_a733_gpu_fwcompute_image *out,
    struct sun60i_a733_gpu_fwcompute_image *expected)
{
	const struct rogue_fwif_fwcommoncontext *ctx = &out->context.cdm_context;
	const uint8_t *bytes = (const uint8_t *)out;
	uint32_t fw[6];
	struct rogue_fwif_cmd_compute cmd;
	unsigned int i;

	CHECK(sun60i_a733_gpu_fwcompute_prepare(in, out, sizeof(*out)) == 0);
	CHECK(out->used_bytes == 184 && out->ctl.write_offset == 0);
	CHECK(out->ctl.read_offset == 0 && out->ctl.dep_offset == 0);
	CHECK(out->ctl.wrap_mask == 32767);
	CHECK(ctx->dm == 4 && ctx->max_deadline_ms == 30000);
	CHECK(ctx->pid == in->pid && ctx->server_common_context_id == in->context_id);
	CHECK(ctx->priority == in->priority && ctx->priority_seq_num == 0);
	CHECK(ctx->ccbctl_fw_addr == in->fw[1].addr);
	CHECK(ctx->ccb_fw_addr == in->fw[2].addr);
	CHECK(ctx->context_state_addr == in->fw[3].addr);
	CHECK(ctx->fw_mem_context_fw_addr == in->fw[5].addr);
	CHECK(out->timeline == 0 && out->state.ctx_state_flags == 0);
	CHECK(out->kick.cmd_type == 0x2abc0065);
	CHECK(out->kick.cmd_data.cmd_kick_data.context_fw_addr == in->fw[0].addr);
	CHECK(out->kick.cmd_data.cmd_kick_data.client_woff_update == 184);
	CHECK(out->kick.cmd_data.cmd_kick_data.client_wrap_mask_update == 32767);
	/* Literal little-endian command IDs and boundaries, independent of enums. */
	CHECK(out->cccb[0] == 0xcd && out->cccb[1] == 0x80);
	CHECK(out->cccb[2] == 0xbc && out->cccb[3] == 0x2a);
	CHECK(out->cccb[4] == 96 && out->cccb[5] == 0);
	CHECK(out->cccb[136] == 0xd5 && out->cccb[137] == 0);
	CHECK(out->cccb[138] == 0xbc && out->cccb[139] == 0x2a);
	CHECK(out->cccb[140] == 8 && out->cccb[141] == 0);
	CHECK(out->cccb[180] == 1 && out->cccb[181] == 0);
	for (i = 184; i < sizeof(out->cccb); i++)
		CHECK(out->cccb[i] == 0);
	/* The common record's ABI padding and all reserved command bytes are zero. */
	for (i = 44; i < 48; i++)
		CHECK(out->cccb[i] == 0);
	memcpy(&cmd, out->cccb + 40, sizeof(cmd));
	CHECK(cmd.regs.padding == 0);
	CHECK(memcmp(&out->context.static_compute_context_state,
	    &in->static_state, sizeof(in->static_state)) == 0);
	for (i = 0; i < 6; i++)
		fw[i] = in->fw[i].addr;
#ifdef FWCOMPUTE_UPSTREAM_ORACLE
	memset(expected, 0, sizeof(*expected));
	/* Canonicalize the INPUT independently; never use native output as oracle input. */
	memcpy(&cmd, &in->command, sizeof(cmd));
	memset((uint8_t *)&cmd + sizeof(cmd.common), 0,
	    offsetof(struct rogue_fwif_cmd_compute, regs) - sizeof(cmd.common));
	cmd.regs.padding = 0;
	fwcompute_upstream(fw, &in->static_state, &cmd, in->priority, in->pid,
	    in->context_id, in->job_id, &expected->context, &expected->ctl,
	    &expected->state, expected->cccb, &expected->kick, &expected->used_bytes);
	/* Compare every byte, including all FW ABI and aggregate padding. */
	for (i = 0; i < sizeof(*out); i++)
		CHECK(bytes[i] == ((const uint8_t *)expected)[i]);
#else
	(void)fw;
	(void)bytes;
	(void)expected;
#endif
}

int
main(void)
{
	struct sun60i_a733_gpu_fwcompute_inputs *in, *base;
	struct sun60i_a733_gpu_fwcompute_image *out, *expected;
	uint32_t *programs;
	uint8_t *arena, *snapshot;
	size_t arena_size = sizeof(*out) + sizeof(*in) + 16;
	unsigned int i, j;

	in = malloc(sizeof(*in));
	base = malloc(sizeof(*base));
	out = malloc(sizeof(*out));
	expected = malloc(sizeof(*expected));
	programs = malloc(3 * 64);
	arena = malloc(arena_size);
	snapshot = malloc(arena_size);
	CHECK(in && base && out && expected && programs && arena && snapshot);
	for (i = 0; i < 48; i++)
		programs[i] = 0xfacade00U + i;
	fixture(base, programs);
	*in = *base;
	check_image(in, out, expected);

	/* Large scalar values, all permitted flags, and priority endpoints. */
	for (i = 0; i < 12; i++) {
		*in = *base;
		in->priority = i % 3;
		in->pid = UINT32_MAX - i;
		in->context_id = UINT32_MAX - i * 5;
		in->job_id = UINT32_MAX - i * 7;
		in->command.flags = ((i & 1) ? (1U << 2) : 0) |
		    ((i & 2) ? (1U << 5) : 0);
		check_image(in, out, expected);
	}
	/* Input implicit padding and explicit reserved words must not leak. */
	*in = *base;
	memset((uint8_t *)&in->command + 4, 0xcc, 4);
	in->command.regs.padding = UINT32_MAX;
	check_image(in, out, expected);
	/* Last valid 32-bit FW and 40-bit GPU ranges are accepted. */
	*in = *base;
	in->fw[5].addr = 0xffffffe0U;
	in->program[0].gpu_va = (UINT64_C(1) << 40) - 64;
	check_image(in, out, expected);

	for (i = 0; i < A733_GPU_FWCOMPUTE_OBJECTS; i++) {
		*in = *base; in->fw[i].addr = 0; reject(in, out, expected, EINVAL);
		*in = *base; in->fw[i].addr++; reject(in, out, expected, EINVAL);
		*in = *base; in->fw[i].size--; reject(in, out, expected, EINVAL);
		*in = *base; in->fw[i].size++; reject(in, out, expected, EINVAL);
		if (base->fw[i].size > 8) {
			*in = *base; in->fw[i].addr = 0xfffffff8U;
			reject(in, out, expected, EINVAL);
		}
		for (j = 0; j < i; j++) {
			*in = *base; in->fw[i].addr = in->fw[j].addr;
			reject(in, out, expected, EINVAL);
			*in = *base;
			in->fw[i].addr = in->fw[j].addr + (in->fw[j].size - 1) / 8 * 8;
			reject(in, out, expected, EINVAL);
		}
	}
	for (i = 0; i < A733_GPU_FWCOMPUTE_PROGRAMS; i++) {
		*in = *base; in->program[i].bytes = NULL; reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].size = 0; reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].size = 63; reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].size = UINT64_C(1) << 32;
		reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].bytes = (uint8_t *)programs + 1;
		reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].bytes = (void *)(UINTPTR_MAX - 3);
		reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].gpu_va = 0; reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].gpu_va++; reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].gpu_va = UINT64_C(1) << 40;
		reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].gpu_va = (UINT64_C(1) << 40) - 16;
		reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].bytes = in; reject(in, out, expected, EINVAL);
		*in = *base; in->program[i].bytes = out; reject(in, out, expected, EINVAL);
		for (j = 0; j < i; j++) {
			*in = *base; in->program[i].bytes = in->program[j].bytes;
			reject(in, out, expected, EINVAL);
			*in = *base; in->program[i].gpu_va = in->program[j].gpu_va;
			reject(in, out, expected, EINVAL);
		}
	}
	*in = *base; in->priority = 3; reject(in, out, expected, EINVAL);
	*in = *base; in->command.common.frame_num = 1; reject(in, out, expected, EINVAL);
	*in = *base; memset(&in->static_state, 0, sizeof(in->static_state));
	reject(in, out, expected, EINVAL);
	for (i = 0; i < 32; i++) {
		if (i == 2 || i == 5) continue;
		*in = *base; in->command.flags = 1U << i;
		reject(in, out, expected, EINVAL);
	}
	*in = *base; in->command.regs.cdm_ctrl_stream_base += 4;
	reject(in, out, expected, EINVAL);
	*in = *base; in->command.regs.cdm_context_state_base_addr = 0;
	reject(in, out, expected, EINVAL);
	*in = *base; in->command.regs.cdm_context_state_base_addr++;
	reject(in, out, expected, EINVAL);
	*in = *base; in->command.regs.cdm_context_state_base_addr = UINT64_C(1) << 40;
	reject(in, out, expected, EINVAL);
#define UNSUPPORTED(member) do { *in = *base; in->command.member = 1; \
	reject(in, out, expected, EOPNOTSUPP); } while (0)
	UNSUPPORTED(regs.cdm_cb_queue); UNSUPPORTED(regs.cdm_cb_base);
	UNSUPPORTED(regs.cdm_cb); UNSUPPORTED(regs.tpu); UNSUPPORTED(regs.cdm_item);
	UNSUPPORTED(regs.compute_cluster); UNSUPPORTED(num_temp_regions);
	UNSUPPORTED(stream_start_offset); UNSUPPORTED(execute_count);
#undef UNSUPPORTED
	reject(NULL, out, expected, EINVAL);
	*in = *base;
	CHECK(sun60i_a733_gpu_fwcompute_prepare(in, NULL, sizeof(*out)) == EINVAL);
	memset(arena, 0x9b, arena_size);
	memcpy(snapshot, arena, arena_size);
	CHECK(sun60i_a733_gpu_fwcompute_prepare(in, (void *)(arena + 1), sizeof(*out)) == EINVAL);
	CHECK(memcmp(snapshot, arena, arena_size) == 0);
	CHECK(sun60i_a733_gpu_fwcompute_prepare(in, (void *)arena, sizeof(*out) - 1) == EINVAL);
	CHECK(memcmp(snapshot, arena, arena_size) == 0);
	CHECK(sun60i_a733_gpu_fwcompute_prepare((void *)(UINTPTR_MAX - 7), out, sizeof(*out)) == EINVAL);
	CHECK(sun60i_a733_gpu_fwcompute_prepare(in, (void *)(UINTPTR_MAX - 7), sizeof(*out)) == EINVAL);
	/* Complete and partial input/output overlap must preserve every byte. */
	for (i = 0; i < 2; i++) {
		size_t offset = i == 0 ? 0 : sizeof(*out) - 8;
		memset(arena, 0x53, arena_size);
		memcpy(arena + offset, base, sizeof(*base));
		memcpy(snapshot, arena, arena_size);
		CHECK(sun60i_a733_gpu_fwcompute_prepare((void *)(arena + offset),
		    (void *)arena, sizeof(*out)) == EINVAL);
		CHECK(memcmp(snapshot, arena, arena_size) == 0);
	}
	free(snapshot); free(arena); free(programs); free(expected); free(out); free(base); free(in);
#ifdef FWCOMPUTE_UPSTREAM_ORACLE
	printf("PASS: %lu CPU checks; exact upstream producer oracle; no GPU execution\n", checks);
#else
	printf("PASS: %lu CPU checks; upstream oracle NOT enabled; no GPU execution\n", checks);
#endif
	return 0;
}
