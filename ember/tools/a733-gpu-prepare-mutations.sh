#!/bin/sh
# Origin: EmberBSD; causal negatives for the experimental GPU preparation.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-prepare-mutants.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
ulimit -c 0
# Only the files read by the three production-body contract builders.
for path in sys/arch/arm/sunxi/sunxi_rtcvar.h \
    sys/arch/arm/sunxi/sunxi_ccu.h sys/arch/arm/sunxi/sunxi_ccu.c \
    sys/arch/arm/sunxi/sun60i_a733_ccu.h sys/arch/arm/sunxi/sun60i_a733_ccu.c \
    sys/arch/arm/sunxi/sunxi_ccu_gate.c sys/arch/arm/sunxi/sunxi_ccu_div.c \
    sys/arch/arm/sunxi/sunxi_ccu_nm.c sys/arch/arm/sunxi/sunxi_ccu_nkmp.c \
    sys/arch/arm/sunxi/sunxi_ccu_fixed_factor.c \
    sys/arch/arm/sunxi/sun60i_a733_gpu.c \
    sys/arch/arm/sunxi/sun60i_a733_pck600.h sys/arch/arm/sunxi/sun60i_a733_pck600.c \
    sys/dev/fdt/fdtvar.h sys/dev/fdt/fdt_powerdomain.c sys/kern/subr_autoconf.c \
    sys/external/gpl2/dts/dist/include/dt-bindings/clock/allwinner,sun60i-a733-ccu.h \
    sys/external/gpl2/dts/dist/include/dt-bindings/reset/allwinner,sun60i-a733-ccu.h; do
    mkdir -p "$work/source/$(dirname -- "$path")"
    cp "$src/$path" "$work/source/$path"
done
run() {
    env CLOCK_SOURCE_ROOT="$work/source" POWER_SOURCE_ROOT="$work/source" \
        GPU_ID_SOURCE_ROOT="$work/source" sh "$tools/$1"
}
for test in a733-accelerator-clock-contract.sh a733-power-contract.sh \
    a733-gpu-identification-contract.sh; do
    run "$test" > "$work/baseline.log" 2>&1 || { cat "$work/baseline.log"; exit 1; }
done
mutate() {
    name=$1 test=$2 path=sys/arch/arm/sunxi/$3 before=$4 after=$5
    awk -v before="$before" -v after="$after" '
        !done && (pos = index($0, before)) {
            $0 = substr($0, 1, pos - 1) after substr($0, pos + length(before)); done = 1
        }
        { print }
        END { if (!done) exit 1 }
    ' "$src/$path" > "$work/source/$path"
    if run "$test" > "$work/$name.log" 2>&1; then
        echo "FAIL: survived $name" >&2; exit 1
    fi
    # A compile failure is not evidence that the behavioral contract caught it.
    awk '/[Aa]ssertion/ { found = 1 } END { exit !found }' "$work/$name.log" || {
        cat "$work/$name.log"; echo "FAIL: no causal assertion for $name" >&2; exit 1;
    }
    cp "$src/$path" "$work/source/$path"
    echo "PASS: rejected $name"
}
ccu=a733-accelerator-clock-contract.sh
pck=a733-power-contract.sh
gpu=a733-gpu-identification-contract.sh
mutate ancestor-writes "$ccu" sun60i_a733_ccu.c \
    'if (shared || local)' 'if (local)'
mutate reset-bypass "$ccu" sun60i_a733_ccu.c \
    'local = reset->reg == GPU0_BGR_REG;' 'local = false;'
mutate lost-writer-accounting "$ccu" sun60i_a733_ccu.c \
    'sun60i_gpu_lease.writers++;' 'sun60i_gpu_lease.writers += 0;'
mutate wrong-clock-selector "$ccu" sun60i_a733_ccu.c \
    'ACCEL_CLK_SEL | __BITS(3,0), __SHIFTIN(3, ACCEL_CLK_SEL));' \
    'ACCEL_CLK_SEL | __BITS(3,0), __SHIFTIN(2, ACCEL_CLK_SEL));'
mutate lost-retention "$ccu" sun60i_a733_ccu.c \
    'sun60i_gpu_lease.attempted = true;' 'sun60i_gpu_lease.attempted = false;'
mutate update-completion-bypass "$ccu" sun60i_a733_ccu.c \
    'if ((CCU_READ(sc, GPU0_CLK_REG) & GPU_CLK_UPDATE) == 0)' 'if (true)'
mutate update-timeout-success "$ccu" sun60i_a733_ccu.c \
    'error = ETIMEDOUT;' 'error = 0;'
mutate pck-reservation-bypass "$pck" sun60i_a733_pck600.c \
    'if (sc->sc_gpu_owner != NULL &&' 'if (false && sc->sc_gpu_owner != NULL &&'
mutate q-acceptance-bypass "$pck" sun60i_a733_pck600.c \
    'if (d == 1 && !initial && (v[4] != __BIT(8) ||' \
    'if (d == 1 && !initial && ('
mutate pck-write "$pck" sun60i_a733_pck600.c \
    'error = sun60i_pck600_gpu_check(sc, false, last);' \
    'error = sun60i_pck600_gpu_check(sc, false, last); (void)sun60i_pck600_write(sc, 0x6000, 8);'
mutate consumer-wait-bypass "$gpu" sun60i_a733_gpu.c \
    'error = sun60i_a733_pck_gpu_wait(power_node, sc);' 'error = 0;'
mutate consumer-request-bypass "$gpu" sun60i_a733_gpu.c \
    'error = sun60i_a733_pck_gpu_request_on(power_node, sc);' 'error = 0;'
mutate top-identify-bypass "$gpu" sun60i_a733_gpu.c \
    'if (request && error != 0 && sc->sc_retained)' \
    'if (false && request && error != 0 && sc->sc_retained)'
mutate runtime-order-bypass "$gpu" sun60i_a733_gpu.c \
    'stage != sc->sc_run_stage + 1 || !sc->sc_run_pass) {' \
    'false) {'
mutate runtime-timeout-gate "$gpu" sun60i_a733_gpu.c \
    'sc->sc_run_pass = (error == 0 || error == ETIMEDOUT);' \
    'sc->sc_run_pass = (error == 0);'
mutate runtime-identify-gate "$gpu" sun60i_a733_gpu.c \
    'if (!sc->sc_run_core_on) {' 'if (false) {'
mutate pck-request-quarantine "$pck" sun60i_a733_pck600.c \
    'sc->sc_failed[PCK600_GPU_CORE] = true;' '(void)0;'
mutate pck-request-success "$pck" sun60i_a733_pck600.c \
    'if ((status & PCK600_MODE) == PCK600_ON) {' 'if (false) {'
mutate conflicting-opt-in "$gpu" sun60i_a733_gpu.c \
    'if (prepare && observe_only)' 'if (false && prepare && observe_only)'
mutate consumer-release-after-write "$gpu" sun60i_a733_gpu.c \
    'if (!sc->sc_retained)' 'if (true)'
mutate terminal-snapshot-bypass "$gpu" sun60i_a733_gpu.c \
    'if (prepare && sc->sc_retained && error == ETIMEDOUT)' \
    'if (false && prepare && sc->sc_retained && error == ETIMEDOUT)'
