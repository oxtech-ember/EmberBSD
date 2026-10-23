#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual VHT codec and AICWF consumer analysis.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TEST_OBJDIR:-${TMPDIR:-/tmp}}/net80211-vht-analysis.XXXXXXXX")
if [ "${VHT_IES_ANALYSIS_KEEP:-0}" = 1 ]; then
    printf 'Analysis sources: %s\n' "$work"
else
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
export TEST_OBJDIR="$work"
cc=${CC:-gcc-16}
flags='-O0 -g -gdwarf-4 -fanalyzer'
TMPDIR="$work" VHT_IES_TEST_KEEP=1 VHT_IES_TEST_CFLAGS="$flags" \
    sh "$src/ember/tools/net80211-vht-ies-contract.sh"
TMPDIR="$work" AICWF_VHT_KEEP=1 AICWF_VHT_CFLAGS="$flags" \
    sh "$src/ember/tests/aicwf-vht.sh"
TMPDIR="$work" AICWF_STA_KEEP=1 AICWF_STA_CFLAGS="$flags" \
    sh "$src/ember/tests/aicwf-sta.sh"
codec=$(find "$work" -name 'net80211-vht-ies.*' -type d)
aic=$(find "$work" -name 'aicwf-vht.*' -type d)
sta=$(find "$work" -name 'aicwf-sta.*' -type d)
# Use compiler-preprocessed concrete host headers and macros, not Cppcheck's
# guessed system include search. Keep the .i files for reproducible findings.
for spec in "codec:$src/sys/net80211/ieee80211_vht_ies.c:$codec" \
    "contract:$src/ember/tools/net80211-vht-ies-contract.c:$codec" \
    "aicwf:$src/ember/tools/aicwf-vht-test.c:$aic" \
    "sta:$sta/check.c:$sta"; do
    name=${spec%%:*}; rest=${spec#*:}
    source=${rest%%:*}; includes=${rest#*:}
    "$cc" -std=c11 -include stdbool.h -include stdint.h \
        -I"$includes" -I"$src/sys/dev/sdmmc" -E "$source" > "$work/$name.i"
    set --
    if [ "$name" = sta ]; then
        # Suppress only verified modeled log sinks. Keep source expressions
        # and macro bodies exact; new or changed sinks require a fresh audit.
        awk '$0 == "#define IEEE80211_DPRINTF(...) ((void)0)" {a++}
            $0 == "#define wpa_printf(...) ((void)0)" {b++}
            END {if (a != 1 || b != 1) exit 1}' "$sta/check.c"
        driver_line=$(awk '
            /^[[:space:]]*IEEE80211_DPRINTF\(&sc->sc_ic, IEEE80211_MSG_STATE,/ {
                print NR; n++
            } END {if (n != 1) exit 1}' "$sta/driver.h")
        client_lines=$(awk '
            /^[[:space:]]*wpa_printf\(MSG_DEBUG, "BSD: host-managed (BSS transitions|neighbor reports) available"\);/ {
                print NR; n++
            } END {if (n != 2) exit 1}' "$sta/client.h")
        set -- "--suppress=constStatement:$sta/driver.h:$driver_line"
        for line in $client_lines; do
            set -- "$@" "--suppress=constStatement:$sta/client.h:$line"
        done
        printf 'Verified inherited fixture log sinks: %s %s\n' \
            "$driver_line" "$client_lines"
    fi
    ${CPPCHECK:-cppcheck} "$@" --enable=warning,performance,portability \
        --error-exitcode=1 --language=c --check-level=exhaustive "$work/$name.i"
    ${CLANG:-clang} --analyze -std=c11 -include stdbool.h -include stdint.h \
        -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
        -I"$includes" -I"$src/sys/dev/sdmmc" "$source" \
        -o "$work/$name.plist"
done
