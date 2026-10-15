#!/bin/sh
# Origin: EmberBSD LLVM host module header regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" = 1 ] || { echo 'Usage: llvm-host-modules.sh obj/tools/llvm/config' >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
config=$1
[ -f "$config/include/llvm/Config/llvm-config.h" ] || exit 1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/header.cc" <<'CPP'
#include "llvm/ADT/STLForwardCompat.h"
llvm::in_place_index_t<4> ember_llvm_index;
CPP
c++ -std=c++14 -fmodules -fcxx-modules \
    -Xclang -fmodules-local-submodule-visibility \
    -fmodules-cache-path="$work/modules" -fsyntax-only \
    -fmodule-map-file="$config/include/llvm/module.modulemap" \
    -fmodule-map-file="$root/external/apache2/llvm/dist/llvm/include/llvm-c/module.modulemap" \
    -I "$root/external/apache2/llvm/dist/llvm/include" \
    -I "$config/include" -I "$config/tools/clang/include" "$work/header.cc"
echo 'LLVM host modules: standalone forward-compatibility header passes'
