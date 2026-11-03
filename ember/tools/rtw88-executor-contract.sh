#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
# Production executor against a deterministic single-thread workqueue model.
set -eu
root=$1
out=$2
: "${HOST_CC:?select the SDK Clang}"
mkdir -p "$out/include/sys" "$out/include/dev/ic"
for h in param systm errno kernel kmem mutex proc workqueue; do
    printf "#include <rtw88-executor-fixture.h>\n" > "$out/include/sys/$h.h"
done
for h in rtw88var rtw88_executor; do
    printf "#include <rtw88-executor-fixture.h>\n" > "$out/include/dev/ic/$h.h"
done
"$HOST_CC" -std=c11 -Wall -Wextra -Werror -I"$out/include" -I"$root/ember/tools" \
    "$root/sys/dev/ic/rtw88_executor.c" "$root/ember/tools/rtw88-executor-contract.c" -o "$out/test"
"$out/test"
printf "%s\n" "rtw88 executor deterministic model: PASS"
