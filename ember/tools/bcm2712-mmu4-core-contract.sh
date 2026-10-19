#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-mmu4-core.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "MMU4 init temporary directory: $work"
for part in bcm2712_mmu4_core bcm2712_mmu4_table bcm2712_iommuc_core; do
    cp "$src/sys/arch/arm/broadcom/$part.c" "$work/"
    cp "$src/sys/arch/arm/broadcom/${part}var.h" "$work/"
done
compile()
{
    # CFLAGS is a caller-supplied argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$work/bcm2712_mmu4_core.c" "$work/bcm2712_mmu4_table.c" \
        "$work/bcm2712_iommuc_core.c" \
        "$src/ember/tools/bcm2712-mmu4-core-contract.c" -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"; else "$work/contract"; fi
}
compile
if [ -n "${MMU4_CORE_OUTPUT:-}" ]; then cp "$work/contract" "$MMU4_CORE_OUTPUT"; fi
if [ -n "${MMU4_CORE_EVIDENCE:-}" ]; then
    mkdir -p "$MMU4_CORE_EVIDENCE"
    cp "$work/"*.c "$work/"*.h \
        "$src/ember/tools/bcm2712-mmu4-core-contract.c" "$MMU4_CORE_EVIDENCE/"
fi
if [ "${MMU4_CORE_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${MMU4_CORE_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/bcm2712_mmu4_core.c" "$work/good.c"
for mutant in ${MMU4_CORE_MUTANTS:-clear enable poll-mask final-mask poll-bound misc cap top default prewrite-fault close-refusal raw-valid prepare reserve retain cache}; do
    case "$mutant" in
        clear) from='0x0810100cU'; to='0x08101004U' ;;
        enable) from='0x04090803U'; to='0x04090801U' ;;
        poll-mask) from='0x08101101U'; to='0x08101100U' ;;
        final-mask) from='0x070f0f03U'; to='0x070f0f02U' ;;
        poll-bound) from='i <= 1024'; to='i < 1024' ;;
        misc) from='~MMU4_SINGLE_TABLE'; to='~0x40000000U' ;;
        cap) from='#define MMU4_CAP_WORD'; to='#define MMU4_CAP_WORD' ;;
        top) from='sc->cfg.top_pa >> 12'; to='sc->cfg.top_pa >> 13' ;;
        default) from='sc->cfg.default_pa >> 12'; to='sc->cfg.default_pa >> 13' ;;
        prewrite-fault) from='if (sc->status.retained)'; to='if (error != 0)' ;;
        close-refusal) from='if (sc->running || sc->status.retained || sc->status.mutation_attempted ||'; to='if (sc->status.first_error != 0 || sc->running || sc->status.retained || sc->status.mutation_attempted ||' ;;
        prepare) from='error = sc->io.prepare_pages(sc->io.arg, &sc->cfg);'; to='error = 0;' ;;
        reserve) from='error = bcm2712_iommuc_core_reserve(sc->cache, BCM2712_IOMMUC_MMU4);'; to='error = 0;' ;;
        retain) from='error = bcm2712_iommuc_core_retain(sc->cache, BCM2712_IOMMUC_MMU4);'; to='error = 0;' ;;
        cache) from='error = bcm2712_iommuc_core_enable_flush(sc->cache, BCM2712_IOMMUC_MMU4);'; to='error = 0;' ;;
        raw-valid) from='sc->status.raw_valid = false'; to='sc->status.raw_valid = true' ;;
        *) echo "Unknown MMU4 init mutant: $mutant" >&2; exit 1 ;;
    esac
    if [ "$mutant" = cap ]; then
        from='#define MMU4_CAP_WORD'; to='#define MMU4_CAP_WORD'
        awk '/^#define MMU4_CAP_WORD/ { sub(/0x80000000U/, "0x80000001U"); count++ } { print } END { if (count != 1) exit 1 }' \
            "$work/good.c" > "$work/bcm2712_mmu4_core.c"
    else
        awk -v from="$from" -v to="$to" '
            { p=index($0,from); if (p) { count++; $0=substr($0,1,p-1) to substr($0,p+length(from)) } print }
            END { if (count != 1) exit 1 }
        ' "$work/good.c" > "$work/bcm2712_mmu4_core.c"
    fi
    cmp -s "$work/good.c" "$work/bcm2712_mmu4_core.c" && exit 1
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "MMU4 init contract accepted mutant: $mutant" >&2; exit 1
    fi
    if [ -n "${MMU4_CORE_EVIDENCE:-}" ]; then cp "$work/mutant.log" "$MMU4_CORE_EVIDENCE/mutant-$mutant.log"; fi
    echo "MMU4 init contract: rejected $mutant mutant"
done
