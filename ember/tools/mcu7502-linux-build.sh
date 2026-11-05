#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD Linux recovery MCU helper build.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 SDK_HOST_PREFIX NEW_OUTPUT_DIR" >&2
    exit 2
fi
prefix=$(CDPATH= cd -- "$1" && pwd -P)
PATH=$prefix/bin:$prefix/sbin:$prefix/gnu/bin
export PATH
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)/mcu7502-linux-aarch64.S
out=$2
for tool in clang ld.lld; do
    [ -x "$prefix/bin/$tool" ] || { echo "missing SDK tool: $tool" >&2; exit 1; }
done
mkdir -- "$out"
build()
{
    "$prefix/bin/clang" --target=aarch64-linux-gnu -x assembler-with-cpp \
        -integrated-as -nostdlib -static -fno-pie -Wl,-no-pie \
        -fuse-ld=lld --ld-path="$prefix/bin/ld.lld" -DMCU_ACTION="$2" \
        "$src" -o "$out/mcu-$1"
}
build arm60 0
build feed60 1
build disable 2
