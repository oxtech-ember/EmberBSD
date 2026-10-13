#!/bin/sh
# Origin: EmberBSD native V3D reset takeover contract, 2026-10-09.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-v3d-takeover.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_v3d_takeover.c" > "$work/takeover.h"
awk '/^#define BCM2712_PM_GRAFX[ \t]/' \
    "$src/sys/arch/arm/broadcom/bcm2835_pmwdog_acpi.c" > "$work/properties.h"
printf 'int\n' > "$work/pm-accessor.h"
sed -n '/^bcmpmwdog_v3d_status(/,$p' \
    "$src/sys/arch/arm/broadcom/bcm2835_pmwdog_acpi.c" >> "$work/pm-accessor.h"
compile_contract()
{
    # Compile actual production takeover/PM bodies; replace only host services.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        -DBCM2712_V3D_TAKEOVER -pthread ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-v3d-takeover-contract.c" -o "$1"
}
run_contract()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$1"
    else
        "$1"
    fi
}
compile_contract "$work/contract"
if [ -n "${V3D_TAKEOVER_OUTPUT:-}" ]; then
    cp "$work/contract" "$V3D_TAKEOVER_OUTPUT"
fi
if [ "${V3D_TAKEOVER_COMPILE_ONLY:-0}" = 1 ]; then
    exit 0
fi
run_contract "$work/contract"
if [ "${V3D_TAKEOVER_SKIP_MUTANTS:-0}" = 1 ]; then
    exit 0
fi
cp "$work/takeover.h" "$work/takeover-good.h"
cp "$work/pm-accessor.h" "$work/pm-good.h"
for mutation in exclusive-owner reset-delay pm-unrelated pm-readback \
    mask-readback quarantine sms-timeout sms-modes sms-progress \
    gmp-block forbidden-submission forbidden-w1c gmp-zero err-zero; do
    cp "$work/takeover-good.h" "$work/takeover.h"
    cp "$work/pm-good.h" "$work/pm-accessor.h"
    case "$mutation" in
        exclusive-owner|reset-delay|pm-unrelated|pm-readback)
            original="$work/pm-good.h"; altered="$work/pm-accessor.h" ;;
        *) original="$work/takeover-good.h"; altered="$work/takeover.h" ;;
    esac
    awk -v mutation="$mutation" '
        mutation == "exclusive-owner" {
            changed += sub(/bcmpmwdog_v3d_claimed.owner != NULL/, "false")
        }
        mutation == "reset-delay" {
            changed += sub(/delay\(1\)/, "delay(0)")
        }
        mutation == "pm-unrelated" {
            changed += sub(/BCM2712_PM_PASSWORD \| \(baseline \& ~BCM2712_PM_RESET_N\)/,
                "BCM2712_PM_PASSWORD | BCM2712_PM_ENABLE")
        }
        mutation == "pm-readback" {
            changed += sub(/value != \(baseline \& ~BCM2712_PM_RESET_N\)/, "false")
        }
        mutation == "mask-readback" {
            changed += sub(/after != \(before \| fields\[i\]\)/, "false")
        }
        mutation == "quarantine" {
            changed += sub(/if \(!tv3d.sealed\)/, "if (true)")
        }
        mutation == "sms-timeout" {
            changed += sub(/return ETIMEDOUT;/, "return 0;")
        }
        mutation == "sms-modes" {
            changed += sub(/\(tee \& ~\(TV3D_SMS_STATE \| TV3D_SMS_PROGRESS\)\) != 0x50/,
                "false")
        }
        mutation == "sms-progress" && /^#define TV3D_SMS_PROGRESS/ {
            changed += sub(/0x1ff00/, "0")
        }
        mutation == "gmp-block" && /"GMP_STATUS"/ {
            changed += sub(/TV3D_HUB/, "TV3D_CORE")
        }
        mutation == "forbidden-submission" {
            changed += sub(/tv3d_write\(TV3D_SMS, 0, TV3D_SMS_RESET\)/,
                "tv3d_write(TV3D_HUB, 0x1200, 1)")
        }
        mutation == "forbidden-w1c" && /^#define TV3D_MASK_SET/ {
            changed += sub(/0x60/, "0x64")
        }
        mutation == "gmp-zero" {
            changed += sub(/\(snapshot->reg\[TV3D_GMP_STATUS\] \& ~TV3D_GMP_ACTIVITY\)/,
                "snapshot->reg[TV3D_GMP_STATUS]")
        }
        mutation == "err-zero" {
            changed += sub(/\(snapshot->reg\[TV3D_ERROR\] \& ~TV3D_ERR_VCD_IDLE\)/,
                "snapshot->reg[TV3D_ERROR]")
        }
        { print }
        END { if (changed != 1) exit 1 }
    ' "$original" > "$altered"
    compile_contract "$work/mutant"
    if (ulimit -c 0; run_contract "$work/mutant") > "$work/mutant.log" 2>&1; then
        echo "surviving BCM2712 V3D takeover causal mutant: $mutation" >&2
        exit 1
    fi
    echo "BCM2712 V3D takeover causal mutant rejected: $mutation"
done
