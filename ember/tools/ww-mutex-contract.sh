#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
# Compile the selected real ww_mutex and rbtree sources with a pthread bridge.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
ww_source=${2:-$src/sys/external/bsd/drm2/linux/linux_ww_mutex.c}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ww-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/sys/rbtree.h" > "$work/rbtree-body.h"
sed '/^#include /d' "$src/common/lib/libc/gen/rb.c" > "$work/rb-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/include/linux/ww_mutex.h" > "$work/ww-header-body.h"
sed '/^#include /d' "$ww_source" > "$work/ww-body.h"
"${CC:?set CC to the selected native Ports compiler}" -std=c11 -pthread -D_POSIX_C_SOURCE=200809L \
    -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter ${CFLAGS:-} \
    -I"$work" "$here/ww-mutex-contract.c" -o "$work/test"
"$work/test"
