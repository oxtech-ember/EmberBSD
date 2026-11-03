#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
# Exercise production RX-drain and scalar I/O with a bounded batch model.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$tools/rtw88-fixture-runner.subr"
case $(uname -s) in
Darwin) gc_sections=-Wl,-dead_strip ;;
*) gc_sections=-Wl,--gc-sections ;;
esac
mkdir -p "$out/include/dev/ic" "$out/include/dev/sdmmc"
for h in dev/ic/rtw88var.h dev/ic/rtw88_executor.h dev/sdmmc/rtw88_sdio_rx_publish.h dev/sdmmc/rtw88_sdio_fifo.h; do
    printf "#include <rtw88-sdio-rx-fault-fixture.h>\n" > "$out/include/$h"
done
cp "$root/sys/dev/ic/rtw88_hci.h" "$out/include/dev/ic/"
cp "$root/sys/dev/sdmmc/rtw88_sdio_io.h" "$root/sys/dev/sdmmc/sdmmc_io_status.h" "$out/include/dev/sdmmc/"
"$HOST_CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -ffunction-sections $gc_sections \
    -I"$out/include" -I"$root/ember/tools" \
    "$root/sys/dev/sdmmc/rtw88_sdio_rx_publish.c" \
    "$root/sys/dev/sdmmc/rtw88_sdio_io.c" \
    "$root/ember/tools/rtw88-sdio-rx-fault-contract.c" -o "$out/test"
run_test "$out/test"
printf "%s\n" "rtw88 invalid RX batch fault: PASS"
