#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
# Exercise actual 8723D descriptor and SDIO batch parsing with a host fixture.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$tools/rtw88-fixture-runner.subr"
: "${HOST_PYTHON:?select the SDK Python}"
mkdir -p "$out/include/dev" "$out/include/sys"
"$HOST_PYTHON" "$root/ember/tools/rtw88-state-contract.py" "$root" "$out"
ln -s "$root/sys/dev/ic" "$out/include/dev/ic"
ln -s "$root/sys/dev/sdmmc" "$out/include/dev/sdmmc"
for header in param.h systm.h; do
    printf "#include <errno.h>\n#include \"rtw88-test-kernel.h\"\n" > "$out/include/sys/$header"
done
"$HOST_CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -include "$root/ember/tools/rtw88-test-kernel.h" \
    -I"$out/include" -I"$out" -I"$root/ember/tools" \
    "$root/sys/dev/ic/rtw8723d_rx.c" "$root/sys/dev/ic/rtw8723d_rx_runtime.c" \
    "$root/sys/dev/ic/rtw8723d_rx_signal.c" "$root/sys/dev/sdmmc/rtw88_sdio_rx.c" \
    "$root/ember/tools/rtw8723d-rx-batch-contract.c" -o "$out/test"
run_test "$out/test"
printf "%s\n" "rtw8723d RX batch contract: PASS"
"$HOST_CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -include "$root/ember/tools/rtw88-test-kernel.h" \
    -I"$out/include" -I"$out" -I"$root/ember/tools" \
    "$root/sys/dev/ic/rtw8723d_ra.c" \
    "$root/ember/tools/rtw8723d-legacy-ra-contract.c" -o "$out/ra-test"
run_test "$out/ra-test"
printf "%s\n" "rtw8723d legacy RA contract: PASS"
