#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Test the complete submit implementation and real command decoder.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-submit.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_submit.c" > "$work/pvr-submit-body.h"
{ echo int; sed -n '/^dma_fence_chain_get_payload_status(/,/^}/p' \
    "$src/sys/external/bsd/drm2/linux/linux_dma_fence_chain.c"; } \
    > "$work/dma-fence-payload-status-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_drm.h" > "$work/pvr-uapi-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_exec_hwrt.h" \
    > "$work/pvr-exec-hwrt-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_hwrt.h" \
    > "$work/pvr-hwrt-body.h"
drm="$src/sys/external/bsd/drm2/dist"
for unit in entity main fence; do
    sed '/^#include /d' "$drm/drm/scheduler/sched_$unit.c" \
        > "$work/sched-$unit-body.h"
done
sed '/^#include /d' "$drm/include/drm/gpu_scheduler.h" > "$work/gpu-scheduler-body.h"
sed '/^#include /d' "$drm/include/drm/spsc_queue.h" > "$work/spsc-queue-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/include/drm/drm_sched_batch.h" \
    > "$work/drm-sched-batch-api.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/drm/drm_sched_batch.c" \
    > "$work/drm-sched-batch-body.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -Wno-sign-compare -Wno-unused-but-set-variable \
    ${CFLAGS:-} -pthread -I"$work" -I"$src/sys/arch" \
    -I"$src/sys/external/bsd/drm2/powervr" \
    "$src/ember/tools/pvr-submit-contract.c" \
    "$src/sys/external/bsd/drm2/powervr/pvr_stream.c" -o "$work/contract"
"$work/contract"
