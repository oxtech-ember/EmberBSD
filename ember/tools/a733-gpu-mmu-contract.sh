#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -gt 1 ]; then
    echo "usage: $0 [pinned-firmware]" >&2
    exit 2
fi
sha256_file()
{
    if command -v sha256 >/dev/null 2>&1; then
        sha256 -q "$1"
    elif command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}
if [ "$#" -eq 1 ] && [ "$(sha256_file "$1")" != \
    1db1c399c17401d1f79d46c880db81c724d748c784d4639433b076aba2f9c0d2 ]; then
    echo 'firmware digest does not match the pinned input' >&2
    exit 1
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-mmu.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
oracle_flag=
if [ -n "${MMU_UPSTREAM_DIR:-}" ]; then
    if [ "$(sha256_file "$MMU_UPSTREAM_DIR/pvr_mmu.c")" != \
        f637771e335223c947b1d294f9f84bfc7d60bf91279d07a175f49470f5d2d9f1 ] ||
       [ "$(sha256_file "$MMU_UPSTREAM_DIR/pvr_rogue_mmu_defs.h")" != \
        123701eb6a9bb2ce408e2f2da2ad8a52c654dac9c78c46ec64426bca9c5ea6d7 ]; then
        echo 'upstream files do not match Linux 7b63ef2d55f2' >&2
        exit 1
    fi
    cp "$MMU_UPSTREAM_DIR/pvr_rogue_mmu_defs.h" "$work/"
    cat > "$work/upstream.h" <<'ORACLE'
/* Imagination Technologies Ltd., 2023. MIT option of the pinned sources. */
#include "pvr_rogue_mmu_defs.h"
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t dma_addr_t;
#define WRITE_ONCE(dst, value) ((dst) = (value))
#define typeof_member(type, member) __typeof__(((type *)0)->member)
#define ROGUE_MMUCTRL_PAGE_SIZE_X ROGUE_MMUCTRL_PAGE_SIZE_4KB
#define ROGUE_MMUCTRL_PAGE_X_RANGE_SHIFT ROGUE_MMUCTRL_PAGE_4KB_RANGE_SHIFT
#define ROGUE_MMUCTRL_PAGE_X_RANGE_CLRMSK ROGUE_MMUCTRL_PAGE_4KB_RANGE_CLRMSK
struct pvr_page_table_l2_entry_raw { u32 val; };
struct pvr_page_table_l1_entry_raw { u64 val; };
struct pvr_page_table_l0_entry_raw { u64 val; };
struct pvr_page_flags_raw { struct pvr_page_table_l0_entry_raw val; };
ORACLE
    # Exact macros and function bodies from the hash-checked input.
    sed -n '382,393p' "$MMU_UPSTREAM_DIR/pvr_mmu.c" >> "$work/upstream.h"
    for name in pvr_page_table_l2_entry_raw_set pvr_page_table_l1_entry_raw_set \
        pvr_page_table_l0_entry_raw_set pvr_page_flags_raw_create \
        pvr_page_table_l2_idx pvr_page_table_l1_idx pvr_page_table_l0_idx; do
        case "$name" in
            *_raw_set) echo 'static void' >> "$work/upstream.h" ;;
            *_raw_create) echo 'static struct pvr_page_flags_raw' >> "$work/upstream.h" ;;
            *_idx) echo 'static u16' >> "$work/upstream.h" ;;
        esac
        awk -v name="$name" '
            $0 ~ "^" name "\\(" { emit = 1; found++ }
            emit { print }
            emit && /^}/ { emit = 0 }
            END { if (found != 1) exit 1 }
        ' "$MMU_UPSTREAM_DIR/pvr_mmu.c" >> "$work/upstream.h"
    done
    oracle_flag=-DMMU_UPSTREAM_ORACLE
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${MMU_TEST_CFLAGS:-} $oracle_flag \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-mmu-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mmu.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" -o "$work/contract"
"$work/contract" "$@"
