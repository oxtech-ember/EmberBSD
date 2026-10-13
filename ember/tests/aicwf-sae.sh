#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-sae.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror \
    "$src/ember/tools/aicwf-sae-contract.c" -o "$work/aicwf-sae"
"$work/aicwf-sae"
${CC:-cc} -std=c99 -Wall -Wextra -Werror \
    "$src/ember/tools/bwfm-sae-contract.c" -o "$work/bwfm-sae"
"$work/bwfm-sae"
