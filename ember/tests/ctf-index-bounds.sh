#!/bin/sh
# Origin: EmberBSD (AI-assisted), compile or replay indexed operand bounds.
set -eu
[ "$#" = 3 ] || { echo "Usage: $0 TARGET_CC_OR_REPLAY API_TEST WORK" >&2; exit 2; }
compiler=$1 test=$2 work=$3
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$compiler" != replay ]; then mkdir "$work"; fi
for form in 0x1f01 0x1f02 0x1a 0x1b; do
    for kind in 0 1 2 3; do
        file=$work/form-$form-kind-$kind.o
        if [ "$compiler" != replay ]; then
            "$compiler" -c -x assembler-with-cpp -DFORM=$form -DKIND=$kind \
                "$root/ctf-dwarf/index-bounds.S" -o "$file"
        fi
        "$test" "$file" "$kind"
    done
done
echo 'PASS: 12 missing/truncated/overflow indices rejected; four full uint64 indices accepted'
