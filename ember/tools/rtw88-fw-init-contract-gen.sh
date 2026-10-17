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
check pci.c c84aca85002a049c07f4ecad9394be680b148580a008e01b01e0363266023ee1
check tx.c d598fe4d658a9c03de9f0468f0bd7f07b50232f7732fd0f49ed6367221aa3978
check tx.h 5f0c1dc2c8e9dbcdfece4e34c264bd24731ab6a3ff28011f9ddb09cb695aaa46
check main.h bab159780b164959b5bd981d24544507558ed816334d49fd1a31d4d17a69285e
x="python3 $tools/rtw88-ref-extract.py $ref"
$x "$work/defs.h" fw.h:defines fw.h:type:rtw_fw_rf_type \
    pci.h:type:rtw_pci_tx_buffer_desc pci.h:type:rtw_pci_tx_data \
    main.h:type:rtw_tx_pkt_info main.h:type:rtw_tx_queue_type \
    tx.h:type:rtw_tx_desc tx.h:type:rtw_tx_desc_queue_select tx.h:defines
$x "$work/body.c" pci.h:fn:avail_desc pci.h:fn:get_tx_buffer_desc \
    pci.c:fn:rtw_pci_get_tx_qsel tx.c:fn:rtw_tx_fill_tx_desc \
    tx.c:fn:rtw_tx_write_data_h2c_get pci.c:fn:rtw_pci_tx_write_data \
    pci.c:fn:rtw_pci_write_data_h2c fw.h:fn:rtw_h2c_pkt_set_header \
    fw.c:fn:rtw_fw_send_h2c_packet fw.c:fn:rtw_fw_send_general_info \
    fw.c:fn:rtw_fw_send_phydm_info
${CC:-cc} -std=gnu11 -w ${RTW88_TEST_CFLAGS:-} -I"$work" \
    "$tools/rtw88-fw-init-contract-ref.c" -o "$work/gen"
"$work/gen"
