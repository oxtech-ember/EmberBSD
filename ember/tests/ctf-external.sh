#!/bin/sh
# Origin: EmberBSD (AI-assisted), actual split and supplementary DWARF contract.
set -eu
[ "$#" = 4 ] || { echo "Usage: $0 TARGET_GCC HOST_CLANG OS_TOOLDIR NEW_WORK" >&2; exit 2; }
gcc=$1 clang=$2 tools=$3 work=$4
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$work"
work=$(CDPATH= cd -- "$work" && pwd)
convert=$tools/bin/nbctfconvert dump=$tools/bin/nbctfdump
objcopy=${gcc%gcc}objcopy
# Compare complete types, function signatures and global-object ownership.
"$gcc" -O2 -gdwarf-5 -c "$root/ctf-dwarf/types.c" -o "$work/reference.o"
"$convert" -g -l EmberBSD "$work/reference.o"
"$dump" -t -f -d "$work/reference.o" > "$work/reference-raw.txt"
sed '/ember_data/s/ ([0-9][0-9]*)$//' "$work/reference-raw.txt" > "$work/reference.txt"
for compiler in gcc clang; do
    for width in 32 64; do
        dir=$work/$compiler-$width
        mkdir "$dir"
        case $compiler in gcc) set -- "$gcc" ;; clang) set -- "$clang" --target=aarch64--netbsd ;; esac
        (cd "$dir"; "$@" -O2 -gdwarf-5 -gdwarf$width -gsplit-dwarf \
            -c "$root/ctf-dwarf/types.c" -o input.o)
        cp "$dir/input.o" "$dir/original.o"
        "$convert" -g -l EmberBSD "$dir/input.o"
        # ELF symbol indices vary when the compiler extracts debug sections.
        "$dump" -t -f -d "$dir/input.o" > "$dir/raw.txt"
        sed '/ember_data/s/ ([0-9][0-9]*)$//' "$dir/raw.txt" > "$dir/types.txt"
        cmp "$work/reference.txt" "$dir/types.txt"
    done
done
echo 'PASS: GCC/Clang split DWARF32/64 preserve types, function signatures and objects'

# Failed conversion must be diagnosed and leave the original ELF untouched.
reject()
{
    file=$1 pattern=$2
    cp "$file" "$work/before.o"
    if "$convert" -g -l EmberBSD "$file" > "$work/reject.log" 2>&1; then
        echo "Accepted invalid external DWARF: $file" >&2; exit 1
    fi
    grep -q "$pattern" "$work/reject.log"
    cmp "$file" "$work/before.o"
}
dir=$work/gcc-32
mv "$dir/input.dwo" "$dir/saved.dwo"
reject "$dir/original.o" 'cannot open external DWARF'
cp "$work/clang-32/input.dwo" "$dir/input.dwo"
reject "$dir/original.o" 'dwo_id mismatch'
mv "$dir/saved.dwo" "$dir/input.dwo"
cp "$dir/input.dwo" "$dir/saved.dwo"
# Header ends before the dwo_id; the section is too short for the declared CU.
printf '\001\000' > "$work/short"
"$objcopy" --update-section ".debug_info.dwo=$work/short" "$dir/saved.dwo" "$dir/input.dwo"
reject "$dir/original.o" 'invalid split DWARF header'
# Validate the implicit split string-offset contribution, including its last entry.
"$objcopy" --update-section ".debug_str_offsets.dwo=$work/short" "$dir/saved.dwo" "$dir/input.dwo"
reject "$dir/original.o" 'Invalid attribute form'
mv "$dir/saved.dwo" "$dir/input.dwo"
# A relocatable link must retain both skeleton CUs and their type namespaces.
for name in first second; do
    printf 'struct %s_record { long value; }; struct %s_record %s_data;\n' "$name" "$name" "$name" > "$work/$name.c"
    (cd "$work"; "$gcc" -gdwarf-5 -gsplit-dwarf -c "$name.c" -o "$name.o")
done
"${gcc%gcc}ld" -r "$work/first.o" "$work/second.o" -o "$work/multiple.o"
cp "$work/multiple.o" "$work/multiple-original.o"
"$convert" -g -l EmberBSD "$work/multiple.o"
"$dump" -t -d "$work/multiple.o" > "$work/multiple.txt"
grep -q 'STRUCT first_record' "$work/multiple.txt"
grep -q 'STRUCT second_record' "$work/multiple.txt"
echo 'PASS: missing/mismatched/truncated DWO fail atomically; multiple skeleton CUs retain both types'


