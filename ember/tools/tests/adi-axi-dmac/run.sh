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
# Include the real ADC stop, its converter-bound callback, and quiesce path.
awk '
    /^(poison_locked|adiaxiadc_dma_drain|quiesce_locked|adiaxiadc_stream_stop)\(/ {
        print $0 ~ /^poison_locked/ ? "static void" : "static int"
        emit = 1
    }
    emit { print; if ($0 == "}") emit = 0 }
' "$source_dir/sys/dev/fdt/adi_axi_adc.c" > "$work/adc-stop.inc"

# Execute ADC admission, channel MMIO and streaming startup against real DMAC.
python3 - "$source_dir" "$work" <<'PYADC'
import pathlib,sys
root,work=map(pathlib.Path,sys.argv[1:]);text=(root/'sys/dev/fdt/adi_axi_adc.c').read_text();functions=[]
for kind,name in [('int','check_dmas'),('bool','io_allowed'),('int','adc_disable_channels'),('int','adc_channels'),('int','adi_axi_adc_start_validate_locked'),('int','adi_axi_adc_start_commit_locked'),('int','adi_axi_adc_start_locked'),('int','adi_axi_adc_start'),('bool','adiaxiadc_reserved_zero'),('int','adiaxiadc_stream_start')]:
 marker=('' if name=='adi_axi_adc_start' else 'static ')+kind+'\n'+name+'('
 if marker not in text:continue
 start=text.index(marker);end=text.index('\n}\n',start)+3;functions.append(text[start:end])
(work/'adc-start.inc').write_text('\n'.join(functions))
header=(root/'sys/dev/fdt/adi_axi_adc.h').read_text()+(root/'sys/dev/fdt/adi_axi_adc_stream.h').read_text()
(work/'adc-abi.inc').write_text('\n'.join(l for l in header.splitlines() if not l.startswith('#include')))
PYADC

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
