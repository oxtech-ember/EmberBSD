#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD host regression for the native AD9361 import.
set -eu
if [ "$#" -ne 1 ]; then
	echo "usage: $0 EMBERBSD_SOURCE" >&2
	exit 2
fi
source_dir=$1/sys/external/bsd/ad9361/dist
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
temp_dir=$(mktemp -d)
trap 'rm -rf "$temp_dir"' EXIT HUP INT TERM
for source in "$source_dir"/*.c "$source_dir"/*.h; do
	name=$(basename "$source")
	{
		echo '#include "shim.h"'
		awk '
			/^#include <(sys|machine|dev)\// { next }
			{ print }
			FILENAME ~ /ad9361_native.h$/ &&
			    /^#include "ad9361_api.h"/ {
				print "#include \"adi_axi_adcvar.h\""
			}
		' "$source"
	} > "$temp_dir/$name"
done
{
	echo '#include "shim.h"'
	awk '!/^#include/' "$1/sys/dev/fdt/adi_axi_adcvar.h"
} > "$temp_dir/adi_axi_adcvar.h"
{
	echo '#include "shim.h"'
	awk '!/^#include/' "$1/sys/dev/spi/ad9361var.h"
} > "$temp_dir/ad9361var.h"
cp "$test_dir/native-shim.h" "$temp_dir/shim.h"
cp "$test_dir/native-trace.c" "$temp_dir/main.c"
sanitize_flags=
if [ "${AD9361_SANITIZE:-0}" = 1 ]; then
	sanitize_flags="-fsanitize=address,undefined -fno-sanitize-recover=all"
fi
"${CC:-cc}" -std=gnu11 -g -O0 $sanitize_flags -I"$temp_dir" \
	"$temp_dir/ad9361_noos.c" "$temp_dir/ad9361_util.c" \
	"$temp_dir/ad9361_api.c" "$temp_dir/ad9361_ext_band_ctrl.c" \
	"$temp_dir/ad9361_conv.c" "$temp_dir/ad9361_rx_tune.c" \
	"$temp_dir/ad9361_native.c" "$temp_dir/ad9361_native_control.c" \
	"$temp_dir/ad9361_native_fdt.c" "$temp_dir/main.c" \
	-o "$temp_dir/trace"
set --
if [ "${AD9361_VALGRIND:-0}" = 1 ]; then
	if [ "${AD9361_SANITIZE:-0}" = 1 ]; then
		echo "ASan and Valgrind require separate runs" >&2
		exit 2
	fi
	set -- valgrind --leak-check=full --show-leak-kinds=all \
	    --errors-for-leak-kinds=all --error-exitcode=1 --track-origins=yes
fi
result=0
"$@" "$temp_dir/trace" > "$temp_dir/result" 2>&1 || result=$?
if [ -n "${AD9361_LOG:-}" ]; then
	cp "$temp_dir/result" "$AD9361_LOG"
fi
if [ "$result" -ne 0 ]; then
	tail -60 "$temp_dir/result" >&2
	exit 1
fi
grep '^PASS' "$temp_dir/result"
if [ "${AD9361_VALGRIND:-0}" = 1 ]; then
	grep -E 'ERROR SUMMARY|All heap blocks were freed' "$temp_dir/result"
fi
