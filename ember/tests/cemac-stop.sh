#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted actual-source CEMAC lifecycle contract.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
driver=${CEMAC_STOP_SOURCE:-$src/sys/dev/cadence/if_cemac.c}
if [ -n "${CEMAC_STOP_BUILD:-}" ]; then
    work=$CEMAC_STOP_BUILD
    mkdir -p "$work"
else
    work=$(mktemp -d "${TMPDIR:-/tmp}/cemac-stop.XXXXXXXX")
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
cp "$src/sys/dev/cadence/cemacreg.h" "$work/cemacreg.h"
sed '/^#include /d' "$src/sys/dev/cadence/if_cemacvar.h" > "$work/if_cemacvar.h"
: > "$work/cemac-bodies.h"
for name in cemac_gctx cemac_gem_recover_tx cemac_ifstart_locked cemac_statchg cemac_intr cemac_watchdog_check cemac_watchdog_tick \
    cemac_tick cemac_ifstop cemac_ifinit cemac_mediachange cemac_handle_reset_work; do
    awk -v name="$name" '
        $0 ~ "^" name "\\(" { print previous; copying = 1; found = 1 }
        copying { print }
        copying && /^}/ { exit }
        { previous = $0 }
        END { if (!found) exit 1 }
    ' "$driver" >> "$work/cemac-bodies.h"
done
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" "$src/ember/tests/cemac-stop.c" -o "$work/contract"
if [ "${CEMAC_STOP_COMPILE_ONLY:-0}" != 1 ]; then
    "$work/contract" "${CEMAC_STOP_CASE:-all}"
fi
