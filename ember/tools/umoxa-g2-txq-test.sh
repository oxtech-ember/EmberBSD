#!/bin/sh
# Origin: EmberBSD shared MOXA G2 TX contract runner, 2026-10-10.
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
set -eu
umask 077
[ "$#" -eq 1 ] || { echo 'usage: umoxa-g2-txq-test.sh ABSOLUTE_NEW_OUTPUT' >&2; exit 1; }
case "$1" in /*) ;; *) echo 'output must be absolute' >&2; exit 1 ;; esac
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
parent=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd -P)
name=$(basename -- "$1")
case "$name" in .|..) echo 'output must name a new directory' >&2; exit 1 ;; esac
out=$parent/$name
case "$out/" in "$src/"*) echo 'output must be outside source tree' >&2; exit 1 ;; esac
mkdir "$out"
cc=${CC:-cc}
"$cc" --version > "$out/compiler.txt" 2>&1
sources="$src/sys/dev/usb"
"$cc" -std=c99 -O2 -Wall -Wextra -Werror -I "$sources" \
    "$src/ember/tools/umoxa-g2-txq-test.c" "$sources/umoxa_g2_txq.c" "$sources/umoxa_frame.c" \
    -o "$out/test" > "$out/build.log" 2>&1 || { cat "$out/build.log" >&2; exit 1; }
"$out/test" > "$out/run.log" 2>&1 || { cat "$out/run.log" >&2; exit 1; }
cat "$out/run.log"
printf 'int main(void) { return 0; }\n' > "$out/sanitizer-probe.c"
if "$cc" -fsanitize=address,undefined "$out/sanitizer-probe.c" \
    -o "$out/sanitizer-probe" > "$out/sanitizer-probe.log" 2>&1; then
    "$cc" -std=c99 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer \
        -fsanitize=address,undefined -I "$sources" \
        "$src/ember/tools/umoxa-g2-txq-test.c" "$sources/umoxa_g2_txq.c" "$sources/umoxa_frame.c" \
        -o "$out/test-sanitized" > "$out/build-sanitized.log" 2>&1
    "$out/test-sanitized" > "$out/run-sanitized.log" 2>&1 || \
        { cat "$out/run-sanitized.log" >&2; exit 1; }
    echo 'ASan/UBSan: PASS'
else
    echo 'ASan/UBSan: SKIP (compiler/linker unsupported; see sanitizer-probe.log)'
fi
hash() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1"
    else sha256 "$1"; fi
}
for file in "$sources/umoxa_frame.h" "$sources/umoxa_frame.c" "$sources/umoxa_g2_txq.h" "$sources/umoxa_g2_txq.c" "$sources/umoxa_frame.c" \
    "$src/ember/tools/umoxa-g2-txq-test.c" "$src/ember/tools/umoxa-g2-txq-test.sh"; do
    hash "$file" >> "$out/sources.sha256"
done
printf 'host shared TX scheduling contract only; no USB/TTY/hardware check\n' > "$out/scope.txt"
