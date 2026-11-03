#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Run the standalone target-format checks with the caller-selected native CC.
set -eu
if [ "$#" -ne 6 ]; then
    echo "usage: $0 OS_SOURCE WORK LINK_LAUNCHER LOCK_ROOT LOCK_CONTEXT LINT2" >&2
    exit 2
fi
: "${CC:?run through the selected native-check compiler binding}"
: "${PYTHON:?run through the selected native-check Python}"
src=$1
work=$2
launcher=$3
lock_root=$4
lock_context=$5
lint2=$6
case "$src:$work:$launcher:$lock_root:$lock_context:$lint2" in
    /*:/*:/*:/*:/*:/*) ;;
    *) echo 'all paths must be absolute' >&2; exit 2 ;;
esac
mkdir -p "$work"
lint=$src/usr.bin/xlint/lint1
gdtoa=$src/lib/libc/gdtoa
for source in lint_fp lint_softfloat lint_fp_parse lint_fp_format; do
    "$CC" -std=c11 -DGDTOA_XLINT_HOST -I"$src/usr.bin/xlint/common" \
        -I"$lint" -I"$lint/gdtoa-host" -I"$gdtoa" -c "$lint/$source.c" -o "$work/$source.o"
done
for source in strtodg gethex hexnan misc smisc gmisc hd_init gdtoa dmisc; do
    "$CC" -std=c11 -DGDTOA_XLINT_HOST -I"$lint/gdtoa-host" -I"$gdtoa" \
        -c "$gdtoa/$source.c" -o "$work/$source.o"
done
for test in lint_fp_test lint_fp_parse_test lint_fp_format_test; do
    "$CC" -std=c11 -I"$src/usr.bin/xlint/common" -c "$lint/tests/$test.c" -o "$work/$test.o"
    "$PYTHON" -B "$launcher" --lock-root "$lock_root" --context "$lock_context" -- \
        "$CC" "$work/$test.o" "$work/lint_fp.o" "$work/lint_softfloat.o" \
        "$work/lint_fp_parse.o" "$work/lint_fp_format.o" \
        "$work/strtodg.o" "$work/gethex.o" "$work/hexnan.o" "$work/misc.o" \
        "$work/smisc.o" "$work/gmisc.o" "$work/hd_init.o" "$work/gdtoa.o" \
        "$work/dmisc.o" -lm -o "$work/$test"
    "$work/$test"
done

# Exercise lint2's type reader and its separate library writer.
fixture=$src/tests/usr.bin/xlint/float128
"$PYTHON" -B -c 'import pathlib,sys; s=pathlib.Path(sys.argv[1]).read_text().splitlines(); pathlib.Path(sys.argv[2]).write_text("\n".join("".join(line.split()) for line in s if line.strip() and not line.lstrip().startswith("#"))+"\n")' "$fixture/wire.ln" "$work/wire.ln"
(cd "$work" && "$lint2" -h -x -C float128 "$work/wire.ln")
"$PYTHON" -B -c 'import pathlib,sys; norm=lambda p: ["".join(line.split()) for line in pathlib.Path(p).read_text().splitlines() if line.strip() and not line.lstrip().startswith("#")]; assert norm(sys.argv[1]) == norm(sys.argv[2]), "lint2 wire roundtrip differs"' "$fixture/wire.exp-ln" "$work/llib-lfloat128.ln"
echo 'PASS: lint2 float128 wire roundtrip'
