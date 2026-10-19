#!/bin/sh
# Origin: EmberBSD native DRM PCI identity for Linux pci_dev drivers, 2026-10-19.
# SPDX-License-Identifier: BSD-2-Clause
# Extract the actual drm_pci_native_identity() and check its attach position.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm-pci-identity.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
pci="$src/sys/external/bsd/drm2/pci/drm_pci.c"
body() {
	awk -v name="$1" '
		prev ~ /^(static )?(void|int)$/ && $0 ~ "^" name "[(]" {
			print prev; copying = 1
		}
		copying { print }
		copying && /^}/ { exit }
		{ prev = $0 }
	' "$pci"
}
body drm_pci_native_identity > "$work/identity-body.h"
grep -q '^drm_pci_native_identity(' "$work/identity-body.h"
# drm_pci_attach() publishes the record unconditionally after its last failure return.
body drm_pci_attach | awk '
	/drm_pci_native_identity\(dev, pdev\);/ {
		calls++; after = 1
		if ($0 != "\tdrm_pci_native_identity(dev, pdev);") bad = 1
		next
	}
	after && /return/ && !/return 0;/ { bad = 1 }
	after && /return 0;/ { ok = 1 }
	END { exit !(calls == 1 && ok && !bad) }
'
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
    -I"$src/sys/external/bsd/drm2/include/drm" \
    "$src/ember/tools/drm-pci-identity-contract.c" -o "$work/contract"
"$work/contract"
