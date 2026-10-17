#!/bin/sh
# Origin: EmberBSD actual ucom parameter-section host runner, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
umask 077
[ "$#" -eq 1 ] || exit 1
case "$1" in /*) ;; *) exit 1 ;; esac
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
parent=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd -P)
out=$parent/$(basename -- "$1")
case "$out/" in "$src/"*) exit 1 ;; esac
mkdir "$out"
python3 - "$src/sys/dev/usb/ucom.c" "$out" <<'EXTRACT'
import pathlib,sys
source=pathlib.Path(sys.argv[1]).read_text();out=pathlib.Path(sys.argv[2])
a=source.index('static int\nucomparam(');b=source.index('static int\nucomhwiflow(',a)
(out/'actual-param.inc').write_text(source[a:b])
a=source.index('\t\tstruct termios t;',source.index('bool firstopen ='))
b=source.index('\t\ttp->t_iflag = TTYDEF_IFLAG;',a)
(out/'actual-initial.inc').write_text(source[a:b])
EXTRACT
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
 flags=-O2
 [ "$mode" = plain ] || flags='-O1 -g -fsanitize=address,undefined'
 "$cc" -std=c99 -Wall -Wextra -Werror -Wno-unused-function $flags -I "$out" -I "$src/ember/tests/umoxa1150" \
  "$src/ember/tests/umoxa1150/strict-param.c" -o "$out/test-$mode" > "$out/build-$mode.log" 2>&1 || { cat "$out/build-$mode.log"; exit 1; }
 "$out/test-$mode" > "$out/run-$mode.log" 2>&1 || { cat "$out/run-$mode.log"; exit 1; }
 cat "$out/run-$mode.log"
done
printf 'Unmodified ucom parameter sections, sequential host doubles; no native TTY/USB concurrency claim\n' > "$out/scope.txt"
