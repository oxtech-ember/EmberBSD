#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-hsm-probe.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "HSM probe temporary directory: $work"
for unit in clock hsm hsm_acpi dvp dvp_acpi; do
    input=$unit
    if [ "$unit" = hsm_acpi ]; then input=clock_acpi; fi
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/broadcom/bcm2712_display_${input}.c" > "$work/$unit.h"
done
sed '/^#include /d' "$src/sys/arch/arm/broadcom/bcm2712_display_bvb.h" > "$work/bvb-types.h"
{
    echo '#ifndef __CTASSERT'
    echo '#define __CTASSERT(e) _Static_assert(e, #e)'
    echo '#endif'
    echo '#include "bvb-types.h"'
    cat "$work/hsm_acpi.h"
} > "$work/native.h"
mv "$work/native.h" "$work/hsm_acpi.h"
sed '/^#include /{ /"opt_bcm2712_ddc.h"/!d; }; /^__KERNEL_RCSID(/d' \
    "$src/sys/modules/bcm2712_hsm_probe/bcm2712_hsm_probe.c" > "$work/probe.h"
# Keep the BSD time arithmetic macros and their original source license.
awk '/^#ifndef/ { exit } { print }' "$src/sys/sys/time.h" > "$work/time-ops.h"
for macro in timespeccmp timespecsub; do
    printf '#ifndef %s\n' "$macro" >> "$work/time-ops.h"
    awk -v macro="$macro" '
        $0 ~ "^#define[ \t]+" macro "[(]" { copying=1; found++ }
        copying { print; if (substr($0,length($0)) != sprintf("%c",92)) copying=0 }
        END { if (found != 1 || copying) exit 1 }
    ' "$src/sys/sys/time.h" >> "$work/time-ops.h"
    echo '#endif' >> "$work/time-ops.h"
done
cp "$src/sys/arch/arm/broadcom/bcm2712_display_hsm.h" "$work/hsm-types.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display_clockvar.h" "$work/clockvar.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display.h" "$work/display.h"
awk '/^#ifdef _KERNEL/ { kernel=1; next } kernel && /^#endif/ { kernel=0; next } !/^#include / { print }' \
    "$src/sys/arch/arm/broadcom/bcm2712_display_irq.h" > "$work/irq-types.h"
