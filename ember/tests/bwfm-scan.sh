#!/bin/sh
# Compile the driver's scan metadata helpers against their real wire constants.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/bwfm-scan.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^#define BWFM_CHANSPEC_/ {print}' "$src/sys/dev/ic/bwfmreg.h" \
    > "$work/bwfm-scan-constants.h"
${CC:-cc} -std=c99 -Wall -Wextra -Wshadow -Werror -I"$work" \
    "$src/ember/tools/bwfm-scan-contract.c" -o "$work/check"
"$work/check"
