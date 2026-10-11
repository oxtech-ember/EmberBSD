#!/bin/sh
# 802.11r FT key hierarchy against FIPS, RFC 4231 and cross-generated vectors.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-ft.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${AICWF_FT_CFLAGS:-} "$src/ember/tools/aicwf-ft.c" -o "$work/ft" && \
    "$work/ft"
