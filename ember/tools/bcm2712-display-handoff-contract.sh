#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-display-handoff.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "Display handoff temporary directory: $work"
cp "$src/sys/arch/arm/broadcom/bcm2712_display_handoff.c" "$work/core.c"
cp "$src/sys/arch/arm/broadcom/bcm2712_display_handoffvar.h" "$work/"
cp "$work/core.c" "$work/baseline.c"
compile()
{
    # CFLAGS is an explicit compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$work/core.c" "$src/ember/tools/bcm2712-display-handoff-contract.c" \
        -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"
    else "$work/contract"; fi
}
compile
if [ -n "${HANDOFF_OUTPUT:-}" ]; then cp "$work/contract" "$HANDOFF_OUTPUT"; fi
if [ "${HANDOFF_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${HANDOFF_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
for mutation in tag size attempt retry global reply-tag end response exact-length only-four fault complete; do
    case "$mutation" in
        tag) from='[8] = 0x66'; to='[8] = 0x67' ;;
        size) from='exchange(arg, packet, sizeof(packet))'; to='exchange(arg, packet, sizeof(packet) - 4)' ;;
        attempt) from='status->attempted = true;'; to='status->attempted = false;' ;;
        retry) from='if (status->attempted || status->complete || status->fault != 0)'; to='if (false)' ;;
        global) from='bcm2712_handoff_word(packet + 4) != 0x80000000U'; to='false' ;;
        reply-tag) from='bcm2712_handoff_word(packet + 8) != 0x00030066U'; to='false' ;;
        end) from='bcm2712_handoff_word(packet + 20) != 0'; to='false' ;;
        response) from='else if ((response & 0x80000000U) == 0)'; to='else if (false)' ;;
        exact-length) from='else if ((response & 0x80000000U) == 0)'; to='else if (response != 0x80000000U)' ;;
        only-four) from='else if ((response & 0x80000000U) == 0)'; to='else if (response != 0x80000000U && response != 0x80000004U)' ;;
        fault) from='status->fault = error;'; to='status->fault = 0;' ;;
        complete) from='status->complete = true;'; to='status->complete = false;' ;;
    esac
    awk -v from="$from" -v to="$to" '
        { at=index($0,from); if (at) { count++; $0=substr($0,1,at-1) to substr($0,at+length(from)) } print }
        END { if (count != 1) exit 1 }
    ' "$work/baseline.c" > "$work/core.c"
    compile > "$work/compile.log" 2>&1 || {
        cat "$work/compile.log" >&2; echo "Mutation did not compile: $mutation" >&2; exit 1;
    }
    if (ulimit -c 0; run) > "$work/mutant.log" 2>&1; then
        echo "Mutation survived: $mutation" >&2; exit 1
    fi
    if [ -n "${HANDOFF_EVIDENCE:-}" ]; then
        mkdir -p "$HANDOFF_EVIDENCE"
        cp "$work/mutant.log" "$HANDOFF_EVIDENCE/mutant-$mutation.log"
    fi
    echo "Display handoff mutation rejected: $mutation"
done
