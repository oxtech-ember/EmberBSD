#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
# Compile the production PHY orchestration with a bounded chip model.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$tools/rtw88-fixture-runner.subr"
mkdir -p "$out/include/dev/ic"
for h in rtw8723d_core_phy rtw8723d_mac rtw8723d_startup rtw8723d_lck rtw8723d_iqk_cal rtw8723d_channel rtw88_io; do
    printf "#include <rtw8723d-core-phy-order-fixture.h>\n" > "$out/include/dev/ic/$h.h"
done
"$HOST_CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -I"$root/ember/tools" -I"$out/include" \
    "$root/sys/dev/ic/rtw8723d_core_phy.c" \
    "$root/ember/tools/rtw8723d-core-phy-order-contract.c" -o "$out/test"
run_test "$out/test"
printf "%s\n" "rtw8723d PHY error ownership: PASS"
