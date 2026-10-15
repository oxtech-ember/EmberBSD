#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted Darwin host-tool flag and configure regression.
set -eu
[ "$#" -eq 3 ] || {
    echo 'Usage: darwin-host-tools.sh NBMAKE_WRAPPER FULL_SOURCE HOST_GCC16' >&2
    exit 2
}
make=$1 src=$2 gcc=$3
[ "$(uname -s)" = Darwin ] || { echo 'This check requires a macOS SDK.' >&2; exit 2; }
[ -x "$make" ] && [ -x "$gcc" ] && [ "$("$gcc" -dumpfullversion)" = 16.2.0 ] || exit 2
sdk=$(xcrun --sdk macosx --show-sdk-path)
clang=$(xcrun --find clang)
work=$(mktemp -d /tmp/ember-darwin-host-tools.XXXXXXXX)
trap 'rm -rf "$work"' EXIT
trap 'exit 130' INT
trap 'exit 143' HUP TERM
failures=0
for directory in compat libmopcopy libctf elftoolchain/libelf elftoolchain/libdwarf; do
    for compiler in gcc clang; do
        case "$compiler" in gcc) cc=$gcc;; clang) cc=$clang;; esac
        flags=$("$make" -C "$src/tools/$directory" -m "$src/share/mk" \
            NETBSDSRCDIR="$src" HOST_CC="$cc" -V '${HOST_CPPFLAGS}')
        case " $flags " in
            *' -no-cpp-precomp '*) present=yes;;
            *) present=no;;
        esac
        expected=no
        [ "$compiler" != clang ] || expected=yes
        if [ "$present" != "$expected" ]; then
            echo "FAIL: $directory selects unsupported Darwin flags for $compiler" >&2
            failures=$((failures + 1))
        fi
        flags=$("$make" -C "$src/tools/$directory" -m "$src/share/mk" \
            NETBSDSRCDIR="$src" HOST_CC="$cc" BUILD_OSTYPE=Linux -V '${HOST_CPPFLAGS}')
        case " $flags " in *' -no-cpp-precomp '*)
            echo "FAIL: $directory leaks Darwin flags to a non-Darwin host" >&2
            failures=$((failures + 1));;
        esac
    done
done

# Execute the actual host-mkdep configure rule in a private object/source copy.
# No target tools are built or installed. All SDK flags must reach configure.
mkdir "$work/mkdep"
cp "$src/tools/host-mkdep/Makefile" "$src/tools/host-mkdep/configure" \
    "$src/tools/host-mkdep/host-mkdep.in" "$work/mkdep/"
if (
    unset CFLAGS CPPFLAGS LDFLAGS SDKROOT
    "$make" -C "$work/mkdep" -m "$src/share/mk" NETBSDSRCDIR="$src" \
        MAKEOBJDIR="$work/mkdep" HOST_CC="$gcc" \
        HOST_CFLAGS='-O2 -std=gnu17 -DEMBER_HOST_CFLAGS=1' \
        HOST_CPPFLAGS="-isysroot $sdk -DEMBER_HOST_CPPFLAGS=1" \
        HOST_LDFLAGS="-isysroot $sdk -Wl,-headerpad_max_install_names" host-mkdep
) > "$work/configure.log" 2>&1; then
    for flag in -DEMBER_HOST_CFLAGS=1 -DEMBER_HOST_CPPFLAGS=1 -Wl,-headerpad_max_install_names; do
        if ! grep -Fq -- "$flag" "$work/mkdep/config.log"; then
            echo "FAIL: host-mkdep configure lost $flag" >&2
            failures=$((failures + 1))
        fi
    done
    test -x "$work/mkdep/host-mkdep"
else
    cat "$work/configure.log" >&2
    echo 'FAIL: host-mkdep configure cannot use the selected GCC16 and SDK' >&2
    failures=$((failures + 1))
fi
mkdir "$work/mandoc"
if (
    unset CFLAGS CPPFLAGS LDFLAGS SDKROOT
    "$make" -C "$src/tools/mandoc" -m "$src/share/mk" NETBSDSRCDIR="$src" \
        MAKEOBJDIR="$work/mandoc" HOST_CC="$gcc" \
        HOST_CFLAGS='-O2 -std=gnu17 -DEMBER_MANDOC_CFLAGS=1' \
        HOST_CPPFLAGS="-isysroot $sdk -DEMBER_MANDOC_CPPFLAGS=1" \
        HOST_LDFLAGS="-isysroot $sdk -Wl,-headerpad_max_install_names" config.h
) > "$work/mandoc-configure.log" 2>&1; then
    for flag in -DEMBER_MANDOC_CFLAGS=1 -DEMBER_MANDOC_CPPFLAGS=1 -Wl,-headerpad_max_install_names; do
        if ! grep -Fq -- "$flag" "$work/mandoc/config.log"; then
            echo "FAIL: mandoc configure lost $flag" >&2
            failures=$((failures + 1))
        fi
    done
    test -s "$work/mandoc/config.h"
    grep -Fq 'result of running nanosleep: yes' "$work/mandoc/config.log"
else
    cat "$work/mandoc-configure.log" >&2
    echo 'FAIL: mandoc configure cannot use the selected GCC16 and SDK' >&2
    failures=$((failures + 1))
fi
[ "$failures" -eq 0 ] || { echo "FAIL: $failures host-tool regressions" >&2; exit 1; }
echo 'PASS: GCC16/Clang Darwin flags, non-Darwin isolation and real host-mkdep/mandoc configure with SDK'
