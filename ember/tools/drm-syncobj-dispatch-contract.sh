#!/bin/sh
# Origin: EmberBSD native DRM wait dispatcher regression, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm-sync-dispatch.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
file="$src/sys/external/bsd/drm2/dist/drm/drm_ioctl.c"
awk '/DRM_IOCTL_DEF\(DRM_IOCTL_SYNCOBJ_(WAIT|TIMELINE_WAIT),/ { print; getline; print }' \
    "$file" > "$work/wait-descriptors.h"
awk '/^drm_ioctl\(struct file \*fp,/ { native=1 }
    native && /drm_ioctl_enter\(dev\);/ { take=1 }
    take { print } take && /drm_ioctl_exit\(dev\);/ { exit }' \
    "$file" > "$work/dispatch-body.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/drm-syncobj-dispatch-contract.c" -o "$work/check"
"$work/check"
