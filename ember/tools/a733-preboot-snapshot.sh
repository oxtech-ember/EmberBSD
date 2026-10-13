#!/bin/sh
# Origin: EmberBSD; AI-assisted A733 read-only preboot snapshot builder.
# SPDX-License-Identifier: BSD-2-Clause
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 normal-boot.cmd snapshot-boot.cmd" >&2
	exit 1
fi
input=$1
output=$2
if [ ! -f "$input" ] || [ ! -r "$input" ]; then
	echo "input must be a readable normal boot command file" >&2
	exit 1
fi
if [ -e "$output" ] || [ -L "$output" ]; then
	echo "refusing to replace existing output: $output" >&2
	exit 1
fi

# Limit this tool to the board's existing boot flow. It does not install
# files, wrap a U-Boot image, change the normal script, or write hardware.
awk '
    /ember,a733-preboot-|BEGIN A733 PREBOOT SNAPSHOT/ { duplicate = 1 }
    /^[ \t]*booti([ \t]|$)/ { boots++ }
    $0 == "booti 0x44000000 - ${fdt_addr_r}" { target++; bootline = NR }
    $0 == "fdt addr ${fdt_addr_r}" { address++; addrline = NR }
    $0 == "fdt resize 8192" { space++; spaceline = NR }
    END { exit duplicate || boots != 1 || target != 1 ||
        address != 1 || space != 1 || addrline >= spaceline ||
        spaceline >= bootline }
' "$input" || {
	echo "input must contain one normal A733 booti/FDT flow, without a snapshot" >&2
	exit 1
}
if [ "$(tail -c 1 "$input" | wc -l | tr -d ' ')" != 1 ]; then
	echo "input must end with a newline" >&2
	exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/a733-preboot.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/snapshot" <<'SNAPSHOT'
# BEGIN A733 PREBOOT SNAPSHOT v1
# Read-only MMIO; FDT properties and temporary variables live in RAM.
# The eight cells after each base are PWPR PMER PWSR DISR MISR PWCR IMR ISR.
# setexpr ignores env_set_hex failure; checked sentinels prevent stale values.
if fdt set /chosen ember,a733-preboot-status started && fdt set /chosen ember,a733-preboot-version "<0x1>"; then
    if setenv aps_gate unavailable && setexpr.l aps_gate *0x070101ac + 0 && test -n "${aps_gate}" && test "${aps_gate}" != unavailable; then
        if fdt set /chosen ember,a733-preboot-gate "<0x070101ac 0x${aps_gate}>"; then
            if setenv aps_enabled unavailable && setexpr.l aps_enabled ${aps_gate} \& 1 && test -n "${aps_enabled}" && test "${aps_enabled}" != unavailable; then
                if test "${aps_enabled}" = "1"; then
                    if setenv aps_t0 unavailable && setexpr.l aps_t0 *0x07065000 + 0 && test -n "${aps_t0}" && test "${aps_t0}" != unavailable && setenv aps_t1 unavailable && setexpr.l aps_t1 *0x07065004 + 0 && test -n "${aps_t1}" && test "${aps_t1}" != unavailable && setenv aps_t2 unavailable && setexpr.l aps_t2 *0x07065008 + 0 && test -n "${aps_t2}" && test "${aps_t2}" != unavailable && setenv aps_t3 unavailable && setexpr.l aps_t3 *0x07065010 + 0 && test -n "${aps_t3}" && test "${aps_t3}" != unavailable; then
                        if setenv aps_t4 unavailable && setexpr.l aps_t4 *0x07065014 + 0 && test -n "${aps_t4}" && test "${aps_t4}" != unavailable && setenv aps_t5 unavailable && setexpr.l aps_t5 *0x07065020 + 0 && test -n "${aps_t5}" && test "${aps_t5}" != unavailable && setenv aps_t6 unavailable && setexpr.l aps_t6 *0x07065030 + 0 && test -n "${aps_t6}" && test "${aps_t6}" != unavailable && setenv aps_t7 unavailable && setexpr.l aps_t7 *0x07065038 + 0 && test -n "${aps_t7}" && test "${aps_t7}" != unavailable; then
                            if setenv aps_c0 unavailable && setexpr.l aps_c0 *0x07066000 + 0 && test -n "${aps_c0}" && test "${aps_c0}" != unavailable && setenv aps_c1 unavailable && setexpr.l aps_c1 *0x07066004 + 0 && test -n "${aps_c1}" && test "${aps_c1}" != unavailable && setenv aps_c2 unavailable && setexpr.l aps_c2 *0x07066008 + 0 && test -n "${aps_c2}" && test "${aps_c2}" != unavailable && setenv aps_c3 unavailable && setexpr.l aps_c3 *0x07066010 + 0 && test -n "${aps_c3}" && test "${aps_c3}" != unavailable; then
                                if setenv aps_c4 unavailable && setexpr.l aps_c4 *0x07066014 + 0 && test -n "${aps_c4}" && test "${aps_c4}" != unavailable && setenv aps_c5 unavailable && setexpr.l aps_c5 *0x07066020 + 0 && test -n "${aps_c5}" && test "${aps_c5}" != unavailable && setenv aps_c6 unavailable && setexpr.l aps_c6 *0x07066030 + 0 && test -n "${aps_c6}" && test "${aps_c6}" != unavailable && setenv aps_c7 unavailable && setexpr.l aps_c7 *0x07066038 + 0 && test -n "${aps_c7}" && test "${aps_c7}" != unavailable; then
                                    if fdt set /chosen ember,a733-preboot-top "<0x07065000 0x${aps_t0} 0x${aps_t1} 0x${aps_t2} 0x${aps_t3} 0x${aps_t4} 0x${aps_t5} 0x${aps_t6} 0x${aps_t7}>" && fdt set /chosen ember,a733-preboot-core "<0x07066000 0x${aps_c0} 0x${aps_c1} 0x${aps_c2} 0x${aps_c3} 0x${aps_c4} 0x${aps_c5} 0x${aps_c6} 0x${aps_c7}>"; then
                                        fdt set /chosen ember,a733-preboot-status complete
                                    fi
                                fi
                            fi
                        fi
                    fi
                else
                    fdt set /chosen ember,a733-preboot-status ppu-clock-gated
                fi
            fi
        fi
    fi
fi
# END A733 PREBOOT SNAPSHOT v1
SNAPSHOT

awk -v snippet="$work/snapshot" '
    $0 == "booti 0x44000000 - ${fdt_addr_r}" {
        while ((getline line < snippet) > 0) print line
        close(snippet)
    }
    { print }
' "$input" > "$work/result"

# noclobber also covers an output created after the earlier existence check.
(set -C; cat "$work/result" > "$output")
