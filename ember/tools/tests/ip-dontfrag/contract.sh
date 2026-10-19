#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD IPv4 fragmentation-control regression.
# Compile actual source branches with host shims; no network or device access.
set -eu
[ "$#" = 1 ] || { echo "usage: $0 source-tree" >&2; exit 1; }
src=$1
self=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ip-dontfrag.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
input=$src/sys/netinet/ip_output.c
awk '/^#define[ \t]+IP_DONTFRAG[ \t]/ { print; found++ }
    END { if (found != 1) exit 1 }' "$src/sys/netinet/in.h" > "$work/defines.h"
awk '/^#define[ \t]+IN[6]?P_[A-Z_]+[ \t]+0x/ { print $1, $2, $3 }
    ' "$src/sys/netinet/in_pcb.h" >> "$work/defines.h"
awk '/^#define[ \t]+IN[6]?P_[A-Z_]+[ \t]+0x/ && $2 != "INP_DONTFRAG" {
    print "_Static_assert((INP_DONTFRAG & " $2 ") == 0, \"PCB flag collision\");"
    }' "$src/sys/netinet/in_pcb.h" >> "$work/defines.h"
# Extract the production option macros and the two option-specific cases.
awk '/^#define[ \t]+OPTSET\(bit\)/ { active=1 }
    active { print; if ($0 !~ /\\$/) { active=0; found++ } }
    END { if (found != 1) exit 1 }' "$input" > "$work/options.h"
awk '/^#define[ \t]+OPTBIT\(bit\)/ { print; found++ }
    END { if (found != 1) exit 1 }' "$input" >> "$work/options.h"
awk '/^\t\t\tcase IP_DONTFRAG:/ { count++; active=(count==1) }
    active { print; if ($0 ~ /break;/) { active=0; found++ } }
    END { if (found != 1) exit 1 }' "$input" > "$work/set.inc"
awk '/^\t\t\tcase IP_DONTFRAG:/ { count++; active=(count==2) }
    active { print; if ($0 ~ /break;/) { active=0; found++ } }
    END { if (found != 1) exit 1 }' "$input" > "$work/get.inc"
awk '/^\tif \(sopt->sopt_name == IP_DONTFRAG/ { active=1 }
    active { print; if ($0 ~ /return ENOPROTOOPT;/) { active=0; found++ } }
    END { if (found != 1) exit 1 }' "$input" > "$work/family.inc"
# Header initialization must precede DF application, before routing/loopback.
awk '/^\tif \(\(flags & \(IP_FORWARDING\|IP_RAWOUTPUT\)\)/ { active=1 }
    active { print }
    active && /INP_DONTFRAG/ { df=1 }
    active && df && /^\t}$/ { active=0; found++ }
    END { if (found != 1) exit 1 }' "$input" > "$work/header.inc"
awk '/^\tif \(\(flags & IP_MTUDISC\)/ { active=1 }
    active { print; if ($0 == "\t}") { active=0; found++ } }
    END { if (found != 1) exit 1 }' "$input" > "$work/pmtu.inc"
awk '/^\tif \(ntohs\(ip->ip_off\) & IP_DF\)/ { active=1 }
    active { print; if ($0 == "\t}") { active=0; found++ } }
    END { if (found != 1) exit 1 }' "$input" > "$work/fragment.inc"
cc=${CC:-cc}
"$cc" -std=c11 -Wall -Wextra -Werror -I "$work" \
    "$self/contract.c" -o "$work/contract"
"$work/contract"
