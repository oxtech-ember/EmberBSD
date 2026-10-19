#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-hvs-observe.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "HVS observer temporary directory: $work"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "${HVS_OBSERVE_SOURCE:-$src/sys/modules/bcm2712_hvs_observe/bcm2712_hvs_observe.c}" > "$work/observer.h"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_display.c" > "$work/display-core.h"
sed '/^#include /d' "$src/sys/arch/arm/broadcom/bcm2712_display.h" > "$work/display.h"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_CORE|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
# Reuse only the existing controlled types, proplib/device/ACPI/FDT services.
# No old observer, MMIO/clock service, tests or classifier enters this header.
# Adapt its controlled word array and property name; remove its old offsets.
awk '
    /^bcm2712_display_acpi_resources\(/ { exit }
    NR > 1 { print previous }
    { previous=$0 }
' "$src/ember/tools/bcm2712-iommu-observe-contract.c" | \
    sed 's/words\[2\]\[7\]/words[2][8]/; s/"iommu-mode"/"hvs-mode"/; /^static const uint64_t offsets\[\]/d' > "$work/observer-services.h"
grep -q 'words\[2\]\[8\]' "$work/observer-services.h"
grep -q '"hvs-mode"' "$work/observer-services.h"
libfdt=$src/sys/external/bsd/libfdt/dist
for unit in fdt fdt_ro fdt_rw fdt_sw fdt_wip fdt_empty_tree fdt_strerror \
    fdt_addresses fdt_overlay; do
    # CFLAGS is an explicit caller-supplied compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra ${CFLAGS:-} -I"$libfdt" \
        -c "$libfdt/$unit.c" -o "$work/$unit.o"
done
compile()
{
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
        -I"$work" -I"$libfdt" "$src/ember/tools/bcm2712-hvs-observe-contract.c" \
        "$work/"*.o -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"
    else "$work/contract"; fi
}
compile
if [ -n "${HVS_OBSERVE_OUTPUT:-}" ]; then cp "$work/contract" "$HVS_OBSERVE_OUTPUT"; fi
if [ -n "${HVS_OBSERVE_EVIDENCE:-}" ]; then
    mkdir -p "$HVS_OBSERVE_EVIDENCE"
    cp "$work/"*.h "$src/ember/tools/bcm2712-hvs-observe-contract.c" "$HVS_OBSERVE_EVIDENCE/"
fi
if [ "${HVS_OBSERVE_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${HVS_OBSERVE_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/observer.h" "$work/baseline.h"
for mutation in ${HVS_OBSERVE_MUTANTS:-cold conflict resource c0-offset d0-offset layout first-version second-version global reset mode all-channels registers clocks priority unmap cleanup diagnostics irq extra-read}; do
    case "$mutation" in
        cold) from='!cold_headless ||'; to='false ||' ;;
        conflict) from='if (prop_dictionary_get(properties, conflicts[i]) != NULL)'; to='if (prop_dictionary_get(properties, conflicts[i]) != NULL && false)' ;;
        resource) from='contract.memory[3].address != UINT64_C(0x107c580000)'; to='contract.memory[3].address != UINT64_C(0x107c580004)' ;;
        c0-offset) from='0x30, 0x44'; to='0x30, 0x48' ;;
        d0-offset) from='0x100, 0x118'; to='0x100, 0x11c' ;;
        layout) from='hvs_version & 0xffU'; to='hvs_version & 0xffffU' ;;
        first-version) from='if (layout < 0)'; to='if (false)' ;;
        second-version) from='} else if (i == 0 && (sample->word[1][0] & 0xffU) !='; to='} else if (false && (sample->word[1][0] & 0xffU) !=' ;;
        global) from='(word[1] & (1U << 31)) != 0'; to='false' ;;
        reset) from='(1U << 31) | (1U << 30)'; to='(1U << 31) | 0U' ;;
        mode) from='(word[i + 1] & 0x6000U) != 0'; to='false' ;;
        all-channels) from='i < HVS_OBSERVE_WORDS; i += 2'; to='i < HVS_OBSERVE_WORDS - 2; i += 2' ;;
        registers) from='if (memcmp(sample->word[0], sample->word[1],'; to='if (false && memcmp(sample->word[0], sample->word[1],' ;;
        clocks) from='sample->clocks[0].clock[i].rate != sample->clocks[1].clock[i].rate'; to='false' ;;
        priority) from='/* A later clock error cannot replace a primary observation errno. */'; to='error = 0;' ;;
        unmap) from='bus_space_unmap(&arm_generic_bs_tag, mapping, contract.memory[3].length);'; to='if (false) bus_space_unmap(&arm_generic_bs_tag, mapping, contract.memory[3].length);' ;;
        cleanup) from='drm_minor_release(minor);'; to='drm_minor_release(NULL);' ;;
        diagnostics) from='hvs_observe_log(&sample, error);'; to='hvs_observe_log(&sample, 0);' ;;
        irq) from='if (cpu_intr_p() || cpu_softintr_p())'; to='if ((cpu_intr_p() || cpu_softintr_p()) && false)' ;;
        extra-read) from='sample->stage = "registers";'; to='sample->stage = "registers"; (void)bus_space_read_4(&arm_generic_bs_tag, mapping, 0);' ;;
        *) echo "Unknown mutation: $mutation" >&2; exit 1 ;;
    esac
    expected=1
    if [ "$mutation" = cleanup ]; then expected=2; fi
    awk -v from="$from" -v to="$to" -v expected="$expected" '
        { at=index($0,from); if (at) { count++; $0=substr($0,1,at-1) to substr($0,at+length(from)) } print }
        END { if (count != expected) exit 1 }
    ' "$work/baseline.h" > "$work/observer.h"
    compile > "$work/mutant-compile.log" 2>&1 || {
        cat "$work/mutant-compile.log" >&2
        echo "Mutation failed to compile: $mutation" >&2; exit 1;
    }
    if (ulimit -c 0; run) > "$work/mutant-run.log" 2>&1; then
        echo "Mutation survived: $mutation" >&2; exit 1
    fi
    if [ -n "${HVS_OBSERVE_EVIDENCE:-}" ]; then
        cp "$work/mutant-run.log" "$HVS_OBSERVE_EVIDENCE/mutant-$mutation.log"
    fi
    echo "HVS observer mutation rejected: $mutation"
done
