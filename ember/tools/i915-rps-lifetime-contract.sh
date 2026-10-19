#!/bin/sh
# Origin: EmberBSD Jasper Lake failed-GT display regression, 2026-10-19.
# SPDX-License-Identifier: BSD-2-Clause
# Extract the actual i915 GT wakeref and RPS lifetime functions. Model their locks.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/i915-rps.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
gt="$src/sys/external/bsd/drm2/dist/drm/i915/gt"
extract() {
	awk -v name="$1" '
		$0 ~ "^void " name "[(]" && $0 !~ /;[[:space:]]*$/ { copying = 1 }
		copying { print }
		copying && /^}/ { exit }
	' "$2"
}
: > "$work/lifetime-body.h"
for f in intel_rps_init_early intel_rps_fini; do
	extract "$f" "$gt/intel_rps.c" >> "$work/lifetime-body.h"
done
extract intel_gt_pm_fini "$gt/intel_gt_pm.c" >> "$work/lifetime-body.h"
for f in intel_gt_driver_release intel_gt_driver_late_release; do
	extract "$f" "$gt/intel_gt.c" >> "$work/lifetime-body.h"
done
for f in intel_rps_init_early intel_rps_fini intel_gt_pm_fini \
    intel_gt_driver_release intel_gt_driver_late_release; do
	grep -q "^void $f(" "$work/lifetime-body.h"
done
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
    "$src/ember/tools/i915-rps-lifetime-contract.c" -o "$work/contract"
"$work/contract"
