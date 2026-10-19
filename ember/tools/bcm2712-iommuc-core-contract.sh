#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-iommuc-core.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "IOMMUC core temporary directory: $work"
cp "$src/sys/arch/arm/broadcom/bcm2712_iommuc_core.c" "$work/core.c"
cp "$src/sys/arch/arm/broadcom/bcm2712_iommuc_corevar.h" "$work/bcm2712_iommuc_corevar.h"
cp "$work/core.c" "$work/baseline.c"
compile()
{
    # CFLAGS is an explicit caller-supplied compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$work/core.c" "$src/ember/tools/bcm2712-iommuc-core-contract.c" \
        -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"
    else "$work/contract"; fi
}
compile
if [ -n "${IOMMUC_OUTPUT:-}" ]; then cp "$work/contract" "$IOMMUC_OUTPUT"; fi
if [ -n "${IOMMUC_EVIDENCE:-}" ]; then
    mkdir -p "$IOMMUC_EVIDENCE"
    cp "$work/core.c" "$work/bcm2712_iommuc_corevar.h" \
        "$src/ember/tools/bcm2712-iommuc-core-contract.c" "$IOMMUC_EVIDENCE/"
fi
if [ "${IOMMUC_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${IOMMUC_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
for mutation in ${IOMMUC_MUTANTS:-command poll-limit enable known preflight retain release fault alias reinit mutation raw barrier fini member fault-retain}; do
    expected=1
    case "$mutation" in
        command) from='IOMMUC_ENABLE | IOMMUC_FLUSH'; to='IOMMUC_ENABLE' ;;
        poll-limit) from='1024U'; to='1025U' ;;
        enable) from='if ((s->raw_control & IOMMUC_ENABLE) == 0)'; to='if (false)' ;;
        known) from='0x7U'; to='0xfU' ;;
        preflight) from='return bcm2712_iommuc_fault(sc, EBUSY);'; to='return 0;' ;;
        retain) from='if ((sc->status.retained & bit) == 0)'; to='if (false)' ;;
        release) from='if ((sc->status.retained & bit) != 0)'; to='if (false)'; expected=2 ;;
        fault) from='if (sc->status.fault != 0)'; to='if (false)' ;;
        alias) from='sc->io = callbacks;'; to='sc->io = *io;' ;;
        reinit) from='if (sc->initialized)'; to='if (false)' ;;
        mutation) from='s->mutation_attempted = true;'; to='s->mutation_attempted = false;' ;;
        raw) from='sc->status.raw_valid = false;'; to='sc->status.raw_valid = true;' ;;
        barrier) from='sc->io.barrier(sc->io.arg);'; to='if (false) sc->io.barrier(sc->io.arg);' ;;
        fini) from='if (sc->status.reserved != 0 || sc->status.retained != 0 ||'; to='if (false || sc->status.retained != 0 ||' ;;
        member) from='*bit = 4U;'; to='*bit = 2U;' ;;
        fault-retain) from='sc->status.fault = error;'; to='sc->status.retained = 0; sc->status.fault = error;' ;;
        *) echo "Unknown mutation: $mutation" >&2; exit 1 ;;
    esac
    awk -v from="$from" -v to="$to" -v expected="$expected" '
        { at=index($0,from); if (at) { count++; $0=substr($0,1,at-1) to substr($0,at+length(from)) } print }
        END { if (count != expected) exit 1 }
    ' "$work/baseline.c" > "$work/core.c"
    compile > "$work/compile.log" 2>&1 || {
        cat "$work/compile.log" >&2
        echo "Mutation failed to compile: $mutation" >&2; exit 1;
    }
    if (ulimit -c 0; run) > "$work/mutant.log" 2>&1; then
        echo "Mutation survived: $mutation" >&2; exit 1
    fi
    if [ -n "${IOMMUC_EVIDENCE:-}" ]; then
        cp "$work/mutant.log" "$IOMMUC_EVIDENCE/mutant-$mutation.log"
    fi
    echo "IOMMUC core mutation rejected: $mutation"
done
