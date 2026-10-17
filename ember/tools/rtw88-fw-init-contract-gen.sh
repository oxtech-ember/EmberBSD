#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: unchanged FreeBSD packet-H2C producer consumer, AI-assisted.
# Usage: rtw88-fw-init-contract-gen.sh REFDIR > rtw88-fw-init-contract.vectors
set -eu
[ "$#" -eq 1 ] || exit 2
ref=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-fw-init-gen.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
check() {
    sum=$(shasum -a 256 "$ref/$1" | cut -d' ' -f1)
    [ "$sum" = "$2" ] || { echo "$1: incorrect SHA256" >&2; exit 1; }
}
check fw.c 495de8bb993985d19af97c8f0da5c1505c0a37812d50e8dbd339b85485ca8526
check fw.h 45a1bf5213eec95cd450a54bc50caa5652c959dd2b644684ec7447b278f6e701
check pci.h 49129551f28352e1edd567199261893a84f4d451d3fec3af04909085b41c219d
x="python3 $tools/rtw88-ref-extract.py $ref"
$x "$work/defs.h" fw.h:defines fw.h:type:rtw_fw_rf_type \
    pci.h:type:rtw_pci_tx_buffer_desc
$x "$work/body.c" fw.h:fn:rtw_h2c_pkt_set_header \
    fw.c:fn:rtw_fw_send_h2c_packet fw.c:fn:rtw_fw_send_general_info \
    fw.c:fn:rtw_fw_send_phydm_info
${CC:-cc} -std=gnu11 -w ${RTW88_TEST_CFLAGS:-} -I"$work" \
    "$tools/rtw88-fw-init-contract-ref.c" -o "$work/gen"
"$work/gen"
