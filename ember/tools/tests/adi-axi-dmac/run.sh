#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD host regression for finite ADI AXI DMAC transfers.
set -eu
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$dir/../../../.." && pwd)
source_dir=${1:-"$root"}
work=$(mktemp -d "${TMPDIR:-/tmp}/ember-axi-dmac.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# Compile the real driver through its interrupt handler. Attachment uses
# kernel-only FDT services; its interface decoder is included and tested.
awk '
    /^#include/ { next }
    /^static const struct device_compatible_entry/ { exit }
    { print }
' "$source_dir/sys/dev/fdt/adi_axi_dmac.c" > "$work/driver.inc"
awk '!/^#include/' "$source_dir/sys/dev/fdt/adi_axi_dmacvar.h" > "$work/api.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -pthread ${CFLAGS:-} \
    -I"$work" "$dir/test.c" -o "$work/test"
if [ "${ANALYZE:-no}" = yes ]; then
    ${CC:-cc} -std=c11 ${CFLAGS:-} -E -dM -I"$work" "$dir/test.c" |
        awk '/^#define LIST_/ { print }' > "$work/list.h"
    cppcheck --enable=warning,style,performance,portability --error-exitcode=98 \
        --inline-suppr --suppress=missingIncludeSystem \
        --include="$work/list.h" -I"$work" "$dir/test.c"
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -pthread ${CFLAGS:-} -fanalyzer \
        -I"$work" "$dir/test.c" -o "$work/analyzed"
fi
if [ "${RACECHECK:-no}" = yes ]; then
    valgrind --quiet --tool=helgrind --error-exitcode=97 "$work/test"
elif [ "${MEMCHECK:-no}" = yes ]; then
    valgrind --quiet --leak-check=full --show-leak-kinds=all \
        --errors-for-leak-kinds=all --error-exitcode=99 "$work/test"
else
    "$work/test"
fi
