#!/bin/sh
# Origin: EmberBSD (AI-assisted), native replay of checked external type units.
set -eu
[ "$#" = 4 ] || { echo "Usage: $0 CTFCONVERT CTFDUMP HOST_FIXTURES NEW_WORK" >&2; exit 2; }
convert=$1 dump=$2 fixtures=$3 work=$4
[ "$(uname -s)" = NetBSD ] && [ "$(uname -p)" = aarch64 ]
[ ! -e "$work" ]
cp -R "$fixtures" "$work"
normalize()
{
    "$dump" -t -f -d "$1" | sed '/ember_data/s/ ([0-9][0-9]*)$//' > "$2"
}
for compiler in gcc clang; do
    for version in 4 5; do
        for width in 32 64; do
            dir=$work/$compiler-v$version-w$width-types
            cp "$dir/original.o" "$dir/target.o"
            "$convert" -g -l EmberBSD "$dir/target.o"
            normalize "$dir/target.o" "$dir/target.txt"
            cmp "$dir/types.txt" "$dir/target.txt"
        done
    done
    for version in 4 5; do
    for width in 32 64; do
        for units in plain types; do
            dir=$work/$compiler-v$version-w$width-split-$units
            cp "$dir/original.o" "$dir/target.o"
            "$convert" -g -l EmberBSD "$dir/target.o"
            normalize "$dir/target.o" "$dir/target.txt"
            cmp "$dir/types.txt" "$dir/target.txt"
            mv "$dir/input.dwo" "$dir/standalone.dwo"
            cp "$dir/original.o" "$dir/target.o"
            cp "$dir/package.o.dwp" "$dir/target.o.dwp"
            "$convert" -g -l EmberBSD "$dir/target.o"
            normalize "$dir/target.o" "$dir/target.txt"
            cmp "$dir/package.txt" "$dir/target.txt"
            mv "$dir/standalone.dwo" "$dir/input.dwo"
        done
    done
    done
done
for width in 32 64; do
    for ref in 4 8; do
        dir=$work/combined-$width-$ref
        cp "$dir/original.o" "$dir/target.o"
        "$convert" -g -l EmberBSD "$dir/target.o"
        "$dump" -t -d "$dir/target.o" | sed 's/ ([0-9][0-9]*)$//' > "$dir/target.txt"
        cmp "$dir/types.txt" "$dir/target.txt"
    done
done
reject()
{
    cp "$1" "$work/before.o"
    if "$convert" -g -l EmberBSD "$1" > "$work/negative.log" 2>&1; then
        echo "Accepted invalid fixture: $1" >&2; exit 1
    fi
    cmp "$1" "$work/before.o"
}
dir=$work/gcc-v5-w32-split-types
mv "$dir/input.dwo" "$dir/standalone.dwo"
for name in missing-index short-index short-info; do
    cp "$dir/original.o" "$dir/$name.o"
    reject "$dir/$name.o"
done
mv "$dir/standalone.dwo" "$dir/input.dwo"
dir=$work/combined-32-4
mv "$dir/types.sup" "$dir/saved.sup"
cp "$dir/static-expr.sup" "$dir/types.sup"
cp "$dir/original.o" "$dir/target.o"
"$convert" -g -l EmberBSD "$dir/target.o"
"$dump" -t -d "$dir/target.o" | sed 's/ ([0-9][0-9]*)$//' > "$dir/target.txt"
cmp "$dir/types.txt" "$dir/target.txt"
cp "$dir/unsupported-expr.sup" "$dir/types.sup"
cp "$dir/original.o" "$dir/target.o"
reject "$dir/target.o"
mv "$dir/saved.sup" "$dir/types.sup"
dir=$work/clang-v5-w32-split-types
mv "$dir/input.dwo" "$dir/saved.dwo"
cp "$dir/mixed.dwo" "$dir/input.dwo"
cp "$dir/original.o" "$dir/target.o"
"$convert" -g -l EmberBSD "$dir/target.o"
normalize "$dir/target.o" "$dir/target.txt"
cmp "$dir/types.txt" "$dir/target.txt"
mv "$dir/saved.dwo" "$dir/input.dwo"
echo 'PASS: native CTF matches 46 host results and atomically rejects four invalid DWP/expression cases'
