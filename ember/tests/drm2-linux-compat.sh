#!/bin/sh
# Host contract for the drm2 Linux compatibility headers that need only
# <linux/types.h>. The kernel-side run is sys/modules/drmkms_linux_test.
# usage: drm2-linux-compat.sh [CC]   (default: $CC, then clang, then cc)
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
root=$(CDPATH= cd -- "$here/../.." && pwd -P)
cc=${1:-${CC:-$(command -v clang || command -v cc)}}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm2-linux-compat.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
flags="-std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror -DDRM2_COMPAT_HOST \
    -I$here/drm2-linux-compat-shim -I$root/sys/external/bsd/drm2/include \
    -I$root/sys/external/bsd/drm2/linux"
# shellcheck disable=SC2086
$cc $flags -o "$work/ok" "$here/drm2-linux-compat.c" -lm
"$work/ok"
# Negative control: a failing check must fail the runner.
# shellcheck disable=SC2086
$cc $flags -DDRM2_COMPAT_SELFTEST_FAIL -o "$work/bad" "$here/drm2-linux-compat.c" -lm
if "$work/bad" >"$work/bad.log" 2>&1; then
    echo 'negative control unexpectedly passed' >&2
    exit 1
fi
grep -q 'FAIL' "$work/bad.log" || { cat "$work/bad.log" >&2; exit 1; }
echo 'drm2-linux-compat host contract: PASS'
