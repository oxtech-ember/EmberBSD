#!/bin/sh
# Origin: EmberBSD (AI-assisted), check DWARF5 conversion with actual ELF types.
set -eu
[ "$#" = 4 ] || { echo "Usage: $0 TARGET_GCC HOST_CLANG OS_TOOLDIR NEW_WORK" >&2; exit 2; }
gcc=$1 clang=$2 tools=$3 work=$4
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$work"
convert=$tools/bin/nbctfconvert
dump=$tools/bin/nbctfdump
objcopy=${gcc%gcc}objcopy
readelf=${gcc%gcc}readelf
for compiler in gcc clang; do
    for version in 4 5; do
        for width in 32 64; do
            case $compiler in gcc) set -- "$gcc" ;; clang) set -- "$clang" --target=aarch64--netbsd ;; esac
            name=$compiler-$version-$width
            "$@" -std=c11 -O2 -gdwarf-$version -gdwarf$width \
                -c "$root/ctf-dwarf/types.c" -o "$work/$name.o"
            cp "$work/$name.o" "$work/$name-input.o"
            "$readelf" --debug-dump=info "$work/$name.o" > "$work/$name-dwarf.txt"
            grep -q "Version: *$version" "$work/$name-dwarf.txt"
            "$convert" -g -l EmberBSD "$work/$name.o"
            "$dump" -t "$work/$name.o" > "$work/$name.txt"
            grep -q 'STRUCT ember_record (72 bytes)' "$work/$name.txt"
            grep -q 'ARRAY .*nelems: 3' "$work/$name.txt"
            grep -q 'code type=.*off=385' "$work/$name.txt"
            grep -q 'delta type=.*off=416' "$work/$name.txt"
            grep -q 'EMBER_NEGATIVE = -3' "$work/$name.txt"
            grep -q 'callback type=.*off=512' "$work/$name.txt"
            cmp "$work/gcc-4-32.txt" "$work/$name.txt"
        done
    done
done
echo 'PASS: GCC/Clang DWARF4/5, DWARF32/64 produce identical checked CTF types'

# More than 256 strings require strx2; the final table entry needs relocation.
awk 'BEGIN { print "struct ember_large {"; for(i=0;i<300;i++) printf "long slot_%03d;\n",i; print "}; struct ember_large ember_big;" }' > "$work/large.c"
"$clang" --target=aarch64--netbsd -O2 -gdwarf-5 -c "$work/large.c" -o "$work/large.o"
"$readelf" --debug-dump=abbrev "$work/large.o" > "$work/large-abbrev.txt"
grep -q DW_FORM_strx2 "$work/large-abbrev.txt"
"$convert" -g -l EmberBSD "$work/large.o"
"$dump" -t "$work/large.o" > "$work/large.txt"
grep -q 'STRUCT ember_large (2400 bytes)' "$work/large.txt"
grep -q 'slot_299 type=.*off=19136' "$work/large.txt"
echo 'PASS: strx2 names and relocation at the exact section boundary are preserved'

# Link once so debug relocations cannot prevent malformed-input construction.
"${gcc%gcc}ld" -shared "$work/clang-5-32-input.o" -o "$work/negative-input.so"
for section in .debug_str_offsets .debug_str; do
    name=$(printf '%s' "$section" | tr . _)
    "$objcopy" --remove-section="$section" --remove-section=".rela$section" \
        "$work/negative-input.so" "$work/missing$name.o"
    if "$convert" -g -l EmberBSD "$work/missing$name.o" > "$work/missing$name.log" 2>&1; then
        echo "Accepted missing $section" >&2; exit 1
    fi
    grep -q 'failed to get string' "$work/missing$name.log"
done
# A truncated offset table has no valid entries; a nonterminated string is invalid.
printf '\004\000\000\000\005\000\000\000' > "$work/short-offsets"
printf 'XXXXXXXX' > "$work/bad-strings"
for kind in offsets strings; do
    case $kind in offsets) section=.debug_str_offsets; data=$work/short-offsets ;; strings) section=.debug_str; data=$work/bad-strings ;; esac
    "$objcopy" --update-section="$section=$data" "$work/negative-input.so" "$work/bad-$kind.o"
    if "$convert" -g -l EmberBSD "$work/bad-$kind.o" > "$work/bad-$kind.log" 2>&1; then
        echo "Accepted malformed $section" >&2; exit 1
    fi
    grep -q 'failed to get string' "$work/bad-$kind.log"
