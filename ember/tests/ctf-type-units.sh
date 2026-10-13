#!/bin/sh
# Origin: EmberBSD (AI-assisted), real COMDAT type units and indexed DWP input.
set -eu
[ "$#" = 5 ] || { echo "Usage: $0 TARGET_GCC HOST_CLANG LLVM_DWP OS_TOOLDIR NEW_WORK" >&2; exit 2; }
gcc=$1 clang=$2 dwp=$3 tools=$4 work=$5
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$work"
work=$(CDPATH= cd -- "$work" && pwd)
convert=$tools/bin/nbctfconvert dump=$tools/bin/nbctfdump
objcopy=${gcc%gcc}objcopy
normalize()
{
    "$dump" -t -f -d "$1" | sed '/ember_data/s/ ([0-9][0-9]*)$//' > "$2"
}
"$gcc" -O2 -gdwarf-5 -c "$root/ctf-dwarf/types.c" -o "$work/reference.o"
"$convert" -g -l EmberBSD "$work/reference.o"
normalize "$work/reference.o" "$work/reference.txt"
for compiler in gcc clang; do
    case $compiler in gcc) set -- "$gcc" ;; clang) set -- "$clang" --target=aarch64--netbsd ;; esac
    for version in 4 5; do
        for width in 32 64; do
            dir=$work/$compiler-v$version-w$width-types
            mkdir "$dir"
            "$@" -O2 -gdwarf-$version -gdwarf$width -fdebug-types-section \
                -c "$root/ctf-dwarf/type-units.cc" -o "$dir/input.o"
            cp "$dir/input.o" "$dir/original.o"
            "$convert" -g -l EmberBSD "$dir/input.o"
            normalize "$dir/input.o" "$dir/types.txt"
            cmp "$work/reference.txt" "$dir/types.txt"
        done
    done
    for version in 4 5; do
    for width in 32 64; do
        for units in plain types; do
            dir=$work/$compiler-v$version-w$width-split-$units
            mkdir "$dir"
            case $units in types) extra=-fdebug-types-section ;; plain) extra= ;; esac
            (cd "$dir"; "$@" -O2 -gdwarf-$version -gdwarf$width -gsplit-dwarf $extra \
                -c "$root/ctf-dwarf/type-units.cc" -o input.o)
            cp "$dir/input.o" "$dir/original.o"
            "$convert" -g -l EmberBSD "$dir/input.o"
            normalize "$dir/input.o" "$dir/types.txt"
            cmp "$work/reference.txt" "$dir/types.txt"
            printf 'struct helper_record { unsigned short other; }; struct helper_record helper_data;\n' > "$dir/helper.c"
            (cd "$dir"; "$@" -O2 -gdwarf-$version -gdwarf$width -gsplit-dwarf $extra \
                -c helper.c -o helper.o)
            "$dwp" -o "$dir/package.o.dwp" "$dir/helper.dwo" "$dir/input.dwo"
            mv "$dir/input.dwo" "$dir/standalone.dwo"
            cp "$dir/original.o" "$dir/package.o"
            "$convert" -g -l EmberBSD "$dir/package.o"
            normalize "$dir/package.o" "$dir/package.txt"
            cmp "$work/reference.txt" "$dir/package.txt"
            mv "$dir/standalone.dwo" "$dir/input.dwo"
        done
    done
    done
done
echo 'PASS: GCC/Clang ordinary type units and standalone/packaged DWARF4/5 preserve complete CTF types'

for width in 32 64; do
    for ref in 4 8; do
        dir=$work/combined-$width-$ref
        mkdir "$dir"
        "$gcc" -c -x assembler-with-cpp -DDWARF64=$((width == 64)) \
            "$root/ctf-dwarf/skeleton.S" -o "$dir/input.o"
        "$gcc" -c -x assembler-with-cpp -DSPLIT=1 -DSUPPLEMENT=0 \
            -DDWARF64=$((width == 64)) -DREF8=$((ref == 8)) \
            "$root/ctf-dwarf/supplementary.S" -o "$dir/input.dwo"
        "$gcc" -c -x assembler-with-cpp -DSUPPLEMENT=1 \
            -DDWARF64=$((width == 64)) -DREF8=$((ref == 8)) \
            "$root/ctf-dwarf/supplementary.S" -o "$dir/types.sup"
        cp "$dir/input.o" "$dir/original.o"
        "$convert" -g -l EmberBSD "$dir/input.o"
        "$dump" -t -d "$dir/input.o" | sed 's/ ([0-9][0-9]*)$//' > "$dir/types.txt"
        grep -q 'STRUCT ember_sup_record (16 bytes)' "$dir/types.txt"
        grep -q 'ember_sup_inherited' "$dir/types.txt"
        cmp "$work/combined-32-4/types.txt" "$dir/types.txt"
    done
