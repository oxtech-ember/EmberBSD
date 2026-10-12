#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-fwcompute.XXXXXXXX")
trap 'if [ "${FWCOMPUTE_KEEP_WORK:-0}" != 1 ]; then rm -rf "$work"; fi' EXIT HUP INT TERM
sha256_file()
{
    if command -v sha256 >/dev/null 2>&1; then sha256 -q "$1"
    elif command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
    else shasum -a 256 "$1" | awk '{print $1}'; fi
}
oracle_flag=
oracle_source=
if [ -n "${FWCOMPUTE_UPSTREAM_DIR:-}" ]; then
    while read -r name digest; do
        [ "$(sha256_file "$FWCOMPUTE_UPSTREAM_DIR/$name")" = "$digest" ] || {
            echo "upstream digest mismatch: $name" >&2; exit 1;
        }
        case "$name" in *.h) cp "$FWCOMPUTE_UPSTREAM_DIR/$name" "$work/";; esac
    done <<'HASHES'
pvr_rogue_fwif.h ac934aafc4a129e9af197ebb1a9b9d49d3214ad12951a5aaa0140f9f68608457
pvr_rogue_fwif_common.h 979ae32fdc3c96e4b0ae38e3b288b4230a66d87b7aa07fb7d5f4d5f6fb8cdcce
pvr_rogue_fwif_shared.h c84297ec46c7cbf3f15abf0407cfa78a654af32d902dd79495ae820244c0ad2e
pvr_rogue_fwif_check.h 92745993eb274decb9e4e6806ee912836d16ec8619dee3df23a0b7ad02f5d977
pvr_rogue_fwif_shared_check.h ef0ba4978214c9e42600f831dd341ffb6be4cf8a1f092d68e08900e2ea1fa628
pvr_rogue_fwif_client.h 3abfc8228e208f9d3dfa79b40f783e94ef5d68887e61270f5e6a2d698b8f4a71
pvr_rogue_fwif_client_check.h 5e2c1fff1470167d3bb40c0df0f01ca877a07e16d4cc6da10153e87c1b8bb411
pvr_check.h 8ec73428fea35ea5b71ba020b0a816d07c9e5ef7108e6e0cdd5ae60b6fc6392f
pvr_rogue_defs.h 7c4c6902f7e456694bd3d64608eae48cdfccc223750773f07648de712e2d4ced
pvr_rogue_cr_defs.h 7421bd9524a485e729421fa1452e09d6687d7a34b092c2502a3c1271b0295b7c
pvr_cccb.c 34fec5973a97f04992ad69148f371ab774b805780450edd249ef051c1b754f5f
pvr_cccb.h 5de47a0e4d4502dee6d41647bde4d3c9a330b468b4d7cc40977097ea9c6b893f
pvr_queue.c ee0662ab685f0ad23285b76f1cbd8a938d66a91f89d630bf5682c7d40edb47b3
HASHES
    mkdir "$work/linux"
    cat > "$work/linux/oracle_types.h" <<'C'
#ifndef ORACLE_TYPES_H
#define ORACLE_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef uint64_t aligned_u64 __attribute__((aligned(8)));
#define __aligned(n) __attribute__((aligned(n)))
#define BIT(n) (1UL << (n))
#define BIT_MASK(n) (1UL << ((n) % (8 * sizeof(unsigned long))))
#define ALIGN(x, a) (((x) + (__typeof__(x))((a) - 1)) & ~(__typeof__(x))((a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)
#define SZ_4K 0x1000U
#define SZ_2G 0x80000000U
#define static_assert _Static_assert
#define sizeof_field(t, m) sizeof(((t *)0)->m)
#define flex_array_size(p, m, n) (sizeof((p)->m[0]) * (n))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define WARN_ON(x) ((x) ? (assert(!(x)), 1) : 0)
#endif
C
    for name in bits build_bug compiler kernel sizes types overflow stddef; do
        echo '#include "oracle_types.h"' > "$work/linux/$name.h"
    done
    cat > "$work/oracle.c" <<'C'
/* MIT option: original Imagination Technologies Ltd. source runs below. */
#include <stdlib.h>
#include "pvr_rogue_fwif.h"
#include "pvr_rogue_fwif_client.h"
#include "pvr_rogue_fwif_shared_check.h"
#include "pvr_rogue_fwif_client_check.h"
SIZE_CHECK(struct rogue_fwif_fwcomputecontext, 232);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, static_compute_context_state, 152);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, work_est_ccb_submitted, 208);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, compute_ctx_flags, 212);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, wgp_state, 216);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, wgp_checksum, 220);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, core_mask_a, 224);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, core_mask_b, 228);
struct pvr_fw_object { u32 addr; };
struct pvr_cccb { struct rogue_fwif_cccb_ctl *ctrl; u8 *cccb;
    u32 size, write_offset, wrap_mask, ctrl_fw_addr, cccb_fw_addr; };
struct pvr_context { struct pvr_fw_object *vm_ctx; u32 priority, ctx_id; };
struct pvr_queue { struct pvr_context *ctx; struct pvr_cccb cccb;
    struct pvr_fw_object *reg_state_obj; size_t ctx_offset; u32 type;
    u64 callstack_addr; };
struct pvr_hwrt_data { struct pvr_fw_object *fw_obj; };
struct pvr_device { struct rogue_fwif_kccb_cmd *captured; };
enum { DRM_PVR_JOB_TYPE_GEOMETRY, DRM_PVR_JOB_TYPE_TRANSFER_FRAG,
    DRM_PVR_JOB_TYPE_FRAGMENT, DRM_PVR_JOB_TYPE_COMPUTE };
