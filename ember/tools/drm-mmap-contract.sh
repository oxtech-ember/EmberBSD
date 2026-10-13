#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual-source DRM mmap regression, AI-assisted.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm-mmap.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
file="$src/sys/external/bsd/drm2/drm/drm_cdevsw.c"
awk '/^drm_fop_mmap\(struct file \*fp,/ { print "static int"; take=1 }
    take { print }
    take && /^}/ { found=1; exit }
    END { if (!found) exit 1 }' "$file" > "$work/drm-mmap-body.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/drm-mmap-contract.c" -o "$work/check"
"$work/check"