done
echo 'PASS: missing/truncated string tables fail instead of silently losing type names'
"${gcc%gcc}ld" -r "$work/gcc-5-32.o" "$work/large.o" -o "$work/merged.o"
"$tools/bin/nbctfmerge" -g -l EmberBSD -o "$work/merged.o" "$work/gcc-5-32.o" "$work/large.o"
"$dump" -t "$work/merged.o" > "$work/merged.txt"
grep -q 'STRUCT ember_record (72 bytes)' "$work/merged.txt"
grep -q 'STRUCT ember_large (2400 bytes)' "$work/merged.txt"
echo 'PASS: GCC/Clang DWARF5 CTF records survive the link/ctfmerge pipeline'
# Repeated zero-offset members make GCC choose implicit constants in abbreviations.
awk 'BEGIN {for(i=0;i<32;i++) printf "struct ember_single_%d { long value; }; struct ember_single_%d ember_single_data_%d;\n",i,i,i}' > "$work/implicit.c"
for version in 4 5; do
    "$gcc" -O2 -gdwarf-$version -c "$work/implicit.c" -o "$work/implicit-$version.o"
    "$readelf" --debug-dump=abbrev "$work/implicit-$version.o" > "$work/implicit-$version-abbrev.txt"
    "$convert" -g -l EmberBSD "$work/implicit-$version.o"
    "$dump" -t "$work/implicit-$version.o" > "$work/implicit-$version.txt"
    grep -q 'STRUCT ember_single_31 (8 bytes)' "$work/implicit-$version.txt"
done
grep -q 'DW_AT_data_member_location DW_FORM_implicit_const' "$work/implicit-5-abbrev.txt"
cmp "$work/implicit-4.txt" "$work/implicit-5.txt"
echo 'PASS: implicit-constant sizes and member offsets match DWARF4 output'
# GCC uses line_strp for CU names; it must receive the same bounded handling.
"${gcc%gcc}ld" -shared "$work/gcc-5-32-input.o" -o "$work/gcc-negative-input.so"
for kind in missing truncated; do
    case $kind in
    missing) "$objcopy" --remove-section=.debug_line_str "$work/gcc-negative-input.so" "$work/line-$kind.so" ;;
    truncated) "$objcopy" --update-section=".debug_line_str=$work/bad-strings" "$work/gcc-negative-input.so" "$work/line-$kind.so" ;;
    esac
    if "$convert" -g -l EmberBSD "$work/line-$kind.so" > "$work/line-$kind.log" 2>&1; then
        echo 'Accepted invalid line_strp table' >&2; exit 1
    fi
    grep -q 'Invalid attribute form' "$work/line-$kind.log"
done
# Mutate the header while retaining all table bytes: ELF section size is no guard.
for kind in short-length bad-version bad-padding; do
    "$objcopy" --dump-section ".debug_str_offsets=$work/$kind-table" "$work/negative-input.so"
    case $kind in
    short-length) printf '\004\000\000\000' | dd of="$work/$kind-table" bs=1 conv=notrunc 2>/dev/null ;;
    bad-version) printf '\006\000' | dd of="$work/$kind-table" bs=1 seek=4 conv=notrunc 2>/dev/null ;;
    bad-padding) printf '\001\000' | dd of="$work/$kind-table" bs=1 seek=6 conv=notrunc 2>/dev/null ;;
    esac
    "$objcopy" --update-section ".debug_str_offsets=$work/$kind-table" "$work/negative-input.so" "$work/$kind.so"
    if "$convert" -g -l EmberBSD "$work/$kind.so" > "$work/$kind.log" 2>&1; then
        echo "Accepted invalid string-offset header: $kind" >&2; exit 1
    fi
    grep -q 'Invalid attribute form' "$work/$kind.log"
done
echo 'PASS: GCC line strings and DWARF5 contribution headers reject malformed input'
