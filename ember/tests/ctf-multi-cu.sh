#!/bin/sh
# Origin: EmberBSD (AI-assisted), linked multi-CU ordinary, DWO and DWP types.
set -eu
[ "$#" = 4 ] || { echo "Usage: $0 TARGET_GCC LLVM_DWP OS_TOOLDIR NEW_WORK" >&2; exit 2; }
gcc=$1 dwp=$2 tools=$3 work=$4
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$work"
work=$(CDPATH= cd -- "$work" && pwd)
convert=$tools/bin/nbctfconvert dump=$tools/bin/nbctfdump
for version in 4 5; do
    for mode in ordinary split; do
        dir=$work/v$version-$mode
        mkdir "$dir"
        case $mode in ordinary) extra= ;; split) extra=-gsplit-dwarf ;; esac
        # Distinct original filenames are required for local-symbol ownership.
        for name in first second; do
            (cd "$dir"; "$gcc" -O0 -gdwarf-$version $extra \
                -c "$root/ctf-dwarf/multi-$name.c" -o "$name.o")
        done
        "$gcc" "$dir/first.o" "$dir/second.o" -o "$dir/input"
        cp "$dir/input" "$dir/original"
        "$convert" -g -l EmberBSD "$dir/input"
        "$dump" -t -f -d "$dir/input" > "$dir/types.txt"
        grep -q 'STRUCT private_record (8 bytes)' "$dir/types.txt"
        grep -q 'STRUCT private_record (16 bytes)' "$dir/types.txt"
        [ "$(grep -c 'private_data' "$dir/types.txt")" = 2 ]
        ! grep -q '0 .*private_data' "$dir/types.txt"
        # Package both primary CUs; each must use its own indexed contributions.
        if [ "$mode" = split ]; then
            "$dwp" -o "$dir/package.dwp" "$dir/first.dwo" "$dir/second.dwo"
            mv "$dir/first.dwo" "$dir/first.saved"
            mv "$dir/second.dwo" "$dir/second.saved"
            cp "$dir/original" "$dir/package"
            "$convert" -g -l EmberBSD "$dir/package"
            "$dump" -t -f -d "$dir/package" > "$dir/package.txt"
            cmp "$dir/types.txt" "$dir/package.txt"
            mv "$dir/first.saved" "$dir/first.dwo"
            mv "$dir/second.saved" "$dir/second.dwo"
        fi
    done
done
echo 'PASS: DWARF4/5 linked ordinary, split and packaged CUs retain both layouts and static objects'

# Two CU paths can have the same basename and static name. STT_FILE cannot
# distinguish these owners, so reject instead of silently assigning one type.
dir=$work/ambiguous
mkdir -p "$dir/a" "$dir/b"
cp "$root/ctf-dwarf/multi-first.c" "$dir/a/shared.c"
cp "$root/ctf-dwarf/multi-second.c" "$dir/b/shared.c"
for name in a b; do
    "$gcc" -O0 -gdwarf-5 -c "$dir/$name/shared.c" -o "$dir/$name.o"
done
"$gcc" "$dir/a.o" "$dir/b.o" -o "$dir/original"
cp "$dir/original" "$dir/input"
if "$convert" -g -l EmberBSD "$dir/input" > "$dir/rejected.txt" 2>&1; then
    echo 'Accepted ambiguous static owners' >&2; exit 1
fi
grep -q 'ambiguous static DWARF symbol ownership' "$dir/rejected.txt"
cmp "$dir/original" "$dir/input"
echo 'PASS: ambiguous static owners fail without modifying the executable'
