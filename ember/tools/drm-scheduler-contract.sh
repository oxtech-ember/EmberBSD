#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Execute the real scheduler with host models of kernel boundaries.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
: "${CC:?Select the native compiler through the SDK run-native-check tool}"
work=$(mktemp -d "${TMPDIR:-/tmp}/drm-scheduler.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
drm="$src/sys/external/bsd/drm2/dist"
for unit in entity main fence; do
    sed '/^#include /d' "$drm/drm/scheduler/sched_$unit.c" \
        > "$work/sched-$unit-body.h"
done
sed '/^#include /d' "$drm/include/drm/gpu_scheduler.h" \
    > "$work/gpu-scheduler-body.h"
sed '/^#include /d' "$drm/include/drm/spsc_queue.h" \
    > "$work/spsc-queue-body.h"
# shellcheck disable=SC2086
"$CC" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -Wno-sign-compare -Wno-unused-but-set-variable \
    ${CFLAGS:-} -pthread -I"$work" \
    "$src/ember/tools/drm-scheduler-contract.c" -o "$work/contract"
"$work/contract"
