#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; source-only compute stream contract.
set -eu
: "${CC:?Select the native SDK compiler}"
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-fwcompute-cmd.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$CC" -std=c11 -Wall -Wextra -Werror -I"$src/sys/arch/arm/sunxi" \
 "$src/ember/tools/pvr-fwcompute-cmd-contract.c" \
 "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_cmd.c" \
 "$src/sys/dev/powervr/pvr_fwcompute_cmd.c" -o "$work/test"
"$work/test"
