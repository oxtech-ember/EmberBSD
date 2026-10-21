#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Cross-build only. No target installation or execution.
set -eu
[ "$#" = 2 ] || { echo 'usage: source-directory new-output-directory' >&2; exit 2; }
: "${CC:?Select the common EmberBSD GCC16 compiler}"
: "${SYSROOT:?Select the recorded common EmberBSD sysroot}"
for path in "$1" "$2" "$CC" "$SYSROOT"; do
    case "$path" in /*) ;; *) echo 'absolute paths required' >&2; exit 1;; esac
    case "$path" in *[[:space:]]*) echo 'whitespace in paths unsupported' >&2; exit 1;; esac
done
src=$(CDPATH= cd -- "$1" && pwd -P)
out=$2
[ ! -e "$out" ] || { echo 'new output directory required' >&2; exit 1; }
[ -z "$(git -C "$src" status --porcelain)" ] || { echo 'commit sources before building' >&2; exit 1; }
case "$("$CC" -dumpfullversion)" in 16.*) ;; *) echo 'GCC16 required' >&2; exit 1;; esac
case "$("$CC" -dumpmachine)" in aarch64*netbsd*) ;; *) echo 'EmberBSD AArch64 target required' >&2; exit 1;; esac
mkdir "$out"
git -C "$src" rev-parse HEAD > "$out/source-commit.txt"
"$CC" --version > "$out/compiler.txt"
set -- "$CC" "--sysroot=$SYSROOT" -O2 -g -static -std=c11 -D_NETBSD_SOURCE \
    -Wall -Wextra -Werror -MD -MF "$out/snapshot.d" \
    -I"$src/sys/external/bsd/drm2/dist/include/uapi/drm" \
    "$src/ember/tools/bcmv3d-kms-snapshot.c" -o "$out/bcmv3d-kms-snapshot"
printf '%s\n' "$@" > "$out/compile.argv"
"$@" > "$out/build.log" 2>&1 || { cat "$out/build.log" >&2; exit 1; }
hash() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi
}
for p in "$src/ember/tools/bcmv3d-kms-snapshot.c" \
    "$SYSROOT/usr/lib/libc.a" "$out/bcmv3d-kms-snapshot"; do hash "$p"; done > "$out/inputs-output.sha256"
printf '%s\n' 'Build only. Record used headers/CRT provenance separately; this does not qualify a complete SDK.' > "$out/scope.txt"
