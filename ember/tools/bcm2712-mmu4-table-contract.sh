#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-mmu4-table.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "MMU4 table temporary directory: $work"
cp "$src/sys/arch/arm/broadcom/bcm2712_mmu4_table.c" "$work/core.c"
cp "$src/sys/arch/arm/broadcom/bcm2712_mmu4_tablevar.h" "$work/"
compile()
{
    # CFLAGS is a caller-supplied argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$work/core.c" "$src/ember/tools/bcm2712-mmu4-table-contract.c" \
        -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"; else "$work/contract"; fi
}
compile
if [ -n "${MMU4_TABLE_OUTPUT:-}" ]; then cp "$work/contract" "$MMU4_TABLE_OUTPUT"; fi
if [ -n "${MMU4_TABLE_EVIDENCE:-}" ]; then
    mkdir -p "$MMU4_TABLE_EVIDENCE"
    cp "$work/core.c" "$work/"*.h \
        "$src/ember/tools/bcm2712-mmu4-table-contract.c" "$MMU4_TABLE_EVIDENCE/"
fi
if [ "${MMU4_TABLE_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${MMU4_TABLE_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/core.c" "$work/good.c"
for mutant in ${MMU4_TABLE_MUTANTS:-valid writable pagesize shift endian top-index boundary tail debug span control-alias frame-alias cpu-overlap output}; do
    case "$mutant" in
        valid) from='0x10000000U'; to='0x00000000U' ;;
        writable) from='value = MMU4_VALID |'; to='value = 0x20000000U | MMU4_VALID |' ;;
        pagesize) from='value = MMU4_VALID |'; to='value = 0xc0000000U | MMU4_VALID |' ;;
        shift) from='pa >> MMU4_PAGE_SHIFT'; to='pa >> (MMU4_PAGE_SHIFT + 1)' ;;
        endian) from='page[offset] = (uint8_t)value'; to='page[offset] = (uint8_t)(value >> 24)' ;;
        top-index) from='mmu4_entry(out->top, 1,'; to='mmu4_entry(out->top, 2,' ;;
        boundary) from='i % MMU4_TABLE_WORDS, input->frame_pa[i]'; to='0, input->frame_pa[i]' ;;
        tail) from='memset(out, 0, sizeof(*out))'; to='memset(out, 0, sizeof(*out) - 1)' ;;
        debug) from='(input->debug & 0xfU) != 4U'; to='(input->debug & 0xfU) != 3U' ;;
        span) from='pa > MMU4_PA_LIMIT - BCM2712_MMU4_PAGE_BYTES'; to='pa > MMU4_PA_LIMIT' ;;
        control-alias) from='control[i] == control[j]'; to='control[i] == 0' ;;
        frame-alias) from='input->frame_pa[i] == control[j]'; to='control[j] == 0' ;;
        cpu-overlap) from='if (a <= b ?'; to='if (a == b ?' ;;
        output) from='error = mmu4_objects(input, out)'; to='if (out != NULL) out->top[0] = 0; error = mmu4_objects(input, out)' ;;
        *) echo "Unknown MMU4 table mutant: $mutant" >&2; exit 1 ;;
    esac
    awk -v from="$from" -v to="$to" '
        { p=index($0,from); if (p) { count++; $0=substr($0,1,p-1) to substr($0,p+length(from)) } print }
        END { if (count != 1) exit 1 }
    ' "$work/good.c" > "$work/core.c"
    cmp -s "$work/good.c" "$work/core.c" && exit 1
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "MMU4 table contract accepted mutant: $mutant" >&2; exit 1
    fi
    if [ -n "${MMU4_TABLE_EVIDENCE:-}" ]; then cp "$work/mutant.log" "$MMU4_TABLE_EVIDENCE/mutant-$mutant.log"; fi
    echo "MMU4 table contract: rejected $mutant mutant"
done
