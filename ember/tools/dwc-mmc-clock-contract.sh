#!/bin/sh
# Exercise the clock divider used by the DesignWare Mobile Storage Host.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
: "${CC:?set CC to the selected host compiler}"
work=$(mktemp -d "${TMPDIR:-/tmp}/dwc-mmc-clock.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM
"$CC" ${CPPFLAGS:-} ${CFLAGS:-} -std=c99 -Wall -Wextra -Werror \
    -iquote "$src/sys" "$src/ember/tools/dwc-mmc-clock-contract.c" \
    ${LDFLAGS:-} -o "$work/check"
"$work/check"
