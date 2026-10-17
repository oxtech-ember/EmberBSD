#!/bin/sh
# Origin: EmberBSD ucom transport state contract runner, 2026-10-09.
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
set -eu
umask 077
[ "$#" -eq 1 ] || { echo 'usage: ucom-transport-core-test.sh ABSOLUTE_NEW_OUTPUT' >&2; exit 1; }
out=$1
case "$out" in /*) ;; *) echo 'output must be absolute' >&2; exit 1 ;; esac
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
case "$out/" in "$src/"*) echo 'output must be outside source tree' >&2; exit 1 ;; esac
mkdir -p "$(dirname -- "$out")"
mkdir "$out" # Refuse to overwrite a previous run, including symlinks.
cc=${CC:-cc}
"$cc" --version > "$out/compiler.txt" 2>&1
"$cc" -std=c99 -O2 -Wall -Wextra -Werror -I "$src/sys/dev/usb" \
    "$src/ember/tools/ucom-transport-core-test.c" "$src/sys/dev/usb/ucom_transport_core.c" \
    -o "$out/test" > "$out/build.log" 2>&1 || { cat "$out/build.log" >&2; exit 1; }
"$out/test" > "$out/run.log" 2>&1 || { cat "$out/run.log" >&2; exit 1; }
cat "$out/run.log"
printf 'int main(void) { return 0; }\n' > "$out/sanitizer-probe.c"
if "$cc" -fsanitize=address,undefined "$out/sanitizer-probe.c" \
    -o "$out/sanitizer-probe" > "$out/sanitizer-probe.log" 2>&1; then
    "$cc" -std=c99 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer \
        -fsanitize=address,undefined -I "$src/sys/dev/usb" \
        "$src/ember/tools/ucom-transport-core-test.c" "$src/sys/dev/usb/ucom_transport_core.c" \
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
for file in "$src/sys/dev/usb/ucom_transport_core.h" "$src/sys/dev/usb/ucom_transport_core.c" \
    "$src/ember/tools/ucom-transport-core-test.c" "$src/ember/tools/ucom-transport-core-test.sh"; do
    hash "$file" >> "$out/sources.sha256"
done
printf 'host sequential state contract only; no USB/TTY/hardware check\n' > "$out/scope.txt"
