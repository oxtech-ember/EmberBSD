#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
# Production executor against a deterministic single-thread workqueue model.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$tools/rtw88-fixture-runner.subr"
mkdir -p "$out/include/sys" "$out/include/dev/ic"
for h in param systm kernel kmem mutex proc workqueue; do
    printf "#include <rtw88-executor-fixture.h>\n" > "$out/include/sys/$h.h"
done
printf '#include_next <sys/errno.h>\n' > "$out/include/sys/errno.h"
for h in rtw88var rtw88_executor; do
    printf "#include <rtw88-executor-fixture.h>\n" > "$out/include/dev/ic/$h.h"
done
"$HOST_CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -I"$out/include" -I"$root/ember/tools" \
    "$root/sys/dev/ic/rtw88_executor.c" "$root/ember/tools/rtw88-executor-contract.c" -o "$out/test"
run_test "$out/test"
printf "%s\n" "rtw88 executor deterministic model: PASS"
