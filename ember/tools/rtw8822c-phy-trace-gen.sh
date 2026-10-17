#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8822C PHY and channel reference trace generator, AI-assisted.
#
# Regenerate rtw8822c-phy.trace from the pinned FreeBSD rtw88 sources:
# rtw8822c_phy_set_param around the table load, and rtw8822c_set_channel
# for 20 MHz channels, run unchanged against the recording register model.
# Usage: rtw8822c-phy-trace-gen.sh REFDIR > rtw8822c-phy.trace
# REFDIR holds rtw8822c.c/.h, phy.c/.h, mac.c/.h, main.h, bf.c/.h, reg.h
# from revision a143197801c680b958ab662c88cf086668a1958b.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 REFDIR" >&2; exit 2; }
ref=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8822c-phy-trace.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

check() {
	sum=$(shasum -a 256 "$ref/$1" | cut -d' ' -f1)
	[ "$sum" = "$2" ] || { echo "$1: SHA256 $sum, want $2" >&2; exit 1; }
}
check rtw8822c.c 6986e6107da9261b97277d57fcc11daf5dcb0d71571c55b1080bdea1495ce160
check mac.c ec8a96ef305231f15af22bc94ad5708e0daf3bfb3ff470ec96757913e2be59f6
check reg.h c8246f9305539347fdb62b0294fe12ea24769cb3ad56c2b0235c4269a0307324
check bf.h f2a98c7fb254b9c467ab1ccf1d0cf6c438d15c8845d47ca91fa8205d06f69c0a

python3 - "$ref" "$work/ref-body.c" <<'EOF'
import re, sys
ref, out = sys.argv[1], sys.argv[2]

def definition(path, name):
    lines = open(f"{ref}/{path}").read().split("\n")
    head = re.compile(r"^(static\s+)?(inline\s+)?[A-Za-z_][\w\s\*]*\b" +
                      re.escape(name) + r"\(")
    for i, line in enumerate(lines):
        if not head.match(line):
            continue
        j = i
        while "{" not in lines[j] and not lines[j].rstrip().endswith(";"):
            j += 1
        if lines[j].rstrip().endswith(";"):
            continue                    # prototype
        k = j
        while lines[k] != "}":
            k += 1
        return "\n".join(lines[i:k + 1]) + "\n"
    raise SystemExit(f"{path}: {name} not found")

order = [
    ("phy.c", "rtw_phy_read_rf"), ("phy.c", "rtw_phy_write_rf_reg_sipi"),
    ("phy.c", "rtw_phy_write_rf_reg"), ("phy.c", "rtw_phy_write_rf_reg_mix"),
    ("phy.c", "rtw_phy_set_edcca_th"), ("phy.c", "rtw_phy_adaptivity_set_mode"),
    ("phy.c", "rtw_phy_cck_pd_init"),
    ("rtw8822c.c", "rtw8822c_adaptivity_init"), ("rtw8822c.c", "rtw8822c_cfo_init"),
    ("phy.c", "rtw_phy_adaptivity_init"), ("phy.c", "rtw_phy_cfo_init"),
    ("phy.c", "rtw_phy_tx_path_div_init"), ("phy.c", "rtw_phy_init"),
    ("rtw8822c.c", "rtw8822c_header_file_init"), ("rtw8822c.c", "rtw8822c_bb_reset"),
    ("rtw8822c.c", "rtw8822c_config_cck_rx_path"),
    ("rtw8822c.c", "rtw8822c_config_ofdm_rx_path"),
    ("rtw8822c.c", "rtw8822c_config_rx_path"),
    ("rtw8822c.c", "rtw8822c_config_cck_tx_path"),
    ("rtw8822c.c", "rtw8822c_config_ofdm_tx_path"),
    ("rtw8822c.c", "rtw8822c_config_tx_path"), ("rtw8822c.c", "rtw8822c_toggle_igi"),
    ("rtw8822c.c", "rtw8822c_config_trx_mode"), ("bf.c", "rtw_bf_phy_init"),
    ("rtw8822c.c", "rtw8822c_rf_x2_check"), ("rtw8822c.c", "rtw8822c_thermal_trim"),
    ("rtw8822c.c", "rtw8822c_set_power_trim"), ("rtw8822c.c", "rtw8822c_power_trim"),
    ("rtw8822c.c", "rtw8822c_pa_bias"), ("rtw8822c.c", "rtw8822c_rf_init"),
    ("rtw8822c.c", "rtw8822c_phy_set_param"), ("rtw8822c.c", "rtw8822c_rstb_3wire"),
    ("rtw8822c.c", "rtw8822c_set_channel_bb"), ("mac.c", "rtw_set_channel_mac"),
    ("rtw8822c.c", "rtw8822c_set_channel_rf"), ("rtw8822c.c", "rtw8822c_set_channel"),
]
with open(out, "w") as fp:
    for path, name in order:
        fp.write(definition(path, name) + "\n")

# Macro-only copies of headers that also pull in Linux types.
import os
for hdr in ("rtw8822c.h", "bf.h", "phy.h", "main.h", "mac.h"):
    src = open(f"{ref}/{hdr}").read().split("\n")
    keep, i = [], 0
    while i < len(src):
        if src[i].startswith("#define") and not src[i].startswith("#define __"):
            keep.append(src[i])
            while src[i].rstrip().endswith("\\"):
                i += 1
                keep.append(src[i])
        i += 1
    base = os.path.dirname(out)
    open(f"{base}/defs-{hdr}", "w").write("\n".join(keep) + "\n")
EOF

${CC:-cc} -std=gnu11 -w -I"$work" -I"$ref" -I"$tools" \
    -DREF_BODY="\"$work/ref-body.c\"" \
    "$tools/rtw8822c-phy-trace-ref.c" -o "$work/gen"
"$work/gen"
