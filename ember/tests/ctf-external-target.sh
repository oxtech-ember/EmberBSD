#!/bin/sh
# Origin: EmberBSD (AI-assisted), execute cross-built CTF tools on their target.
set -eu
[ "$#" = 4 ] || { echo "Usage: $0 CTFCONVERT CTFDUMP HOST_FIXTURES NEW_WORK" >&2; exit 2; }
convert=$1 dump=$2 fixtures=$3 work=$4
[ "$(uname -s)" = NetBSD ] && [ "$(uname -p)" = aarch64 ]
[ ! -e "$work" ]
cp -R "$fixtures" "$work"
for name in gcc-32 gcc-64 clang-32 clang-64 sup-32-4 sup-32-8 sup-64-4 sup-64-8; do
    dir=$work/$name
    cp "$dir/original.o" "$dir/target.o"
    "$convert" -g -l EmberBSD "$dir/target.o"
    case $name in
    sup-*) "$dump" -t -d "$dir/target.o" > "$dir/target.txt" ;;
    *) "$dump" -t -f -d "$dir/target.o" > "$dir/raw.txt"
       sed '/ember_data/s/ ([0-9][0-9]*)$//' "$dir/raw.txt" > "$dir/target.txt" ;;
    esac
    cmp "$dir/types.txt" "$dir/target.txt"
done
for name in sup-32-4/short-ref sup-32-8/short-ref sup-32-4/bad-ref \
    sup-32-4/local-collision sup-32-4/missing-header; do
    cp "$work/$name.o" "$work/before.o"
    if "$convert" -g -l EmberBSD "$work/$name.o" > "$work/negative.log" 2>&1; then
        echo "Accepted invalid fixture: $name" >&2; exit 1
    fi
    cmp "$work/before.o" "$work/$name.o"
done
cp "$work/multiple-original.o" "$work/multiple.o"
"$convert" -g -l EmberBSD "$work/multiple.o"
"$dump" -t -d "$work/multiple.o" > "$work/multiple-target.txt"
cmp "$work/multiple.txt" "$work/multiple-target.txt"
echo 'PASS: native CTF tools match nine external-DWARF results and reject five malformed inputs'
