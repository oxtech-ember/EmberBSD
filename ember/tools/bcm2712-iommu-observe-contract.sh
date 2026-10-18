#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-iommu-observe.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "IOMMU observer temporary directory: $work"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/modules/bcm2712_iommu_observe/bcm2712_iommu_observe.c" > "$work/observer.h"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_display.c" > "$work/display-core.h"
sed '/^#include /d' "$src/sys/arch/arm/broadcom/bcm2712_display.h" > "$work/display.h"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_CORE|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
libfdt=$src/sys/external/bsd/libfdt/dist
for unit in fdt fdt_ro fdt_rw fdt_sw fdt_wip fdt_empty_tree fdt_strerror \
    fdt_addresses fdt_overlay; do
    # CFLAGS is an explicit caller-supplied argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra ${CFLAGS:-} -I"$libfdt" \
        -c "$libfdt/$unit.c" -o "$work/$unit.o"
done
compile()
{
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
        -I"$work" -I"$libfdt" "$src/ember/tools/bcm2712-iommu-observe-contract.c" \
        "$work/"*.o -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"
    else "$work/contract"; fi
}
compile
if [ -n "${IOMMU_OBSERVE_OUTPUT:-}" ]; then cp "$work/contract" "$IOMMU_OBSERVE_OUTPUT"; fi
if [ -n "${IOMMU_OBSERVE_EVIDENCE:-}" ]; then
    mkdir -p "$IOMMU_OBSERVE_EVIDENCE"
    cp "$work/"*.h "$src/ember/tools/bcm2712-iommu-observe-contract.c" \
        "$IOMMU_OBSERVE_EVIDENCE/"
fi
if [ "${IOMMU_OBSERVE_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${IOMMU_OBSERVE_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/observer.h" "$work/baseline.h"
for mutation in ${IOMMU_OBSERVE_MUTANTS:-cold conflict resource offset extra-read pre-clocks debug-width unknown-ctrl command-bits pending global-bypass start cap-unit shift cap-bound registers clocks cleanup unmap diagnostics}; do
    case "$mutation" in
        cold) from='!cold_headless ||'; to='false ||' ;;
        conflict) from='if (prop_dictionary_get(properties, conflicts[i]) != NULL)'; to='if (prop_dictionary_get(properties, conflicts[i]) != NULL && false)' ;;
        resource) from='contract.memory[4].address != UINT64_C(0x1000005200)'; to='contract.memory[4].address != UINT64_C(0x1000005204)' ;;
        offset) from='0x38, 0x00, 0x04, 0x14, 0x1c, 0x20, 0x24'; to='0x38, 0x00, 0x04, 0x14, 0x1c, 0x20, 0x28' ;;
        extra-read) from='sample->stage = "registers";'; to='sample->stage = "registers"; (void)bus_space_read_4(&arm_generic_bs_tag, mapping, 0x38);' ;;
        pre-clocks) from='sample->clocks_valid[0] = true;'; to='sample->clocks_valid[0] = true; return 0;' ;;
        debug-width) from='((debug >> 8) & 0xfU) >= 6'; to='((debug >> 8) & 0xfU) >= 5' ;;
        unknown-ctrl) from='~0x0f1f1f8fU'; to='~UINT32_MAX' ;;
        command-bits) from='(1U << 3) | (1U << 2)'; to='0U | 0U' ;;
        pending) from='(1U << 7)'; to='0U' ;;
        global-bypass) from='(1U << 8) | (1U << 7)'; to='0U | (1U << 7)' ;;
        start) from='word[4] != 0'; to='false' ;;
        cap-unit) from='(uint64_t)(word[3] & 0x7fffffffU) + 1'; to='(uint64_t)(word[3] & 0x7fffffffU) + 0' ;;
        shift) from='? 22 : 28'; to='? 28 : 22' ;;
        cap-bound) from='cap > IOMMU_OBSERVE_BOUND'; to='false' ;;
        registers) from='if (memcmp(sample->word[0], sample->word[1],'; to='if (false && memcmp(sample->word[0], sample->word[1],' ;;
        clocks) from='sample->clocks[0].clock[i].rate != sample->clocks[1].clock[i].rate'; to='false' ;;
        cleanup) from='drm_minor_release(minor);'; to='drm_minor_release(NULL);' ;;
        unmap) from='bus_space_unmap(&arm_generic_bs_tag, mapping, contract.memory[4].length);'; to='(void)mapping;' ;;
        diagnostics) from='iommu_observe_log(&sample, error);'; to='iommu_observe_log(&sample, 0);' ;;
        *) echo "Unknown mutation: $mutation" >&2; exit 1 ;;
    esac
    # Replace literal source text. Each mutation must hit exactly one line,
    # except the two identical releases checked by the cleanup mutation.
    expected=1
    if [ "$mutation" = cleanup ]; then expected=2; fi
    awk -v from="$from" -v to="$to" -v expected="$expected" '
        { at=index($0,from); if (at) { count++; $0=substr($0,1,at-1) to substr($0,at+length(from)) } print }
        END { if (count != expected) exit 1 }
    ' "$work/baseline.h" > "$work/observer.h"
    # Compilation failure is never counted as a behavioral rejection.
    compile > "$work/mutant-compile.log" 2>&1 || {
        cat "$work/mutant-compile.log" >&2
        echo "Mutation failed to compile: $mutation" >&2; exit 1;
    }
    if (ulimit -c 0; run) > "$work/mutant-run.log" 2>&1; then
        echo "Mutation survived: $mutation" >&2; exit 1
    fi
    echo "IOMMU observer mutation rejected: $mutation"
done
