#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD A133 GPU explicit owner contract, AI-assisted.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/a133-gpu-owner.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$src/sys/arch/arm/sunxi" "$src/ember/tools/a133-gpu-owner-contract.c" \
    "$src/sys/arch/arm/sunxi/sun50i_a133_gpu_owner.c" -o "$work/test"
"$work/test"
