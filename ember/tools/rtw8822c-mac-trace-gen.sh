#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8822C MAC init reference trace generator, AI-assisted.
#
# Regenerate rtw8822c-mac.trace from the pinned FreeBSD rtw88 sources.
# The reference functions run unchanged against a recording register
# model; the trace is the ordered list of their register writes.
# Usage: rtw8822c-mac-trace-gen.sh REFDIR > rtw8822c-mac.trace
# REFDIR holds mac.c, reg.h, pci.c, rtw8822c.c from revision
# a143197801c680b958ab662c88cf086668a1958b of sys/contrib/dev/rtw88.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 REFDIR" >&2; exit 2; }
ref=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8822c-trace.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

check() {
	sum=$(shasum -a 256 "$ref/$1" | cut -d' ' -f1)
	[ "$sum" = "$2" ] || { echo "$1: SHA256 $sum, want $2" >&2; exit 1; }
}
check mac.c ec8a96ef305231f15af22bc94ad5708e0daf3bfb3ff470ec96757913e2be59f6
check rtw8822c.c 6986e6107da9261b97277d57fcc11daf5dcb0d71571c55b1080bdea1495ce160
check pci.c c84aca85002a049c07f4ecad9394be680b148580a008e01b01e0363266023ee1
check reg.h c8246f9305539347fdb62b0294fe12ea24769cb3ad56c2b0235c4269a0307324

# Print one top-level function, from its signature to the closing brace.
fn() {
	awk -v name="$2" '
	    !on && $0 ~ "^(static )?[a-z_ ]*[ *]" name "\\(" { on = 1 }
	    on { print }
	    on && /^}/ { exit }' "$ref/$1"
}
{
	sed -n '/^#define WLAN_TXQ_RPT_EN/,/^static int rtw8822c_mac_init/p' \
	    "$ref/rtw8822c.c" | sed '$d'
	for f in txdma_queue_mapping rtw_set_trx_fifo_info __priority_queue_cfg \
	    __priority_queue_cfg_legacy priority_queue_cfg init_h2c \
	    rtw_init_trx_cfg rtw_drv_info_cfg; do
		fn mac.c "$f"
	done
	fn rtw8822c.c rtw8822c_mac_init
	fn pci.c rtw_pci_interface_cfg
} > "$work/ref-body.c"

${CC:-cc} -std=gnu11 -w -I"$ref" -I"$tools" -DREF_BODY="\"$work/ref-body.c\"" \
    "$tools/rtw8822c-mac-trace-ref.c" -o "$work/gen"
"$work/gen"
