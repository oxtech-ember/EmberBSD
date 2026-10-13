#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EmberBSD contributors. AI-assisted ABI checks.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -ne 0 ]; then
    echo "usage: $0" >&2
    exit 2
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-fwabi.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWABI_TEST_CFLAGS:-} \
    -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwabi-contract.c" -o "$work/contract"
"$work/contract"
# ABI-changing compiler flags must fail the production assertions.
for flag in -fshort-enums -fpack-struct=1; do
    if ${CC:-cc} -std=c11 -Wall -Wextra -Werror \
        -DPVR_FWABI_COMPILE_ONLY "$flag" \
        -I"$src/sys/arch/arm/sunxi" -fsyntax-only \
        "$tools/a733-gpu-fwabi-contract.c" >"$work/reject.log" 2>&1; then
        echo "ABI assertions did not reject $flag" >&2
        exit 1
    fi
    if ! grep -q 'static assertion' "$work/reject.log"; then
        cat "$work/reject.log" >&2
        echo "compiler rejected $flag without testing the ABI" >&2
        exit 1
    fi
    echo "rejected incompatible ABI: $flag"
done
cat >"$work/not-array.c" <<'C'
#include "powervr/pvr_check.h"
struct pvr_fwabi_not_array { uint32_t *member; };
FLEX_ARRAY_CHECK(struct pvr_fwabi_not_array, member);
C
if ${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    -I"$src/sys/arch/arm/sunxi" -fsyntax-only "$work/not-array.c" \
    >"$work/reject.log" 2>&1; then
    echo 'ABI array check accepted a pointer member' >&2
    exit 1
fi
if ! grep -q 'static assertion' "$work/reject.log"; then
    cat "$work/reject.log" >&2
    echo 'compiler failed without testing the array ABI' >&2
    exit 1
fi
echo 'rejected incompatible ABI: pointer in place of array'