done
echo 'PASS: combined split+supplementary DWARF32/64 resolves strings, imported units and inherited types'

reject()
{
    file=$1 pattern=$2
    cp "$file" "$work/before.o"
    if "$convert" -g -l EmberBSD "$file" > "$work/reject.log" 2>&1; then
        echo "Accepted invalid DWARF: $file" >&2; exit 1
    fi
    grep -q "$pattern" "$work/reject.log"
    cmp "$file" "$work/before.o"
}
dir=$work/gcc-v5-w32-split-types
mv "$dir/input.dwo" "$dir/standalone.dwo"
cp "$dir/package.o.dwp" "$dir/saved.dwp"
# DWP type signatures must resolve only through the advertised TU index.
"$objcopy" --remove-section .debug_tu_index "$dir/saved.dwp" "$dir/package.o.dwp"
cp "$dir/original.o" "$dir/package.o"
reject "$dir/package.o" 'unresolved DWARF type signature'
cp "$dir/package.o.dwp" "$dir/missing-index.o.dwp"
printf '\001\000' > "$work/short"
"$objcopy" --update-section ".debug_cu_index=$work/short" "$dir/saved.dwp" "$dir/package.o.dwp"
reject "$dir/package.o" 'invalid DWARF package index'
cp "$dir/package.o.dwp" "$dir/short-index.o.dwp"
"$objcopy" --update-section ".debug_info.dwo=$work/short" "$dir/saved.dwp" "$dir/package.o.dwp"
reject "$dir/package.o" 'invalid DWARF package index or contribution'
cp "$dir/package.o.dwp" "$dir/short-info.o.dwp"
mv "$dir/saved.dwp" "$dir/package.o.dwp"
mv "$dir/standalone.dwo" "$dir/input.dwo"
echo 'PASS: missing TU index and truncated DWP index/contributions fail without modifying ELF'

dir=$work/combined-32-4
cp "$dir/types.sup" "$dir/saved.sup"
"$gcc" -c -x assembler-with-cpp -DSUPPLEMENT=1 -DMEMEXPR=1 \
    -DDWARF64=0 -DREF8=0 "$root/ctf-dwarf/supplementary.S" -o "$dir/types.sup"
cp "$dir/types.sup" "$dir/static-expr.sup"
cp "$dir/original.o" "$dir/expr.o"
"$convert" -g -l EmberBSD "$dir/expr.o"
"$dump" -t -d "$dir/expr.o" | sed 's/ ([0-9][0-9]*)$//' > "$dir/expr.txt"
cmp "$dir/types.txt" "$dir/expr.txt"
"$gcc" -c -x assembler-with-cpp -DSUPPLEMENT=1 -DMEMEXPR=2 \
    -DDWARF64=0 -DREF8=0 "$root/ctf-dwarf/supplementary.S" -o "$dir/types.sup"
cp "$dir/types.sup" "$dir/unsupported-expr.sup"
cp "$dir/original.o" "$dir/bad-expr.o"
reject "$dir/bad-expr.o" 'cannot parse member offset'
mv "$dir/saved.sup" "$dir/types.sup"
echo 'PASS: static exprloc member offsets work; unsupported trailing operators fail atomically'

# An unrelated ordinary table must not override the selected .dwo namespace.
dir=$work/clang-v5-w32-split-types
printf '\000' > "$work/unrelated-abbrev"
cp "$dir/input.dwo" "$dir/saved.dwo"
"$objcopy" --add-section ".debug_abbrev=$work/unrelated-abbrev" "$dir/saved.dwo" "$dir/input.dwo"
cp "$dir/input.dwo" "$dir/mixed.dwo"
cp "$dir/original.o" "$dir/mixed.o"
"$convert" -g -l EmberBSD "$dir/mixed.o"
normalize "$dir/mixed.o" "$dir/mixed.txt"
cmp "$dir/types.txt" "$dir/mixed.txt"
mv "$dir/saved.dwo" "$dir/input.dwo"
echo 'PASS: split abbreviation tables are isolated from unrelated ordinary tables'
