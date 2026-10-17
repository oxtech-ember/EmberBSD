#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8822C TX power contract runner, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8822c-txpwr-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
run_test()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$@"
    else
        "$@"
    fi
}
mkdir -p "$work/include/sys" "$work/include/dev"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
for header in sys/param.h sys/systm.h; do
    printf '#ifndef RTW88_TXPWR_TEST_PARAM\n#define RTW88_TXPWR_TEST_PARAM\n#include <errno.h>\n#undef __arraycount\n#include "rtw88-test-kernel.h"\n#endif\n' > "$work/include/$header"
done
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
    "$tools/rtw8822c-txpwr-contract.c" "$src/sys/dev/ic/rtw8822c_txpwr.c" \
    "$src/sys/dev/ic/rtw8822c_table.c" -o "$work/test"
if [ -n "${RTW88_TEST_OUTPUT:-}" ]; then cp "$work/test" "$RTW88_TEST_OUTPUT"; fi
if [ "${RTW88_COMPILE_ONLY:-0}" != 1 ]; then
    run_test "$work/test" "$tools/rtw8822c-txpwr.trace"
fi
# Rename only the supplied table descriptor. The production consumer and PG
# source stay unchanged; the test supplies a limit table missing one section.
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
    -Drtw8822c_txpwr_lmt_type0_tbl=rtw8822c_missing_limit_original \
    -c "$src/sys/dev/ic/rtw8822c_table.c" -o "$work/tables.o"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
    -DRTW88_TXPWR_MISSING_LIMIT \
    "$tools/rtw8822c-txpwr-contract.c" "$src/sys/dev/ic/rtw8822c_txpwr.c" \
    "$work/tables.o" -o "$work/missing"
if [ "${RTW88_COMPILE_ONLY:-0}" != 1 ]; then
    run_test "$work/missing" "$tools/rtw8822c-txpwr.trace"
fi
