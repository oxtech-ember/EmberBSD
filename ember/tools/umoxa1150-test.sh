#!/bin/sh
# Origin: EmberBSD UPort 1150 host contract runner, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
set -eu
umask 077
[ "$#" -eq 1 ] || { echo 'usage: umoxa1150-test.sh ABSOLUTE_NEW_OUTPUT' >&2; exit 1; }
case "$1" in /*) ;; *) exit 1 ;; esac
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
mkdir -p "$(dirname -- "$1")"
parent=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd -P)
out=$parent/$(basename -- "$1")
case "$out/" in "$src/"*) echo 'output must be outside source tree' >&2; exit 1 ;; esac
mkdir "$out"
python3 - "$src" "$out" <<'HASH'
import hashlib,pathlib,sys
src,out=map(pathlib.Path,sys.argv[1:])
files=[src/'sys/dev/usb/umoxa1150.c',src/'sys/dev/usb/umoxa1150_protocol.c',src/'sys/dev/usb/umoxa1150_protocol.h',src/'sys/dev/usb/umoxa1150io.h',src/'sys/dev/usb/ucom.c',src/'sys/dev/usb/ucomvar.h']
files+=list((src/'ember/tests/umoxa1150').rglob('*'))
files+=list((src/'ember/tools').glob('umoxa1150*'))+[src/'ember/tools/ucom-strict-param-test.sh']
(out/'sources.sha256').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(src))+'\n' for p in sorted(set(files)) if p.is_file()))
HASH
cc=${CC:-cc}
"$cc" --version > "$out/compiler.txt"
for mode in plain sanitized; do
    flags='-O2'
    [ "$mode" = plain ] || flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined'
    "$cc" -std=c99 -Wall -Wextra -Werror $flags -I "$src/sys/dev/usb" \
        "$src/ember/tools/umoxa1150-test.c" "$src/sys/dev/usb/umoxa1150_protocol.c" \
        -o "$out/test-$mode" > "$out/build-$mode.log" 2>&1 || { cat "$out/build-$mode.log"; exit 1; }
    "$out/test-$mode" > "$out/run-$mode.log" 2>&1 || { cat "$out/run-$mode.log"; exit 1; }
    cat "$out/run-$mode.log"
done
printf 'UPort1150 wire/lifecycle host contracts; no physical USB or serial acceptance\n' > "$out/scope.txt"
