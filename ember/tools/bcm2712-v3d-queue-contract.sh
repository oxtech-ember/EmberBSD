#!/bin/sh
# Origin: EmberBSD native V3D bin/render queue contract, 2026-10-10.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-v3d-queue.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_v3d_queue.c" > "$work/queue-body.h"
compile_contract()
{
    # Compile the actual production probe body; replace only host services.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-v3d-queue-contract.c" -o "$1"
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
if [ -n "${V3D_QUEUE_OUTPUT:-}" ]; then
    cp "$work/contract" "$V3D_QUEUE_OUTPUT"
fi
if [ "${V3D_QUEUE_COMPILE_ONLY:-0}" = 1 ]; then
    exit 0
fi
run_contract "$work/contract"
if [ "${V3D_QUEUE_SKIP_MUTANTS:-0}" = 1 ]; then
    exit 0
fi
cp "$work/queue-body.h" "$work/queue-good.h"
for mutation in skip-l2t-invalidate skip-int-clr wrong-store-address \
    missing-start-binning missing-flush-epilogue release-always \
    wrong-clear-color skip-tlb-clear; do
    cp "$work/queue-good.h" "$work/queue-body.h"
    awk -v mutation="$mutation" '
        mutation == "skip-l2t-invalidate" && /QV3D_CTL_L2TCACTL,/ {
            print "\terror = 0;"
            skip = 1
            changed = 1
            next
        }
        mutation == "skip-l2t-invalidate" && skip && /QV3D_L2TCACTL_FLUSH/ {
            skip = 0
            next
        }
        mutation == "skip-int-clr" &&
        /bcmv3d_takeover_core_poke\(QV3D_CTL_INT_CLR, QV3D_INT_FLDONE\)/ {
            changed = 1
            next
        }
        mutation == "wrong-store-address" &&
        /qv3d_set\(cl, o, 64, 32, output_va\)/ {
            changed += sub(/output_va\)/, "output_va + 4096)")
        }
        mutation == "missing-start-binning" &&
        /qv3d_packet\(cl, QV3D_OP_START_TILE_BINNING, 0\)/ {
            changed = 1
            next
        }
        mutation == "missing-flush-epilogue" &&
        /qv3d_packet\(cl, QV3D_OP_FLUSH, 0\)/ {
            changed = 1
            next
        }
        mutation == "release-always" {
            changed += sub(/if \(!qv3d\.published\)/, "if (1)")
        }
        mutation == "wrong-clear-color" {
            changed += sub(/0x305e7b4c/, "0x305e7b4d")
        }
        mutation == "skip-tlb-clear" &&
        /error = bcmv3d_takeover_hub_poke\(QV3D_MMU_CTL,$/ {
            print "\terror = 0;"
            skip = 1
            changed = 1
            next
        }
        mutation == "skip-tlb-clear" && skip && /QV3D_MMU_CTL_TLB_CLEAR/ {
            skip = 0
            next
        }
        { print }
        END { exit changed > 0 ? 0 : 1 }
    ' "$work/queue-good.h" > "$work/queue-body.h" || {
        echo "mutant $mutation: source pattern not found" >&2
        exit 1
    }
    if ! compile_contract "$work/contract-mutant"; then
        echo "mutant $mutation failed to compile" >&2
        exit 1
    fi
    if run_contract "$work/contract-mutant" >/dev/null 2>&1; then
        echo "mutant $mutation was not rejected" >&2
        exit 1
    fi
done
echo "PASS: queue contract; 8 causal mutants rejected"