for width in 32 64; do
    for ref in 4 8; do
        dir=$work/sup-$width-$ref
        mkdir "$dir"
        for role in 0 1; do
            case $role in 0) output=$dir/main.o ;; 1) output=$dir/types.sup ;; esac
            "$gcc" -c -x assembler-with-cpp -DSUPPLEMENT=$role \
                -DDWARF64=$((width == 64)) -DREF8=$((ref == 8)) \
                "$root/ctf-dwarf/supplementary.S" -o "$output"
        done
        cp "$dir/main.o" "$dir/original.o"
        "$convert" -g -l EmberBSD "$dir/main.o"
        "$dump" -t -d "$dir/main.o" > "$dir/types.txt"
        grep -q 'STRUCT ember_sup_record (16 bytes)' "$dir/types.txt"
        grep -q 'id type=.*off=0' "$dir/types.txt"
        grep -q 'flags type=.*off=64' "$dir/types.txt"
        grep -q 'ember_sup_global' "$dir/types.txt"
        awk '/INTEGER unsigned int .*bits=32/ { id=$1; gsub(/[^0-9]/, "", id) } /ember_sup_inherited/ { type=$2 } END { exit !(id != "" && type == id) }' "$dir/types.txt"
        cmp "$work/sup-32-4/types.txt" "$dir/types.txt"
    done
done
echo 'PASS: supplementary strings, ref_sup4/ref_sup8, imported units and cross-CU types (32/64)'

dir=$work/sup-32-4
mv "$dir/types.sup" "$dir/saved.sup"
reject "$dir/original.o" 'cannot open external DWARF'
cp "$dir/saved.sup" "$dir/types.sup"
"$objcopy" --dump-section ".debug_sup=$work/sup-header" "$dir/types.sup"
printf X | dd of="$work/sup-header" bs=1 seek=5 conv=notrunc 2>/dev/null
"$objcopy" --update-section ".debug_sup=$work/sup-header" "$dir/saved.sup" "$dir/types.sup"
reject "$dir/original.o" 'checksum mismatch'
"$objcopy" --update-section ".debug_sup=$work/short" "$dir/saved.sup" "$dir/types.sup"
reject "$dir/original.o" 'invalid .debug_sup header'
"$objcopy" --update-section ".debug_info=$work/short" "$dir/saved.sup" "$dir/types.sup"
reject "$dir/original.o" 'invalid supplementary.*DWARF'
mv "$dir/saved.sup" "$dir/types.sup"
"$objcopy" --remove-section .debug_sup "$dir/original.o" "$dir/missing-header.o"
reject "$dir/missing-header.o" 'failed to get string'
"$objcopy" --dump-section ".debug_info=$work/bad-ref" "$dir/original.o"
printf '\377\377\377\177' | dd of="$work/bad-ref" bs=1 seek=23 conv=notrunc 2>/dev/null
"$objcopy" --update-section ".debug_info=$work/bad-ref" "$dir/original.o" "$dir/bad-ref.o"
reject "$dir/bad-ref.o" 'invalid supplementary DWARF reference'
# A valid CU length must not allow a fixed reference to overread its body.
for ref in 4 8; do
    dir=$work/sup-32-$ref
    "$objcopy" --dump-section ".debug_info=$work/full-info" "$dir/original.o"
    dd if="$work/full-info" of="$work/short-ref" bs=1 count=25 2>/dev/null
    printf '\025\000\000\000' | dd of="$work/short-ref" bs=1 conv=notrunc 2>/dev/null
    "$objcopy" --update-section ".debug_info=$work/short-ref" "$dir/original.o" "$dir/short-ref.o"
    reject "$dir/short-ref.o" 'Invalid attribute form'
done
echo 'PASS: missing/mismatched/malformed supplements and dangling/truncated references fail atomically'

# A local reference may never alias a supplementary type's synthetic ID.
dir=$work/sup-32-4
"$gcc" -c -x assembler-with-cpp -DSUPPLEMENT=0 -DLOCAL_COLLISION=1 \
    -DDWARF64=0 -DREF8=0 "$root/ctf-dwarf/supplementary.S" -o "$dir/local-collision.o"
reject "$dir/local-collision.o" 'invalid DWARF type reference'
"$gcc" -c -x assembler-with-cpp -DSUPPLEMENT=1 -DBAD_ORIGIN=1 \
    -DDWARF64=0 -DREF8=0 "$root/ctf-dwarf/supplementary.S" -o "$dir/bad-origin.sup"
mv "$dir/types.sup" "$dir/saved.sup"
mv "$dir/bad-origin.sup" "$dir/types.sup"
reject "$dir/original.o" 'invalid CU-relative DWARF reference'
mv "$dir/saved.sup" "$dir/types.sup"
echo 'PASS: inherited types preserve CU/object namespaces; cross-CU local references and collisions fail'