static u32 oracle_pid;
static unsigned int barriers;
#define current oracle_pid
#define task_tgid_nr(task) (task)
#define wmb() (++barriers)
static struct pvr_fw_object *pvr_vm_get_fw_mem_context(struct pvr_fw_object *obj)
{ return obj; }
static void pvr_fw_object_get_fw_addr(const struct pvr_fw_object *obj, u32 *out)
{ *out = obj->addr; }
static void pvr_fw_object_get_fw_addr_offset(const struct pvr_fw_object *obj, u32 off, u32 *out)
{ *out = obj->addr + off; }
static void pvr_kccb_send_cmd_reserved_powered(struct pvr_device *dev,
    const struct rogue_fwif_kccb_cmd *cmd, const void *slot)
{ assert(slot == NULL); memcpy(dev->captured, cmd, sizeof(*cmd)); }
C
    # Only transport/types are stubbed. Copy exact producer bodies and constants.
    sed -n '/^#define MAX_DEADLINE_MS /p; /^#define CTX_COMPUTE_CCCB_SIZE_LOG2 /p' \
        "$FWCOMPUTE_UPSTREAM_DIR/pvr_queue.c" >> "$work/oracle.c"
    sed -n '/^#define PADDING_COMMAND_SIZE /p' \
        "$FWCOMPUTE_UPSTREAM_DIR/pvr_cccb.h" >> "$work/oracle.c"
    extract()
    {
        awk -v name="$1" -v prefix="$2" '
            /^[a-zA-Z_]/ && $0 ~ "(^| )" name "\\(" {
                if ($0 ~ "^" name "\\(") print prefix;
                emit=1; found++
            }
            emit { print }
            emit && /^}/ { emit=0 }
            END { if (found != 1) exit 1 }
        ' "$FWCOMPUTE_UPSTREAM_DIR/$3" >> "$work/oracle.c"
    }
    extract get_ccb_space 'static u32' pvr_cccb.c
    extract cccb_ctrl_init 'static void' pvr_cccb.c
    extract pvr_cccb_get_size_of_cmd_with_hdr 'static u32' pvr_cccb.h
    extract pvr_cccb_write_command_with_header void pvr_cccb.c
    extract fill_cmd_kick_data '' pvr_cccb.c
    extract pvr_cccb_send_kccb_kick void pvr_cccb.c
    extract get_dm_type '' pvr_queue.c
    extract init_fw_context '' pvr_queue.c
    extract reg_state_init '' pvr_queue.c
    cat >> "$work/oracle.c" <<'C'
/* Outputs use original layouts in this separate TU, not native headers. */
void fwcompute_upstream(const uint32_t fw[6], const void *static_state,
    const void *command, uint32_t priority, uint32_t pid, uint32_t ctxid,
    uint32_t jobid, void *context, void *ctl, void *state, void *cccb,
    void *kick, uint32_t *used)
{
    struct pvr_fw_object vm = { fw[5] }, reg = { fw[3] };
    struct pvr_context ctx = { &vm, priority, ctxid };
    struct pvr_queue queue = { .ctx = &ctx, .reg_state_obj = &reg,
        .type = DRM_PVR_JOB_TYPE_COMPUTE,
        .cccb = { .ctrl = ctl, .cccb = cccb,
            .size = 1U << CTX_COMPUTE_CCCB_SIZE_LOG2,
            .wrap_mask = (1U << CTX_COMPUTE_CCCB_SIZE_LOG2) - 1,
            .ctrl_fw_addr = fw[1], .cccb_fw_addr = fw[2] } };
    struct pvr_device dev = { kick };
    struct rogue_fwif_fwcomputecontext *compute = context;
    struct rogue_fwif_ufo update = { fw[4], 1 };
    struct rogue_fwif_cmd_compute cmd;
    oracle_pid = pid;
    memset(context, 0, sizeof(*compute));
    memset(ctl, 0, sizeof(struct rogue_fwif_cccb_ctl));
    memset(state, 0, sizeof(struct rogue_fwif_compute_ctx_state));
    memset(cccb, 0, queue.cccb.size);
    init_fw_context(&queue, context);
    reg_state_init(state, &queue);
    memcpy(&compute->static_compute_context_state, static_state,
        sizeof(compute->static_compute_context_state));
    memcpy(&cmd, command, sizeof(cmd));
    cccb_ctrl_init(ctl, &queue.cccb);
    pvr_cccb_write_command_with_header(&queue.cccb,
        ROGUE_FWIF_CCB_CMD_TYPE_CDM, sizeof(cmd), &cmd, jobid, jobid);
    pvr_cccb_write_command_with_header(&queue.cccb,
        ROGUE_FWIF_CCB_CMD_TYPE_UPDATE, sizeof(update), &update, 0, 0);
    barriers = 0;
    pvr_cccb_send_kccb_kick(&dev, &queue.cccb, fw[0], NULL);
    assert(barriers == 1);
    *used = queue.cccb.write_offset;
}
C
    oracle_flag=-DFWCOMPUTE_UPSTREAM_ORACLE
    oracle_source="$work/oracle.c"
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWCOMPUTE_TEST_CFLAGS:-} \
    $oracle_flag -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwcompute-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute.c" \
    ${oracle_source:+"$oracle_source"} -o "$work/contract"
"$work/contract"
if [ "${FWCOMPUTE_KEEP_WORK:-0}" = 1 ]; then echo "work=$work"; fi
