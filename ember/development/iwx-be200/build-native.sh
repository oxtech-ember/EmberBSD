#!/bin/sh
# Build the work-in-progress iwx(4) module natively on an installed
# EmberBSD 1.0 amd64 system (release c5d396f5698b).
#
# Prerequisites on the target:
#   - /usr/src/sys, /usr/src/common and /usr/share/mk from the same
#     source revision as the running kernel (git archive c5d396f5698b
#     sys common share/mk);
#   - pkgsrc clang (clang-21 and llvm-21), because the release excludes
#     native GCC commands.
#
# Usage: sh build-native.sh <directory with src/ of this proposal>
set -eu
src=${1:?usage: build-native.sh <proposal dir>}/src
cp "$src"/if_iwx.c "$src"/if_iwxreg.h "$src"/if_iwxvar.h \
    "$src"/if_iwx_compat.h /usr/src/sys/dev/pci/
grep -q 'file	dev/pci/if_iwx.c' /usr/src/sys/dev/pci/files.pci ||
    cat "$src"/files.iwx >> /usr/src/sys/dev/pci/files.pci
mkdir -p /usr/src/sys/modules/if_iwx
cp "$src"/modules/if_iwx/Makefile "$src"/modules/if_iwx/if_iwx.ioconf \
    "$src"/modules/if_iwx/ioconf.c \
    /usr/src/sys/modules/if_iwx/
cd /usr/src/sys/modules/if_iwx
rm -f ./*.o ./*.kmod
set +e
make -m /usr/share/mk USETOOLS=no HAVE_LLVM=yes MKLLVM=yes HAVE_GCC=no \
    CC=/usr/pkg/bin/clang NETBSDSRCDIR=/usr/src S=/usr/src/sys \
    MKDEBUG=no MKCTF=no -j8 > /tmp/iwx-build.log 2>&1
rc=$?
set -e
echo "errors: $(grep -c 'error:' /tmp/iwx-build.log) make exit: $rc"
ls -l ./*.kmod 2>/dev/null || true
exit $rc
