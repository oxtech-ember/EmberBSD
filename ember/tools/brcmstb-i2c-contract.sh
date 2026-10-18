#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/brcmstb-i2c-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "BRCMSTB temporary directory: $work"
mkdir -p "$work/include/dev/i2c" "$work/include/sys" "$work/include/prop"
for file in i2cvar.h i2c_io.h brcmstb_i2creg.h brcmstb_i2cvar.h; do
    cp "$src/sys/dev/i2c/$file" "$work/include/dev/i2c/"
done
# Keep the production BSD op, flag and type headers. Stub only kernel services.
# Extract the exact BSD __BIT helper without replacing host libc's cdefs.h.
awk '
    /^#define[ \t]+__BIT\(/ { copying = 1 }
    copying { print; if (substr($0, length($0)) != sprintf("%c", 92)) copying = 0 }
' "$src/sys/sys/cdefs.h" > "$work/include/bsd-bit.h"
cat > "$work/include/sys/device.h" <<'HEADER'
#include <stdbool.h>
#include <stdint.h>
#include <sys/queue.h>
typedef void *device_t;
typedef void *cfdata_t;
struct device_compatible_entry;
HEADER
cat > "$work/include/sys/mutex.h" <<'HEADER'
typedef struct { int unused; } kmutex_t;
HEADER
cat > "$work/include/prop/proplib.h" <<'HEADER'
typedef void *prop_array_t;
typedef void *prop_dictionary_t;
HEADER
cp "$src/sys/dev/i2c/brcmstb_i2c.c" "$work/core.c"
ulimit -c 0
compile()
{
    # CFLAGS is a caller-supplied compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
        -include "$work/include/bsd-bit.h" -I"$work/include" "$work/core.c" \
        "$src/ember/tools/brcmstb-i2c-contract.c" -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"; else "$work/contract"; fi
}
compile
if [ -n "${BRCMSTB_OUTPUT:-}" ]; then cp "$work/contract" "$BRCMSTB_OUTPUT"; fi
if [ -n "${BRCMSTB_EVIDENCE:-}" ]; then
    mkdir -p "$BRCMSTB_EVIDENCE"
    cp -R "$work/include" "$BRCMSTB_EVIDENCE/"
    cp "$work/core.c" "$src/ember/tools/brcmstb-i2c-contract.c" "$BRCMSTB_EVIDENCE/"
fi
if [ "${BRCMSTB_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${BRCMSTB_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/core.c" "$work/good.c"
for mutation in restart stop address packing noack guard mutation fault deadline budget count-readback ctl-readback init-alias initial-budget guard-budget; do
    nth=1
    case "$mutation" in
        restart) from='condition = done == 0 ? 0 : BRCMSTB_I2C_NOSTART;'; to='condition = BRCMSTB_I2C_RESTART;' ;;
        init-alias) from='sc->io = saved;'; to='sc->io = io == &sc->io ? *io : saved;' ;;
        initial-budget) from='error = brcmstb_i2c_read(sc, BRCMSTB_I2C_CTL, &ctl);'; to='sc->started = sc->previous = sc->io.time_us(sc->io.arg); error = brcmstb_i2c_read(sc, BRCMSTB_I2C_CTL, &ctl);' ;;
        guard-budget) from='error = brcmstb_i2c_tick(sc);'; to='error = 0;'; nth=3 ;;
        stop) from='if (done + chunk < buflen)'; to='if (done + chunk <= buflen)' ;;
        address) from='((uint32_t)addr << 1) | 1'; to='((uint32_t)addr << 1)' ;;
        packing) from='(uint8_t)(value >> (8 * byte))'; to='(uint8_t)(value >> (8 * (3 - byte)))' ;;
        noack) from='return (enable & BRCMSTB_I2C_NOACK) != 0 ? EIO : 0;'; to='return 0;' ;;
        guard) from='error = brcmstb_i2c_error(sc->io.guard(sc->io.arg, false));'; to='error = 0;' ;;
        mutation) from='sc->status.mutation_attempted = true;'; to='sc->status.mutation_attempted = false;' ;;
        fault) from='sc->status.fault = error;'; to='sc->status.fault = 0;' ;;
        deadline) from='now - sc->started >= BRCMSTB_I2C_TIMEOUT_US'; to='now - sc->started >= BRCMSTB_I2C_TIMEOUT_US * 5U' ;;
        budget) from='sc->status.polls == BRCMSTB_I2C_POLL_BUDGET'; to='sc->status.polls == BRCMSTB_I2C_POLL_BUDGET + 1U' ;;
        count-readback) from='if ((value & BRCMSTB_I2C_COUNT_MASK) != 0)'; to='if (false)' ;;
        ctl-readback) from='if ((value & BRCMSTB_I2C_CTL_MASK) !='; to='if (false && (value & BRCMSTB_I2C_CTL_MASK) !=' ;;
    esac
    awk -v from="$from" -v to="$to" -v nth="$nth" '
        !changed { p = index($0, from); if (p && ++seen == nth) { $0 = substr($0,1,p-1) to substr($0,p+length(from)); changed++ } }
        { print } END { if (changed != 1) exit 1 }
    ' "$work/good.c" > "$work/core.c"
    compile
    if (run) >"$work/mutant.log" 2>&1; then
        echo "Surviving BRCMSTB causal mutant: $mutation" >&2
        exit 1
    fi
    echo "BRCMSTB causal mutant rejected: $mutation"
done
