#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8821C MAC init reference trace generator, AI-assisted.
#
# Regenerate rtw8821c-mac.trace from the pinned FreeBSD rtw88 sources.
# The reference functions run unchanged against a recording register
# model; the trace is the ordered list of their register writes.
# Usage: rtw8822c-mac-trace-gen.sh REFDIR > rtw8821c-mac.trace
# REFDIR holds mac.c, reg.h, pci.c, rtw8821c.c from revision
# a143197801c680b958ab662c88cf086668a1958b of sys/contrib/dev/rtw88.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 REFDIR" >&2; exit 2; }
ref=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8821c-trace.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

check() {
	sum=$(shasum -a 256 "$ref/$1" | cut -d' ' -f1)
	[ "$sum" = "$2" ] || { echo "$1: SHA256 $sum, want $2" >&2; exit 1; }
}
check mac.c ec8a96ef305231f15af22bc94ad5708e0daf3bfb3ff470ec96757913e2be59f6
check rtw8821c.c a51946d4496d8856bbf01010f1786230478b951c4e034b67457a47d4acda414b
check rtw8821c.h 10a311ec1135e887d41bb0977c57375fd9b9a0707e3cda1f5eaa53d20eef97c1
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
	sed -n '/^#define WLAN_SLOT_TIME/,/^\/\* phy status page0 \*\//p' "$ref/rtw8821c.h" | sed '$d'
	sed -n '/^#define REG_INIRTS_RATE_SEL /p' "$ref/rtw8821c.h"
	for f in txdma_queue_mapping rtw_set_trx_fifo_info __priority_queue_cfg \
	    __priority_queue_cfg_legacy priority_queue_cfg init_h2c \
	    rtw_init_trx_cfg rtw_drv_info_cfg; do
		fn mac.c "$f"
	done
	fn rtw8821c.c rtw8821c_mac_init
	fn pci.c rtw_pci_interface_cfg
} > "$work/ref-body.c"

"${CC:?select CC using SDK run-native-check}" -std=gnu11 -w -I"$ref" -I"$tools" -DREF_BODY="\"$work/ref-body.c\"" \
    "$tools/rtw8821c-mac-trace-ref.c" -o "$work/gen"
"$work/gen"
