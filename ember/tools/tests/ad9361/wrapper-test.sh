#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual-source AD9361 TX provider contract regression.
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: $0 EMBERBSD_SOURCE" >&2
    exit 2
fi
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source=$1/sys/dev/spi/ad9361.c
work=$(mktemp -d "${TMPDIR:-/tmp}/ad9361-wrapper.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cp "$test_dir/wrapper-shim.h" "$work/shim.h"
cp "$1/sys/dev/spi/ad9361reg.h" "$work/ad9361reg.h"
{
    echo '#include "shim.h"'
    awk '!/^#include/' "$1/sys/dev/spi/ad9361var.h"
    awk '/^struct ad9361_softc;$/ { copy=1 } /^static int ad9361_match/ { copy=0 } copy' "$source"
    echo 'static void ad9361_tx_attach(device_t);'
} > "$work/test.c"
extract()
{
    awk -v name="$1" -v type="$2" '
        $0 ~ ("^(static (int|void) )?" name "\\(") && $0 !~ /;$/ {
            if ($0 !~ /^static /) print type
            copy=1
        }
        copy { print }
        copy && /^}/ { exit }
    ' "$source" >> "$work/test.c"
}
extract ad9361_native_reset 'static int'
extract ad9361_read 'static int'
extract ad9361_attach 'static void'
extract ad9361_tx_registry_init 'static int'
extract ad9361_tx_register 'static int'
extract ad9361_tx_attach 'static void'
extract ad9361_tx_acquire int
extract ad9361_tx_enter 'static int'
extract ad9361_tx_invalidate 'static int'
extract ad9361_tx_prepare_spi 'static int'
extract ad9361_tx_prepare_reset int
extract ad9361_tx_bootstrap int
awk '/^enum ad9361_tx_operation/ { copy=1 } copy { print } copy && /^};/ { exit }' "$source" >> "$work/test.c"
extract ad9361_tx_control 'static int'
extract ad9361_tx_lease_fastlock_prepare int
extract ad9361_tx_lease_fastlock_stage int
extract ad9361_tx_lease_fastlock_recall int
extract ad9361_tx_retune int
extract ad9361_tx_set_attenuation int
extract ad9361_tx_lease_mute int
extract ad9361_tx_read_status int
extract ad9361_tx_release int
cat "$test_dir/wrapper-trace.c" >> "$work/test.c"
flags=
if [ "${AD9361_WRAPPER_SANITIZE:-0}" = 1 ]; then
    flags='-fsanitize=address,undefined -fno-sanitize-recover=all'
fi
"${CC:-cc}" -std=gnu11 -g -O0 -Wall -Werror $flags "$work/test.c" -o "$work/test"
if [ "${AD9361_WRAPPER_ANALYZE:-0}" = 1 ]; then
    "${CC:-cc}" -std=gnu11 -g -O0 -Wall -Werror -fanalyzer -c "$work/test.c" -o "$work/analyze.o"
    "${CC:-cc}" -std=gnu11 -E -P "$work/test.c" > "$work/preprocessed.c"
    cppcheck --enable=warning,style,performance,portability --error-exitcode=1 \
        --inline-suppr "$work/preprocessed.c"
fi
if [ "${AD9361_WRAPPER_VALGRIND:-0}" = 1 ]; then
    if [ -n "$flags" ]; then
        echo 'ASan and Valgrind require separate runs' >&2
        exit 2
    fi
    valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all \
        --error-exitcode=1 --track-origins=yes "$work/test"
else
    "$work/test"
fi
