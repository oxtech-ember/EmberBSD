#!/bin/sh
# Pass the target compiler and actual kernel flags, including include paths.
set -eu
[ "$#" -ge 1 ] || { echo "usage: $0 CLANG [KERNEL_FLAGS ...]" >&2; exit 2; }
source=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)/amdgpu-flex-const.c
work=$(mktemp -d "${TMPDIR:-/tmp}/amdgpu-flex-const.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$@" -Wno-missing-prototypes -fsyntax-only "$source"
if "$@" -Wno-missing-prototypes -DTEST_CONST_REJECTION -fsyntax-only "$source" >"$work/negative.log" 2>&1; then
    echo 'const-discard negative control unexpectedly compiled' >&2
    exit 1
fi
if ! grep -q 'discards qualifiers' "$work/negative.log"; then
    cat "$work/negative.log" >&2
    exit 1
fi
