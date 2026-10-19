#!/bin/sh
# Origin: EmberBSD; host regression for the production ARM pmap lookup.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: $0 source-tree" >&2
    exit 2
fi
src=$(CDPATH= cd -- "$1" && pwd)
script=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/arm-dma-coherency.XXXXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
pmap=$src/sys/arch/arm/arm32/pmap.c
header=$src/sys/arch/arm/include/arm32/pmap.h
cc=${CC:-cc}
: > "$out/contract-function.h"
if grep -q '^pmap_dma_coherent_p(' "$pmap"; then
    printf 'static bool\n' >> "$out/contract-function.h"
    sed -n '/^pmap_dma_coherent_p(/,/^}/p' "$pmap" >> "$out/contract-function.h"
fi
printf 'bool\n' >> "$out/contract-function.h"
sed -n '/^pmap_extract_coherency(/,/^}/p' "$pmap" >> "$out/contract-function.h"
grep -q '^pmap_extract_coherency(' "$out/contract-function.h"
# Import the real descriptor geometry and cache masks rather than mirror them.
awk '
/^#define[ \t]+(L[12]_[SL]_CACHE_MASK_(generic|xscale|armv6n|armv7)|L2_BUCKET_(XLOG2|LOG2|SIZE)|l1pte_section_p|l1pte_index|l2pte_index)[ \t(]/ { print }
/^#define[ \t]+l1pte_supersection_p/ { print; getline; print }
' "$header" > "$out/defs-base.h"
awk '/^#define[ \t]+(L2_BUCKET_XSHIFT|L2_BUCKET_IDX|L2_IDX|L2_BUCKET)[ \t(]/ { print }' \
    "$pmap" >> "$out/defs-base.h"
status=0
for mode in generic xscale armv6n armv7; do
    cp "$out/defs-base.h" "$out/contract-defs.h"
    for level in L1_S L2_L L2_S; do
        printf '#define %s_CACHE_MASK %s_CACHE_MASK_%s\n' \
            "$level" "$level" "$mode" >> "$out/contract-defs.h"
    done
    extended=0
    xscale=0
    init=generic
    case "$mode" in
        xscale) xscale=1 ;;
        armv6n) extended=1; init=armv6 ;;
        armv7) extended=1; init=armv7 ;;
    esac
    sed -n "/^pmap_pte_init_${init}(/,/^}/p" "$pmap" |
        grep '^[[:space:]]*pte_l[12]_[sl]_nocache_mode =' > "$out/contract-init.h"
    test "$(wc -l < "$out/contract-init.h" | tr -d ' ')" -eq 3
    "$cc" -std=c99 -O2 -Wall -Wextra -Werror \
        -DARM_MMU_V6=0 -DARM_MMU_V7="$extended" \
        -DCONTRACT_EXTENDED="$extended" -DCONTRACT_XSCALE="$xscale" \
        -DCONTRACT_NAME=\""$mode"\" \
        -I"$out" -I"$src/sys/arch/arm/include/arm32" \
        "$script/arm-dma-coherency-contract.c" -o "$out/contract"
    "$out/contract" || status=1
done
exit "$status"
