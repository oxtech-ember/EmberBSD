#!/bin/sh
# Origin: EmberBSD (AI-assisted), native replay of linked multi-CU types.
set -eu
[ "$#" = 4 ] || { echo "Usage: $0 CTFCONVERT CTFDUMP HOST_FIXTURES NEW_WORK" >&2; exit 2; }
convert=$1 dump=$2 fixtures=$3 work=$4
[ "$(uname -s)" = NetBSD ] && [ "$(uname -p)" = aarch64 ]
[ ! -e "$work" ]
cp -R "$fixtures" "$work"
for version in 4 5; do
    for mode in ordinary split; do
        dir=$work/v$version-$mode
        cp "$dir/original" "$dir/target"
        "$dir/target"
        "$convert" -g -l EmberBSD "$dir/target"
        "$dump" -t -f -d "$dir/target" > "$dir/target.txt"
        cmp "$dir/types.txt" "$dir/target.txt"
        "$dir/target"
        if [ "$mode" = split ]; then
            mv "$dir/first.dwo" "$dir/first.saved"
            mv "$dir/second.dwo" "$dir/second.saved"
            cp "$dir/original" "$dir/target"
            cp "$dir/package.dwp" "$dir/target.dwp"
            "$convert" -g -l EmberBSD "$dir/target"
            "$dump" -t -f -d "$dir/target" > "$dir/target.txt"
            cmp "$dir/package.txt" "$dir/target.txt"
            "$dir/target"
            mv "$dir/first.saved" "$dir/first.dwo"
            mv "$dir/second.saved" "$dir/second.dwo"
        fi
    done
done
dir=$work/ambiguous
cp "$dir/original" "$dir/target"
if "$convert" -g -l EmberBSD "$dir/target" > "$dir/rejected-target.txt" 2>&1; then
    echo 'Accepted ambiguous static owners' >&2; exit 1
fi
grep -q 'ambiguous static DWARF symbol ownership' "$dir/rejected-target.txt"
cmp "$dir/original" "$dir/target"
echo 'PASS: six native multi-CU conversions match, run successfully and ambiguous owners fail atomically'
