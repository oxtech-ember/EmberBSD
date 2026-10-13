#!/bin/sh
# Origin: EmberBSD; explicit, separate Zero 3W GPU clock experiment artifact.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 1 ] || { echo 'usage: a733-gpu-experimental-dtb.sh ABSOLUTE_OUTPUT' >&2; exit 1; }
case "$1" in /*) ;; *) echo 'output must be absolute' >&2; exit 1 ;; esac
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
mkdir -p "$1"
out=$(CDPATH= cd -- "$1" && pwd)
case "$out/" in "$src/"*) echo 'output must be outside sources' >&2; exit 1 ;; esac
work=$(mktemp -d "$out/.gpu-dtb.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
name=sun60i-a733-orangepi-zero3w-gpu-experimental
# A failed new build must not leave an earlier candidate looking current.
rm -f "$out/$name.dts" "$out/$name.dtb"
cat > "$work/input.dts" <<'DTS'
#include "sun60i-a733-orangepi-zero3w.dts"
&gpu {
	/delete-property/ netbsd,observe-only;
	netbsd,experimental-clock-prepare;
	netbsd,experimental-domain-request;
};
&npu {
	status = "okay";
};
DTS
base=$src/sys/external/gpl2/dts/dist
"${CPP:-cpp}" -P -xassembler-with-cpp -I "$base/include" \
    -I "$base/arch/arm64/boot/dts/allwinner" "$work/input.dts" > "$work/$name.dts"
"${DTC:-dtc}" -I dts -O dtb -o "$work/$name.dtb" "$work/$name.dts"
# Distinct filenames; never replace the normal board DTB or install anything.
mv "$work/$name.dts" "$out/$name.dts"
mv "$work/$name.dtb" "$out/$name.dtb"
printf '%s\n' "$out/$name.dtb"
