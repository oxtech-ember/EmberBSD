#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# Runs in an EmberBSD guest (amd64): loads the vgem test driver and runs the
# client of ember/tests/drm2-vgem-client.c against it.
#	drm2-vgem-guest.sh <vgem.kmod> <drm2-vgem-client> [client args]
# Build:
#	nbmake-amd64 -C sys/modules/vgem dependall                 5.6 dma_resv calls
#	nbmake-amd64 -C sys/modules/vgem VGEM_NEWAPI=1 dependall   6.18 dma_resv calls
# and cross-compile the client with the target compiler, -Isys/external/bsd/drm2/dist/include/uapi.
# The module cannot be unloaded: reboot the guest to run the other variant.
# Prints the client output; exit status 0 if the module loaded and every check passed.

set -u
[ $# -ge 2 ] || { echo "usage: $0 <vgem.kmod> <client> [client args]" >&2; exit 2; }
kmod=$1; client=$2; shift 2
modload "$kmod" || { echo "FAIL modload $kmod" >&2; exit 1; }
"$client" "$@"
st=$?
dmesg | grep -E "vgem|panic" | tail -5
exit $st
