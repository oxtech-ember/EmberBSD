#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Host test of the efidrm shadow logic.  EFIDRM_TEST_LEGACY=1 builds the
# pre-review logic (the regressions must fail).
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cc=${CC:-cc}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
extra=
[ -z "${EFIDRM_TEST_LEGACY:-}" ] || extra=-DEFIDRM_TEST_LEGACY
"$cc" -std=c23 -Wall -Wextra -Werror ${CFLAGS:-} $extra \
    "$here/t_efidrm_shadow.c" -o "$out/t"
"$out/t"