cp "$src/ember/tools/bcm2712-display-hsm-contract.c" "$work/hsm-services.c"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_CORE|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_MIN_CLOCKRATE|VCPROPTAG_GET_MAX_CLOCKRATE|VCPROPTAG_SET_CLOCKSTATE|VCPROPTAG_SET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
for pair in display-core:bcm2712_display.c dvp-types:bcm2712_display_dvp.h; do
    name=${pair%%:*}; input=${pair#*:}
    sed '/^#include /d; /^__KERNEL_RCSID(/d' "$src/sys/arch/arm/broadcom/$input" > "$work/$name.h"
done
mv "$work/dvp.h" "$work/dvp-core.h"
mv "$work/dvp_acpi.h" "$work/dvp-native.h"
libfdt=$src/sys/external/bsd/libfdt/dist
# Keep diagnostics from unchanged upstream libfdt. Apply the fixture's strict
# warning policy to the new test and included production probe, not to libfdt.
for unit in fdt fdt_ro fdt_rw fdt_sw fdt_wip fdt_empty_tree fdt_strerror \
    fdt_addresses fdt_overlay; do
    # CFLAGS is a caller-supplied argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra ${CFLAGS:-} -I"$libfdt" \
        -c "$libfdt/$unit.c" -o "$work/$unit.o"
done
compile()
{
    set -- "$work"/*.o
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${CFLAGS:-} \
        -I"$work" -I"$libfdt" \
        "$src/ember/tools/bcm2712-hsm-probe-contract.c" "$@" -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$work/contract"
    else
        "$work/contract"
    fi
}
# These are explicit fixture option configurations, not kernel-header fallbacks.
# The native module requires the selected kernel's generated header.
if [ -f "$src/sys/arch/arm/broadcom/bcm2712_display_ddc.h" ]; then
    sed '/^#include /d' "$src/sys/arch/arm/broadcom/bcm2712_display_ddc.h" > "$work/ddc-types.h"
    sed '/^#include /d' "$src/sys/dev/i2c/brcmstb_i2cvar.h" > "$work/brcm-types.h"
    cp "$src/sys/dev/i2c/i2c_io.h" "$work/i2c-types.h"
fi
cp "$work/probe.h" "$work/probe-good.h"
for feature in ${HSM_PROBE_FEATURES:-off on}; do
    case "$feature" in
        off) : > "$work/opt_bcm2712_ddc.h" ;;
        on)
            test -f "$work/ddc-types.h" || {
                echo "The enabled fixture requires the published native DDC header" >&2
                exit 1
            }
            echo '#define BCM2712_DISPLAY_DDC 1' > "$work/opt_bcm2712_ddc.h"
            ;;
        *) echo "Unknown HSM probe fixture feature: $feature" >&2; exit 1 ;;
    esac
    cp "$work/probe-good.h" "$work/probe.h"
    compile
    if [ "$feature" = off ]; then
        "${NM:-nm}" "$work/contract" > "$work/symbols"
        awk '
            /bcm2712_display_ddc_/ { bad = 1 }
            END { if (bad) exit 1 }
        ' "$work/symbols"
    fi
    if [ -n "${HSM_PROBE_OUTPUT:-}" ]; then cp "$work/contract" "$HSM_PROBE_OUTPUT"; fi
    if [ -n "${HSM_PROBE_EVIDENCE:-}" ]; then
        mkdir -p "$HSM_PROBE_EVIDENCE/$feature"
        cp "$work/"*.h "$work/hsm-services.c" \
            "$src/ember/tools/bcm2712-hsm-probe-contract.c" "$HSM_PROBE_EVIDENCE/$feature/"
    fi
    if [ "${HSM_PROBE_COMPILE_ONLY:-0}" = 1 ]; then continue; fi
    run
    echo "HSM probe feature $feature: PASS"
    if [ "${HSM_PROBE_SKIP_MUTANT:-0}" = 1 ]; then continue; fi
    # Shared behavior is mutated once. The disabled feature gets its own gate mutant.
    if [ -n "${HSM_PROBE_MUTANTS:-}" ]; then
        mutations=$HSM_PROBE_MUTANTS
    elif [ "$feature" = off ]; then
        mutations='edid-feature'
    else
        mutations='fdt-full mode-fallback selected-reset hsm-held retained-map
            hpd-inherited-gate hpd-mutation hpd-offset hpd-held hpd-conflict
            ddc-range ddc-offset ddc-extra-read ddc-held ddc-mask ddc-busy-cleanup
            ddc-partial ddc-conflict ddc-value
            edid-range edid-length edid-inherited-gate edid-status edid-fault
            edid-active edid-validity edid-native-owner edid-null-owner edid-error
            edid-payload-order edid-payload-format edid-destroy
            irq-range irq-owned irq-offset irq-mask irq-pending irq-stale-phase irq-label
            irq-deadline irq-callback-deadline irq-destroy-lock irq-unmap
            irq-retain irq-fini irq-render
            bvb-conflict bvb-hsm-floor bvb-hsm-verify bvb-minimum bvb-clean-error
            bvb-fault bvb-mutation bvb-lease bvb-held bvb-after-free
            bvb-initial-floor bvb-acquired-rate bvb-released-rate
            hd-conflict hd-value hd-initial-hsm hd-hsm-mutation hd-gates hd-resets hd-port0-mutation hd-port1-status hd-range hd-offset hd-extra-read hd-held hd-unmap hd-release-order hd-candidate hd-partial hd-busy-cleanup
            hd-init-precondition hd-init-address hd-init-value hd-init-extra-write hd-init-attempt
            hd-init-barrier hd-init-post-ctl hd-init-post-stopped hd-init-fresh hd-init-retain
            hd-init-error hd-init-no-transition'
    fi
for mutation in $mutations; do
    case "$mutation" in
hd-init-precondition)
    from='if (!hsm_probe_hd_stopped(hd)) {'; to='if (false) {' ;;
hd-init-address)
    from='hd->handle[0], 0, 0);'; to='hd->handle[0], 4, 0);' ;;
hd-init-value)
    from='hd->handle[0], 0, 0);'; to='hd->handle[0], 0, 1);' ;;
hd-init-extra-write)
    from='hd->write_attempted = true;'
    to='hd->write_attempted = true; bus_space_write_4(&arm_generic_bs_tag, hd->handle[0], 0, 0);' ;;
hd-init-attempt)
    from='hd->write_attempted = true;'; to='hd->write_attempted = false;' ;;
hd-init-barrier)
    from='bus_space_barrier(&arm_generic_bs_tag, hd->handle[0], 0,'
    to='hsm_probe_omitted_barrier(&arm_generic_bs_tag, hd->handle[0], 0,' ;;
hd-init-post-ctl)
    from='if (hd->ctl != 0 || !hsm_probe_hd_stopped(hd))'; to='if (false || !hsm_probe_hd_stopped(hd))' ;;
hd-init-post-stopped)
    from='if (hd->ctl != 0 || !hsm_probe_hd_stopped(hd))'; to='if (hd->ctl != 0 || false)' ;;
hd-init-fresh)
    from='dvp->stage = "HD-initialize0-readback";'
    to='if (hd->write_attempted) goto unmap; dvp->stage = "HD-initialize0-readback";' ;;
hd-init-retain)
    from='if (hd->ctl != 0 || !hsm_probe_hd_stopped(hd))'; to='if (false)' ;;
hd-init-error)
    from='return dvp->hd.operation_error;'; to='return 0;' ;;
hd-init-no-transition)
    from='hd->write_attempted = true;'
    to='(void)bcm2712_display_bvb_create(dev, NULL, NULL); hd->write_attempted = true;' ;;
        hd-conflict)
            from='if (prop_dictionary_get(properties, "clock-mode") != NULL ||'
            to='if (false ||' ;;
        hd-value)
            from='(strcmp(hd_mode, "observe") != 0 &&'; to='(false &&' ;;
        hd-initial-hsm)
            from='if ((mode == HSM_PROBE_HD || mode == HSM_PROBE_HD_INIT) &&'
            to='if ((false) &&' ;;
        hd-hsm-mutation)
            from='(status.mutation_attempted ||'
            to='(false ||' ;;
        hd-gates)
            from='if ((initial.gates & 0x18U) != 0 || (initial.resets & 6U) != 0)'
            to='if ((initial.gates & 8U) != 0 || (initial.resets & 6U) != 0)' ;;
        hd-resets)
            from='if ((initial.gates & 0x18U) != 0 || (initial.resets & 6U) != 0)'
            to='if ((initial.gates & 0x18U) != 0 || (initial.resets & 2U) != 0)' ;;
        hd-port0-mutation)
            from='if (status.leases != 1 || status.fault != 0 || status.mutation_attempted ||'
            to='if (status.leases != 1 || status.fault != 0 || false ||' ;;
        hd-port1-status)
            from='if (status.leases != 3 || status.fault != 0 || status.mutation_attempted ||'
            to='if (false || false || status.mutation_attempted ||' ;;
        hd-range)
            from='const unsigned int resource[] = { 11, 17, 25 };'
            to='const unsigned int resource[] = { 12, 17, 25 };' ;;
        hd-offset)
            from='hd->handle[0], 0x48);'; to='hd->handle[0], 0x44);' ;;
        hd-extra-read)
            from='hd->valid = true;'
            to='(void)bus_space_read_4(&arm_generic_bs_tag, hd->handle[2], 0xbc); hd->valid = true;' ;;
        hd-held)
            from='dvp->stage = "HD-read";'
            to='(void)bcm2712_display_dvp_release(dvp->owner, 1); dvp->stage = "HD-read";' ;;
        hd-unmap)
            from='bus_space_unmap(&arm_generic_bs_tag, hd->handle[i - 1],'
            to='bus_space_unmap(&arm_generic_bs_tag, hd->handle[0],' ;;
        hd-release-order)
            from='error = bcm2712_display_dvp_release(dvp->owner, i - 1);'
            to='error = bcm2712_display_dvp_release(dvp->owner, 2 - i);' ;;
        hd-candidate)
            from='((hd->video[0] | hd->video[1]) &'
            to='((hd->video[0] | 0U) &' ;;
        hd-partial)
            from='hd->mapped |= 1U << i;'; to='hd->mapped = 7;' ;;
        hd-busy-cleanup)
            from='hsm_probe_hd_read(dev, hd);'
            to='hsm_probe_hd_read(dev, hd); if ((hd->video[1] & (1U << 31)) != 0) return EBUSY;' ;;
        bvb-conflict)
            from='if (prop_dictionary_get(properties, "dvp-mode") != NULL ||'
            to='if (false ||' ;;
        bvb-hsm-floor)
            from='#define HSM_PROBE_BVB_HSM_MIN 149985000U'
            to='#define HSM_PROBE_BVB_HSM_MIN 120000000U' ;;
        bvb-hsm-verify)
            from='status.rate < minimum || status.fault != 0 ||'
            to='status.rate < BCM2712_DISPLAY_HSM_MIN_RATE || status.fault != 0 ||' ;;
        bvb-minimum)
            from='#define HSM_PROBE_BVB_MIN 75000000U'
            to='#define HSM_PROBE_BVB_MIN 74000000U' ;;
        bvb-clean-error)
            from='clock_error = error;'; to='clock_error = 0;' ;;
        bvb-fault)
            from='status->fault != 0 || status->mutation_attempted ||'
            to='false || status->mutation_attempted ||' ;;
        bvb-mutation)
            from='status->fault != 0 || status->mutation_attempted ||'
            to='status->fault != 0 || false ||' ;;
        bvb-lease)
            from='status->leases != 0)'; to='false)' ;;
        bvb-held)
            from='error = hsm_probe_bvb_cycle(dev, handle, &bvb);'
            to='(void)bcm2712_display_hsm_release(provider, 0); error = hsm_probe_bvb_cycle(dev, handle, &bvb);' ;;
        bvb-after-free)
            from='bvb->owner = NULL;'
            to='(void)hsm_probe_bvb_sample(dev, bvb, "after-free"); bvb->owner = NULL;' ;;
        bvb-initial-floor)
            from='if (status->floor == 0 ||'; to='if (false ||' ;;
        bvb-acquired-rate)
            from='status->rate < MAX(status->floor, HSM_PROBE_BVB_MIN)'
            to='false' ;;
        bvb-released-rate)
            from='status->rate < status->floor || status->minimum[0] != 0 ||'
            to='false || status->minimum[0] != 0 ||' ;;
        irq-range)
            from='const struct bcm2712_display_memory *aon = &contract->memory[12];'
            to='const struct bcm2712_display_memory *aon = &contract->memory[13];' ;;
        irq-owned)
            from='&arm_generic_bs_tag, irq->aon_handle, 0x180,'
            to='&arm_generic_bs_tag, irq->aon_handle, 0x80,' ;;
        irq-offset)
            from='irq->dvp.hpd_handle, 0x1c8);'
            to='irq->dvp.hpd_handle, 0x1a8);' ;;
        irq-mask)
            from='(mask & 0x180U) != 0x180U ||'; to='false ||' ;;
        irq-pending)
            from='if ((mask & 0x180U) != 0x180U || (raw & 1U) == 0) {'
            to='if ((mask & 0x180U) != 0x180U || (pending & 0x180U) != 0 || (raw & 1U) == 0) {' ;;
        irq-stale-phase)
            from='if (irq->phase == 0 && (raw & 1U) == 0)'
            to='if (irq->phase == 0)' ;;
        irq-label)
            from='if (irq->phase == 0 && (raw & 1U) == 0)'
            to='if (irq->phase == 0 && (pending & 0x80U) != 0)' ;;
        irq-deadline)
            from='while (irq->error == 0 && irq->phase != 2) {'
            to='while (irq->error == 0 && irq->phase != 2) { irq->started = irq->previous;' ;;
        irq-callback-deadline)
            from='hsm_probe_irq_remaining(irq, &remaining) : 0;'
            to='hsm_probe_irq_remaining(irq, &remaining) : 0; error = 0;' ;;
        irq-destroy-lock)
            from='error = bcm2712_display_irq_destroy(irq->owner);'
            to='mutex_enter(&irq->lock); error = bcm2712_display_irq_destroy(irq->owner); mutex_exit(&irq->lock);' ;;
        irq-unmap)
            from='error = bcm2712_display_irq_destroy(irq->owner);'
            to='bus_space_unmap(&arm_generic_bs_tag, irq->aon_handle, 0x30); error = bcm2712_display_irq_destroy(irq->owner);' ;;
        irq-retain)
            from='if (hsm_probe_irq0.retained) {'; to='if (false) {' ;;
        irq-fini)
            from='return hsm_probe_irq0.retained ? EBUSY : 0;'; to='return 0;' ;;
        irq-render)
            from='error);'
            to='error);' ;;
        edid-feature) from='#ifndef BCM2712_DISPLAY_DDC'; to='#if 0' ;;
        edid-range)
            from='error = bcm2712_display_ddc_create(dev, contract, bsc,'
            to='error = bcm2712_display_ddc_create(dev, contract, &contract->memory[12],' ;;
        edid-length)
            from='ddc->edid, sizeof(ddc->edid));'; to='ddc->edid, sizeof(ddc->edid) - 1);' ;;
        edid-inherited-gate)
            from='if (mode == HSM_PROBE_EDID0 && (initial.gates & (1U << 3)) != 0)'
            to='if (false)' ;;
        edid-status)
            from='error = bcm2712_display_ddc_status(ddc->owner, &ddc->sample);'
            to='error = bcm2712_display_ddc_status(ddc->owner, &ddc->sample); error = 0;' ;;
        edid-fault)
            from='if (ddc->sample.fault != 0 || ddc->sample.core.fault != 0 ||'
            to='if (false || false ||' ;;
        edid-validity)
            from='if (!ddc->sample.status_valid || !ddc->sample.mask_valid)'
            to='if (false)' ;;
        edid-active)
            from='ddc->sample.core.active)'; to='false)' ;;
        edid-native-owner)
            from='if (ddc->owner != NULL)'; to='if (false)' ;;
        edid-null-owner)
            from='if (ddc->owner == NULL)'; to='if (false)' ;;
        edid-error)
            from='return dvp->ddc.operation_error;'; to='return 0;' ;;
        edid-payload-order)
            from='error = hsm_probe_dvp_cycle(dev, handle, mode, dvp);'
            to='error = hsm_probe_dvp_cycle(dev, handle, mode, dvp); if (mode == HSM_PROBE_EDID0) hsm_probe_edid0_payload(dev, dvp->ddc.edid);' ;;
        edid-payload-format)
            from='edid[i], edid[i + 1], edid[i + 2], edid[i + 3],'
            to='edid[i + 1], edid[i], edid[i + 2], edid[i + 3],' ;;
        edid-destroy)
            from='error = bcm2712_display_ddc_destroy(ddc->owner);'
            to='error = 0;' ;;
        fdt-full)
            from='fdt_check_full(fdt, fdt_totalsize(fdt))'; to='fdt_check_header(fdt)' ;;
        selected-reset)
            from='if ((initial.resets & (1U << 1)) != 0)'; to='if (false)' ;;
        mode-fallback)
            from=''; to='' ;;
        hsm-held)
            from='error = hsm_probe_dvp_cycle(dev, handle, mode, dvp);'
            to='(void)bcm2712_display_hsm_release(provider, 0); error = hsm_probe_dvp_cycle(dev, handle, mode, dvp);' ;;
        hpd-inherited-gate)
            from='if (mode == HSM_PROBE_HPD0 && (initial.gates & (1U << 3)) != 0)'
            to='if (false)' ;;
        hpd-mutation)
            from='if (status.mutation_attempted)'; to='if (false)' ;;
        hpd-offset)
            from='0x1c8);'; to='0x1a8);' ;;
        hpd-held)
            from='dvp->hpd_raw = bus_space_read_4'
            to='(void)bcm2712_display_dvp_release(dvp->owner, 0); dvp->hpd_raw = bus_space_read_4' ;;
        hpd-conflict)
            from='if (prop_dictionary_get(properties, "dvp-mode") != NULL ||'
            to='if (false ||' ;;
        ddc-range)
            from='&contract->memory[14]'; to='&contract->memory[15]' ;;
        ddc-offset)
            from='ddc->handle, 0x2c);'; to='ddc->handle, 0x24);' ;;
        ddc-extra-read)
            from='ddc->valid = true;'
            to='(void)bus_space_read_4(&arm_generic_bs_tag, ddc->handle, 0x50); ddc->valid = true;' ;;
        ddc-held)
            from='dvp->stage = "BSC-map";'
            to='(void)bcm2712_display_dvp_release(dvp->owner, 0); dvp->stage = "BSC-map";' ;;
        ddc-mask)
            from='(ddc->mask & 2U) != 0 &&'; to='true &&' ;;
        ddc-busy-cleanup)
            from='hsm_probe_ddc_snapshot(dev, "sample", ddc);'
            to='hsm_probe_ddc_snapshot(dev, "sample", ddc); if (ddc->enable & 3U) return EBUSY;' ;;
        ddc-partial)
            from='ddc->bsc_valid = true;'
            to='ddc->bsc_valid = true; ddc->valid = true;' ;;
        ddc-conflict)
            from='if (prop_dictionary_get(properties, "hpd-mode") != NULL ||'
            to='if (false ||' ;;
        ddc-value)
            from='if (strcmp(ddc_mode, "observe0") == 0)'; to='if (true)' ;;
        retained-map)
            from='error = hsm_probe_dvp_cycle(dev, handle, mode, dvp);'
            to='error = hsm_probe_dvp_cycle(dev, handle, mode, dvp); if (error != 0 && dvp->mapped) bus_space_unmap(&arm_generic_bs_tag, dvp->handle, 0x10);' ;;
        *) echo "Unknown probe mutant: $mutation" >&2; exit 1 ;;
    esac
    if [ "$mutation" = irq-render ]; then
        awk '
            /module registered for/ { retained=1 }
            retained && /return 0;/ { sub(/return 0;/, "drm_minor_release(minor); return 0;"); changed++; retained=0 }
            { print } END { if (changed != 1) exit 1 }
        ' "$work/probe-good.h" > "$work/probe.h"
    elif [ "$mutation" = hd-conflict ]; then
        awk -v from="$from" -v to="$to" '
            /^\tif \(prop_dictionary_get\(properties, "hd-mode"\) != NULL\)/ { selected=1; branches++ }
            selected && !changed { p = index($0, from); if (p) {
                $0 = substr($0,1,p-1) to substr($0,p+length(from)); changed++
            } }
            { print } END { if (branches != 1 || changed != 1) exit 1 }
        ' "$work/probe-good.h" > "$work/probe.h"
    elif [ "$mutation" = hpd-conflict ]; then
        awk -v from="$from" -v to="$to" '
            /else if \(prop_dictionary_get\(properties, "hpd-mode"\) != NULL\)/ { selected=1; branches++ }
            selected && !changed { p = index($0, from); if (p) {
                $0 = substr($0,1,p-1) to substr($0,p+length(from)); changed++
            } }
            { print } END { if (branches != 1 || changed != 1) exit 1 }
        ' "$work/probe-good.h" > "$work/probe.h"
    elif [ "$mutation" = mode-fallback ]; then
        awk '
            /else if \(strcmp\(dvp_mode, "gate0"\)/ { selected=1 }
            selected && previous == "\t\telse" && $0 == "\t\t\treturn EINVAL;" {
                $0 = "\t\t\tmode = HSM_PROBE_CLOCK;"; changed++
            }
            { previous = $0; print } END { if (changed != 1) exit 1 }
        ' "$work/probe-good.h" > "$work/probe.h"
    else
        awk -v from="$from" -v to="$to" '
            !changed { p = index($0, from); if (p) {
                $0 = substr($0,1,p-1) to substr($0,p+length(from)); changed++
            } }
            { print } END { if (changed != 1) exit 1 }
        ' "$work/probe-good.h" > "$work/probe.h"
    fi
    if [ "$mutation" = hd-init-barrier ]; then
        printf '#define hsm_probe_omitted_barrier(a,b,c,d,e) ((void)0)\n' > "$work/barrier-prefix"
        cat "$work/barrier-prefix" "$work/probe.h" > "$work/probe-with-prefix"
        mv "$work/probe-with-prefix" "$work/probe.h"
    fi
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "HSM/DVP/HPD/DDC probe accepted causal mutant: $mutation" >&2
        exit 1
    fi
    echo "HSM/DVP/HPD/DDC probe: rejected causal mutant $mutation"
done
done
