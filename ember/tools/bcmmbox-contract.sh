#!/bin/sh
# Origin: EmberBSD bounded VideoCore mailbox transaction contract, 2026-10-08.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcmmbox-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mbox=${MBOX_SOURCE_DIR:-$src/sys/arch/arm/broadcom}
awk '!/^#include/' "$mbox/bcm2835_mbox.h" > "$work/bcm2835_mbox-types.h"
cp "$mbox/bcm2835_mboxreg.h" "$work/bcm2835_mboxreg.h"
awk '/^static struct bcm2835mbox_softc \*bcm2835mbox_sc;/ { copy = 1 }
    copy { print } END { if (!copy) exit 1 }' \
    "$mbox/bcm2835_mbox.c" > "$work/mbox-under-test.h"
awk '/^(int|void)$/ { copy = 1 } copy { print }
    END { if (!copy) exit 1 }' \
    "$mbox/bcm2835_mbox_subr.c" > "$work/mbox-subr-under-test.h"
# The fixture invokes the extracted production functions with fake bus/DMA
# operations and real host threads. CC/CFLAGS can select sanitizers or a target.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L \
    -DKERNEL_ASSERTIONS=${KERNEL_ASSERTIONS:-0} -Wall -Wextra -Werror \
    ${CFLAGS:-} -pthread -I"$work" "$src/ember/tools/bcmmbox-contract.c" \
    -o "$work/contract"
if [ -n "${MBOX_OUTPUT:-}" ]; then
    cp "$work/contract" "$MBOX_OUTPUT"
fi
if [ "${MBOX_COMPILE_ONLY:-0}" = 1 ]; then
    exit 0
fi
"$work/contract" ${MBOX_CASE:-}
if [ "${MBOX_SKIP_MUTANTS:-0}" = 1 ]; then
    exit 0
fi
cp "$work/mbox-under-test.h" "$work/mbox-good.h"
cp "$work/mbox-subr-under-test.h" "$work/mbox-subr-good.h"
for mutation in status0 no-echo no-quarantine free-timeout rx-deadline \
    wake-deadline no-overflow early-unlock; do
    cp "$work/mbox-subr-good.h" "$work/mbox-subr-under-test.h"
    awk -v mutation="$mutation" '
        /^bcmmbox_read_locked\(/ { in_read = 1 }
        /^bcmmbox_request\(/ { in_request = 1 }
        mutation == "no-echo" {
            changed += sub(/res != map->dm_segs\[0\].ds_addr/, "false")
        }
        mutation == "no-quarantine" {
            changed += sub(/sc->sc_quarantined\[chan\] = true/, "sc->sc_quarantined[chan] = false")
        }
        mutation == "free-timeout" && /sc->sc_retained_map\[chan\] = map;/ { retain = 1 }
        mutation == "free-timeout" && retain && /goto out;/ {
            sub(/goto out;/, "goto not_sent;"); changed++; retain = 0
        }
        mutation == "rx-deadline" && in_read && /mutex_enter\(&sc->sc_intr_lock\);/ {
            print "\tbcmmbox_deadline_init(deadline);"; changed++
            in_read = 0
        }
        mutation == "wake-deadline" && /Recompute from the same deadline/ {
            print "\t\t\tbcmmbox_deadline_init(deadline);"; changed++
        }
        mutation == "no-overflow" {
            changed += sub(/sc->sc_overflow\[chan\] = true/, "sc->sc_overflow[chan] = false")
        }
        mutation == "early-unlock" && in_request && /bcmmbox_deadline_init\(&deadline\);/ {
            print "\tmutex_exit(&sc->sc_chan_lock[chan]);"; changed++
        }
        { print }
        END { if (mutation != "status0" && changed != 1) exit 1 }
    ' "$work/mbox-good.h" > "$work/mbox-under-test.h"
    if [ "$mutation" = status0 ]; then
        awk '{ changed += sub(/BCM2835_MBOX1_STATUS/, "BCM2835_MBOX0_STATUS"); print }
            END { if (changed != 1) exit 1 }' "$work/mbox-subr-good.h" \
            > "$work/mbox-subr-under-test.h"
    fi
    # Every mutant must compile, then fail a behavioral assertion.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -DKERNEL_ASSERTIONS=0 \
        -Wall -Wextra -Werror ${CFLAGS:-} -pthread -I"$work" \
        "$src/ember/tools/bcmmbox-contract.c" -o "$work/mutant"
    if (ulimit -c 0; "$work/mutant") > "$work/mutant.log" 2>&1; then
        echo "surviving mailbox causal mutant: $mutation" >&2
        exit 1
    fi
    echo "VideoCore mailbox causal mutant rejected: $mutation"
done
