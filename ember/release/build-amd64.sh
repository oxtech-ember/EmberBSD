#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Build the complete reduced installer from one clean source commit.
set -eu

if [ "$#" -ne 2 ]; then
	echo 'Usage: sh ember/release/build-amd64.sh FULL-COMMIT /absolute/unused-output' >&2
	exit 1
fi
revision=$1
out=$2
case "$revision" in
	*[!0-9a-f]*|'') echo 'Use a full hexadecimal source commit.' >&2; exit 1 ;;
esac
[ "${#revision}" -eq 40 ] || { echo 'Use a full source commit.' >&2; exit 1; }
case "$out" in
	/*) ;;
	*) echo 'Use an absolute output path.' >&2; exit 1 ;;
esac
case "$out" in
	*[[:space:]]*) echo 'The build output path must not contain whitespace.' >&2; exit 1 ;;
esac
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
git -C "$root" cat-file -e "$revision^{commit}"
timestamp=$(git -C "$root" show -s --format=%ct "$revision")
tag=build-$revision-amd64-no-x11
jobs=${EMBER_RELEASE_JOBS:-4}
case "$jobs" in
	*[!0-9]*|''|0) echo 'Use a positive worker count.' >&2; exit 1 ;;
esac
mkdir "$out"
git -C "$root" archive -o "$out/source.tar" "$revision"
mkdir "$out/source"
tar -xf "$out/source.tar" -C "$out/source"
cd "$out/source"
export MAKECONF=/dev/null
unset EXTERNAL_TOOLCHAIN MAKEFLAGS MFLAGS MAKEOBJDIR MAKEOBJDIRPREFIX NETBSDOBJDIR NETBSDOBJROOT
printf 'Build log: %s/build.log\n' "$out"
sh ./build.sh -U -m amd64 -j"$jobs" \
	-O "$out/obj" -T "$out/tools" -D "$out/dest" -R "$out/release" \
	-V MKX11=no -V MKRUMP=no -V MKGCCCMDS=no \
	-V MKREPRO=yes -V "MKREPRO_TIMESTAMP=$timestamp" \
	-V KERNEL_SETS=GENERIC -V BUILD_KERNELS=INSTALL \
	-V EMBER_VERSION=1.0 -V "EMBER_BUILD_ID=$revision" -V "BUILDINFO=EmberBSD:$revision" \
	-V SYSINST_HTTP_HOST=github.com -V SYSINST_FTP_HOST=github.com \
	-V "SYSINST_FTP_DIR=oxtech-ember/EmberBSD/releases/download/$tag" \
	-V SYSINST_FTP_SET_DIR_BIN= \
	tools release install-image iso-image > "$out/build.log" 2>&1
printf '%s\n' "$revision" > "$out/build-success"
printf 'Completed pinned build: %s\n' "$revision"
