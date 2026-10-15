#!/bin/sh
# Origin: EmberBSD - compile the A133 PCM core with native kernel build flags.
set -eu
set -f
if [ "$(uname -s)" != NetBSD ] || [ "$#" -ne 2 ]; then
    echo "usage (native NetBSD): $0 SOURCE_ROOT KERNEL_COMPILE_DIRECTORY" >&2
    exit 2
fi
src=$(CDPATH= cd -- "$1" && pwd)
build=$(CDPATH= cd -- "$2" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-pcm-kernel.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cd "$build"
cc_cmd=$(make -v CC)
cpp_flags=$(make -v CPPFLAGS)
c_flags=$(make -v CFLAGS)
# Intentional splitting of the configured compiler and expanded build flags.
$cc_cmd $cpp_flags $c_flags -c \
    "$src/sys/arch/arm/sunxi/sun50i_a133_pcm.c" -o "$tmp/pcm.o"
echo 'A133 PCM native kernel-context compilation passed'
