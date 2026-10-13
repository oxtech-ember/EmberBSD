#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-fwcompute-vm.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# FWCOMPUTE_VM_SOURCE selects an earlier encoder for the causal regression.
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    ${FWCOMPUTE_VM_TEST_CFLAGS:--fsanitize=address,undefined} \
    -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwcompute-vm-contract.c" \
    "${FWCOMPUTE_VM_SOURCE:-$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_vm.c}" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mmu.c" \
    -o "$work/fwcompute-vm"
"$work/fwcompute-vm"
